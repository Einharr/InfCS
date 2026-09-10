# The test rig

The Python scripts that bring the module into the client and verify it. Two of
them are working ones - nothing starts without them; the rest are probes, kept
because each answers a question that has already cost a run once. Everything here
works with a **real** controller: the ViGEm virtual pad the rig leaned on until
9 Sep 2026 is gone from the package, because the client enumerates devices only at
initialization and an extra pad takes a slot.

## Working: nothing starts without these

| Script | What it does |
|---|---|
| `patch_joy.py` | **the saturation guard.** The client treats any `DIPROP_SATURATION` refusal as fatal, and DualSense answers in a way that kills it: `FATAL 38efea56 failed to set joystick saturation for x`. It changes `test eax,eax` into `xor eax,eax` at the eight set sites: the properties are optional and their failure must not kill the client. Started once with a timeout, and Play can be pressed whenever |
| `inject.py` | **bringing the module in.** A classic `LoadLibrary` into the running client, with modest rights requested (like `patch_joy`, which Sentinel does not trim). A watchdog too: every new `swgemu.exe` gets the DLL exactly once. In this package it takes `build/cpinject.dll` by default |

The order and the pitfalls are in [`../docs/04-run-and-build.md`](../docs/04-run-and-build.md).

## Input probes

| Script | The question it answers |
|---|---|
| `di.py` | the DirectInput8 primitives - enumeration, device state, properties. The other DI scripts import it |
| `ditest.py` | what the device reports through DirectInput8 the same way the client does: axes, buttons, the POV hat |
| `hidraw.py` | raw HID reports - by the same route `sony_hid.h` reads them. This is where the DualSense report formats (USB 0x01, BT 0x01/0x31) were read off |
| `xi.py` | XInput alongside DirectInput: are the independent triggers taken, does the DI stream suffer |
| `xiprobe.cpp`, `buildprobe.cmd`, `xiprobe.exe` | the same probe **32-bit**, as inside the client: SysWOW64 has `XInput1_4.dll` and `XInput9_1_0.dll` but no `XInput1_3.dll`. `buildprobe.cmd` builds it; the built `xiprobe.exe` is included |

## Taking the saturation crash apart

`satur.py` and `satur5.py` - the first and the last of the attempts through which
it was worked out **why** the client dies on a DualSense: at first the cooperation
modes were suspected (`EXCLUSIVE|FOREGROUND`), then the call order, and only then
did it become clear that a `DIPROP_SATURATION` refusal is treated as fatal.
`satur5.py` is the one that shows it directly. The outcome is `patch_joy.py`. The
whole breakdown is in
[`../docs/notes/tc-crash-2026-09-05.md`](../docs/notes/tc-crash-2026-09-05.md).

## In-game helpers

| Script | What it does |
|---|---|
| `keytest.py`, `keydrag.py` | keys and dragging items: sending a key into the client window, dragging an icon from A to B |
| `click.py`, `hover.py` | the mouse over the UI: a click and a hover at a given point |
| `shot.py`, `holdshot.py`, `show.py` | a screenshot of the client window, a screenshot with a button held, bringing the window to the front |
| `loginprobe.py` | an SOE SessionRequest over UDP - checking whether the server is alive, without entering the game |

**The verification metric is only the pane's digit, read by eye off a "before |
after" pair of screenshots.** A pixel sum is no good: it changes with highlights
and animations. The `shots/` directory with 710 MB of run screenshots is not part
of the package.
