// Cursor tests: node collection, navigation over a grid, a list and tabs (the
// cone, the half-plane, the wrap), repeat on hold, gestures, dragging, the
// visuals, the hints and the window stack.
#include "test.h"
#include "mock_node.h"
#include "../cursor/stack.h"

using namespace cp; using namespace cp::mock; using namespace cp::cursor; using namespace cp::gen;

// the window: a 3x2 grid of volume cells + two buttons below + a hidden button
static MockPtr makeWindow(MockPtr& hud)
{
    hud = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr win = hud->add("Win", "Page", 100, 100, 400, 300);
    MockPtr vol = win->add("volume", "VolumePage", 10, 10, 300, 150);
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 3; ++c) { char nm[8]; std::snprintf(nm, 8, "c%d%d", r, c); vol->add(nm, "Viewer", 4 + c * 68, 4 + r * 68, 64, 64); }
    win->add("ok", "Button", 20, 250, 80, 24);
    win->add("cancel", "Button", 200, 250, 80, 24);
    MockPtr hidden = win->add("hidden", "Button", 300, 250, 80, 24); hidden->setVisible(false);
    win->add("label", "Text", 20, 200, 100, 20);
    return win;
}

TEST(nodes_scan_collects_cells_and_buttons)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    ScanRules r; std::vector<NodeInfo> n = scanNodes(win, r);
    CHECK_EQ(n.size(), 8u);                    // 6 cells + 2 buttons; the hidden one and the text are out
    int cells = 0, buttons = 0; for (const NodeInfo& i : n) { if (i.kind == NK_Cell) ++cells; if (i.kind == NK_Button) ++buttons; }
    CHECK_EQ(cells, 6); CHECK_EQ(buttons, 2);
    CHECK_EQ(n[0].rect.left, 100 + 10 + 4); CHECK_EQ(n[0].center.x, 114 + 32);
    r.ignore.push_back("cancel"); CHECK_EQ(scanNodes(win, r).size(), 7u);
    ScanRules p; p.pass.push_back("volume"); CHECK_EQ(scanNodes(win, p).size(), 2u);
    win->setVisible(false); CHECK_EQ(scanNodes(win, ScanRules()).size(), 0u);
}

TEST(navigate_grid_cone_halfplane_wrap)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    std::vector<NodeInfo> n = scanNodes(win, ScanRules());
    // c00 c01 c02 / c10 c11 c12 / ok cancel
    int c00 = 0, c01 = 1, c02 = 2, c10 = 3, c11 = 4, c12 = 5, ok = 6, cancel = 7;
    CHECK_EQ(bestInDirection(n, c00, DIR_RIGHT, true), c01);
    CHECK_EQ(bestInDirection(n, c01, DIR_RIGHT, true), c02);
    CHECK_EQ(bestInDirection(n, c02, DIR_RIGHT, true), c00);        // wrap along the row
    CHECK_EQ(bestInDirection(n, c02, DIR_RIGHT, false), -1);
    CHECK_EQ(bestInDirection(n, c00, DIR_DOWN, true), c10);
    CHECK_EQ(bestInDirection(n, c11, DIR_DOWN, true), ok);          // below the grid - the nearest button (half-plane)
    CHECK_EQ(bestInDirection(n, c12, DIR_DOWN, true), cancel);
    CHECK_EQ(bestInDirection(n, ok, DIR_UP, true), c10);
    CHECK_EQ(bestInDirection(n, cancel, DIR_RIGHT, true), ok);      // wrap
    CHECK_EQ(bestInDirection(n, ok, DIR_DOWN, true), c00);          // wrap up the column: the furthest one behind on the same line
    CHECK_EQ(bestInDirection(n, c00, DIR_LEFT, true), c02);
    CHECK_EQ(closestTo(n, UIPoint{150, 350}), ok); CHECK_EQ(closestTo(n, UIPoint{330, 350}), cancel);
    NodePtr none;
    CHECK_EQ(arbitrary(n, none, none, UIPoint{0, 0}), c00);
    CHECK_EQ(arbitrary(n, n[cancel].node, none, UIPoint{0, 0}), cancel);
    CHECK_EQ(arbitrary(n, none, n[ok].node, UIPoint{0, 0}), ok);
    ScanRules pr; pr.priority["cancel"] = 3;
    std::vector<NodeInfo> np = scanNodes(win, pr);
    CHECK_EQ(arbitrary(np, none, none, UIPoint{0, 0}), cancel);
}

