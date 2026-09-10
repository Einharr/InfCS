# -*- coding: utf-8 -*-
"""Reading a device's state through DirectInput8 - through the client's eyes.

Needed in order to check the layout without starting the game: we press a virtual
button and look at which button index DirectInput sees. That index is the one that goes
into the DATA record of the input map's JOYB form.
"""
import ctypes, sys
from ctypes import wintypes, POINTER, byref, c_void_p, c_ulong, c_ushort, c_wchar, c_long, c_ubyte

class GUID(ctypes.Structure):
    _fields_ = [('d1', c_ulong), ('d2', c_ushort), ('d3', c_ushort), ('d4', c_ubyte * 8)]
    def __init__(self, d1=0, d2=0, d3=0, d4=b''):
        super().__init__()
        self.d1, self.d2, self.d3 = d1, d2, d3
        for i, b in enumerate(d4): self.d4[i] = b
    def __str__(self):
        return '%08X-%04X-%04X-%s' % (self.d1, self.d2, self.d3, bytes(self.d4).hex().upper())
    def key(self):
        return (self.d1, self.d2, self.d3, bytes(self.d4))

class DIDEVICEINSTANCEW(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('guidInstance', GUID), ('guidProduct', GUID),
                ('dwDevType', wintypes.DWORD), ('tszInstanceName', c_wchar * 260),
                ('tszProductName', c_wchar * 260), ('guidFFDriver', GUID),
                ('wUsagePage', c_ushort), ('wUsage', c_ushort)]

class DIOBJECTDATAFORMAT(ctypes.Structure):
    _fields_ = [('pguid', POINTER(GUID)), ('dwOfs', wintypes.DWORD),
                ('dwType', wintypes.DWORD), ('dwFlags', wintypes.DWORD)]

class DIDATAFORMAT(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('dwObjSize', wintypes.DWORD),
                ('dwFlags', wintypes.DWORD), ('dwDataSize', wintypes.DWORD),
                ('dwNumObjs', wintypes.DWORD), ('rgodf', POINTER(DIOBJECTDATAFORMAT))]

class JOYSTATE(ctypes.Structure):
    """A compact analogue of DIJOYSTATE2: only what we need to read.
    The field layout defines the offsets in the data format below and must not change."""
    _pack_ = 4
    _fields_ = [('lX', c_long), ('lY', c_long), ('lZ', c_long),
                ('lRx', c_long), ('lRy', c_long), ('lRz', c_long),
                ('rglSlider', c_long * 2), ('rgdwPOV', wintypes.DWORD * 4),
                ('rgbButtons', c_ubyte * 128)]

DIDFT_AXIS, DIDFT_BUTTON, DIDFT_POV = 0x03, 0x0C, 0x10
DIDFT_ANYINSTANCE, DIDFT_OPTIONAL = 0x00FFFF00, 0x80000000
DIDF_ABSAXIS = 0x00000001

def _axis_guid(last):
    """The stock DirectInput object GUIDs: A36D02xx-C9F3-11CF-BFC7-444553540000."""
    return GUID(0xA36D0200 | last, 0xC9F3, 0x11CF,
                bytes([0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00]))

G_X, G_Y, G_Z = _axis_guid(0xE0), _axis_guid(0xE1), _axis_guid(0xE2)
G_RX, G_RY, G_RZ = _axis_guid(0xF4), _axis_guid(0xF5), _axis_guid(0xE3)
G_SLIDER, G_POV = _axis_guid(0xE4), _axis_guid(0xF2)
AXIS_ORDER = ['X', 'Y', 'Z', 'Rx', 'Ry', 'Rz']
_KEEP = [G_X, G_Y, G_Z, G_RX, G_RY, G_RZ, G_SLIDER, G_POV]

def _build_format():
    """The DIDATAFORMAT for JOYSTATE.

    The axes and the POV are declared with their real GUIDs - otherwise DirectInput
    hands them out in enumeration order and the meaningful names stop matching
    reality. The buttons need no GUID: what matters for them is the instance number."""
    objs = []
    def add(guid, ofs, typ, flags=0):
        o = DIOBJECTDATAFORMAT()
        o.pguid = ctypes.pointer(guid) if guid is not None else None
        o.dwOfs = ofs
        o.dwType = typ | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL
        o.dwFlags = flags
        objs.append(o)
    for i, g in enumerate((G_X, G_Y, G_Z, G_RX, G_RY, G_RZ)):
        add(g, i * 4, DIDFT_AXIS)
    for i in range(2):   add(G_SLIDER, 24 + i * 4, DIDFT_AXIS)
    for i in range(4):   add(G_POV, 32 + i * 4, DIDFT_POV)
    for i in range(128): add(None, 48 + i, DIDFT_BUTTON)
    arr = (DIOBJECTDATAFORMAT * len(objs))(*objs)
    fmt = DIDATAFORMAT()
    fmt.dwSize = ctypes.sizeof(DIDATAFORMAT)
    fmt.dwObjSize = ctypes.sizeof(DIOBJECTDATAFORMAT)
    fmt.dwFlags = DIDF_ABSAXIS
    fmt.dwDataSize = ctypes.sizeof(JOYSTATE)
    fmt.dwNumObjs = len(objs)
    fmt.rgodf = arr
    return fmt, arr        # we keep the array so the garbage collector does not take it

