#include "targetring.h"
#include "../abi/props.h"
#include "../core/theme.h"
#include "../windows/gamebar.h"
#include "../windows/popup.h"
#include "../core/runtime.h"
#include <cmath>
#include <cstdio>

namespace cp { namespace world {

namespace {
// Proportions agreed off the 9 Sep 2026 mockup. The radius is not taste: neighbours are
// a chord of 2*r*sin(pi/N) apart, and with eight names like "Jawa Scavenger" (~110 px)
// anything smaller puts the labels flush. One radius for every ring and any item count -
// a ring that breathes from four targets to twelve reads as different menus - so it is
// the label width that gets capped instead. 2*r*sin(pi/12) = 0.518*r.
const int RADIUS = 205;
// How often the distances are recomputed: everyone moves, and a frozen "12m" is a lie
// one step later.
const float DIST_MS = 250.f;
const int ITEM_W_MIN = 68, ITEM_H = 22, CHAR_W = 7;
const int ITEM_W_MAX = 100;             // 0.518*205 = 106, with a gap
// How long a hover has to hold before it selects. Without it dragging the stick across
// the ring would send a target change per sector passed.
const float HOVER_MS = 120.f;

// Client palette entries, so the ring follows the theme. Roles as in the stock markup:
// line1 outlines, back3 a background, text1 a caption, highlight a selection.
const char* PAL_IDLE = "line1";
const char* PAL_KEY_BG = "back3";
const char* PAL_TEXT = "text1";
const char* PAL_PICK = "highlight";
const char* PAL_TITLE = "header";
// Enemy colours as the client paints a name overhead (CuiGameColorManager: attackable
// yellow, canAttackYou red).
const char* COLOR_ATTACKABLE = "#FFDD00";
const char* COLOR_HOSTILE = "#FF4444";

const char* labelColor(bin::game::Threat t)
{
    switch (t) {
    case bin::game::THREAT_HOSTILE:    return COLOR_HOSTILE;
    case bin::game::THREAT_ATTACKABLE: return COLOR_ATTACKABLE;
    default:                            return "#D8E4EC";
    }
}
} // namespace

std::vector<RingItem> partyItems()
{
    std::vector<RingItem> v;
    RingItem self; self.action = "targetSelf"; self.label = u"Self";
    // Our own name comes the same way the targets' do, getLocalizedName on the player.
    // It works solo with no group at all, and exercises the name chain on an object
    // that is certainly alive.
    void* player = bin::game::playerCreature();
    std::u16string mine;
    if (player && bin::game::localizedName(bin::game::asClientObject(player), mine) && !mine.empty())
        self.label = mine;
    v.push_back(self);

    // targetGroup0 is the first mate, not the player - targetGroupMember skips oneself
    // (GroupObject::findMemberByIndex), so we skip it too.
    static const char* const G[8] = {"targetGroup0", "targetGroup1", "targetGroup2", "targetGroup3",
                                     "targetGroup4", "targetGroup5", "targetGroup6", "targetGroup7"};
    std::u16string names[8];
    const int n = bin::game::groupMemberNames(names, 8);
    for (int i = 0; i < n; ++i) {
        RingItem it; it.action = G[i];
        // A mate's name is in the group itself; if it comes back empty a number is
        // used, as the group window does, so no item goes nameless.
        if (!names[i].empty()) it.label = names[i];
        else it.label.push_back(static_cast<char16_t>(u'1' + i));
        v.push_back(it);
    }
    return v;
}

bool TargetRing::attach(NodePtr overlay, NodePtr frameTemplate, NodePtr textTemplate, NodePtr imageTemplate)
{
    detach();
    if (!overlay) return false;
    overlay_ = overlay; frameTpl_ = frameTemplate; textTpl_ = textTemplate; imageTpl_ = imageTemplate;
    decor_.attach(overlay, imageTemplate);
    return true;
}

void TargetRing::detach()
{
    teardown();
    decor_.detach();
    overlay_.reset(); frameTpl_.reset(); textTpl_.reset(); imageTpl_.reset();
}

void TargetRing::teardown()
{
    decor_.hide();
    if (overlay_) {
        for (const NodePtr& n : frames_) if (n) overlay_->removeChild(n);
        for (const NodePtr& n : texts_)  if (n) overlay_->removeChild(n);
        if (hub_)  overlay_->removeChild(hub_);
        if (hubTitle_) overlay_->removeChild(hubTitle_);
    }
    frames_.clear(); texts_.clear(); items_.clear();
    hub_.reset(); hubTitle_.reset(); title_.clear();
    open_ = false; sector_ = -1; painted_ = -2; applied_ = -1; hoverMs_ = 0.f;
    savedTarget_ = nullptr; hadTarget_ = false;
}

void TargetRing::build()
{
    // Decor first so the labels land on top of it.
    decor_.show(radius_, rimColor_);

    char b[64];
    for (size_t i = 0; i < items_.size(); ++i) {
        NodePtr f, t;
        if (frameTpl_) {
            f = frameTpl_->clone();
            if (f) {
                std::snprintf(b, sizeof b, "cpRingF%d", static_cast<int>(i));
                f->setPropA(abi::PROP_Name, b);
                f->setPropA(abi::PROP_GetsInput, "false"); f->setPropA(abi::PROP_AbsorbsInput, "false");
                f->setPropA(abi::PROP_PackLocation, "nfn,nfn"); f->setPropA(abi::PROP_PackSize, "f,f");
                f->setPropA(abi::PROP_RStyleDefault, "/Styles.New.fatFrameInside_palW.rs_default");
                theme::paint(f, theme::ROLE_COLOR, PAL_IDLE);
                theme::paint(f, theme::ROLE_BG, PAL_KEY_BG);
                f->setPropA(abi::PROP_BackgroundOpacity, "0.75");
                std::snprintf(b, sizeof b, "%d,%d", itemW_, ITEM_H); f->setPropA(abi::PROP_ScrollExtent, b);
                f->setSize(itemW_, ITEM_H);
                // The template is a toolbar corner with captions and arrows inside; a
                // clone brings them along and we want the ground only.
                for (const NodePtr& c : f->children()) if (c->isA(T_Widget)) c->setVisible(false);
                overlay_->addChild(f); f->link(); overlay_->moveChild(f, 2);
            }
        }
        if (textTpl_) {
            t = textTpl_->clone();
            if (t) {
                std::snprintf(b, sizeof b, "cpRingT%d", static_cast<int>(i));
                t->setPropA(abi::PROP_Name, b);
                t->setPropA(abi::PROP_GetsInput, "false"); t->setPropA(abi::PROP_AbsorbsInput, "false");
                t->setPropA(abi::PROP_PackLocation, "nfn,nfn"); t->setPropA(abi::PROP_PackSize, "f,f");
                t->setPropA(abi::PROP_BackgroundOpacity, "0.00");
                t->setPropA(abi::PROP_TextAlignment, "Center"); t->setPropA(abi::PROP_Font, "bold_11");
                theme::paint(t, theme::ROLE_TEXT, labelColor(items_[i].threat));
                std::snprintf(b, sizeof b, "%d,%d", itemW_, ITEM_H); t->setPropA(abi::PROP_ScrollExtent, b);
                t->setSize(itemW_, ITEM_H);
                overlay_->addChild(t); t->link(); overlay_->moveChild(t, 2);
            }
        }
        frames_.push_back(f); texts_.push_back(t);
        setItemText(i);
    }

    // The selected name goes in the middle: the ring is large, and the centre is easier
    // to read than the far edge under your thumb.
    if (textTpl_) {
        hub_ = textTpl_->clone();
        if (hub_) {
            hub_->setPropA(abi::PROP_Name, "cpRingHub");
            hub_->setPropA(abi::PROP_GetsInput, "false"); hub_->setPropA(abi::PROP_AbsorbsInput, "false");
            hub_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); hub_->setPropA(abi::PROP_PackSize, "f,f");
            hub_->setPropA(abi::PROP_BackgroundOpacity, "0.00");
            hub_->setPropA(abi::PROP_TextAlignment, "Center"); hub_->setPropA(abi::PROP_Font, "bold_12");
            theme::paint(hub_, theme::ROLE_TEXT, PAL_PICK);
            hub_->setPropA(abi::PROP_ScrollExtent, "200,20");
            hub_->setSize(200, 20);
            hub_->setLocalText(u"");
            overlay_->addChild(hub_); hub_->link(); overlay_->moveChild(hub_, 2);
        }
        // Above the name, which ring this is: without it the four differ only by
        // content, and content can look alike.
        const UISize screen = overlay_->size();
        makeTitle(screen.x / 2, screen.y / 2 - 32);
    }
}

void TargetRing::makeTitle(int cx, int cy)
{
    if (!textTpl_ || title_.empty()) return;
    hubTitle_ = textTpl_->clone();
    if (!hubTitle_) return;
    hubTitle_->setPropA(abi::PROP_Name, "cpRingTitle");
    hubTitle_->setPropA(abi::PROP_GetsInput, "false"); hubTitle_->setPropA(abi::PROP_AbsorbsInput, "false");
    hubTitle_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); hubTitle_->setPropA(abi::PROP_PackSize, "f,f");
    hubTitle_->setPropA(abi::PROP_BackgroundOpacity, "0.00");
    hubTitle_->setPropA(abi::PROP_TextAlignment, "Center"); hubTitle_->setPropA(abi::PROP_Font, "bold_13");
    theme::paint(hubTitle_, theme::ROLE_TEXT, PAL_TITLE);
    hubTitle_->setPropA(abi::PROP_ScrollExtent, "220,20");
    hubTitle_->setSize(220, 20);
    hubTitle_->setLocalText(title_.c_str());
    hubTitle_->setLocation(cx - 110, cy);
    overlay_->addChild(hubTitle_); hubTitle_->link(); overlay_->moveChild(hubTitle_, 2);
}