TEST(cursor_navigation_repeat_and_gestures)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    MockInput in; Ui ui(in); Cursor cur(ui);
    cur.setRoot(win, ScanRules());
    CHECK_EQ(cur.nodeCount(), 8u);
    CHECK(cur.current() != nullptr);
    CHECK(cur.current()->name == "c12");                       // with no history - the closest to the window center
    cur.setCurrentNode(win->byPath("volume.c00"));
    in.clear();
    cur.input(DIR_RIGHT, true);
    CHECK(cur.current()->name == "c01");
    CHECK_EQ(in.log.size(), 2u); CHECK_EQ(in.log[0].kind, Event::Warp);   // the mouse arrived at the node center
    CHECK_EQ(in.log[0].x, cur.current()->center.x);
    cur.tick(0.1f); CHECK(cur.current()->name == "c01");       // 0.125 to the first repeat
    cur.tick(0.05f); CHECK(cur.current()->name == "c02");      // a repeat
    cur.tick(0.125f); CHECK(cur.current()->name == "c00");     // one more repeat: the wrap
    cur.input(DIR_RIGHT, false); cur.tick(0.5f); CHECK(cur.current()->name == "c00");
    // gestures
    in.clear(); cur.click();
    std::vector<int> t = in.types(); CHECK_EQ(t.size(), 2u); CHECK_EQ(t[0], abi::MSG_LeftMouseDown);
    CHECK_EQ(in.log.back().m.mouseX, cur.current()->center.x);
    in.clear(); cur.doubleClick(); CHECK_EQ(in.types()[1], abi::MSG_LeftMouseDoubleClick);
    in.clear(); cur.context(); CHECK_EQ(in.types()[0], abi::MSG_ContextRequest);
    in.clear(); cur.scroll(-2); CHECK_EQ(in.types()[0], abi::MSG_MouseWheel);
    // drag: start on c00, move to c02, drop
    in.clear(); CHECK(cur.dragStart()); CHECK(cur.dragging());
    cur.navigate(DIR_RIGHT); cur.navigate(DIR_RIGHT);
    CHECK(cur.current()->name == "c02");
    cur.dragDrop(); CHECK(!cur.dragging());
    t = in.types(true);
    size_t down = 0; while (down < t.size() && t[down] != abi::MSG_LeftMouseDown) ++down;
    CHECK(down > 0 && down < t.size()); CHECK_EQ(t[0], abi::MSG_MouseMove);
    CHECK_EQ(t.back(), abi::MSG_LeftMouseUp); CHECK_EQ(in.log.back().m.mouseX, cur.current()->center.x);
    // cancelling a drag - Escape
    in.clear(); cur.dragStart(); cur.dragCancel(); CHECK(!cur.dragging());
    CHECK_EQ(in.log.back().m.type, abi::MSG_KeyUp); CHECK_EQ(in.log[in.log.size() - 2].m.keystroke, abi::UIMessage_key_Escape_VALUE);
}

TEST(cursor_survives_rescan_and_hidden_window)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    MockInput in; Ui ui(in); Cursor cur(ui);
    cur.setRoot(win, ScanRules()); cur.setCurrentNode(win->byPath("volume.c00"));
    cur.navigate(DIR_DOWN); cur.navigate(DIR_DOWN);
    CHECK(cur.current()->name == "ok");
    // the window was redrawn: the node is alive - we stay on it
    cur.tick(0.25f); CHECK(cur.current()->name == "ok");
    // the node is gone - we move to the nearest one
    std::static_pointer_cast<MockNode>(win->child("ok"))->setVisible(false);
    cur.tick(0.25f); CHECK(cur.current() != nullptr); CHECK(cur.current()->name != "ok");
    // the window went away: there are no nodes
    win->setVisible(false); cur.tick(0.25f);
    CHECK(cur.current() == nullptr);
    win->setVisible(true); cur.tick(0.25f); CHECK(cur.current() != nullptr);
    cur.clearRoot(); CHECK(!cur.active()); CHECK_EQ(cur.nodeCount(), 0u);
}

