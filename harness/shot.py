# -*- coding: utf-8 -*-
"""A window screenshot - to check the result when nobody is at the screen.

We capture the window itself, by title or by process, rather than the desktop. For the
game in windowed mode copying off the screen will do; in exclusive fullscreen the copy
comes out black, and then the client has to be put into a window.
"""
import ctypes, os, sys, time
from ctypes import wintypes

u32, g32 = ctypes.windll.user32, ctypes.windll.gdi32

class RECT(ctypes.Structure):
    _fields_ = [('left', ctypes.c_long), ('top', ctypes.c_long),
                ('right', ctypes.c_long), ('bottom', ctypes.c_long)]

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', ctypes.c_long),
                ('biHeight', ctypes.c_long), ('biPlanes', wintypes.WORD),
                ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', ctypes.c_long),
                ('biYPelsPerMeter', ctypes.c_long), ('biClrUsed', wintypes.DWORD),
                ('biClrImportant', wintypes.DWORD)]

ENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

def windows():
    """Every visible window with a non-empty title: (hwnd, title, pid)."""
    out = []
    def cb(h, l):
        if not u32.IsWindowVisible(h):
            return True
        n = u32.GetWindowTextLengthW(h)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, buf, n + 1)
            pid = wintypes.DWORD()
            u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            out.append((h, buf.value, pid.value))
        return True
    u32.EnumWindows(ENUMPROC(cb), 0)
    return out

def find(title_part):
    t = title_part.lower()
    for h, name, pid in windows():
        if t in name.lower():
            return h, name, pid
    return None, None, None

def capture(hwnd, path, force_front=False):
    """A capture of a window's contents.

    PrintWindow first: it draws the window by itself, so being covered by another
    window does not spoil the frame. The first attempt, a BitBlt off the screen, gave
    the contents of whichever window was on top. If PrintWindow returned emptiness (as
    happens with fullscreen DirectX), we raise the window and copy off the screen.
    """
    from PIL import Image
    u32.SetProcessDPIAware()
    if force_front:
        u32.SetForegroundWindow(hwnd)
        time.sleep(0.6)
    r = RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        raise ValueError('the window has zero size')

    def grab(use_print):
        src = u32.GetDC(0)
        dst = g32.CreateCompatibleDC(src)
        bmp = g32.CreateCompatibleBitmap(src, w, h)
        g32.SelectObject(dst, bmp)
        if use_print:
            u32.PrintWindow(hwnd, dst, 2)          # PW_RENDERFULLCONTENT
        else:
            g32.BitBlt(dst, 0, 0, w, h, src, r.left, r.top, 0x00CC0020)
        bi = BITMAPINFOHEADER()
        bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bi.biWidth, bi.biHeight = w, -h
        bi.biPlanes, bi.biBitCount, bi.biCompression = 1, 32, 0
        buf = ctypes.create_string_buffer(w * h * 4)
        g32.GetDIBits(dst, bmp, 0, h, buf, ctypes.byref(bi), 0)
        g32.DeleteObject(bmp); g32.DeleteDC(dst); u32.ReleaseDC(0, src)
        return Image.frombuffer('RGBA', (w, h), buf.raw, 'raw', 'BGRA', 0, 1).convert('RGB')

    im = grab(True)
    if is_black(im):
        u32.SetForegroundWindow(hwnd)
        time.sleep(0.8)
        im = grab(False)
    im.save(path)
    return w, h, im


def is_black(im, thresh=6):
    """Is the capture empty - a sign of exclusive fullscreen mode."""
    small = im.resize((32, 32))
    px = list(small.getdata())
    return max(max(p) for p in px) < thresh

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] == '--list':
        for h, name, pid in windows():
            print('  hwnd=%-10d pid=%-7d %s' % (h, pid, name[:70]))
        raise SystemExit(0)
    hwnd, name, pid = find(sys.argv[1])
    if not hwnd:
        print('window not found: %r' % sys.argv[1]); raise SystemExit(1)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'shots', 'shot.png')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    w, h, im = capture(hwnd, out, force_front='--front' in sys.argv)
    print('window %r (pid %d): %dx%d -> %s  %d bytes'
          % (name[:50], pid, w, h, os.path.basename(out), os.path.getsize(out)))
    if is_black(im):
        print('  WARNING: the capture is black - probably exclusive fullscreen mode')
