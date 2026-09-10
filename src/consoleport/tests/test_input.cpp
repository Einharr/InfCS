// Input tests: the pad state from the DirectInput buffer, D-pad and trigger
// synthesis, the ds4/steam profiles, UI mode, the key duplicates.
#include "test.h"
#include "../input/pad.h"

using namespace cp; using namespace cp::input; using namespace cp::gen;

static DiEvent btn(int joyb, bool down, uint32_t seq = 1) { DiEvent e = {static_cast<uint32_t>(DI_BUTTON0 + joyb), down ? 0x80u : 0u, 100, seq, 0}; return e; }
static DiEvent axis(int ofs, int32_t v, uint32_t seq = 1) { DiEvent e = {static_cast<uint32_t>(ofs), static_cast<uint32_t>(v), 100, seq, 0}; return e; }
static DiEvent pov(uint32_t v, uint32_t seq = 1) { DiEvent e = {DI_POV0, v, 100, seq, 0}; return e; }

TEST(pad_ds4_buttons_and_edges)
{
    Pad p; p.setProfile("ds4");
    DiEvent buf[16]; uint32_t n = 0;
    buf[n++] = btn(1, true);            // CROSS on ds4 is 1
    buf[n++] = btn(6, true);            // L2 is button 6
    p.feed(buf, n, 16, false);
    CHECK(p.down(B_CROSS)); CHECK(p.pressed(B_CROSS)); CHECK(p.down(B_L2));
    CHECK_EQ(p.modBits(), MODBIT_L2);
    CHECK_EQ(activeMod(p), MOD_L2);
    p.endFrame(0.016f);
    CHECK(!p.pressed(B_CROSS)); CHECK(p.down(B_CROSS));
    n = 0; buf[n++] = btn(1, false); buf[n++] = btn(7, true);
    p.feed(buf, n, 16, false);
    CHECK(p.released(B_CROSS)); CHECK_EQ(activeMod(p), MOD_M3);
    p.endFrame(0.5f); p.endFrame(0.5f);
    CHECK(p.heldSeconds(B_L2) > 0.9f); CHECK(p.heldSeconds(B_CROSS) == 0.f);
}

TEST(pad_pov_synthesizes_dpad_buttons)
{
    Pad p; p.setProfile("ds4");
    DiEvent buf[16]; uint32_t n = 0;
    buf[n++] = pov(0);                  // up
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 2u);                    // the POV plus a synthetic button
    CHECK_EQ(buf[1].ofs, static_cast<uint32_t>(DI_BUTTON0 + JOYB_DPAD_UP)); CHECK_EQ(buf[1].data, 0x80u);
    CHECK(p.down(B_DPAD_UP)); CHECK(!p.down(B_DPAD_LEFT));
    n = 0; buf[n++] = pov(31500);       // up-left: both
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 2u); CHECK(p.down(B_DPAD_UP)); CHECK(p.down(B_DPAD_LEFT));
    n = 0; buf[n++] = pov(DI_POV_CENTERED);
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 3u);                    // two releases
    CHECK(!p.down(B_DPAD_UP)); CHECK(!p.down(B_DPAD_LEFT));
    // no room in the buffer - we stay silent but keep tracking the state
    n = 0; buf[n++] = pov(9000);
    p.feed(buf, n, 1, false);
    CHECK_EQ(n, 1u); CHECK(p.down(B_DPAD_RIGHT));
}

TEST(pad_steam_triggers_from_axes)
{
    Pad p; p.setProfile("steam");
    p.setTriggerAxes(DI_RX, DI_RY);
    p.setAxisRange(DI_RX, 0, 65535); p.setAxisRange(DI_RY, 0, 65535);
    DiEvent buf[16]; uint32_t n = 0;
    buf[n++] = axis(DI_RX, 20000);      // 0.30 - below the threshold
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 1u); CHECK(!p.down(B_L2));
    n = 0; buf[n++] = axis(DI_RX, 40000);   // 0.61 - pressed
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 2u); CHECK_EQ(buf[1].ofs, static_cast<uint32_t>(DI_BUTTON0 + JOYB_TRIGGER_L2)); CHECK(p.down(B_L2));
    n = 0; buf[n++] = axis(DI_RX, 28000);   // 0.43 - hysteresis, still pressed
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 1u); CHECK(p.down(B_L2));
    n = 0; buf[n++] = axis(DI_RX, 10000);   // released
    p.feed(buf, n, 16, false);
    CHECK_EQ(n, 2u); CHECK(!p.down(B_L2)); CHECK_EQ(buf[1].data, 0u);
    // on the steam profile CROSS is button 0
    n = 0; buf[n++] = btn(0, true); p.feed(buf, n, 16, false);
    CHECK(p.down(B_CROSS)); CHECK(!p.down(B_SQUARE));
}

