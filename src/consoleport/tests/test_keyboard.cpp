// The on-screen keyboard: the grid, typing characters, the service keys, cleanup.
#include "test.h"
#include "mock_node.h"
#include "../windows/keyboard.h"
#include "../cursor/cursor.h"
#include "../cursor/hints.h"

using namespace cp; using namespace cp::windows; using namespace cp::cursor; using namespace cp::gen;
using cp::mock::MockNode; using cp::mock::MockPtr; using cp::mock::MockInput;

namespace {

struct Bench {
    MockPtr hud, frameTpl, textTpl, imgTpl, field;
    MockInput in; Ui ui{in}; Cursor cur{ui}; input::Pad pad; HintBar hints;
    Context ctx{cur, ui, pad, hints};
    Keyboard kb;

    Bench(int fieldY = 40)
    {
        hud = MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
        frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
        textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
        imgTpl = hud->add("imgTpl", "Image", 0, 0, 20, 20);
        field = hud->add("Input", "Textbox", 176, fieldY, 296, 24);
        kb.attach(hud, frameTpl, textTpl, imgTpl);
    }
    int clones(const char* prefix) const
    {
        int n = 0;
        const size_t len = std::string(prefix).size();
        for (const NodePtr& k : hud->children())
            if (k->name().compare(0, len, prefix) == 0) ++n;
        return n;
    }
    // The last message in the input log
    const cp::mock::Event* last() const { return in.log.empty() ? nullptr : &in.log.back(); }
};

} // namespace

TEST(keyboard_builds_and_cleans_up)
{
    Bench b;
    CHECK(!b.kb.isOpen());
    b.kb.open(b.field, true);
    CHECK(b.kb.isOpen());
    CHECK(b.kb.page() != nullptr);              // there is a backdrop, and the stack will find the window by it
    CHECK(b.clones("cpKeyF") > 40);             // 4 rows of 10 plus the service ones
    CHECK(b.clones("cpKeyT") > 40);

    // Closing takes everything down: clones in the HUD overlay outlive the window if they are not removed.
    b.kb.onBlur(b.ctx);
    CHECK(!b.kb.isOpen());
    CHECK_EQ(b.clones("cpKeyF"), 0);
    CHECK_EQ(b.clones("cpKeyT"), 0);
    CHECK_EQ(b.clones("cpKeyBack"), 0);
}

TEST(keyboard_grid_walks_and_wraps)
{
    Bench b; b.kb.open(b.field, true);
    // It opens on the letter Q - the top row of digits is too often not what is wanted.
    CHECK_EQ(b.kb.row(), 1); CHECK_EQ(b.kb.col(), 0);
    b.kb.onButton(b.ctx, B_DPAD_RIGHT, true);
    CHECK_EQ(b.kb.col(), 1);
    b.kb.onButton(b.ctx, B_DPAD_LEFT, true);
    b.kb.onButton(b.ctx, B_DPAD_LEFT, true);
    CHECK_EQ(b.kb.col(), 9);                    // wrapping around the left edge
    b.kb.onButton(b.ctx, B_DPAD_UP, true);
    CHECK_EQ(b.kb.row(), 0);
    b.kb.onButton(b.ctx, B_DPAD_UP, true);
    CHECK_EQ(b.kb.row(), 4);                    // and around the top one
    // In the service row the keys are double width - there are no odd columns there.
    CHECK_EQ(b.kb.col() % 2, 0);
}

TEST(keyboard_types_characters)
{
    Bench b; b.kb.open(b.field, true);
    // The first cross is swallowed: the keyboard is opened with that same cross.
    CHECK(b.kb.onButton(b.ctx, B_CROSS, true));
    CHECK(b.in.log.empty());

    b.kb.onButton(b.ctx, B_CROSS, true);        // Q, lower case - Shift is not pressed
    CHECK_EQ(b.in.log.size(), 1u);
    CHECK(b.in.log[0].kind == cp::mock::Event::Msg);
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_Character);
    CHECK_EQ(b.in.log[0].m.keystroke, static_cast<uint16_t>(u'q'));
    // We type into a specific field - which means it was given the focus.
    CHECK(b.field->prop(abi::PROP_Focus) == u"true");

    b.in.clear();
    b.kb.onButton(b.ctx, B_DPAD_RIGHT, true);
    b.kb.onButton(b.ctx, B_CROSS, true);        // W
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'w'));
}

