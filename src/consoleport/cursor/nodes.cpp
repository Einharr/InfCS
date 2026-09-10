#include "nodes.h"
#include <algorithm>

namespace cp { namespace cursor {

bool ScanRules::wantsName(const std::string& n) const
{
    return std::find(ignore.begin(), ignore.end(), n) == ignore.end();
}

// UITable is a list like UIList: the widget draws the rows, they have no child widgets,
// and it walks them on the arrow keys by focus. Recognized by TYPE NAME, not by the
// numeric tag - the sources' UITypeID is shifted relative to this binary (Pie, Runner,
// WebBrowser may not be in it) while GetTypeName comes from the widget itself. On the
// live client, 9 Sep 2026, the character list reports "Table".
static bool isTable(const Node& n) { return n.typeName() == "Table"; }

NodeKind kindOf(const Node& n, const NodePtr& parent)
{
    if (parent && parent->isA(T_VolumePage)) return NK_Cell;
    if (n.isA(T_Checkbox)) return NK_Checkbox;
    if (n.isA(T_Button)) return parent && parent->isA(T_TabSet) ? NK_Tab : NK_Button;
    if (n.isA(T_Textbox)) return NK_Textbox;
    if (n.isA(T_Dropdownbox) || n.isA(T_ComboBox)) return NK_Dropdown;
    if (n.isA(T_Sliderbar)) return NK_Slider;
    if (n.isA(T_List) || n.isA(T_Listbox) || n.isA(T_TreeView) || isTable(n)) return NK_List;
    return NK_Generic;
}

static bool isLeafInteractive(const Node& n)
{
    return n.isA(T_Button) || n.isA(T_Checkbox) || n.isA(T_Textbox) || n.isA(T_Dropdownbox) || n.isA(T_ComboBox)
        || n.isA(T_Sliderbar) || n.isA(T_List) || n.isA(T_Listbox) || n.isA(T_TreeView) || isTable(n);
}

static void scanRec(const NodePtr& n, const NodePtr& parent, int depth, const ScanRules& r, std::vector<NodeInfo>& out)
{
    if (!n || depth > r.maxDepth || static_cast<int>(out.size()) >= r.maxNodes) return;
    if (!n->willDraw()) return;
    std::string name = n->name();
    if (!r.wantsName(name)) return;
    bool cell = parent && parent->isA(T_VolumePage) && n->isA(T_Widget);
    if (cell || isLeafInteractive(*n)) {
        // Leaves are judged by us, not by the client's CanSelect: in game on 8 Sep 2026
        // it reported 16 inventory cells and not one button, though they are visible and
        // pressable. Our test: drawn, enabled, takes input.
        const bool ok = cell ? n->enabled()
                             : (n->canSelect() || (n->enabled() && n->getsInput()));
        if (ok) {
            NodeInfo i; i.node = n; i.rect = n->worldRect(); i.kind = kindOf(*n, parent);
            i.center = UIPoint{(i.rect.left + i.rect.right) / 2, (i.rect.top + i.rect.bottom) / 2};
            i.container = cell ? parent : NodePtr(); i.name = name;
            std::map<std::string, int>::const_iterator p = r.priority.find(name); i.priority = p == r.priority.end() ? 0 : p->second;
            if (i.rect.right > i.rect.left && i.rect.bottom > i.rect.top) out.push_back(i);
        }
        if (!n->isA(T_Page)) return;       // a button or a list is a leaf
        if (cell) return;
    }
    if (std::find(r.pass.begin(), r.pass.end(), name) != r.pass.end()) return;
    if (!n->isA(T_Page)) return;
    std::vector<NodePtr> kids = n->children();
    // children in draw order (the first ones on top): for navigation the order does not matter
    for (const NodePtr& k : kids) scanRec(k, n, depth + 1, r, out);
}

std::vector<NodeInfo> scanNodes(const NodePtr& root, const ScanRules& rules)
{
    std::vector<NodeInfo> out;
    if (!root || !root->willDraw()) return out;
    for (const NodePtr& k : root->children()) scanRec(k, root, 1, rules, out);
    return out;
}

}} // namespace cp::cursor
