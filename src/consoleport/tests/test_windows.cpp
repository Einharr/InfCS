// Window profile tests: the inventory grid and buttons, and the popup/radial.
#include "test.h"
#include "mock_node.h"
#include "../windows/inventory.h"
#include "../windows/popup.h"
#include "../windows/gamebar.h"
#include "../windows/commands.h"
#include "../crossbar/crossbar.h"

using namespace cp; using namespace cp::mock; using namespace cp::cursor; using namespace cp::windows; using namespace cp::gen;

static MockPtr makeInventory(MockPtr& root)
{
    root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    // The live client's path: the inventory hangs off GroundHUD (tree walk, 8 Sep 2026),
    // not off the root and not under the PDA.
    MockPtr hud = root->add("GroundHUD", "Page", 0, 0, 1920, 1080);
    MockPtr inv = hud->add("Inventory", "Page", 37, 209, 668, 519);
    MockPtr comp = inv->add("comp", "Composite", 0, 30, 668, 480);
    MockPtr cont = comp->add("Container", "Page", 200, 0, 420, 472);
    MockPtr info = cont->add("info", "Page", 0, 0, 420, 40);
    info->add("up", "Button", 4, 4, 30, 30);
    cont->add("buttonView", "Button", 350, 4, 60, 30);
    MockPtr icons = cont->add("icons", "Page", 0, 50, 420, 351);
    MockPtr vol = icons->add("volume", "VolumePage", 4, 7, 399, 336);
    vol->add("buttonExit", "Button", 2, 2, 64, 64);            // a sample, ignored
    for (int i = 0; i < 6; ++i) { char nm[8]; std::snprintf(nm, 8, "it%d", i); vol->add(nm, "Viewer", 2 + (i % 3) * 66, 2 + (i / 3) * 66, 64, 64); }
    MockPtr bg = inv->add("bg", "Page", 0, 0, 668, 30);
    bg->add("mmc", "Page", 640, 0, 16, 10)->add("close", "Button", 0, -3, 16, 16);
    return inv;
}

TEST(inventory_profile_grid_and_buttons)
{
    MockPtr root; MockPtr inv = makeInventory(root);
    MockInput in; Ui ui(in); Cursor cur(ui); input::Pad pad; HintBar hints;
    Context ctx = {cur, ui, pad, hints};
    Inventory w(root);
    CHECK(w.page() == inv); CHECK(w.isOpen());
    cur.setRoot(w.page(), w.rules());
    CHECK_EQ(cur.nodeCount(), 9u);                    // 6 items + up + view + close; buttonExit is out
    // the starting node - the volume has priority (the first item in the grid)
    CHECK(cur.current()->kind == NK_Cell);
    // cross on an item is a double click (performDefaultAction)
    in.clear(); CHECK(w.onButton(ctx, B_CROSS, true));
    CHECK_EQ(in.types()[1], abi::MSG_LeftMouseDoubleClick);
    // square is the item radial menu
    in.clear(); CHECK(w.onButton(ctx, B_SQUARE, true)); CHECK_EQ(in.types()[0], abi::MSG_ContextRequest);
    // triangle picks up, the D-pad, triangle puts down
    in.clear(); CHECK(w.onButton(ctx, B_TRIANGLE, true)); CHECK(cur.dragging());
    std::vector<Hint> h = w.hints(ctx); CHECK_EQ(h.size(), 2u); CHECK(h[0].button == B_TRIANGLE);
    cur.navigate(DIR_RIGHT);
    CHECK(w.onButton(ctx, B_TRIANGLE, true)); CHECK(!cur.dragging());
    CHECK_EQ(in.types().back(), abi::MSG_LeftMouseUp);
    // circle during a move cancels it, with no move it closes
    w.onButton(ctx, B_TRIANGLE, true); in.clear(); w.onButton(ctx, B_CIRCLE, true);
    CHECK(!cur.dragging()); CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_Escape_VALUE);
    in.clear(); w.onButton(ctx, B_CIRCLE, true);
    CHECK_EQ(in.log.back().kind, Event::Action); CHECK(in.log.back().id == "inventoryClose");
    // L1 is "up", R1 the view. Pressed through the Press property, not the mouse: the
    // client takes no mouse messages in game mode, and until 9 Sep 2026 both were silent
    // because of it.
    NodePtr up = inv->byPath("comp.Container.info.up");
    NodePtr view = inv->byPath("comp.Container.buttonView");
    in.clear(); CHECK(w.onButton(ctx, B_L1, true));
    CHECK(std::static_pointer_cast<MockNode>(up)->pressed());
    CHECK(in.log.empty());                                   // the mouse was not touched
    in.clear(); CHECK(w.onButton(ctx, B_R1, true));
    CHECK(std::static_pointer_cast<MockNode>(view)->pressed());
    // on a button, cross is a single click, square is not handled
    cur.setCurrentNode(inv->byPath("bg.mmc.close"));
    in.clear(); w.onButton(ctx, B_CROSS, true); CHECK_EQ(in.types().size(), 2u); CHECK_EQ(in.types()[0], abi::MSG_LeftMouseDown);
    CHECK(!w.onButton(ctx, B_SQUARE, true));
    // Captions in Latin-1 only - the stock fonts cover nothing else. The legend starts
    // with navigation: D-pad and both sticks, one text.
    h = w.hints(ctx);
    CHECK_EQ((int)h[0].glyph, (int)crossbar::Glyph::DPAD);   CHECK(h[0].text.empty());
    CHECK_EQ((int)h[1].glyph, (int)crossbar::Glyph::STICK_L); CHECK(h[1].text.empty());
    CHECK_EQ((int)h[2].glyph, (int)crossbar::Glyph::STICK_R); CHECK(h[2].text == u"Navigate");
    CHECK(h[3].text == u"Press");
    // releases do nothing
    in.clear(); CHECK(!w.onButton(ctx, B_CROSS, false)); CHECK(in.log.empty());
    inv->setVisible(false); CHECK(!w.isOpen());

    // The page must not be cached: the client destroys it on close and a freed pointer
    // cannot be checked - that is the ACCESS VIOLATION on leaving the game. Pull the
    // window out of the tree and the profile has to say "closed" rather than reach into
    // the old pointer. It hangs off GroundHUD, so that is where it is removed from.
    std::static_pointer_cast<MockNode>(inv->parent())->removeChild(inv);
    CHECK(!w.page());
    CHECK(!w.isOpen());
}