void TargetRing::setItemText(size_t i)
{
    if (i >= texts_.size() || !texts_[i]) return;
    std::u16string t = items_[i].label;
    // Three identical "Bank Terminal"s are told apart by distance alone. Action items
    // have none and keep their caption.
    if (items_[i].dist >= 0.f) {
        char b[16]; std::snprintf(b, sizeof b, "  %dm", static_cast<int>(items_[i].dist + 0.5f));
        for (const char* p = b; *p; ++p) t.push_back(static_cast<char16_t>(*p));
    }
    texts_[i]->setLocalText(t.c_str());
}

void TargetRing::refreshDistances()
{
    void* player = bin::game::playerCreature();
    float ppos[3];
    if (!player || !bin::game::positionOf(player, ppos)) return;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (!items_[i].object || items_[i].dist < 0.f) continue;
        float tpos[3];
        if (!bin::game::positionOf(items_[i].object, tpos)) continue;
        const float dx = tpos[0] - ppos[0], dy = tpos[1] - ppos[1], dz = tpos[2] - ppos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        // Redraw only when the metre figure actually changed - setLocalText parses the
        // string inside the client.
        if (static_cast<int>(d + 0.5f) == static_cast<int>(items_[i].dist + 0.5f)) continue;
        items_[i].dist = d;
        setItemText(i);
    }
}

