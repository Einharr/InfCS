// Test-rig injection of the ConsolePort module into a live client.
//
// Nothing is substituted: the client starts normally through the launcher (dinput8.dll
// stays QoL's) and the module goes into the already running process the way the
// saturation fix does (harness/patch_joy.py) - from outside. inject.py LoadLibrary's
// this DLL into swgemu.exe; DllMain puts a trampoline on the game loop and brings
// cp::install up on the first frame. Sentinel reports it as DLL_INJECTION, as it should
// - the developer is whitelisted by Infinity.
//
// The injection is late, so CreateDevice is long gone and the frame tick comes from a
// fixed address instead:
//   runGameLoopOnce 0x004237C0, prologue 55 8B EC 6A FF - 5 bytes, no rel, we steal 5.
// UIVolumePage::Pack 0x0113C6C0 (prologue 64 A1.., 6 bytes) goes through the same
// trampoline for the toolbar geometry (cp::install).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include "../consoleport/core/runtime.h"
#include "sony_hid.h"
#include "../consoleport/input/xinput.h"

namespace {

FILE* g_log = nullptr;
void logf(const char* fmt, ...)
{
    if (!g_log) return;
    char t[16]; time_t now = time(nullptr); strftime(t, sizeof t, "%H:%M:%S ", localtime(&now)); fputs(t, g_log);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}

// ------------------------------------------------------- the generic trampoline
// JMP rel32 to the detour; the stolen bytes plus a JMP back live on an executable page.
// Prologue lengths are known per address, no disassembler: Pack 6, runGameLoopOnce 5.
// Neither holds a rel address.
struct Patch { void* target; unsigned char saved[16]; size_t len; void* tramp; };
Patch g_patches[4] = {}; int g_npatch = 0;

size_t prologueLen(void* target)
{
    if (target == reinterpret_cast<void*>(0x0113C6C0)) return 6;  // Pack
    if (target == reinterpret_cast<void*>(0x004237C0)) return 5;  // runGameLoopOnce
    // getToolbarItem: 55 8B EC 56 57 - exactly five bytes, room for an E9.
    if (target == reinterpret_cast<void*>(0x00F67890)) return 5;
    return 0;
}

// original gets the trampoline: the stolen bytes plus a JMP to target+len.
bool hookCreate(void* target, void* detour, void** original)
{
    size_t len = prologueLen(target);
    if (!len) { logf("hook: unknown target %p", target); return false; }
    unsigned char* t = static_cast<unsigned char*>(target);
    unsigned char* tramp = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return false;
    // The target may already be hooked - QoL has its own on runGameLoopOnce. Then the
    // prologue is an E9 rel32, position dependent: copied into the trampoline byte for
    // byte the offset is measured from the new address and the jump lands nowhere (the
    // client died on an unmapped address right after our first frame). So we chain -
    // resolve QoL's detour and jump to it ourselves.
    if (t[0] == 0xE9) {
        unsigned char* chained = t + 5 + *reinterpret_cast<int32_t*>(t + 1);
        len = 5;                                   // we cover exactly QoL's JMP
        tramp[0] = 0xE9; *reinterpret_cast<int32_t*>(tramp + 1) = static_cast<int32_t>(chained - (tramp + 5));
        logf("hook: the target is already hooked, chaining onto %p", chained);
    } else {
        memcpy(tramp, t, len);
        tramp[len] = 0xE9; *reinterpret_cast<int32_t*>(tramp + len + 1) = static_cast<int32_t>((t + len) - (tramp + len + 5));
    }
    DWORD old;
    if (!VirtualProtect(t, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    Patch& p = g_patches[g_npatch++];
    memcpy(p.saved, t, len); p.len = len; p.target = target; p.tramp = tramp;
    t[0] = 0xE9; *reinterpret_cast<int32_t*>(t + 1) = static_cast<int32_t>(static_cast<unsigned char*>(detour) - (t + 5));
    for (size_t i = 5; i < len; ++i) t[i] = 0x90;
    VirtualProtect(t, len, old, &old); FlushInstructionCache(GetCurrentProcess(), t, len);
    *original = tramp;
    logf("hook: %p -> %p, trampoline %p (%zu b)", target, detour, tramp, len);
    return true;
}

bool hookRemove(void* target)
{
    for (int i = 0; i < g_npatch; ++i) {
        Patch& p = g_patches[i];
        if (p.target != target || !p.len) continue;
        DWORD old; unsigned char* t = static_cast<unsigned char*>(target);
        VirtualProtect(t, p.len, PAGE_EXECUTE_READWRITE, &old); memcpy(t, p.saved, p.len); VirtualProtect(t, p.len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), t, p.len);
        p.len = 0; return true;
    }
    return false;
}

// ------------------------------------------------------------ pad input
// The module needs the joystick events: it tracks pad state from them (the held
// modifier glyph) and appends synthetic buttons - triggers as JOYB 30/31, the D-pad out
// of the POV. Without that the triggers never reach the input map: the client drops
// JOYB 6 and 7 even though the device reports them (measured one by one, 8 Sep 2026).
//
// CreateDevice is long past, but COM objects of one class share a single method table,
// so we create a device of our own of the same class and replace slot 10
// (GetDeviceData) in that table. The client's device goes through it too.
namespace di {

typedef HRESULT (WINAPI* FnCreate)(HINSTANCE, DWORD, const IID&, void**, void*);
typedef HRESULT (__stdcall* FnGetData)(void* self, DWORD cb, void* rgdod, DWORD* pn, DWORD flags);
typedef HRESULT (__stdcall* FnGetProp)(void* self, const GUID* prop, void* hdr);
typedef HRESULT (__stdcall* FnGetCaps)(void* self, void* caps);
typedef ULONG   (__stdcall* FnRelease)(void* self);

const int SLOT_RELEASE = 2, SLOT_GETCAPS = 3, SLOT_GETPROP = 5, SLOT_GETDATA = 10;
const DWORD DIGDD_PEEK_FLAG = 0x00000001;

// {BF798030-483A-4DA2-AA99-5D64ED369700} IID_IDirectInput8A
const IID IID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const DWORD DEVCLASS_GAMECTRL = 4;
const DWORD ENUM_ATTACHEDONLY = 1;

struct DevInstanceA { DWORD dwSize; GUID guidInstance; GUID guidProduct; DWORD dwDevType; CHAR name[260]; CHAR product[260]; GUID ff; CHAR ffName[260]; };
struct DevCaps { DWORD dwSize; DWORD dwFlags; DWORD dwDevType; DWORD dwAxes; DWORD dwButtons; DWORD dwPOVs; DWORD ffSamplePeriod; DWORD ffMinTimeResolution; DWORD ffFirmwareRevision; DWORD ffHardwareRevision; DWORD ffDriverVersion; };
struct PropHeader { DWORD dwSize; DWORD dwHeaderSize; DWORD dwObj; DWORD dwHow; };
struct PropRange { PropHeader diph; LONG lMin; LONG lMax; };

void**    g_vt = nullptr;
FnGetData g_orig = nullptr;
void*     g_joy = nullptr;          // the client joystick device
bool      g_ranges = false;
SonyHid g_sony;
// Triggers by a second route: on XInput pads (Steam's virtual one included) DirectInput
// merges them onto one axis and cannot express L2+R2 at all. See input/xinput.h.
cp::input::XPads g_xpads;
bool g_xpadsTried=false;
unsigned g_xpadsRetry=0;
wchar_t g_hidPath[260]={};
bool g_sonyIdentity=false;
DWORD g_vid=0;                       // the device VIDPID, to find the XInput slot again
GUID      g_found = {};
bool      g_haveGuid = false;

BOOL CALLBACK enumCb(const DevInstanceA* inst, void*)
{
    if (inst && !g_haveGuid) { g_found = inst->guidInstance; g_haveGuid = true; }
    return FALSE;                    // the first one is enough
}

// Ask the device for its type rather than guess: a keyboard and a mouse can share the
// same method table.
bool isJoystick(void* dev)
{
    void** vt = *reinterpret_cast<void***>(dev);
    DevCaps c; memset(&c, 0, sizeof c); c.dwSize = sizeof c;
    if (FAILED(reinterpret_cast<FnGetCaps>(vt[SLOT_GETCAPS])(dev, &c))) return false;
    const DWORD t = c.dwDevType & 0xFF;
    return t >= 0x14 && t <= 0x18;   // JOYSTICK..1STPERSON
}

// Trigger axis range: without it there is no travel fraction and no buttons 30/31.
void askRanges(void* dev)
{
    if (g_ranges) return;
    g_ranges = true;
    void** vt = *reinterpret_cast<void***>(dev);
    const GUID* DIPROP_RANGE_ = reinterpret_cast<const GUID*>(4);
    const DWORD ofs[6] = {0,4,8,12,16,20};                    // DIJOFS_RX, DIJOFS_RY
    for (int i = 0; i < 6; ++i) {
        PropRange pr; memset(&pr, 0, sizeof pr);
        pr.diph.dwSize = sizeof pr; pr.diph.dwHeaderSize = sizeof pr.diph;
        // DIPH_BYOFFSET = 1. With DIPH_BYID (2) GetProperty refuses silently: no
        // range, no travel fraction, no buttons 30/31 (caught in game 8 Sep 2026).
        pr.diph.dwHow = 1 /*DIPH_BYOFFSET*/; pr.diph.dwObj = ofs[i];
        if (SUCCEEDED(reinterpret_cast<FnGetProp>(vt[SLOT_GETPROP])(dev, DIPROP_RANGE_, &pr.diph)) && pr.lMax != pr.lMin) {
            cp::onDeviceAxisRange(static_cast<int>(ofs[i]), pr.lMin, pr.lMax);
            g_sony.range(i,pr.lMin,pr.lMax);
            logf("input: axis %lu range %ld..%ld", ofs[i], pr.lMin, pr.lMax);
        } else {
            cp::onDeviceAxisRange(static_cast<int>(ofs[i]),0,65535);
            logf("input: axis %lu is absent, 0..65535 is used for HID", ofs[i]);
        }
    }
}

HRESULT __stdcall getDataDetour(void* self, DWORD cb, void* rgdod, DWORD* pn, DWORD flags)
{
    const DWORD capacity = pn ? *pn : 0;
    const HRESULT hr = g_orig(self, cb, rgdod, pn, flags);
    // rgdod == 0 means the client is just flushing the queue; cb has to be exactly
    // sizeof(DIDEVICEOBJECTDATA) on x86; anything else is a format we do not know and do not touch.
    if (!rgdod || !pn || cb != 20) return hr;
    if (flags & DIGDD_PEEK_FLAG) return hr;
    if (!g_joy) {
        if (!isJoystick(self)) return hr;
        g_joy = self;
        void** vt=*reinterpret_cast<void***>(self);
        struct PathProp { PropHeader h; GUID cls; wchar_t path[260]; } path={};
        path.h={sizeof path,sizeof(PropHeader),0,0};
        if (SUCCEEDED(reinterpret_cast<FnGetProp>(vt[SLOT_GETPROP])(self,reinterpret_cast<const GUID*>(12),&path)))
            wcsncpy(g_hidPath,path.path,259);
        struct IdProp { PropHeader h; DWORD id; } id={};
        id.h={sizeof id,sizeof(PropHeader),0,0};
        if (SUCCEEDED(reinterpret_cast<FnGetProp>(vt[SLOT_GETPROP])(self,reinterpret_cast<const GUID*>(24),&id)))
            g_sonyIdentity=LOWORD(id.id)==0x054c && (HIWORD(id.id)==0x0ce6 || HIWORD(id.id)==0x0df2);
        g_vid=id.id;
        logf("input: VIDPID=%08lX HID=%ls",id.id,g_hidPath);
        // Through Steam Input the game sees a virtual Xbox pad: different numbering,
        // triggers merged. Layout and glyphs switch at once, without waiting for the
        // ini - the pad can change between runs.
        if (LOWORD(id.id)==0x28de) { logf("input: the pad came through Steam Input"); cp::onSteamPad(); }
        // The XInput slot is found by this device's VID/PID, not by "first occupied":
        // an empty slot costs 38 us against 2.3, so they cannot be scanned per frame.
        // The library loads here, on the pad's first appearance, not lazily on the
        // first trigger - Sentinel logs every module loaded after its baseline, and a
        // later line would read as a separate event.
        if (!g_xpadsTried) {
            g_xpadsTried=true;
            if (g_xpads.load()) {
                const bool bound=g_xpads.bind(LOWORD(id.id),HIWORD(id.id));
                logf("input: XInput loaded, slot %d%s",g_xpads.slot(),bound?"":" (not found by VID/PID)");
            } else logf("input: XInput unavailable - the triggers will stay on the shared axis");
        }
        logf("input: joystick %p, events are going to the module", self);
    }
    if (self != g_joy) return hr;
    askRanges(self);
    g_sony.open(g_hidPath,g_sonyIdentity);
    if (g_sony.poll()) {
        cp::onSonyHidInput();
        logf("input: native DualSense HID is active; X/Y=left, Z/Rz=right, Rx/Ry=independent triggers");
    }
    // Trigger travel comes from XInput, which knows nothing about focus and reads the
    // pad in the background - hence the foreground guard, same as SonyHid.
    if (g_xpads.loaded() && !g_sony.owned()) {
        DWORD fg=0; GetWindowThreadProcessId(GetForegroundWindow(),&fg);
        uint8_t lt=0,rt=0;
        if (fg!=GetCurrentProcessId()) cp::onXInputLost();
        else if (g_xpads.poll(lt,rt)) cp::onXInputTriggers(lt,rt);
        else {
            cp::onXInputLost();
            // Slot lost: look again, but rarely - empty slots are expensive.
            if (++g_xpadsRetry >= 300) { g_xpadsRetry=0; g_xpads.bind(LOWORD(g_vid),HIWORD(g_vid)); }
        }
    }
    uint32_t count = SUCCEEDED(hr) ? *pn : 0;
    if (g_sony.owned()) {
        DWORD foregroundPid=0; GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        g_sony.events(static_cast<cp::input::DiEvent*>(rgdod),count,capacity,foregroundPid==GetCurrentProcessId());
    }
    else if (FAILED(hr)) { cp::onDeviceLost(self); return hr; }
    cp::onDeviceData(self, rgdod, count, capacity, (flags & DIGDD_PEEK_FLAG) != 0);
    *pn = count;
    return g_sony.owned() ? S_OK : hr;
}

bool install()
{
    HMODULE h = GetModuleHandleA("dinput8.dll");
    if (!h) { logf("input: dinput8.dll is not loaded"); return false; }
    FnCreate create = reinterpret_cast<FnCreate>(GetProcAddress(h, "DirectInput8Create"));
    if (!create) { logf("input: DirectInput8Create was not found"); return false; }
    void* di8 = nullptr;
    if (FAILED(create(GetModuleHandleA(nullptr), 0x0800, IID_DI8A, &di8, nullptr)) || !di8) {
        logf("input: DirectInput8Create refused"); return false;
    }
    void** dvt = *reinterpret_cast<void***>(di8);
    typedef HRESULT (__stdcall* FnEnum)(void*, DWORD, void*, void*, DWORD);
    typedef HRESULT (__stdcall* FnCreateDev)(void*, const GUID&, void**, void*);
    HRESULT hr = reinterpret_cast<FnEnum>(dvt[4])(di8, DEVCLASS_GAMECTRL, reinterpret_cast<void*>(&enumCb), nullptr, ENUM_ATTACHEDONLY);
    if (FAILED(hr) || !g_haveGuid) { logf("input: no game devices were found"); reinterpret_cast<FnRelease>(dvt[SLOT_RELEASE])(di8); return false; }
    void* dev = nullptr;
    hr = reinterpret_cast<FnCreateDev>(dvt[3])(di8, g_found, &dev, nullptr);
    reinterpret_cast<FnRelease>(dvt[SLOT_RELEASE])(di8);
    if (FAILED(hr) || !dev) { logf("input: CreateDevice refused"); return false; }

    g_vt = *reinterpret_cast<void***>(dev);
    g_orig = reinterpret_cast<FnGetData>(g_vt[SLOT_GETDATA]);
    DWORD old = 0;
    if (!VirtualProtect(&g_vt[SLOT_GETDATA], sizeof(void*), PAGE_READWRITE, &old)) {
        logf("input: the method table is not writable"); reinterpret_cast<FnRelease>(g_vt[SLOT_RELEASE])(dev); return false;
    }
    g_vt[SLOT_GETDATA] = reinterpret_cast<void*>(&getDataDetour);
    VirtualProtect(&g_vt[SLOT_GETDATA], sizeof(void*), old, &old);
    reinterpret_cast<FnRelease>(g_vt[SLOT_RELEASE])(dev);
    logf("input: GetDeviceData hooked, table %p, original %p", g_vt, g_orig);
    return true;
}

void uninstall()
{
    if (!g_vt || !g_orig) return;
    DWORD old = 0;
    if (VirtualProtect(&g_vt[SLOT_GETDATA], sizeof(void*), PAGE_READWRITE, &old)) {
        g_vt[SLOT_GETDATA] = reinterpret_cast<void*>(g_orig);
        VirtualProtect(&g_vt[SLOT_GETDATA], sizeof(void*), old, &old);
    }
    g_vt = nullptr; g_orig = nullptr; g_joy = nullptr;
}

} // namespace di

// ------------------------------------------------------ installing the module
bool g_installed = false; unsigned g_frames = 0, g_lastReport = 0;

// The module trace shares the log and is flushed per line, so if the client dies the
// last line names the call it died on.
void traceSink(const char* line) { logf("  . %s", line); }

void ensureInstalled()
{
    if (g_installed) return;
    g_installed = true;
    cp::setTrace(&traceSink);
    cp::Config cfg;
    cfg.profile = "ds4"; cfg.previewAtLogin = true; cfg.bindBadges = false;
    char ini[MAX_PATH]; GetModuleFileNameA(nullptr, ini, MAX_PATH); char* p = strrchr(ini, '\\'); if (p) strcpy(p + 1, "consoleport.ini");
    char v[64];
    if (GetPrivateProfileStringA("consoleport", "profile", "", v, sizeof v, ini) > 0) cfg.profile = v;
    cfg.previewAtLogin = GetPrivateProfileIntA("consoleport", "previewAtLogin", 1, ini) != 0;
    cfg.bindBadges = GetPrivateProfileIntA("consoleport", "bindBadges", 0, ini) != 0;
    char skip[512];   // a comma separated list of names - it will not fit into v at 64 bytes
    if (GetPrivateProfileStringA("consoleport", "uiSkip", "", skip, sizeof skip, ini) > 0) cfg.uiSkip = skip;
    // previewFan: -1 off, -2 loop the clusters, 0..9 hold that cluster's flyout.
    // A rig knob for checking without a mouse.
    cfg.previewFan = GetPrivateProfileIntA("consoleport", "previewFan", -1, ini);
    cfg.previewFanPane = GetPrivateProfileIntA("consoleport", "previewFanPane", -1, ini);
    cfg.previewAssign = GetPrivateProfileIntA("consoleport", "previewAssign", -1, ini);
    cfg.previewClear = GetPrivateProfileIntA("consoleport", "previewClear", -1, ini);
    // Absent keys leave auto-detection on; native HID has canonical axes anyway.
    cfg.triggerAxisL = GetPrivateProfileIntA("consoleport", "trigAxisL", -1, ini);
    cfg.triggerAxisR = GetPrivateProfileIntA("consoleport", "trigAxisR", -1, ini);
    // Rig switches: the cursor gets in the way while debugging the diamonds.
    cfg.crossbar = GetPrivateProfileIntA("consoleport", "crossbar", 1, ini) != 0;
    cfg.cursor = GetPrivateProfileIntA("consoleport", "cursor", 1, ini) != 0;
    cfg.charSelect = GetPrivateProfileIntA("consoleport", "charSelect", 1, ini) != 0;
    // Bumper targeting. targetHold is in milliseconds - GetPrivateProfileInt has no
    // fractions, and the threshold wants editing without a rebuild.
    cfg.targeting = GetPrivateProfileIntA("consoleport", "targeting", 1, ini) != 0;
    cfg.targetHold = GetPrivateProfileIntA("consoleport", "targetHoldMs", 300, ini) / 1000.f;
    cfg.targetRingLeft = GetPrivateProfileIntA("consoleport", "targetRingLeft", 1, ini) != 0;
    cfg.targetRingRight = GetPrivateProfileIntA("consoleport", "targetRingRight", 1, ini) != 0;
    cfg.targetModeBar = GetPrivateProfileIntA("consoleport", "targetModeBar", 1, ini) != 0;
    cfg.targetContext = GetPrivateProfileIntA("consoleport", "targetContext", 1, ini) != 0;
    cfg.keyboard = GetPrivateProfileIntA("consoleport", "keyboard", 1, ini) != 0;
    cfg.debugWindow = GetPrivateProfileIntA("consoleport", "debugWindow", 1, ini) != 0;
    // The Pack hook can be switched off: the volume then repacks the cells on its own
    // grid and the diamonds go askew, but it tells us whether the hook is the killer.
    if (GetPrivateProfileIntA("consoleport", "packHook", 1, ini) != 0) {
        cfg.hooks.create = &hookCreate; cfg.hooks.remove = &hookRemove;
    } else {
        logf("packHook is turned off in the ini");
    }
    bool ok = cp::install(cfg);
    logf("cp::install: %s %s", ok ? "ok" : "FAIL", cp::status().error.c_str());
    if (ok && GetPrivateProfileIntA("consoleport", "padHook", 1, ini) != 0)
        logf("input: the pad hook %s", di::install() ? "is in place" : "is NOT in place");
}

void report()
{
    const cp::Status& s = cp::status();
    logf("frames=%u installed=%d abi=%d crossbar=%d packHooked=%d cursor=%d charSel=%d err=%s",
         s.frames, s.installed, s.abiOk, s.crossbarAttached, s.packHooked, s.cursorActive,
         s.charSelectActive, s.error.c_str());
}

// ------------------------------------------------------------ the crash catcher
// The client's handler writes a dump, but by then the context is its own and the fault
// address lands outside the modules. Ours goes first and sees the real EIP/ESP and
// stack. Nothing is swallowed: the exception is passed on, we only log it.
HMODULE g_self = nullptr;
const char* whereIs(uintptr_t a)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return "not mapped";
    static char buf[MAX_PATH + 32];
    char path[MAX_PATH];
    HMODULE m = reinterpret_cast<HMODULE>(mbi.AllocationBase);
    if (GetModuleFileNameA(m, path, MAX_PATH)) {
        const char* base = strrchr(path, '\\'); base = base ? base + 1 : path;
        snprintf(buf, sizeof buf, "%s+0x%X", base, static_cast<unsigned>(a - reinterpret_cast<uintptr_t>(m)));
    } else {
        snprintf(buf, sizeof buf, "anon %p+0x%X", mbi.AllocationBase, static_cast<unsigned>(a - reinterpret_cast<uintptr_t>(mbi.AllocationBase)));
    }
    return buf;
}

LONG CALLBACK onException(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    static bool once = false; if (once) return EXCEPTION_CONTINUE_SEARCH; once = true;
    const CONTEXT* c = ep->ContextRecord;
    logf("!! ACCESS VIOLATION at %p (%s)", er->ExceptionAddress, whereIs(reinterpret_cast<uintptr_t>(er->ExceptionAddress)));
    if (er->NumberParameters >= 2)
        logf("!! a %s at address %p", er->ExceptionInformation[0] ? "write" : "read", (void*)er->ExceptionInformation[1]);
    logf("!! EIP=%08X ESP=%08X EBP=%08X ECX=%08X EDX=%08X EAX=%08X", c->Eip, c->Esp, c->Ebp, c->Ecx, c->Edx, c->Eax);
    // return addresses: whatever on the stack points into code
    uintptr_t* sp = reinterpret_cast<uintptr_t*>(c->Esp);
    int shown = 0;
    for (int i = 0; i < 256 && shown < 12; ++i) {
        if (IsBadReadPtr(sp + i, sizeof(uintptr_t))) break;
        uintptr_t v = sp[i];
        if (v < 0x10000) continue;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(v), &mbi, sizeof mbi)) continue;
        if (mbi.State != MEM_COMMIT) continue;
        const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & exec)) continue;
        logf("!!   stack[%d] %08X  %s", i, (unsigned)v, whereIs(v));
        ++shown;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Called from the trampoline every frame, already on the game thread.
