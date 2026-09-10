#include "inventory.h"
#include "../core/runtime.h"

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

Inventory::Inventory(NodePtr root, const char* path) : root_(root), path_(path)
{
    rules_.ignore.push_back("buttonExit");        // the samples inside the volume
    rules_.ignore.push_back("checkPopupHelp");
    rules_.pass.push_back("PopupHelp");
    rules_.priority["volume"] = 5;
}

NodePtr Inventory::page() const
{
    // The page is taken by path every time, with no cache. A cache cost us a crash
    // (9 Sep 2026, ACCESS VIOLATION while closing the client): the remembered widget's
    // liveness was checked through cached_->parent(), i.e. a read through a pointer the
    // client had already freed. A freed pointer cannot be checked, only not kept. The
    // price is one GetObjectFromPath over the root per frame.
    return root_ ? root_->byPath(path_.c_str()) : NodePtr();
}

std::vector<Hint> Inventory::hints(const Context& c) const
{
    std::vector<Hint> h;
    const NodeInfo* n = c.cursor.current();
    bool cell = n && n->kind == NK_Cell;
    if (c.cursor.dragging()) {
        h.push_back(Hint{B_TRIANGLE, u"Drop"}); h.push_back(Hint{B_CIRCLE, u"Cancel"});
        return h;
    }
    navigateHints(h);
    h.push_back(Hint{B_CROSS, cell ? u"Use" : u"Press"});
    if (cell) { h.push_back(Hint{B_SQUARE, u"Menu"}); h.push_back(Hint{B_TRIANGLE, u"Pick up"}); }
    h.push_back(Hint{B_CIRCLE, u"Close"});
    h.push_back(Hint{B_L1, u"Up"}); h.push_back(Hint{B_R1, u"View"});
    return h;
}

bool Inventory::clickNamed(Context& c, const char* rel)
{
    NodePtr p = page(); if (!p) return false;
    NodePtr b = p->byPath(rel);
    if (!b) { tracef("inventory: %s - no such node", rel); return false; }
    // canSelect() is no use here: on 9 Sep 2026 it rejected the live buttonView, which
    // has visible, enabled and takes-input all raised. A disabled button is filtered by
    // UIButton::Press anyway - it checks IsEnabled itself.
    if (!b->enabled() || !b->willDraw()) {
        tracef("inventory: %s unavailable (visible=%d enabled=%d)", rel, (int)b->willDraw(), (int)b->enabled());
        return false;
    }
    if (b->press()) { tracef("inventory: %s (%s) pressed", rel, b->typeName().c_str()); return true; }
    UIPoint ctr = b->center();
    const bool took = c.ui.click(ctr.x, ctr.y);
    tracef("inventory: %s (%s) with the mouse at %d,%d -> UIManager %s",
           rel, b->typeName().c_str(), ctr.x, ctr.y, took ? "took it" : "did NOT take it");
    return took;
}

bool Inventory::onButton(Context& c, Button b, bool pressed)
{
    if (!pressed) return false;
    const NodeInfo* n = c.cursor.current();
    switch (b) {
    case B_CROSS:
        if (c.cursor.dragging()) { c.cursor.dragDrop(); return true; }
        if (n && n->kind == NK_Cell) c.cursor.doubleClick(); else c.cursor.click();
        return true;
    case B_SQUARE:
        if (n && n->kind == NK_Cell) {
            // The item radial opens on a ContextRequest, and whether that goes through
            // is still open: the client takes no mouse messages in game mode, but with a
            // window open the pointer is active and it may differ. So we print
            // UIManager's verdict instead of guessing.
            const bool ctx = c.cursor.context();
            // A real right button does reach the client (the left one works) and it
            // makes the context request itself - unlike a bare ContextRequest, which
            // UIManager rejected on 9 Sep 2026.
            const bool rmb = ctx ? false : c.cursor.rightClick();
            tracef("inventory: item radial %s -> ContextRequest %s, RMB %s",
                   n->name.c_str(), ctx ? "taken" : "NOT taken",
                   ctx ? "not tried" : (rmb ? "taken" : "NOT taken"));
            return true;
        }
        return false;
    case B_TRIANGLE:
        if (c.cursor.dragging()) c.cursor.dragDrop(); else if (n && n->kind == NK_Cell) c.cursor.dragStart();
        return true;
    case B_CIRCLE:
        if (c.cursor.dragging()) c.cursor.dragCancel(); else c.ui.action("inventoryClose");
        return true;
    case B_L1: return clickNamed(c, "comp.Container.info.up");
    case B_R1: return clickNamed(c, "comp.Container.buttonView");
    case B_R3: c.ui.action("examine"); return true;
    default: return false;
    }
}

void Inventory::onBlur(Context& c) { if (c.cursor.dragging()) c.cursor.dragCancel(); }

}} // namespace cp::windows