void TargetRing::place()
{
    const int n = static_cast<int>(items_.size());
    if (n <= 0 || !overlay_) return;
    const UISize screen = overlay_->size();
    const int cx = screen.x / 2, cy = screen.y / 2;

    for (int i = 0; i < n; ++i) {
        int dx = 0, dy = 0;
        // Same traversal as GameBar and Popup::sectorOf - sector 0 at the top,
        // clockwise. Diverge and the highlight stops matching the selection.
        windows::GameBar::ringPoint(i, n, radius_, dx, dy);
        const int x = cx + dx - itemW_ / 2, y = cy + dy - ITEM_H / 2;
        if (frames_[i]) frames_[i]->setLocation(x, y);
        if (texts_[i])  texts_[i]->setLocation(x, y);
    }
    if (hub_) hub_->setLocation(cx - 100, cy - 10);
}

void TargetRing::paint()
{
    if (painted_ == sector_) return;
    painted_ = sector_;
    for (size_t i = 0; i < frames_.size(); ++i) {
        const bool pick = static_cast<int>(i) == sector_;
        if (frames_[i]) {
            theme::paint(frames_[i], theme::ROLE_COLOR, pick ? PAL_PICK : PAL_IDLE);
            theme::paint(frames_[i], theme::ROLE_BG, pick ? PAL_PICK : PAL_KEY_BG);
            frames_[i]->setPropA(abi::PROP_BackgroundOpacity, pick ? "0.92" : "0.75");
        }
        // The caption keeps its hostility colour and the selection is shown by the
        // frame and the wedge; repainting the text would lose enemy from target.
        if (texts_[i]) texts_[i]->setPropA(abi::PROP_Font, pick ? "bold_12" : "bold_11");
    }
    if (hub_) {
        const bool have = sector_ >= 0 && sector_ < static_cast<int>(items_.size());
        hub_->setLocalText(have ? items_[sector_].label.c_str() : u"");
        if (have) theme::paint(hub_, theme::ROLE_TEXT, labelColor(items_[sector_].threat));
    }
}

