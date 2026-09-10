# -*- coding: utf-8 -*-
"""Take the fatality out of the joystick's optional properties, at runtime.

Initializing the joystick, the client sets DIPROP_DEADZONE and DIPROP_SATURATION on
every axis and treats any refusal but DIERR_OBJECTNOTFOUND as fatal. DualSense answers
saturation in a way that kills it: FATAL 38efea56 "failed to set joystick saturation
for x". The properties are optional; their failure must not kill anything.

Every set site looks like this:

    call [ecx+0x18]      ; IDirectInputDevice8::SetProperty
    cmp  eax, 0x80070002 ; DIERR_OBJECTNOTFOUND - forgiven
    je   skip
    test eax, eax        ; 85 C0   <-- patched
    jge  skip
    ...FATAL

`test eax,eax` (85 C0) becomes `xor eax,eax` (31 C0): eax is zero, the jge always
taken, the fatal branch dead. The range GetProperty sites are left alone - they work on
a DualSense and muting them would be a risk for nothing.

Written into another process's memory, with no DLL and no change to the file on disk.
The exe base is fixed (ImageBase 0x400000, ASLR off), so the runtime address is the
static one. Idempotent: an already patched site is recognized and skipped.
"""
import ctypes, os, sys, time
from ctypes import wintypes

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'patch_joy.log')


def say(msg):
    """Screen and file both - without a log the reason for a miss is invisible."""
    line = time.strftime('%H:%M:%S ') + msg
    try:
        with open(LOG, 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    except OSError:
        pass
    print(line, flush=True)

# All 14 set sites: the address of the `test eax,eax` to be turned into an `xor`. On the
# z, rz and slider axes a `pop esi` wedges between the cmp and the je, so one rigid
# pattern missed them and the first attempt patched x/y/rx/ry only - after which the
# client died on "saturation for z" instead. What they all share: the test lies exactly
# 10 bytes before the push of the error string.
SITES = {
    0x0041FA19: 'dead zone x',
    0x0041FA5E: 'dead zone y',
    0x0041FAA4: 'dead zone z',
    0x0041FB29: 'saturation x',
    0x0041FB6E: 'saturation y',
    0x0041FBB4: 'saturation z',
    0x0041FCBD: 'dead zone rx',
    0x0041FD02: 'dead zone ry',
    0x0041FD48: 'dead zone rz',
    0x0041FDCD: 'saturation rx',
    0x0041FE12: 'saturation ry',
    0x0041FE58: 'saturation rz',
    0x0041FF24: 'dead zone slider',
    0x0041FFB4: 'saturation slider',
}
ORIG = b'\x85\xc0'      # test eax,eax
PATCH = b'\x31\xc0'     # xor  eax,eax

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_VM_OPERATION = 0x0008
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x0002
PAGE_EXECUTE_READWRITE = 0x40

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                ('th32ProcessID', wintypes.DWORD), ('th32DefaultHeapID', ctypes.c_void_p),
                ('th32ModuleID', wintypes.DWORD), ('cntThreads', wintypes.DWORD),
                ('th32ParentProcessID', wintypes.DWORD), ('pcPriClassBase', ctypes.c_long),
                ('dwFlags', wintypes.DWORD), ('szExeFile', ctypes.c_char * 260)]


def find_pid(name=b'swgemu.exe'):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32(); pe.dwSize = ctypes.sizeof(PROCESSENTRY32)
    pids = []
    if k32.Process32First(snap, ctypes.byref(pe)):
        while True:
            if pe.szExeFile.lower() == name:
                pids.append(pe.th32ProcessID)
            if not k32.Process32Next(snap, ctypes.byref(pe)):
                break
    k32.CloseHandle(snap)
    return pids


def read(h, va, n):
    buf = (ctypes.c_char * n)()
    got = ctypes.c_size_t(0)
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(va), buf, n, ctypes.byref(got))
    return bytes(buf) if ok else None


def write(h, va, data):
    old = wintypes.DWORD(0)
    k32.VirtualProtectEx(h, ctypes.c_void_p(va), len(data),
                         PAGE_EXECUTE_READWRITE, ctypes.byref(old))
    wrote = ctypes.c_size_t(0)
    ok = k32.WriteProcessMemory(h, ctypes.c_void_p(va), data, len(data),
                                ctypes.byref(wrote))
    restore = wintypes.DWORD(0)
    k32.VirtualProtectEx(h, ctypes.c_void_p(va), len(data), old.value,
                         ctypes.byref(restore))
    return ok and wrote.value == len(data)


IMAGE_BASE = 0x00400000        # the client has ASLR off, the base is fixed


def patch_pid(pid):
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE |
                        PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        return None, 'OpenProcess refused (err %d)' % ctypes.get_last_error()
    # A process exists before its image is mapped. Catching it milliseconds after
    # CreateProcess gave empty reads and the watchdog struck the pid off as done. So we
    # wait for the PE header.
    if read(h, IMAGE_BASE, 2) != b'MZ':
        k32.CloseHandle(h)
        return None, 'the image is not mapped yet'
    done, already, bad = 0, 0, []
    for va, label in sorted(SITES.items()):
        cur = read(h, va, 2)
        if cur == PATCH:
            already += 1
            continue
        if cur != ORIG:
            bad.append('%s @0x%08X: unexpected bytes %s' %
                       (label, va, cur.hex() if cur else 'unreadable'))
            continue
        if write(h, va, PATCH) and read(h, va, 2) == PATCH:
            done += 1
        else:
            bad.append('%s @0x%08X: the write was not confirmed' % (label, va))
    k32.CloseHandle(h)
    return (done, already, bad), None


def main():
    """A watchdog: it does not exit after the first success.

    The first version quit right after patching, so a client started later died as if
    nothing had happened. Now it watches the whole time: start it once, press Play
    whenever.
    """
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 600.0
    try:
        open(LOG, 'w', encoding='utf-8').close()
    except OSError:
        pass
    say('watchdog started for %.0f s, the log is in %s' % (timeout, os.path.basename(LOG)))
    say('you can press Play in the launcher now.')
    t0 = time.time()
    seen = set()
    patched_any = False
    complained = {}
    while time.time() - t0 < timeout:
        live = set(find_pid())
        for pid in sorted(live - seen):
            res, err = patch_pid(pid)
            if err:
                # Complain once per process but keep trying: access can appear a
                # moment after CreateProcess.
                if complained.get(pid) != err:
                    complained[pid] = err
                    say('pid %d: not yielding yet - %s' % (pid, err))
                continue
            done, already, bad = res
            ok = not bad and (done or already)
            # Only a success goes into seen - otherwise a failed pass would strike the
            # process off for good.
            if ok:
                seen.add(pid)
            patched_any = patched_any or ok
            # Once per process: the loop spins 200 times a second.
            if ok or complained.get(pid) != 'bad':
                if not ok:
                    complained[pid] = 'bad'
                say('pid %d: patched %d, already in place %d, errors %d'
                    % (pid, done, already, len(bad)))
                for line in bad[:3]:
                    say('   ! ' + line)
            if ok:
                say('   the joystick properties are no longer fatal (%d/%d sites)'
                    % (done + already, len(SITES)))
        seen &= live | seen      # keep the history but do not grow it forever
        time.sleep(0.005)
    say('the watchdog expired; successful patches: %s'
        % ('yes' if patched_any else 'NO'))
    return 0 if patched_any else 1


if __name__ == '__main__':
    sys.exit(main())
