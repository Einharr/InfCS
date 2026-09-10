// Pad input on top of the client's buffered DirectInput.
//
// The client reads the joystick through IDirectInputDevice8::GetDeviceData. QoL's hook
// calls Pad::feed() after the real call, and we do three things to that buffer: track
// the full pad state, append synthetic buttons (the D-pad out of the POV as JOYB 26..29,
// the triggers out of the axes as 30/31), and in UI mode cut events out so the game's
// input map never sees them.
//
// Nothing here needs dinput.h - the c_dfDIJoystick offsets are duplicated below, which
// is what makes the whole thing testable over plain event arrays.
#pragma once
#include "../gen/profiles.h"
#include <cstdint>

namespace cp { namespace input {

// DIJOFS_* for c_dfDIJoystick
enum DiOfs { DI_X = 0, DI_Y = 4, DI_Z = 8, DI_RX = 12, DI_RY = 16, DI_RZ = 20,
             DI_SLIDER0 = 24, DI_SLIDER1 = 28, DI_POV0 = 32, DI_BUTTON0 = 48 };
static const uint32_t DI_POV_CENTERED = 0xFFFFFFFFu;

// DIDEVICEOBJECTDATA (DIRECTINPUT_VERSION 0x0800, x86)
struct DiEvent { uint32_t ofs; uint32_t data; uint32_t stamp; uint32_t seq; uint32_t appData; };
static_assert(sizeof(DiEvent) == 20, "DIDEVICEOBJECTDATA");

struct AxisRange { int32_t lo = 0, hi = 0; bool known = false; };

struct PadState {
    uint32_t buttons = 0;              // one bit per JOYB 0..31 (the synthetic 26..31 included)
    int32_t  axis[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // X Y Z RX RY RZ S0 S1 - raw values
    uint32_t pov = DI_POV_CENTERED;
    bool down(int joyb) const { return joyb >= 0 && joyb < 32 && ((buttons >> joyb) & 1u) != 0; }
};

// A stick in [-1,1] with a dead zone
struct Stick { float x = 0, y = 0; float len() const; };

class Pad {
public:
    Pad();
    void setProfile(const char* name);        // "ds4" | "steam"; an unknown name -> ds4
    const gen::Profile& profile() const { return *profile_; }
    void setAxisRange(int ofs, int32_t lo, int32_t hi);   // from DIPROP_RANGE, asked for by the DI wrapper
    void setTriggerThresholds(float on, float off, bool restHigh) { trigOn_ = on; trigOff_ = off; trigRestHigh_ = restHigh; }
    // Explicit override. Equal offsets mean a merged DirectInput axis - a DirectInput
    // trait, not a Bluetooth one; native HID always has them separate.
    void setTriggerAxes(int lo, int ro);
    bool triggersShareAxis() const { return trigOfs_[0] == trigOfs_[1]; }
    // Triggers from XInput. On a merged DirectInput axis both held return exactly to
    // rest (probe, 10 Sep 2026: Z=32767 at LT=255 RT=255), so M3 cannot be had from
    // there by any threshold. XInput reports the two bytes separately, so when it is
    // available the travel comes from there and the axis is ignored.
    void setExternalTriggers(float l, float r) { extTrig_ = true; extTrav_[0] = l; extTrav_[1] = r; }
    void clearExternalTriggers() { extTrig_ = false; extTrav_[0] = extTrav_[1] = 0; }
    bool externalTriggers() const { return extTrig_; }

    // Work the trigger axes out from the rest position: a trigger rests at the EDGE of
    // its range, a stick at the CENTRE, and the device reports the difference itself.
    // Rx/Ry resting near the edge means a trigger each; otherwise they share Z.
    // Called once the ranges are known and rest has been sampled.
    bool autoDetectTriggerAxes();
    bool triggerAxesAuto() const { return autoAxes_; }
    int triggerAxis(int side) const { return trigOfs_[side ? 1 : 0]; }
    void setUiMode(bool on) { uiMode_ = on; }
    bool uiMode() const { return uiMode_; }
    // Narrower than UI mode: mutes the right stick's camera only, movement and buttons
    // still reach the client. The target ring turns its sectors with that stick and the
    // player must not freeze in place.
    void suppressCamera(bool on) { suppressCam_ = on; }
    bool cameraSuppressed() const { return suppressCam_; }
    // Cut the listed logical buttons out of the buffer entirely - the client never sees
    // them. Context mode needs it: there the faces mean actions on the target, not
    // slots. Bits indexed by gen::Button.
    void suppressButtons(uint32_t logicalMask) { suppressBtn_ = logicalMask; }
    uint32_t suppressedButtons() const { return suppressBtn_; }
    void forget();                            // device lost: everything released

