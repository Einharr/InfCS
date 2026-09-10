"""ConsolePort for SWG Infinity - the layout. The single source of truth: the module,
the input maps and the markup all read this file.

A SET IS A TOOLBAR PANE (7 Sep 2026): ten positions take slots 0..9 in every pane, and
a held modifier switches the pane - rest 0, L2 1, R2 2, L2+R2 3. Panes 4 and 5 are left
to the player. See HOOK_POSITIONS / PANE_HOLD below.

The DirectInput button indices were measured in game, two independent passes over the
toolbar pane indicator:

  0 Cross  1 Circle  2 Square  3 Triangle  4 L1  5 R1  6 Share  7 Options  8 L3  9 R3

L2 and R2 are not buttons there - the device reports them as the Rx and Ry axes
(JOYX 3 and 4). The module makes buttons of them (input/pad.cpp) by appending synthetic
JOYB 30/31 to the DirectInput stream, and the generated map declares BOTH numberings as
the modifiers: 6/7 as the device sends them without Steam, 30/31 as the module
synthesizes them. So the map behaves the same with or without the module loaded, and
the NUMPAD9/NUMPAD1 duplicates give a way in from the keyboard.

The D-pad arrives as a POV hat, and on the ground a POV gives one command for the whole
hat, so the directions are indistinguishable: without the module the D-pad columns run
on the keys F13/F15/NUMPAD7/F14 (any remapper).
"""

# ---------------------------------------------------------------- the inputs
# DS4/DualSense in DirectInput, read off the device with Steam CLOSED (harness/di.py,
# 5 Sep 2026). The earlier table - Cross 0, Circle 1, Square 2, and "L2/R2 are not
# buttons" - was taken with Steam running, and that was Steam Input substituting the pad
# profile. Without Steam L2/R2 are plain buttons 6 and 7. Steam gets its own preset.
JOY = dict(SQUARE=0, CROSS=1, CIRCLE=2, TRIANGLE=3, L1=4, R1=5, L2=6, R2=7,
           SHARE=8, OPTIONS=9, L3=10, R3=11, PS=12, PAD=13)
JOY['CREATE'] = JOY['SHARE']      # the former name of the same input

# What the client sees depends on who sits between it and the hardware:
#   ds4   - the pad directly, no Steam: 12+ buttons, triggers on 6/7.
#   steam - through Steam Input: a virtual Xbox pad (A=Cross 0 ... RS 9) with no trigger
#           buttons at all - L2/R2 and the D-pad arrive as KEYS. This is the table taken
#           in game on 5 Sep with Steam alive.
# Each profile is installed under its own stock preset name (the list is baked into the
# exe); the player picks the preset in Options > Controls.
PROFILES = {
    # Names checked against the BINARY, not ref/client-src: the reference has three
    # ground schemes, this binary has seven - swg, iso, mmo, fps, ja101, mmo2, swg2
    # (InputScheme::install, 0x006E0EF0). Trusting the reference on 10 Sep 2026 cost a
    # pointless profile swap. It also never loads _modern or _classic, which is why ds4
    # is the single swg.iff and steam is swg2.iff.
    'ds4':   dict(joy=JOY, triggers_as_buttons=True,
                  presets=['groundinputmap_swg.iff']),
    'steam': dict(joy=dict(CROSS=0, CIRCLE=1, SQUARE=2, TRIANGLE=3, L1=4, R1=5,
                           SHARE=6, OPTIONS=7, L3=8, R3=9, L2=None, R2=None, PS=None, PAD=None),
                  triggers_as_buttons=False,
                  presets=['groundinputmap_swg2.iff']),
}
PROFILES['steam']['joy']['CREATE'] = PROFILES['steam']['joy']['SHARE']

TRIGGER_JOYB = {                  # the module's synthetic trigger buttons
    'hook':  dict(L2=30, R2=31),
}

# JOYB names in inputnames.iff -> the short glyphs the stock labels show
GLYPH = {0: 'SQR', 1: 'CRS', 2: 'CIR', 3: 'TRI', 4: 'L1', 5: 'R1', 6: 'L2', 7: 'R2',
         8: 'CRE', 9: 'OPT', 10: 'L3', 11: 'R3', 12: 'PS', 13: 'PAD', 30: 'L2', 31: 'R2'}

# DirectInput scancodes free in every stock map. The R2 duplicate moved twice
# (7-8 Sep 2026): NUMPAD= (0x8D) is a scancode Windows does not know at all -
# MapVirtualKey gives 0 and it never reaches the client; F16 (103) the system knows, but
# no physical key produces it and DirectInput never sees it. Only real keys work, so R2
# ended up on NUMPAD1 - free everywhere and actually pressable.
KEY = dict(DPAD_UP=100, DPAD_DOWN=101, DPAD_LEFT=102, DPAD_RIGHT=71,
           L2=73, R2=79)
