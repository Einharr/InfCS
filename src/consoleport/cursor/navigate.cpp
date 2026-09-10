#include "navigate.h"
#include <climits>
#include <cstdlib>

namespace cp { namespace cursor {

int indexOf(const std::vector<NodeInfo>& nodes, const NodePtr& n)
{
    if (!n) return -1;
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].node->sameAs(*n)) return static_cast<int>(i);
    return -1;
}

static void axes(Dir d, int& dx, int& dy) { dx = d == DIR_LEFT ? -1 : d == DIR_RIGHT ? 1 : 0; dy = d == DIR_UP ? -1 : d == DIR_DOWN ? 1 : 0; }

// the projection of the cur->cand vector onto the direction (along) and across it (perp, absolute)
static void project(const NodeInfo& a, const NodeInfo& b, Dir d, int& along, int& perp)
{
    int dx, dy; axes(d, dx, dy);
    int vx = b.center.x - a.center.x, vy = b.center.y - a.center.y;
    along = vx * dx + vy * dy;
    perp = std::abs(dx ? vy : vx);
}

// the overlap of the rectangles on the axis across the direction (the same row or column)
static bool sameLine(const NodeInfo& a, const NodeInfo& b, Dir d)
{
    if (d == DIR_UP || d == DIR_DOWN) return a.rect.left < b.rect.right && b.rect.left < a.rect.right;
    return a.rect.top < b.rect.bottom && b.rect.top < a.rect.bottom;
}

int bestInDirection(const std::vector<NodeInfo>& nodes, int cur, Dir d, bool wrap)
{
    if (cur < 0 || cur >= static_cast<int>(nodes.size())) return -1;
    const NodeInfo& c = nodes[cur];
    int best = -1; long bestScore = LONG_MAX;
    // 1) a 45 degree cone: perp <= along, the straighter the better
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (static_cast<int>(i) == cur) continue;
        int along, perp; project(c, nodes[i], d, along, perp);
        if (along <= 0 || perp > along) continue;
        long score = static_cast<long>(along) + 2L * perp - (sameLine(c, nodes[i], d) ? along / 2 : 0);
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    if (best >= 0) return best;
    // 2) ahead but outside the cone - up to 2:1 sideways (past that it is "the wrong way", wrap is better)
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (static_cast<int>(i) == cur) continue;
        int along, perp; project(c, nodes[i], d, along, perp);
        if (along <= 0 || perp > 2 * along + 16) continue;
        long score = static_cast<long>(along) + 3L * perp;
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    if (best >= 0 || !wrap) return best;
    // 3) wrap: the furthest one behind, on the same line if there is one
    long bestFar = -1; int farLine = -1, farAny = -1; long farAnyScore = LONG_MAX;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (static_cast<int>(i) == cur) continue;
        int along, perp; project(c, nodes[i], d, along, perp);
        if (along >= 0) continue;
        if (sameLine(c, nodes[i], d)) { if (-along > bestFar) { bestFar = -along; farLine = static_cast<int>(i); } }
        long score = static_cast<long>(perp) * 4 - along;   // closer to the line matters more than distance
        if (score < farAnyScore) { farAnyScore = score; farAny = static_cast<int>(i); }
    }
    return farLine >= 0 ? farLine : farAny;
}

int closestTo(const std::vector<NodeInfo>& nodes, UIPoint p)
{
    int best = -1; long bestD = LONG_MAX;
    for (size_t i = 0; i < nodes.size(); ++i) {
        long dx = nodes[i].center.x - p.x, dy = nodes[i].center.y - p.y; long dd = dx * dx + dy * dy;
        if (dd < bestD) { bestD = dd; best = static_cast<int>(i); }
    }
    return best;
}

int arbitrary(const std::vector<NodeInfo>& nodes, const NodePtr& prev, const NodePtr& old, UIPoint center)
{
    if (nodes.empty()) return -1;
    int i = indexOf(nodes, prev); if (i >= 0) return i;
    i = indexOf(nodes, old); if (i >= 0) return i;
    int maxP = 0; int byP = -1;
    for (size_t k = 0; k < nodes.size(); ++k) if (nodes[k].priority > maxP) { maxP = nodes[k].priority; byP = static_cast<int>(k); }
    if (byP >= 0) return byP;
    return closestTo(nodes, center);
}

}} // namespace cp::cursor
