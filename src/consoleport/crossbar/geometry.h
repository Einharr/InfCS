// The crossbar geometry, 7 Sep 2026 model.
//
// At rest only the main cells of the two crosses and L3/R3 show. A cross is a plus of
// four cells round a centre with a glyph in the middle (one dpad icon for the D-pad, the
// four faces as a single picture). The backdrop lies under the crosses and the L3/R3
// block only.
//
// The flyouts (L2, L2+R2, R2) are hidden at rest and appear on hover or mid-drag,
// outside the backdrop along their own cell's direction: three in a row above the top
// cell, below the bottom one, left of the left one, right of the right one.
//
// Holding a modifier opens no flyout at all - the whole crossbar is swapped for that
// set, with the modifier's glyph centred under the bar.
//
// Spacing between blocks comes from the MAXIMUM, not the sum: only one flyout is ever
// open, so the corridor between a cross and the L3/R3 block has to fit one, not two
// facing each other. Reserving for both made the bar a quarter wider than the screen.
#pragma once
#include "../core/types.h"
#include "../gen/profiles.h"

namespace cp { namespace crossbar {

struct Metrics {
    int main = 44;        // a cross cell
    // No shorter than a cell width, or the plus overlaps at the corners and the centre
    // has no room for the glyph.
    int arm = 46;         // cross center -> main cell center
    int pad = 5;          // the backdrop's margin around the cross
    int fly = 30;         // a flyout cell
    int fanGap = 8;       // the gap from the backdrop's edge to the flyout
    int fanStep = 34;     // the step between flyout cells
    int crossGap = 10;    // the gap between a cross and the L3/R3 block
    int extra = 36;       // an L3/R3 cell
    int extraGap = 6;     // between L3 and R3
    int extraGlyph = 20;  // the L3/R3 glyph sits above its own cell
    int glyph = 28;       // the glyph at the cross's center
    int modGlyph = 28;    // the modifier glyph under the bar
    int fanBadge = 16;    // the layer glyph (L2 / R2) on a flyout cell
    int modGap = 6;       // the modifier glyph's offset from the bottom of the crosses

    // half of the square the cross occupies, flyout space included
    int crossHalf() const { return arm + main / 2 + pad + fanGap + fly; }
    // cell centre to flyout cell centre: pad to the backdrop's edge, then the gap and
    // half a flyout cell. One formula for the crosses and the L3/R3 block alike.
    int fanOut() const { return main / 2 + pad + fanGap + fly / 2; }
    // half the side of a cross's backdrop
    int frameHalf() const { return arm + main / 2 + pad; }
};

struct BarLayout {
    UIPoint crossCenter[gen::CROSS_COUNT];
    UIRect  crossFrame[gen::CROSS_COUNT];
    UIRect  extraFrame;
    UIPoint extraCenter[gen::EXTRA_COUNT];
    UIPoint extraGlyphAt[gen::EXTRA_COUNT];   // the L3/R3 glyph above its own cell
    UIPoint modGlyphAt;                       // the center of the modifier glyph
    UISize  size;
};

BarLayout barLayout(const Metrics& m);

// The flyout's direction vector and the axis it lays out along: up/down flyouts run
// horizontally, left/right ones vertically.
void fanAxes(gen::Fan fan, int& dx, int& dy, int& px, int& py);

// Rectangles in bar coordinates, top left = 0,0. mainRect handles both the crosses and
// the L3/R3 block (cross == EXTRA_CROSS).
UIRect mainRect(const BarLayout& bl, const Metrics& m, int cluster);
UIRect fanRect(const BarLayout& bl, const Metrics& m, int cluster, int i);  // i: 0 L2, 1 L2+R2, 2 R2
UIRect extraRect(const BarLayout& bl, const Metrics& m, int extra);
// index inside the L3/R3 block (0 or 1), -1 if the cluster is not in it
int extraIndexOf(int cluster);

inline UIRect rectAt(UIPoint c, int size)
{
    int h = size / 2;
    UIRect r = {c.x - h, c.y - h, c.x - h + size, c.y - h + size};
    return r;
}

}} // namespace cp::crossbar
