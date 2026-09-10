#include "hints.h"
#include "../abi/props.h"
#include "../crossbar/crossbar.h"
#include <cstdio>

namespace cp { namespace cursor {

void navigateHints(std::vector<Hint>& out)
{
    out.push_back(Hint{gen::B_DPAD_UP, u"", crossbar::Glyph::DPAD});
    out.push_back(Hint{gen::B_L3,      u"", crossbar::Glyph::STICK_L});
    out.push_back(Hint{gen::B_R3,      u"Navigate", crossbar::Glyph::STICK_R});
}

void HintBar::attach(NodePtr overlay, NodePtr imageTemplate, NodePtr textTemplate, int x, int y)
{
    detach(); overlay_ = overlay; imageTpl_ = imageTemplate; textTpl_ = textTemplate; x_ = x; y_ = y;
    rebuild();
}

void HintBar::detach()
{
    if (overlay_) for (Item& i : items_) { if (i.glyph) overlay_->removeChild(i.glyph); if (i.text) overlay_->removeChild(i.text); }
    items_.clear(); overlay_.reset(); imageTpl_.reset(); textTpl_.reset(); width_ = 0;
}

void HintBar::set(const std::vector<Hint>& hints)
{
    bool same = hints.size() == hints_.size();
    for (size_t i = 0; same && i < hints.size(); ++i) same = hints[i].button == hints_[i].button && hints[i].text == hints_[i].text;
    if (same) return;
    hints_ = hints; rebuild();
}

void HintBar::setBackdrop(NodePtr tpl, int pad) { backdropTpl_ = tpl; backdropPad_ = pad; rebuild(); }

void HintBar::raise()
{
    if (!overlay_) return;
    // The backdrop is raised FIRST so the glyphs and text land on top of it.
    if (backdrop_) overlay_->moveChild(backdrop_, 2);
    for (Item& i : items_) {
        if (i.glyph) overlay_->moveChild(i.glyph, 2);
        if (i.text) overlay_->moveChild(i.text, 2);
    }
}

void HintBar::rebuild()
{
    if (!overlay_) return;
    for (Item& i : items_) { if (i.glyph) overlay_->removeChild(i.glyph); if (i.text) overlay_->removeChild(i.text); }
    if (backdrop_) { overlay_->removeChild(backdrop_); backdrop_.reset(); }
    items_.clear(); width_ = 0;
    int x = x_; char b[48];
    for (size_t k = 0; k < hints_.size(); ++k) {
        Item it;
        if (imageTpl_) {
            it.glyph = imageTpl_->clone();
            if (it.glyph) {
                std::snprintf(b, sizeof b, "%sG%d", prefix_, static_cast<int>(k)); it.glyph->setPropA(abi::PROP_Name, b);
                UIRect r = crossbar::Glyph::rect(hints_[k].glyph >= 0 ? hints_[k].glyph
                                                                        : (int)crossbar::Glyph::forButton(hints_[k].button));
                it.glyph->setPropA(abi::PROP_SourceResource, crossbar::Glyph::atlas());
                std::snprintf(b, sizeof b, "%d,%d,%d,%d", r.left, r.top, r.right, r.bottom); it.glyph->setPropA(abi::PROP_SourceRect, b);
                it.glyph->setPropA(abi::PROP_GetsInput, "false"); it.glyph->setPropA(abi::PROP_PackLocation, "nfn,nfn"); it.glyph->setPropA(abi::PROP_PackSize, "f,f");
                std::snprintf(b, sizeof b, "%d,%d", glyph_, glyph_); it.glyph->setPropA(abi::PROP_ScrollExtent, b);
                it.glyph->setSize(glyph_, glyph_); it.glyph->setLocation(x, y_); it.glyph->setVisible(true);
                overlay_->addChild(it.glyph); it.glyph->link(); overlay_->moveChild(it.glyph, 2);
            }
        }
        x += glyph_ + gap_;
        int tw = hints_[k].text.empty() ? 0 : static_cast<int>(hints_[k].text.size()) * charW_ + 4;
        if (textTpl_ && tw > 0) {
            it.text = textTpl_->clone();
            if (it.text) {
                std::snprintf(b, sizeof b, "%sT%d", prefix_, static_cast<int>(k)); it.text->setPropA(abi::PROP_Name, b);
                it.text->setPropA(abi::PROP_GetsInput, "false"); it.text->setPropA(abi::PROP_PackLocation, "nfn,nfn"); it.text->setPropA(abi::PROP_PackSize, "f,f");
                it.text->setPropA(abi::PROP_BackgroundOpacity, "0.00"); it.text->setPropA(abi::PROP_TextAlignment, "Left"); it.text->setPropA(abi::PROP_Font, "bold_12");
                std::snprintf(b, sizeof b, "%d,%d", tw, glyph_); it.text->setPropA(abi::PROP_ScrollExtent, b);
                it.text->setSize(tw, glyph_); it.text->setLocation(x, y_); it.text->setLocalText(hints_[k].text.c_str()); it.text->setVisible(true);
                overlay_->addChild(it.text); it.text->link(); overlay_->moveChild(it.text, 2);
            }
        }
        x += tw + pad_;
        items_.push_back(it);
    }
    width_ = x - x_;

    // The backdrop is built after the elements, from the finished width. raise() fixes
    // the order: backdrop first, glyphs and text after, so it stays underneath.
    if (backdropTpl_ && !items_.empty()) {
        backdrop_ = backdropTpl_->clone();
        if (backdrop_) {
            const int w = contentWidth() + backdropPad_ * 2, h = glyph_ + backdropPad_ * 2;
            std::snprintf(b, sizeof b, "%sBg", prefix_); backdrop_->setPropA(abi::PROP_Name, b);
            backdrop_->setPropA(abi::PROP_GetsInput, "false"); backdrop_->setPropA(abi::PROP_AbsorbsInput, "false");
            backdrop_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); backdrop_->setPropA(abi::PROP_PackSize, "f,f");
            backdrop_->setPropA(abi::PROP_BackgroundOpacity, "0.90"); backdrop_->setPropA(abi::PROP_Opacity, "1.00");
            // The template is a toolbar corner and carries the pane number and arrows;
            // muted as the cursor frame does it.
            for (const NodePtr& c : backdrop_->children()) if (c->isA(T_Widget)) c->setVisible(false);
            std::snprintf(b, sizeof b, "%d,%d", w, h); backdrop_->setPropA(abi::PROP_ScrollExtent, b);
            backdrop_->setSize(w, h);
            backdrop_->setLocation(x_ - backdropPad_, y_ - backdropPad_);
            backdrop_->setVisible(true);
            overlay_->addChild(backdrop_); backdrop_->link();
        }
    }
    raise();
}

void HintBar::moveTo(int x, int y)
{
    if (x == x_ && y == y_) return;
    const int dx = x - x_, dy = y - y_;
    x_ = x; y_ = y;
    if (backdrop_) { UIPoint l = backdrop_->location(); backdrop_->setLocation(l.x + dx, l.y + dy); }
    for (Item& i : items_) {
        if (i.glyph) { UIPoint l = i.glyph->location(); i.glyph->setLocation(l.x + dx, l.y + dy); }
        if (i.text)  { UIPoint l = i.text->location();  i.text->setLocation(l.x + dx, l.y + dy); }
    }
}

void HintBar::tick(float) {}

}} // namespace cp::cursor
