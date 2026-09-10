// Desktop tests: which window takes focus, what counts as furniture, the shared
// button layout and the fact that a specific window profile overrides it.
#include "test.h"
#include "mock_node.h"
#include "../windows/desktop.h"
#include "../windows/inventory.h"

using namespace cp; using namespace cp::mock; using namespace cp::cursor; using namespace cp::windows; using namespace cp::gen;

namespace {

// A window as the client markup builds them: an mmc caption with a close box and contents.
MockPtr window(const MockPtr& hud, const char* name, int x, int y, int w, int h)
{
    MockPtr p = hud->add(name, "Page", x, y, w, h);
    p->add("bg", "Page", 0, 0, w, 24)->add("mmc", "Page", w - 40, 0, 40, 20)->add("close", "Button", 24, 2, 16, 16);
    return p;
}

struct Bench {
    MockPtr root, hud;
    MockInput in; Ui ui{in}; Cursor cur{ui}; input::Pad pad; HintBar hints;
    Context ctx{cur, ui, pad, hints};
    Bench(const char* hudName = "GroundHUD")
    {
        root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
        hud = root->add(hudName, "Page", 0, 0, 1920, 1080);
        // HUD furniture: it is always visible and must not be a window
        hud->add("Toolbar", "Page", 700, 980, 520, 90)->add("slot", "Button", 0, 0, 40, 40);
        hud->add("ChatWindow", "Page", 20, 700, 400, 300)->add("input", "Textbox", 0, 0, 380, 20);
        hud->add("expMon", "Page", 1600, 100, 300, 120)->add("close", "Button", 280, 2, 16, 16);
    }
    void focus(Desktop& d) { NodePtr p = d.page(); cur.setRoot(p, d.rules()); }
};

} // namespace

TEST(desktop_focus_follows_top_window)
{
    Bench b;
    MockPtr quest = window(b.hud, "Quest", 100, 100, 600, 400);
    quest->add("accept", "Button", 20, 300, 100, 30);
    MockPtr mail = window(b.hud, "pmBrowser", 200, 150, 600, 400);
    mail->add("reply", "Button", 20, 300, 100, 30);
    Desktop d(b.root);
    // Draw order: first child on top, and the client raises the focused window there.
    // Furniture is skipped even when it lies above the windows.
    CHECK(d.page() == quest);
    CHECK(d.isOpen());
    b.hud->moveChild(mail, 2);                       // the client raised the mail
    CHECK(d.page() == mail);
    // The window name is visible from outside - it is what the cause is looked up by in the rig trace
    CHECK(std::string(d.id()) == "desktop:pmBrowser");
    mail->setVisible(false);
    CHECK(d.page() == quest);
    quest->setVisible(false);
    CHECK(!d.page()); CHECK(!d.isOpen());            // only furniture is left
}

TEST(desktop_skips_furniture_and_empty_pages)
{
    Bench b;
    Desktop d(b.root);
    // The toolbar, the chat and the xp counter are not windows: otherwise the desktop would take the pad for good
    CHECK(!d.isOpen());
    // A page with not a single navigation node is not a window either
    MockPtr empty = window(b.hud, "Decor", 10, 10, 400, 300);
    std::static_pointer_cast<MockNode>(empty->byPath("bg.mmc"))->removeChild(empty->byPath("bg.mmc.close"));
    b.hud->moveChild(empty, 2);
    CHECK(!d.isOpen());
    // A page that is too small is decoration
    MockPtr tiny = b.hud->add("Blip", "Page", 0, 0, 40, 20); tiny->add("b", "Button", 0, 0, 20, 20);
    b.hud->moveChild(tiny, 2);
    CHECK(!d.isOpen());
    // A name from the ini mutes a window the module knows nothing about
    MockPtr srv = window(b.hud, "ServerPanel", 50, 50, 300, 200);
    b.hud->moveChild(srv, 2);
    CHECK(d.page() == srv);
    Desktop d2(b.root); d2.addSkip(" ServerPanel , Decor ");
    CHECK(!d2.isOpen());
}

