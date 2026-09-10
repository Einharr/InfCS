#include "runtime.h"
#include "theme.h"
#include "binary.h"
#include "ui.h"
#include "tick.h"
#include "hooks.h"
#include "../input/pad.h"
#include "../crossbar/crossbar.h"
#include "../cursor/stack.h"
#include "../windows/charselect.h"
#include "../windows/desktop.h"
#include "../windows/inventory.h"
#include "../windows/commands.h"
#include "../windows/popup.h"
#include "../windows/gamebar.h"
#include "../windows/keyboard.h"
#include "../world/targeting.h"
#include "../world/targetring.h"
#include "../world/foes.h"
#include <chrono>
#include <memory>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace cp {

namespace {
void (*g_trace)(const char*) = nullptr;
}

void setTrace(void (*sink)(const char*)) { g_trace = sink; }

void tracef(const char* fmt, ...)
{
    if (!g_trace) return;
    char buf[512];
    va_list a; va_start(a, fmt); std::vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    g_trace(buf);
}

namespace {

struct BinaryBinds : crossbar::BindSource {
    bool label(const char* cmd, std::u16string& out) override { return bin::bindingLabel(cmd, out); }
};

struct Runtime {
    Config cfg; Status st;
    BinaryInput sink; Ui ui{sink};
    input::Pad pad;
    crossbar::Crossbar bar; BinaryBinds binds;
    cursor::Cursor cur{ui}; cursor::HintBar hints; cursor::Stack stack;
    world::Targeting targeting; world::TargetRing ring;
    // Two rows either side of the modifier glyphs: top is tap (cycles), bottom hold (rings).
    cursor::HintBar modeBarL, modeBarR, holdBarL, holdBarR;
    cursor::HintBar bindPrompt;              // the command binding window: screen center, above the browser window
    windows::Commands* commands = nullptr;   // owned by the desktop; kept here to drive the binding window
    windows::Desktop* desktop = nullptr;      // owned by the stack, kept here only for the trace
    // Character select comes before the world: no toolbar, no window stack, so it is
    // neither a desktop window nor in the stack.
    std::unique_ptr<windows::CharSelect> charsel;
    windows::GameBar* gamebar = nullptr;      // also owned by the stack; kept here to toggle it on Options
    windows::Keyboard* keyb = nullptr;
    int chordArmed_ = -1; float chordWait_ = 0.f;   // the deferred L3/R3 slot; the stack owns the keyboard, we open it on L3+R3 and on an input field
    cursor::Context ctx{cur, ui, pad, hints};
    std::chrono::steady_clock::time_point last; bool haveLast = false;
    float attachRetry = 0; int lastMod = -1; unsigned bindRefresh = 0;
    unsigned padEvents = 0, padCalls = 0;      // how many pad events actually arrived
    float previewFanTimer = 0; bool previewAssignDone = false, previewClearDone = false;
    void* joyDevice = nullptr;
    // Wanted scheme and how long to keep retrying. Cannot switch the moment the pad is
    // recognized: resetFromType needs a loaded ground map and a real scene type, and the
    // client polls the pad on character select already - REFUSED there, 10 Sep 2026.
    const char* wantScheme = nullptr; float schemeRetry = 0;

