// Tests for the character selection screen: what counts as the screen, where the
// frame goes, how the D-pad differs from the stick, what the legend says and why
// the delete dialog leaves only the cancel.
#include "test.h"
#include "mock_node.h"
#include "../windows/charselect.h"
#include "../cursor/nodes.h"

using namespace cp; using namespace cp::mock; using namespace cp::windows;
using namespace cp::cursor; using namespace cp::gen;
using cp::input::DiEvent;

namespace {

// ds4: CROSS=1, CIRCLE=2, SQUARE=0, TRIANGLE=3 (gen/profiles.h)
const int JB_CROSS = 1, JB_CIRCLE = 2, JB_SQUARE = 0, JB_TRIANGLE = 3;

struct Bench {
    MockPtr root, avsel, table, viewer, delBg;
    MockInput in; Ui ui{in}; cp::input::Pad pad;
    uint32_t seq = 1;

    Bench()
    {
        root = MockNode::make("root", "Page", 0, 0, 1024, 768);
        // The root backdrop: it is drawn on this same screen and is not the screen.
        root->add("Back", "Page", 0, 0, 1024, 768);
        avsel = root->add("AvSel", "Page", 0, 0, 1024, 768);

        delBg = avsel->add("deleteConfirmationBackground", "Page", 0, 0, 1024, 768);
        delBg->setVisible(false);
        MockPtr del = delBg->add("deleteConfirmation", "Page", 283, 250, 459, 269);
        MockPtr win = del->add("window", "Page", 0, 0, 459, 269);
        win->add("buttonOk", "Button", 264, 232, 173, 17);
        win->add("buttonCancel", "Button", 21, 232, 173, 17);

        MockPtr bottom = avsel->add("pageButtonsBottom", "Page", 0, 700, 1024, 68);
        bottom->add("Prev", "Page", 0, 0, 200, 40)->add("buttonPrev", "Button", 0, 0, 173, 17);
        bottom->add("Next", "Page", 800, 0, 200, 40)->add("buttonNext", "Button", 0, 0, 173, 17);

        MockPtr sel = avsel->add("pageSelection", "Page", 500, 100, 500, 500);
        MockPtr pt = sel->add("pageTable", "Page", 0, 40, 480, 300);
        // The scrollbar: its buttons must not get into the frame.
        pt->add("scroll", "Page", 460, 0, 20, 300)->add("thumb", "Button", 0, 40, 20, 60);
        table = pt->add("table", "Table", 0, 0, 460, 300);
        sel->add("buttonDelete", "Button", 0, 400, 173, 17);
        sel->add("buttonCreate", "Button", 200, 400, 173, 17);

        viewer = avsel->add("viewer", "Viewer", 0, -4, 1026, 773);
        viewer->setProp(abi::PROP_CameraYaw, u"3.45");

        // The templates from ui_consoleport_charselect.inc: without them no frame and no glyphs.
        MockPtr tpl = avsel->add("cpTpl", "Page", 0, 0, 64, 64);
        tpl->setVisible(false);
        tpl->add("cpFrameTemplate", "Page", 0, 0, 64, 64);
        tpl->add("cpGlyphTemplate", "Image", 0, 0, 64, 64);
        tpl->add("cpTextTemplate", "Text", 0, 0, 240, 20);

        // The axis ranges arrive from the DirectInput wrapper; without them the sticks stay silent.
        pad.setAxisRange(cp::input::DI_X, 0, 65535);
        pad.setAxisRange(cp::input::DI_Y, 0, 65535);
        pad.setAxisRange(cp::input::DI_Z, 0, 65535);
        pad.setAxisRange(cp::input::DI_RZ, 0, 65535);
    }

    void feed(const DiEvent& e)
    {
        DiEvent buf[16]; buf[0] = e; uint32_t n = 1;
        pad.feed(buf, n, 16, false);
    }
    void button(int joyb, bool down)
    {
        DiEvent e = {static_cast<uint32_t>(cp::input::DI_BUTTON0 + joyb), down ? 0x80u : 0u, 100, seq++, 0};
        feed(e);
    }
    void tap(CharSelect& cs, int joyb) { button(joyb, true); frame(cs); button(joyb, false); frame(cs); }
    void pov(uint32_t v) { DiEvent e = {cp::input::DI_POV0, v, 100, seq++, 0}; feed(e); }
    void axis(int ofs, int32_t v) { DiEvent e = {static_cast<uint32_t>(ofs), static_cast<uint32_t>(v), 100, seq++, 0}; feed(e); }

