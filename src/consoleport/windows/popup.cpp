#include "popup.h"
#include "../abi/props.h"
#include "../core/runtime.h"
#include "../crossbar/crossbar.h"
#include <cmath>

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

Popup::Popup(PageFn fn) : fn_(fn) { rules_.maxDepth = 6; }

NodePtr Popup::radial() const
{
    NodePtr p = page(); if (!p) return NodePtr();
    if (p->isA(T_RadialMenu)) return p;
    for (const NodePtr& k : p->children()) if (k->isA(T_RadialMenu) && k->willDraw()) return k;
    return NodePtr();
}

// The frame stays in the HUD. Raising it above the radial does not work: the radial
// lives in UIManager's context page, drawn after the whole HUD, and reparenting the
// frame kills the client - RemoveChild here frees the child instead of detaching it, so
// AddChild gets a dead object (two crashes on 9 Sep 2026, Popup::onFocus ->
// useOverlay -> addChild). The fix is a second frame built in the root page from the
// start and switched by visibility, with no moves. Not done yet.
void Popup::onFocus(Context& c)
{
    c.cursor.raiseRing();
    // Once per opening: where the context page sits in the tree. If it is a child of
    // the root, a second frame as the root's last child is enough; if UIManager draws
    // it separately after the root, the frame will have to live inside that page.
    NodePtr p = page(); if (!p) return;
    std::string chain; NodePtr up = p->parent();
    for (int i = 0; i < 4 && up; ++i) {
        if (!chain.empty()) chain += " <- ";
        chain += up->name() + "(" + up->typeName() + ")";
        up = up->parent();
    }
    NodePtr root = rootNode();
    tracef("popup: page %s(%s) parents: %s | root %p, parent %p",
           p->name().c_str(), p->typeName().c_str(),
           chain.empty() ? "none" : chain.c_str(),
           root ? root->handle() : nullptr,
           p->parent() ? p->parent()->handle() : nullptr);
}

NodePtr Popup::comboList() const
{
    NodePtr p = page(); if (!p) return NodePtr();
    // The name comes from the client itself (UIComboBox::PerformPopup), which beats
    // the type: a button popup can hold a list too, and there the cursor walks buttons,
    // not rows. It is not a direct child either - measured 10 Sep 2026, the chain is
    // InvisibleContextPage -> TransientComboPopup -> TransientComboList, and searching
    // direct children only left the whole dropdown branch silent.
    for (const NodePtr& k : p->children()) {
        if (!k->willDraw()) continue;
        if (k->name() == "TransientComboList") return k;
        for (const NodePtr& g : k->children())
            if (g->willDraw() && g->name() == "TransientComboList") return g;
    }
    return NodePtr();
}

std::vector<Hint> Popup::hints(const Context&) const
{
    std::vector<Hint> h;
    if (comboList()) {
        // Same set as the lists in windows (Desktop::hints): the widget walks the
        // rows, we only send arrows.
        h.push_back(Hint{B_DPAD_DOWN, u"Rows", crossbar::Glyph::DPAD});
        h.push_back(Hint{B_CROSS, u"Choose"});
        h.push_back(Hint{B_L2, u"", crossbar::Glyph::TR_L});
        h.push_back(Hint{B_R2, u"Page", crossbar::Glyph::TR_R});
        h.push_back(Hint{B_CIRCLE, u"Close"});
        return h;
    }
    if (radial()) {
        // A sector is picked by deflecting the right stick, not pressing it.
        h.push_back(Hint{B_L3, u"", crossbar::Glyph::STICK_L});
        h.push_back(Hint{B_R3, u"Select", crossbar::Glyph::STICK_R});
        h.push_back(Hint{B_CROSS, u"Activate"});
    } else {
        navigateHints(h);
        h.push_back(Hint{B_CROSS, u"Select"});
    }
    h.push_back(Hint{B_CIRCLE, u"Close"});
    return h;
}