    bool attachToolbar()
    {
        tracef("attachToolbar: looking for %s", cfg.toolbarPath.c_str());
        NodePtr tb = nodeByPath(cfg.toolbarPath.c_str());
        if (!tb) { tracef("attachToolbar: no toolbar"); return false; }
        // Theme colors before our first widget: every clone from here on is painted by
        // palette entry, and the entry has to resolve already. The crossbar attaches
        // before the cursor, so the scan belongs here.
        // The HUD, not the root: over the root this is thousands of nodes on the very
        // frame the toolbar appears and the world is still building - the client died
        // there, in theme::harvest over a half-built node (10 Sep 2026). Our widgets all
        // live in the HUD anyway.
        theme::scan(nodeByPath(cfg.hudPath.c_str()));
        // Catcher before bar.attach: the mediator pointer only comes out of the client's
        // own getToolbarItem, i.e. on a pane repopulation, and bar.attach does the first
        // one itself. Hooked after it we had no mediator until the next pane change, and
        // writes into another pane did nothing (8 Sep 2026: clearLayer dead).
        const bool selfHook = toolbarself::install(cfg.hooks);
        tracef("attachToolbar: toolbar %p", tb->handle());
        NodePtr hud = nodeByPath(cfg.hudPath.c_str());
        tracef("attachToolbar: hud %p", hud ? hud->handle() : nullptr);
        // The template has to be OUR markup: SourceResource on a UIImage is resolved by
        // the loader, so a clone keeps whatever texture it came with. cpGlyphTemplate is
        // already on our atlas - only SourceRect changes.
        NodePtr img = hud ? hud->byPath("cpDebug.cpGlyphTemplate") : NodePtr();
        if (!img) img = findFirst(hud ? hud : tb, T_Image, 8);
        tracef("attachToolbar: image template %p (%s)", img ? img->handle() : nullptr,
               hud && hud->byPath("cpDebug.cpGlyphTemplate") ? "our atlas" : "not ours - there will be no glyphs");
        NodePtr txt = tb->byPath("cornerTL.textPane"); if (!txt) txt = findFirst(tb, T_Text, 6);
        tracef("attachToolbar: text template %p", txt ? txt->handle() : nullptr);
        NodePtr frameTpl = tb->child("cornerTL");
        tracef("attachToolbar: frame template %p", frameTpl ? frameTpl->handle() : nullptr);
        if (!bar.attach(tb, img, txt, frameTpl)) { tracef("attachToolbar: bar.attach refused"); return false; }
        tracef("attachToolbar: bar.attach ok");
        if (cfg.bindBadges) bar.setBindSource(&binds);
        // Pack ignores DoNotPackChildren, so we hook it when QoL hands us the API;
        // without it only the per-tick guard holds the geometry.
        packguard::setGuarded(bar.volume() ? bar.volume()->handle() : nullptr);
        st.packHooked = packguard::install(cfg.hooks);
        tracef("attachToolbar: packHooked=%d, mediator catcher=%d, mediator=%p",
               (int)st.packHooked, (int)selfHook, bin::toolbar::self());
        return true;
    }

    // Prints the path of every visible page whose name contains the substring. Cheaper
    // than deducing paths from the sources - the inventory was not where they say.
    void findWindow(const char* needle)
    {
        NodePtr root = rootNode(); if (!root) return;
        struct Item { NodePtr n; std::string path; };
        std::vector<Item> q; q.push_back(Item{root, std::string()});
        int found = 0;
        for (size_t i = 0; i < q.size() && i < 4096 && found < 8; ++i) {
            NodePtr n = q[i].n;
            for (const NodePtr& k : n->children()) {
                if (!k->willDraw()) continue;
                std::string path = q[i].path.empty() ? k->name() : q[i].path + "." + k->name();
                if (std::strstr(k->name().c_str(), needle) && k->isA(T_Page)) {
                    UIRect r = k->worldRect();
                    tracef("search '%s': %s  %d,%d..%d,%d children=%d",
                           needle, path.c_str(), r.left, r.top, r.right, r.bottom, (int)k->children().size());
                    ++found;
                }
                if (path.size() < 120 && k->isA(T_Page)) q.push_back(Item{k, path});
            }
        }
        if (!found) tracef("search '%s': no visible pages by that name", needle);
    }

    void previewShow()
    {
        NodePtr hud = nodeByPath(cfg.hudPath.c_str()); if (!hud) return;
        NodePtr tb = nodeByPath(cfg.toolbarPath.c_str()); if (!tb) return;
        if (!hud->willDraw()) hud->setVisible(true);
        if (!tb->willDraw()) tb->setVisible(true);
    }

    // Bumper hints go under the bar, either side of the modifier glyphs, next to the
    // held L2/R2 the crossbar already draws. Triggers and bumpers in one place.
    void updateModeBar(bool active)
    {
        if (!active || !cfg.targetModeBar) {
            modeBarL.clear(); modeBarR.clear(); holdBarL.clear(); holdBarR.clear(); return;
        }
        const bool ringOpen = ring.isOpen();
        const bool foeRing = ringOpen && targeting.ringNow() == world::RING_FOES;
        modeBarL.set(world::modeHints(targeting.set(), ringOpen, false, foeRing));
        modeBarR.set(world::modeHints(targeting.set(), ringOpen, true, foeRing));
        // With a ring open the second row would only repeat select/cancel.
        holdBarL.set(ringOpen ? std::vector<cursor::Hint>() : world::holdHints(targeting.set(), false));
        holdBarR.set(ringOpen ? std::vector<cursor::Hint>() : world::holdHints(targeting.set(), true));
        NodePtr ov = nodeByPath(cfg.hudPath.c_str());
        if (!ov || !bar.attached()) return;
        const UIPoint c = bar.modGlyphWorld(), o = ov->worldLocation();
        const int cx = c.x - o.x, cy = c.y - o.y - modeBarL.glyphSize() / 2;
        // Leave a corridor for the two modifier glyphs spread out from the center.
        const int gap = bar.metrics().modGlyph + 10;
        const int line2 = cy + modeBarL.glyphSize() + 2;
        modeBarL.moveTo(cx - gap - modeBarL.contentWidth(), cy);
        modeBarR.moveTo(cx + gap, cy);
        holdBarL.moveTo(cx - gap - holdBarL.contentWidth(), line2);
        holdBarR.moveTo(cx + gap, line2);
    }