TEST(popup_menu_navigation)
{
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr ctxPage = root->add("ctx", "Page", 0, 0, 1920, 1080);
    MockPtr menu = ctxPage->add("popup", "PopupMenu", 300, 300, 120, 60);
    menu->add("a", "Button", 0, 0, 120, 20); menu->add("b", "Button", 0, 20, 120, 20); menu->add("c", "Button", 0, 40, 120, 20);
    MockInput in; Ui ui(in); Cursor cur(ui); input::Pad pad; HintBar hints;
    Context c = {cur, ui, pad, hints};
    Popup w([&]() -> NodePtr { return ctxPage; });
    CHECK(w.isOpen()); CHECK(!w.radial());
    cur.setRoot(w.page(), w.rules()); CHECK_EQ(cur.nodeCount(), 3u);
    cur.setCurrentNode(menu->child("a"));
    cur.navigate(DIR_DOWN); CHECK(cur.current()->name == "b");
    cur.navigate(DIR_DOWN); cur.navigate(DIR_DOWN); CHECK(cur.current()->name == "a");   // wrap
    in.clear(); CHECK(w.onButton(c, B_CROSS, true)); CHECK_EQ(in.types()[0], abi::MSG_LeftMouseDown);
    CHECK_EQ(in.log.back().m.mouseY, cur.current()->center.y);
    in.clear(); CHECK(w.onButton(c, B_CIRCLE, true)); CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_Escape_VALUE);
}