TEST(cursor_focus_ring_drops_the_template_decor)
{
    // The frame is cloned off a toolbar corner, which carries the pane number and the
    // paging arrows: leave them visible and they ride across the screen with the frame,
    // the way the crossbar backdrops multiplied. There is no pointer arrow at all.
    MockPtr hud; MockPtr win = makeWindow(hud);
    MockPtr overlay = hud->add("cpOverlay", "Page", 0, 0, 1920, 1080);
    MockPtr frame = hud->add("frame", "Page", 0, 0, 8, 8);
    frame->add("textPane", "Text", 0, 0, 8, 8);
    frame->add("arrows", "Button", 0, 0, 8, 8);
    MockInput in; Ui ui(in); Cursor cur(ui);
    cur.attachVisuals(overlay, NodePtr(), frame);

    NodePtr foc = overlay->child("cpFocus");
    CHECK(foc);
    CHECK(!overlay->child("cpCursor"));                 // there is no pointer
    CHECK(foc->child("textPane") && !foc->child("textPane")->willDraw());
    CHECK(foc->child("arrows") && !foc->child("arrows")->willDraw());
    // Four bars along the edges: the client's backdrop styles give a soft fill, and on a
    // green inventory cell that looks like the stock highlight.
    for (int i = 0; i < 4; ++i) { char nm[16]; std::snprintf(nm, 16, "cpFocusBar%d", i); CHECK(foc->child(nm)); }

    cur.setRoot(win, ScanRules()); cur.setCurrentNode(win->byPath("volume.c00")); cur.tick(0.016f);
    const NodeInfo* n = cur.current();
    CHECK(foc->willDraw());
    CHECK_EQ(foc->location().y, n->rect.top - 2);
    CHECK_EQ(foc->size().y, n->rect.bottom - n->rect.top + 4);
    // the bars line the frame from the inside: top and bottom full width, sides full height
    CHECK_EQ(foc->child("cpFocusBar0")->size().x, foc->size().x);
    CHECK_EQ(foc->child("cpFocusBar1")->location().y, foc->size().y - 4);
    CHECK_EQ(foc->child("cpFocusBar3")->location().x, foc->size().x - 4);
    CHECK_EQ(foc->child("cpFocusBar2")->size().y, foc->size().y);
    cur.clearRoot(); cur.tick(0.016f);
    CHECK(!foc->willDraw());                            // the window left - so did the frame
}

TEST(nodes_take_buttons_the_client_refuses_to_select)
{
    // The client's CanSelect refuses the inventory's buttons - 16 cells and zero
    // buttons, though they are visible and pressable. The scanner has to judge such
    // leaves itself, or the frame reaches neither close nor the view switch.
    MockPtr root = MockNode::make("root", "Page", 0, 0, 800, 600);
    MockPtr win = root->add("win", "Page", 0, 0, 400, 300);
    MockPtr b1 = win->add("close", "Button", 10, 10, 20, 20);
    MockPtr b2 = win->add("view", "Button", 40, 10, 20, 20);
    b1->setCanSelect(0); b2->setCanSelect(0);            // the client says "not allowed"
    std::vector<NodeInfo> got = scanNodes(win, ScanRules());
    CHECK_EQ(got.size(), 2u);
    CHECK(got[0].kind == NK_Button);

    // a disabled button is still not taken
    b2->setFlags(BF_Visible);
    got = scanNodes(win, ScanRules());
    CHECK_EQ(got.size(), 1u);
}