    // Debug window: the rows the module owns; the rest come from the markup scripts.
    // A few times a second is plenty - setLocalText parses the string in the client.
    NodePtr dbgRow(const char* name)
    {
        NodePtr hud = nodeByPath(cfg.hudPath.c_str());
        if (!hud) return NodePtr();
        std::string path = "cpDebug."; path += name;
        return hud->byPath(path.c_str());
    }

    void dbgSet(const char* name, const char* fmt, ...)
    {
        NodePtr n = dbgRow(name); if (!n) return;
        char buf[160];
        va_list a2; va_start(a2, fmt); std::vsnprintf(buf, sizeof buf, fmt, a2); va_end(a2);
        std::u16string t;
        for (const char* p = buf; *p; ++p) t.push_back(static_cast<char16_t>(*p));
        n->setLocalText(t.c_str());
    }

    void updateDebug()
    {
        static const char* const SET[] = {"ALL", "FOE", "ALLY"};
        const world::Set set = targeting.set();
        dbgSet("lSet", "%s   (L2 foes / R2 allies)", SET[set < 3 ? set : 0]);

        const char* holdL = "-";
        switch (world::ringKind(false, set)) {
        case world::RING_PARTY:   holdL = "LB party"; break;
        case world::RING_OBJECTS: holdL = "LB objects"; break;
        default: break;
        }
        dbgSet("lHold", "%s   |   RB targets", holdL);

        if (ring.isOpen()) {
            static const char* const KIND[] = {"-", "party", "foes", "objects", "all"};
            const world::Ring k = targeting.ringNow();
            dbgSet("lRing", "%s, %d items, sector %d, r%d", KIND[k < 5 ? k : 0],
                   static_cast<int>(ring.itemCount()), ring.sector(), ring.radius());
        } else {
            dbgSet("lRing", "closed");
        }

        void* player = bin::game::playerCreature();
        void* target = player ? bin::game::lookAtTarget(player) : nullptr;
        if (target) {
            std::u16string name;
            bin::game::localizedName(bin::game::asClientObject(target), name);
            char narrow[64] = {0};
            size_t k = 0;
            for (; k < name.size() && k < sizeof narrow - 1; ++k)
                narrow[k] = (name[k] >= 32 && name[k] < 127) ? static_cast<char>(name[k]) : '?';
            const bin::game::Threat th = bin::game::threatOf(target);
            static const char* const T[] = {"peaceful", "attackable", "hostile"};
            dbgSet("lTarget", "%s  [%s]  pvp %04X", k ? narrow : "(unnamed)",
                   T[th < 3 ? th : 0], bin::game::pvpFlags(target));
        } else {
            dbgSet("lTarget", "none");
        }
        static const char* const CTX[] = {"off", "peaceful - faces: radial / use / clear", "hostile - faces stay slots"};
        dbgSet("lCtx", "%s", CTX[bar.context() < 3 ? bar.context() : 0]);
        // Zero palette entries means the whole UI is on fallback colors.
        dbgSet("lTheme", "%d colours read  (line1 %s, back1 %s)", theme::known(),
               theme::color("line1"), theme::color("back1"));
        if (keyb && keyb->isOpen())
            dbgSet("lMode", "keyboard %d,%d%s%s", keyb->row(), keyb->col(),
                   keyb->symbols() ? " symbols" : "", keyb->caps() ? " caps" : (keyb->shift() ? " shift" : ""));

        const input::PadState& ps = pad.state();
        const input::Stick rs = pad.rightStick();
        dbgSet("lPad", "btn %08X  stick %+.2f %+.2f  %s%s", ps.buttons, rs.x, rs.y,
               pad.uiMode() ? "ui " : "", pad.suppressedButtons() ? "faces-held" : "");
    }