    // A module frame: the parse first, then the edges are latched - as in Runtime::frame.
    void frame(CharSelect& cs, float dt = 0.016f) { cs.update(pad, dt); pad.endFrame(dt); }

    int keys(uint16_t code) const
    {
        int n = 0;
        for (const Event& e : in.log)
            if (e.kind == Event::Msg && e.m.type == abi::MSG_KeyDown && e.m.keystroke == code) ++n;
        return n;
    }
    // Our widgets on the screen page (the cpTpl template does not count - it comes from the markup).
    int ourWidgets() const
    {
        int n = 0;
        for (const NodePtr& k : avsel->children())
            if (k->name().compare(0, 2, "cp") == 0 && k->name() != "cpTpl") ++n;
        return n;
    }
    std::string ringOn(const CharSelect& cs) const
    {
        const NodeInfo* n = cs.cursor().current();
        return n ? n->name : std::string();
    }
    static bool says(const std::vector<Hint>& h, const char16_t* text)
    {
        for (const Hint& i : h) if (i.text == text) return true;
        return false;
    }
};

uint16_t keyUp() { return abi::UIMessage_key_UpArrow_VALUE; }
uint16_t keyDown() { return abi::UIMessage_key_DownArrow_VALUE; }

} // namespace

TEST(charselect_finds_screen_by_name_and_by_signature)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    CHECK(cs.page() == b.avsel);
    CHECK(cs.table() == b.table);
    CHECK(cs.viewer() == b.viewer);
    CHECK(cs.button("buttonNext") != nullptr);
    CHECK(cs.button("buttonDelete") != nullptr);
    CHECK(!cs.button("pageSelection"));               // not a button - we do not hand it over

    // The page name can change, the signature made of buttons cannot.
    b.avsel->setProp(abi::PROP_Name, u"CharacterSelect");
    CHECK(cs.page() == b.avsel);

    // No screen - we find nothing, and the root backdrop does not become the screen.
    b.avsel->setVisible(false);
    CHECK(!cs.page()); CHECK(!cs.active()); CHECK(!cs.table());
}

TEST(charselect_ring_starts_on_the_list)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK(cs.attached());                              // the templates were found, the frame was built
    CHECK(cs.onList());
    CHECK(b.ringOn(cs) == "table");
    // The scrollbar buttons are not frame stops
    for (const NodeInfo& n : cs.cursor().nodes()) CHECK(n.name != "thumb");
    // The legend says DIFFERENT things about the D-pad and the stick, or no way out of the list is visible
    std::vector<Hint> h = cs.hints();
    CHECK(Bench::says(h, u"Rows"));
    CHECK(Bench::says(h, u"Move"));
    CHECK(Bench::says(h, u"Enter world"));
    CHECK(Bench::says(h, u"Turn"));
}

TEST(charselect_dpad_walks_rows_with_repeat)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK_EQ(b.keys(keyDown()), 0);

    b.pov(18000);                                     // the D-pad down
    b.frame(cs);
    CHECK_EQ(b.keys(keyDown()), 1);
    // The table is given focus - otherwise the arrow would have gone elsewhere
    CHECK(b.table->prop(abi::PROP_Focus) == u"true");
    CHECK(cs.onList());                                // we lead the rows and do not move the frame

    for (int i = 0; i < 5; ++i) b.frame(cs);          // 0.08 s - the 0.125 threshold was not passed
    CHECK_EQ(b.keys(keyDown()), 1);
    for (int i = 0; i < 10; ++i) b.frame(cs);
    CHECK(b.keys(keyDown()) >= 2);

    // Released - the counter stopped
    b.pov(cp::input::DI_POV_CENTERED);
    b.frame(cs);
    const int seen = b.keys(keyDown());
    for (int i = 0; i < 30; ++i) b.frame(cs);
    CHECK_EQ(b.keys(keyDown()), seen);

    // Up - an arrow of its own
    b.pov(0);
    b.frame(cs);
    CHECK_EQ(b.keys(keyUp()), 1);
}