TEST(cursor_visuals_follow_current_node)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    MockPtr overlay = hud->add("cpOverlay", "Page", 0, 0, 1920, 1080);
    MockPtr img = hud->add("img", "Image", 0, 0, 8, 8);
    MockPtr frame = hud->add("frame", "Page", 0, 0, 8, 8);
    MockInput in; Ui ui(in); Cursor cur(ui);
    cur.attachVisuals(overlay, img, frame);
    // No pointer arrow: the atlas has no sprite for one. The position is still tracked -
    // the gestures aim by it - and what gets drawn is the frame.
    CHECK(!overlay->child("cpCursor"));
    CHECK(overlay->child("cpFocus"));
    cur.setRoot(win, ScanRules()); cur.setCurrentNode(win->byPath("volume.c00")); cur.tick(0.016f);
    NodePtr foc = overlay->child("cpFocus");
    CHECK(foc->willDraw());
    const NodeInfo* n = cur.current();
    CHECK_EQ(foc->location().x, n->rect.left - 2); CHECK_EQ(foc->size().x, n->rect.right - n->rect.left + 4);
    UIPoint p0 = cur.pointerPos();
    CHECK_EQ(p0.x, n->center.x - 2);
    cur.navigate(DIR_RIGHT); cur.tick(0.016f);
    UIPoint p1 = cur.pointerPos();
    CHECK(p1.x > p0.x && p1.x < cur.current()->center.x);       // on the way (exponentially)
    for (int i = 0; i < 60; ++i) cur.tick(0.016f);
    CHECK_EQ(cur.pointerPos().x, cur.current()->center.x - 2);
    cur.clearRoot(); cur.tick(0.016f); CHECK(!foc->willDraw());
    cur.detachVisuals(); CHECK(!overlay->child("cpFocus"));
}

TEST(hintbar_builds_items)
{
    MockPtr hud = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr overlay = hud->add("cpOverlay", "Page", 0, 0, 1920, 1080);
    MockPtr img = hud->add("img", "Image", 0, 0, 8, 8), txt = hud->add("txt", "Text", 0, 0, 8, 8);
    HintBar hb; hb.attach(overlay, img, txt, 40, 1000);
    std::vector<Hint> h; h.push_back(Hint{B_CROSS, u"Use"}); h.push_back(Hint{B_CIRCLE, u"Close"});
    hb.set(h);
    CHECK_EQ(overlay->children().size(), 4u);
    MockPtr g0 = std::static_pointer_cast<MockNode>(overlay->child("cpHintG0"));
    MockPtr t1 = std::static_pointer_cast<MockNode>(overlay->child("cpHintT1"));
    CHECK(g0 && t1);
    CHECK(g0->prop(abi::PROP_SourceRect) == u"224,0,256,32");   // face_bottom (index 7)
    CHECK(t1->text() == u"Close");
    CHECK(t1->location().x > g0->location().x); CHECK(hb.width() > 0);
    hb.set(h); CHECK_EQ(overlay->children().size(), 4u);          // unchanged - we do not rebuild

    // The bar rides with the window: moveTo shifts the finished elements instead of
    // rebuilding them, so it can move every frame.
    UIPoint g0was = g0->location(), t1was = t1->location();
    hb.moveTo(140, 900);
    CHECK_EQ(g0->location().x, g0was.x + 100); CHECK_EQ(g0->location().y, g0was.y - 100);
    CHECK_EQ(t1->location().x, t1was.x + 100); CHECK_EQ(t1->location().y, t1was.y - 100);
    CHECK_EQ(overlay->children().size(), 4u);
    hb.moveTo(140, 900);                                          // a repeat moves nothing
    CHECK_EQ(g0->location().x, g0was.x + 100);

    hb.clear(); CHECK_EQ(overlay->children().size(), 0u);
    hb.detach();
}

// a stub window for the stack
struct FakeWin : Window {
    MockPtr pg; const char* nm; int pr; ScanRules r; int focus = 0, blur = 0; Button last = B_COUNT;
    FakeWin(MockPtr p, const char* n, int prio) : pg(p), nm(n), pr(prio) {}
    const char* id() const override { return nm; }
    NodePtr page() const override { return pg; }
    int priority() const override { return pr; }
    const ScanRules& rules() const override { return r; }
    std::vector<Hint> hints(const Context&) const override { std::vector<Hint> h; h.push_back(Hint{B_CROSS, u"x"}); return h; }
    void onFocus(Context&) override { ++focus; }
    void onBlur(Context&) override { ++blur; }
    bool onButton(Context&, Button b, bool pressed) override { if (pressed) last = b; return b == B_CROSS; }
};

