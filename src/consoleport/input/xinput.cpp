#include "xinput.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace cp { namespace input {
namespace {

// Declared by hand so as not to pull in the SDK's xinput.h: two fields out of all of it are what we need.
#pragma pack(push, 1)
struct XiGamepad { uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };
struct XiState   { uint32_t packet; XiGamepad pad; };
#pragma pack(pop)

typedef uint32_t (WINAPI* FnGetState)(uint32_t index, XiState* state);
// XInputGetCapabilitiesEx(1, index, flags, caps). The structure is
// XINPUT_CAPABILITIES (20 bytes) followed by vendorId and productId - which is
// what we read at offsets 20 and 22.
typedef uint32_t (WINAPI* FnCapsEx)(uint32_t reserved, uint32_t index, uint32_t flags, void* caps);

const uint32_t ERROR_OK = 0;

} // namespace

bool XPads::load()
{
    if (mod_) return loaded();
    static const char* kNames[] = {"XInput1_4.dll", "XInput9_1_0.dll"};
    for (int i = 0; i < 2 && !mod_; ++i) mod_ = LoadLibraryA(kNames[i]);
    if (!mod_) return false;
    HMODULE h = static_cast<HMODULE>(mod_);
    getState_ = reinterpret_cast<void*>(GetProcAddress(h, "XInputGetState"));
    // It has no name, only ordinal 108. It exists in 1_4 only; without it the
    // slot has to be found some other way, so its absence is not treated as a
    // load failure.
    capsEx_ = reinterpret_cast<void*>(GetProcAddress(h, reinterpret_cast<const char*>(108)));
    return loaded();
}

bool XPads::bind(uint16_t vid, uint16_t pid)
{
    slot_ = -1;
    if (!loaded()) return false;
    unsigned char caps[64];
    for (uint32_t i = 0; i < 4; ++i) {
        XiState st;
        if (reinterpret_cast<FnGetState>(getState_)(i, &st) != ERROR_OK) continue;  // the slot is empty
        if (!capsEx_) { slot_ = static_cast<int>(i); return true; }                 // without Ex, the first occupied one
        for (int k = 0; k < 64; ++k) caps[k] = 0;
        if (reinterpret_cast<FnCapsEx>(capsEx_)(1, i, 0, caps) != ERROR_OK) continue;
        const uint16_t v = *reinterpret_cast<const uint16_t*>(caps + 20);
        const uint16_t p = *reinterpret_cast<const uint16_t*>(caps + 22);
        if (v == vid && p == pid) { slot_ = static_cast<int>(i); return true; }
    }
    return false;
}

bool XPads::poll(uint8_t& lt, uint8_t& rt)
{
    lt = rt = 0;
    if (!loaded() || slot_ < 0) return false;
    XiState st;
    if (reinterpret_cast<FnGetState>(getState_)(static_cast<uint32_t>(slot_), &st) != ERROR_OK) {
        slot_ = -1;                     // the pad is gone - find it again
        return false;
    }
    lt = st.pad.lt; rt = st.pad.rt;
    return true;
}

}} // namespace cp::input
