# -*- coding: utf-8 -*-
"""A mouse click on a window - the coordinates are relative to the window, not the screen.

We make the process DPI-aware, otherwise GetWindowRect returns logical pixels while
SendInput expects physical ones, and the click lands twice as far off.
"""
import ctypes, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

class RECT(ctypes.Structure):
    _fields_ = [('left', ctypes.c_long), ('top', ctypes.c_long),
                ('right', ctypes.c_long), ('bottom', ctypes.c_long)]

ENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

def find(part):
    hit = []
    def cb(h, l):
        if not u32.IsWindowVisible(h): return True
        n = u32.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if part.lower() in b.value.lower(): hit.append((h, b.value))
        return True
    u32.EnumWindows(ENUMPROC(cb), 0)
    return hit[0] if hit else (None, None)

def click(title_part, x, y, front=True, double=False):
    h, name = find(title_part)
    if not h: raise SystemExit('window not found: %r' % title_part)
    if front:
        u32.SetForegroundWindow(h); time.sleep(0.5)   # no ShowWindow:
        # SW_RESTORE un-maximizes a maximized window and takes its edge off screen
    r = RECT(); u32.GetWindowRect(h, ctypes.byref(r))
    sx, sy = r.left + int(x), r.top + int(y)
    u32.SetCursorPos(sx, sy); time.sleep(0.15)
    for _ in range(2 if double else 1):
        u32.mouse_event(0x0002, 0, 0, 0, 0)   # LEFTDOWN
        time.sleep(0.06)
        u32.mouse_event(0x0004, 0, 0, 0, 0)   # LEFTUP
        time.sleep(0.09)
    print('click on %r window=%dx%d window point=(%d,%d) screen=(%d,%d)'
          % (name[:40], r.right - r.left, r.bottom - r.top, x, y, sx, sy))
    return h

if __name__ == '__main__':
    click(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]),
          double='--double' in sys.argv)
