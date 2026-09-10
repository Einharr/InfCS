// Collecting navigation nodes out of a window tree - our ConsolePortNode. A node is a
// selectable widget (WillDraw, Enabled, GetsInput) of an interactive type; cells are the
// children of a UIVolumePage. ConsolePort's nodeignore / nodepass / nodepriority come
// from the window profile by name - markup has no user attributes without a hook.
#pragma once
#include "../core/node.h"
#include <map>
#include <string>
#include <vector>

namespace cp { namespace cursor {

enum NodeKind { NK_Generic, NK_Button, NK_Checkbox, NK_Cell, NK_Textbox, NK_Dropdown, NK_Slider, NK_List, NK_Tab };

struct NodeInfo {
    NodePtr node;
    UIRect  rect;          // the screen rectangle
    UIPoint center;
    NodeKind kind;
    NodePtr container;     // the volume for a cell, otherwise empty
    int priority;          // higher means picked first on entering the window
    std::string name;
};

struct ScanRules {
    std::vector<std::string> ignore;      // nodeignore: names of nodes we skip
    std::vector<std::string> pass;        // nodepass: pages we do not descend into
    std::map<std::string, int> priority;  // nodepriority by name
    int maxDepth = 14;
    int maxNodes = 512;
    bool wantsName(const std::string& n) const;
};

// Walk the subtree of root; hidden branches and nodepass are skipped.
std::vector<NodeInfo> scanNodes(const NodePtr& root, const ScanRules& rules);

NodeKind kindOf(const Node& n, const NodePtr& parent);

}} // namespace cp::cursor
