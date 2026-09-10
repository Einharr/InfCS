# The SWG inputmap (IMAP) - the binding format

Taken apart on the Infinity client (`E:\Games\SWG\Live`, `swgemu.exe`).
The files were extracted from `mtg_patch_011_files_01.tre` and `mtg_patch_017.tre`.

## The IFF tree

    FORM IMAP
      FORM 0006
        FORM CMDS
          NAME  cstring            the path to the command table (*_cmds.iff)
        FORM SHFT                  the modifier declaration
          FORM KEY | MOSB | JOYB
            DATA  u32 code, u32 shiftBit, u8 flag
        FORM MAP                   ONE modifier layer, repeated
          INFO  u32 shiftState, u8 flag
          FORM KEYS | MOSB | JOYB | JOYX | JOYS | POV
            DATA  u32 index, cstring commandName

The chunk sizes are big-endian. There is NO alignment to even boundaries
(unlike classic EA IFF).

## What is confirmed in the exe

The `SHFT` parser is one function, three tags in a row, all optional:

    6b0a0f  push 1; push 1; push 'KEY '; call enterForm
    6b0a43  push 1; push 1; push 'MOSB'; call enterForm
    6b0a7a  push 1; push 1; push 'JOYB'; call enterForm

=> a joystick button can be declared a modifier.

The `MAP` parser is one function, six tags in a row, all optional:

    6b1348 KEYS   6b139f MOSB   6b13f7 JOYB
    6b144f JOYX   6b14a7 JOYS   6b14ff POV

=> the ground and space maps are loaded by the SAME code. A ground map
may contain JOYX/JOYS/POV - the parser will accept them.

## The command table (*_cmds.iff)

    FORM CMDS / FORM 0006 / FORM CATE
      NAME  cstring              the category
      CMD   cstring name, u32 type, <payload>

`type` is the kind of input the command accepts:

    2  POV (a hat)
    3  discrete (a button)
    4  an analog axis
    8  a slider

The distribution:

    groundinputmap_cmds.iff   169 commands   ALL type 3
    spaceinputmap_cmds.iff    235 commands   227x3, 5x4, 2x8, 1x2
    ui_inputmap_cmds.iff       32 commands    30x3, 2x4

Type 4 in space: CMD_yaw, CMD_pitch, CMD_roll,
CMD_shipThrottleSetAxis, CMD_shipThrottleDeltaAxis
Type 4 in ui:    CuiM_JoyAxisX, CuiM_JoyAxisY

## The input dictionary (input/inputnames.iff)

    KEYS  150 keys (DIK codes)
    MBTS    6  MOUSEBUTTON1..6
    JOYA    6  JOYAXIS1..6
    JOYB   70  JOYBUTTON1..70
    JOYH    5  JOYHAT1..5
    JOYS    3  JOYSLIDER1..3

## The layers in the shipped set

ground (swg/mmorpg2/...): the modifiers come from the keyboard only -
Alt=0x01 (LAlt 0x38 / RAlt 0xB8), Ctrl=0x02 (0x1D/0x9D),
Shift=0x04 (0x2A/0x36). The layers: 0x00, 0x02, 0x04, 0x06
(swg_modern adds 0x03). JOYB is EMPTY everywhere.

ui_inputmap: besides keys it declares THE MOUSE as modifiers -
MOSB0=0x10, MOSB1=0x40, MOSB2=0x20. The layers: 0x00 0x02 0x06
0x04 0x10 0x20 0x40 0x01. That is, 7 bits of shiftState are
really in use.

## The tools

    tre.py    ls|get           unpacking a .tre (EERT/0005)
    imap.py   d|c|test         IMAP .iff <-> text, round-trip
    cmds.py                    decoding *_cmds.iff
    tagscan.py                 searching the exe for tag constants

`imap.py test` over all 14 maps: 14/14 byte for byte.

