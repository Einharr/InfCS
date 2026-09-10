// Targeting on the bumpers: the cycle table, the tap/hold state machine, the group ring.
#include "test.h"
#include "mock_node.h"
#include "../world/targeting.h"
#include "../world/targetring.h"
#include "../world/foes.h"
#include "../core/binary.h"
#include <cmath>

using namespace cp; using namespace cp::world; using namespace cp::input; using namespace cp::gen;

namespace {

// ds4: L1 is button 4, R1 5, L2 6, R2 7 (measured in game, see layout.py).
DiEvent btn(int joyb, bool down) { DiEvent e = {static_cast<uint32_t>(DI_BUTTON0 + joyb), down ? 0x80u : 0u, 100, 1, 0}; return e; }

struct Rig {
    Pad pad; Targeting t;
    Rig() { pad.setProfile("ds4"); }
    void push(int joyb, bool down) { DiEvent b[4]; uint32_t n = 0; b[n++] = btn(joyb, down); pad.feed(b, n, 4, false); }
    // A frame with no new events: time only. Returns the machine's decision.
    Decision tick(float dt = 0.016f, bool active = true) { Decision d = t.update(pad, active); pad.endFrame(dt); return d; }
    // A frame with one button event.
    Decision tick(int joyb, bool down, float dt = 0.016f, bool active = true) { push(joyb, down); return tick(dt, active); }
};

const int L1 = 4, R1 = 5, L2 = 6, R2 = 7, CIRCLE = 2;

} // namespace

TEST(targeting_cycle_table)
{
    // The client's six actions: Outward is away from the player (RB), Inward towards (LB).
    CHECK(std::string(cycleAction(SET_ALL, true)) == "cycleTargetsOutwardAll");
    CHECK(std::string(cycleAction(SET_ALL, false)) == "cycleTargetsInwardAll");
    CHECK(std::string(cycleAction(SET_FOES, true)) == "cycleTargetOutward");
    CHECK(std::string(cycleAction(SET_FOES, false)) == "cycleTargetInward");
    CHECK(std::string(cycleAction(SET_ALLIES, true)) == "cycleTargetsOutwardFriendly");
    CHECK(std::string(cycleAction(SET_ALLIES, false)) == "cycleTargetsInwardFriendly");
    CHECK_EQ(setOfMod(MOD_NONE), SET_ALL);
    CHECK_EQ(setOfMod(MOD_L2), SET_FOES);
    CHECK_EQ(setOfMod(MOD_R2), SET_ALLIES);
    CHECK_EQ(setOfMod(MOD_M3), SET_ALL);   // the client has no separate set for both triggers
}

TEST(targeting_tap_fires_on_release)
{
    Rig r;
    Decision d = r.tick(R1, true);         // pressed - we stay silent: tap or hold is not known yet
    CHECK(d.cycle == nullptr); CHECK(!d.openRing);
    d = r.tick(R1, false);                 // released quickly - now the cycle
    CHECK(d.cycle != nullptr && std::string(d.cycle) == "cycleTargetsOutwardAll");
    d = r.tick(L1, true); CHECK(d.cycle == nullptr);
    d = r.tick(L1, false);
    CHECK(d.cycle != nullptr && std::string(d.cycle) == "cycleTargetsInwardAll");
}

TEST(targeting_trigger_picks_the_set)
{
    Rig r;
    r.push(L2, true);                       // holding L2 - the "enemies" set
    r.tick();
    CHECK_EQ(r.t.set(), SET_FOES);
    r.tick(R1, true);
    Decision d = r.tick(R1, false);
    CHECK(d.cycle != nullptr && std::string(d.cycle) == "cycleTargetOutward");

    // The set is sampled on the press: release the trigger with the bumper still held
    // and the cycle still runs over the set the player chose.
    r.tick(R1, true);
    r.tick(L2, false);
    d = r.tick(R1, false);
    CHECK(d.cycle != nullptr && std::string(d.cycle) == "cycleTargetOutward");

    r.push(R2, true); r.tick();
    CHECK_EQ(r.t.set(), SET_ALLIES);
    r.tick(L1, true);
    d = r.tick(L1, false);
    CHECK(d.cycle != nullptr && std::string(d.cycle) == "cycleTargetsInwardFriendly");
}