void TargetRing::open(const std::vector<RingItem>& items, const char16_t* title, const char16_t* emptyLabel)
{
    if (!overlay_) return;
    if (open_) close();
    items_ = items;
    title_ = title ? std::u16string(title) : std::u16string();
    if (items_.empty()) {
        // An empty ring is a backdrop, a rim and a caption: nothing to select, but the
        // button visibly worked.
        decor_.show(RADIUS, rimColor_);
        open_ = true; sector_ = -1; applied_ = -1; hoverMs_ = 0.f;
        savedTarget_ = nullptr; hadTarget_ = false;
        if (textTpl_) {
            hub_ = textTpl_->clone();
            if (hub_) {
                hub_->setPropA(abi::PROP_Name, "cpRingHub");
                hub_->setPropA(abi::PROP_GetsInput, "false"); hub_->setPropA(abi::PROP_AbsorbsInput, "false");
                hub_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); hub_->setPropA(abi::PROP_PackSize, "f,f");
                hub_->setPropA(abi::PROP_BackgroundOpacity, "0.00");
                hub_->setPropA(abi::PROP_TextAlignment, "Center"); hub_->setPropA(abi::PROP_Font, "bold_12");
                theme::paint(hub_, theme::ROLE_TEXT, PAL_TEXT);
                hub_->setPropA(abi::PROP_ScrollExtent, "220,20");
                hub_->setSize(220, 20);
                hub_->setLocalText(emptyLabel);
                const UISize screen = overlay_->size();
                hub_->setLocation(screen.x / 2 - 110, screen.y / 2 - 10);
                makeTitle(screen.x / 2, screen.y / 2 - 32);
                overlay_->addChild(hub_); hub_->link(); overlay_->moveChild(hub_, 2);
            }
        }
        tracef("ring: opened empty");
        return;
    }
    // Label width follows the longest caption, within limits: too narrow clips the name,
    // too wide spreads the ring over half the screen.
    size_t longest = 0;
    for (const RingItem& it : items_) if (it.label.size() > longest) longest = it.label.size();
    itemW_ = static_cast<int>(longest) * CHAR_W + 16;
    if (itemW_ < ITEM_W_MIN) itemW_ = ITEM_W_MIN;
    if (itemW_ > ITEM_W_MAX) itemW_ = ITEM_W_MAX;

    radius_ = RADIUS;
    build();
    place();
    sector_ = -1; painted_ = -2; applied_ = -1; hoverMs_ = 0.f; paint();
    // Remember the pre-ring target so circle can put it back after a hover.
    void* player = bin::game::playerCreature();
    savedTarget_ = bin::game::lookAtTarget(player);
    hadTarget_ = savedTarget_ != nullptr;
    for (const NodePtr& n : frames_) if (n) n->setVisible(true);
    for (const NodePtr& n : texts_)  if (n) n->setVisible(true);
    open_ = true;
    tracef("ring: opened, %d items, label width %d", static_cast<int>(items_.size()), itemW_);
}

void TargetRing::close()
{
    if (!open_ && frames_.empty()) return;
    // The labels are demolished, not hidden: the item list changes (a group forms or
    // falls apart) and a surviving clone would show yesterday's.
    const bool was = open_;
    teardown();
    if (was) tracef("ring: closed");
}

void TargetRing::update(const input::Pad& pad, float dt, Ui& ui)
{
    if (!open_) return;
    const int n = static_cast<int>(items_.size());
    if (n <= 0) { sector_ = -1; return; }      // an empty ring: nothing to select
    const input::Stick s = pad.rightStick();
    const int was = sector_;
    sector_ = windows::Popup::sectorOf(s.x, s.y, n);
    paint();
    decor_.update(sector_, n, dt);

    distMs_ += dt * 1000.f;
    if (distMs_ >= DIST_MS) { distMs_ = 0.f; refreshDistances(); }

    // Selection on hover: a sector has to hold for HOVER_MS, so a stick travelling
    // across the ring does not select every sector on the way.
    if (sector_ != was) hoverMs_ = 0.f;
    else hoverMs_ += dt * 1000.f;
    if (sector_ >= 0 && sector_ != applied_ && hoverMs_ >= HOVER_MS)
        if (applyItem(ui, sector_)) applied_ = sector_;
}

bool TargetRing::applyItem(Ui& ui, int index)
{
    if (index < 0 || index >= static_cast<int>(items_.size())) return false;
    const RingItem& it = items_[index];
    if (it.object) {
        // A target: one setLookAtTarget, what a left click does.
        void* player = bin::game::playerCreature();
        const bool took = bin::game::setLookAtTarget(player, it.object);
        tracef("ring: hover %d -> target %p (%s)", index, it.object, took ? "selected" : "NOT selected");
        return took;
    }
    if (!it.action) return false;
    const bool took = ui.action(it.action);
    tracef("ring: hover %d -> %s (%s)", index, it.action, took ? "accepted" : "NOT accepted");
    return took;
}

bool TargetRing::apply(Ui&)
{
    // The hover already selected; release only closes.
    const bool picked = applied_ >= 0;
    if (!picked) tracef("ring: closed with nothing selected");
    close();
    return picked;
}

void TargetRing::cancel(Ui& ui)
{
    // Cancel has to put back what was there - the hover may already have changed it.
    if (applied_ >= 0) {
        void* player = bin::game::playerCreature();
        if (hadTarget_ && savedTarget_ && bin::game::setLookAtTarget(player, savedTarget_))
            tracef("ring: cancelled, target restored to %p", savedTarget_);
        else {
            ui.action("untarget");
            tracef("ring: cancelled, there was no target - clearing it");
        }
    }
    close();
}

}} // namespace cp::world
