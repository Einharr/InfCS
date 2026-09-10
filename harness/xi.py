# -*- coding: utf-8 -*-
"""Reading the pad through XInput - a second, independent route alongside DirectInput.

WHY. A pad brought up as an XInput device (the Steam Input virtual controller, any
Xbox compatible one, ViGEm x360) is shown by DirectInput GLUED: both triggers live on
one Z axis, rest at the center, LT pulling one way and RT the other.
A simultaneous press cannot be expressed on that axis at all - the values cancel each
other out. XInput reports the same pad as an XINPUT_GAMEPAD, where bLeftTrigger and
bRightTrigger are two independent bytes, 0..255.

The DLLs are tried in descending order: xinput1_4 (Win8+), xinput9_1_0 (always present
since Vista), xinput1_3 (only with the DirectX redistributable). On this machine
SysWOW64 holds 1_4 and 9_1_0 - so the x86 client has what it needs and no redistributable.
"""
import ctypes, time
from ctypes import wintypes

DLLS = ('XInput1_4', 'XInput9_1_0', 'XInput1_3')

class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [('wButtons', wintypes.WORD),
                ('bLeftTrigger', ctypes.c_ubyte), ('bRightTrigger', ctypes.c_ubyte),
                ('sThumbLX', ctypes.c_short), ('sThumbLY', ctypes.c_short),
                ('sThumbRX', ctypes.c_short), ('sThumbRY', ctypes.c_short)]

class XINPUT_STATE(ctypes.Structure):
    _fields_ = [('dwPacketNumber', wintypes.DWORD), ('Gamepad', XINPUT_GAMEPAD)]

class XINPUT_CAPABILITIES(ctypes.Structure):
    _fields_ = [('Type', ctypes.c_ubyte), ('SubType', ctypes.c_ubyte),
                ('Flags', wintypes.WORD), ('Gamepad', XINPUT_GAMEPAD),
                ('Vibration', wintypes.WORD * 2)]

ERROR_SUCCESS = 0
ERROR_DEVICE_NOT_CONNECTED = 1167

BUTTONS = [(0x0001,'DPAD_UP'), (0x0002,'DPAD_DOWN'), (0x0004,'DPAD_LEFT'), (0x0008,'DPAD_RIGHT'),
           (0x0010,'START'), (0x0020,'BACK'), (0x0040,'LS'), (0x0080,'RS'),
           (0x0100,'LB'), (0x0200,'RB'), (0x0400,'GUIDE'), (0x1000,'A'), (0x2000,'B'),
           (0x4000,'X'), (0x8000,'Y')]

def button_names(mask):
    return [n for b, n in BUTTONS if mask & b] or ['none']


class XInput(object):
    """One loaded XInput implementation."""

    def __init__(self):
        self.name = None
        self.dll = None
        for d in DLLS:
            try:
                self.dll = ctypes.WinDLL(d)
                self.name = d
                break
            except OSError:
                continue
        if self.dll is None:
            raise OSError('not one XInput DLL was found: %s' % ', '.join(DLLS))
        self._get = self.dll.XInputGetState
        self._get.argtypes = [wintypes.DWORD, ctypes.POINTER(XINPUT_STATE)]
        self._get.restype = wintypes.DWORD
        self._caps = self.dll.XInputGetCapabilities
        self._caps.argtypes = [wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(XINPUT_CAPABILITIES)]
        self._caps.restype = wintypes.DWORD
        # XInputGetStateEx - ordinal 100, it also reports the GUIDE button. Not everywhere.
        self._getex = None
        try:
            f = getattr(self.dll, '100', None) or self.dll[100]
            f.argtypes = [wintypes.DWORD, ctypes.POINTER(XINPUT_STATE)]
            f.restype = wintypes.DWORD
            self._getex = f
        except Exception:
            pass

    def state(self, idx):
        """The slot's XINPUT_STATE, or None if the slot is empty."""
        s = XINPUT_STATE()
        return s if self._get(idx, ctypes.byref(s)) == ERROR_SUCCESS else None

    def caps(self, idx):
        c = XINPUT_CAPABILITIES()
        return c if self._caps(idx, 0, ctypes.byref(c)) == ERROR_SUCCESS else None

    def slots(self):
        return [i for i in range(4) if self.state(i) is not None]

    def triggers(self, idx):
        s = self.state(idx)
        return (None, None) if s is None else (s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger)


def describe(x, idx):
    c = x.caps(idx)
    s = x.state(idx)
    if s is None:
        return 'slot %d: empty' % idx
    sub = {0:'UNKNOWN',1:'GAMEPAD',2:'WHEEL',3:'ARCADE_STICK',4:'FLIGHT_STICK',
           5:'DANCE_PAD',6:'GUITAR',8:'DRUM_KIT'}.get(c.SubType if c else -1, '?')
    return ('slot %d: subtype %s, packet %d, LT=%3d RT=%3d, buttons %s'
            % (idx, sub, s.dwPacketNumber, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger,
               ','.join(button_names(s.Gamepad.wButtons))))


if __name__ == '__main__':
    x = XInput()
    print('loaded: %s.dll%s' % (x.name, ', XInputGetStateEx present (ordinal 100)' if x._getex else ''))
    busy = x.slots()
    print('occupied slots:', busy or 'none')
    for i in busy:
        print('   ', describe(x, i))
    if busy:
        print()
        print('live reading for 3 s, step 0.2 s - press the triggers:')
        t0 = time.time()
        while time.time() - t0 < 3.0:
            for i in busy:
                s = x.state(i)
                if s:
                    print('  %d: LT=%3d RT=%3d %s' % (i, s.Gamepad.bLeftTrigger,
                          s.Gamepad.bRightTrigger, ','.join(button_names(s.Gamepad.wButtons))))
            time.sleep(0.2)