TEST(popup_combo_list_is_driven_by_keys)
{
    // A dropdown goes into the context widget as a separate page and the client focuses
    // the list itself (UIComboBox::PerformPopup). There is one widget there and nowhere
    // for the cursor to go, so before this the D-pad did nothing in dropdowns (reported
    // 10 Sep 2026). The hierarchy was measured the same day and is a level deeper than
    // it looked: InvisibleContextPage -> TransientComboPopup -> TransientComboList. A
    // mock with the popup AS the context page stayed green while the branch was silent
    // in game, because the search only looked at direct children.
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr ctxPage = root->add("InvisibleContextPage", "Page", 0, 0, 1920, 1080);
    MockPtr popup = ctxPage->add("TransientComboPopup", "Page", 300, 300, 200, 220);
    popup->add("TransientComboList", "List", 0, 20, 200, 200);
    MockInput in; Ui ui(in); Cursor cur(ui); input::Pad pad; HintBar hints;
    Context c = {cur, ui, pad, hints};
    Popup w([&]() -> NodePtr { return ctxPage; });
    CHECK(w.isOpen()); CHECK(!w.radial()); CHECK(w.comboList() != nullptr);

    // UIList walks the rows on arrows, so arrows is what we send - and focus goes in
    // before each one: in game the ring stood around the whole list and the rows never
    // moved, because focus had left it.
    MockPtr lst = std::static_pointer_cast<MockNode>(w.comboList());
    in.clear(); CHECK(w.onButton(c, B_DPAD_DOWN, true));
    CHECK(lst->prop(abi::PROP_Focus) == u"true");
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_DownArrow_VALUE);
    in.clear(); CHECK(w.onButton(c, B_DPAD_UP, true));
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_UpArrow_VALUE);
    // The client handles Enter on the popup page: it takes the highlighted row.
    in.clear(); CHECK(w.onButton(c, B_CROSS, true));
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_Enter_VALUE);
    in.clear(); CHECK(w.onButton(c, B_R2, true));
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_PageDown_VALUE);
    in.clear(); CHECK(w.onButton(c, B_CIRCLE, true));
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_Escape_VALUE);

    // Sideways means nothing here and must not leak out either - a window lies under
    // the popup and its cursor would move blind.
    in.clear(); CHECK(w.onButton(c, B_DPAD_LEFT, true)); CHECK(in.log.empty());

    // Holding pages through: the first step on the press, auto-repeat after that.
    in.clear(); w.onButton(c, B_DPAD_DOWN, true); in.clear();
    w.onTick(c, 1.0f);
    CHECK(!in.log.empty());
    CHECK_EQ(in.log[0].m.keystroke, abi::UIMessage_key_DownArrow_VALUE);
    // A release stops the repeat.
    CHECK(w.onButton(c, B_DPAD_DOWN, false));
    in.clear(); w.onTick(c, 1.0f); CHECK(in.log.empty());
}

