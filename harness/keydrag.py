# -*- coding: utf-8 -*-
"""Keys by scancode and a mouse drag inside the client window.

    python keydrag.py key 39            # press a DirectInput scancode (39 = ';')
    python keydrag.py drag X1 Y1 X2 Y2  # drag, in SwgClient window coordinates

The scancodes are the ones in the input map (DIK_*). The client reads the keyboard
through DirectInput in buffered mode, so we send scancodes
(KEYEVENTF_SCANCODE) rather than virtual keys.
"""
import ctypes, sys, time
from ctypes import wintypes
sys.path.insert(0, __import__('os').path.dirname(__file__))
import click

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

ULONG_PTR = ctypes.c_ulonglong if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_ulong

class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD),
                ('time', wintypes.DWORD), ('dwExtraInfo', ULONG_PTR)]

class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('mouseData', wintypes.DWORD),
                ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ULONG_PTR)]

class _U(ctypes.Union):
    _fields_ = [('ki', KEYBDINPUT), ('mi', MOUSEINPUT)]

class INPUT(ctypes.Structure):
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def key(scan, hold=0.08):
    ext = 0x0100 if scan >= 0x80 else 0
    down = INPUT(type=1); down.u.ki = KEYBDINPUT(0, scan & 0x7F, 0x0008 | ext, 0, 0)
    up = INPUT(type=1); up.u.ki = KEYBDINPUT(0, scan & 0x7F, 0x0008 | 0x0002 | ext, 0, 0)
    u32.SendInput(1, ctypes.byref(down), ctypes.sizeof(INPUT)); time.sleep(hold)
    u32.SendInput(1, ctypes.byref(up), ctypes.sizeof(INPUT))


def drag(title, x1, y1, x2, y2, steps=25):
    h, name = click.find(title)
    if not h: raise SystemExit('window not found: %r' % title)
    u32.SetForegroundWindow(h); time.sleep(0.4)
    r = click.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
    sx1, sy1, sx2, sy2 = r.left + x1, r.top + y1, r.left + x2, r.top + y2
    u32.SetCursorPos(sx1, sy1); time.sleep(0.15)
    u32.mouse_event(0x0002, 0, 0, 0, 0); time.sleep(0.15)          # LEFTDOWN
    for i in range(1, steps + 1):
        u32.SetCursorPos(sx1 + (sx2 - sx1) * i // steps, sy1 + (sy2 - sy1) * i // steps)
        time.sleep(0.02)
    time.sleep(0.25)
    u32.mouse_event(0x0004, 0, 0, 0, 0)                             # LEFTUP
    print('dragged (%d,%d) -> (%d,%d) in window %r' % (x1, y1, x2, y2, name[:20]))


if __name__ == '__main__':
    if sys.argv[1] == 'key':
        h, _ = click.find('SwgClient')
        if h: u32.SetForegroundWindow(h); time.sleep(0.4)
        key(int(sys.argv[2], 0)); print('scancode %s pressed' % sys.argv[2])
    elif sys.argv[1] == 'drag':
        drag('SwgClient', *map(int, sys.argv[2:6]))
