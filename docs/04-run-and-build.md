# Running, building, verifying

Current as of 10 Sep 2026. The order below is for a real controller. The ViGEm
virtual pad the rig used until 9 Sep is gone from the package: it gets in the way -
the client enumerates devices only at initialization, and an extra pad takes a slot.

## The boundaries that must not be crossed

Set by the project owner and by the Infinity server team, in force permanently:

- work on **Test Center** only (`E:\Games\SWG\Dev\SWG Infinity\Test Center`).
  Live is not to be touched at all;
- the client is to be started **only through the launcher**: the `anticheat`
  directory sits next to it and we are not going to bypass it;
- outside the project's scope: automation, timings, action queues, changing what
  the client sends the server, revealing state hidden from the player, a separate
  binary or loader. The hooks go in as a patch to the QoL sources, with review;
- no passwords are typed, a human logs into the game.

The Sentinel anti-cheat counts any module loaded after its baseline as a violation
(the baseline is taken after about 2 s, 31 modules), and it honestly writes
`DLL_INJECTION: Unknown module loaded` for the test-rig injection. That is how it
should be: the work goes on by agreement with the Infinity developers, and the
anti-cheat is neither bypassed nor needs to be.

## What has to be installed into the client

Once, with the client **closed**:

```
python client-files/deploy.py --game "E:\Games\SWG\Dev\SWG Infinity\Test Center"
```

The script puts `ui/*.inc`, `input/*.iff`, `texture/*.dds` and `consoleport.ini`
in place, making a `.bak` of the stock files exactly once. To undo, `--undo`.

What is in there and why:

| File | Why |
|---|---|
| `ui/ui_consoleport_context.inc` | hidden templates: a glyph on our atlas, a solid block, texts. **Cloning at runtime has to come from the markup**: `SourceResource` on a `UIImage` is applied only by the markup loader, never to a clone at runtime |
| `ui/ui_consoleport_charselect.inc` | the same for the character selection screen (`GroundHUD` is not built there yet) |
| `ui/ui_ground_hud.inc`, `ui_avatar_selection.inc`, `ui_pda_inventory.inc`, `ui_pda_char_sheet.inc` | stock plus one `<include>` line - the attachment points |
| `input/*.iff` | input maps for the hook slot model: main k, L2 -> 8+k, R2 -> 16+k, the D-pad as JOYB 26..29, the triggers as 30/31 |
| `texture/consoleport_*.dds` | the glyph atlas and the ring textures. **The atlas is strictly 256x256**: 256x320 kills the client while loading, 512x512 loads silently empty |
| `consoleport.ini` | every key with comments, read from the exe's directory |

**Note at review time:** the markup in this package (10 Sep, with the `theme` line
in the debug window) is NEWER than what was installed in the client when the
package was assembled - a 9 Sep 17:20 copy is there. `deploy.py` has to be run
before a run, or part of the debug information will not show.

## The order of starting

```
1. python harness/patch_joy.py 2400      # the guard: removes the FATAL on saturation
2. launcher -> Play                      # the client enters the world by itself
3. python harness/inject.py              # brings build/cpinject.dll into the live process
```

To stop: close the client **through WM_CLOSE** (not `Stop-Process`), then kill the
guards.

About step 1: when initializing the joystick the client sets `DIPROP_DEADZONE` and
`DIPROP_SATURATION` on every axis and treats any refusal except
`DIERR_OBJECTNOTFOUND` as fatal. DualSense answers `DIPROP_SATURATION` in a way
that kills the client: `FATAL 38efea56 failed to set joystick saturation for x`.
The guard changes `test eax,eax` into `xor eax,eax` at the eight set sites - the
properties are optional and their failure must not kill the client. Details are in
the header comment of `harness/patch_joy.py`.

`inject.py` is a watchdog rather than a one-shot: it is started once, Play can be
pressed whenever, and every new `swgemu.exe` gets the DLL exactly once. By default
it takes `build/cpinject.dll` from this package, resolved relative to itself.

## Where to look