TEST(radial_menu_by_stick)
{
    CHECK_EQ(Popup::sectorOf(0, 0, 8), -1);
    CHECK_EQ(Popup::sectorOf(0, -1, 8), 0);          // up
    CHECK_EQ(Popup::sectorOf(1, 0, 8), 2);           // right
    CHECK_EQ(Popup::sectorOf(0, 1, 8), 4);           // down
    CHECK_EQ(Popup::sectorOf(-1, 0, 8), 6);          // left
    CHECK_EQ(Popup::sectorOf(0.7f, -0.7f, 8), 1);    // up-right
    CHECK_EQ(Popup::sectorOf(0, -1, 4), 0); CHECK_EQ(Popup::sectorOf(1, 0, 4), 1);
    CHECK_EQ(Popup::sectorOf(0.3f, 0, 8), -1);       // the 0.5 dead zone

    MockPtr root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr ctxPage = root->add("ctx", "Page", 0, 0, 1920, 1080);
    MockPtr rad = ctxPage->add("radial", "RadialMenu", 400, 400, 200, 200);
    // The items stand in a circle round the menu centre, as the client places them, not
    // in a column: the selection comes from their real angles and a column fairly fails.
    // A menu at 400,400 sized 200x200 -> world centre 500,500; buttons 40x40.
    const char* names[4] = {"use", "examine", "drop", "destroy"};
    const int loc[4][2] = {{80, 10}, {150, 80}, {80, 150}, {10, 80}};   // up, right, down, left
    for (int i = 0; i < 4; ++i) rad->add(names[i], "Button", loc[i][0], loc[i][1], 40, 40);
    MockInput in; Ui ui(in); Cursor cur(ui); input::Pad pad; HintBar hints;
    pad.setProfile("ds4"); for (int ofs = input::DI_X; ofs <= input::DI_RZ; ofs += 4) pad.setAxisRange(ofs, 0, 65535);
    Context c = {cur, ui, pad, hints};
    Popup w([&]() -> NodePtr { return ctxPage; });
    CHECK(w.radial() == rad);
    cur.setRoot(w.page(), w.rules());
    // right stick right (Z is the right stick X on ds4): sector 1 of 4
    input::DiEvent buf[2]; uint32_t n = 0; buf[n++] = input::DiEvent{input::DI_Z, 65535, 0, 1, 0};
    pad.feed(buf, n, 2, false);
    w.onTick(c, 0.016f);
    CHECK_EQ(w.hoverSector(), 1); CHECK(cur.current()->name == "examine");
    in.clear(); CHECK(w.onButton(c, B_CROSS, true));
    CHECK_EQ(in.log[0].m.type, abi::MSG_Character); CHECK_EQ(in.log[0].m.keystroke, u'2');
    // Both sticks are in the legend: the radial can be turned with either.
    std::vector<Hint> h = w.hints(c);
    CHECK_EQ((int)h[0].glyph, (int)crossbar::Glyph::STICK_L); CHECK(h[0].text.empty());
    CHECK_EQ((int)h[1].glyph, (int)crossbar::Glyph::STICK_R); CHECK(h[1].text == u"Select");
    // The left stick leads the radial on a par with the right one.
    pad.endFrame(0.016f);
    n = 0; buf[n++] = input::DiEvent{input::DI_Z, 32767, 0, 2, 0};
    buf[n++] = input::DiEvent{input::DI_Y, 0, 0, 3, 0};                 // the left stick up
    pad.feed(buf, n, 2, false);
    w.onTick(c, 0.016f);
    CHECK_EQ(w.hoverSector(), 0); CHECK(cur.current()->name == "use");
    // Past every item - nothing is selected, rather than "the nearest one somehow".
    std::vector<NodePtr> items;
    for (const NodePtr& k : rad->children()) if (k->isA(T_Button)) items.push_back(k);
    CHECK_EQ(Popup::nearestTo(items, rad->center(), 0.f, 0.f), -1);     // the dead zone
    CHECK_EQ(Popup::nearestTo(items, rad->center(), 1.f, 0.f), 1);      // right - examine
    CHECK_EQ(Popup::nearestTo(items, rad->center(), 0.f, 1.f), 2);      // down - drop
}

TEST(gamebar_leaves_the_button_bar_alone)
{
    // SwgCuiButtonBar declares setSettingsAutoSizeLocation(true, true), so the client
    // saves this page's Location and Size into the player's settings. Move it under the
    // ring and the panel stays mid-screen after a reload. Hence clones, and the original
    // untouched.
    MockPtr hud = MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    MockPtr bar = hud->add("ButtonBar", "Page", 30, 700, 264, 24);
    MockPtr vs = bar->add("vs", "VolumePage", 0, 0, 264, 24);
    for (int i = 0; i < 12; ++i) vs->add(std::string(1, char('0' + i % 10)) + "b", "Button", i * 22, 0, 22, 22);

    MockInput in; Ui ui(in); cursor::Cursor cur(ui); cursor::HintBar hints; cp::input::Pad pad;
    cursor::Context ctx{cur, ui, pad, hints};
    GameBar gb([bar]() -> NodePtr { return bar; });

    const UIPoint before = bar->location(); const UISize sizeBefore = bar->size();
    const UIPoint volBefore = vs->location();
    gb.toggle();
    gb.onFocus(ctx);
    CHECK_EQ(bar->location().x, before.x); CHECK_EQ(bar->location().y, before.y);
    CHECK_EQ(bar->size().x, sizeBefore.x); CHECK_EQ(bar->size().y, sizeBefore.y);
    CHECK_EQ(vs->location().x, volBefore.x);

    // The ring was built after all: the clones landed in the HUD overlay.
    int clones = 0;
    for (const NodePtr& k : hud->children())
        if (k->name().compare(0, 10, "cpBarClone") == 0) ++clones;
    CHECK(clones > 0);

    gb.onBlur(ctx);
    clones = 0;
    for (const NodePtr& k : hud->children())
        if (k->name().compare(0, 10, "cpBarClone") == 0) ++clones;
    CHECK_EQ(clones, 0);                      // closed - the clones came down
    CHECK_EQ(bar->location().x, before.x);
}