TEST(targeting_hold_opens_ring_and_release_applies)
{
    Rig r;
    Targeting::Config c; c.hold = 0.30f; r.t.setConfig(c);

    // Pad adds up the hold in endFrame, so heldSeconds is still zero on the press frame
    // and the machine sees the threshold one frame late. At 300 ms that is noise.
    r.tick(R1, true, 0.1f);                 // held 0.0 -> 0.1
    Decision d = r.tick(0.1f);              // sees 0.1, accumulates to 0.2
    CHECK(!d.openRing);
    d = r.tick(0.1f);                       // sees 0.2, accumulates to 0.3
    CHECK(!d.openRing);
    d = r.tick(0.1f);                       // sees 0.3 - the threshold is taken
    CHECK_EQ(d.openRing, RING_FOES); CHECK(r.t.ringOpen());
    CHECK_EQ(r.t.ringButton(), B_R1);
    d = r.tick(0.1f);
    CHECK(!d.openRing);                     // we do not open it a second time
    d = r.tick(R1, false);
    CHECK(d.applyRing); CHECK(d.cycle == nullptr); CHECK(!r.t.ringOpen());

    // LB with no trigger - peaceful things around.
    r.tick(L1, true, 0.1f); r.tick(0.1f); r.tick(0.1f);
    d = r.tick(0.1f);
    CHECK_EQ(d.openRing, RING_OBJECTS);
    d = r.tick(L1, false); CHECK(d.applyRing);

    // R2 refines it to "friendlies": the same LB hold gives the group.
    r.push(R2, true); r.tick();
    r.tick(L1, true, 0.1f); r.tick(0.1f); r.tick(0.1f);
    d = r.tick(0.1f);
    CHECK_EQ(d.openRing, RING_PARTY);
    d = r.tick(L1, false); CHECK(d.applyRing);
    r.push(R2, false); r.tick();

    // L2 + LB is "everything around at once": the last free pair.
    r.push(L2, true); r.tick();
    r.tick(L1, true, 0.1f); r.tick(0.1f); r.tick(0.1f);
    d = r.tick(0.1f);
    CHECK_EQ(d.openRing, RING_ALL);
    d = r.tick(L1, false); CHECK(d.applyRing);
    r.push(L2, false); r.tick();
}

TEST(targeting_circle_cancels_ring)
{
    Rig r;
    Targeting::Config c; c.hold = 0.10f; r.t.setConfig(c);
    r.tick(R1, true, 0.2f);
    Decision d = r.tick(0.2f);
    CHECK(d.openRing);
    d = r.tick(CIRCLE, true);
    CHECK(d.cancelRing); CHECK(!r.t.ringOpen());
    // After a cancel the release must neither apply the sector nor become a cycle.
    d = r.tick(R1, false);
    CHECK(!d.applyRing); CHECK(d.cycle == nullptr);
}

TEST(targeting_sleeps_while_ui_window_is_open)
{
    Rig r;
    Targeting::Config c; c.hold = 0.10f; r.t.setConfig(c);
    r.tick(R1, true, 0.2f);
    Decision d = r.tick(0.2f);
    CHECK(d.openRing);
    d = r.tick(0.016f, false);              // a window opened - the ring goes out
    CHECK(d.cancelRing); CHECK(!r.t.ringOpen());
    // While the window is open the bumpers belong to it.
    r.push(R1, true); d = r.tick(0.016f, false);
    CHECK(d.cycle == nullptr);
    r.push(R1, false); d = r.tick(0.016f, false);
    CHECK(d.cycle == nullptr);
}