TEST(keyboard_shift_is_one_shot_and_caps_holds)
{
    Bench b; b.kb.open(b.field, true);
    b.kb.onButton(b.ctx, B_CROSS, true);        // the swallowed opening

    b.kb.onButton(b.ctx, B_TRIANGLE, true);     // Shift
    CHECK(b.kb.shift());
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'Q'));
    CHECK(!b.kb.shift());                       // Shift holds for exactly one character
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'q'));

    // A second triangle in a row turns Caps on - it holds until it is taken off.
    b.kb.onButton(b.ctx, B_TRIANGLE, true);
    b.kb.onButton(b.ctx, B_TRIANGLE, true);
    CHECK(b.kb.caps());
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'Q'));
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'Q'));
    b.kb.onButton(b.ctx, B_TRIANGLE, true);
    CHECK(!b.kb.caps());
}

TEST(keyboard_symbol_layer)
{
    Bench b; b.kb.open(b.field, true);
    b.kb.onButton(b.ctx, B_CROSS, true);        // the swallowed opening
    CHECK(!b.kb.symbols());
    CHECK(b.kb.label(1, 0) == u"q");

    b.kb.onButton(b.ctx, B_R1, true);
    CHECK(b.kb.symbols());
    CHECK(b.kb.label(1, 0) == u"-");            // the symbol layer, the second row
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.keystroke, static_cast<uint16_t>(u'-'));

    b.kb.onButton(b.ctx, B_R1, true);
    CHECK(!b.kb.symbols());
}

TEST(keyboard_backspace_space_and_enter)
{
    Bench b; b.kb.open(b.field, true);
    b.kb.onButton(b.ctx, B_CROSS, true);        // the swallowed opening

    // Backspace and Enter are NOT Character but KeyDown/KeyUp with the client's constants.
    b.kb.onButton(b.ctx, B_SQUARE, true);
    CHECK_EQ(b.in.log.size(), 2u);
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_KeyDown);
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_BackSpace_VALUE);
    CHECK_EQ(b.in.log[1].m.type, abi::MSG_KeyUp);

    b.in.clear();
    b.kb.onButton(b.ctx, B_OPTIONS, true);      // Start - sending the line
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_KeyDown);
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_Enter_VALUE);

    // R2 is the same Enter under the trigger: send without a trip to the service row.
    b.in.clear();
    b.kb.onButton(b.ctx, B_R2, true);
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_KeyDown);
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_Enter_VALUE);

    // Space is an ordinary character, the textbox accepts it as a Character.
    b.in.clear();
    b.kb.onButton(b.ctx, B_L1, true);
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_Character);
    CHECK_EQ(b.in.log[0].m.keystroke, static_cast<uint16_t>(u' '));
}

TEST(keyboard_circle_closes)
{
    Bench b; b.kb.open(b.field, true);
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK(b.kb.isOpen());
    b.kb.onButton(b.ctx, B_CIRCLE, true);
    CHECK(!b.kb.isOpen());
    CHECK_EQ(b.clones("cpKeyF"), 0);
}

TEST(keyboard_avoids_the_target_field)
{
    // A field at the bottom (the chat line) pushes the keyboard up, or it covers the
    // field being typed into.
    Bench low(700);
    low.kb.open(low.field, true);
    NodePtr back = low.hud->child("cpKeyBack");
    CHECK(back != nullptr);
    CHECK(back->location().y < 384);

    // A field at the top - the keyboard at the bottom.
    Bench high(40);
    high.kb.open(high.field, true);
    NodePtr back2 = high.hud->child("cpKeyBack");
    CHECK(back2 != nullptr);
    CHECK(back2->location().y > 384);
}

