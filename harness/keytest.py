# -*- coding: utf-8 -*-
"""Do the toolbar pane commands work at all - a check from the keyboard.

In the stock map the Ctrl layer already holds Ctrl+F12 -> CMD_uiToolbarPaneNext and
Ctrl+F11 -> PanePrev. No restart is needed: the map is already loaded.

If the pane switches on Ctrl+F12 but not on a pad button, the joystick route is to
blame rather than the map or the commands. If it does not switch either way, the
problem is higher up: either our map was not picked up or the pane commands do nothing.

The keys are sent as scancodes: the client reads the keyboard through DirectInput, and
that looks at the scancode rather than the virtual key.
"""
import ctypes, os, sys, time
from ctypes import wintypes
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shot

OUT = os.path.join(HERE, 'shots')
PANE = (1020, 35, 1100, 105)          # the corner with the pane number

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

KEYEVENTF_SCANCODE = 0x0008
KEYEVENTF_KEYUP = 0x0002
INPUT_KEYBOARD = 1

DIK_LCTRL, DIK_F11, DIK_F12 = 0x1D, 0x57, 0x58


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD),
                ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD),
                ('dwExtraInfo', ctypes.POINTER(ctypes.c_ulong))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KEYBDINPUT), ('pad', ctypes.c_byte * 24)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def key(scan, up=False):
    i = INPUT()
    i.type = INPUT_KEYBOARD
    i.ki.wVk = 0
    i.ki.wScan = scan
    i.ki.dwFlags = KEYEVENTF_SCANCODE | (KEYEVENTF_KEYUP if up else 0)
    u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def combo(mod, k, hold=0.12):
    key(mod); time.sleep(0.05)
    key(k); time.sleep(hold); key(k, True)
    time.sleep(0.05); key(mod, True)


def grab(tag):
    hwnd, _, _ = shot.find('SwgClient')
    w, h, im = shot.capture(hwnd, os.path.join(OUT, 'key_%s.png' % tag))
    return im.crop(PANE)


if __name__ == '__main__':
    hwnd, _, _ = shot.find('SwgClient')
    if not hwnd:
        raise SystemExit('the client window was not found')
    u32.SetForegroundWindow(hwnd)
    time.sleep(1.2)
    print('the window is in front: %s' % ('yes' if u32.GetForegroundWindow() == hwnd else 'NO'))

    # We capture WHILE the keys are held: the pane commands are a DOWN/UP pair and the
    # pane only holds while held, returning on the release.
    before = grab('before')
    key(DIK_LCTRL); time.sleep(0.08)
    key(DIK_F12); time.sleep(0.7)
    after = grab('after')
    key(DIK_F12, True); time.sleep(0.08); key(DIK_LCTRL, True)

    d = ImageChops.difference(before.convert('RGB'), after.convert('RGB'))
    box = d.getbbox()
    px = sum(sum(t) for t in d.getdata())
    print('the pane corner: difference %d, area %s' % (px, box))
    print('switching panes from the keyboard: %s'
          % ('WORKS' if px > 300 else 'not noticeable'))
    both = Image.new('RGB', (before.width * 2 + 8, before.height), (20, 20, 20))
    both.paste(before, (0, 0)); both.paste(after, (before.width + 8, 0))
    both = both.resize((both.width * 5, both.height * 5), Image.LANCZOS)
    both.save(os.path.join(OUT, 'pane_cmp.png'))
    print('before on the left, after on the right -> pane_cmp.png')
