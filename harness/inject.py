# -*- coding: utf-8 -*-
"""Bring the module into a live client: a LoadLibrary into the running process.

Nothing is substituted - the client starts normally through the launcher, dinput8.dll
stays QoL's. The module goes in from outside, the way patch_joy.py does the saturation
fix, except that instead of writing bytes this is a plain LoadLibrary of cpinject.dll.
A watchdog like patch_joy: start it once, press Play whenever, and every new swgemu.exe
gets the DLL exactly once.

Sentinel notices and reports it as DLL_INJECTION, as it should - the developer is
whitelisted by Infinity and nothing here tries to get around it.

    python inject.py [dll] [timeout_seconds]
Default dll: this package's build/cpinject.dll. Default timeout: 600 s.
"""
import ctypes, os, sys, time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
# Two places are tried, in order: this package's build/ and the working tree's out/,
# where inject\build.cmd leaves it.
_CANDIDATES = [os.path.join(HERE, '..', 'build', 'cpinject.dll'),
               os.path.join(HERE, '..', 'hook', 'inject', 'out', 'cpinject.dll')]
DEFAULT_DLL = next((os.path.normpath(p) for p in _CANDIDATES if os.path.isfile(p)),
                   os.path.normpath(_CANDIDATES[0]))
LOG = os.path.join(HERE, 'inject.log')


def say(msg):
    line = time.strftime('%H:%M:%S ') + msg
    try:
        with open(LOG, 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    except OSError:
        pass
    print(line, flush=True)


k32 = ctypes.WinDLL('kernel32', use_last_error=True)
TH32CS_SNAPPROCESS = 0x0002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
# The client is 32-bit, python is 64-bit, so LoadLibraryA from our kernel32 is useless -
# the address comes from the target's own 32-bit kernel32. Rights are kept modest, as in
# patch_joy: create thread + vm operation/write/read + query. PROCESS_ALL is the kind of
# request the anti-cheat may trim.
PROCESS_INJECT = 0x0002 | 0x0400 | 0x0008 | 0x0020 | 0x0010
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
INFINITE = 0xFFFFFFFF

k32.OpenProcess.restype = wintypes.HANDLE
k32.VirtualAllocEx.restype = wintypes.LPVOID
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.CreateRemoteThread.restype = wintypes.HANDLE
k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.LPVOID,
                                   wintypes.LPVOID, wintypes.DWORD, wintypes.LPVOID]
k32.GetProcAddress.restype = wintypes.LPVOID
k32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
k32.GetModuleHandleA.restype = wintypes.HMODULE


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                ('th32ProcessID', wintypes.DWORD), ('th32DefaultHeapID', ctypes.c_void_p),
                ('th32ModuleID', wintypes.DWORD), ('cntThreads', wintypes.DWORD),
                ('th32ParentProcessID', wintypes.DWORD), ('pcPriClassBase', ctypes.c_long),
                ('dwFlags', wintypes.DWORD), ('szExeFile', ctypes.c_char * 260)]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('th32ModuleID', wintypes.DWORD),
                ('th32ProcessID', wintypes.DWORD), ('GlblcntUsage', wintypes.DWORD),
                ('ProccntUsage', wintypes.DWORD), ('modBaseAddr', ctypes.c_void_p),
                ('modBaseSize', wintypes.DWORD), ('hModule', wintypes.HMODULE),
                ('szModule', ctypes.c_char * 256), ('szExePath', ctypes.c_char * 260)]


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


def has_module(pid, dllname):
    """Already injected? We look for the module by file name in the process module list."""
    want = os.path.basename(dllname).lower().encode()
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == wintypes.HANDLE(-1).value or snap == -1:
        return False
    me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(MODULEENTRY32)
    found = False
    if k32.Module32First(snap, ctypes.byref(me)):
        while True:
            if me.szModule.lower() == want:
                found = True; break
            if not k32.Module32Next(snap, ctypes.byref(me)):
                break
    k32.CloseHandle(snap)
    return found


