# -*- coding: utf-8 -*-
"""Un-minimize a window by title and bring it to the front.

The launcher hides in the tray and shrinks to a strip; SW_RESTORE undoes the
maximization and takes Play off screen, so SW_SHOW comes first, and if the window is
still tiny, SW_MAXIMIZE.

    python show.py "Infinity Launcher"
"""
import ctypes, sys, time
from ctypes import wintypes

u32 = ctypes.WinDLL('user32', use_last_error=True)
SW_SHOW, SW_RESTORE, SW_MAXIMIZE = 5, 9, 3


def find(title):
    out = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        n = u32.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if title.lower() in b.value.lower():
                out.append((h, b.value))
        return True
    u32.EnumWindows(cb, 0)
    return out


def rect(h):
    r = wintypes.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
    return r.right - r.left, r.bottom - r.top


def main():
    title = sys.argv[1] if len(sys.argv) > 1 else 'Infinity Launcher'
    wins = find(title)
    if not wins:
        print('window not found:', title); return 1
    for h, name in wins:
        u32.ShowWindow(h, SW_SHOW)
        u32.SetForegroundWindow(h)
        time.sleep(0.3)
        w, ht = rect(h)
        if w < 600 or ht < 400:
            u32.ShowWindow(h, SW_MAXIMIZE)
            time.sleep(0.5)
            w, ht = rect(h)
        print('%s: %dx%d' % (name, w, ht))
    return 0


if __name__ == '__main__':
    sys.exit(main())