KEYNAME = {100: 'F13', 101: 'F14', 102: 'F15', 71: 'NUMPAD7',
           73: 'NUMPAD9', 79: 'NUMPAD1'}

# ---------------------------------------------------------------- the grid
# A cluster is a pad button with its two modifier variants [L2 | main | R2], like
# ConsolePort's main plus two flyouts. The cells themselves are no longer laid out by
# these lists - the module places them at runtime; what is left here is the ORDER, which
# the modifier layers and the crossbar positions below are built from.
CLUSTER = ['L2', None, 'R2']                  # the cell order inside a cluster
COLUMNS = ['DPAD_UP', 'DPAD_LEFT', 'DPAD_RIGHT', 'DPAD_DOWN',
           'TRIANGLE', 'SQUARE', 'CIRCLE', 'CROSS']   # the buttons in cluster order


# SHFT modifier bits in the input map; the stock ones are 0x01 Alt, 0x02 Ctrl, 0x04 Shift
MOD_BITS = {'L1': 0x08, 'L2': 0x10, 'R2': 0x20}
LAYER_OF_MOD = {m: (MOD_BITS[m] if m else 0x00) for m in CLUSTER}

# how a column is pressed: a pad button by name (index from the profile), or a key -
# the D-pad without the module, and everything through Steam Input
COL_INPUT = {b: ('JOYB', b) for b in ('TRIANGLE', 'SQUARE', 'CIRCLE', 'CROSS')}
COL_INPUT.update({b: ('KEYS', KEY[b]) for b in ('DPAD_UP', 'DPAD_LEFT', 'DPAD_RIGHT', 'DPAD_DOWN')})

# ---------------------------------------------------------------- the visuals
# the PS5 face colours, from ConsolePort's PlayStation5.lua
FACE_COLOR = {'CROSS': '#6882A1', 'CIRCLE': '#D84E58',
              'SQUARE': '#D35280', 'TRIANGLE': '#62BBB2'}
DPAD_COLOR = '#FFFFFF'
MOD_COLOR = '#BFD8E0'      # fallback shade when the theme cannot be read
# L2/R2 have no colour of their own the way the face buttons do, so the trigger glyphs
# follow the interface theme: the module paints them by palette entry (core/theme.h).
MOD_PALETTE = 'line1'

# ---------------------------------------------------------------- the service layer
# The buttons outside the crossbar. Where the triggers are the device's own buttons
# 6/7 (profile ds4), SHARE/OPTIONS collide with them and their commands are not bound -
# build.py filters by index.
# OPTIONS is not taken in the base layer - the module has it for the game menu radial.
#
# L1 and R1 are not bound at all and the L1 layer is gone: on 9 Sep 2026 the module took
# both for the target cycles and the group ring (world/targeting). A button cannot be a
# SHFT modifier and a hold button at once, or reaching for L1 plus something gives you a
# ring. The module needs no bindings here either - it calls the actions through
# CuiActionManager::performAction, and a binding would fire second on the same press.
#
# L3/R3 are crossbar positions (HOOK_POSITIONS), so these two only matter as the
# keyboard / no-module fallback.
UTIL = {
    0x00: [('SHARE',   'CMD_uiGameMenuActivate'),
           ('L3',      'CMD_autoRun'),
           ('R3',      'CMD_uiCycleTargetNext')],
}

# ---------------------------------------------------------------- the sticks
# Our own AXIS commands in the custom category, confirmed in game: the character walks
# with the deflection and stands still at rest. Messages from
# PlayerCreatureController::realAlter - 155 moveLongitudinal, 156 moveLateral, 136 turn.
# Both 155 and 156 add into the same component (a client bug), so sideways cannot be had
# separately and only forward and turn are bound. The value multiplies the deflection;
# its sign gives the direction.
AXIS_CMDS = [                      # (name, message, multiplier)
    ('CMD_cpMoveLong', 155, 1.0),
    ('CMD_cpMoveLat',  156, 1.0),
    ('CMD_cpTurn',     136, 1.0),
]
# JOYX is the axis index in IOMT_* order: 0 X, 1 Y, 2 Z, 3 Rx, 4 Ry, 5 Rz.
#
# The axis layout is not ours but SDL's (SDL_GameControllerDB, entry
# 030000004c050000e60c000000000000 "PS5 Controller"):
#     leftx:a0  lefty:a1         -> X, Y   left stick
#     rightx:a2 righty:a5        -> Z, Rz  right stick
#     lefttrigger:a3 righttrigger:a4 -> Rx, Ry  triggers
# Every Windows entry there has bus 03 - USB and Bluetooth are not distinguished,
# because the HID stack presents the pad the same way.
#
# A live measurement on 9 Sep 2026 said otherwise (both triggers on Z, the right stick
# on Rx/Ry) and nearly earned a rewrite. That was the signature of the broken extended
# mode 0x31 Steam drives the pad into - see harness/hidraw.py. Fix the mode; do not fit
# the layout to a breakage.

