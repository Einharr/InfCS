import ctypes, os, sys
from ctypes import c_void_p, POINTER, byref, wintypes
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import di
path = sys.argv[1] if len(sys.argv) > 1 else 'dinput8'
lib = ctypes.WinDLL(path)
create = lib.DirectInput8Create
create.argtypes = [c_void_p, wintypes.DWORD, POINTER(di.GUID), POINTER(c_void_p), c_void_p]
create.restype = ctypes.HRESULT
hinst = ctypes.windll.kernel32.GetModuleHandleW; hinst.restype = c_void_p; hinst.argtypes = [c_void_p]
d = c_void_p()
hr = create(hinst(None), 0x0800, byref(di.IID_IDirectInput8W), byref(d), None)
print(path, 'DirectInput8Create hr=0x%08X' % (hr & 0xffffffff), 'ptr', d.value)
def guid(s):
    import uuid; u = uuid.UUID(s); g = di.GUID(); ctypes.memmove(byref(g), u.bytes_le, 16); return g
for name, g in (('keyboard', '6F1D2B61-D5A0-11CF-BFC7-444553540000'), ('mouse', '6F1D2B60-D5A0-11CF-BFC7-444553540000')):
    dev = c_void_p()
    try:
        hr = di._vt(d, 3, ctypes.HRESULT, POINTER(di.GUID), POINTER(c_void_p), c_void_p)(d, byref(guid(g)), byref(dev), None)
        print('  CreateDevice', name, 'hr=0x%08X' % (hr & 0xffffffff), 'ptr', dev.value)
    except OSError as e:
        print('  CreateDevice', name, 'EXC', e)