// Radial.lua: the angle from vertical, clockwise, sector 0 at the top; count sectors
int Popup::sectorOf(float x, float y, int count, float deadzone)
{
    if (count <= 0) return -1;
    float len = std::sqrt(x * x + y * y);
    if (len < deadzone) return -1;
    float ang = std::atan2(x, -y);                  // 0 up, +90 deg to the right (screen y goes down)
    if (ang < 0) ang += 6.28318530718f;
    float step = 6.28318530718f / count;
    int idx = static_cast<int>(std::floor((ang + step / 2) / step)) % count;
    return idx;
}

int Popup::nearestTo(const std::vector<NodePtr>& items, UIPoint centre, float x, float y, float deadzone)
{
    const float len = std::sqrt(x * x + y * y);
    if (items.empty() || len < deadzone) return -1;
    int best = -1; float bestCos = -2.f;
    for (size_t i = 0; i < items.size(); ++i) {
        UIPoint p = items[i]->center();
        const float dx = static_cast<float>(p.x - centre.x), dy = static_cast<float>(p.y - centre.y);
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l < 1.f) continue;                       // an item at the very center is not a direction
        const float cosang = (x * dx + y * dy) / (len * l);
        if (cosang > bestCos) { bestCos = cosang; best = static_cast<int>(i); }
    }
    // Under ~66 deg to the nearest item: nothing was aimed at.
    return bestCos > 0.4f ? best : -1;
}

void Popup::onTick(Context& c, float dt)
{
    // Holding the D-pad pages the list - fifty palettes one click at a time is no way
    // to live.
    if (listHeld_ && comboList()) {
        listRepeat_ -= dt;
        while (listRepeat_ <= 0) {
            comboList()->setPropA(abi::PROP_Focus, "true");
            c.ui.keyTap(listHeld_ < 0 ? K_Up : K_Down);
            listRepeat_ += c.cursor.config().repeat;
        }
    } else {
        listHeld_ = 0;
    }
    NodePtr r = radial(); if (!r) { sector_ = -1; return; }
    input::Stick s = input::anyStick(c.pad);
    std::vector<NodePtr> buttons;
    for (const NodePtr& k : r->children()) if (k->isA(T_Button)) buttons.push_back(k);
    // By the buttons' own angles, not by dividing the circle: the client's radial puts
    // items where it likes, and three of them are not 120 degrees apart.
    const int sec = nearestTo(buttons, r->center(), s.x, s.y);
    if (sec != sector_) {
        sector_ = sec;
        if (sec >= 0) c.cursor.setCurrentNode(buttons[sec]);
    }
}

bool Popup::onButton(Context& c, Button b, bool pressed)
{
    const bool combo = comboList() != nullptr;
    if (!pressed) {
        if (combo && (b == B_DPAD_UP || b == B_DPAD_DOWN)) { listHeld_ = 0; return true; }
        return false;
    }
    if (combo) {
        // Focus is set by us before every key. The client sets it when the popup
        // opens, but it is not always still there when our arrow arrives - in game on
        // 10 Sep 2026 the ring stood around the whole list and the rows never moved.
        // Same trick as Desktop::enterList and the character select table.
        comboList()->setPropA(abi::PROP_Focus, "true");
        switch (b) {
        case B_DPAD_UP:   c.ui.keyTap(K_Up);   listHeld_ = -1; listRepeat_ = c.cursor.config().repeatFirst; return true;
        case B_DPAD_DOWN: c.ui.keyTap(K_Down); listHeld_ =  1; listRepeat_ = c.cursor.config().repeatFirst; return true;
        case B_CROSS:     c.ui.keyTap(K_Enter); listHeld_ = 0; return true;
        case B_L2:        c.ui.keyTap(K_PageUp); return true;
        case B_R2:        c.ui.keyTap(K_PageDown); return true;
        case B_CIRCLE:    c.ui.escape(); listHeld_ = 0; return true;
        // Sideways and everything else mean nothing in a dropdown, and must not leak
        // out either - a window lies underneath and its cursor would move blind.
        default: return true;
        }
    }
    switch (b) {
    case B_CROSS:
        if (radial() && sector_ >= 0) { c.ui.character(static_cast<char16_t>(u'1' + sector_)); return true; }
        c.cursor.click(); return true;
    case B_CIRCLE: c.ui.escape(); return true;
    default: return false;
    }
}

}} // namespace cp::windows
