# -*- coding: utf-8 -*-
"""The device's raw HID reports - to see the MODE rather than guess at it.

Over Bluetooth a DualSense can do two input modes:
    report 0x01 - the simple, truncated one. It is the only one the stock Windows HID
                  driver expands into a game controller, and the only one DirectInput sees.
    report 0x31 - the extended one, with the gyro and the touchpad. Windows does NOT
                  expand it into a controller: the device is in the list, the events are not.

Steam (through SDL HIDAPI) puts the pad into the extended mode and DirectInput goes blind.
Here we read the stream directly and print the report id: that is the answer.

    python hidraw.py [seconds]
"""
import ctypes, sys, time
from ctypes import wintypes

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
setup = ctypes.WinDLL('setupapi', use_last_error=True)
hid = ctypes.WinDLL('hid', use_last_error=True)

GENERIC_READ = 0x80000000
FILE_SHARE_READ, FILE_SHARE_WRITE = 1, 2
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
INVALID_HANDLE = wintypes.HANDLE(-1).value
DIGCF_PRESENT, DIGCF_DEVICEINTERFACE = 0x02, 0x10
WAIT_TIMEOUT = 258


class GUID(ctypes.Structure):
    _fields_ = [('d1', wintypes.DWORD), ('d2', wintypes.WORD), ('d3', wintypes.WORD),
                ('d4', ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('InterfaceClassGuid', GUID),
                ('Flags', wintypes.DWORD), ('Reserved', ctypes.POINTER(wintypes.ULONG))]


class OVERLAPPED(ctypes.Structure):
    _fields_ = [('Internal', ctypes.POINTER(wintypes.ULONG)), ('InternalHigh', ctypes.POINTER(wintypes.ULONG)),
                ('Offset', wintypes.DWORD), ('OffsetHigh', wintypes.DWORD), ('hEvent', wintypes.HANDLE)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [('Size', wintypes.ULONG), ('VendorID', wintypes.USHORT),
                ('ProductID', wintypes.USHORT), ('VersionNumber', wintypes.USHORT)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [('Usage', wintypes.USHORT), ('UsagePage', wintypes.USHORT),
                ('InputReportByteLength', wintypes.USHORT), ('OutputReportByteLength', wintypes.USHORT),
                ('FeatureReportByteLength', wintypes.USHORT), ('Reserved', wintypes.USHORT * 17),
                ('NumberLinkCollectionNodes', wintypes.USHORT), ('NumberInputButtonCaps', wintypes.USHORT),
                ('NumberInputValueCaps', wintypes.USHORT), ('NumberInputDataIndices', wintypes.USHORT),
                ('NumberOutputButtonCaps', wintypes.USHORT), ('NumberOutputValueCaps', wintypes.USHORT),
                ('NumberOutputDataIndices', wintypes.USHORT), ('NumberFeatureButtonCaps', wintypes.USHORT),
                ('NumberFeatureValueCaps', wintypes.USHORT), ('NumberFeatureDataIndices', wintypes.USHORT)]


def paths():
    """The paths of every HID interface in the system."""
    guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))
    # 64-bit Python: a HANDLE does not fit into an int by default - we set the types
    # explicitly, otherwise ctypes throws OverflowError on the very first call.
    setup.SetupDiGetClassDevsW.restype = wintypes.HANDLE
    setup.SetupDiEnumDeviceInterfaces.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID),
                                                  wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
    setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
                                                       ctypes.c_void_p, wintypes.DWORD,
                                                       ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    setup.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    k32.CreateEventW.restype = wintypes.HANDLE
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                             ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    k32.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                        ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
    k32.CancelIo.argtypes = [wintypes.HANDLE]
    hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
    hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
    dev = setup.SetupDiGetClassDevsW(ctypes.byref(guid), None, None,
                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    out = []
    i = 0
    while True:
        did = SP_DEVICE_INTERFACE_DATA()
        did.cbSize = ctypes.sizeof(did)
        if not setup.SetupDiEnumDeviceInterfaces(dev, None, ctypes.byref(guid), i, ctypes.byref(did)):
            break
        i += 1
        need = wintypes.DWORD()
        setup.SetupDiGetDeviceInterfaceDetailW(dev, ctypes.byref(did), None, 0, ctypes.byref(need), None)
        buf = ctypes.create_string_buffer(need.value)
        # SP_DEVICE_INTERFACE_DETAIL_DATA_W: cbSize (4/8 depending on the bitness) + the path
        ctypes.memmove(buf, ctypes.byref(wintypes.DWORD(8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6)), 4)
        if setup.SetupDiGetDeviceInterfaceDetailW(dev, ctypes.byref(did), buf, need.value,
                                                  ctypes.byref(need), None):
            out.append(ctypes.wstring_at(ctypes.addressof(buf) + 4))
    setup.SetupDiDestroyDeviceInfoList(dev)
    return out


def open_dev(path):
    h = k32.CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, None,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, None)
    return None if h == INVALID_HANDLE else h


