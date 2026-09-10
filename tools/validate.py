# -*- coding: utf-8 -*-
"""Checks of the input files and of the crossbar model without running the client.

The input map is checked for every device profile (layout.PROFILES):
each has its own button numbering and its own set of preset files.
"""
import os, sys, struct, glob, re
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from iffio import walk
import cmdrec2, cmdtable, imap, layout as L

ROOT = os.path.dirname(HERE)
DIST = os.path.join(ROOT, 'client-files'); SRC = os.path.join(ROOT, 'stock')
ok = []; bad = []
def chk(c, m): (ok if c else bad).append(m)

# ---------- 1. the command table ----------
cp = os.path.join(DIST, 'input', 'groundinputmap_cmds.iff')
C = open(cp, 'rb').read(); S = open(os.path.join(SRC, 'input', 'groundinputmap_cmds.iff'), 'rb').read()
seen = cmdtable.read_like_client(C); stock = cmdtable.read_like_client(S)
extra = len(L.AXIS_CMDS) + 1 + len(L.PANE_HOLD)      # the axes + the file model M3 + the pane holds
chk(len(seen) == len(stock) + extra,
    "commands the client sees: %d (%d stock + %d axes + M3 + %d pane holds)"
    % (len(seen), len(stock), len(L.AXIS_CMDS), len(L.PANE_HOLD)))
chk([x for x in seen if x[0] != 'custom'] == stock, "every stock command was carried over byte for byte")
custom = {n: cmdrec2.parse(p) for cat, n, p in seen if cat == 'custom'}
for name, msg, mult in L.AXIS_CMDS:
    d = custom.get(name)
    chk(d is not None and d['types'] == cmdrec2.T_AXIS and d['press'][0] == msg and abs(d['press'][1] - mult) < 1e-6,
        "%s: AXIS, message %d, multiplier %g, in the custom category" % (name, msg, mult))
chk(all(c[0] == 'custom' for c in seen if c[1].startswith('CMD_cp')), "our own commands are in custom only")

# ---------- 2. the input maps by profile ----------
occ = set()
for f in glob.glob(os.path.join(SRC, '*inputmap*.txt')):
    for l in open(f, encoding='latin-1'):
        w = l.split()
        if len(w) == 3 and w[0] == 'KEYS' and w[1].isdigit(): occ.add(int(w[1]))
clash = [L.KEYNAME[k] for k in L.KEYNAME if k in occ]
chk(not clash, "all %d new keys are free in the stock maps" % len(L.KEYNAME) + (" | TAKEN: %s" % clash if clash else ""))