TEST(target_kind_drives_the_context)
{
    // A peaceful target puts the faces on actions; a hostile one only recolours the
    // backdrop, because in combat the faces are abilities.
    bin::game::mockReset();
    bin::game::mockSetPeacefulTarget(false);
    CHECK_EQ(bin::game::targetKind(), bin::game::TARGET_NONE);
    CHECK(!bin::game::targetIsPeaceful());
    bin::game::mockSetPeacefulTarget(true);
    CHECK_EQ(bin::game::targetKind(), bin::game::TARGET_PEACEFUL);
    CHECK(bin::game::targetIsPeaceful());
    bin::game::mockSetPeacefulTarget(false);
}

TEST(targeting_mode_hints_follow_the_set)
{
    // Two bars, either side of the modifier glyphs, so one hint each.
    std::vector<cursor::Hint> l = modeHints(SET_ALL, false, false);
    std::vector<cursor::Hint> r = modeHints(SET_ALL, false, true);
    CHECK_EQ(l.size(), 1u); CHECK_EQ(r.size(), 1u);
    CHECK_EQ(l[0].button, B_L1); CHECK(l[0].text == u"Prev ALL");
    CHECK_EQ(r[0].button, B_R1); CHECK(r[0].text == u"Next ALL");
    CHECK(modeHints(SET_FOES, false, true)[0].text == u"Next FOE");
    CHECK(modeHints(SET_ALLIES, false, false)[0].text == u"Prev ALLY");
    // While the ring is open, on the left what selects, on the right what cancels.
    l = modeHints(SET_ALL, true, false); r = modeHints(SET_ALL, true, true);
    CHECK_EQ(l[0].button, B_R3); CHECK_EQ(r[0].button, B_CIRCLE);
    CHECK(l[0].text == u"Party");
    // The target ring is captioned differently - it picks a target, not a mate.
    CHECK(modeHints(SET_ALL, true, false, true)[0].text == u"Target");
    // The legend's second line names what will actually open.
    CHECK(holdHints(SET_ALL, false)[0].text == u"Hold Objects");
    CHECK(holdHints(SET_ALLIES, false)[0].text == u"Hold Party");
    CHECK(holdHints(SET_FOES, false)[0].text == u"Hold All");
    CHECK(holdHints(SET_ALL, true)[0].text == u"Hold Targets");
}

TEST(foe_ring_takes_what_the_client_already_sees)
{
    // The mock stands in for the client's "attackables around", so the ring assembly is
    // tested with no game. Object order is findObjectsInRange's.
    void* objs[3] = {reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000), reinterpret_cast<void*>(0x3000)};
    const char16_t* names[3] = {u"Rancor", u"Tusken Raider", u"Womp Rat"};
    bin::game::mockReset();
    bin::game::mockSetFoes(objs, names, 3);

    std::vector<RingItem> items = foeItems();
    CHECK_EQ(items.size(), 3u);
    for (const RingItem& it : items) {
        CHECK(it.object != nullptr);      // a target, not an action
        CHECK(it.action == nullptr);
        CHECK(!it.label.empty());         // the caption is the name the client draws
    }

    // No unnamed items: the client writes nothing overhead for them either, and an
    // empty wedge cannot be picked meaningfully.
    const char16_t* withBlank[3] = {u"Rancor", u"", u"Womp Rat"};
    bin::game::mockSetFoes(objs, withBlank, 3);
    items = foeItems();
    CHECK_EQ(items.size(), 2u);

    // Empty when there is nobody around.
    bin::game::mockReset();
    CHECK(foeItems().empty());
}

TEST(target_ring_keeps_one_radius_for_any_count)
{
    // The radius is fixed - three items and twelve give the same ring, or it breathes
    // and the eye has nothing to hold. The price is a cap on label width so the chord
    // 2*r*sin(pi/12) fits without overlap.
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);

    void* objs[12]; const char16_t* names[12];
    for (int i = 0; i < 12; ++i) { objs[i] = reinterpret_cast<void*>(0x100 + i * 0x10); names[i] = u"Jawa Scavenger"; }

    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 3);
    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    ring.open(foeItems(), u"Targets");
    const int small = ring.radius();
    ring.close();

    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 12);
    std::vector<RingItem> items = foeItems();
    CHECK_EQ(items.size(), 12u);                 // the ceiling was raised from eight to twelve
    ring.open(items, u"Targets");
    CHECK_EQ(ring.radius(), small);              // the size does not depend on the item count
    // The chord between neighbors has to fit a whole label.
    const double chord = 2.0 * ring.radius() * std::sin(3.14159265358979 / 12.0);
    CHECK(chord >= ring.itemWidth());
    bin::game::mockReset();
}

