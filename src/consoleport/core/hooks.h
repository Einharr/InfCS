// Our own two hooks, installed through the host API - QoL's MinHook, passed in as two
// functions in cp::Config::hooks. We patch nothing ourselves.
//   UIVolumePage::Pack - keeps the toolbar volume from laying its cells back onto a grid
//     over our flyout geometry (the binary ignores DoNotPackChildren, 6 Sep 2026).
//   SwgCuiToolbar::getToolbarItem - substitutes nothing, it is there for ecx: the only
//     way to learn the toolbar mediator's pointer. Without it items in other panes can
//     be neither read nor written, and s_activeToolbar is nowhere in the binary
//     (getActiveToolbar is a trivial getter, inlined into every caller).
#pragma once
#include <cstdint>

namespace cp {

struct HookApi {
    // MinHook-shaped: create and enable target -> detour, return the trampoline
    bool (*create)(void* target, void* detour, void** original) = nullptr;
    bool (*remove)(void* target) = nullptr;
};

// Pack guard: a guarded volume skips Pack entirely.
namespace packguard {
    // Two volumes are guarded: the toolbar (crossbar) and ButtonBar.vs (the game menu
    // ring). We lay both out ourselves, and Pack would regrid both every frame.
    void  setGuarded(void* volumeHandle);     // replace the set with one; 0 = unguard all
    void  guard(void* volumeHandle);          // add a volume to the guard
    void  unguard(void* volumeHandle);        // remove one
    void* guarded();                          // first guarded one (for the trace)
    bool  shouldSkip(void* self);             // detour logic, tested on the mock
    // install/remove through HookApi; true if the detour is in place
    bool  install(const HookApi& api);
    void  uninstall(const HookApi& api);
    bool  installed();
}

// Catcher for the toolbar mediator pointer: the detour changes nothing, it remembers
// the first call's this and goes straight on to the original.
namespace toolbarself {
    bool  install(const HookApi& api);
    void  uninstall(const HookApi& api);
    bool  installed();
}

} // namespace cp