for profile, prof in L.PROFILES.items():
    joy = prof['joy']; P = '[%s] ' % profile
    files = [os.path.join(DIST, 'input', n) for n in prof['presets']]
    blobs = [open(f, 'rb').read() for f in files]
    chk(all(b == blobs[0] for b in blobs), P + "every copy of the preset is identical: %s" % ', '.join(prof['presets']))
    txt = imap.decompile(files[0])
    chk(imap.compile_(txt) == blobs[0], P + "the input map rebuilds byte for byte")
    chk('cmds input/groundinputmap_cmds.iff' in txt, P + "the map references our command table")

    lay = {}; cur = None; dup = []; shifts = {}
    for l in txt.splitlines():
        w = l.split()
        if not w: continue
        if w[0] == 'shift': shifts.setdefault(int(w[3], 0), []).append((w[1], int(w[2], 0)))
        elif w[0] == 'layer': cur = int(w[1], 0); lay[cur] = {}
        elif l.startswith('  ') and len(w) == 3:
            k = (w[0], int(w[1]))
            if k in lay[cur]: dup.append("0x%02X %s %d" % (cur, w[0], k[1]))
            lay[cur][k] = w[2]
    chk(not dup, P + "no double bindings on one input within a layer" + (" | " + ", ".join(dup) if dup else ""))
    for m in ('L2', 'R2'):
        bit = L.MOD_BITS[m]
        want_joy = joy.get(m) if prof['triggers_as_buttons'] else None
        have_key = ('KEY', L.KEY[m]) in shifts.get(bit, [])
        have_joy = want_joy is None or ('JOYB', want_joy) in shifts.get(bit, [])
        chk(have_key and have_joy, P + "%s: modifier 0x%02X = %s%s" % (m, bit, ('JOYB %d + ' % want_joy) if want_joy is not None else '', L.KEYNAME[L.KEY[m]]))
    # L1 stopped being a modifier: the module took the button for the target cycles.
    chk(('JOYB', joy['L1']) not in sum(shifts.values(), []), P + "L1: not a modifier, JOYB %d is free" % joy['L1'])
    for name in ('L1', 'R1'):
        used = [(s_, k) for s_, lay_ in lay.items() for k in lay_ if k == ('JOYB', joy[name])]
        chk(not used, P + "%s: not taken in any layer" % name)
    ours = set(L.LAYER_OF_MOD.values())
    chk(all(s in lay for s in ours), P + "layers: " + " ".join("0x%02X" % s for s in sorted(lay)))

    # The map no longer dispatches the slots itself: the module reads the pad and calls
    # the toolbar through the getToolbarItem hook, so there are no per-cluster
    # CMD_uiToolbarSlotNN bindings, no keyboard 1..8 chain and no service buttons on
    # L1/R1 in here (see layout.UTIL and world/targeting). What the map still owes us is
    # the movement in every layer - the module never sends movement to the server.
    for s in ours:
        got = set(lay[s].values())
        chk(L.MOVEMENT_CMDS <= got, P + "layer 0x%02X: the movement keys are in place" % s)
        chk(not [k for k in lay[s] if k[0] == 'JOYX'],
            P + "layer 0x%02X: no axis bindings - the sticks arrive as JOYB 18..25 from the module" % s)

# ---------- 3. the input names ----------
b = open(os.path.join(DIST, 'input', 'inputnames.iff'), 'rb').read(); nm = {}
walk(b, 0, len(b), [], lambda pa, t, d: nm.__setitem__(struct.unpack_from('<I', d)[0], d[4:].split(b'\0')[0].decode('latin-1'))
     if t.strip() == 'CODE' and pa[-1] == 'JOYB' else None)
wrong = [(k, nm.get(k)) for k, v in L.GLYPH.items() if nm.get(k) != v]
chk(not wrong, "the JOYB names are captioned with glyphs by the DS4 numbering (with 30/31 as the fallback)" + (" | MISMATCH: %s" % wrong if wrong else ""))

# ---------- 4. the markup ----------
g = open(os.path.join(DIST, 'ui', 'ui_ground_hud.inc'), encoding='latin-1').read()
# In the module's model the toolbar page stays the stock/QoL one - the crossbar is built
# inside it from code (crossbar/crossbar.cpp off the hooked SwgCuiToolbar). All the HUD
# owes us is our own context page, which carries the glyph template the module clones.
chk('<include>ui_ground_hud_toolbar.inc</include>' in g,
    "ui_ground_hud.inc keeps the stock toolbar include: the module works off that page")
chk('<include>ui_consoleport_context.inc</include>' in g,
    "ui_ground_hud.inc includes our context page (cpDebug + cpGlyphTemplate)")
# There is no toolbar markup of ours any more: the crossbar cells, the glyphs and the
# cluster frames are created at runtime inside the stock toolbar page, and what they
# have to look like is checked by the module's own tests (tests/test_crossbar.cpp).
# The structure of the files we do ship is checked by validate_ui.py.

print("\n".join("  OK   " + m for m in ok))
if bad: print("\n".join("  FAIL " + m for m in bad))
print("\n%d checks passed, %d failed" % (len(ok), len(bad)))
sys.exit(1 if bad else 0)
