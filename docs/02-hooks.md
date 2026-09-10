# Hooks: a full inventory

Taken on 10 Sep 2026 from the code, not from memory. There are **exactly four**
hooks in the project, and they sit in two different layers. Everything else the
project does with the client is **calls**, not interceptions.

The module (`src/consoleport/`) does not patch the client's code at all: nowhere
in that directory is there a single `VirtualProtect`, `WriteProcessMemory` or
write into a code page. It asks its host to install the interceptions through
`cp::HookApi` - two functions (`create`/`remove`) handed over by the QoL DLL with
its own MinHook.

## 1. Ours, in the shipped module - 2

Code: [`src/consoleport/core/hooks.cpp`](../src/consoleport/core/hooks.cpp); the
justification for each is in the `hooks.h` header comment.

### `UIVolumePage::Pack` - 0x0113C6C0 (thiscall), `cp::packguard`

The detour: if `this` is in the set of guarded volumes, Pack is **skipped
entirely**, otherwise it goes on to the original.

Why. The module sets the cluster diamonds' geometry itself
(`SetLocation`/`SetSize`), while `UIVolumePage::Pack` returns the cells to its own
grid by `CellSize`/`CellPadding` every frame. The `DoNotPackChildren` property
does not help here: the Infinity binary checks it only in `UIPage::Pack`
(0x010FE200) and does not look at it in `UIVolumePage::Pack` - measured in game
6 Sep 2026.

There are two guarded volumes at present, and the set has room for four:

- the toolbar volume `/GroundHUD.Toolbar.volume` - the crossbar;
- `/GroundHUD.ButtonBar.vs` (CellCount 12x1) - the game menu radial.

A side effect that matters at review time: a volume's `mWidgetCount` is
recomputed ONLY inside `Pack`. Having muted Pack, we freeze it, after which
`UIVolumePage::InsertChildAfter` refuses at `mWidgetCount >= mCellMax`, while
`SwgCuiToolbar::populateSlot` does `RemoveChild` after the refusal anyway - and
the cell is gone forever. The cure is the `CellMax = 16384` property, set BEFORE
the cells are first touched, plus our own repair with a clone of a neighboring
cell.

Turned off with `packHook=0` in `consoleport.ini` - the diamonds then go askew,
but it shows whether it is the cause of a crash.

### `SwgCuiToolbar::getToolbarItem` - 0x00F67890 (thiscall, ret 8), `cp::toolbarself`

The detour **changes nothing**: it remembers the `ecx` of the first call and goes
on to the original.

Why. The pointer to the toolbar mediator is needed in order to read and write the
pane's items. `SwgCuiToolbar::getActiveToolbar` is a trivial getter of a static,
inlined into every caller, and the static itself was not found by searching
`.data`. Hooking the getter is the only available route.

The pitfall: the client calls `getToolbarItem` only when a pane is repopulated, so
the hook has to be in place BEFORE the first redraw, otherwise there will be no
pointer until the next pane change.

## 2. Ours, in the test rig only - 2 (QoL provides these in the shipped build)

Code: [`src/inject/cpinject.cpp`](../src/inject/cpinject.cpp). This is the wrapper
for bringing the module into an ALREADY RUNNING client; in the target delivery the
module is linked into the QoL DLL, which already has both interceptions.

### `runGameLoopOnce` - 0x004237C0 (cdecl, 4 arguments)

The frame tick. A `__declspec(naked)` detour: pushad/pushfd, a call to
`cpFrameTick()`, popfd/popad, a jump into the trampoline. The trampoline is the
stolen prologue plus a jump back.

This is also where the ability to **chain onto QoL's detour** lives: QoL
already keeps a hook on this function (CrashGuard), and the prologue holds not
`55 8B EC 6A FF` but an `E9 rel32`. That instruction is position dependent and
must not be copied into the trampoline byte for byte - the jump would go into
unmapped memory and the client would die right after our first frame.
`hookCreate` recognizes the `E9`, computes QoL's detour (`t+5+rel`) and
jumps to it itself.

### `IDirectInputDevice8::GetDeviceData` - vtable slot 10

Not through `CreateDevice`: the client created its devices back at startup, and
catching the creation is too late. The trick is a different one - COM objects of
one class share ONE method table, so the rig creates a device of its own of the
same class, reads its vtable and substitutes slot 10. The client's device goes
through that same table.

The hook works both ways, and that is essential: it not only reads the buffer
after the real call but also **appends to it** -

- the D-pad out of the POV -> JOYB 26..29;
- the triggers -> JOYB 30/31 (hysteresis 0.55 / 0.35);
- the camera and movement on the sticks -> JOYB 18..25 (the client has no axial
  camera command);
