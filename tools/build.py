# -*- coding: utf-8 -*-
"""Building the input files: inputnames.iff, the command table, the input maps.

    python build.py

The layout is built for every device profile out of layout.PROFILES and installed
under the stock preset names (the name list is baked into the exe):
  ds4   -> groundinputmap_swg.iff
  steam -> groundinputmap_swg2.iff
The player picks a preset in Options > Controls: "SWG" without Steam, "SWG 2" through
Steam Input. The slot model is the module's one (main k, L2 8+k, R2 16+k) and the
fallback trigger indices 30/31 always go in.
"""
import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from iffio import form, chunk, walk
import cmdrec2, cmdtable, imap, layout as L

ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'stock')
DIST = os.path.join(ROOT, 'client-files')
VER = '0.1.0'


def rd(p): return open(p, 'rb').read()


# ---------- 1. inputnames.iff : the JOYB input names -> glyphs ----------
def build_inputnames():
    b = rd(os.path.join(SRC, 'input', 'inputnames.iff'))
    groups = {}; order = []

    def cb(path, tag, data):
        k = tuple(path)
        if k not in groups: groups[k] = []; order.append(k)
        groups[k].append((tag, data))
    walk(b, 0, len(b), [], cb)

    def emit_codes(items, rename=None):
        out = b''
        for tag, d in items:
            if tag.strip() == 'CODE' and rename:
                code = struct.unpack_from('<I', d)[0]
                if code in rename:
                    d = struct.pack('<I', code) + rename[code].encode() + b'\0'
            out += chunk(tag.strip(), d)
        # The codes 30/31 already exist in the stock list (JOYBUTTON31/32, 70 in all),
        # so we only rename - nothing is added.
        return out
    inner = b''
    for dev in ('KEYS', 'MBTS', 'JOYA', 'JOYB', 'JOYH', 'JOYS'):
        k = ('INMS', '0000', dev)
        if k in groups:
            inner += form(dev, emit_codes(groups[k], L.GLYPH if dev == 'JOYB' else None))
    eq = b''
    k = ('INMS', 'EQIV', 'KEYS')
    if k in groups: eq = form('KEYS', emit_codes(groups[k]))
    return form('INMS', form('0000', inner) + form('EQIV', eq))


# ---------- 2. the command table: our own axes into the custom category ----------
def build_cmds():
    b = rd(os.path.join(SRC, 'input', 'groundinputmap_cmds.iff'))
    recs = [cmdrec2.make(name, types=cmdrec2.T_AXIS, press=(msg, mult, b''))
            for name, msg, mult in L.AXIS_CMDS]
    recs.append(cmdrec2.make(L.M3_CMD, types=cmdrec2.T_PRESSABLE,
                             press=(cmdrec2.MSG_EXEC, 0, L.M3_PRESS.encode('latin-1')),
                             release=(cmdrec2.MSG_EXEC, 0, L.M3_RELEASE.encode('latin-1'))))
    # holding a modifier switches the pane = the crossbar set
    for name, (on, off) in L.PANE_HOLD.items():
        recs.append(cmdrec2.make(name, types=cmdrec2.T_PRESSABLE,
                                 press=(cmdrec2.MSG_EXEC, 0, L.pane_action(on).encode('latin-1')),
                                 release=(cmdrec2.MSG_EXEC, 0, L.pane_action(off).encode('latin-1'))))
    return cmdtable.add_commands(b, recs, 'custom')


# ---------- 3. the input map ----------
def parse_base(path):
    base = imap.decompile(path)
    shifts = []; order = []; layers = {}; cur = None
    for line in base.splitlines():
        w = line.split()
        if not w or w[0] in ('version', 'cmds'): continue
        if w[0] == 'shift': shifts.append(line); continue
        if w[0] == 'layer':
            cur = int(w[1], 0); order.append((cur, int(w[2]))); layers[cur] = []; continue
        layers[cur].append(line)
    return shifts, order, layers


