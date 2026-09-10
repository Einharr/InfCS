# -*- coding: utf-8 -*-
"""Repairing the ButtonBar position in the player settings (.uis).

Why. SwgCuiButtonBar is a SwgCuiLockableMediator, and in its constructor it calls
setSettingsAutoSizeLocation(true, true): the client SAVES its page's Location and Size
into the profile ITSELF and restores them on the next login. While GameBar laid the
panel itself out on the ring, its shifted coordinates went into the profile - and the
panel stayed in mid-screen with no ring at all.

GameBar no longer does that (the ring is built out of clones and the original is left
alone), but what has already been written into the profile has to be put back by hand - once.

    python fixuis.py                 # show what is in the profile
    python fixuis.py --apply         # restore the stock Location/Size

The stock values come from stock/ui/ui_ground_hud_buttonbar.inc: Location 727,734,
Size 294,270. Before writing, a .cpbak is placed alongside - the client's own .bak is
left untouched, it belongs to the client.
"""
import glob
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = r'E:\Games\SWG\Dev\SWG Infinity\Test Center'
OWNER = b'SwgCuiButtonBar\x00'
STOCK_LOC = (727, 734)
STOCK_SIZE = (294, 270)


def find_files():
    return glob.glob(os.path.join(GAME, 'profiles', '*', '*', '*.uis'))


def patch(path, apply_it):
    d = bytearray(open(path, 'rb').read())
    i = d.find(OWNER)
    if i < 0:
        print('  there is no SwgCuiButtonBar record - touching nothing')
        return False
    end = i + len(OWNER)
    changed = False
    # Next come the SIZE and LOCA chunks: tag(4) + length(4, big-endian) + two int32.
    for tag, want in ((b'SIZE', STOCK_SIZE), (b'LOCA', STOCK_LOC)):
        j = d.find(tag, end, end + 64)
        if j < 0:
            print('  chunk %s not found' % tag.decode())
            continue
        n = struct.unpack_from('>I', d, j + 4)[0]
        if n != 8:
            print('  %s has an unexpected length of %d - skipping' % (tag.decode(), n))
            continue
        cur = struct.unpack_from('<ii', d, j + 8)
        print('  %s: currently %s, stock %s' % (tag.decode(), cur, want))
        if cur != want:
            struct.pack_into('<ii', d, j + 8, *want)
            changed = True
    if not changed:
        print('  already the stock values')
        return False
    if not apply_it:
        print('  (a dry run, the file was not changed - add --apply)')
        return False
    bak = path + '.cpbak'
    if not os.path.exists(bak):
        shutil.copy2(path, bak)
        print('  backup -> %s' % os.path.basename(bak))
    open(path, 'wb').write(bytes(d))
    print('  written')
    return True


def main():
    apply_it = '--apply' in sys.argv
    files = find_files()
    if not files:
        print('found no .uis files in', GAME)
        return 1
    for p in files:
        print(os.path.relpath(p, GAME))
        patch(p, apply_it)
    if not apply_it:
        print('\nThe client has to be CLOSED: it rewrites the .uis on exit.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
