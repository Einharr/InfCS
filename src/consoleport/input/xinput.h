// The triggers by a second route - XInput.
//
// On XInput pads (Steam's virtual one included) DirectInput merges both triggers onto
// one axis, and that axis cannot express a simultaneous press: at LT=255 RT=255 it
// returns exactly to rest, 32767 (probe, 10 Sep 2026, x86 as in the client). So the
// crossbar's third layer cannot come from DirectInput by any threshold.
//
// XInput reports bLeftTrigger and bRightTrigger separately, intermediate values
// included, and the DirectInput stream is unaffected: buttons, POV and sticks keep
// coming.
//
// Three things that matter in use:
//   * the slot is found by the device's VID/PID, not by "first occupied" - the VID/PID
//     comes from XInputGetCapabilitiesEx, exported by ORDINAL 108 only;
//   * empty slots cost 38 us against 2.3 for an occupied one, so the slot is found once
//     and re-checked on a timer, never scanned per frame;
//   * XInput ignores focus and reads the pad in the background, so a foreground guard is
//     mandatory - as for SonyHid.
#pragma once
#include <cstdint>

namespace cp { namespace input {

class XPads {
public:
    // XInput1_4 -> XInput9_1_0. SysWOW64 has no 1_3, and no redistributable is needed.
    bool load();
    bool loaded() const { return getState_ != nullptr; }
    bool bind(uint16_t vid, uint16_t pid);        // find this device's slot
    int  slot() const { return slot_; }
    // Trigger travel, 0..255. false means the slot is gone and must be found again.
    bool poll(uint8_t& lt, uint8_t& rt);
private:
    void* mod_ = nullptr;
    void* getState_ = nullptr;
    void* capsEx_ = nullptr;
    int   slot_ = -1;
};

}} // namespace cp::input