    bool attachCursor()
    {
        NodePtr hud = nodeByPath(cfg.hudPath.c_str()); if (!hud) return false;
        if (theme::known() == 0) theme::scan(hud);          // if the crossbar is off; over the HUD, not the root
        // Our markup only, again: a clone of any other image brings its own texture
        // along and the hints would show it (same trap as the crossbar).
        NodePtr img = hud->byPath("cpDebug.cpGlyphTemplate");
        NodePtr frame = hud->byPath("Toolbar.cornerTL");
        tracef("attachCursor: hud %p, glyph %p (%s), frame %p",
               hud->handle(), img ? img->handle() : nullptr, img ? "our atlas" : "MISSING", frame ? frame->handle() : nullptr);
        // The cursor frame's bars are sprites off our atlas: a tinted page smears.
        cur.attachVisuals(hud, img, frame);
        NodePtr txt = hud->byPath("Toolbar.cornerTL.textPane"); if (!txt) txt = findFirst(hud, T_Text, 8);
        UISize hs = hud->size();
        hints.attach(hud, img, txt, cfg.hintX, cfg.hintY < 0 ? hs.y + cfg.hintY : cfg.hintY);
        // Ring and mode bar share an overlay, so they need distinct names: the client
        // collapses two same-named clones under one parent into one.
        modeBarL.setPrefix("cpTgtL"); modeBarR.setPrefix("cpTgtR");
        holdBarL.setPrefix("cpHldL"); holdBarR.setPrefix("cpHldR");
        holdBarL.attach(hud, img, txt, 0, 0);
        holdBarR.attach(hud, img, txt, 0, 0); bindPrompt.setPrefix("cpBind");
        bindPrompt.attach(hud, img, txt, 0, 0);
        // The binding prompt is a window, so it takes the cursor frame's backdrop.
        bindPrompt.setBackdrop(frame, 16);
        modeBarL.attach(hud, img, txt, 0, 0);
        modeBarR.attach(hud, img, txt, 0, 0);
        ring.attach(hud, frame, txt, img);
        tracef("attachCursor: bumper targeting %s, threshold %.2f s, ring %s",
               cfg.targeting ? "on" : "off", cfg.targetHold,
               cfg.targetRingLeft ? "on LB" : "off");
        NodePtr root = rootNode();
        // The desktop drives ANY client window; profiles only refine single ones. Hence
        // the stack holds the desktop plus the context popups, which live in UIManager.
        std::unique_ptr<windows::Desktop> desk(new windows::Desktop(root));
        if (!cfg.uiSkip.empty()) desk->addSkip(cfg.uiSkip);
        desk->addProfile(std::unique_ptr<cursor::Window>(new windows::Inventory(root, "GroundHUD.Inventory")));
        std::unique_ptr<windows::Commands> cmds(new windows::Commands(root, &bar));
        commands = cmds.get();
        desk->addProfile(std::unique_ptr<cursor::Window>(cmds.release()));
        for (const std::string& l : desk->survey()) tracef("attachCursor: %s", l.c_str());
        desktop = desk.get();
        stack.add(std::unique_ptr<cursor::Window>(desk.release()));
        stack.add(std::unique_ptr<cursor::Window>(new windows::Popup([]() -> NodePtr { return contextPage(); })));
        // Game menu radial: ButtonBar is in the HUD, not on the desktop, and always
        // visible - being 'open' is our own state, toggled by Options.
        std::string barPath = cfg.hudPath + ".ButtonBar";
        std::unique_ptr<windows::GameBar> gb(new windows::GameBar(
            [barPath]() -> NodePtr { return nodeByPath(barPath.c_str()); }));
        gamebar = gb.get();
        gb->attachDecor(hud, img, txt);      // the same styling as the target ring
        std::unique_ptr<windows::Keyboard> kb(new windows::Keyboard);
        kb->attach(hud, frame, txt, img);
        keyb = kb.get();
        stack.add(std::unique_ptr<cursor::Window>(kb.release()));
        tracef("attachCursor: menu panel %s %s", barPath.c_str(), gb->page() ? "found" : "MISSING");
        stack.add(std::unique_ptr<cursor::Window>(gb.release()));
        tracef("attachCursor: windows in the stack: %d", (int)stack.size());
        return true;
    }

    void frame()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float dt = haveLast ? std::chrono::duration<float>(now - last).count() : 0.016f;
        last = now; haveLast = true; if (dt > 0.25f) dt = 0.25f;
        ++st.frames;

        // Once a second until attached, both attachments on one timer. Per frame it used
        // to be ~60 pointless walks of the login screen's tree; the toolbar only shows up
        // on entering the world anyway.
        bool wantBar = cfg.crossbar && !st.crossbarAttached;
        // Cursor only after the toolbar: it is the reliable sign the HUD is finished.
        // Without that it reached into a half-built GroundHUD on character select, which
        // is why it used to be switched off in the ini.
        bool wantCur = cfg.cursor && !st.cursorActive && st.crossbarAttached;
        if (wantBar || wantCur) {
            attachRetry -= dt;
            if (attachRetry <= 0) {
                attachRetry = 1.0f;
                if (wantBar) st.crossbarAttached = attachToolbar();
                if (wantCur) st.cursorActive = attachCursor();
            }
        }