TEST(stack_focus_and_ui_mode)
{
    MockPtr hud; MockPtr win = makeWindow(hud);
    MockPtr popup = hud->add("ctx", "Page", 500, 500, 100, 100); popup->add("item", "Button", 0, 0, 100, 20); popup->setVisible(false);
    win->setVisible(false);
    MockInput in; Ui ui(in); Cursor cur(ui); input::Pad pad; HintBar hints;
    Context ctx = {cur, ui, pad, hints};
    Stack st;
    FakeWin* w = new FakeWin(win, "win", 10); FakeWin* p = new FakeWin(popup, "popup", 100);
    st.add(std::unique_ptr<Window>(w)); st.add(std::unique_ptr<Window>(p));
    CHECK(!st.update(ctx)); CHECK(!st.uiMode()); CHECK(!pad.uiMode());
    win->setVisible(true);
    CHECK(st.update(ctx)); CHECK(st.focused() == w); CHECK(pad.uiMode()); CHECK_EQ(w->focus, 1);
    CHECK(cur.root() == win); CHECK_EQ(hints.hints().size(), 1u);
    popup->setVisible(true);
    CHECK(st.update(ctx)); CHECK(st.focused() == p); CHECK_EQ(w->blur, 1); CHECK(cur.root() == popup);
    popup->setVisible(false);
    CHECK(st.update(ctx)); CHECK(st.focused() == w); CHECK_EQ(w->focus, 2);
    // buttons: CROSS is handled by the window, the D-pad goes to the cursor
    input::DiEvent buf[4]; uint32_t n = 0;
    buf[n++] = input::DiEvent{input::DI_BUTTON0 + 1u, 0x80, 0, 1, 0};     // CROSS on ds4
    pad.feed(buf, n, 4, false);
    st.dispatch(ctx, 0.016f); CHECK(w->last == B_CROSS);
    pad.endFrame(0.016f);
    n = 0; buf[n++] = input::DiEvent{input::DI_POV0, 9000, 0, 2, 0}; pad.feed(buf, n, 4, false);
    std::string before = cur.current()->name;
    st.dispatch(ctx, 0.016f); CHECK(cur.current()->name != before);
    CHECK(w->last == B_DPAD_RIGHT);
    // The stick moves the cursor as well as the D-pad: after a radial, where everything
    // is done with a stick, D-pad only reads as a loss of control.
    pad.endFrame(0.016f);
    pad.setAxisRange(input::DI_X, 0, 65535); pad.setAxisRange(input::DI_Y, 0, 65535);
    // Left, not right: the D-pad above is still held right, and a hold gives no second
    // step in the same direction.
    n = 0; buf[n++] = input::DiEvent{input::DI_X, 0, 0, 3, 0}; pad.feed(buf, n, 4, false);
    before = cur.current()->name;
    st.dispatch(ctx, 0.016f); CHECK(cur.current()->name != before);   // the first step at once
    before = cur.current()->name;
    st.dispatch(ctx, 0.016f); CHECK(cur.current()->name == before);   // held - a pause before auto-repeat
    st.dispatch(ctx, 0.4f);   CHECK(cur.current()->name != before);   // waited it out - a repeat
    // The dead zone: near the center the stick does not move the cursor.
    n = 0; buf[n++] = input::DiEvent{input::DI_X, 32767, 0, 4, 0}; pad.feed(buf, n, 4, false);
    before = cur.current()->name;
    st.dispatch(ctx, 0.4f); CHECK(cur.current()->name == before);

    win->setVisible(false);
    CHECK(st.update(ctx)); CHECK(!st.uiMode()); CHECK(!pad.uiMode()); CHECK(!cur.active());
}