# The right stick as the module synthesizes it (JOYB 22..25). The client has no axial
# camera command - the markup only has cameraYaw*/cameraPitch*, which are press/release
# (NUMPAD4/6, NUMPAD8/2 in game). So the right stick goes here and CMD_cpTurn comes off
# the Z axis: the owner asked for camera on the right stick (9 Sep 2026), and in a chase
# camera the character turns itself as it follows.
CAMERA_JOYB = [(22, 'CMD_cameraYawLeft'), (23, 'CMD_cameraYawRight'),
               (24, 'CMD_cameraPitchBackward'), (25, 'CMD_cameraPitchForward')]

# the movement keys every layer of ours has to repeat - layers do not inherit the base
# one, and the stock Ctrl layer repeats them too
MOVEMENT_CMDS = {'CMD_walk', 'CMD_down', 'CMD_turnLeft', 'CMD_turnRight',
                 'CMD_left', 'CMD_right', 'CMD_toggleRunMomentary',
                 'CMD_autoRun', 'CMD_jump'}

# ---------------------------------------------------------------- the crossbar model
# In the module the slot geometry is free, but the volume's child order is the draw
# order (first on top) and the children must not be rearranged - discoverToolbarSlot
# counts them in order.
#
# Since 7 Sep 2026 the flyouts no longer hang over main: at rest only the ten main cells
# show, two crosses and the L3/R3 block.
#
# A set is a PANE, not a slot range. The toolbar has 6 panes of 24 slots
# (DEFAULT_PANE_COUNT / DEFAULT_ITEM_COUNT_PER_PANE), and 24 is a storage limit as much
# as an execution one - a ToolbarItemPane is a vector of exactly 24 CuiDragInfo. Forty
# assignments do not fit one layered range; across panes they fit easily.
#
#     the positions occupy slots 0..9 in EVERY pane
#     rest -> pane 0, L2 -> 1, R2 -> 2, L2+R2 -> 3; panes 4 and 5 stay the player's
#
# The stock CMD_uiToolbarSlot00..09 do the executing: the pane changes, not the slot.
HOOK_POSITIONS = ['DPAD_UP', 'DPAD_LEFT', 'DPAD_RIGHT', 'DPAD_DOWN',
                  'TRIANGLE', 'SQUARE', 'CIRCLE', 'CROSS', 'L3', 'R3']
HOOK_PANE = {None: 0, 'L2': 1, 'R2': 2, 'L2R2': 3}

# the D-pad as the module synthesizes it out of the POV (JOYB 26..29); the map binds the
# D-pad columns to both these and the key duplicates
DPAD_JOYB = dict(DPAD_UP=26, DPAD_DOWN=27, DPAD_LEFT=28, DPAD_RIGHT=29)


def hook_slot_of(button, mod=None):
    """A position's slot. Independent of the modifier - the pane carries that."""
    return HOOK_POSITIONS.index(button)


def hook_slot_count():
    return len(HOOK_POSITIONS)


# a cluster's flyout direction (ConsolePort's flyoutDirection): where the button faces
# from the centre of the diamond
FAN_DIR = {'DPAD_UP': 'UP', 'DPAD_LEFT': 'LEFT', 'DPAD_RIGHT': 'RIGHT', 'DPAD_DOWN': 'DOWN',
           'TRIANGLE': 'UP', 'SQUARE': 'LEFT', 'CIRCLE': 'RIGHT', 'CROSS': 'DOWN',
           # L3/R3 sit in the block between the crosses, so their flyouts go outwards,
           # middle cell level with its own slot
           'L3': 'LEFT', 'R3': 'RIGHT'}

# M3 = L2+R2. A pane has no room for a third flyout, so holding both makes the next pane
# active and the faces hit its slots 0..7. One command with press/release: the second
# trigger pages forward, its release pages back (the InputMap remembers which command
# got the press and sends it the release).
M3_CMD = 'CMD_cpPaneHold'
M3_PRESS, M3_RELEASE = '/ui action toolbarPaneNext', '/ui action toolbarPanePrev'
M3_LAYER = MOD_BITS['L2'] | MOD_BITS['R2']

# Holding a modifier switches the PANE for the duration, and the pane IS the set (see
# HOOK_PANE). toolbarPane00..05 are stock client actions. A release steps one level down,
# so letting go of R2 with L2 still held returns to the L2 set, not to rest.
#   command -> (pane on press, pane on release)
PANE_HOLD = {
    'CMD_cpPane1Hold': (1, 0),      # L2
    'CMD_cpPane2Hold': (2, 0),      # R2
    'CMD_cpPane3Hold': (3, 1),      # R2 on top of a held L2 -> the L2+R2 set
}


def pane_action(n):
    return '/ui action toolbarPane%02d' % n