        // Character select has no crossbar and no UI mode, and its layout needs no
        // cursor, so it runs separately - and only while the crossbar is unattached, or
        // in game we would walk the root every frame for nothing.
        if (cfg.charSelect && !st.crossbarAttached) {
            if (!charsel || !charsel->hasRoot()) {
                NodePtr r = rootNode();
                if (r) charsel.reset(new windows::CharSelect(r, ui));
            }
            st.charSelectActive = charsel && charsel->update(pad, dt);
        } else if (charsel) {
            // Entered the world: pull our widgets off the select page ourselves. The
            // page is markup, so it stays alive - just hidden - and they would hang
            // there for the rest of the session.
            charsel->detach();
            st.charSelectActive = false;
        }

        // Palette refresh every ~5 s, about 790 HUD nodes. Widgets already drawn follow
        // the theme themselves (paint gives them Pal*); it was the name CACHE that went
        // stale, so anything built after a switch came out in the old colors.
        if (st.crossbarAttached && (st.frames % 300) == 0)
            theme::scan(nodeByPath(cfg.hudPath.c_str()));
        // Every ~5 s: how often the client asked the device, how many events came back.
        // Tells 'the pad is silent' from 'the client is not parsing them'.
        if ((st.frames % 60) == 0) {
            const input::PadState& ps = pad.state();
            tracef("pad: polls=%u events=%u buttons=%08X POV=%d ui=%d | X=%d Y=%d Z=%d RX=%d RY=%d RZ=%d",
                   padCalls, padEvents, ps.buttons, (int)ps.pov, (int)pad.uiMode(),
                   ps.axis[0], ps.axis[1], ps.axis[2], ps.axis[3], ps.axis[4], ps.axis[5]);
            padCalls = 0; padEvents = 0;
        }
        // Once a second until it takes: the axis ranges do not come out of the
        // DirectInput wrapper straight away, and a fixed frame number kept missing.
        if ((st.frames % 60) == 0) detectPadAxes();
        int mod = static_cast<int>(input::activeMod(pad));
        if (st.crossbarAttached) {
            if (cfg.previewAtLogin && (st.frames % 30) == 0) previewShow();
            // Rig preview: -2 loops the clusters, 1.5 s each; otherwise hold this one.
            if (cfg.previewFan == -2) {
                previewFanTimer += dt;
                bar.forceReveal(static_cast<int>(previewFanTimer / 1.5f) % gen::CLUSTER_COUNT);
            } else if (cfg.previewFan >= 0) {
                bar.forceReveal(cfg.previewFan);
            }
            if (cfg.previewFanPane >= 0) bar.forceFanPane(cfg.previewFanPane);
            // One-off rig actions, once the hook has caught the mediator.
            if (cfg.previewAssign >= 0 && !previewAssignDone && bin::toolbar::self())
                previewAssignDone = bar.copyMainToLayer(cfg.previewAssign);
            if (cfg.previewClear >= 0 && !previewClearDone && bin::toolbar::self())
                previewClearDone = bar.clearLayer(cfg.previewClear);
            // A rebuilt toolbar takes the cells with it: re-attach on the next pass.
            if (bar.update(dt, mod, bin::dragActive()) < 0 || !bar.attached()) {
                st.crossbarAttached = false; attachRetry = 0.5f;
                tracef("frame: the crossbar detached, re-attaching");
            }
            if (++bindRefresh % 300 == 0) bar.refreshBindings();     // about every 5 s: bindings changed in Options
        }
        // Bumper targeting. With a UI window open the bumpers are its own, so such a
        // frame only closes a ring that was already up.
        if (cfg.targeting && st.crossbarAttached) {
            // No overlay, nowhere to draw: a hold has to stay a plain tap.
            world::Targeting::Config tc = targeting.config();
            const bool canRing = ring.attached();
            tc.hold = cfg.targetHold;
            tc.ringLeft = cfg.targetRingLeft && canRing;
            tc.ringRight = cfg.targetRingRight && canRing;
            targeting.setConfig(tc);

            const bool active = !stack.uiMode();
            const world::Decision d = targeting.update(pad, active);
            // Cancel puts back the pre-ring target: hover may have selected already.
            if (d.cancelRing) ring.cancel(ui);
            // Which ring: LB group, R2+LB peaceful things nearby, RB targets.
            if (d.openRing != world::RING_NONE) ring.setRimColor(world::ringColor(d.openRing));
            switch (d.openRing) {
            // Empty rings still open, with the reason in the middle - otherwise a hold
            // into nothing looks like a breakage.
            case world::RING_PARTY:   ring.open(world::partyItems(), u"Party", u"No group"); break;
            case world::RING_FOES:    ring.open(world::foeItems(), u"Targets", u"No targets in view"); break;
            case world::RING_OBJECTS: ring.open(world::objectItems(), u"Objects", u"Nothing nearby"); break;
            case world::RING_ALL:     ring.open(world::allItems(), u"Everything", u"Nothing in view"); break;
            default: break;
            }
            if (d.applyRing) ring.apply(ui);
            if (d.cycle) tracef("targeting: %s (%s)", d.cycle, ui.action(d.cycle) ? "accepted" : "NOT accepted");
            // LB+RB: nearest target, selected through the same setLookAtTarget a left
            // click and a ring selection go through.
            if (d.nearest) {
                void* obj = world::nearestTarget(targeting.set());
                void* me = bin::game::playerCreature();
                const bool took = obj && me && bin::game::setLookAtTarget(me, obj);
                tracef("targeting: nearest -> %s", took ? "selected" : "nothing suitable");
            }
            ring.update(pad, dt, ui);

            // Context mode: with a peaceful target the right cross's faces mean actions,
            // not slots - so they are cut out of the buffer or the client fires the slot
            // as well.
            int ctx = crossbar::Crossbar::CTX_OFF;
            if (cfg.targetContext && active && !ring.isOpen()) {
                switch (bin::game::targetKind()) {
                case bin::game::TARGET_PEACEFUL: ctx = crossbar::Crossbar::CTX_PEACEFUL; break;
                case bin::game::TARGET_HOSTILE:  ctx = crossbar::Crossbar::CTX_HOSTILE; break;
                default: break;
                }
            }
            if (ctx != bar.context()) {
                bar.setContext(ctx);
                static const char* const W[] = {"off", "peaceful target, faces for actions", "hostile target, backdrop red"};
                tracef("context: %s", W[ctx]);
            }
            // Only on a peaceful target: in combat the faces are the player's abilities.
            const bool faces = ctx == crossbar::Crossbar::CTX_PEACEFUL;
            uint32_t suppress = faces ? ((1u << gen::B_SQUARE) | (1u << gen::B_CROSS) | (1u << gen::B_CIRCLE)) : 0u;
            // L3/R3 are slots 08/09 and also the keyboard chord: passed through as is,
            // every open would fire two abilities. We hold them back and run the slot
            // ourselves if the second stick never came.
            if (cfg.keyboard) suppress |= (1u << gen::B_L3) | (1u << gen::B_R3);
            pad.suppressButtons(suppress);
            if (faces) {
                if (pad.pressed(gen::B_SQUARE)) tracef("context: radial (%s)", ui.action("radialMenu") ? "ok" : "NO");
                else if (pad.pressed(gen::B_CROSS)) tracef("context: interact (%s)", ui.action("defaultAction") ? "ok" : "NO");
                else if (pad.pressed(gen::B_CIRCLE)) tracef("context: untarget (%s)", ui.action("untarget") ? "ok" : "NO");
            }
            // The right stick turns the sectors: mute the camera, leave walking alone.
            pad.suppressCamera(ring.isOpen());
            updateModeBar(active);
            if (cfg.debugWindow && (st.frames % 15) == 0) updateDebug();
        }
        // Scheme switch only once the world is built (crossbar attached), retried each
        // second until the client takes it.
        if (wantScheme && st.crossbarAttached) {
            schemeRetry -= dt;
            if (schemeRetry <= 0) {
                schemeRetry = 1.0f;
                if (bin::scheme::reset(wantScheme)) wantScheme = nullptr;
            }
        }
        if (st.cursorActive) {
            // Options toggles the game menu radial, caught here and not in the window:
            // closed, the radial has no UI mode and the stack hands buttons to nobody.
            // Same reason B_OPTIONS is not in GameBar - both would fire on close and
            // reopen the ring. With L1 held the button stays with the client (pane
            // change). One stick is a crossbar slot, both are the keyboard, so we wait
            // out the chord window before deciding.
            if (cfg.keyboard && keyb) {
                for (int i = 0; i < 2; ++i) {
                    const gen::Button b = i ? gen::B_R3 : gen::B_L3;
                    if (pad.pressed(b) && chordArmed_ < 0) { chordArmed_ = i; chordWait_ = cfg.keyboardChord; }
                }
                if (chordArmed_ >= 0) {
                    chordWait_ -= dt;
                    const gen::Button armed = chordArmed_ ? gen::B_R3 : gen::B_L3;
                    if (chordWait_ <= 0.f || !pad.down(armed)) {
                        static const char* const SLOT[2] = {"toolbarSlot08", "toolbarSlot09"};
                        tracef("stick %s alone: slot %s", chordArmed_ ? "R3" : "L3", SLOT[chordArmed_]);
                        ui.action(SLOT[chordArmed_]);
                        chordArmed_ = -1;
                    }
                }
            }
            // Keyboard, caught here for the same reason: closed, it has no focus and
            // nobody would hand it a button.
            if (cfg.keyboard && keyb) {
                const bool l3r3 = (pad.pressed(gen::B_L3) && pad.down(gen::B_R3))
                               || (pad.pressed(gen::B_R3) && pad.down(gen::B_L3));
                const cursor::NodeInfo* n = cur.current();
                const bool onField = n && n->node && n->kind == cursor::NK_Textbox;
                if (l3r3) {
                    chordArmed_ = -1;             // the chord happened: do not fire the slot
                    if (keyb->isOpen()) keyb->close(&ui);
                    else if (onField) keyb->open(n->node, false);
                    else {
                        // No field of our own: chat, which the client opens and focuses.
                        tracef("keyboard: opening chat (%s)", ui.action("startChat") ? "ok" : "NO");
                        keyb->open(NodePtr(), false);
                    }
                } else if (!keyb->isOpen() && onField && pad.pressed(gen::B_CROSS)) {
                    // Cross on a text field opens the keyboard too; the press reaches it
                    // this same frame and it swallows it.
                    keyb->open(n->node, true);
                }
            }
            // While typing Options is the keyboard's Enter, or one press would do three
            // things: ring, Escape, send.
            if (gamebar && pad.pressed(gen::B_OPTIONS) && !pad.down(gen::B_L1)
                && !(keyb && keyb->isOpen())) {
                // A ring over an open window reads as a mess, so close the window the
                // way Escape does.
                cursor::Window* was = stack.focused();
                const bool opened = gamebar->toggle();
                if (opened && was && was != static_cast<cursor::Window*>(gamebar)) {
                    tracef("menu radial: closing %s", was->id());
                    ui.escape();
                }
                tracef("menu radial: %s", opened ? "opened" : "closed");
            }
            stack.update(ctx);
            // Every ~10 s: focus, node count, frame position. Without it 'no frame' is
            // indistinguishable from 'window not recognized' and from 'zero nodes'.
            if ((st.frames % 600) == 0) {
                cursor::Window* w = stack.focused();
                NodePtr ring = cur.focusNode();
                UIPoint rl = ring ? ring->location() : UIPoint{0, 0};
                UISize rs = ring ? ring->size() : UISize{0, 0};
                tracef("cursor: window=%s nodes=%d cur=%d frame=%p %d,%d %dx%d visible=%d ui=%d",
                       w ? w->id() : "-", (int)cur.nodeCount(), cur.currentIndex(),
                       ring ? ring->handle() : nullptr, rl.x, rl.y, rs.x, rs.y,
                       (int)(ring && ring->willDraw()), (int)pad.uiMode());
                // Nothing found: dump what the desktop sees and why those are not
                // windows. Cheaper than deducing a page path from the sources.
                if (!w && desktop) for (const std::string& l : desktop->survey()) tracef("%s", l.c_str());
            // The breakdown by kind: otherwise you cannot tell whether the scanner
            // picks up the window's buttons or copes with the other list layouts.
            if (w) {
                int byKind[9] = {0};
                std::string names; int shown = 0;
                for (const cursor::NodeInfo& ni : cur.nodes()) {
                    if (ni.kind >= 0 && ni.kind < 9) ++byKind[ni.kind];
                    if (shown < 10 && ni.kind != cursor::NK_Cell) { if (!names.empty()) names += ','; names += ni.name; ++shown; }
                }
                tracef("nodes: cells=%d buttons=%d lists=%d checkboxes=%d other=%d | non-cells: %s",
                       byKind[cursor::NK_Cell], byKind[cursor::NK_Button], byKind[cursor::NK_List],
                       byKind[cursor::NK_Checkbox], byKind[cursor::NK_Generic], names.c_str());
            }
            }
            stack.dispatch(ctx, dt);
            // Do not compare focus against a profile: the stack holds the desktop and
            // profiles only refine it, so the desktop is always the focused one on record
            // ('window=desktop:CommandBrowser/commands' in the trace). Ask the profile.
            if (commands && commands->binding()) {
                bindPrompt.set(windows::Commands::promptHints());
                NodePtr hud = nodeByPath(cfg.hudPath.c_str());
                if (hud) {
                    UISize hs = hud->size();
                    bindPrompt.moveTo((hs.x - bindPrompt.contentWidth()) / 2, hs.y / 2 - bindPrompt.glyphSize());
                }
                // The browser is our sibling under the HUD and the client raises it when
                // focused, so we raise ourselves every frame while binding.
                bindPrompt.raise();
            } else bindPrompt.clear();
            if (cursor::Window* w = stack.focused()) {
                w->onTick(ctx, dt);
                // The hint bar rides under the window: the legend belongs to it, and
                // that is where it gets read.
                NodePtr pg = w->page();
                NodePtr ov = nodeByPath(cfg.hudPath.c_str());
                if (pg && ov) {
                    UIRect r = pg->worldRect(); UIPoint o = ov->worldLocation();
                    const int cx = (r.left + r.right) / 2 - o.x - hints.width() / 2;
                    hints.moveTo(cx, r.bottom - o.y + 6);
                }
            }
            cur.tick(dt); hints.tick(dt);
        }
        Tick::get().run(dt);
        pad.endFrame(dt);
    }
};