TEST(keyboard_legend_takes_two_rows_under_the_panel)
{
    // Nine hints in one row run off the screen, so the legend is our own, in two lines
    // under the backdrop.
    Bench b; b.kb.open(b.field, true);
    NodePtr back = b.hud->child("cpKeyBack");
    CHECK(back != nullptr);

    int row1 = 0, row2 = 0;
    for (const NodePtr& k : b.hud->children()) {
        if (k->name().compare(0, 7, "cpKbdL1") == 0) ++row1;
        if (k->name().compare(0, 7, "cpKbdL2") == 0) ++row2;
    }
    CHECK(row1 > 0); CHECK(row2 > 0);       // both lines were built

    // The second line is below the first, and both are under the keyboard.
    NodePtr g1 = b.hud->child("cpKbdL1G0"), g2 = b.hud->child("cpKbdL2G0");
    CHECK(g1 && g2);
    CHECK(g1->location().y >= back->location().y + back->size().y);
    CHECK(g2->location().y > g1->location().y);

    // The stack bar gets navigation only: our own legend has already said everything.
    std::vector<Hint> h = b.kb.hints(b.ctx);
    CHECK(h.size() <= 3u);

    b.kb.onBlur(b.ctx);
    int left = 0;
    for (const NodePtr& k : b.hud->children())
        if (k->name().compare(0, 5, "cpKbd") == 0) ++left;
    CHECK_EQ(left, 0);                      // closed - the legend came down with the keys
}

TEST(keyboard_finds_the_chat_input_itself)
{
    // The client focuses the chat line itself, but not always on the same frame, and the
    // first character went nowhere. So we find the field and focus it before every
    // character.
    Bench b;
    MockPtr chat = b.hud->add("ChatWindow", "Page", 176, 623, 296, 112);
    MockPtr body = chat->add("body", "Page", 0, 0, 296, 80);
    MockPtr input = body->add("Input", "Textbox", 0, 84, 296, 24);

    b.kb.open(NodePtr(), false);                 // as if by the L3+R3 chord
    CHECK(b.kb.openedChat());
    CHECK(b.kb.target() == input);               // found the line by type, not by path

    b.kb.onButton(b.ctx, B_CROSS, true);         // a chord swallows nothing
    CHECK_EQ(b.in.log.size(), 1u);
    CHECK_EQ(b.in.log[0].m.type, abi::MSG_Character);
    CHECK(input->prop(abi::PROP_Focus) == u"true");
}

TEST(keyboard_without_target_types_into_focus)
{
    // An empty target means chat: the client holds the focus, we just send.
    Bench b; b.kb.open(NodePtr(), true);
    CHECK(b.kb.target() == nullptr);
    b.kb.onButton(b.ctx, B_CROSS, true);        // the swallowed opening
    b.kb.onButton(b.ctx, B_CROSS, true);
    CHECK_EQ(b.in.log.back().m.type, abi::MSG_Character);
    CHECK(b.field->prop(abi::PROP_Focus) != u"true");
}

TEST(keyboard_owns_its_whole_legend)
{
    // The stack bar has a fixed place by the crossbar while the keyboard lands wherever
    // it fits, and in game the two met and drew hints on top of each other (10 Sep
    // 2026). So the keyboard keeps its whole legend and the stack gets nothing.
    Bench b;
    b.kb.open(b.field, true);
    CHECK(b.kb.hints(b.ctx).empty());
    // Both lines are in place, and navigation moved into the second.
    CHECK(b.clones("cpKbdL1") > 0);
    CHECK(b.clones("cpKbdL2") > 0);
}

TEST(keyboard_backdrop_is_visible_and_sits_where_asked)
{
    // The backdrop is a clone of a hidden template, so without explicit visibility it
    // never appeared: on character select the keys showed and the ground did not. Keys
    // are given
    Bench b;
    b.kb.open(NodePtr(), false, true);               // bottom center
    MockPtr back = std::static_pointer_cast<MockNode>(b.hud->child("cpKeyBack"));
    CHECK(back);
    CHECK(back->willDraw());

    const UISize scr = b.hud->size();
    const UIPoint at = back->location();
    const UISize sz = back->size();
    CHECK(at.y + sz.y <= scr.y);                     // entirely on screen
    CHECK(at.y > scr.y / 2);                         // and at the bottom specifically
    // Horizontally, centered, up to rounding.
    const int dx = (scr.x - sz.x) / 2 - at.x;
    CHECK(dx <= 1 && dx >= -1);

    // With no request for the bottom the behavior is as before: in the world people type into chat, which is at the bottom.
    b.kb.close(nullptr);
    b.kb.open(NodePtr(), false);
    back = std::static_pointer_cast<MockNode>(b.hud->child("cpKeyBack"));
    CHECK(back); CHECK(back->willDraw());
    CHECK(back->location().y < scr.y / 2);
}