TEST(target_ring_opens_empty_when_there_is_nobody)
{
    // Nobody around and the ring still opens: the hold has to look like it worked.
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);
    bin::game::mockReset();

    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    ring.open(foeItems(), u"Targets", u"No targets in view");
    CHECK(ring.isOpen());
    CHECK(ring.isEmpty());
    CHECK_EQ(ring.itemCount(), 0u);

    // In an empty ring the stick selects nothing and does not divide by zero.
    Pad pad; pad.setProfile("ds4");
    pad.setAxisRange(DI_Z, 0, 65535); pad.setAxisRange(DI_RZ, 0, 65535);
    DiEvent b[2]; uint32_t n = 0;
    b[n++] = DiEvent{static_cast<uint32_t>(DI_Z), 32767u, 100, 1, 0};
    b[n++] = DiEvent{static_cast<uint32_t>(DI_RZ), 0u, 100, 2, 0};
    pad.feed(b, n, 2, false);
    ring.update(pad, 0.5f, ui);
    ring.update(pad, 0.5f, ui);
    CHECK_EQ(ring.sector(), -1);
    CHECK_EQ(ring.appliedSector(), -1);
    CHECK(in.log.empty());
    CHECK(bin::game::mockLastTarget() == nullptr);

    CHECK(!ring.apply(ui));                  // it closed having selected nothing
    CHECK(!ring.isOpen());
}

TEST(target_ring_hover_needs_a_pause)
{
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);

    void* objs[4] = {reinterpret_cast<void*>(0x10), reinterpret_cast<void*>(0x20),
                     reinterpret_cast<void*>(0x30), reinterpret_cast<void*>(0x40)};
    const char16_t* names[4] = {u"A", u"B", u"C", u"D"};
    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 4);

    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    ring.open(foeItems(), u"Targets");

    Pad pad; pad.setProfile("ds4");
    pad.setAxisRange(DI_Z, 0, 65535); pad.setAxisRange(DI_RZ, 0, 65535);
    // The stick crosses the ring faster than the delay, and no intermediate sector may
    // reach the client.
    static const uint32_t path[4][2] = {{32767, 0}, {65535, 32767}, {32767, 65535}, {0, 32767}};
    for (int i = 0; i < 4; ++i) {
        DiEvent b[2]; uint32_t n = 0;
        b[n++] = DiEvent{static_cast<uint32_t>(DI_Z), path[i][0], 100, 1, 0};
        b[n++] = DiEvent{static_cast<uint32_t>(DI_RZ), path[i][1], 100, 2, 0};
        pad.feed(b, n, 2, false);
        ring.update(pad, 0.05f, ui);            // 50 ms per sector against a 120 threshold
    }
    CHECK_EQ(ring.appliedSector(), -1);
    CHECK(bin::game::mockLastTarget() == nullptr);

    // It rested - the selection went out.
    ring.update(pad, 0.2f, ui);
    CHECK(ring.appliedSector() >= 0);
    CHECK(bin::game::mockLastTarget() != nullptr);
    bin::game::mockReset();
}