Runtime* g = nullptr;

} // namespace

bool install(const Config& cfg)
{
    uninstall();
    g = new Runtime; g->cfg = cfg;
    std::string err = bin::verify();
    if (!err.empty()) { g->st.error = "ABI: " + err; g->st.abiOk = false; return false; }
    g->st.abiOk = true;
    g->pad.setProfile(cfg.profile.c_str());
    crossbar::Glyph::setFamily(cfg.profile.c_str());   // before recognition, whatever the ini says
    g->pad.setTriggerThresholds(cfg.triggerOn, cfg.triggerOff, cfg.triggerRestHigh);
    if (cfg.triggerAxisL >= 0 && cfg.triggerAxisR >= 0)
        g->pad.setTriggerAxes(cfg.triggerAxisL, cfg.triggerAxisR);
    g->st.installed = true;
    return true;
}

void uninstall()
{
    if (!g) return;
    packguard::uninstall(g->cfg.hooks); packguard::setGuarded(nullptr);
    toolbarself::uninstall(g->cfg.hooks);
    if (g->st.crossbarAttached) g->bar.detach();
    if (g->st.cursorActive) { g->cur.detachVisuals(); g->hints.detach(); }
    if (g->charsel) g->charsel->detach();
    delete g; g = nullptr;
}

