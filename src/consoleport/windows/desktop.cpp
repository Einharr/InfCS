#include "desktop.h"
#include "../crossbar/crossbar.h"
#include "../abi/props.h"
#include "../core/runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

namespace {

// Desktop furniture: pages that are always on view and are not windows. Taken off the
// client markup - the static children of GroundHUD and HudSpace plus the counter
// mediators, which live on the desktop but never leave the screen (expMon, waypointMon
// and the rest from SwgCuiMediatorFactorySetup). Without this list the desktop would
// hold the pad for good: the counters have a close box too.
const char* const FURNITURE[] = {
    // furniture shared by both HUDs
    "glowFocused", "corner_mfd_L", "corner_mfd_R", "SystemMessage", "SystemMessageNoob",
    "reticleImage", "CombatState", "AllTargets", "SelectionBoxPage", "MFDStatus", "Target",
    "incap", "ButtonBar", "group", "ChatWindow", "Toolbar", "CombatQueue", "radar", "AttribMod",
    // space
    "energyLevel", "missileCount", "radarGauge", "offScreenIndicator", "missileLockOnYou",
    "buttons", "Scroll", "FlyAway", "SpaceTarget", "MissileLock", "GroupStatus", "Damage",
    "shipReticle", "ShipTargetInfo", "ShipSelfInfo",
    // counter mediators: windows by construction, furniture by intent
    "QuestHelper", "expMon", "waypointMon", "netStatus", "dpsMeter", "KillMeter",
    "LocationDisplay", "VoiceFlyBar", "VoiceActiveSpeakers", "ime", "imeSpace",
    "PopUpHelp", "PopupHelp",
    // root pages that cover the screen but are not windows
    "Back", "trans", "Load2", "SpaceLoading", "MustaLoading", "HothLoading", "Splash", "Start",
};

bool sameName(const std::string& a, const char* b)
{
    return a.size() == std::strlen(b) && _strnicmp(a.c_str(), b, a.size()) == 0;
}

// The close button. 122 of the client's 140 windows call it bg.mmc.close (mmc = the
// minimize/maximize/close caption); the rest use one of a few other paths, and dialogs
// with no caption close on their cancel button.
const char* const CLOSE_PATHS[] = {
    "bg.mmc.close", "mmc.close", "inner.mmc.close", "menu.bg.mmc.close",
    "collapsing.mmc.metal.inner.close", "background.mmc.close", "layout.close",
    "close", "buttonClose", "buttonCancel", "btnCancel", "btnclose",
};
// Paths where the close box hangs right on the window page. This is what tells a window
// from a folder page: a folder (Opt, pda, Auction) has no caption of its own - the
// caption belongs to the window inside it.
const int OWN_CLOSE_PATHS = 7;          // the first this many of CLOSE_PATHS mean a caption of its own

const char* const CLOSE_NAMES[] = { "close", "buttonClose", "closeButton", "buttonCancel", "btnCancel", "btnclose", "cancel" };

const int MIN_W = 80, MIN_H = 60;      // a page smaller than this is decoration, not a window

void findNamedRec(const NodePtr& n, const char* const* names, size_t count, int depth, NodePtr& out)
{
    if (out || !n || depth < 0 || !n->willDraw()) return;
    for (const NodePtr& k : n->children()) {
        if (out) return;
        if (!k->willDraw()) continue;
        if (k->isA(T_Button)) {
            std::string nm = k->name();
            for (size_t i = 0; i < count; ++i)
                if (sameName(nm, names[i]) && Desktop::selectable(k)) { out = k; return; }
        }
        if (k->isA(T_Page)) findNamedRec(k, names, count, depth - 1, out);
    }
}

bool interactiveRec(const NodePtr& n, int depth, int& budget)
{
    if (!n || depth < 0 || --budget < 0 || !n->willDraw()) return false;
    for (const NodePtr& k : n->children()) {
        if (!k->willDraw()) continue;
        const bool cell = n->isA(T_VolumePage) && k->isA(T_Widget);
        if (cell) { if (k->enabled()) return true; continue; }
        if ((k->isA(T_Button) || k->isA(T_Checkbox) || k->isA(T_Textbox) || k->isA(T_List)
             || k->isA(T_Listbox) || k->isA(T_TreeView) || k->isA(T_Dropdownbox)
             || k->isA(T_ComboBox) || k->isA(T_Sliderbar))
            && Desktop::selectable(k)) return true;
        if (k->isA(T_Page) && interactiveRec(k, depth - 1, budget)) return true;
    }
    return false;
}

void tabsRec(const NodePtr& n, int depth, std::vector<NodePtr>& out)
{
    if (!out.empty() || !n || depth < 0 || !n->willDraw()) return;
    for (const NodePtr& k : n->children()) {
        if (!out.empty()) return;
        if (!k->willDraw()) continue;
        if (k->isA(T_TabSet) || k->isA(T_TabbedPane)) {
            for (const NodePtr& t : k->children())
                if (t->isA(T_Button) && Desktop::selectable(t)) out.push_back(t);
            if (!out.empty()) return;
        }
        if (k->isA(T_Page)) tabsRec(k, depth - 1, out);
    }
}

} // namespace

