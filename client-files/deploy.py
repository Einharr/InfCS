# -*- coding: utf-8 -*-
"""Install the client-side part of ConsolePort onto Test Center.

Puts the markup (ui/*.inc), the input maps (input/*.iff), the textures
(texture/*.dds) and consoleport.ini into the client directory. Stock files get a
.bak - exactly once, so that a repeat install cannot overwrite the original with
a copy of our own.

Run it with the client CLOSED: the client holds the loaded .inc and .dds files
open, and it only re-reads them at startup anyway.

    python deploy.py --game "E:\\Games\\SWG\\Dev\\SWG Infinity\\Test Center"
    python deploy.py --game ... --undo      # put it back as it was
    python deploy.py --game ... --force     # do not check whether the client is running

Without --game the default Test Center path is used.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_GAME = r'E:\Games\SWG\Dev\SWG Infinity\Test Center'

# our directory -> the client's subdirectory
TREES = [('ui', 'ui'), ('input', 'input'), ('texture', 'texture')]

# Entirely ours - no stock file by these names exists, so an undo just deletes them.
# Everything else is a stock file plus one <include> line and comes back from its .bak.
OURS = {
    'ui_consoleport_context.inc',
    'ui_consoleport_charselect.inc',
    'consoleport_glyphs.dds',
    'consoleport_glyphs_xbox.dds',
    'consoleport_ring.dds',
    'consoleport_rim.dds',
    'consoleport_wedge.dds',
}


# What earlier versions installed and this one does not: the fileless 12x2 crossbar
# markup. An install sweeps it out of the client so a stale copy cannot be picked up.
STALE = ['ui/ui_consoleport_toolbar.inc']


def client_running():
    """True if swgemu.exe is alive. A tasklist failure counts as "no idea" -> False."""
    try:
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq swgemu.exe'],
                             capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return False
    return 'swgemu.exe' in out.stdout


def sweep(game):
    """Remove what an earlier version of the mod installed and this one no longer ships."""
    for rel in STALE:
        p = os.path.join(game, rel.replace('/', os.sep))
        if os.path.exists(p):
            os.remove(p)
            print('swept   ', rel, '(no longer part of the mod)')
        bak = p + '.bak'
        if os.path.exists(bak):
            os.remove(bak)


def install(game):
    sweep(game)
    for src_sub, dst_sub in TREES:
        src_dir = os.path.join(HERE, src_sub)
        dst_dir = os.path.join(game, dst_sub)
        os.makedirs(dst_dir, exist_ok=True)
        for name in sorted(os.listdir(src_dir)):
            src = os.path.join(src_dir, name)
            if not os.path.isfile(src):
                continue
            dst = os.path.join(dst_dir, name)
            bak = dst + '.bak'
            if os.path.exists(dst) and not os.path.exists(bak) and name not in OURS:
                shutil.copy2(dst, bak)
                print('   .bak', dst_sub + '/' + name)
            shutil.copy2(src, dst)
            print('->      ', dst_sub + '/' + name)

    ini_src = os.path.join(HERE, 'consoleport.ini')
    ini_dst = os.path.join(game, 'consoleport.ini')
    if os.path.exists(ini_dst):
        print('=       consoleport.ini is already there, leaving it alone (your settings)')
    else:
        shutil.copy2(ini_src, ini_dst)
        print('->      consoleport.ini')

    print()
    print('done. The DLL stays in the package: python harness/inject.py build/cpinject.dll')


def undo(game):
    sweep(game)
    for src_sub, dst_sub in TREES:
        src_dir = os.path.join(HERE, src_sub)
        for name in sorted(os.listdir(src_dir)):
            if not os.path.isfile(os.path.join(src_dir, name)):
                continue
            dst = os.path.join(game, dst_sub, name)
            bak = dst + '.bak'
            if name in OURS:
                if os.path.exists(dst):
                    os.remove(dst)
                    print('deleted  ', dst_sub + '/' + name)
            elif os.path.exists(bak):
                shutil.copy2(bak, dst)
                print('restored ', dst_sub + '/' + name)
            elif os.path.exists(dst):
                # No .bak and not ours: there was no loose stock file here, it came
                # from the .tre. Delete ours and the client falls back to the .tre.
                os.remove(dst)
                print('deleted  ', dst_sub + '/' + name, '(will come back from the .tre)')
    print()
    print('done. consoleport.ini was left in place - your settings are in it.')


def main():
    ap = argparse.ArgumentParser(description='install the client-side part of ConsolePort')
    ap.add_argument('--game', default=DEFAULT_GAME, help='the client directory (where swgemu.exe is)')
    ap.add_argument('--undo', action='store_true', help='put the stock files back')
    ap.add_argument('--force', action='store_true', help='do not check whether the client is running')
    args = ap.parse_args()

    game = os.path.abspath(args.game)
    if not os.path.isfile(os.path.join(game, 'swgemu.exe')):
        sys.exit('no swgemu.exe in %s - point --game at the client directory' % game)
    if not args.force and client_running():
        sys.exit('the client is running: close it or add --force')

    print('client:', game)
    print()
    undo(game) if args.undo else install(game)


if __name__ == '__main__':
    main()