TEST(desktop_generic_buttons)
{
    Bench b;
    MockPtr w = window(b.hud, "Collections", 100, 100, 600, 400);
    MockPtr list = w->add("list", "Listbox", 20, 60, 300, 300);
    MockPtr ok = w->add("buttonOk", "Button", 400, 340, 100, 30);
    b.hud->moveChild(w, 2);
    Desktop d(b.root);
    b.focus(d);
    CHECK_EQ(b.cur.nodeCount(), 3u);                 // the list, the button, the close box
    b.cur.setCurrentNode(ok);
    // cross on a button is an ordinary click
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CROSS, true));
    CHECK_EQ(b.in.types()[0], abi::MSG_LeftMouseDown); CHECK_EQ(b.in.types().size(), 2u);
    // L2/R2 are the wheel over the current node: that is how long lists are paged
    b.in.clear(); CHECK(d.onButton(b.ctx, B_R2, true));
    CHECK_EQ(b.in.types().back(), abi::MSG_MouseWheel);
    CHECK_EQ(b.in.log.back().m.mouseX, ok->center().x);
    // circle is the window close box rather than Escape: closing must go the client's normal route
    NodePtr close = w->byPath("bg.mmc.close");
    CHECK(Desktop::closeButton(w) == close);
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CIRCLE, true));
    CHECK_EQ(b.in.log.back().m.type, abi::MSG_LeftMouseUp);
    CHECK_EQ(b.in.log.back().m.mouseX, close->center().x);
    // releases do nothing
    b.in.clear(); CHECK(!d.onButton(b.ctx, B_CROSS, false)); CHECK(b.in.log.empty());
    // captions in Latin-1: that is the whole coverage of the client's stock fonts
    std::vector<Hint> h = d.hints(b.ctx);
    CHECK(h[0].button == B_CROSS); CHECK(h[0].text == u"Press");
}

TEST(desktop_close_falls_back_to_escape)
{
    Bench b;
    MockPtr w = b.hud->add("Convo", "Page", 100, 100, 500, 300);
    w->add("line1", "Button", 10, 10, 400, 24);
    b.hud->moveChild(w, 2);
    Desktop d(b.root);
    b.focus(d);
    CHECK(!Desktop::closeButton(w));
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CIRCLE, true));
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_Escape_VALUE);
}

TEST(desktop_tabs_on_shoulders)
{
    Bench b;
    MockPtr w = window(b.hud, "Opt", 100, 100, 700, 500);
    MockPtr tabs = w->add("tabs", "TabbedPane", 10, 30, 680, 24);
    const char* names[3] = {"tGame", "tGraphics", "tSound"};
    for (int i = 0; i < 3; ++i) tabs->add(names[i], "Button", i * 120, 0, 120, 24);
    w->add("apply", "Button", 560, 440, 100, 30);
    b.hud->moveChild(w, 2);
    Desktop d(b.root);
    b.focus(d);
    CHECK_EQ(Desktop::tabs(w).size(), 3u);
    b.in.clear(); CHECK(d.onButton(b.ctx, B_R1, true));
    CHECK_EQ(b.in.log.back().m.mouseX, tabs->child("tGraphics")->center().x);
    b.in.clear(); d.onButton(b.ctx, B_R1, true);
    CHECK_EQ(b.in.log.back().m.mouseX, tabs->child("tSound")->center().x);
    b.in.clear(); d.onButton(b.ctx, B_R1, true);     // we do not go past the last one
    CHECK_EQ(b.in.log.back().m.mouseX, tabs->child("tSound")->center().x);
    b.in.clear(); d.onButton(b.ctx, B_L1, true);
    CHECK_EQ(b.in.log.back().m.mouseX, tabs->child("tGraphics")->center().x);
    std::vector<Hint> h = d.hints(b.ctx);
    bool hasTabs = false; for (const Hint& x : h) if (x.button == B_R1) hasTabs = true;
    CHECK(hasTabs);
    // A window change resets the tab: a new window has a set of its own
    MockPtr other = window(b.hud, "Quest", 0, 0, 400, 300);
    other->add("b", "Button", 10, 40, 60, 24);
    b.hud->moveChild(other, 2);
    CHECK(d.page() == other);
    b.hud->moveChild(w, 2);
    CHECK(d.page() == w);
    b.in.clear(); d.onButton(b.ctx, B_R1, true);
    CHECK_EQ(b.in.log.back().m.mouseX, tabs->child("tGraphics")->center().x);
}

