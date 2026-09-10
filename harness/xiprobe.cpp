// xiprobe.cpp - a 32-bit probe: DirectInput and XInput at once, as in the client.
//
// The earlier python check ran from a 64-bit process, and the client is i386, so the
// same route had to be confirmed in x86: does xinput1_4.dll load from SysWOW64
// dynamically (a static import in the QoL DLL is not what we want), does the slot read,
// and does any of it disturb an acquired DirectInput device.
//
// Building:
//   call vcvars32.bat
//   cl /nologo /EHsc /std:c++17 /O2 /MT harness\xiprobe.cpp /Fe:out\xiprobe.exe ^
//      /link /MACHINE:X86 dinput8.lib dxguid.lib user32.lib
//
// To run: xiprobe.exe [seconds]

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>

// --- XInput through LoadLibrary, with no import in the binary ---------------
struct XGamepad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState   { DWORD packet; XGamepad pad; };
struct XCaps    { BYTE type, subType; WORD flags; XGamepad pad; WORD vib[2]; };
struct XCapsEx  { XCaps caps; WORD vid, pid, rev; DWORD xinputId, reserved; };

typedef DWORD (WINAPI* FnGetState)(DWORD, XState*);
typedef DWORD (WINAPI* FnGetCaps)(DWORD, DWORD, XCaps*);
typedef DWORD (WINAPI* FnGetCapsEx)(DWORD, DWORD, DWORD, XCapsEx*);

HMODULE    g_xi = nullptr;
FnGetState g_getState = nullptr;
FnGetCaps  g_getCaps = nullptr;
FnGetCapsEx g_getCapsEx = nullptr;
const char* g_xiName = nullptr;

bool xiLoad()
{
    // Order: 1_4 (Win8+, in SysWOW64), 9_1_0 (always there since Vista), 1_3 (only with
    // the DirectX redistributable, which this machine has not got).
    static const char* names[] = {"xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll"};
    for (const char* n : names) {
        g_xi = LoadLibraryA(n);
        if (!g_xi) continue;
        g_getState = (FnGetState)GetProcAddress(g_xi, "XInputGetState");
        g_getCaps  = (FnGetCaps)GetProcAddress(g_xi, "XInputGetCapabilities");
        // XInputGetCapabilitiesEx has no name, only ordinal 108. It gives the VID/PID
        // that matches an XInput slot to a DirectInput device.
        g_getCapsEx = (FnGetCapsEx)GetProcAddress(g_xi, MAKEINTRESOURCEA(108));
        if (g_getState) { g_xiName = n; return true; }
        FreeLibrary(g_xi); g_xi = nullptr;
    }
    return false;
}

// --- DirectInput, the way the client opens it -------------------------------
LPDIRECTINPUT8A       g_di = nullptr;
LPDIRECTINPUTDEVICE8A g_dev = nullptr;
char g_devName[260] = {};
DWORD g_diVidPid = 0;

BOOL CALLBACK enumCb(LPCDIDEVICEINSTANCEA inst, void*)
{
    lstrcpynA(g_devName, inst->tszProductName, 259);
    if (FAILED(g_di->CreateDevice(inst->guidInstance, &g_dev, nullptr))) return DIENUM_CONTINUE;
    return DIENUM_STOP;
}

bool diOpen(HWND hwnd)
{
    if (FAILED(DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION,
                                  IID_IDirectInput8A, (void**)&g_di, nullptr))) return false;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, nullptr, DIEDFL_ATTACHEDONLY);
    if (!g_dev) return false;
    g_dev->SetDataFormat(&c_dfDIJoystick);
    if (hwnd) g_dev->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    DIPROPDWORD id = {};
    id.diph.dwSize = sizeof id; id.diph.dwHeaderSize = sizeof id.diph;
    id.diph.dwObj = 0; id.diph.dwHow = DIPH_DEVICE;
    if (SUCCEEDED(g_dev->GetProperty(DIPROP_VIDPID, &id.diph))) g_diVidPid = id.dwData;
    return SUCCEEDED(g_dev->Acquire()) || true;
}

int main(int argc, char** argv)
{
    const double secs = argc > 1 ? atof(argv[1]) : 12.0;
    SetConsoleOutputCP(65001);

    if (!xiLoad()) { printf("XInput: not one DLL loaded\n"); return 2; }
    printf("XInput: loaded %s dynamically, GetCapabilitiesEx(108) %s\n",
           g_xiName, g_getCapsEx ? "present" : "MISSING");

    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        XState s = {};
        if (g_getState(i, &s) != ERROR_SUCCESS) continue;
        XCaps c = {};
        const bool haveCaps = g_getCaps && g_getCaps(i, 0, &c) == ERROR_SUCCESS;
        XCapsEx cx = {};
        const bool haveEx = g_getCapsEx && g_getCapsEx(1, i, 0, &cx) == ERROR_SUCCESS;
        printf("  slot %d is occupied: subtype %u%s", i, haveCaps ? c.subType : 0u,
               haveEx ? "" : ", the VID/PID is unavailable");
        if (haveEx) printf(", VID=%04X PID=%04X", cx.vid, cx.pid);
        printf("\n");
        if (slot < 0) slot = i;
    }
    if (slot < 0) { printf("  there are no occupied XInput slots - plug a pad in\n"); return 3; }

    if (!diOpen(GetConsoleWindow())) { printf("DirectInput: the device did not open\n"); return 2; }
    printf("DirectInput: %s, VID=%04X PID=%04X\n", g_devName, LOWORD(g_diVidPid), HIWORD(g_diVidPid));
    printf("matching the slot to the device: %s\n\n",
           g_getCapsEx ? "by the VID/PID from ordinal 108" : "by the only slot (the client has MAX_JOYSTICKS=1)");

    printf("reading in parallel for %.0f s. The Z column is the glued DirectInput triggers,\n"
           "LT/RT are the independent XInput bytes. Press the triggers, BOTH at once too.\n\n", secs);

    const DWORD t0 = GetTickCount();
    DWORD lastPacket = 0xFFFFFFFF;
    LONG  lastZ = -1;
    int lines = 0;
    while (GetTickCount() - t0 < (DWORD)(secs * 1000)) {
        DIJOYSTATE js = {};
        g_dev->Acquire();
        g_dev->Poll();
        const HRESULT hr = g_dev->GetDeviceState(sizeof js, &js);
        XState xs = {};
        const DWORD xr = g_getState(slot, &xs);
        const bool changed = xs.packet != lastPacket || js.lZ != lastZ;
        if (changed && SUCCEEDED(hr) && xr == ERROR_SUCCESS) {
            lastPacket = xs.packet; lastZ = js.lZ;
            printf("  DI: Z=%6ld Rx=%6ld Ry=%6ld buttons=%08X POV=%ld || XI: LT=%3u RT=%3u btn=%04X%s\n",
                   js.lZ, js.lRx, js.lRy,
                   *(DWORD*)js.rgbButtons, (long)js.rgdwPOV[0], xs.pad.lt, xs.pad.rt, xs.pad.buttons,
                   (xs.pad.lt > 40 && xs.pad.rt > 40) ? "   <-- BOTH TRIGGERS" : "");
            ++lines;
        }
        Sleep(16);
    }
    DIJOYSTATE tail = {};
    const HRESULT after = g_dev->GetDeviceState(sizeof tail, &tail);
    printf("\nlines of change: %d; DirectInput answers after the XInput polling: %s (Z=%ld)\n",
           lines, SUCCEEDED(after) ? "yes" : "no", tail.lZ);
    g_dev->Unacquire(); g_dev->Release(); g_di->Release();
    return 0;
}