TEST(gamebar_ring_geometry_and_toggle)
{
    // Sector 0 at the top, clockwise, as Popup::sectorOf computes - otherwise the
    // highlight drifts off the selection.
    int dx = 0, dy = 0;
    GameBar::ringPoint(0, 4, 100, dx, dy); CHECK_EQ(dx, 0);   CHECK_EQ(dy, -100);
    GameBar::ringPoint(1, 4, 100, dx, dy); CHECK_EQ(dx, 100); CHECK_EQ(dy, 0);
    GameBar::ringPoint(2, 4, 100, dx, dy); CHECK_EQ(dx, 0);   CHECK_EQ(dy, 100);
    GameBar::ringPoint(3, 4, 100, dx, dy); CHECK_EQ(dx, -100);CHECK_EQ(dy, 0);
    // Every sector of the ring corresponds to the same cell the stick will pick.
    for (int i = 0; i < 12; ++i) {
        GameBar::ringPoint(i, 12, 100, dx, dy);
        CHECK_EQ(Popup::sectorOf(dx / 100.f, dy / 100.f, 12), i);
    }
    // The radius fits every cell: the circumference covers their sum plus the gap.
    const int r = GameBar::ringRadius(12, 30, 15);
    CHECK(2 * 3.14159f * r >= 12 * (30 + 15) - 1.f);
    CHECK(GameBar::ringRadius(4, 30, 15) >= 45);      // it does not collapse on a small count

    GameBar gb([]() -> NodePtr { return NodePtr(); });
    CHECK(!gb.open());
    CHECK(!gb.toggle());                              // with no page it does not open
    CHECK(!gb.open());
}

TEST(commands_bind_resolves_button_and_combo)
{
    int pane = -1, slot = -1;
    // No triggers means the main set, and the slot comes from the same CLUSTERS table
    // the crossbar uses - otherwise the binding lands off the cell.
    CHECK(Commands::resolve(B_TRIANGLE, false, false, pane, slot));
    CHECK_EQ(pane, (int)PANE_MAIN); CHECK_EQ(slot, 4);
    CHECK(Commands::resolve(B_TRIANGLE, true, false, pane, slot));
    CHECK_EQ(pane, (int)PANE_L2);   CHECK_EQ(slot, 4);
    CHECK(Commands::resolve(B_TRIANGLE, false, true, pane, slot));
    CHECK_EQ(pane, (int)PANE_R2);   CHECK_EQ(slot, 4);
    CHECK(Commands::resolve(B_TRIANGLE, true, true, pane, slot));
    CHECK_EQ(pane, (int)PANE_L2R2); CHECK_EQ(slot, 4);
    // The D-pad and the stick presses are clusters too.
    CHECK(Commands::resolve(B_DPAD_LEFT, false, false, pane, slot)); CHECK_EQ(slot, 1);
    CHECK(Commands::resolve(B_R3, true, false, pane, slot)); CHECK_EQ(slot, 9); CHECK_EQ(pane, (int)PANE_L2);
    // The triggers themselves and the service buttons form no combo.
    CHECK(!Commands::resolve(B_L2, false, false, pane, slot));
    CHECK(!Commands::resolve(B_R2, false, false, pane, slot));
    CHECK(!Commands::resolve(B_OPTIONS, false, false, pane, slot));
    CHECK(!Commands::resolve(B_L1, true, true, pane, slot));
    // Every cluster gives its OWN slot: two buttons into one slot is a silent loss.
    bool seen[SLOT_COUNT] = {};
    for (int i = 0; i < CLUSTER_COUNT; ++i) {
        CHECK(Commands::resolve(CLUSTERS[i].button, false, false, pane, slot));
        CHECK(slot >= 0 && slot < SLOT_COUNT); CHECK(!seen[slot]); seen[slot] = true;
    }
}

TEST(commands_cancel_button_is_not_bindable_cluster)
{
    // The cancel must not sit on a bindable button: while it hung on circle, circle
    // could not be bound at all.
    int pane = -1, slot = -1;
    CHECK(!Commands::resolve(B_R1, false, false, pane, slot));   // R1 is not a cluster - it will do for the cancel
    CHECK(Commands::resolve(B_CIRCLE, false, false, pane, slot));// circle is a cluster button
    CHECK_EQ(slot, 6);
    // And all ten cluster buttons are obliged to stay bindable.
    for (int i = 0; i < CLUSTER_COUNT; ++i)
        CHECK(Commands::resolve(CLUSTERS[i].button, false, false, pane, slot));
}