TEST(target_ring_wedge_follows_the_stick)
{
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockPtr imgTpl = hud->add("imgTpl", "Image", 0, 0, 32, 32);

    mock::MockInput in; Ui ui(in);
    void* objs[4] = {reinterpret_cast<void*>(0x10), reinterpret_cast<void*>(0x20),
                     reinterpret_cast<void*>(0x30), reinterpret_cast<void*>(0x40)};
    const char16_t* names[4] = {u"A", u"B", u"C", u"D"};
    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 4);

    TargetRing ring;
    CHECK(ring.attach(hud, frameTpl, textTpl, imgTpl));
    ring.open(foeItems(), u"Targets");
    CHECK(ring.wedge() != nullptr);          // the backdrop, the rim and the wedge were built
    CHECK_EQ(ring.wedgeTurn(), 0.f);

    Pad pad; pad.setProfile("ds4");
    pad.setAxisRange(DI_Z, 0, 65535); pad.setAxisRange(DI_RZ, 0, 65535);
    DiEvent b[2]; uint32_t n = 0;
    b[n++] = DiEvent{static_cast<uint32_t>(DI_Z), 65535u, 100, 1, 0};   // the stick to the right
    b[n++] = DiEvent{static_cast<uint32_t>(DI_RZ), 32767u, 100, 2, 0};
    pad.feed(b, n, 2, false);

    ring.update(pad, 0.016f, ui);
    CHECK_EQ(ring.sector(), 1);              // of four: right is the second
    CHECK(ring.wedgeTurn() > 0.f);           // it set off but has not arrived yet
    CHECK(ring.wedgeTurn() < 0.25f);
    for (int i = 0; i < 30; ++i) ring.update(pad, 0.016f, ui);
    // Arrived: a quarter turn, the value that goes into Rotation - UICanvas::Rotate
    // counts turns, not degrees.
    CHECK(std::fabs(ring.wedgeTurn() - 0.25f) < 0.01f);
    bin::game::mockReset();
}

TEST(foe_ring_selection_sets_the_target)
{
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);

    void* objs[2] = {reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000)};
    const char16_t* names[2] = {u"Rancor", u"Womp Rat"};
    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 2);

    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    std::vector<RingItem> items = foeItems();
    ring.open(items, u"Targets");
    CHECK(ring.isOpen());
    // The label widened for the longest name.
    CHECK(ring.itemWidth() >= 68);

    Pad pad; pad.setProfile("ds4");
    pad.setAxisRange(DI_Z, 0, 65535); pad.setAxisRange(DI_RZ, 0, 65535);
    DiEvent b[2]; uint32_t n = 0;
    b[n++] = DiEvent{static_cast<uint32_t>(DI_Z), 32767u, 100, 1, 0};
    b[n++] = DiEvent{static_cast<uint32_t>(DI_RZ), 0u, 100, 2, 0};      // the stick up -> sector 0
    pad.feed(b, n, 2, false);
    ring.update(pad, 0.5f, ui);
    CHECK_EQ(ring.sector(), 0);
    CHECK(bin::game::mockLastTarget() == nullptr);   // the first frame only noticed the sector
    ring.update(pad, 0.5f, ui);                      // the delay ran out - the selection went
    // Selecting a target is setLookAtTarget, as for a mouse click - not an interface
    // action, so the input log stays empty.
    CHECK(in.log.empty());
    CHECK(bin::game::mockLastTarget() == items[0].object);
    CHECK(ring.apply(ui));
    bin::game::mockReset();
}

TEST(target_ring_builds_places_and_applies)
{
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);

    TargetRing ring;
    CHECK(ring.attach(hud, frameTpl, textTpl));
    CHECK(!ring.isOpen());

    // A group of three mates: the captions are real names, not numbers.
    const char16_t* mates[3] = {u"Kaelen", u"Riv Solari", u"Tozz"};
    bin::game::mockSetGroup(mates, 3);
    std::vector<RingItem> items = partyItems();
    CHECK_EQ(items.size(), 4u);             // ourselves plus three
    CHECK(std::string(items[0].action) == "targetSelf");
    CHECK(std::string(items[1].action) == "targetGroup0");
    CHECK(items[1].label == u"Kaelen");
    CHECK(items[3].label == u"Tozz");
    // With no group one item is left: targeting yourself still makes sense.
    bin::game::mockSetGroup(nullptr, 0);
    CHECK_EQ(partyItems().size(), 1u);
    bin::game::mockSetGroup(mates, 3);
    items = partyItems();

    ring.open(items, u"Targets");
    CHECK(ring.isOpen()); CHECK_EQ(ring.itemCount(), items.size());
    // The items spread around the ring: the first is above the center, all distinct.
    NodePtr first = ring.itemFrame(0), second = ring.itemFrame(1);
    CHECK(first && second);
    CHECK(first->location().y < second->location().y);
    CHECK(first->location().x != second->location().x || first->location().y != second->location().y);

    Pad pad; pad.setProfile("ds4");
    ring.update(pad, 0.5f, ui);                       // the stick at rest - there is no sector
    CHECK_EQ(ring.sector(), -1);
    CHECK(!ring.apply(ui));                 // released outside a sector: that is a cancel
    CHECK(!ring.isOpen());
    CHECK(in.log.empty());

    // Clones are demolished on close: the list changes, and a survivor would show
    // yesterday's group.
    CHECK_EQ(ring.itemCount(), 0u);
    CHECK(!ring.itemFrame(0));
}