def kernel32_base(pid):
    """The base of the 32-bit kernel32.dll in the target process (a KnownDLL module)."""
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap in (-1, wintypes.HANDLE(-1).value):
        return None
    me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(MODULEENTRY32)
    base = None
    if k32.Module32First(snap, ctypes.byref(me)):
        while True:
            if me.szModule.lower() == b'kernel32.dll':
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value; break
            if not k32.Module32Next(snap, ctypes.byref(me)):
                break
    k32.CloseHandle(snap)
    return base


_LL_RVA = None
def loadlib_rva():
    """The RVA of LoadLibraryA in the 32-bit kernel32 (SysWOW64) - read off disk once."""
    global _LL_RVA
    if _LL_RVA is not None:
        return _LL_RVA
    import pefile
    path = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'SysWOW64', 'kernel32.dll')
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if exp.name == b'LoadLibraryA':
            _LL_RVA = exp.address; break
    pe.close()
    return _LL_RVA


def remote_loadlib(pid):
    base = kernel32_base(pid); rva = loadlib_rva()
    return (base + rva) if (base and rva) else None


def inject(pid, dll):
    load = remote_loadlib(pid)
    if not load:
        return 'could not find the 32-bit LoadLibraryA (kernel32 is not in the module list yet)'
    h = k32.OpenProcess(PROCESS_INJECT, False, pid)
    if not h:
        return 'OpenProcess refused (err %d)' % ctypes.get_last_error()
    try:
        data = dll.encode('mbcs') + b'\x00'
        remote = k32.VirtualAllocEx(h, None, len(data), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        if not remote:
            return 'VirtualAllocEx refused (err %d)' % ctypes.get_last_error()
        wrote = ctypes.c_size_t(0)
        if not k32.WriteProcessMemory(h, remote, data, len(data), ctypes.byref(wrote)):
            return 'WriteProcessMemory refused (err %d)' % ctypes.get_last_error()
        th = k32.CreateRemoteThread(h, None, 0, ctypes.c_void_p(load), remote, 0, None)
        if not th:
            return 'CreateRemoteThread refused (err %d)' % ctypes.get_last_error()
        k32.WaitForSingleObject(th, 15000)
        code = wintypes.DWORD(0)
        k32.GetExitCodeThread(th, ctypes.byref(code))
        k32.CloseHandle(th)
        k32.VirtualFreeEx(h, remote, 0, MEM_RELEASE)
        if not code.value:
            return 'LoadLibrary in the process returned 0 (the DLL did not load - broken or missing dependencies)'
        return None
    finally:
        k32.CloseHandle(h)


def main():
    dll = DEFAULT_DLL
    timeout = 600.0
    args = sys.argv[1:]
    if args and not args[0].replace('.', '', 1).isdigit():
        dll = os.path.abspath(args.pop(0))
    if args:
        timeout = float(args[0])
    if not os.path.exists(dll):
        say('DLL not found: %s' % dll); return 2
    try:
        open(LOG, 'w', encoding='utf-8').close()
    except OSError:
        pass
    say('injection watchdog for %.0f s; DLL = %s' % (timeout, dll))
    say('you can press Play in the launcher now.')
    t0 = time.time(); done = set(); complained = {}
    while time.time() - t0 < timeout:
        for pid in sorted(find_pid()):
            if pid in done:
                continue
            if has_module(pid, dll):
                say('pid %d: the module is already loaded' % pid); done.add(pid); continue
            err = inject(pid, dll)
            if err:
                if complained.get(pid) != err:
                    complained[pid] = err
                    say('pid %d: not yielding yet - %s' % (pid, err))
                continue
            done.add(pid)
            say('pid %d: cpinject.dll injected' % pid)
        time.sleep(0.05)
    say('the watchdog expired; injected into %d process(es)' % len(done))
    return 0 if done else 1


if __name__ == '__main__':
    sys.exit(main())