Desktop::Desktop(NodePtr root) : root_(root)
{
    huds_.push_back("GroundHUD");
    huds_.push_back("HudSpace");
    rules_.pass.push_back("PopupHelp");
    rules_.pass.push_back("PopUpHelp");
}

void Desktop::addHud(const char* name) { if (name && *name) huds_.push_back(name); }

void Desktop::addProfile(std::unique_ptr<cursor::Window> p) { if (p) profiles_.push_back(std::move(p)); }

void Desktop::addSkip(const std::string& csv)
{
    size_t i = 0;
    while (i < csv.size()) {
        size_t j = csv.find(',', i);
        if (j == std::string::npos) j = csv.size();
        std::string s = csv.substr(i, j - i);
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        if (!s.empty()) skip_.push_back(s);
        i = j + 1;
    }
}

bool Desktop::selectable(const NodePtr& n)
{
    return n && n->willDraw() && (n->canSelect() || (n->enabled() && n->getsInput()));
}

bool Desktop::isWindowName(const std::string& name) const
{
    if (name.empty()) return false;
    if (name.size() >= 2 && name[0] == 'c' && name[1] == 'p') return false;   // our own widgets
    for (const char* f : FURNITURE) if (sameName(name, f)) return false;
    for (const std::string& s : skip_) if (sameName(name, s.c_str())) return false;
    return true;
}

NodePtr Desktop::closeButton(const NodePtr& page)
{
    if (!page) return NodePtr();
    for (const char* p : CLOSE_PATHS) {
        NodePtr b = page->byPath(p);
        if (b && b->isA(T_Button) && selectable(b)) return b;
    }
    NodePtr out; findNamedRec(page, CLOSE_NAMES, sizeof CLOSE_NAMES / sizeof *CLOSE_NAMES, 5, out);
    return out;
}

std::vector<NodePtr> Desktop::tabs(const NodePtr& page)
{
    std::vector<NodePtr> out;
    if (page) tabsRec(page, 6, out);
    return out;
}

bool Desktop::hasInteractive(const NodePtr& page)
{
    int budget = 256;
    return interactiveRec(page, 8, budget);
}

bool Desktop::isHudName(const std::string& name) const
{
    for (const std::string& h : huds_) if (sameName(name, h.c_str())) return true;
    return false;
}

// The page has a caption of its own: the close box is on it, not on a window inside.
static bool hasTitleBar(const NodePtr& page)
{
    if (!page) return false;
    for (int i = 0; i < OWN_CLOSE_PATHS; ++i) {
        NodePtr b = page->byPath(CLOSE_PATHS[i]);
        if (b && b->isA(T_Button) && Desktop::selectable(b)) return true;
    }
    return false;
}

bool Desktop::isWindow(const NodePtr& page) const
{
    if (!page || !page->isA(T_Page) || !page->willDraw()) return false;
    UISize s = page->size();
    if (s.x < MIN_W || s.y < MIN_H) return false;
    if (!isWindowName(page->name())) return false;
    // The expensive check last: a window with no navigation nodes would take the pad
    // into the void.
    return hasInteractive(page);
}

// A folder page passes the window check - there are nodes inside it - and would stand in
// for the real window: the hints would slide off the bottom of the screen and circle
// would hunt for the close box in the wrong place. The caption tells them apart: present
// means a window, absent means look one level in. One level only, or a window with its
// own caption would hand back its inner panel.
NodePtr Desktop::pick(const NodePtr& page) const
{
    if (hasTitleBar(page)) return page;
    for (const NodePtr& k : page->children()) {
        if (!k->isA(T_Page) || !k->willDraw()) continue;
        if (isWindow(k) && hasTitleBar(k)) return k;
    }
    return page;                                       // a window with no caption: a dialog, a conversation, a message box
}