void onFrame() { if (g && g->st.installed) g->frame(); }

void onDeviceData(void* device, void* buf, uint32_t& count, uint32_t capacity, bool peek)
{
    if (!g || !g->st.installed) return;
    if (!g->joyDevice) g->joyDevice = device;
    if (device != g->joyDevice) return;
    ++g->padCalls; g->padEvents += count;
    g->pad.feed(static_cast<input::DiEvent*>(buf), count, capacity, peek);
}

void onDeviceLost(void* device) { if (g && device == g->joyDevice) g->pad.forget(); }

void onDeviceAxisRange(int ofs, int32_t lo, int32_t hi)
{
    if (g && g->st.installed) g->pad.setAxisRange(ofs, lo, hi);
}

// Legacy DirectInput fallback; native HID selects its known canonical layout.
void detectPadAxes()
{
    if (!g || !g->st.installed || !g->pad.triggerAxesAuto()) return;
    if (!g->pad.autoDetectTriggerAxes()) return;
    tracef("pad: trigger axes determined - L=%d R=%d (%s)",
           g->pad.triggerAxis(0), g->pad.triggerAxis(1),
           g->pad.triggersShareAxis() ? "shared axis, rest at center" : "own axis each, rest at the edge");
}

bool onKey(int scancode, bool down) { return g && g->st.installed && g->pad.feedKey(scancode, down); }