The imap.py text format:

    version 0006
    cmds input/groundinputmap_cmds.iff
    shift KEY  0x38 0x01 0
    shift JOYB 4    0x08 0
    layer 0x00 0
      KEYS  200 CMD_walk
      JOYB    0 CMD_jump
      JOYB    - 
    layer 0x08 0
      JOYB    0 CMD_toolbarSlot00

---

# Part 2: the loose override and the command structure

## Loose folders override the .tre - confirmed

`TreeFile::install` @ 0x00a92800 reads `[SharedFile]`:
`searchPath_NN_M`, `searchTree_NN_M`, `searchTOC_NN_M` (NN = the priority,
0..maxSearchPriority, M = the index). The Infinity configs have NO
`searchPath` keys - but they are not needed either:

    00a929e7  eax = nodes.begin(); cmp against nodes.end()
    00a929f7  eax = nodes[0]->priority + 1        ; the vector is descending
    00a92a04  getKeyInt("SharedFile","searchAbsolute", default=eax)
    00a92a14  call addSearchAbsolute(priority)    ; UNCONDITIONALLY

The `searchAbsolute` node is always added and gets a priority above
any .tre. The insertion comparator @ 0x00a92b00 is a `setg` on field +4
(priority), that is, a descending sort, and the node ends up first.

`TreeFile::open` @ ~0x00a93260 walks the vector forward from begin and
stops at the first successful open:

    00a9326a  edi = [0x193c100]        ; begin
    00a9328c  ecx = [edi]              ; the node
    00a9329c  call [edx+0x14]          ; SearchNode::open(path)
    00a932a1  edi += 4                 ; forward
    00a932a6  je loop                  ; the first hit wins

=> any loose file in the client CWD overrides one of the same name in a .tre.
The CWD is the Live directory (see `Toolbar3: CWD=E:\Games\SWG\Live` in
infinity_qol.log). This is already used in practice: QoL writes
`ui/ui_ground_hud_toolbar.inc` and counts on exactly that.
`searchAbsolute` can be overridden with the `[SharedFile] searchAbsolute=N` key,
but not one Infinity config sets it.

## The structure of a CMD record

    cstring name
    u32  types                      a bit MASK: 1 button, 2 POV hat, 4 axis, 8 slider
    u8   userDefined
    u8   repeatStartDelay
    event press
    event repeat
    event release
    event reset

An event:

    i32  message
    f32  value
    cstring str

This is the layout of `load_0006` in the client's own `InputMap_Command.cpp`, and
it is what `cmdrec2.py` implements - the module's generators go through that file.
The mask matters: "type 3" on almost every ground command is button|POV, which is
why one command legitimately hangs on both a button and the D-pad, and an axis is
a bit of its own.

`message = 0x0144` means "run `str` as a slash command". The other messages are
hardcoded engine handlers (the camera, movement, ship weapons), and for those the
value carries the meaning instead of the string. For an axis the client queues
`axis position * value`, so `value` there is a multiplier.

Examples of press/release pairs in the shipped set:

    CMD_uiPointerToggle       /ui action pointerToggleDown
                              /ui action pointerToggleUp
    CMD_uiToggleObjectNames   /ui action toggleObjectNamesDown
                              /ui action toggleObjectNamesUp
    CMD_targetAtCursor        /ui action targetAtCursor
                              /ui action targetAtCursorStop

Checking the model: `validate.py` rebuilds our command table through `cmdrec2.py`
and asserts that every stock command came over **byte for byte**, with only the
axis commands, the pane holds and M3 added on top.

## What follows from this

A command table of our own can define NEW commands with message 0x0144 and an
arbitrary string in press and release. A gamepad button gets everything a player
can type: `/macro`, `/ui action ...`, any server command.

The toolbar panes are selected directly: `CMD_uiToolbarPane00..11`
(12 panes), the slots are `CMD_uiToolbarSlot00..23`.

Hence hold-to-reveal with not a single instruction in memory:

    press:   /ui action toolbarPane01
    release: /ui action toolbarPane00

plus the same button declared a modifier in SHFT/JOYB - you hold the
trigger, the toolbar visually switches to another pane AND the face buttons
move into another binding layer; you release it and it comes back.