- the events of the native DualSense HID, once it has taken the device over;
- in UI mode the events are **cut out** so they never reach the game's input map.

Alongside, but WITHOUT interception, `GetCapabilities` (slot 3 - to tell a joystick
from a keyboard) and `GetProperty` (slot 5 - axis ranges, the HID path, the
VID/PID) are called.

Turned off with `padHook=0`.

## 3. Not hooks, but parallel input routes

- **`SonyHid`** ([`src/inject/sony_hid.h`](../src/inject/sony_hid.h)) - reading
  DualSense HID reports directly through `CreateFileW`/`HidD_*`. It intercepts
  nothing; once the device is recognized (VID 054C, PID 0CE6/0DF2) it substitutes
  the buffer's contents inside that same `GetDeviceData` detour. It gives canonical
  axes and independent triggers.
- **XInput** ([`src/consoleport/input/xinput.cpp`](../src/consoleport/input/xinput.cpp)) -
  a dynamic load of `XInput1_4.dll` -> `XInput9_1_0.dll` from SysWOW64, with the
  slot found by VID/PID through `XInputGetCapabilitiesEx` (ordinal 108 only, the
  export has no name). It is needed because on XInput pads DirectInput glues the
  triggers onto one axis and does not express a simultaneous L2+R2 at all. The
  DirectInput stream does not suffer for it - confirmed with two rigs on 9 Sep 2026.
- **Our own `AddVectoredExceptionHandler`** - a crash catcher, installed first,
  muting nothing (`EXCEPTION_CONTINUE_SEARCH`), only writing the real EIP/ESP and a
  stack breakdown into the log. By the time it makes its dump, the client's own
  handler has lost the original context.

## 4. QoL's own hooks the module has to reckon with

Not ours, but they shape the code:

| QoL function | What it does | Consequence for us |
|---|---|---|
| `runGameLoopOnce` 0x004237C0 | CrashGuard | the prologue is an `E9 rel32`, and the rig chains onto their detour |
| `UIText::SetLocalText` 0x0110F580 | NumberCommas | the ABI signature check is **obliged to tolerate** a prologue starting with `E9`/`EB`/`FF 25` - otherwise `cp::install` fails with a signature mismatch |
| `CuiActionManager::addAction` 0x008CC650 | Toolbar3 | noted in `03-abi.md`, we do not touch it |
| - | QoL occupies the name `ui_ground_hud_toolbar.inc` | no collision: we ship no toolbar file at all - the crossbar is built inside that very page at runtime |

## Everything else is calls, not interceptions

38 functions with 8-byte prologue signatures, the vtable slots of
`UIBaseObject`/`UIWidget`/`UIPage`, field offsets, the `UIMessage::*` key statics,
1595 property-name globals. All of it is in
[`src/consoleport/abi/`](../src/consoleport/abi/), generated by
`tools/abitable.py` from the exe and checked on `cp::install`: on a mismatch the
module stays silent and writes the reason into `cp::status().error`.

The evidence for every address is in [`03-abi.md`](03-abi.md).

## What may be needed next

The plausible things, in descending order of likelihood:

1. **The keyboard device.** `cp::onKey(scancode, down)` exists in the API but is
   **called by nobody** in the rig: the detour filters for the joystick only. It
   will be wanted if the pad arrives through Steam Input with the D-pad and the
   triggers on keys, and as a safeguard for "do not swallow keys while chat has
   focus". It requires no new interception target - the same shared vtable, slot 10.
2. **An analog camera and movement.** They are currently synthesized as buttons,
   because the client has no axial commands. A real analog one would either
   synthesize mouse deltas through `UIManager::ProcessMessage` (a call) or require
   **a new hook** at the place where the camera is applied. That is the only
   plausible new interception on the horizon.
3. **DualSense output** - rumble, adaptive triggers, the light bar: writing an HID
   report into the handle we already hold. No hooks needed.
4. **A settings window and hot layout switching** - pure calls and widgets.
5. `CuiManager::requestPointer`, if a real mouse cursor in game mode is ever
   wanted (in game mode `UIManager::ProcessMessage` does not take mouse messages);
   `CuiWorkspace::getFocusMediator` never resolved - we work around it through draw
   order. Both are calls, not interceptions.

The good news about robustness: there are only four interception targets, they are
stable and every one of them is justified. The project's fragile spot is not the
hooks but the 38 addresses nailed to one client build (md5
`d84ee5565c24420858972edda9dddbf4`).
