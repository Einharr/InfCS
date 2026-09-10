"""Removing the saved window positions from a character profile.

The client stores every window's geometry in profiles/<name>/<server>/<id>.uis and
restores it OVER the markup. While an old rectangle sits there, no edit to the .inc
affects the position.

We delete only the SIZE and LOCA chunks of the chosen mediators. Everything else
(the chat tabs, the channels, the pane counters) is left untouched.

Run it with the game CLOSED: the client rewrites the file on exit.
"""
import os, re, struct, sys, shutil, glob

MEDIATORS = [
    'SwgCuiToolbar', 'SwgCuiStatusGround', 'SwgCuiTargets', 'SwgCuiCombatQueue',
    'SwgCuiGroundRadar', 'SwgCuiAttributeModifiers', 'SwgCuiChatWindow',
    'SwgCuiGroup', 'SwgCuiButtonBar', 'SwgCuiIncap',
]
STRIP = {'SIZE', 'LOCA'}


def parse(b, o, end):
    items = []
    while o < end:
        tag = b[o:o + 4]; size = struct.unpack_from('>I', b, o + 4)[0]; o += 8
        if tag == b'FORM':
            items.append(['FORM', b[o:o + 4].decode('latin-1'), parse(b, o + 4, o + size)])
        else:
            items.append(['CHUNK', tag.decode('latin-1'), b[o:o + size]])
        o += size
    return items


def ser(items):
    out = b''
    for kind, name, payload in items:
        if kind == 'FORM':
            body = ser(payload)
            out += b'FORM' + struct.pack('>I', 4 + len(body)) + name.encode('latin-1').ljust(4) + body
        else:
            out += name.encode('latin-1').ljust(4) + struct.pack('>I', len(payload)) + payload
    return out


def owner_name(items):
    for kind, name, payload in items:
        if kind == 'CHUNK' and name.strip() == 'NAME':
            return payload.split(b'\0')[0].decode('latin-1')
    return None


def clean(items, hits):
    for it in items:
        if it[0] != 'FORM':
            continue
        if it[1].strip() == 'OWNE':
            nm = owner_name(it[2])
            if nm in MEDIATORS:
                before = len(it[2])
                it[2] = [c for c in it[2] if not (c[0] == 'CHUNK' and c[1].strip() in STRIP)]
                if len(it[2]) != before:
                    hits.append((nm, before - len(it[2])))
        clean(it[2], hits)
    return items


def process(path, dry=False):
    b = open(path, 'rb').read()
    tree = parse(b, 0, len(b))
    if ser(tree) != b:
        print('  SKIPPED (the round-trip did not add up, the file was not touched): %s' % path)
        return False
    hits = []
    clean(tree, hits)
    if not hits:
        print('  nothing to remove: %s' % os.path.basename(path))
        return True
    out = ser(tree)
    print('  %s: %d bytes -> %d' % (os.path.basename(path), len(b), len(out)))
    for nm, n in hits:
        print('     %d chunk(s) removed from %s' % (n, nm))
    if not dry:
        if not os.path.exists(path + '.bak'):
            shutil.copy2(path, path + '.bak')
        open(path, 'wb').write(out)
    return True


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else r'E:\Games\SWG\Live\profiles'
    dry = '--dry' in sys.argv
    files = glob.glob(os.path.join(root, '*', '*', '*.uis'))
    if not files:
        print('no .uis files found in', root); sys.exit(1)
    print('mode: %s\n' % ('a dry run' if dry else 'writing'))
    for f in files:
        process(f, dry)