TEST(pad_detects_trigger_axes_from_rest_position)
{
    // A trigger rests at the EDGE of its range, a stick at the CENTRE. That alone tells
    // the two DualSense layouts apart - no table, no ini.
    {   // the ordinary layout: Rx and Ry at zero - those are the triggers
        Pad p; p.setProfile("ds4");
        p.setAxisRange(DI_RX, 0, 65535); p.setAxisRange(DI_RY, 0, 65535);
        DiEvent buf[4]; uint32_t n = 0;
        buf[n++] = axis(DI_RX, 0); p.feed(buf, n, 4, false);
        n = 0; buf[n++] = axis(DI_RY, 0); p.feed(buf, n, 4, false);
        CHECK(p.autoDetectTriggerAxes());
        CHECK(!p.triggersShareAxis());
        CHECK_EQ(p.triggerAxis(0), (int)DI_RX);
        CHECK_EQ(p.triggerAxis(1), (int)DI_RY);
    }
    {   // extended mode: the right stick is on Rx/Ry, rest at the center -> triggers on Z
        Pad p; p.setProfile("ds4");
        p.setAxisRange(DI_RX, 0, 65535); p.setAxisRange(DI_RY, 0, 65535);
        DiEvent buf[4]; uint32_t n = 0;
        buf[n++] = axis(DI_RX, 32767); p.feed(buf, n, 4, false);
        n = 0; buf[n++] = axis(DI_RY, 32767); p.feed(buf, n, 4, false);
        CHECK(p.autoDetectTriggerAxes());
        CHECK(p.triggersShareAxis());
        CHECK_EQ(p.triggerAxis(0), (int)DI_Z);
    }
    {   // a manual setting turns the auto-detection off
        Pad p; p.setProfile("ds4");
        p.setTriggerAxes(DI_Z, DI_Z);
        CHECK(!p.triggerAxesAuto());
    }
}

TEST(pad_reads_both_triggers_from_one_shared_axis)
{
    // Over Bluetooth a DualSense puts both triggers on Z: rest in the centre, L2 towards
    // the maximum, R2 towards the minimum (in game, 9 Sep 2026). Over USB each has its
    // own axis resting at zero, so the axes are set from outside and equal offsets are
    // what enable the shared mode.
    Pad p; p.setProfile("ds4");
    p.setTriggerAxes(DI_Z, DI_Z);
    p.setAxisRange(DI_Z, 0, 65535);
    CHECK(p.triggersShareAxis());

    DiEvent buf[8]; uint32_t n = 0;
    buf[n++] = axis(DI_Z, 32767);                 // rest - nobody is pressing
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 1u); CHECK(!p.down(B_L2)); CHECK(!p.down(B_R2));

    n = 0; buf[n++] = axis(DI_Z, 65000);          // L2 towards the maximum
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 2u);
    CHECK_EQ(buf[1].ofs, (uint32_t)(DI_BUTTON0 + JOYB_TRIGGER_L2));
    CHECK_EQ(buf[1].data, 0x80u);
    CHECK(p.down(B_L2)); CHECK(!p.down(B_R2));

    n = 0; buf[n++] = axis(DI_Z, 500);            // R2 towards the minimum: L2 is released
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 3u);
    CHECK(!p.down(B_L2)); CHECK(p.down(B_R2));

    n = 0; buf[n++] = axis(DI_Z, 32767);          // back to the center - both are off
    p.feed(buf, n, 8, false);
    CHECK(!p.down(B_L2)); CHECK(!p.down(B_R2));
}

TEST(pad_centered_axis_does_not_latch_a_trigger)
{
    // The defect from the live pad: the right stick sat on the axes we took for
    // triggers, and rest in the centre read as a half press - 30/31 stuck
    // (buttons=C0000000 in the trace). A centre-resting axis assigned to one trigger
    // must not stick.
    Pad p; p.setProfile("ds4");
    p.setTriggerAxes(DI_Z, DI_Z);
    p.setAxisRange(DI_Z, 0, 65535);
    DiEvent buf[4]; uint32_t n = 0;
    for (int i = 0; i < 6; ++i) { n = 0; buf[n++] = axis(DI_Z, 32767); p.feed(buf, n, 4, false); }
    CHECK(!p.down(B_L2)); CHECK(!p.down(B_R2));
}