TEST(charselect_left_stick_moves_the_ring_off_the_list)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK(cs.onList());

    b.axis(cp::input::DI_Y, 65535);                   // the stick all the way down
    b.frame(cs);
    CHECK(!cs.onList());                               // we left for the buttons
    CHECK(b.ringOn(cs) == "buttonCreate");
    CHECK_EQ(b.keys(keyDown()), 0);                    // no arrows were sent to the list

    // The legend moved along with the frame
    CHECK(Bench::says(cs.hints(), u"Press"));
    CHECK(!Bench::says(cs.hints(), u"Rows"));

    // Outside the list the D-pad leads the frame too, not the rows
    b.axis(cp::input::DI_Y, 32767);
    b.frame(cs);
    b.pov(0);
    b.frame(cs);
    CHECK_EQ(b.keys(keyUp()), 0);
    CHECK(b.ringOn(cs) != "buttonCreate");
}

TEST(charselect_cross_enters_world_from_the_list_and_presses_buttons)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    MockPtr next = std::static_pointer_cast<MockNode>(cs.button("buttonNext"));
    MockPtr create = std::static_pointer_cast<MockNode>(cs.button("buttonCreate"));
    b.frame(cs);

    // On the list, cross enters the world: the client answers a double click on a row the same way
    b.tap(cs, JB_CROSS);
    CHECK(next->pressed());
    CHECK(!create->pressed());

    // On a button, cross presses that button
    b.axis(cp::input::DI_Y, 65535);
    b.frame(cs);
    CHECK(b.ringOn(cs) == "buttonCreate");
    b.tap(cs, JB_CROSS);
    CHECK(create->pressed());
}

TEST(charselect_circle_goes_back_and_square_creates)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    MockPtr prev = std::static_pointer_cast<MockNode>(cs.button("buttonPrev"));
    MockPtr create = std::static_pointer_cast<MockNode>(cs.button("buttonCreate"));
    MockPtr del = std::static_pointer_cast<MockNode>(cs.button("buttonDelete"));
    b.frame(cs);

    b.tap(cs, JB_CIRCLE);
    CHECK(prev->pressed());
    b.tap(cs, JB_SQUARE);
    CHECK(create->pressed());
    // Triangle is free: deleting requires typing a name, and the pad has no keyboard
    b.tap(cs, JB_TRIANGLE);
    CHECK(!del->pressed());
}

TEST(charselect_delete_dialog_offers_typing_and_cancel)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK(!cs.blocked());
    MockPtr next = std::static_pointer_cast<MockNode>(cs.button("buttonNext"));

    b.delBg->setVisible(true);
    CHECK(cs.blocked());
    b.frame(cs);

    // Only one button used to work here - a name could not be typed from the pad. The
    // screen has a keyboard of its own now.
    std::vector<Hint> h = cs.hints();
    CHECK_EQ((int)h.size(), 2);
    CHECK(Bench::says(h, u"Type"));
    CHECK(Bench::says(h, u"Cancel"));

    b.tap(cs, JB_CROSS);
    CHECK(!next->pressed());                          // cross would have gone to the dialog's OK
    b.pov(18000); b.frame(cs);
    CHECK_EQ(b.keys(keyDown()), 0);

    // Circle cancels - otherwise the pad would lead into a dialog it cannot leave
    MockPtr cancel = std::static_pointer_cast<MockNode>(CharSelect::findNamed(b.avsel, "buttonCancel"));
    b.pov(cp::input::DI_POV_CENTERED); b.frame(cs);
    b.tap(cs, JB_CIRCLE);
    CHECK(cancel->pressed());

    b.delBg->setVisible(false);
    CHECK(!cs.blocked());
}

