// Stage 0 tests: ABI verification against the real exe (reading the file), node
// geometry, Ui gestures in terms of client messages, the tick and the tween.
#include "test.h"
#include "mock_node.h"
#include "../core/binary.h"
#include "../core/tick.h"
#include "../core/runtime.h"
#include "../core/hooks.h"

using namespace cp; using namespace cp::mock; using namespace cp::abi;

namespace cp { namespace bin { bool mockExeAvailable(); } }

TEST(abi_verify_on_real_exe)
{
    if (!bin::mockExeAvailable()) { std::printf("  (swgemu.exe not found, set CP_EXE=path)\n"); return; }
    std::string err = bin::verify();
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(err.empty());
    CHECK_EQ(bin::keyEscape(), UIMessage_key_Escape_VALUE);
    CHECK_EQ(bin::keyHome(), UIMessage_key_Home_VALUE);
    CHECK_EQ(bin::keyEnd(), UIMessage_key_End_VALUE);
}

TEST(abi_props_sorted_unique)
{
    size_t n = sizeof(PROPS) / sizeof(PROPS[0]);
    CHECK(n > 1500);
    bool ok = true;
    for (size_t i = 1; i < n; ++i) if (std::string(PROPS[i - 1].name) >= PROPS[i].name) ok = false;
    CHECK(ok);
    CHECK_EQ(PROP_Visible, 0x01997908u ? PROP_Visible : 0u); // the constant exists
}

TEST(node_world_geometry)
{
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1920, 1080);
    MockPtr win = root->add("win", "Page", 100, 50, 400, 300);
    MockPtr vol = win->add("volume", "VolumePage", 10, 20, 300, 200);
    vol->setScroll(0, 40);
    MockPtr cell = vol->add("5", "Page", 46, 92, 40, 40);
    UIPoint w = cell->worldLocation();
    CHECK_EQ(w.x, 100 + 10 + 46);
    CHECK_EQ(w.y, 50 + 20 + 92 - 40);
    UIRect r = cell->worldRect();
    CHECK_EQ(r.right, w.x + 40); CHECK_EQ(r.bottom, w.y + 40);
    UIPoint c = cell->center();
    CHECK_EQ(c.x, w.x + 20); CHECK_EQ(c.y, w.y + 20);
    CHECK(root->byPath("win.volume.5").get() == cell.get());
    CHECK(!root->byPath("win.nothing"));
}

TEST(node_can_select_follows_parents)
{
    MockPtr root = MockNode::make("root", "Page");
    MockPtr win = root->add("win", "Page");
    MockPtr b = win->add("ok", "Button", 0, 0, 10, 10);
    CHECK(b->canSelect());
    win->setVisible(false);
    CHECK(!b->canSelect());
    win->setVisible(true);
    b->setPropA(PROP_Enabled, "false");
    CHECK(!b->canSelect());
    b->setPropA(PROP_Enabled, "true");
    b->setPropA(PROP_GetsInput, "false");
    CHECK(!b->canSelect());
}

TEST(node_move_child_order)
{
    MockPtr p = MockNode::make("p", "Page");
    MockPtr a = p->add("a", "Image"), b = p->add("b", "Image"), c = p->add("c", "Image");
    p->moveChild(c, 2);   // Top
    CHECK(p->children()[0].get() == c.get());
    p->moveChild(c, 3);   // Bottom
    CHECK(p->children()[2].get() == c.get());
    p->moveChild(b, 0);   // Up
    CHECK(p->children()[0].get() == b.get());
    p->moveChild(b, 1);   // Down
    CHECK(p->children()[1].get() == b.get());
}

TEST(ui_click_sequence)
{
    MockInput in; Ui ui(in);
    ui.click(300, 200);
    std::vector<int> t = in.types(true);
    CHECK_EQ(t.size(), 3u);
    CHECK_EQ(t[0], MSG_MouseMove); CHECK_EQ(t[1], MSG_LeftMouseDown); CHECK_EQ(t[2], MSG_LeftMouseUp);
    CHECK_EQ(in.log[0].kind, Event::Warp); CHECK_EQ(in.log[0].x, 300);
    CHECK_EQ(in.log[2].m.mouseX, 300); CHECK_EQ(in.log[2].m.mouseY, 200);
    in.clear();
    ui.click(300, 200);   // the same pixel: the warp is taken aside and back
    CHECK_EQ(in.log[0].kind, Event::Warp); CHECK_EQ(in.log[0].x, 301);
    CHECK_EQ(in.log[2].kind, Event::Warp); CHECK_EQ(in.log[2].x, 300);
}

TEST(ui_double_click_like_cuiiowin)
{
    MockInput in; Ui ui(in);
    ui.doubleClick(10, 10);
    std::vector<int> t = in.types();
    CHECK_EQ(t.size(), 3u);
    CHECK_EQ(t[0], MSG_LeftMouseDown); CHECK_EQ(t[1], MSG_LeftMouseDoubleClick); CHECK_EQ(t[2], MSG_LeftMouseUp);
}

TEST(ui_context_and_wheel_and_keys)
{
    MockInput in; Ui ui(in);
    ui.contextRequest(5, 6);
    CHECK_EQ(in.types().back(), MSG_ContextRequest);
    ui.wheel(5, 7, -3);
    CHECK_EQ(in.log.back().m.type, MSG_MouseWheel);
    CHECK_EQ(static_cast<int16_t>(in.log.back().m.data), -3);
    in.clear();
    ui.escape();
    CHECK_EQ(in.log[0].m.type, MSG_KeyDown); CHECK_EQ(in.log[0].m.keystroke, UIMessage_key_Escape_VALUE);
    CHECK_EQ(in.log[1].m.type, MSG_KeyUp);
    in.clear();
    ui.character(u'3');
    CHECK_EQ(in.log[0].m.type, MSG_Character); CHECK_EQ(in.log[0].m.keystroke, u'3');
    ui.action("inventoryClose");
    CHECK_EQ(in.log.back().kind, Event::Action); CHECK(in.log.back().id == "inventoryClose");
}

