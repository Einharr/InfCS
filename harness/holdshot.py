# -*- coding: utf-8 -*-
"""Hold some scancodes down, capture a frame, release them.

Needed in order to catch a HOLD: the crossbar changes the pane on the press and brings
it back on the release, so an ordinary press/release leaves nothing to capture.

    python holdshot.py out.png 73        # hold NUMPAD9 (L2), capture, release
    python holdshot.py out.png 73 79     # both triggers -> the L2+R2 pane

The DirectInput scancodes, as in the input map (layout.KEY): 73 NUMPAD9 = L2,
79 NUMPAD1 = R2 (Windows does not know 141, and F16/103 does not physically exist).
The client reads the keyboard through DirectInput, so we send scancodes.
"""
import ctypes, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import subprocess
import click, keydrag, shot

u32 = ctypes.windll.user32


def _send(scan, up):
    ext = 0x0100 if scan >= 0x80 else 0
    flags = 0x0008 | ext | (0x0002 if up else 0)
    ev = keydrag.INPUT(type=1)
    ev.u.ki = keydrag.KEYBDINPUT(0, scan & 0x7F, flags, 0, 0)
    u32.SendInput(1, ctypes.byref(ev), ctypes.sizeof(keydrag.INPUT))


def main():
    out = sys.argv[1]
    args = sys.argv[2:]
    hold = 0.6
    if '--hold' in args:
        i = args.index('--hold'); hold = float(args[i + 1]); del args[i:i + 2]
    scans = [int(a) for a in args]
    hw = shot.find('SwgClient')
    if not hw:
        print('the client window was not found'); return 1
    hwnd = hw[0] if isinstance(hw, (list, tuple)) else hw
    if isinstance(hwnd, (list, tuple)): hwnd = hwnd[0]
    u32.SetForegroundWindow(hwnd); time.sleep(0.5)
    for s in scans:
        _send(s, False); time.sleep(0.12)
    time.sleep(hold)                     # give the client time to switch the pane (and to make it into the capture)
    try:
        # we capture by the same route as the ordinary shot.py: it knows how to catch
        # a black frame and to bring the window forward
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shot.py'),
                        'SwgClient', out, '--front'], check=False)
    finally:
        for s in reversed(scans):        # release in reverse order
            _send(s, True); time.sleep(0.12)
    print('captured with %s held -> %s' % (scans, out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
