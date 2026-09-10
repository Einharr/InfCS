// The module's entry point for the QoL DLL of Infinity. Everything the outside
// world needs:
//
//   cp::Config cfg; cfg.profile = "ds4";            // from [consoleport] in infinity_qol.ini
//   cp::install(cfg);                                // after the UI is loaded (first frame)
//   cp::onFrame();                                   // from the runGameLoopOnce hook, every frame
//   cp::onDeviceData(dev, buf, count, capacity, peek); // from hook_DI_GetDeviceData for the joystick
//   cp::onKey(scancode, down);                       // from the same hook for the keyboard (optional)
//   cp::uninstall();                                 // on unload
//
// The module hooks nothing itself - QoL already has runGameLoopOnce and GetDeviceData,
// and its MinHook is what our own two interceptions go in through.
#pragma once
#include "hooks.h"
#include <cstdint>
#include <string>

namespace cp {

struct Config {
    std::string profile = "ds4";         // ds4 | steam
    bool crossbar = true;                // flyouts on the toolbar
    bool cursor = true;                  // UI cursor and window profiles
    // Character select: a fixed pad layout instead of the cursor, for the time before
    // the world exists - no toolbar, no window stack.
    bool charSelect = true;
    // Pages that are not windows, comma separated. The HUD furniture is known, but a
    // server build may add a permanent panel of its own: its name shows up in the
    // desktop trace and gets suppressed from here, with no rebuild.
    std::string uiSkip;
    bool bindBadges = false;             // text binding labels on the slots (the button and modifier glyphs already say everything; turn on when rebinding)
    bool triggersAsButtons = true;       // triggers -> JOYB 30/31 (steam profile)
    float triggerOn = 0.55f, triggerOff = 0.35f; bool triggerRestHigh = false;
    // DirectInput fallback override; native Sony HID has canonical axes.
    int triggerAxisL = -1, triggerAxisR = -1; // -1: automatic, no implicit override
    std::string toolbarPath = "/GroundHUD.Toolbar";
    std::string hudPath = "/GroundHUD";
    int hintX = 24, hintY = -40;         // negative means from the bottom of the HUD
    // Bumper targeting (world/targeting): LB/RB cycle, the triggers pick the set,
    // holding LB opens the group ring. 300 ms by the owner's call on 9 Sep 2026 - a
    // second felt like sticking.
    bool targeting = true;
    float targetHold = 0.30f;            // bumper hold threshold, seconds
    bool targetRingLeft = true;          // group ring on LB
    bool targetRingRight = true;         // target ring on RB (world/foes)
    bool targetModeBar = true;           // a bar saying "what the bumpers do" under the crossbar
    bool targetContext = true;           // right cross face buttons for actions on a peaceful target
    bool keyboard = true;                // on-screen keyboard on L3+R3 and on an input field
    // How long to wait for the second stick before treating the press as single and
    // firing the slot. The cost of the chord not firing an ability.
    float keyboardChord = 0.15f;
    bool debugWindow = true;             // live lines in the cpDebug window (set, target, ring, pad)
    HookApi hooks;                       // from QoL (MinHook): the UIVolumePage::Pack interception for the toolbar volume
    bool previewAtLogin = false;         // test rig: show GroundHUD+Toolbar on the login screen (without logging in)
    // Rig: open a flyout without a pad. -1 off, >=0 hold that cluster, -2 loop them.
    // Working remotely there is nothing to point with, and synthetic cursor input does
    // not reach the client's UI.
    int previewFan = -1;
    int previewFanPane = -1;             // test rig: have the flyout read this pane instead of L2/L2+R2/R2
    int previewAssign = -1;              // test rig: copy a cluster's item into the L2 layer once
    int previewClear = -1;               // test rig: clear a cluster's L2 layer once
};

struct Status {
    bool installed = false;
    bool abiOk = false;
    bool crossbarAttached = false;
    bool packHooked = false;             // the toolbar volume's Pack is intercepted (without it only the per-tick guard)
    bool cursorActive = false;
    bool charSelectActive = false;       // the character selection screen is ours right now
    std::string error;                   // why it is off
    unsigned frames = 0;
};

// Rig diagnostics: where the trace goes (QoL has a log of its own). Set before
// install; without it tracef is silent and free.
void setTrace(void (*sink)(const char* line));
void tracef(const char* fmt, ...);

bool install(const Config& cfg);         // false + Status::error if the ABI did not match
void uninstall();
void onFrame();
// the joystick's DIDEVICEOBJECTDATA buffer (20 bytes an element), after the real call
void onDeviceData(void* device, void* buf, uint32_t& count, uint32_t capacity, bool peek);
void onDeviceLost(void* device);
// An axis range from DIPROP_RANGE: without it there is no trigger travel fraction and
// no synthetic buttons 30/31. Called by the DirectInput wrapper.
void onSonyHidInput();                    // validated native HID takes ownership
// The pad came through Steam Input: the game sees a virtual Xbox controller, with its
// own numbering and merged triggers. Switches both the layout and the glyphs.
void onSteamPad();
// Triggers from XInput, 0..255 - DirectInput cannot express a simultaneous press on
// such pads at all. See input/xinput.h.
void onXInputTriggers(uint8_t l, uint8_t r);
void onXInputLost();
void onDeviceAxisRange(int ofs, int32_t lo, int32_t hi);
void detectPadAxes();                    // determine the trigger axes from their rest position
bool onKey(int scancode, bool down);     // true = swallow the key (UI mode)
const Status& status();

} // namespace cp