NodePtr Desktop::findIn(const NodePtr& page, int depth) const
{
    if (!page || !page->willDraw()) return NodePtr();
    for (const NodePtr& k : page->children()) {
        if (!k->isA(T_Page) || !k->willDraw()) continue;
        if (isWindow(k)) return pick(k);               // we do not descend into a window with its own caption
        if (depth > 0) { NodePtr w = findIn(k, depth - 1); if (w) return w; }
    }
    return NodePtr();
}

// The topmost open window. Children are in draw order, first on top, and the client
// raises the focused window with MoveChild(Top); it finds focus by the same "first
// visible mediator" walk in CuiWorkspace::updateGlow, so this order is not a heuristic.
// The desktop is taken apart by its direct children, a folder page is passed through,
// anything else is tried as a window.
NodePtr Desktop::find() const
{
    if (!root_) return NodePtr();
    for (const NodePtr& k : root_->children()) {
        if (!k->isA(T_Page) || !k->willDraw()) continue;
        if (isHudName(k->name())) {
            NodePtr w = findIn(k, 0);                  // the desktop's windows are direct children
            if (w) return w;
            continue;
        }
        if (isWindow(k)) return pick(k);               // GameMenu, MsgBox, confirm and the rest from the root
        NodePtr w = findIn(k, 1);                      // a folder page: Opt.OptMain, pda.inputbox, ...
        if (w) return w;
    }
    return NodePtr();
}

void Desktop::retune(const NodePtr& page) const
{
    profile_ = nullptr;
    window_ = page ? page->name() : std::string();
    tab_ = 0; list_ = nullptr;
    hasTabs_ = !tabs(page).empty();       // the hints only need the flag, the list is collected on a press
    if (page) {
        for (const std::unique_ptr<cursor::Window>& p : profiles_) {
            NodePtr pp = p->page();
            if (pp && pp->handle() == page->handle()) { profile_ = p.get(); break; }
        }
    }
    if (profile_) {
        rules_ = profile_->rules();
    } else {
        ScanRules base;
        base.pass.push_back("PopupHelp");
        base.pass.push_back("PopUpHelp");
        rules_ = base;
    }
    id_ = "desktop";
    if (!window_.empty()) { id_ += ':'; id_ += window_; }
    if (profile_) { id_ += '/'; id_ += profile_->id(); }
    cp::tracef("desktop: window %s%s", window_.empty() ? "-" : window_.c_str(),
               profile_ ? " (own profile)" : "");
}

NodePtr Desktop::page() const
{
    NodePtr p = find();
    void* h = p ? p->handle() : nullptr;
    if (h != curHandle_) { curHandle_ = h; retune(p); }
    return p;
}

std::vector<Hint> Desktop::hints(const Context& c) const
{
    if (profile_) return profile_->hints(c);
    std::vector<Hint> h;
    if (c.cursor.dragging()) {
        h.push_back(Hint{B_TRIANGLE, u"Drop"}); h.push_back(Hint{B_CIRCLE, u"Cancel"});
        return h;
    }
    const NodeInfo* n = c.cursor.current();
    if (inList()) {
        h.push_back(Hint{B_DPAD_DOWN, u"Rows"});
        h.push_back(Hint{B_CROSS, u"Choose"});
        h.push_back(Hint{B_CIRCLE, u"Back"});
        h.push_back(Hint{B_R2, u"Page"});
        return h;
    }
    const bool cell = n && n->kind == NK_Cell;
    const bool list = n && n->kind == NK_List;
    h.push_back(Hint{B_CROSS, cell ? u"Use" : list ? u"Open" : u"Press"});
    if (cell) { h.push_back(Hint{B_SQUARE, u"Menu"}); h.push_back(Hint{B_TRIANGLE, u"Move"}); }
    h.push_back(Hint{B_CIRCLE, u"Close"});
    if (hasTabs_) h.push_back(Hint{B_R1, u"Tabs"});
    // Both triggers scroll, L2 up and R2 down: showing R2 alone is half a lie.
    h.push_back(Hint{B_L2, u"", crossbar::Glyph::TR_L});
    h.push_back(Hint{B_R2, u"Scroll", crossbar::Glyph::TR_R});
    return h;
}