TEST(target_ring_applies_selected_sector)
{
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);
    mock::MockInput in; Ui ui(in);

    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    ring.open(partyItems(), u"Party");

    // The stick up is sector 0 (Self): the same traversal as GameBar and Popup use.
    Pad pad; pad.setProfile("ds4");
    pad.setAxisRange(DI_X, 0, 65535); pad.setAxisRange(DI_Y, 0, 65535);
    pad.setTriggerAxes(DI_RX, DI_RY);
    DiEvent b[4]; uint32_t n = 0;
    b[n++] = DiEvent{static_cast<uint32_t>(DI_Z), 32767u, 100, 1, 0};      // the right stick X centered
    b[n++] = DiEvent{static_cast<uint32_t>(DI_RZ), 0u, 100, 2, 0};         // the right stick Y - up
    pad.setAxisRange(DI_Z, 0, 65535); pad.setAxisRange(DI_RZ, 0, 65535);
    pad.feed(b, n, 4, false);
    // Frame one notices the sector, frame two waits out the delay and applies: hover
    // selects, release only closes.
    ring.update(pad, 0.5f, ui);
    CHECK_EQ(ring.sector(), 0);
    CHECK_EQ(ring.appliedSector(), -1);
    CHECK(in.log.empty());
    ring.update(pad, 0.5f, ui);
    CHECK_EQ(ring.appliedSector(), 0);
    CHECK_EQ(in.log.size(), 1u);
    CHECK(in.log[0].kind == mock::Event::Action);
    CHECK(std::string(in.log[0].id) == "targetSelf");
    // While the stick stands on the same sector we do not send it a second time.
    ring.update(pad, 0.5f, ui);
    CHECK_EQ(in.log.size(), 1u);
    CHECK(ring.apply(ui));
}

TEST(targeting_both_bumpers_ask_for_nearest)
{
    // LB+RB is its own gesture, and it was free: with a ring open the second button
    // says nothing, and before the threshold both only accumulate a timer.
    Rig r; Targeting::Config c; c.hold = 0.30f; r.t.setConfig(c);

    r.push(L1, true);
    Decision d = r.tick(R1, true);
    CHECK(d.nearest);                       // we asked for the nearest
    CHECK(d.cycle == nullptr);              // and NOT for a cycle
    CHECK_EQ((int)d.openRing, (int)RING_NONE);

    d = r.tick(); CHECK(!d.nearest);        // while both are held it does not repeat
    d = r.tick(0.5f); CHECK_EQ((int)d.openRing, (int)RING_NONE);   // and no ring opens

    r.push(L1, false);
    d = r.tick(R1, false);
    CHECK(!d.nearest); CHECK(d.cycle == nullptr);   // the release does not add a cycle after the fact

    // A single button still gives a cycle on release.
    r.push(R1, true); d = r.tick();
    d = r.tick(R1, false);
    CHECK(d.cycle != nullptr); CHECK(!d.nearest);
}

TEST(targeting_nearest_follows_the_same_modifier_convention)
{
    // No new agreement: LT is enemies, RT peaceful ones, no trigger is everything.
    CHECK_EQ((int)setOfMod(input::MOD_L2), (int)SET_FOES);
    CHECK_EQ((int)setOfMod(input::MOD_R2), (int)SET_ALLIES);
    CHECK_EQ((int)setOfMod(input::MOD_NONE), (int)SET_ALL);
}

