#include "commands.h"
#include "../core/runtime.h"
#include "../core/binary.h"

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

Commands::Commands(NodePtr root, crossbar::Crossbar* bar, const char* path)
    : root_(root), bar_(bar), path_(path)
{
    rules_.ignore.push_back("checkPopupHelp");
    rules_.priority["volume"] = 5;          // the command grid matters more than the tabs and buttons
    rules_.priority["macrovolume"] = 5;
}

// By path every time, no cache: the client frees a remembered widget and such a pointer
// cannot be checked (see Inventory::page).
NodePtr Commands::page() const { return root_ ? root_->byPath(path_.c_str()) : NodePtr(); }

bool Commands::resolve(Button b, bool l2, bool r2, int& pane, int& slot)
{
    for (int i = 0; i < CLUSTER_COUNT; ++i) {
        if (CLUSTERS[i].button != b) continue;
        slot = CLUSTERS[i].slot;
        pane = (l2 && r2) ? PANE_L2R2 : l2 ? PANE_L2 : r2 ? PANE_R2 : PANE_MAIN;
        return true;
    }
    return false;
}

std::vector<Hint> Commands::promptHints()
{
    std::vector<Hint> h;
    // Two glyphs: D-pad and faces. A combo goes on any of the ten cluster buttons, and
    // one glyph would read as "D-pad only".
    h.push_back(Hint{B_DPAD_UP, u"", crossbar::Glyph::DPAD});
    h.push_back(Hint{B_CROSS, u"Press a button to bind", crossbar::Glyph::FACE_ALL});
    h.push_back(Hint{B_L2, u"", crossbar::Glyph::TR_L});
    h.push_back(Hint{B_R2, u"hold for layer", crossbar::Glyph::TR_R});
    h.push_back(Hint{B_R1, u"Cancel"});
    return h;
}

std::vector<Hint> Commands::hints(const Context& c) const
{
    std::vector<Hint> h;
    if (binding_) return promptHints();   // the same as in the window at screen center
    navigateHints(h);
    const NodeInfo* n = c.cursor.current();
    h.push_back(Hint{B_CROSS, u"Run"});
    if (n && n->kind == NK_Cell) h.push_back(Hint{B_SQUARE, u"Bind"});
    h.push_back(Hint{B_CIRCLE, u"Close"});
    return h;
}

bool Commands::onButton(Context& c, Button b, bool pressed)
{
    if (!pressed) return false;
    const NodeInfo* n = c.cursor.current();
    if (binding_) {
        // Cancel is not on circle: circle is cluster slot 6, and while it cancelled it
        // could not be bound at all. R1 is neither a cluster button nor a modifier, and
        // it has a glyph.
        if (b == B_R1) { binding_ = false; tracef("bind: cancelled"); return true; }
        int pane = 0, slot = 0;
        if (!resolve(b, c.pad.down(B_L2), c.pad.down(B_R2), pane, slot)) return true;  // not a combo - keep waiting
        binding_ = false;
        if (!bar_ || !n || !n->node) { tracef("bind: nothing to bind"); return true; }
        tracef("bind: %s -> set %d slot %d", n->name.c_str(), pane, slot);
        bar_->bindFrom(c.ui, n->node, pane, slot);
        return true;
    }
    switch (b) {
    case B_SQUARE:
        if (n && n->kind == NK_Cell) {
            binding_ = true;
            tracef("bind: waiting for a button for %s", n->name.c_str());
            return true;
        }
        return false;
    case B_CROSS:  c.cursor.activate(); return true;
    case B_CIRCLE: c.ui.escape(); return true;
    default: return false;
    }
}

void Commands::onBlur(Context&) { binding_ = false; }

}} // namespace cp::windows
