#include "ringdecor.h"
#include "../core/theme.h"
#include "../abi/props.h"
#include "../core/runtime.h"
#include <cstdio>

namespace cp { namespace world {

namespace {
// Fractions of the ring radius: the backdrop wider than the labels, the wedge covering
// them, the rim exactly on the radius. Proportions off the 9 Sep 2026 mockup.
const float DISC_K = 1.32f, RIM_K = 1.06f, WEDGE_K = 1.18f;
const float FOLLOW_MS = 70.f;        // the wedge takes this long to reach a sector
const float FADE_MS = 120.f;

const char* TEX_DISC = "consoleport_ring";
const char* TEX_RIM  = "consoleport_rim";
const char* TEX_WEDGE = "consoleport_wedge";
// Palette entry names rather than numbers, so the rings follow the theme. Roles as in
// the stock markup: back1 a panel background, line1 outlines, highlight a selection
// (1631 / 1523 / 249 uses across the client markup).
const char* PAL_DISC = "back1";
const char* PAL_RIM  = "line1";
const char* PAL_WEDGE = "highlight";
}

bool RingDecor::attach(NodePtr overlay, NodePtr imageTemplate)
{
    detach();
    overlay_ = overlay; imageTpl_ = imageTemplate;
    return attached();
}

void RingDecor::detach()
{
    hide();
    overlay_.reset(); imageTpl_.reset();
}

NodePtr RingDecor::sprite(const char* name, const char* texture, const char* color, const char* opacity, int size)
{
    if (!imageTpl_ || !overlay_) return NodePtr();
    NodePtr n = imageTpl_->clone(); if (!n) return NodePtr();
    char b[48];
    n->setPropA(abi::PROP_Name, name);
    n->setPropA(abi::PROP_SourceResource, texture);
    n->setPropA(abi::PROP_SourceRect, "0,0,256,256");
    // Two forms: a palette entry name ("line1"), which follows the theme, or a number
    // ("#D84E58"), which is how the runtime paints the rim per ring - a number needs no
    // palette and is meant to override the theme.
    theme::paint(n, theme::ROLE_COLOR, color);
    n->setPropA(abi::PROP_Opacity, opacity);
    n->setPropA(abi::PROP_GetsInput, "false"); n->setPropA(abi::PROP_AbsorbsInput, "false");
    n->setPropA(abi::PROP_PackLocation, "nfn,nfn"); n->setPropA(abi::PROP_PackSize, "f,f");
    std::snprintf(b, sizeof b, "%d,%d", size, size); n->setPropA(abi::PROP_ScrollExtent, b);
    n->setSize(size, size);
    n->setPropA(abi::PROP_Visible, "true");
    overlay_->addChild(n); n->link(); overlay_->moveChild(n, 2);
    return n;
}

void RingDecor::show(int radius, const char* rim)
{
    if (!attached() || radius <= 0) return;
    hide();
    radius_ = radius;
    // Creation order is layer order: each moveChild(..., Top) lands higher. The caller's
    // labels come after, so they end up above the decor.
    disc_ = sprite("cpDecorDisc", TEX_DISC, PAL_DISC, "0.85", static_cast<int>(radius * 2 * DISC_K));
    rim_  = sprite("cpDecorRim", TEX_RIM, rim ? rim : PAL_RIM, "0.55", static_cast<int>(radius * 2 * RIM_K));
    // The wedge stays hidden until the first deflection - an empty ring must not glow
    // on a random sector.
    wedge_ = sprite("cpDecorWedge", TEX_WEDGE, PAL_WEDGE, "0.00", static_cast<int>(radius * 2 * WEDGE_K));
    alpha_ = 0.f; turnPainted_ = -1;

    const UISize screen = overlay_->size();
    const int cx = screen.x / 2, cy = screen.y / 2;
    struct { NodePtr node; float k; } layers[3] = {{disc_, DISC_K}, {rim_, RIM_K}, {wedge_, WEDGE_K}};
    for (int i = 0; i < 3; ++i) {
        if (!layers[i].node) continue;
        const int side = static_cast<int>(radius * 2 * layers[i].k);
        layers[i].node->setLocation(cx - side / 2, cy - side / 2);
    }
}

void RingDecor::hide()
{
    if (overlay_) {
        if (disc_) overlay_->removeChild(disc_);
        if (rim_) overlay_->removeChild(rim_);
        if (wedge_) overlay_->removeChild(wedge_);
    }
    disc_.reset(); rim_.reset(); wedge_.reset();
    alpha_ = 0.f; turnPainted_ = -1;
}

void RingDecor::update(int sector, int count, float dt)
{
    if (!wedge_ || count <= 0) return;

    if (sector >= 0) {
        const float target = static_cast<float>(sector) / static_cast<float>(count);
        float d = target - turn_;
        while (d > 0.5f) d -= 1.f;          // the shortest arc: otherwise the wedge drives all the way around
        while (d < -0.5f) d += 1.f;
        const float k = FOLLOW_MS <= 0.f ? 1.f : (dt * 1000.f / FOLLOW_MS);
        turn_ += d * (k > 1.f ? 1.f : k);
        if (turn_ < 0.f) turn_ += 1.f;
        if (turn_ >= 1.f) turn_ -= 1.f;
    }
    const float wantAlpha = sector >= 0 ? 1.f : 0.f;
    const float ka = dt * 1000.f / FADE_MS;
    alpha_ += (wantAlpha - alpha_) * (ka > 1.f ? 1.f : ka);

    // Properties are touched only when the picture really changed: setProperty parses
    // the string inside the client.
    const int turns = static_cast<int>(turn_ * 1000.f + 0.5f);
    if (turns != turnPainted_) {
        turnPainted_ = turns;
        char b[24]; std::snprintf(b, sizeof b, "%.3f", turn_);
        wedge_->setPropA(abi::PROP_Rotation, b);
    }
    char o[16]; std::snprintf(o, sizeof o, "%.2f", alpha_ * 0.9f);
    wedge_->setPropA(abi::PROP_Opacity, o);
}

}} // namespace cp::world