bool Desktop::clickNode(Context& c, const NodePtr& n)
{
    if (!n) return false;
    UIPoint p = n->center();
    c.ui.click(p.x, p.y);
    return true;
}

// Entering a list sets the Focus property, exactly as the client markup does
// (SetProperty "Focus" calls SetFocus) rather than clicking, which would also select
// whatever row lay under the pointer. After that UIList walks the rows itself.
bool Desktop::enterList(Context& c, const NodeInfo& n)
{
    if (!n.node) return false;
    n.node->setPropA(abi::PROP_Focus, "true");
    list_ = n.node->handle();
    c.hints.set(hints(c));
    return true;
}

// The cursor may have left the list - a rescan, a window change, the D-pad sideways -
// and then list mode is over by itself.
void Desktop::syncList(Context& c)
{
    if (!list_) return;
    const NodeInfo* n = c.cursor.current();
    if (!n || !n->node || n->node->handle() != list_ || n->kind != NK_List) leaveList();
}

bool Desktop::onButton(Context& c, Button b, bool pressed)
{
    if (profile_ && profile_->onButton(c, b, pressed)) return true;
    if (!pressed) {
        // Releasing the D-pad in a list only stops the repeat.
        if (inList() && (b == B_DPAD_UP || b == B_DPAD_DOWN)) { listHeld_ = 0; return true; }
        return false;
    }
    syncList(c);
    const NodeInfo* n = c.cursor.current();
    const bool cell = n && n->kind == NK_Cell;
    if (inList()) {
        switch (b) {
        case B_DPAD_UP:   c.ui.keyTap(K_Up); listHeld_ = -1; listRepeat_ = c.cursor.config().repeatFirst; return true;
        case B_DPAD_DOWN: c.ui.keyTap(K_Down); listHeld_ = 1; listRepeat_ = c.cursor.config().repeatFirst; return true;
        case B_DPAD_LEFT: case B_DPAD_RIGHT: leaveList(); return false;   // the D-pad leads out to the widgets
        case B_CROSS:     c.ui.keyTap(K_Enter); return true;              // the client answers a double click on a row the same way
        case B_CIRCLE:    leaveList(); c.hints.set(hints(c)); return true;
        case B_L2:        c.ui.keyTap(K_PageUp); return true;
        case B_R2:        c.ui.keyTap(K_PageDown); return true;
        default: break;
        }
    }
    switch (b) {
    case B_CROSS:
        if (c.cursor.dragging()) { c.cursor.dragDrop(); return true; }
        if (n && n->kind == NK_List) return enterList(c, *n);
        // The scanner recognizes dropdowns (NK_Dropdown), but nothing handles them yet:
        // an opened list is the client's own widget and it is not even clear where it is
        // born. Measured rather than guessed - we print what lies in the context page and
        // what has focus right after it opens.
        if (n && n->kind == NK_Dropdown) {
            c.cursor.click();
            NodePtr ctx = contextPage();
            NodePtr leaf = focusedLeaf();
            tracef("dropdown %s: context %s, focused %s",
                   n->name.c_str(),
                   ctx ? (ctx->name() + "(" + ctx->typeName() + ")").c_str() : "none",
                   leaf ? (leaf->name() + "(" + leaf->typeName() + ")").c_str() : "none");
            if (ctx) for (const NodePtr& k : ctx->children())
                tracef("dropdown: in the context %s(%s) visible=%d", k->name().c_str(), k->typeName().c_str(), (int)k->willDraw());
            return true;
        }
        if (cell) c.cursor.doubleClick(); else c.cursor.click();
        return true;
    case B_SQUARE:
        // The client builds the radial from whatever is under the cursor: on a cell the
        // item menu, on a plain button nothing, which does no harm.
        if (n) { c.cursor.context(); return true; }
        return false;
    case B_TRIANGLE:
        if (c.cursor.dragging()) { c.cursor.dragDrop(); return true; }
        if (cell) { c.cursor.dragStart(); return true; }
        return false;
    case B_CIRCLE: {
        if (c.cursor.dragging()) { c.cursor.dragCancel(); return true; }
        NodePtr close = closeButton(page());
        if (close) return clickNode(c, close);
        c.ui.escape();                       // windows with no close box are closed by Escape
        return true;
    }
    case B_L1: case B_R1: {
        std::vector<NodePtr> t = tabs(page());
        if (t.empty()) { c.cursor.scroll(b == B_L1 ? 3 : -3); return true; }
        // The client does not report the active tab, so we keep our own, starting from
        // the one under the cursor if it happens to be there.
        int base = tab_;
        if (n && n->kind == NK_Tab)
            for (size_t i = 0; i < t.size(); ++i)
                if (t[i]->handle() == n->node->handle()) { base = static_cast<int>(i); break; }
        int idx = base + (b == B_R1 ? 1 : -1);
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(t.size())) idx = static_cast<int>(t.size()) - 1;
        tab_ = idx;
        return clickNode(c, t[idx]);
    }
    case B_L2: c.cursor.scroll(1); return true;
    case B_R2: c.cursor.scroll(-1); return true;
    case B_R3: c.ui.action("examine"); return true;
    default: return false;
    }
}