TEST(charselect_right_stick_turns_the_model)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK(b.viewer->prop(abi::PROP_CameraYaw) == u"3.45");

    b.axis(cp::input::DI_Z, 65535);                   // the right stick to the right
    for (int i = 0; i < 10; ++i) b.frame(cs, 0.1f);
    float yaw = 0;
    CHECK(b.viewer->getPropF(abi::PROP_CameraYaw, yaw));
    CHECK(yaw > 4.0f);                                 // 3.45 + about 2 rad/s * 1 s

    const float turned = yaw;
    b.axis(cp::input::DI_Z, 32767);                   // released - the angle stands
    for (int i = 0; i < 10; ++i) b.frame(cs, 0.1f);
    CHECK(b.viewer->getPropF(abi::PROP_CameraYaw, yaw));
    CHECK(yaw == turned);

    b.axis(cp::input::DI_Z, 0);                       // left - back again
    for (int i = 0; i < 10; ++i) b.frame(cs, 0.1f);
    CHECK(b.viewer->getPropF(abi::PROP_CameraYaw, yaw));
    CHECK(yaw < turned);
}

TEST(charselect_takes_its_widgets_off_when_the_screen_goes)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    b.frame(cs);
    CHECK(cs.attached());
    CHECK(b.ourWidgets() > 0);                         // the frame and the hints are on the page

    b.avsel->setVisible(false);
    b.frame(cs);
    CHECK(!cs.attached());
    CHECK_EQ(b.ourWidgets(), 0);                       // nothing was left hanging

    // Came back - attached again
    b.avsel->setVisible(true);
    b.frame(cs);
    CHECK(cs.attached());
    CHECK(cs.onList());
}

TEST(charselect_without_markup_keeps_the_buttons)
{
    Bench b;
    // No markup means no templates and nothing to draw, but the buttons still have to
    // work - an update must not break what worked.
    b.avsel->removeChild(b.avsel->child("cpTpl"));
    CharSelect cs(b.root, b.ui);
    MockPtr next = std::static_pointer_cast<MockNode>(cs.button("buttonNext"));
    b.frame(cs);
    CHECK(!cs.attached());
    CHECK_EQ(b.ourWidgets(), 0);
    b.tap(cs, JB_CROSS);
    CHECK(next->pressed());
    b.pov(18000); b.frame(cs);
    CHECK_EQ(b.keys(keyDown()), 1);                    // the rows are led even with no frame
}

TEST(charselect_survey_reports_what_it_found)
{
    Bench b;
    CharSelect cs(b.root, b.ui);
    std::vector<std::string> s = cs.survey();
    CHECK_EQ((int)s.size(), 2);
    CHECK(s[0].find("AvSel") != std::string::npos);
    CHECK(s[0].find("Table") != std::string::npos);
    CHECK(s[1].find("buttonNext=present") != std::string::npos);

    // A disabled button shows in the breakdown separately from a missing one
    std::static_pointer_cast<MockNode>(cs.button("buttonDelete"))->setProp(abi::PROP_Enabled, u"false");
    s = cs.survey();
    CHECK(s[1].find("buttonDelete=disabled") != std::string::npos);

    b.avsel->setVisible(false);
    s = cs.survey();
    CHECK_EQ((int)s.size(), 1);
    CHECK(s[0].find("no screen") != std::string::npos);
}

TEST(nodes_scan_takes_the_client_table)
{
    // UITable is a list like UIList - the widget draws the rows. The binary's type tag
    // does not match the sources' enum, so we match on the type name.
    MockPtr page = MockNode::make("win", "Page", 0, 0, 400, 300);
    page->add("table", "Table", 10, 10, 380, 200);
    page->add("ok", "Button", 10, 260, 80, 20);
    ScanRules r;
    std::vector<NodeInfo> n = scanNodes(page, r);
    CHECK_EQ((int)n.size(), 2);
    int lists = 0, buttons = 0;
    for (const NodeInfo& i : n) { if (i.kind == NK_List) ++lists; if (i.kind == NK_Button) ++buttons; }
    CHECK_EQ(lists, 1);
    CHECK_EQ(buttons, 1);
}

TEST(charselect_legend_offers_the_on_screen_keyboard)
{
    // In the world the keyboard is created in attachCursor on top of GroundHUD, which
    // this screen has not got, so it keeps its own instance - and the legend has to
    // mention it or nobody finds triangle.
    Bench b;
    CharSelect cs(b.root, b.ui);
    cs.update(b.pad, 0.016f);
    std::vector<Hint> h = cs.hints();
    bool type = false;
    for (const Hint& x : h) if (x.text == u"Type") type = true;
    CHECK(type);
}