def build_inputmap(profile):
    """The slot layout of the module's model: a position has one slot, 0..9, and the
    set is the toolbar pane a held trigger switches to."""
    prof = L.PROFILES[profile]; joy = prof['joy']
    slot_of = L.hook_slot_of
    shifts, order, layers = parse_base(os.path.join(SRC, 'input', 'groundinputmap_swg_modern.iff'))
    base0 = layers[0x00]

    # the keyboard hotkeys 1..8 -> each button's main, 9 0 - = -> the D-pad R2
    slot_keys = {}
    for l in base0:
        w = l.split()
        if len(w) == 3 and w[0] == 'KEYS' and w[2].startswith('CMD_uiToolbarSlot'):
            slot_keys[int(w[1])] = w[2]
    keys_sorted = sorted(slot_keys)                       # scancodes 2..13
    target = [slot_of(b) for b in L.COLUMNS] + [slot_of(b, 'R2') for b in L.COLUMNS[:4]]
    key_retarget = {k: "CMD_uiToolbarSlot%02d" % t for k, t in zip(keys_sorted, target)}

    def stock_lines(layer):
        out = []
        for l in layers[layer]:
            w = l.split()
            if len(w) == 3 and w[0] == 'KEYS' and layer == 0x00 and int(w[1]) in key_retarget:
                out.append("  KEYS %3d %s" % (int(w[1]), key_retarget[int(w[1])])); continue
            out.append(l)
        return out

    movement = [l for l in base0 if len(l.split()) == 3 and l.split()[2] in L.MOVEMENT_CMDS]

    # the trigger indices in this profile: pad buttons, or keys only
    trig = {m: joy.get(m) for m in ('L2', 'R2')} if prof['triggers_as_buttons'] else {'L2': None, 'R2': None}
    # We give the synthetic trigger buttons 30/31 to BOTH profiles. The "ds4 only"
    # restriction was right while a Steam pad's triggers arrived on one glued axis;
    # since 10 Sep 2026 the module takes them from XInput independently, and on the
    # swg2 preset the crossbar pages simply did not switch without these bindings
    # (in the steam preset only the keyboard duplicates were left of them).
    trig_hook = L.TRIGGER_JOYB['hook']
    busy = {v for v in trig.values() if v is not None}

    def crossbar(layer, mod):
        r = []
        # A position has ONE slot, the same in every layer: the set is defined by the
        # pane, which a held modifier switches.
        for button in L.HOOK_POSITIONS:
            if button in L.COL_INPUT:
                dev, idx = L.COL_INPUT[button]
                if dev == 'JOYB': idx = joy[idx]
            else:                                            # L3/R3 - pad buttons by name
                dev, idx = 'JOYB', joy.get(button)
                if idx is None:
                    continue
            sl = slot_of(button, mod)
            r.append("  %-4s %3d CMD_uiToolbarSlot%02d" % (dev, idx, sl))
            if dev == 'KEYS':                                # the D-pad out of the POV (the module, JOYB 26..29)
                r.append("  JOYB %3d CMD_uiToolbarSlot%02d" % (L.DPAD_JOYB[button], sl))
        for name, cmd in L.UTIL.get(layer, []):
            if name in L.HOOK_POSITIONS:
                continue                                     # taken by the crossbar slots
            j = joy.get(name)
            if j is None or j in busy:
                continue
            r.append("  JOYB %3d %s" % (j, cmd))
        # No axis bindings at all: movement goes on buttons 18..21 and the camera on
        # 22..25, both lists synthesized by the module.
        for jb, cmd in enumerate(('CMD_walk', 'CMD_down', 'CMD_left', 'CMD_right'), 18):
            r.append("  JOYB %3d %s" % (jb, cmd))
        for jb, cmd in L.CAMERA_JOYB:
            r.append("  JOYB %3d %s" % (jb, cmd))
        return r

    t = ['version 0006', 'cmds input/groundinputmap_cmds.iff']
    t += shifts
    # L1 NO LONGER WORKS as a modifier: the module took the button (see UTIL).
    for m in ('L2', 'R2'):
        if trig[m] is not None:
            t.append("shift JOYB %-4d 0x%02X 0" % (trig[m], L.MOD_BITS[m]))
        if m in trig_hook:
            t.append("shift JOYB %-4d 0x%02X 0" % (trig_hook[m], L.MOD_BITS[m]))
        t.append("shift KEY  0x%02X 0x%02X 0" % (L.KEY[m], L.MOD_BITS[m]))

    mod_of_layer = {L.LAYER_OF_MOD[m]: m for m in L.CLUSTER}
    def pane_holds():
        """A trigger is a modifier (SHFT) and a binding at once: the press takes you to
        its own pane and the release brings you back. That a modifier can carry a
        binding was confirmed in game on M3, 6 Sep 2026."""
        r = []
        for m, cmd in (('L2', 'CMD_cpPane1Hold'), ('R2', 'CMD_cpPane2Hold')):
            for idx in (trig.get(m), trig_hook.get(m)):
                if idx is not None: r.append("  JOYB %3d %s" % (idx, cmd))
            r.append("  KEYS %3d %s" % (L.KEY[m], cmd))
        return r

    for st, flag in order:
        t.append("layer 0x%02X %d" % (st, flag))
        t += stock_lines(st)
        if st == 0x00:
            t += crossbar(0x00, None)
            t += pane_holds()
    for st in (L.MOD_BITS['L2'], L.MOD_BITS['R2']):
        t.append("layer 0x%02X 0" % st)
        t += movement
        t += crossbar(st, mod_of_layer.get(st))
        if st in (L.MOD_BITS['L2'], L.MOD_BITS['R2']):
            other = 'R2' if st == L.MOD_BITS['L2'] else 'L2'
            # The second trigger on top of the first takes you to the L2+R2 set pane,
            # and the release returns to the first modifier's pane.
            cmd = 'CMD_cpPane3Hold'
            for idx in (trig.get(other), trig_hook.get(other)):
                if idx is not None: t.append("  JOYB %3d %s" % (idx, cmd))
            t.append("  KEYS %3d %s" % (L.KEY[other], cmd))
    t.append("layer 0x%02X 0" % L.M3_LAYER)
    t += movement
    # L2+R2 is a set of its own
    t += crossbar(L.M3_LAYER, 'L2R2')
    txt = chr(10).join(t) + chr(10)
    return imap.compile_(txt), txt


if __name__ == '__main__':
    os.makedirs(os.path.join(DIST, 'input'), exist_ok=True)
    open(os.path.join(DIST, 'input', 'inputnames.iff'), 'wb').write(build_inputnames())
    open(os.path.join(DIST, 'input', 'groundinputmap_cmds.iff'), 'wb').write(build_cmds())
    for stale in ('consoleport_cmds.iff', 'VARIANT', 'groundinputmap_swg_modern.iff'):
        for p in (os.path.join(DIST, 'input', stale), os.path.join(DIST, stale)):
            if os.path.exists(p): os.remove(p)
    for profile, prof in L.PROFILES.items():
        iff, txt = build_inputmap(profile)
        for nm in prof['presets']:
            open(os.path.join(DIST, 'input', nm), 'wb').write(iff)
        open(os.path.join(HERE, 'consoleport.%s.imap.txt' % profile), 'w', encoding='utf-8').write(txt)
        print('profile %-6s -> %s' % (profile, ', '.join(prof['presets'])))
    for f in sorted(os.listdir(os.path.join(DIST, 'input'))):
        p = os.path.join(DIST, 'input', f); print("  %-34s %7d B" % (f, os.path.getsize(p)))
