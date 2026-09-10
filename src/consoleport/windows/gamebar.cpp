#include "gamebar.h"
#include "../abi/props.h"
#include "popup.h"
#include "../crossbar/crossbar.h"
#include "../core/theme.h"
#include "../core/runtime.h"
#include "../core/hooks.h"
#include <cmath>
#include <cctype>
#include <cstdio>

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

GameBar::GameBar(PageFn fn) : fn_(fn) { rules_.maxDepth = 6; }

NodePtr GameBar::volume() const
{
    NodePtr p = page(); if (!p) return NodePtr();
    NodePtr v = p->child("vs");
    if (v && v->isA(T_VolumePage)) return v;
    for (const NodePtr& k : p->children()) if (k->isA(T_VolumePage)) return k;
    return NodePtr();
}

std::vector<NodePtr> GameBar::cells() const
{
    std::vector<NodePtr> out;
    NodePtr v = volume(); if (!v) return out;
    for (const NodePtr& k : v->children()) if (k->willDraw()) out.push_back(k);
    return out;
}

std::u16string GameBar::labelOf(const NodePtr& cell)
{
    if (!cell) return std::u16string();
    // A tooltip is an already localized client string - what the mouse would show. We
    // take it if it exists and fits under the icon.
    std::u16string tip;
    if ((cell->getProp(abi::PROP_LocalTooltip, tip) || cell->getProp(abi::PROP_Tooltip, tip))
        && !tip.empty() && tip.size() <= 16)
        return tip;

    // Fallback: the widget name. 'buttonJournal' -> 'Journal', 'mail' -> 'Mail'.
    std::string n = cell->name();
    if (n.compare(0, 6, "button") == 0 && n.size() > 6) n = n.substr(6);
    std::u16string out;
    for (size_t i = 0; i < n.size() && i < 16; ++i) {
        char c = n[i];
        if (i == 0) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
        out.push_back(static_cast<char16_t>(c));
    }
    return out;
}

void GameBar::ringPoint(int i, int count, int radius, int& dx, int& dy)
{
    if (count <= 0) { dx = dy = 0; return; }
    const float step = 6.28318530718f / count, ang = step * i;
    dx = static_cast<int>(std::lround(radius * std::sin(ang)));
    dy = static_cast<int>(std::lround(-radius * std::cos(ang)));
}

int GameBar::ringRadius(int count, int cell, int gap)
{
    if (count <= 0) return 0;
    const float need = static_cast<float>(count) * (cell + gap);
    int r = static_cast<int>(std::ceil(need / 6.28318530718f));   // rounding down is not allowed: the cells would meet
    const int least = cell + gap;                 // with three or four items the formula comes out too tight
    return r < least ? least : r;
}

void GameBar::layout()
{
    NodePtr p = page(), v = volume(); if (!p || !v || laid_) return;
    std::vector<NodePtr> cs = cells(); if (cs.empty()) return;
    NodePtr hud = p->parent(); if (!hud) return;
    overlay_ = hud;

    // Cell size off a live widget, not the markup: the UI is scaled, and the 22x22 in
    // ui_ground_hud_buttonbar.inc is a lie by then.
    int cell = 0;
    for (const NodePtr& k : cs) {
        if (k->size().x > cell) cell = k->size().x;
        if (k->size().y > cell) cell = k->size().y;
    }
    const int count = static_cast<int>(cs.size());
    // With captions an item is as wide as its caption, not its cell, so the tight
    // by-icon formula would overlap them. The shared ring radius also puts this menu and
    // the target rings in the same place on screen.
    const int radius = textTpl_ ? world::RING_RADIUS : ringRadius(count, cell, cell / 2);
    const UISize screen = hud->size();
    const int cx = screen.x / 2, cy = screen.y / 2;

    // Decor first, so the clones land on top.
    decor_.show(radius);

    clones_.clear(); origin_.clear();
    char nm[32];
    for (int i = 0; i < count; ++i) {
        NodePtr c = cs[i]->clone();
        if (!c) continue;
        std::snprintf(nm, sizeof nm, "cpBarClone%d", i);
        c->setPropA(abi::PROP_Name, nm);
        c->setPropA(abi::PROP_GetsInput, "false");
        c->setPropA(abi::PROP_AbsorbsInput, "false");
        c->setPropA(abi::PROP_PackLocation, "nfn,nfn");
        c->setPropA(abi::PROP_PackSize, "f,f");
        int dx = 0, dy = 0; ringPoint(i, count, radius, dx, dy);
        const UISize sz = cs[i]->size();
        const int sz2 = sz.y;
        c->setLocation(cx + dx - sz.x / 2, cy + dy - sz.y / 2);
        c->setVisible(true);
        hud->addChild(c); c->link(); hud->moveChild(c, 2);
        clones_.push_back(c); origin_.push_back(cs[i]);

        // A caption under the icon: twelve small squares differing only by artwork,
        // and the panel's artwork is small and much alike.
        if (!textTpl_) continue;
        NodePtr t = textTpl_->clone();
        if (!t) continue;
        std::snprintf(nm, sizeof nm, "cpBarLabel%d", i);
        t->setPropA(abi::PROP_Name, nm);
        t->setPropA(abi::PROP_GetsInput, "false"); t->setPropA(abi::PROP_AbsorbsInput, "false");
        t->setPropA(abi::PROP_PackLocation, "nfn,nfn"); t->setPropA(abi::PROP_PackSize, "f,f");
        t->setPropA(abi::PROP_BackgroundOpacity, "0.00");
        t->setPropA(abi::PROP_TextAlignment, "Center"); t->setPropA(abi::PROP_Font, "bold_11");
        theme::paint(t, theme::ROLE_TEXT, "text1");
        const int lw = 96;
        char ext[24]; std::snprintf(ext, sizeof ext, "%d,%d", lw, 16);
        t->setPropA(abi::PROP_ScrollExtent, ext); t->setSize(lw, 16);
        t->setLocalText(labelOf(cs[i]).c_str());
        t->setLocation(cx + dx - lw / 2, cy + dy + sz2 / 2 + 2);
        t->setVisible(true);
        hud->addChild(t); t->link(); hud->moveChild(t, 2);
        labels_.push_back(t);
    }
    laid_ = true;
    radius_ = radius;
    tracef("gamebar: a ring of %d clones, radius %d; the panel was left alone",
           static_cast<int>(clones_.size()), radius);
}

