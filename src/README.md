# The code: a map by file

Two directories: `consoleport/` is the module itself (what will go into the QoL
DLL) and `inject/` is the test-rig wrapper (not part of the shipped build).

All of it is C++17, MSVC x86, with no external dependencies: only Win32 and the
standard library. The comments explain not "what" but "why this and not
otherwise" - nearly every non-trivial piece is marked with the date of the run on
which the reason came to light.

## `consoleport/` - the module, 11,238 lines

### `abi/` - the binary's surface. Generated, must not be edited by hand

| File | Lines | What |
|---|---:|---|
| `addresses.h` | 279 | 38 functions with 8-byte prologue signatures, vtable slots, field offsets, the UIMessage layout, the key statics, the exe md5. From `tools/abitable.py` |
| `props.h` | 1660 | the 1595 UILowerString globals of the binary's property names. From `tools/gen_props.py` |
| `abi.json` | 491 | the same machine-readable, with a "why it is confirmed" field |

### `core/` - the foundation

| File | Lines | What |
|---|---:|---|
| `runtime.cpp/h` | 840 | `cp::install/onFrame/onDeviceData/onKey/uninstall`, the config, the status, the trace. **The module's only public interface** |
| `binary.cpp/h` | 891 | calls into the client by address: function wrappers, virtuals by slot, STLport strings. Also where the tolerance for QoL's detours during signature checking lives |
| `node.cpp/h` | 308 | the widget tree: walking children, properties, lookup by path. The walk is obliged to close on its sentinel - if it did not, the offset is wrong and we report empty rather than garbage |
| `hooks.cpp/h` | 143 | **both of the module's hooks**: `packguard` (Pack) and `toolbarself` (getToolbarItem). Read together with `docs/02-hooks.md` |
| `ui.cpp/h` | 172 | gestures: synthetic mouse and key messages, `warpCursor`, pressing buttons with the `Press` property |
| `theme.cpp/h` | 167 | colors from the client's active palette rather than from constants of ours |
| `types.h`, `tick.h` | 95 | the base types, the frame tick |

### `input/` - the pad

| File | Lines | What |
|---|---:|---|
| `pad.cpp/h` | 476 | the pad state from the DirectInput buffer, the ds4/steam profiles, synthesizing the D-pad out of the POV, the triggers by hysteresis, the sticks; UI mode, which swallows events |
| `xinput.cpp/h` | 110 | the second route for the triggers: DirectInput glues them onto one axis on XInput pads |
| `dualsense.cpp/h` | 53 | decoding the DualSense HID report |

### `crossbar/` - the crossbar

| File | Lines | What |
|---|---:|---|
| `crossbar.cpp/h` | 1077 | the toolbar volume's cells, the decor (button and modifier glyphs, binding labels), flyout reactivity, the per-tick geometry guard, restoring lost cells |
| `geometry.cpp/h` | 217 | the cluster geometry after ConsolePort's `Const.Cluster.Layout` at the client's scale: main 44, flyout 32, offsets 9/14 |

### `cursor/` - the UI cursor

| File | Lines | What |
|---|---:|---|
| `cursor.cpp/h` | 334 | the cursor itself: the arrow, the frame, attaching to a window, dragging |
| `nodes.cpp/h` | 116 | collecting a window's nodes. The client's `CanSelect` returns false for buttons, so for leaves we decide ourselves: drawn, enabled, takes input |
| `navigate.cpp/h` | 110 | navigation: a 45 degree cone with metric L1, then a half-plane up to 2:1 sideways, then a wrap onto the furthest node behind |
| `hints.cpp/h` | 188 | the button hint bar (ConsolePort's `HintBar`) |
| `stack.cpp/h` | 138 | the window stack and switching UI mode |

### `windows/` - the windows

| File | Lines | What |
|---|---:|---|
| `desktop.cpp/h` | 611 | **the desktop: the pad drives any client window.** A window is searched for from the root in draw order, a folder is told from a window by its own caption, the HUD furniture is filtered out. The trickiest file - read the comments |
| `keyboard.cpp/h` | 586 | the on-screen keyboard (L3+R3, or cross on an input field), with a legend of its own |
| `charselect.cpp/h` | 552 | the character selection screen: a fixed layout before entering the world, when there is no toolbar |
| `gamebar.cpp/h` | 298 | the game menu radial out of `ButtonBar.vs` - 12 wedges. The client cannot do an empty `UIRadialMenu` at all, so the ring is built with the crossbar's trick |
| `popup.cpp/h` | 235 | popup menus, the item radial, `UIComboBox` dropdowns |
| `commands.cpp/h` | 131 | assigning a command into a pane slot without a mouse: our own `CuiDragInfo` on top of a copy of a live item, plus `saveSettings` |
| `inventory.cpp/h` | 136 | the inventory profile: node collection rules, hints, buttons |

### `world/` - the world

| File | Lines | What |
|---|---:|---|
| `targetring.cpp/h` | 526 | the selection rings: targets, group, objects, everything. One radius for all of them - 205 |
| `foes.cpp/h` | 235 | the target ring on RB: collecting creatures in range, picking the attackable ones |
| `targeting.cpp/h` | 265 | the target cycle on the bumpers, the sets by trigger, the mode bar |
| `ringdecor.cpp/h` | 189 | the rings' decor: the rim, the backdrop, the captions |

### `tests/` - 3,783 lines, 132 tests, 1989 checks

A mock of the widget tree and of the input (`mock_node.h`), plus
`mock_binary.cpp`, which reads `swgemu.exe` **from disk** and checks all 38
addresses against their signatures. That is, the tests verify not only the logic
but also that the ABI still matches the binary.

## `inject/` - the test-rig wrapper, 663 lines. Not part of the shipped build

| File | What |
|---|---|
| `cpinject.cpp` | bringing the module into a live process: the rig's two hooks (`runGameLoopOnce`, `GetDeviceData`), reading `consoleport.ini`, the trace into the log, the crash catcher with stack symbolization |
| `sony_hid.h` | native DualSense HID reading: canonical axes, independent triggers |
| `diagnose_hid.cpp`, `diagnose_hid.cmd` | a standalone read-only utility for decoding a pad's HID reports, and its one-line build |
| `build.cmd` | builds `cpinject.dll` (it calls `consoleport/build.cmd lib` first) |

Why the wrapper exists at all: the client's files must not be substituted (the
launcher checks `dinput8.dll` against a manifest on every Play), yet the module
had to be tested on a live client before it ends up inside the QoL DLL. The
injection is a test rig, not a form of delivery.
