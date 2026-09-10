#include "cursor.h"
#include "../crossbar/crossbar.h"
#include "../abi/props.h"
#include <cmath>
#include <cstdio>

namespace cp { namespace cursor {

Cursor::Cursor(Ui& ui) : ui_(ui) {}

void Cursor::setRoot(NodePtr root, const ScanRules& rules)
{
    if (root_ && root && !root_->sameAs(*root)) { oldNode_ = curNode_; curNode_.reset(); }
    root_ = root; rules_ = rules; rescanTimer_ = 0;
    rescan();
    if (cur_ < 0 && !nodes_.empty()) {
        UIPoint c = root_ ? root_->center() : UIPoint{0, 0};
        setCurrent(arbitrary(nodes_, curNode_, oldNode_, c));
    }
}

void Cursor::clearRoot()
{
    if (dragging_) dragCancel();
    oldNode_ = curNode_; curNode_.reset(); root_.reset(); nodes_.clear(); cur_ = -1; held_ = false;
    if (pointer_) pointer_->setVisible(false);
    if (focus_) focus_->setVisible(false);
}

void Cursor::rescan()
{
    nodes_ = root_ ? scanNodes(root_, rules_) : std::vector<NodeInfo>();
    cur_ = indexOf(nodes_, curNode_);
    if (cur_ < 0 && curNode_) { oldNode_ = curNode_; curNode_.reset(); }
    if (cur_ < 0 && root_ && !nodes_.empty()) setCurrent(arbitrary(nodes_, NodePtr(), oldNode_, root_->center()));
}

bool Cursor::setCurrent(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return false;
    bool changed = cur_ != idx;
    cur_ = idx; curNode_ = nodes_[idx].node;
    if (changed && !dragging_) moveMouseToCurrent();
    if (dragging_) { UIPoint c = nodes_[idx].center; ui_.dragMove(c.x, c.y); }
    if (focus_) focus_->setVisible(true);
    if (pointer_) pointer_->setVisible(true);
    return true;
}

bool Cursor::setCurrentNode(const NodePtr& n) { int i = indexOf(nodes_, n); return i >= 0 && setCurrent(i); }

void Cursor::moveMouseToCurrent()
{
    const NodeInfo* n = current(); if (!n) return;
    ui_.moveTo(n->center.x, n->center.y);
}

bool Cursor::navigate(Dir d)
{
    if (nodes_.empty()) return false;
    if (cur_ < 0) return setCurrent(arbitrary(nodes_, curNode_, oldNode_, root_ ? root_->center() : UIPoint{0, 0}));
    int nxt = bestInDirection(nodes_, cur_, d, cfg_.wrap);
    return nxt >= 0 && setCurrent(nxt);
}

void Cursor::input(Dir d, bool down)
{
    if (down) {
        if (!held_ || heldDir_ != d) { navigate(d); held_ = true; heldDir_ = d; repeatTimer_ = cfg_.repeatFirst; }
    } else if (held_ && heldDir_ == d) {
        held_ = false;
    }
}

void Cursor::tick(float dt)
{
    if (!root_) return;
    rescanTimer_ += dt;
    if (rescanTimer_ >= cfg_.rescanEvery) { rescanTimer_ = 0; rescan(); }
    if (held_) {
        repeatTimer_ -= dt;
        while (repeatTimer_ <= 0) { navigate(heldDir_); repeatTimer_ += cfg_.repeat; }
    }
    updateVisuals(dt);
}

bool Cursor::click()       { const NodeInfo* n = current(); return n && ui_.click(n->center.x, n->center.y); }

// A mouse press is not available. In game mode the client hands out no pointer (a panel
// asks for one itself via CuiManager::requestPointer) and UIManager takes no mouse
// messages: on 9 Sep 2026 ProcessMessage returned false on all twelve game menu buttons
// with the aim dead on. UIButton has a method-property instead: SetProperty("Press")
// calls UIButton::Press(), which sends OnButtonPressed and NotifyActionListener - a real
// press without a mouse or pointer mode.
bool Cursor::activate()
{
    const NodeInfo* n = current();
    if (!n || !n->node) return false;
    if (n->node->press()) return true;
    return click();
}
void Cursor::doubleClick() { const NodeInfo* n = current(); if (n) ui_.doubleClick(n->center.x, n->center.y); }
bool Cursor::context()     { const NodeInfo* n = current(); return n && ui_.contextRequest(n->center.x, n->center.y); }
bool Cursor::rightClick()  { const NodeInfo* n = current(); return n && ui_.rightClick(n->center.x, n->center.y); }
void Cursor::scroll(int delta) { const NodeInfo* n = current(); if (n) ui_.wheel(n->center.x, n->center.y, delta); }

bool Cursor::dragStart()
{
    const NodeInfo* n = current(); if (!n || dragging_) return false;
    dragging_ = true; dragSource_ = n->node;
    ui_.dragStart(n->center.x, n->center.y);
    return true;
}

void Cursor::dragDrop()
{
    if (!dragging_) return;
    const NodeInfo* n = current();
    if (n) ui_.dragDrop(n->center.x, n->center.y); else ui_.escape();
    dragging_ = false; dragSource_.reset();
}

void Cursor::dragCancel()
{
    if (!dragging_) return;
    ui_.escape();               // UIManager: Escape during a drag = DragCancel
    dragging_ = false; dragSource_.reset();
}

void Cursor::attachVisuals(NodePtr overlay, NodePtr imageTemplate, NodePtr pageTemplate)
{
    detachVisuals();
    overlay_ = overlay; if (!overlay_) return;
    char b[32];
    if (pageTemplate) {
        focus_ = pageTemplate->clone();
        if (focus_) {
            focus_->setPropA(abi::PROP_Name, "cpFocus"); focus_->setPropA(abi::PROP_GetsInput, "false"); focus_->setPropA(abi::PROP_AbsorbsInput, "false");
            focus_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); focus_->setPropA(abi::PROP_PackSize, "f,f");
            // Our own style, not the template's: the template is a toolbar corner and
            // we want a plain outline. The clone's children are hidden too - the pane
            // number and the paging arrows come with the corner.
            focus_->setPropA(abi::PROP_BackgroundOpacity, "0.00"); focus_->setPropA(abi::PROP_Opacity, "1.00");
            for (const NodePtr& c : focus_->children()) if (c->isA(T_Widget)) c->setVisible(false);
            focus_->setVisible(false); overlay_->addChild(focus_); focus_->link(); overlay_->moveChild(focus_, 2);
            for (int i = 0; i < 4; ++i) {
                // The bar is a picture, not a cloned page: a page's background is the
                // style's artwork and tinting it gives a muddy olive edge (in game,
                // 8 Sep 2026). On a picture Color paints directly, as for the crossbar
                // glyphs. The source is the solid white block in the atlas, stretched.
                NodePtr bar = imageTemplate ? imageTemplate->clone() : pageTemplate->clone();
                if (!bar) continue;
                std::snprintf(b, sizeof b, "cpFocusBar%d", i);
                bar->setPropA(abi::PROP_Name, b);
                bar->setPropA(abi::PROP_GetsInput, "false"); bar->setPropA(abi::PROP_AbsorbsInput, "false");
                bar->setPropA(abi::PROP_PackLocation, "nfn,nfn"); bar->setPropA(abi::PROP_PackSize, "f,f");
                if (imageTemplate) {
                    bar->setPropA(abi::PROP_SourceResource, crossbar::Glyph::atlas());
                    UIRect sr = crossbar::Glyph::solidRect();
                    std::snprintf(b, sizeof b, "%d,%d,%d,%d", sr.left, sr.top, sr.right, sr.bottom);
                    bar->setPropA(abi::PROP_SourceRect, b);
                } else {
                    bar->setPropA(abi::PROP_RStyleDefault, "/Styles.New.fatFrameInside_palW.rs_default");
                    bar->setPropA(abi::PROP_BackgroundTint, "#FFD24A");
                    bar->setPropA(abi::PROP_BackgroundOpacity, "1.00");
                }
                bar->setPropA(abi::PROP_Color, "#FFD24A");
                bar->setPropA(abi::PROP_OpacityRelativeMin, "1.00"); bar->setPropA(abi::PROP_Opacity, "1.00");
                for (const NodePtr& c : bar->children()) if (c->isA(T_Widget)) c->setVisible(false);
                bar->setVisible(true);
                focus_->addChild(bar); bar->link();
                ringBar_[i] = bar;
            }
        }
    }
    // No pointer arrow: the atlas is full and sprite 12 is the D-pad, so a pointer
    // would draw that. The frame leads instead.
    pointerInit_ = false;
}

void Cursor::detachVisuals()
{
    // The bars are the frame's children and go with it; the pointers are nulled so
    // nothing refers to demolished widgets.
    if (overlay_) { if (pointer_) overlay_->removeChild(pointer_); if (focus_) overlay_->removeChild(focus_); }
    for (int i = 0; i < 4; ++i) ringBar_[i].reset();
    overlay_.reset(); pointer_.reset(); focus_.reset(); ringOver_.reset();
}

void Cursor::updateVisuals(float dt)
{
    const NodeInfo* n = current();
    if (!n) { if (pointer_) pointer_->setVisible(false); if (focus_) focus_->setVisible(false); ringOver_.reset(); return; }
    UIPoint ov = overlay_ ? overlay_->worldLocation() : UIPoint{0, 0};
    // The arrow aims at the node centre with an offset. A stump of the old formula used
    // to divide by zero here - the value was overwritten on the next line, but an
    // integer division by zero is a trap for nothing.
    float tx = static_cast<float>(n->center.x + cfg_.pointerOffset);
    float ty = static_cast<float>(n->center.y + cfg_.pointerOffset);
    if (!pointerInit_) { px_ = tx; py_ = ty; pointerInit_ = true; }
    else {
        // exponential approach: 1/travel of the distance per frame at 60 fps
        float k = 1.f - std::pow(1.f - 1.f / cfg_.travel, dt * 60.f);
        if (k > 1.f) k = 1.f;
        px_ += (tx - px_) * k; py_ += (ty - py_) * k;
        if (std::fabs(tx - px_) < 0.5f) px_ = tx; if (std::fabs(ty - py_) < 0.5f) py_ = ty;
    }
    if (pointer_) {
        int x = static_cast<int>(px_) - ov.x, y = static_cast<int>(py_) - ov.y;
        UIPoint l = pointer_->location(); if (l.x != x || l.y != y) pointer_->setLocation(x, y);
        pointer_->setVisible(true);
    }
    if (focus_) {
        if (overlay_ && (!ringOver_ || !ringOver_->sameAs(*n->node))) {
            overlay_->moveChild(focus_, 2);      // above the window the client raised
            ringOver_ = n->node;
        }
        int x = n->rect.left - ov.x - 2, y = n->rect.top - ov.y - 2, w = n->rect.right - n->rect.left + 4, h = n->rect.bottom - n->rect.top + 4;
        UIPoint l = focus_->location(); UISize s = focus_->size();
        if (l.x != x || l.y != y) focus_->setLocation(x, y);
        if (s.x != w || s.y != h) {
            char b[32]; std::snprintf(b, sizeof b, "%d,%d", w, h); focus_->setPropA(abi::PROP_ScrollExtent, b); focus_->setSize(w, h);
            // Bars: top, bottom, left, right, in the frame's own coordinates.
            const int t = ringThick_;
            const int geo[4][4] = {{0, 0, w, t}, {0, h - t, w, t}, {0, 0, t, h}, {w - t, 0, t, h}};
            for (int i = 0; i < 4; ++i) {
                if (!ringBar_[i]) continue;
                ringBar_[i]->setLocation(geo[i][0], geo[i][1]);
                std::snprintf(b, sizeof b, "%d,%d", geo[i][2], geo[i][3]);
                ringBar_[i]->setPropA(abi::PROP_ScrollExtent, b);
                ringBar_[i]->setSize(geo[i][2], geo[i][3]);
            }
        }
        focus_->setVisible(true);
    }
}

}} // namespace cp::cursor