TEST(ui_drag_sequence)
{
    MockInput in; Ui ui(in);
    ui.dragStart(100, 100);
    ui.dragMove(200, 150);
    ui.dragDrop(250, 160);
    std::vector<int> t = in.types(true);
    // warp-move, down, move(threshold), move, move, up
    CHECK_EQ(t.size(), 6u);
    CHECK_EQ(t[1], MSG_LeftMouseDown); CHECK_EQ(t[2], MSG_MouseMove); CHECK_EQ(t[5], MSG_LeftMouseUp);
    CHECK_EQ(in.log.back().m.mouseX, 250);
}

TEST(tick_and_tween)
{
    Tick::get().reset();
    int calls = 0; float last = 0;
    int id = Tick::get().add([&](float dt) { ++calls; last = dt; });
    Tick::get().run(0.016f); Tick::get().run(5.0f);
    CHECK_EQ(calls, 2); CHECK(last == 0.25f);
    Tick::get().remove(id); Tick::get().run(0.016f);
    CHECK_EQ(calls, 2);

    Tween tw; tw.set(0.75f); tw.go(1.0f, 0.1f);
    tw.step(0.05f); CHECK(tw.value > 0.87f && tw.value < 0.88f); CHECK(tw.active());
    tw.step(0.05f); CHECK(tw.value == 1.0f); CHECK(!tw.active());
    tw.go(0.75f, 0); CHECK(tw.value == 0.75f);
}

TEST(uimessage_layout)
{
    CHECK_EQ(sizeof(UIMessage), 0x28u);
    UIMessage m; CHECK_EQ(m.type, 0); CHECK_EQ(m.keystroke, 0u);
    CHECK_EQ(reinterpret_cast<char*>(&m.keystroke) - reinterpret_cast<char*>(&m), OFF_UIMessage_Keystroke);
    CHECK_EQ(reinterpret_cast<char*>(&m.data) - reinterpret_cast<char*>(&m), OFF_UIMessage_Data);
    CHECK_EQ(reinterpret_cast<char*>(&m.mouseX) - reinterpret_cast<char*>(&m), OFF_UIMessage_MouseX);
    CHECK_EQ(reinterpret_cast<char*>(&m.dragTarget) - reinterpret_cast<char*>(&m), OFF_UIMessage_DragTarget);
}

TEST(runtime_mock_lifecycle)
{
    // on the binary mock: the ABI is checked against the exe file, the UI tree is
    // unavailable - the module installs, frames run, nothing crashes, no toolbar is found
    Config cfg; cfg.profile = "steam";
    bool ok = install(cfg);
    const Status& st = status();
    if (!bin::mockExeAvailable()) { CHECK(!ok); CHECK(!st.error.empty()); uninstall(); return; }
    CHECK(ok); CHECK(st.installed); CHECK(st.abiOk);
    for (int i = 0; i < 100; ++i) onFrame();
    CHECK(status().frames == 100u);
    CHECK(!status().crossbarAttached); CHECK(!status().cursorActive);
    uint32_t n = 0; onDeviceData(reinterpret_cast<void*>(1), nullptr, n, 0, false);
    CHECK(!onKey(73, true));
    onDeviceLost(reinterpret_cast<void*>(1));
    uninstall(); CHECK(!status().installed);
    onFrame();                                    // after uninstall - a no-op
}

static void* g_hookTarget = nullptr; static void* g_hookDetour = nullptr; static int g_removed = 0;
static void __fastcall fakeOriginalPack(void*, void*) {}
static bool fakeCreate(void* target, void* detour, void** original) { g_hookTarget = target; g_hookDetour = detour; *original = reinterpret_cast<void*>(&fakeOriginalPack); return true; }
static bool fakeRemove(void* target) { ++g_removed; return target == g_hookTarget; }

TEST(packguard_skips_only_guarded_volume)
{
    int a = 0, b = 0;
    packguard::setGuarded(&a);
    CHECK(packguard::shouldSkip(&a)); CHECK(!packguard::shouldSkip(&b)); CHECK(!packguard::shouldSkip(nullptr));
    packguard::setGuarded(nullptr); CHECK(!packguard::shouldSkip(&a));
    HookApi none; CHECK(!packguard::install(none)); CHECK(!packguard::installed());
    HookApi api; api.create = &fakeCreate; api.remove = &fakeRemove;
    CHECK(packguard::install(api)); CHECK(packguard::installed());
    CHECK_EQ(reinterpret_cast<uint32_t>(g_hookTarget), abi::UIVolumePage_Pack);
    CHECK(g_hookDetour != nullptr);
    // the detour calls the original for another volume and not for a guarded one (the original is a stub, what matters is that it does not crash)
    typedef void (__fastcall* PackFn)(void*, void*);
    packguard::setGuarded(&a);
    reinterpret_cast<PackFn>(g_hookDetour)(&a, nullptr);
    reinterpret_cast<PackFn>(g_hookDetour)(&b, nullptr);
    packguard::uninstall(api); CHECK(!packguard::installed()); CHECK_EQ(g_removed, 1); CHECK(packguard::guarded() == nullptr);
}

int main(int argc, char** argv) { return cp::test::run(argc, argv); }