void GameBar::restore()
{
    decor_.hide();
    if (overlay_) {
        for (const NodePtr& c : clones_) if (c) overlay_->removeChild(c);
        for (const NodePtr& t : labels_) if (t) overlay_->removeChild(t);
    }
    clones_.clear(); origin_.clear(); labels_.clear();
    overlay_.reset();
    laid_ = false;
}

bool GameBar::toggle()
{
    if (!page()) return false;
    open_ = !open_;
    return open_;
}

void GameBar::onFocus(Context& c) { layout(); c.cursor.raiseRing(); }
void GameBar::onBlur(Context&)  { restore(); sector_ = -1; }

std::vector<Hint> GameBar::hints(const Context&) const
{
    std::vector<Hint> h;
    // A sector is picked by deflecting either stick, hence two glyphs and one caption -
    // empty text draws the picture alone.
    h.push_back(Hint{B_L3, u"", crossbar::Glyph::STICK_L});
    h.push_back(Hint{B_R3, u"Select", crossbar::Glyph::STICK_R});
    h.push_back(Hint{B_CROSS, u"Open"});
    // Options closes it too, but the atlas has no Options glyph and a cross instead
    // would be a lie, so the hint shows circle - which also closes it.
    h.push_back(Hint{B_CIRCLE, u"Close"});
    return h;
}

void GameBar::onTick(Context& c, float dt)
{
    std::vector<NodePtr> cs = cells();
    if (cs.empty()) { sector_ = -1; return; }
    input::Stick s = input::anyStick(c.pad);
    // As many sectors as there are clones: counting the original cells is wrong while
    // the ring is not built.
    const int n = clones_.empty() ? static_cast<int>(cs.size()) : static_cast<int>(clones_.size());
    const int sec = Popup::sectorOf(s.x, s.y, n);
    if (sec != sector_) {
        sector_ = sec;
        if (sec >= 0 && sec < static_cast<int>(clones_.size())) c.cursor.setCurrentNode(clones_[sec]);
    }
    // The wedge follows the stick as in the target ring: one motion for both menus.
    decor_.update(sector_, static_cast<int>(cs.size()), dt);
}

bool GameBar::onButton(Context& c, Button b, bool pressed)
{
    if (!pressed) return false;
    switch (b) {
    case B_CROSS: {
        // The press goes to the original panel button - the clone is only a picture.
        const int sec = sector_;
        NodePtr target = (sec >= 0 && sec < static_cast<int>(origin_.size())) ? origin_[sec] : NodePtr();
        const bool took = target && target->press();
        tracef("gamebar: pressing item %d (%s) -> %s", sec,
               target ? target->name().c_str() : "-", took ? "pressed" : "NOT pressed");
        open_ = false;                             // it was chosen - the ring closed
        return true;
    }
    case B_CIRCLE:
        open_ = false; return true;                // the stack will drop the focus -> onBlur returns the panel
        // B_OPTIONS never reaches here on purpose: the runtime drives it, see frame()
    default: return false;
    }
}

}} // namespace cp::windows
