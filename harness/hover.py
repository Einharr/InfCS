# -*- coding: utf-8 -*-
"""Move the cursor to a point in the window WITHOUT clicking.

A click on a toolbar cell would execute an ability, and all we need is the hover: the
crossbar opens a flyout from the cell's "under mouse" bit.

    python hover.py SwgClient 1200 150
"""
import ctypes, sys, time
from ctypes import wintypes

u32 = ctypes.WinDLL('user32', use_last_error=True)


def find(title):
    out = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        n = u32.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if title.lower() in b.value.lower() and u32.IsWindowVisible(h):
                r = wintypes.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
                out.append((h, r))
        return True
    u32.EnumWindows(cb, 0)
    return out


def main():
    title = sys.argv[1]
    x, y = int(sys.argv[2]), int(sys.argv[3])
    wins = find(title)
    if not wins:
        print('window not found:', title); return 1
    # we take the largest window with that title - the client has several
    h, r = max(wins, key=lambda w: (w[1].right - w[1].left) * (w[1].bottom - w[1].top))
    u32.SetForegroundWindow(h)
    time.sleep(0.2)
    sx, sy = r.left + x, r.top + y
    u32.SetCursorPos(sx, sy)
    time.sleep(0.4)
    u32.SetCursorPos(sx, sy)          # a second time: the client catches the movement
    print('window %dx%d, cursor in the window at (%d,%d), on screen at (%d,%d)'
          % (r.right - r.left, r.bottom - r.top, x, y, sx, sy))
    return 0


if __name__ == '__main__':
    sys.exit(main())