TEST(pad_ds4_synthesizes_trigger_buttons_too)
{
    // This once checked the opposite - on ds4 a trigger arrives as button 6, so the axis
    // was not duplicated. Measured in game 8 Sep 2026: the device does report buttons 6
    // and 7, the CLIENT drops them, and 0..5 and 8..11 work. So 30/31 are needed here
    // too, or ds4 has no triggers at all.
    Pad p; p.setProfile("ds4");
    p.setTriggerAxes(DI_RX, DI_RY);
    p.setAxisRange(DI_RX, 0, 65535);
    DiEvent buf[4]; uint32_t n = 0; buf[n++] = axis(DI_RX, 60000);
    p.feed(buf, n, 4, false);
    CHECK_EQ(n, 2u);                          // the axis stayed, the button was appended
    CHECK_EQ(buf[1].ofs, (uint32_t)(DI_BUTTON0 + JOYB_TRIGGER_L2));
    CHECK_EQ(buf[1].data, 0x80u);
    CHECK(p.down(B_L2));

    // a release produces an event too, otherwise the input map layer sticks
    n = 0; buf[n++] = axis(DI_RX, 1000);
    p.feed(buf, n, 4, false);
    CHECK_EQ(n, 2u); CHECK_EQ(buf[1].data, 0x00u); CHECK(!p.down(B_L2));
}

TEST(pad_ui_mode_eats_events_but_tracks_state)
{
    Pad p; p.setProfile("ds4"); p.setUiMode(true);
    DiEvent buf[8]; uint32_t n = 0; buf[n++] = btn(3, true); buf[n++] = pov(18000);
    uint32_t eaten = p.feed(buf, n, 8, false);
    CHECK_EQ(n, 0u); CHECK_EQ(eaten, 3u);
    CHECK(p.down(B_TRIANGLE)); CHECK(p.down(B_DPAD_DOWN));
    // peek: we touch nothing
    n = 1; buf[0] = btn(3, false);
    CHECK_EQ(p.feed(buf, n, 8, true), 0u); CHECK_EQ(n, 1u); CHECK(p.down(B_TRIANGLE));
    p.setUiMode(false); n = 1; p.feed(buf, n, 8, false);
    CHECK_EQ(n, 2u); CHECK(!p.down(B_TRIANGLE));
    CHECK_EQ(buf[1].ofs, uint32_t(DI_BUTTON0+JOYB_DPAD_DOWN));
}

TEST(pad_suppressed_buttons_are_cut_from_the_buffer)
{
    // Context mode: the faces mean actions on the target, so the client must not see
    // their events or it fires the slot as well.
    Pad p; p.setProfile("ds4");
    p.suppressButtons((1u << B_SQUARE) | (1u << B_CIRCLE));
    DiEvent buf[8]; uint32_t n = 0;
    buf[n++] = btn(0, true);            // SQUARE on ds4 is button 0, intercepted
    buf[n++] = btn(3, true);            // TRIANGLE is untouched and goes to the client
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 1u);
    CHECK_EQ(buf[0].ofs, static_cast<uint32_t>(DI_BUTTON0 + 3));
    // An intercepted button is still tracked - that is how we know it was pressed.
    CHECK(p.down(B_SQUARE)); CHECK(p.pressed(B_SQUARE));
    CHECK(p.down(B_TRIANGLE));

    p.endFrame(0.016f);
    n = 0; buf[n++] = btn(0, false);
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 0u);                    // the release does not reach the client either
    CHECK(p.released(B_SQUARE));

    // Interception off - the button goes to the client again.
    p.endFrame(0.016f);
    p.suppressButtons(0);
    n = 0; buf[n++] = btn(0, true);
    p.feed(buf, n, 8, false);
    CHECK_EQ(n, 1u);
}

TEST(pad_key_duplicates)
{
    Pad p; p.setProfile("steam");
    CHECK(!p.feedKey(KEY_L2, true));          // not UI mode: we do not eat the key
    CHECK(p.down(B_L2)); CHECK_EQ(p.modBits(), MODBIT_L2);
    CHECK(!p.feedKey(30, true));              // a key that is not ours
    p.setUiMode(true);
    CHECK(p.feedKey(KEY_DPAD_LEFT, true)); CHECK(p.down(B_DPAD_LEFT));
    CHECK(p.feedKey(KEY_L2, false)); CHECK(!p.down(B_L2));
    p.forget(); CHECK(!p.down(B_DPAD_LEFT));
}

TEST(pad_sticks_deadzone)
{
    Pad p; p.setProfile("ds4");
    for (int ofs = DI_X; ofs <= DI_RZ; ofs += 4) p.setAxisRange(ofs, 0, 65535);
    DiEvent buf[4]; uint32_t n = 0; buf[n++] = axis(DI_X, 32767); buf[n++] = axis(DI_Y, 65535);
    p.feed(buf, n, 4, false);
    Stick l = p.leftStick();
    CHECK(l.x > -0.01f && l.x < 0.01f); CHECK(l.y > 0.99f);
    n = 0; buf[n++] = axis(DI_Y, 36000); p.feed(buf, n, 4, false);   // 0.10 - in the dead zone
    CHECK(p.leftStick().len() == 0.f);
}