void Desktop::onTick(Context& c, float dt)
{
    syncList(c);
    if (inList() && listHeld_) {
        listRepeat_ -= dt;
        while (listRepeat_ <= 0) { c.ui.keyTap(listHeld_ < 0 ? K_Up : K_Down); listRepeat_ += c.cursor.config().repeat; }
    } else {
        listHeld_ = 0;
    }
    // The first scroll tick came as an onButton edge; from here it repeats while the
    // trigger is held, or a long list would have to be hammered at.
    const int dir = c.pad.down(B_L2) ? 1 : c.pad.down(B_R2) ? -1 : 0;
    if (dir) {
        if (dir != scrollHeld_) { scrollHeld_ = dir; scrollRepeat_ = 0.35f; }   // a pause before it picks up speed
        else { scrollRepeat_ -= dt; while (scrollRepeat_ <= 0) { c.cursor.scroll(dir); scrollRepeat_ += 0.08f; } }
    } else scrollHeld_ = 0;
    if (profile_) profile_->onTick(c, dt);
}

void Desktop::onBlur(Context& c)
{
    leaveList();
    if (profile_) profile_->onBlur(c);
    else if (c.cursor.dragging()) c.cursor.dragCancel();
}

// Why a page is not a window, in one word. Same order as find().
static const char* verdict(const Desktop& d, const NodePtr& k)
{
    UISize s = k->size();
    if (!d.isWindowName(k->name())) return "furniture";
    if (s.x < MIN_W || s.y < MIN_H) return "small";
    if (!Desktop::hasInteractive(k)) return "empty";
    return hasTitleBar(k) ? "WINDOW" : "folder?";       // no caption of its own: either a folder or a dialog
}

// What the desktop sees and why it skips things - one line for the root and one per
// page it descends into, because a single trace line is 512 bytes. Cheaper than
// deducing names and paths from the sources: the inventory is not where the mediator
// factory says it is.
std::vector<std::string> Desktop::survey() const
{
    std::vector<std::string> out;
    if (!root_) { out.push_back("desktop: no root"); return out; }
    char buf[160];
    std::string line = "desktop/root:";
    std::vector<NodePtr> folders;
    for (const NodePtr& k : root_->children()) {
        if (!k->isA(T_Page) || !k->willDraw()) continue;
        const bool hud = isHudName(k->name());
        std::snprintf(buf, sizeof buf, " %s:%s", k->name().c_str(), hud ? "DESKTOP" : verdict(*this, k));
        line += buf;
        if (hud || !isWindow(k)) folders.push_back(k);
        if (line.size() > 380) { out.push_back(line); line = "desktop/root...:"; }
    }
    out.push_back(line);
    for (const NodePtr& f : folders) {
        line = "desktop/" + f->name() + ":";
        bool any = false;
        for (const NodePtr& k : f->children()) {
            if (!k->isA(T_Page) || !k->willDraw()) continue;
            std::snprintf(buf, sizeof buf, " %s:%s(%dx%d)", k->name().c_str(), verdict(*this, k), k->size().x, k->size().y);
            line += buf; any = true;
            if (line.size() > 380) { out.push_back(line); line = "desktop/" + f->name() + "...:"; }
        }
        if (any) out.push_back(line);
    }
    return out;
}

}} // namespace cp::windows