_FMT, _FMT_ARR = _build_format()

IID_IDirectInput8W = GUID(0xBF798031, 0x483A, 0x4DA2,
                          bytes([0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00]))
DI8DEVCLASS_GAMECTRL = 4
DIEDFL_ATTACHEDONLY = 1
DISCL_BACKGROUND, DISCL_NONEXCLUSIVE = 0x08, 0x02

_di8 = ctypes.WinDLL('dinput8')
_create = _di8.DirectInput8Create
_create.argtypes = [c_void_p, wintypes.DWORD, POINTER(GUID), POINTER(c_void_p), c_void_p]
_create.restype = ctypes.HRESULT
ENUMCB = ctypes.WINFUNCTYPE(wintypes.BOOL, POINTER(DIDEVICEINSTANCEW), c_void_p)

def _vt(p, i, restype, *args):
    tbl = ctypes.cast(p, POINTER(POINTER(c_void_p))).contents
    return ctypes.WINFUNCTYPE(restype, c_void_p, *args)(tbl[i])

class Device(object):
    """One game device, opened the same way the client opens it."""
    def __init__(self, di, inst):
        self.name = inst.tszProductName
        self.product = str(inst.guidProduct)
        self._p = c_void_p()
        _vt(di, 3, ctypes.HRESULT, POINTER(GUID), POINTER(c_void_p), c_void_p)(
            di, byref(inst.guidInstance), byref(self._p), None)
        _vt(self._p, 11, ctypes.HRESULT, POINTER(DIDATAFORMAT))(self._p, byref(_FMT))
        hwnd = ctypes.windll.kernel32.GetConsoleWindow()
        if hwnd:
            _vt(self._p, 13, ctypes.HRESULT, c_void_p, wintypes.DWORD)(
                self._p, hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)
        _vt(self._p, 7, ctypes.HRESULT)(self._p)

    def state(self):
        st = JOYSTATE()
        _vt(self._p, 7, ctypes.HRESULT)(self._p)      # Acquire, if we lost it
        _vt(self._p, 25, ctypes.HRESULT)(self._p)     # Poll: XInput pads do not update their state without it (the client calls it too)
        _vt(self._p, 9, ctypes.HRESULT, wintypes.DWORD, c_void_p)(
            self._p, ctypes.sizeof(st), byref(st))
        return st

    def buttons(self):
        st = self.state()
        return [i for i in range(128) if st.rgbButtons[i] & 0x80]

    def axes(self):
        st = self.state()
        return dict(X=st.lX, Y=st.lY, Z=st.lZ, Rx=st.lRx, Ry=st.lRy, Rz=st.lRz,
                    S0=st.rglSlider[0], S1=st.rglSlider[1])

    def pov(self):
        return self.state().rgdwPOV[0]

def open_devices(match=None):
    hinst_f = ctypes.windll.kernel32.GetModuleHandleW
    hinst_f.restype = c_void_p; hinst_f.argtypes = [c_void_p]
    di = c_void_p()
    _create(hinst_f(None), 0x0800, byref(IID_IDirectInput8W), byref(di), None)
    insts = []
    def cb(p, r):
        insts.append(DIDEVICEINSTANCEW.from_buffer_copy(p.contents)); return 1
    _vt(di, 4, ctypes.HRESULT, wintypes.DWORD, ENUMCB, c_void_p, wintypes.DWORD)(
        di, DI8DEVCLASS_GAMECTRL, ENUMCB(cb), None, DIEDFL_ATTACHEDONLY)
    out = []
    for i in insts:
        if match and match.lower() not in i.tszProductName.lower():
            continue
        out.append(Device(di, i))
    return out

if __name__ == '__main__':
    for d in open_devices(sys.argv[1] if len(sys.argv) > 1 else None):
        print('%-42s %s' % (d.name[:42], d.product))
        print('    buttons pressed: %s' % (d.buttons() or 'none'))
        print('    axes: %s' % d.axes())
        print('    POV: %d' % d.pov())