const Status& status() { static Status none; return g ? g->st : none; }

} // namespace cp

namespace cp { void onSonyHidInput() {
    if (!g) return;
    g->pad.forget(); g->pad.setProfile("ds4");
    crossbar::Glyph::setFamily("ds4");
    g->pad.setTriggerAxes(input::DI_RX, input::DI_RY);
    tracef("pad: native Sony - ds4 layout, PlayStation glyphs");
    if (std::strcmp(bin::scheme::current(), "swg") != 0) { g->wantScheme = "swg"; g->schemeRetry = 0; }
} }

namespace cp {
void onXInputTriggers(uint8_t l, uint8_t r)
{
    if (!g || !g->st.installed) return;
    g->pad.setExternalTriggers(l / 255.f, r / 255.f);
}
void onXInputLost() { if (g && g->st.installed) g->pad.clearExternalTriggers(); }
}

namespace cp { void onSteamPad() {
    if (!g) return;
    // Trigger axes left unset: Steam merges them, which is what the auto-detector is
    // for.
    g->pad.forget(); g->pad.setProfile("steam");
    crossbar::Glyph::setFamily("steam");
    tracef("pad: Steam Input - steam layout, Xbox glyphs");
    // Our profile changed, the client's input map did not: the player picks that in
    // Options > Controls and it is read once at startup. Left on 'SWG', the faces and
    // L3/R3 drift apart from the crossbar. So we make the same call Options makes - but
    // only if the scheme is not ours already, because reset re-reads the file and drops
    // the player's rebindings in it.
    if (std::strcmp(bin::scheme::current(), "swg2") != 0) { g->wantScheme = "swg2"; g->schemeRetry = 0; }
} }