    // The main entry: the buffer after the real GetDeviceData. count is in/out,
    // capacity is what the client allocated, peek means change nothing. Returns how
    // many events UI mode cut out.
    uint32_t feed(DiEvent* buf, uint32_t& count, uint32_t capacity, bool peek);

    // Key duplicates - Steam Input sends the D-pad and triggers as keys. Takes a DI
    // scancode, returns true if it is one of ours and UI mode should swallow it.
    bool feedKey(int scancode, bool down);

    // logical buttons, through the profile
    bool down(gen::Button b) const;
    bool pressed(gen::Button b) const;        // an edge since the last endFrame()
    bool released(gen::Button b) const;
    float heldSeconds(gen::Button b) const { return held_[b]; }
    unsigned modBits() const;                 // MODBIT_L1|L2|R2, like the input map's SHFT
    Stick leftStick() const;
    Stick rightStick() const;
    const PadState& state() const { return st_; }

    // end of frame: latch the previous state for the edges, add up hold time
    void endFrame(float dt);

private:
    int joyb(gen::Button b) const { return profile_->joyb[b]; }
    void setButton(int joyb, bool down) { if (joyb < 0 || joyb > 31) return; if (down) st_.buttons |= (1u << joyb); else st_.buttons &= ~(1u << joyb); }
    void emit(DiEvent* buf, uint32_t& count, uint32_t capacity, int joyb, bool down, const DiEvent& like);
    float travel(int axisIndex, int32_t raw) const;
    float deflect(int axisIndex, int32_t raw) const;   // from the center, [-1,1]
    bool seen_[8] = {};
    bool moveDown_[4] = {};
    bool camDown_[4] = {};      // camera on the right stick: 22 left, 23 right, 24 up, 25 down
    uint32_t sentSynthetic_ = 0;
    bool  atEdge(int axisIndex) const;                 // the axis rests at the edge of its range
    float axisNorm(int axisIndex) const;
    bool logicalDown(const PadState& s, gen::Button b) const;

    const gen::Profile* profile_;
    PadState st_, prev_;
    AxisRange range_[8];
    bool trigDown_[2] = {false, false};
    bool dpadDown_[4] = {false, false, false, false};   // UP DOWN LEFT RIGHT
    bool keyDup_[6] = {false, false, false, false, false, false}; // DPAD_UP DOWN LEFT RIGHT L2 R2 from the keyboard
    float held_[gen::B_COUNT] = {};
    float trigOn_ = 0.55f, trigOff_ = 0.35f; bool trigRestHigh_ = false;
    int trigOfs_[2] = {DI_RX, DI_RY};
    bool autoAxes_ = true;      // until the layout is set from outside we work it out ourselves
    bool extTrig_ = false; float extTrav_[2] = {0.f, 0.f};   // triggers from XInput
    // Template for synthetic events on calls that brought none of their own. The client
    // orders its queue by seq and drops events with a zero one, and the XInput triggers
    // change on frames with no DirectInput events at all - so we keep the last real
    // event and bump the number.
    DiEvent lastLike_ = {0, 0, 0, 0, 0}; uint32_t seqBump_ = 0;
    bool uiMode_ = false;
    bool suppressCam_ = false;
    uint32_t suppressBtn_ = 0;
};

// Whichever stick is deflected further. People turn radials with either: the right
// because it also aims the camera, the left because the hand is already there.
inline Stick anyStick(const Pad& p)
{
    Stick l = p.leftStick(), r = p.rightStick();
    return l.len() >= r.len() ? l : r;
}

// Cluster modifiers: what is held and for how long
enum Mod { MOD_NONE = 0, MOD_L2 = 1, MOD_R2 = 2, MOD_M3 = 3 };
Mod activeMod(const Pad& p);

}} // namespace cp::input