TEST(pad_right_stick_drives_camera_buttons)
{
    // The native layout: the triggers are on Rx/Ry, which means the right stick is on Z/Rz.
    Pad p; p.setTriggerAxes(DI_RX, DI_RY);
    p.setAxisRange(DI_Z, 0, 65535); p.setAxisRange(DI_RZ, 0, 65535);
    DiEvent b[16] = {{DI_Z, 65535, 0, 0, 0}, {DI_RZ, 0, 0, 0, 0}}; uint32_t n = 2;
    p.feed(b, n, 16, false); CHECK_EQ(n, 4u);
    CHECK_EQ(b[2].ofs, 48u + 23); CHECK_EQ(b[2].data, 0x80u);   // right - yaw right
    CHECK_EQ(b[3].ofs, 48u + 24); CHECK_EQ(b[3].data, 0x80u);   // up    - pitch backward
    b[0] = {DI_Z, 32767, 0, 0, 0}; b[1] = {DI_RZ, 32767, 0, 0, 0}; n = 2;
    p.feed(b, n, 16, false); CHECK_EQ(n, 4u);                   // the center releases both
    CHECK_EQ(b[2].data, 0u); CHECK_EQ(b[3].data, 0u);
}

TEST(pad_external_triggers_separate_l2_and_r2)
{
    // The whole point: on a shared axis both triggers together read as REST (Z=32767 at
    // LT=255 RT=255), so M3 is unobtainable by any threshold. XInput reports the bytes
    // separately.
    Pad p; p.setProfile("steam");
    p.setAxisRange(DI_Z, 0, 65535);
    DiEvent b[16]; uint32_t n = 0;
    CHECK(!p.externalTriggers());

    p.setExternalTriggers(1.f, 1.f);            // both held all the way
    CHECK(p.externalTriggers());
    n = 0; p.feed(b, n, 16, false);
    CHECK_EQ(n, 2u);                            // BOTH synthetic buttons were born
    CHECK_EQ(b[0].ofs, 48u + 30); CHECK_EQ(b[0].data, 0x80u);
    CHECK_EQ(b[1].ofs, 48u + 31); CHECK_EQ(b[1].data, 0x80u);

    p.setExternalTriggers(1.f, 0.f);            // the right one was released
    n = 0; p.feed(b, n, 16, false);
    CHECK_EQ(n, 1u); CHECK_EQ(b[0].ofs, 48u + 31); CHECK_EQ(b[0].data, 0u);

    // The hysteresis is shared with the axis route: 0.55 in, 0.35 out.
    p.setExternalTriggers(0.45f, 0.f);          // below the entry one but above the exit one
    n = 0; p.feed(b, n, 16, false); CHECK_EQ(n, 0u);   // it holds
    p.setExternalTriggers(0.30f, 0.f);
    n = 0; p.feed(b, n, 16, false);
    CHECK_EQ(n, 1u); CHECK_EQ(b[0].ofs, 48u + 30); CHECK_EQ(b[0].data, 0u);

    // While the triggers are external, the glued axis has no effect on them.
    p.setExternalTriggers(1.f, 1.f); n = 0; p.feed(b, n, 16, false); CHECK_EQ(n, 2u);
    n = 0; b[n++] = DiEvent{DI_Z, 32767, 0, 1, 0};     // the "rest" of the shared axis
    p.feed(b, n, 16, false);
    CHECK_EQ(n, 1u);                            // only the axis event itself, the buttons are untouched
}

TEST(pad_synthetic_events_never_carry_zero_sequence)
{
    // The client orders its queue by dwSequence and drops zeros. The XInput triggers
    // change on frames with no events of their own, and ours went out with seq=0 -
    // which is why crossbar paging silently stopped working.
    Pad p; p.setProfile("steam");
    p.setAxisRange(DI_X, 0, 65535);
    DiEvent b[8]; uint32_t n = 0;

    // A real event sets the template.
    n = 0; b[n++] = DiEvent{DI_X, 40000, 111, 7, 0};
    p.feed(b, n, 8, false);

    // After that there are no events of its own - only the triggers.
    p.setExternalTriggers(1.f, 0.f);
    n = 0; p.feed(b, n, 8, false);
    CHECK_EQ(n, 1u); CHECK_EQ(b[0].ofs, 48u + 30);
    CHECK(b[0].seq != 0u);
    const uint32_t first = b[0].seq;

    p.setExternalTriggers(1.f, 1.f);
    n = 0; p.feed(b, n, 8, false);
    CHECK_EQ(n, 1u); CHECK_EQ(b[0].ofs, 48u + 31);
    CHECK(b[0].seq != 0u);
    CHECK(b[0].seq > first);              // the numbers grow, the queue does not get confused
}