TEST(ring_colour_differs_by_context)
{
    // The rim is the only instant cue for which ring this is, so every context gets
    // its own.
    const char* all = ringColor(RING_ALL);
    const char* foes = ringColor(RING_FOES);
    const char* party = ringColor(RING_PARTY);
    const char* obj = ringColor(RING_OBJECTS);
    CHECK(std::string(foes) != std::string(all));
    CHECK(std::string(party) != std::string(all));
    CHECK(std::string(obj) != std::string(all));
    CHECK(std::string(foes) != std::string(party));
    CHECK(std::string(foes) != std::string(obj));
    CHECK(std::string(party) != std::string(obj));
    CHECK(std::string(ringColor(RING_NONE)) == std::string(all));   // the fallback is the ordinary one
}

TEST(target_ring_names_itself_in_the_middle)
{
    // The four rings differ only by content, and content overlaps - "everything around"
    // and "targets" show the same creatures - so the middle caption names the ring.
    mock::MockPtr hud = mock::MockNode::make("GroundHUD", "Page", 0, 0, 1024, 768);
    mock::MockPtr frameTpl = hud->add("frameTpl", "Page", 0, 0, 40, 20);
    mock::MockPtr textTpl = hud->add("textTpl", "Text", 0, 0, 40, 20);

    void* objs[2] = {reinterpret_cast<void*>(0x100), reinterpret_cast<void*>(0x110)};
    const char16_t* names[2] = {u"Jawa", u"Womp Rat"};
    bin::game::mockReset(); bin::game::mockSetFoes(objs, names, 2);

    TargetRing ring; ring.attach(hud, frameTpl, textTpl);
    ring.open(foeItems(), u"Targets");
    NodePtr title = hud->child("cpRingTitle");
    CHECK(title != nullptr);
    CHECK(std::static_pointer_cast<mock::MockNode>(title)->text() == u"Targets");

    // An empty ring is captioned too: what worked, and what came back empty.
    ring.close();
    bin::game::mockReset();
    ring.open(std::vector<RingItem>(), u"Party", u"No group");
    title = hud->child("cpRingTitle");
    CHECK(title != nullptr);
    CHECK(std::static_pointer_cast<mock::MockNode>(title)->text() == u"Party");

    // Closing takes the caption down as well: clones must not pile up in the overlay.
    ring.close();
    CHECK(hud->child("cpRingTitle") == nullptr);
}

TEST(ring_picks_by_bearing_and_distance_together)
{
    const float PI = 3.14159265f;
    // The case the change was made for: far ahead against near and slightly aside. By
    // bearing alone the far one won, and at 64 m a city ring filled up with distance.
    const float farAhead  = ringScore(0.0f,      60.f, 64.f);
    const float nearSide  = ringScore(PI / 6,     5.f, 64.f);   // 30 degrees to the side
    CHECK(nearSide < farAhead);

    // At equal distance the bearing decides - as it did before.
    CHECK(ringScore(0.0f, 30.f, 64.f) < ringScore(PI / 2, 30.f, 64.f));
    // At equal bearing the distance decides - which did not happen at all before.
    CHECK(ringScore(0.3f, 10.f, 64.f) < ringScore(0.3f, 50.f, 64.f));
    // The sign of the bearing does not matter: left and right are equally good.
    CHECK_EQ(ringScore(0.7f, 20.f, 64.f), ringScore(-0.7f, 20.f, 64.f));
    // All at the same distance: no measure, bearing only, and no division by zero.
    CHECK(ringScore(0.1f, 0.f, 0.f) < ringScore(1.0f, 0.f, 0.f));
    // Straight behind is the worst even point blank - but closeness still beats a
    // distant one off to the side.
    CHECK(ringScore(PI, 1.f, 64.f) > ringScore(0.f, 64.f, 64.f) - 0.001f);
}
