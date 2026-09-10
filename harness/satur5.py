# -*- coding: utf-8 -*-
"""The last difference: the aspect flag on the axes in the data format.

In the client the axis objects are declared with dwFlags=0x100 (DIDOI_ASPECTPOSITION),
ours had 0. We build the format exactly as the client does and repeat SetProperty.
"""
import ctypes, os, sys
from ctypes import wintypes, POINTER, byref, c_void_p, c_ulong
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import di
from satur import DIPROPDWORD, DIPROPHEADER, name, DIPH_BYOFFSET, DIPROP_SATURATION

DIDOI_ASPECTPOSITION = 0x100

def build(aspect):
    objs = []
    def add(guid, ofs, typ, flags):
        o = di.DIOBJECTDATAFORMAT()
        o.pguid = ctypes.pointer(guid) if guid is not None else None
        o.dwOfs, o.dwType, o.dwFlags = ofs, typ | di.DIDFT_ANYINSTANCE | di.DIDFT_OPTIONAL, flags
        objs.append(o)
    for i, g in enumerate((di.G_X, di.G_Y, di.G_Z, di.G_RX, di.G_RY, di.G_RZ)):
        add(g, i * 4, di.DIDFT_AXIS, aspect)
    for i in range(2):   add(di.G_SLIDER, 24 + i * 4, di.DIDFT_AXIS, aspect)
    for i in range(4):   add(di.G_POV, 32 + i * 4, di.DIDFT_POV, 0)
    for i in range(32):  add(None, 48 + i, di.DIDFT_BUTTON, 0)
    arr = (di.DIOBJECTDATAFORMAT * len(objs))(*objs)
    f = di.DIDATAFORMAT()
    f.dwSize = ctypes.sizeof(di.DIDATAFORMAT)
    f.dwObjSize = ctypes.sizeof(di.DIOBJECTDATAFORMAT)
    f.dwFlags = 1                       # DIDF_ABSAXIS
    f.dwDataSize = 80                   # as in c_dfDIJoystick
    f.dwNumObjs = len(objs)
    f.rgodf = arr
    return f, arr

hinst = ctypes.windll.kernel32.GetModuleHandleW
hinst.restype = c_void_p; hinst.argtypes = [c_void_p]
dinput = c_void_p()
di._create(hinst(None), 0x0800, byref(di.IID_IDirectInput8W), byref(dinput), None)
insts = []
def cb(p, r):
    insts.append(di.DIDEVICEINSTANCEW.from_buffer_copy(p.contents)); return 1
di._vt(dinput, 4, ctypes.HRESULT, wintypes.DWORD, di.ENUMCB, c_void_p, wintypes.DWORD)(
    dinput, di.DI8DEVCLASS_GAMECTRL, di.ENUMCB(cb), None, di.DIEDFL_ATTACHEDONLY)

for inst in insts[:1]:
    print('=== %s' % inst.tszProductName)
    for label, aspect in (('flags=0 (as ours used to be)', 0),
                          ('flags=0x100 ASPECTPOSITION (as the client)', DIDOI_ASPECTPOSITION)):
        fmt, keep = build(aspect)
        dev = c_void_p()
        di._vt(dinput, 3, ctypes.HRESULT, POINTER(di.GUID), POINTER(c_void_p), c_void_p)(
            dinput, byref(inst.guidInstance), byref(dev), None)
        hr_f = di._vt(dev, 11, c_ulong, POINTER(di.DIDATAFORMAT))(dev, byref(fmt)) & 0xFFFFFFFF
        p = DIPROPDWORD()
        p.diph.dwSize = ctypes.sizeof(DIPROPDWORD)
        p.diph.dwHeaderSize = ctypes.sizeof(DIPROPHEADER)
        p.diph.dwObj, p.diph.dwHow, p.dwData = 0, DIPH_BYOFFSET, 10000
        fn = di._vt(dev, 6, c_ulong, c_void_p, POINTER(DIPROPDWORD))
        hr = fn(dev, ctypes.cast(DIPROP_SATURATION, c_void_p), byref(p)) & 0xFFFFFFFF
        print('   %-44s SetDataFormat=%-10s saturation=%s' % (label, name(hr_f), name(hr)))
        di._vt(dev, 2, c_ulong)(dev)
