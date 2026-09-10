#include "geometry.h"

namespace cp { namespace crossbar {

using namespace gen;

void fanAxes(Fan fan, int& dx, int& dy, int& px, int& py)
{
    switch (fan) {
        case FAN_UP:    dx = 0;  dy = -1; px = 1; py = 0; break;   // flyout horizontally above the cell
        case FAN_DOWN:  dx = 0;  dy = 1;  px = 1; py = 0; break;   // horizontally below it
        case FAN_LEFT:  dx = -1; dy = 0;  px = 0; py = 1; break;   // in a column to the left
        default:        dx = 1;  dy = 0;  px = 0; py = 1; break;   // FAN_RIGHT - in a column to the right
    }
}

BarLayout barLayout(const Metrics& m)
{
    BarLayout bl;
    const int half = m.crossHalf();
    const int centerW = 2 * m.extra + m.extraGap + 2 * m.pad;
    // the L3/R3 block keeps a row above the cell for the button glyph
    const int centerH = m.extraGlyph + 2 + m.extra + 2 * m.pad;

    // D-pad left, the L3/R3 block in the middle, face buttons right.
    //
    // The corridor between neighbouring blocks has to fit ONE flyout - only one is ever
    // open (reveal_ is a single cluster) - so it is the maximum of the two facing
    // requirements, not their sum:
    //   a cross's side flyout reaches crossHalf from the cross centre and must not climb
    //   onto the block's backdrop (half of it is centerW/2);
    //   the L3/R3 flyout reaches extraReach from the block's centre and must not climb
    //   onto the cross's backdrop (half of it is frameHalf).
    const int extraHalfCell = m.extra / 2 + m.extraGap / 2;   // block center -> L3/R3 center
    const int extraReach = extraHalfCell + m.fanOut() + m.fly / 2;
    const int a = half + centerW / 2;
    const int b = m.frameHalf() + extraReach;
    const int step = (a > b ? a : b) + m.crossGap;             // cross center -> block center
    const int cx0 = half;
    const int cxc = cx0 + step;
    const int cx1 = cxc + step;
    const int cy = half;

    bl.crossCenter[0] = UIPoint{cx0, cy};
    bl.crossCenter[1] = UIPoint{cx1, cy};
    for (int c = 0; c < CROSS_COUNT; ++c) {
        const UIPoint p = bl.crossCenter[c];
        const int fh = m.frameHalf();
        bl.crossFrame[c] = UIRect{p.x - fh, p.y - fh, p.x + fh, p.y + fh};
    }

    bl.extraFrame = UIRect{cxc - centerW / 2, cy - centerH / 2, cxc - centerW / 2 + centerW, cy - centerH / 2 + centerH};
    const int ex = cxc - (m.extra + m.extraGap / 2);
    const int ecy = bl.extraFrame.bottom - m.pad - m.extra / 2;          // the cells hug the bottom of the block
    const int egy = bl.extraFrame.top + m.pad + m.extraGlyph / 2;        // the glyphs one row higher
    bl.extraCenter[0] = UIPoint{ex + m.extra / 2, ecy};
    bl.extraCenter[1] = UIPoint{ex + m.extra + m.extraGap + m.extra / 2, ecy};
    for (int e = 0; e < EXTRA_COUNT; ++e) bl.extraGlyphAt[e] = UIPoint{bl.extraCenter[e].x, egy};

    // The bar is sized from the actual extent of what we draw - cells, flyouts,
    // backdrops. A formula was a mistake: it outlived the L3/R3 flyouts turning from
    // downwards to sideways and kept reserving fifty pixels at the bottom.
    int right = 0, bottom = 0;
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        const UIRect mr = mainRect(bl, m, k);
        if (mr.right > right) right = mr.right;
        if (mr.bottom > bottom) bottom = mr.bottom;
        for (int i = 0; i < 3; ++i) {
            const UIRect fr = fanRect(bl, m, k, i);
            if (fr.right > right) right = fr.right;
            if (fr.bottom > bottom) bottom = fr.bottom;
        }
    }
    for (int c = 0; c < CROSS_COUNT; ++c) {
        if (bl.crossFrame[c].right > right) right = bl.crossFrame[c].right;
        if (bl.crossFrame[c].bottom > bottom) bottom = bl.crossFrame[c].bottom;
    }
    if (bl.extraFrame.right > right) right = bl.extraFrame.right;
    if (bl.extraFrame.bottom > bottom) bottom = bl.extraFrame.bottom;

    // The modifier glyph goes under the backdrops, not under the whole extent: a flyout
    // and the glyph are mutually exclusive, since a held modifier opens no flyouts.
    // Reserving below the flyout band is what left the fifty pixel gap.
    int decorBottom = bl.extraFrame.bottom;
    for (int c = 0; c < CROSS_COUNT; ++c)
        if (bl.crossFrame[c].bottom > decorBottom) decorBottom = bl.crossFrame[c].bottom;
    bl.modGlyphAt = UIPoint{right / 2, decorBottom + m.modGap + m.modGlyph / 2};

    const int glyphBottom = bl.modGlyphAt.y + m.modGlyph / 2;
    bl.size = UISize{right, bottom > glyphBottom ? bottom : glyphBottom};
    return bl;
}

int extraIndexOf(int cluster)
{
    if (CLUSTERS[cluster].cross != EXTRA_CROSS) return -1;
    int n = 0;
    for (int k = 0; k < cluster; ++k) if (CLUSTERS[k].cross == EXTRA_CROSS) ++n;
    return n;
}

UIRect mainRect(const BarLayout& bl, const Metrics& m, int cluster)
{
    const Cluster& c = CLUSTERS[cluster];
    const int e = extraIndexOf(cluster);
    if (e >= 0) return extraRect(bl, m, e);          // L3/R3 sit side by side, not as a plus
    int dx, dy, px, py; fanAxes(c.fan, dx, dy, px, py);
    const UIPoint cc = bl.crossCenter[c.cross];
    return rectAt(UIPoint{cc.x + dx * m.arm, cc.y + dy * m.arm}, m.main);
}

UIRect fanRect(const BarLayout& bl, const Metrics& m, int cluster, int i)
{
    // Measured from the cell's own centre, not the cross's: one formula then covers the
    // L3/R3 block too, which has no cross at all.
    const Cluster& c = CLUSTERS[cluster];
    int dx, dy, px, py; fanAxes(c.fan, dx, dy, px, py);
    const UIRect mr = mainRect(bl, m, cluster);
    const UIPoint mc = {(mr.left + mr.right) / 2, (mr.top + mr.bottom) / 2};
    const int d = m.fanOut(), s = (i - 1) * m.fanStep;
    return rectAt(UIPoint{mc.x + dx * d + px * s, mc.y + dy * d + py * s}, m.fly);
}

UIRect extraRect(const BarLayout& bl, const Metrics& m, int extra)
{
    return rectAt(bl.extraCenter[extra], m.extra);
}

}} // namespace cp::crossbar
