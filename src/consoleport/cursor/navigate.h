// Picking a node by direction, after ConsolePortNode's NavigateToBestCandidate:
//   1) a 45 degree cone along the direction, metric L1 with a penalty for going sideways
//   2) nothing in the cone: any half-plane ahead
//   3) nothing ahead: wrap to the furthest node on the opposite side, preferably on the
//      same line
#pragma once
#include "nodes.h"

namespace cp { namespace cursor {

enum Dir { DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };

// the index of the best candidate, or -1
int bestInDirection(const std::vector<NodeInfo>& nodes, int cur, Dir d, bool wrap);
int closestTo(const std::vector<NodeInfo>& nodes, UIPoint p);
// the starting node: the previous one, then the old one, then the closest to the point,
// then the highest priority
int arbitrary(const std::vector<NodeInfo>& nodes, const NodePtr& prev, const NodePtr& old, UIPoint center);
int indexOf(const std::vector<NodeInfo>& nodes, const NodePtr& n);

}} // namespace cp::cursor