- **`<client directory>\consoleport.log`** - the rig's log and the module's trace.
  The status line is printed every 600 frames:
  `frames= installed=1 abi=1 crossbar=1 packHooked=1 cursor= charSel= err=`.
- The lines `hook: <target> -> <detour>, trampoline <addr>` show that the
  interceptions went in; `hook: the target is already hooked, chaining onto ...`
  shows that QoL had been there already.
- The line `desktop: window NAME` shows what the module counts as a window and
  what as furniture. If the pad went into UI mode over nothing, the culprit is
  visible here and is muted with the `uiSkip` key.
- The live `cpDebug` debug window in the HUD (key `debugWindow=1`): the set, the
  target, the ring, the pad, the theme.
- On a crash our own catcher writes `!! ACCESS VIOLATION`, the registers and a
  breakdown of the return addresses off the stack, symbolized by module.

## Building

Needs MSVC x86 (the client is i386): VS2022 Community, `vcvars32.bat`.

```
src\consoleport\build.cmd            # build and run the tests -> out\cp_tests.exe
src\consoleport\build.cmd lib        # out\consoleport.lib for linking into the QoL DLL
src\consoleport\build.cmd test name  # only the tests whose name contains "name"
src\inject\build.cmd                 # lib + out\cpinject.dll (the rig wrapper)
```

The tests are built with `CP_MOCK_BINARY`: a mock of the tree and of the input,
while `mock_binary.cpp` reads `swgemu.exe` **from disk** and checks the ABI against
it. The path is given by the `CP_EXE` variable, otherwise the standard Test Center
path is used.

This package's own run is in
[`build/tests-2026-09-10.txt`](../build/tests-2026-09-10.txt):
**132 tests, 1989 checks, 0 failures**.

Two build pitfalls:

- **The DLL cannot be rebuilt while the client is alive**: it is loaded and pinned
  (`GET_MODULE_HANDLE_EX_FLAG_PIN`), and the linker gives LNK1104. Build it under a
  temporary name (`build.cmd cpinject_new.dll`) and swap it after leaving the game.
- An address from the log is symbolized by relinking with `/MAP`; the anchor for
  comparison is `frameDetour`, which is also the trampoline's target in the line
  `hook: 004237C0 -> ...`.

## Regenerating the artifacts

| What | With what |
|---|---|
| `src/consoleport/abi/addresses.h` + `docs/03-abi.md` | `python tools/abitable.py` against the exe |
| `src/consoleport/abi/props.h` (1595 property names) | `python tools/gen_props.py` from `tools/ui_props.txt` |
| `src/consoleport/gen/profiles.h` (pad profiles, slots, colors) | `python tools/gen_profiles.py` from `tools/layout.py` |
| `client-files/input/*.iff` | `python tools/build.py` |
| `client-files/ui/*.inc` | `python tools/diamond.py` |
| `client-files/texture/*.dds` | `python tools/glyphs.py`, `tools/ringtex.py` (the source is `assets/kenney_input-prompts_1.5.zip`, CC0) |
| the disassembler for new addresses | `python tools/abigen.py` (RTTI -> vtable, strings -> xref, capstone) |

**When the client build changes** `cp::install` will refuse to work: the exe md5
and the 8-byte signatures of all 38 functions are baked into `addresses.h`. The
order is: regenerate with `abitable.py`, run the tests (they check against the
exe), verify in game.

## The rig's pitfalls, each of which cost a run

- do not restart the pad daemon while the client is alive;
- do not kill the client with `Stop-Process` - WM_CLOSE only;
- the launcher hides in the tray: show it with `SW_SHOW`, not `SW_RESTORE`;
- return focus with a click on the window's title bar;
- Steam grabs the PS button and substitutes the pad profile: without Steam, L2/R2
  arrive as buttons 6/7, with it, an Xbox profile with the triggers on one axis.
  Hence the two presets;
- **the client dies with FATAL DirectInput 340 if the system has no active mouse** -
  it has nothing to do with the module, but it kills a run;
- the verification metric is only the pane's digit, read by eye off a "before |
  after" contact sheet. A pixel sum is no good.
