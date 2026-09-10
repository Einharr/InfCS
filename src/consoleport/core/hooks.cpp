#include "hooks.h"
#include "binary.h"
#include "../abi/addresses.h"

namespace cp { namespace packguard {

namespace {
enum { GUARD_MAX = 4 };
void* g_guard[GUARD_MAX] = {nullptr, nullptr, nullptr, nullptr};
bool  g_installed = false;
typedef void (__fastcall* PackFn)(void* self, void* edx);
PackFn g_original = nullptr;

// thiscall UIVolumePage::Pack(this) under fastcall: ecx = this, edx is garbage
void __fastcall packDetour(void* self, void* edx)
{
    if (shouldSkip(self)) return;
    if (g_original) g_original(self, edx);
}
} // namespace

void  setGuarded(void* h) { for (int i=0;i<GUARD_MAX;++i) g_guard[i]=nullptr; g_guard[0]=h; }
void  guard(void* h)
{
    if (!h) return;
    for (int i=0;i<GUARD_MAX;++i) if (g_guard[i]==h) return;
    for (int i=0;i<GUARD_MAX;++i) if (!g_guard[i]) { g_guard[i]=h; return; }
}
void  unguard(void* h) { for (int i=0;i<GUARD_MAX;++i) if (g_guard[i]==h) g_guard[i]=nullptr; }
void* guarded() { return g_guard[0]; }
bool  shouldSkip(void* self)
{
    if (!self) return false;
    for (int i=0;i<GUARD_MAX;++i) if (g_guard[i]==self) return true;
    return false;
}
bool  installed() { return g_installed; }

bool install(const HookApi& api)
{
    if (g_installed) return true;
    if (!api.create) return false;
    void* orig = nullptr;
    if (!api.create(reinterpret_cast<void*>(abi::UIVolumePage_Pack), reinterpret_cast<void*>(&packDetour), &orig)) return false;
    g_original = reinterpret_cast<PackFn>(orig);
    g_installed = true;
    return true;
}

void uninstall(const HookApi& api)
{
    if (!g_installed) return;
    if (api.remove) api.remove(reinterpret_cast<void*>(abi::UIVolumePage_Pack));
    g_installed = false; g_original = nullptr; setGuarded(nullptr);
}

}} // namespace cp::packguard

namespace cp { namespace toolbarself {

namespace {
bool g_installed = false;
typedef void* (__fastcall* GetItemFn)(void* self, void* edx, int pane, int slot);
GetItemFn g_original = nullptr;

// The client calls getToolbarItem from populateSlot for every cell of every
// pane repopulation, so the pointer arrives at once and reliably.
void* __fastcall getItemDetour(void* self, void* edx, int pane, int slot)
{
    bin::toolbar::setSelf(self);
    return g_original ? g_original(self, edx, pane, slot) : nullptr;
}
} // namespace

bool installed() { return g_installed; }

bool install(const HookApi& api)
{
    if (g_installed) return true;
    if (!api.create) return false;
    void* orig = nullptr;
    if (!api.create(reinterpret_cast<void*>(abi::SwgCuiToolbar_getToolbarItem),
                    reinterpret_cast<void*>(&getItemDetour), &orig)) return false;
    g_original = reinterpret_cast<GetItemFn>(orig);
    g_installed = true;
    return true;
}

void uninstall(const HookApi& api)
{
    if (!g_installed) return;
    if (api.remove) api.remove(reinterpret_cast<void*>(abi::SwgCuiToolbar_getToolbarItem));
    g_installed = false; g_original = nullptr; bin::toolbar::setSelf(nullptr);
}

}} // namespace cp::toolbarself
