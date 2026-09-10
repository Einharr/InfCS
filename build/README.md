# The finished build

Built 10 Sep 2026, MSVC x86 (VS2022), `/O2 /MT`. The client is 32-bit, so
everything here is x86 too - 64-bit builds will not do.

| File | What it is | md5 |
|---|---|---|
| `cpinject.dll` | the **test-rig** DLL: the module plus the wrapper that brings it into a live process (the frame tick, the input hook, native HID, the crash catcher). Brought in by `harness/inject.py`. Not a form of delivery | `32339a83f1161a4bb0543e881f516c4f` |
| `consoleport.lib` | the **target** form: the module itself with no wrapper, for linking into the Infinity QoL DLL. Built by `src\consoleport\build.cmd lib` | `14bad413255b5352645035c160d0c597` |
| `cp_tests.exe` | the module's tests on a mock of the tree and of the input | `de3ebf3f3766ee4b69dd69f94378b6f0` |
| `tests-2026-09-10.txt` | the test run: **132 tests, 1989 checks, 0 failures** | - |

## Running the tests yourself

```
set CP_EXE=E:\Games\SWG\Dev\SWG Infinity\Test Center\swgemu.exe
build\cp_tests.exe
```

`CP_EXE` is required if the client sits somewhere other than the standard Test
Center path: `tests/mock_binary.cpp` reads `swgemu.exe` **from disk** and checks
all 38 addresses and their 8-byte signatures against it. That is, the tests verify
not only the logic but also that the ABI still matches the binary.

A single argument filters by name: `build\cp_tests.exe crossbar`.

## Checking that the DLL is the right one

On load `cpinject.dll` writes its base, its image size and the fact that it was
pinned into the log:

```
cpinject: attached to the live process
cpinject: base 6F...  size 0x...  pinned=1
frame hook: ok
```

Pinning (`GET_MODULE_HANDLE_EX_FLAG_PIN`) is mandatory: our patches in the
client's code outlive the call, and if the module were unloaded the jump at
0x004237C0 would go into freed memory.

## Rebuilding

```
src\consoleport\build.cmd        # the tests
src\consoleport\build.cmd lib    # consoleport.lib
src\inject\build.cmd             # lib + cpinject.dll
```

**The DLL cannot be rebuilt while the client is alive**: it is loaded and pinned,
and the linker gives LNK1104. Build it under a temporary name -
`src\inject\build.cmd cpinject_new.dll` - and swap it after leaving the game.