TEST(desktop_profile_overrides_generic)
{
    Bench b;
    MockPtr inv = window(b.hud, "Inventory", 37, 209, 668, 519);
    MockPtr comp = inv->add("comp", "Composite", 0, 30, 668, 480);
    MockPtr cont = comp->add("Container", "Page", 200, 0, 420, 472);
    cont->add("info", "Page", 0, 0, 420, 40)->add("up", "Button", 4, 4, 30, 30);
    cont->add("buttonView", "Button", 350, 4, 60, 30);
    MockPtr vol = cont->add("icons", "Page", 0, 50, 420, 351)->add("volume", "VolumePage", 4, 7, 399, 336);
    vol->add("buttonExit", "Button", 2, 2, 64, 64);               // a profile sample, ignored
    for (int i = 0; i < 4; ++i) { char nm[8]; std::snprintf(nm, 8, "it%d", i); vol->add(nm, "Viewer", 2 + i * 66, 2, 64, 64); }
    b.hud->moveChild(inv, 2);

    Desktop d(b.root);
    d.addProfile(std::unique_ptr<cursor::Window>(new Inventory(b.root)));
    CHECK(d.page() == inv);
    CHECK(d.profile() != nullptr);
    CHECK(std::string(d.id()) == "desktop:Inventory/inventory");
    b.focus(d);
    // The profile rules are in force: the volume's cell sample did not get into the nodes
    CHECK_EQ(b.cur.nodeCount(), 7u);                              // 4 items + up + view + the close box
    // circle in the inventory is a client action of its own rather than a click on the close box
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CIRCLE, true));
    CHECK_EQ(b.in.log.back().kind, Event::Action);
    CHECK(b.in.log.back().id == "inventoryClose");
    // The profile's L1 is "up through the container" rather than scrolling
    NodePtr up = inv->byPath("comp.Container.info.up");
    b.in.clear(); CHECK(d.onButton(b.ctx, B_L1, true));
    CHECK(std::static_pointer_cast<MockNode>(up)->pressed()); CHECK(b.in.log.empty());
    // The hints are the profile's too
    std::vector<Hint> h = d.hints(b.ctx);
    bool hasView = false; for (const Hint& x : h) if (x.button == B_R1 && x.text == u"View") hasView = true;
    CHECK(hasView);
    // The window closed - the desktop honestly answers "no window", without touching
    // the remembered pointer: by then the client has already freed the page.
    b.hud->removeChild(inv);
    CHECK(!d.page()); CHECK(d.profile() == nullptr);
}

TEST(desktop_space_workspace)
{
    Bench b("HudSpace");
    MockPtr w = window(b.hud, "shipcomponents", 100, 100, 600, 400);
    w->add("install", "Button", 20, 300, 100, 30);
    b.hud->moveChild(w, 2);
    Desktop d(b.root);
    CHECK(d.page() == w);
    // Space has furniture of its own, but the rule is the same
    MockPtr gauge = b.hud->add("radarGauge", "Page", 0, 0, 300, 300); gauge->add("b", "Button", 0, 0, 40, 40);
    b.hud->moveChild(gauge, 2);
    CHECK(d.page() == w);
}

TEST(desktop_list_rows_by_dpad)
{
    Bench b;
    MockPtr w = window(b.hud, "Quest", 100, 100, 600, 400);
    MockPtr list = w->add("questList", "Listbox", 20, 60, 300, 300);
    MockPtr abandon = w->add("buttonAbandon", "Button", 400, 340, 100, 30);
    b.hud->moveChild(w, 2);
    Desktop d(b.root);
    b.focus(d);
    b.cur.setCurrentNode(list);
    CHECK(b.cur.current()->kind == NK_List);
    CHECK(!d.inList());
    // cross on a list enters it: we set the focus with the property, as the client
    // markup does, rather than with a click, which would select a random row
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CROSS, true));
    CHECK(d.inList());
    CHECK(std::static_pointer_cast<MockNode>(list)->prop(abi::PROP_Focus) == u"true");
    CHECK(b.in.log.empty());
    // the D-pad up/down leads the rows with arrows instead of jumping across widgets
    b.in.clear(); CHECK(d.onButton(b.ctx, B_DPAD_DOWN, true));
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_DownArrow_VALUE);
    CHECK(b.cur.current()->node == list);
    b.in.clear(); CHECK(d.onButton(b.ctx, B_DPAD_UP, true));
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_UpArrow_VALUE);
    // cross inside a list is Enter: the client answers a double click the same way
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CROSS, true));
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_Enter_VALUE);
    // L2/R2 by pages
    b.in.clear(); CHECK(d.onButton(b.ctx, B_R2, true));
    CHECK_EQ(b.in.log[0].m.keystroke, abi::UIMessage_key_PageDown_VALUE);
    // the hints say the mode is on
    std::vector<Hint> h = d.hints(b.ctx);
    CHECK(h[0].button == B_DPAD_DOWN); CHECK(h[2].text == u"Back");
    // circle leaves the list and does NOT close the window; a second circle does close it
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CIRCLE, true));
    CHECK(!d.inList()); CHECK(b.in.log.empty());
    b.in.clear(); CHECK(d.onButton(b.ctx, B_CIRCLE, true));
    CHECK_EQ(b.in.log.back().m.mouseX, w->byPath("bg.mmc.close")->center().x);
    // sideways - leaving the list by ordinary navigation
    d.onButton(b.ctx, B_CROSS, true); CHECK(d.inList());
    CHECK(!d.onButton(b.ctx, B_DPAD_RIGHT, true)); CHECK(!d.inList());
    // the cursor left the list by itself (a rescan, a node change) - the mode ended
    d.onButton(b.ctx, B_CROSS, true); CHECK(d.inList());
    b.cur.setCurrentNode(abandon);
    d.onTick(b.ctx, 0.016f);
    CHECK(!d.inList());

    // Holding the D-pad repeats the arrow: fifty rows one poke at a time is no way to
    // live, and nothing sends KeyRepeat for synthetic arrows - the repeat is ours, at
    // the cursor's pace.
    b.cur.setCurrentNode(list);
    d.onButton(b.ctx, B_CROSS, true); CHECK(d.inList());
    b.in.clear(); d.onButton(b.ctx, B_DPAD_DOWN, true);
    CHECK_EQ(b.in.log.size(), 2u);                   // KeyDown + KeyUp of one arrow
    d.onTick(b.ctx, 0.05f); CHECK_EQ(b.in.log.size(), 2u);          // silent until the first delay
    d.onTick(b.ctx, 0.3f);
    CHECK(b.in.log.size() > 2u);
    CHECK_EQ(b.in.log.back().m.keystroke, abi::UIMessage_key_DownArrow_VALUE);
    size_t after = b.in.log.size();
    CHECK(d.onButton(b.ctx, B_DPAD_DOWN, false));    // released - the repeat ended
    d.onTick(b.ctx, 0.5f);
    CHECK_EQ(b.in.log.size(), after);
}