void frameTick()
{
    ensureInstalled();
    cp::onFrame();
    if (++g_frames - g_lastReport >= 600) { g_lastReport = g_frames; report(); }
}

} // namespace

extern "C" void __cdecl cpFrameTick() { frameTick(); }

void* g_frameTramp = nullptr;   // the stolen runGameLoopOnce prologue plus a JMP back

// Game loop detour: save everything, run our tick, restore, jump into the trampoline
// (the original push ebp / mov ebp,esp / push -1 and a JMP to runGameLoopOnce+5). The
// jmp goes through memory so no register is spoiled after popad.
__declspec(naked) void frameDetour()
{
    __asm {
        pushad
        pushfd
        call cpFrameTick
        popfd
        popad
        jmp dword ptr [g_frameTramp]
    }
}

namespace {
bool installFrameHook()
{
    void* target = reinterpret_cast<void*>(0x004237C0);
    return hookCreate(target, reinterpret_cast<void*>(&frameDetour), &g_frameTramp);
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // the log sits next to the client exe
        char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH); char* p = strrchr(path, '\\'); if (p) strcpy(p + 1, "consoleport.log");
        g_log = fopen(path, "w"); logf("cpinject: attached to the live process");
        g_self = h;
        AddVectoredExceptionHandler(1, &onException);
        // Our patches outlive the call: unloaded, the JMP at 0x004237C0 would point at
        // freed memory. So the module is nailed down for good.
        HMODULE pinned = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCSTR>(&onException), &pinned);
        {
            const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
            const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(h) + dos->e_lfanew);
            logf("cpinject: base %p, size 0x%X, pinned=%d", h, nt->OptionalHeader.SizeOfImage, pinned ? 1 : 0);
        }
        // Nothing heavy under the loader lock: only the trampoline goes in here,
        // install happens on the first frame.
        logf("frame hook: %s", installFrameHook() ? "ok" : "FAIL");
    } else if (reason == DLL_PROCESS_DETACH) {
        // reserved != 0 means the process is dying, not a FreeLibrary. The client has
        // already demolished its windows by then, and tearing the crossbar down reaches
        // into freed widgets - on 9 Sep 2026 leaving the game died that way every time
        // (ACCESS VIOLATION in bin::removeChild under Crossbar::detach). Pinning means
        // FreeLibrary never reaches us, so this branch has nothing to clean up.
        if (reserved) { logf("cpinject: the process is closing, skipping the cleanup"); }
        else {
            report(); cp::uninstall();
            hookRemove(reinterpret_cast<void*>(0x004237C0));
            logf("cpinject: detach");
        }
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