def attrs(h):
    a = HIDD_ATTRIBUTES(); a.Size = ctypes.sizeof(a)
    return a if hid.HidD_GetAttributes(h, ctypes.byref(a)) else None


def caps(h):
    pre = ctypes.c_void_p()
    if not hid.HidD_GetPreparsedData(h, ctypes.byref(pre)):
        return None
    c = HIDP_CAPS()
    ok = hid.HidP_GetCaps(pre, ctypes.byref(c)) == 0x00110000   # HIDP_STATUS_SUCCESS
    hid.HidD_FreePreparsedData(pre)
    return c if ok else None


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
    # EVERY matching device rather than the first: a pad can have several nodes
    # (USB and Bluetooth, two paired copies), and the live one may not be the first.
    found = []
    for p in paths():
        h = open_dev(p)
        if not h:
            continue
        a = attrs(h)
        if a and a.VendorID == 0x054C and a.ProductID == 0x0CE6:
            found.append((p, h, caps(h)))
        else:
            k32.CloseHandle(h)
    if not found:
        print('device 054C:0CE6 did not open'); return 1
    print('DualSense nodes: %d' % len(found))
    which = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    for i, (p, h, c) in enumerate(found):
        mark = '<< reading' if i == which else ''
        print(' [%d] %s  report %d bytes %s' % (i, p[:78], c.InputReportByteLength if c else -1, mark))
    p, target, c = found[which]
    for i, (_, h, _) in enumerate(found):
        if i != which:
            k32.CloseHandle(h)

    n = c.InputReportByteLength if c else 64
    buf = ctypes.create_string_buffer(n)
    ov = OVERLAPPED()
    ov.hEvent = k32.CreateEventW(None, True, False, None)
    read = wintypes.DWORD()
    seen = {}
    t0 = time.time()
    print('reading reports for %g s - press some buttons' % secs)
    while time.time() - t0 < secs:
        k32.ResetEvent(ov.hEvent)
        ok = k32.ReadFile(target, buf, n, ctypes.byref(read), ctypes.byref(ov))
        if not ok and ctypes.get_last_error() != 997:      # ERROR_IO_PENDING
            print('ReadFile refused, code %d' % ctypes.get_last_error()); break
        if k32.WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT:
            k32.CancelIo(target)
            continue
        k32.GetOverlappedResult(target, ctypes.byref(ov), ctypes.byref(read), False)
        if read.value:
            rid = buf.raw[0]
            e = seen.setdefault(rid, [0, None])
            e[0] += 1
            e[1] = buf.raw[:min(read.value, 16)]
    k32.CloseHandle(ov.hEvent); k32.CloseHandle(target)

    if not seen:
        print('NOT ONE report the whole time - the pad is silent at the HID level')
    for rid, (cnt, sample) in sorted(seen.items()):
        mode = {0x01: 'simple (DirectInput understands it)',
                0x31: 'EXTENDED (DirectInput goes blind)'}.get(rid, 'unknown')
        print('report 0x%02X: %d of them - %s' % (rid, cnt, mode))
        print('   the first bytes:', ' '.join('%02X' % b for b in sample))
    return 0


if __name__ == '__main__':
    sys.exit(main())