TEST(desktop_finds_windows_outside_the_workspace)
{
    Bench b;
    MockPtr inv = window(b.hud, "Inventory", 37, 209, 668, 519);
    inv->add("comp", "Composite", 0, 30, 668, 480)->add("useButton", "Button", 20, 300, 100, 30);
    Desktop d(b.root);
    CHECK(d.page() == inv);

    // The game menu never reaches the desktop: it is a page of the root ("/GameMenu" in
    // the mediator factory, not duplicated into the desktop). ui_root.ui includes it
    // before the ground hud, i.e. above it, so focus is its while it is open.
    MockPtr gm = b.root->add("GameMenu", "Page", 0, 0, 1024, 768);
    gm->add("locationsButton", "Button", 407, 458, 211, 23);
    MockPtr menu = gm->add("menu", "Page", 300, 200, 400, 300);
    menu->add("bg", "Page", 0, 0, 400, 24)->add("mmc", "Page", 360, 0, 40, 20)->add("close", "Button", 24, 2, 16, 16);
    menu->add("exitButton", "Button", 20, 100, 200, 24);
    std::static_pointer_cast<MockNode>(b.root)->moveChild(gm, 2);
    CHECK(d.page() == gm);
    CHECK(std::string(d.id()) == "desktop:GameMenu");
    // the menu close box lies at menu.bg.mmc.close - one of the markup paths
    CHECK(Desktop::closeButton(gm) == gm->byPath("menu.bg.mmc.close"));
    gm->setVisible(false);
    CHECK(d.page() == inv);                              // the menu was closed - a desktop window again
}

TEST(desktop_looks_inside_folder_pages)
{
    Bench b;
    Desktop d(b.root);
    // The options live at "/Opt.OptMain": Opt is a folder page with the window inside.
    // The folder passes the window check on its own - there are nodes in it - and would
    // stand in for the window, moving the hints off the screen and sending circle after
    // a close box that is elsewhere. The caption tells them apart.
    MockPtr folder = b.root->add("Opt", "Page", 0, 0, 1024, 768);
    MockPtr opt = window(folder, "OptMain", 100, 100, 700, 500);
    opt->add("apply", "Button", 560, 440, 100, 30);
    std::static_pointer_cast<MockNode>(b.root)->moveChild(folder, 2);
    CHECK(d.page() == opt);
    CHECK(std::string(d.id()) == "desktop:OptMain");
    CHECK(Desktop::closeButton(opt) == opt->byPath("bg.mmc.close"));

    // A folder with hidden contents - how the client keeps its window samples - is not
    // a window at all
    opt->setVisible(false);
    CHECK(!d.isOpen());

    // A window with a caption of its own is not seen through, or the inventory would
    // hand back its inner panel and the profile would not match
    opt->setVisible(true);
    MockPtr inner = opt->add("comp", "Composite", 10, 40, 680, 440);
    inner->add("bg", "Page", 0, 0, 680, 24)->add("mmc", "Page", 640, 0, 40, 20)->add("close", "Button", 24, 2, 16, 16);
    inner->add("b", "Button", 20, 100, 80, 24);
    CHECK(d.page() == opt);
}
