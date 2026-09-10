# -*- coding: utf-8 -*-
"""The ABI table of swgemu.exe (stage.119798), and the module header it generates.

Everything here came off the Infinity binary with tools/abigen.py - RTTI to vtable,
strings to xref, a disassembler - and was confirmed. The header carries the 8-byte
prologue signature the module checks at startup; the "evidence" column says what proves
each row.

    python abitable.py            # -> src/consoleport/abi/addresses.h, abi.json, docs/03-abi.md
"""
import os, sys, struct, json, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import abigen as A

FUNCS = [
    # name,                          address,    convention, evidence
    ('UIManager_gUIManager',        0x0087FF60, 'cdecl',    'warpCursor calls it before ProcessMessage; reads [0x1996E98], creates on zero'),
    ('UIManager_ProcessMessage',    0x010E77C0, 'thiscall', '10 calls from CuiIoWin::processEvent with a UIMessage on the stack'),
    ('UIManager_GetObjectFromPath', 0x010EA7E0, 'thiscall', 'ecx=[this+4] (root), two arguments (path, type)'),
    ('UIManager_GetFocusedLeaf',    0x010EA810, 'thiscall', 'ecx=[this+4]; jmp [vtbl+0xBC] = slot 47 GetFocusedLeafWidget'),
    ('UIManager_PushContextWidget', 0x010EA050, 'thiscall', 'creates a 0x128 UIPage in [this+0xD8], 21 calls'),
    ('UIManager_PopContextWidgets', 0x010EA220, 'thiscall', 'guard 0x1996E9C, [this+0xD8]'),
    ('UIManager_AbortDrag',         0x010EA8F0, 'thiscall', 'builds a UIMessage Type 27 (DragCancel) for [this+0x3C]/[this+0x40]'),
    ('UIBaseObject_GetObjectFromPath', 0x010F4FA0, 'thiscall', 'taken earlier from SwgCuiDebugInfoPage (getCodeDataObject)'),
    ('UIWidget_SetLocation',        0x01106160, 'thiscall', 'SetRect calls it with (x, y, 0); writes [this+0x2C]/[this+0x30]'),
    ('UIText_SetLocalText',         0x0110F580, 'thiscall', 'QoL NumberCommas hook'),
    ('UIMessage_ctor',              0x01124BF0, 'thiscall', 'zeroes 0x28 bytes; 21 calls, warpCursor among them'),
    ('CuiIoWin_warpCursor',         0x0093D1F0, 'thiscall', 'gotoXY([this+0x10]) + UIMessage(MouseMove=18) + ProcessMessage'),
    # Switching the input scheme, what Options > Controls does. Identified by its own
    # warnings - "resetFromType with no input map", "resetFromType [%s] invalid" - and by
    # the call to InputScheme::install (0x006E0EF0). This binary has seven ground types
    # (swg, iso, mmo, fps, ja101, mmo2, swg2), not the reference's three.
    ('InputScheme_resetFromType', 0x006E1B40, 'cdecl', '(const std::string& type, bool confirmed); its own WARNING strings inside'),
    ('CuiActionManager_performAction', 0x008CC460, 'cdecl', '(const std::string& id, const Unicode::String& params): map[index]/map[-1] -> vtbl[1]'),
    ('CuiActionManager_addAction',  0x008CC650, 'cdecl',    'QoL Toolbar3 hook (action registration)'),
    ('CuiMediator_getCodeDataObject', 0x00A68210, 'cdecl',  'taken earlier'),
    ('runGameLoopOnce',             0x004237C0, 'cdecl',    'QoL CrashGuard hook (the frame)'),
    ('Game_getGameInputMap',        0x00425B80, 'cdecl',    'SwgCuiToolbar::updateKeyBindings (0xF696D0) calls it before "No inputmap"'),
    ('CuiInputNames_getInputValueString', 0x009EB770, 'cdecl', '(InputMap*, std::string* cmd, Unicode::String* out) - the call at 0xF697FD with add esp,0xC'),
    ('Stlp_allocate',               0x012EA770, 'cdecl',    'node_alloc::allocate(bytes): updateKeyBindings pushes 0x10 for an empty Unicode::String'),
    ('Stlp_deallocate',             0x00424B00, 'thiscall', 'alloc_proxy::deallocate(p, nchars): the end of updateKeyBindings; over 0x80 bytes goes to 0xAC1640'),
    ('UIText_SetPreLocalized',      0x01112660, 'thiscall', '(bool): updateKeyBindings pushes 1 before SetLocalText'),
    ('UIVolumePage_Pack',           0x0113C6C0, 'thiscall', 'UIVolumePage vtable slot 59; does not check DoNotPackChildren (only the +0x154 guard), forces CellSize onto the children'),
    ('SwgCuiToolbar_getToolbarItem', 0x00F67890, 'thiscall', 'xref WARNING "the pane request (%d) for the toolbar was > data vector size"; ret 8 -> (int pane, int slot), returns a CuiDragInfo*'),
    ('SwgCuiToolbar_setToolbarItem', 0x00F667C0, 'thiscall', 'the only caller of getToolbarItem with pane<0 and slot<0 checks and an inline operator= for CuiDragInfo; ret 0xC -> (int pane, int slot, const CuiDragInfo*)'),
    # --- targeting, traced 9 Sep 2026 from the string "cycleTargetOutward":
    # literal 0x186E454 -> the std::string global 0x1929C50 initializer (the
    # CuiCombatManager copy) -> install 0x8C74A0 (addAction) and handler 0x8C79D0 ->
    # cycleTargetsOutward 0x8C8A10 -> cycleTargetsForPlayer 0x8C7340 (predicate,
    # direction). In this build cycleTargetsForPlayer takes TWO arguments and has no
    # intendedTarget branch at all - the string is not even in the exe. The ground
    # target is lookAtTarget, player field +0x598.
    ('Game_getPlayerCreature',      0x00425200, 'cdecl',    'the first call in cycleTargetsForPlayer (0x8C7395) and in isTargetCycleOk (0x8C8AB8); reads global 0x190885C and runs it through a type check'),
    ('ClientWorld_findObjectsInRange', 0x00561980, 'cdecl',  '(Vector const* pos, float range, ObjectVector* out): the call at 0x8C73F5 with add esp,0xC; reads [pos],[pos+4],[pos+8] and a float'),
    ('CuiCombatManager_targetIsAttackable', 0x008C72F0, 'cdecl', 'the predicate cycleTargetsOutward (0x8C8A10) passes into cycleTargetsForPlayer; asClientObject vtbl+0x40, asTangibleObject vtbl+0x6C, isTargetCycleOk 0x8C8AB0, isAttackable 0x640220; ret with no number'),
    ('CuiCombatManager_isTargetCycleOk', 0x008C8AB0, 'cdecl', 'called from targetIsAttackable; takes getPlayerCreature itself and compares against [player+0x598]; contains a camera frustum test'),
    ('TangibleObject_isAttackable', 0x00640220, 'thiscall', 'the second call in targetIsAttackable, ecx = tangible'),
    ('CreatureObject_setLookAtTarget', 0x00434AB0, 'thiscall', 'the tail of cycleTargetsForPlayer: ecx=player, the argument is the NetworkId* from getNetworkId; a mouse click makes the same call (SwgCuiHud::targetAtCursor)'),
    ('ClientObject_getLocalizedName', 0x00556EC0, 'thiscall', '131 calls; cmp byte [this+0x184] (dirty) -> call 0x556000 (update) -> lea eax,[this+0x178] (Unicode::String)'),
    ('Object_getNetworkId',         0x00B23C60, 'thiscall', 'lea eax,[ecx+0x20]; ret - the NetworkId field'),
    ('Object_getTransform',         0x00B22C80, 'thiscall', 'the call at 0x8C73BE before the position is read; lazy recompute driven by [this+0x34] and the [this+0xC]&0x20 flag; a 3x4 matrix in row order'),
    ('CachedNetworkId_getObject',   0x00B30160, 'thiscall', 'the call at 0x8C73FF in cycleTargetsForPlayer and 0x8C8849 in cycleTargetsGroup: ecx = &CachedNetworkId, returns an Object*'),
    ('ConfigClientGame_getTargetingRange', 0x005034F0, 'cdecl', 'call 0x8C73D5 in cycleTargetsForPlayer; picks the ground or space radius itself from isSpace (0x426170)'),
    ('Stlp_free_big',               0x00AC1640, 'cdecl',    'the vector release branch in cycleTargetsForPlayer for a capacity over 0x80 bytes (0x8C745D)'),
    ('Stlp_free_small',             0x012EA920, 'cdecl',    '(ptr, bytes): the second branch of the same release (0x8C747B)'),
]
GLOBALS = [
    ('UIManager_gSingleton',    0x01996E98, 'UIManager*; CuiManager::remove deletes through vtbl[1]'),
    ('UIManager_gIsPopping',    0x01996E9C, 'the guard in PopContextWidgets'),
    ('CuiManager_ms_theIoWin',  0x0192613C, 'a store after the CuiIoWin constructor (0x93ADB0, size 0x68)'),
    ('UIMessage_key_BackSpace', 0x018D98E0, 'word'), ('UIMessage_key_Insert', 0x018D98E2, 'word'),
    ('UIMessage_key_Delete',    0x018D98E4, 'word'), ('UIMessage_key_LeftArrow', 0x018D98E6, 'word'),
    ('UIMessage_key_RightArrow', 0x018D98E8, 'word'), ('UIMessage_key_UpArrow', 0x018D98EA, 'word'),
    ('UIMessage_key_DownArrow', 0x018D98EC, 'word'), ('UIMessage_key_Home', 0x018D98EE, 'word'),
    ('UIMessage_key_End',       0x018D98F0, 'word'), ('UIMessage_key_PageUp', 0x018D98F2, 'word'),
    ('UIMessage_key_PageDown',  0x018D98F4, 'word'), ('UIMessage_key_Tab', 0x018D98F6, 'word'),
    ('UIMessage_key_Space',     0x018D98F8, 'word'), ('UIMessage_key_Enter', 0x018D98FA, 'word'),
    ('UIMessage_key_Escape',    0x018D98FC, 'word'),
    # CuiDragInfo properties (UILowerString), off the initializer 0x009D23F0 on
    # 9 Sep 2026 - the "push string / mov ecx, global" pairs run one after another there.
    # Needed to read a command off a browser row and assemble a pane item: CuiDragInfo::set
    # is inlined and has no address of its own.
    ('PROP_CmdName',          0x019355B8, '"CmdName" -> CuiDragInfo::cmd'),
    ('PROP_DragInfoName',     0x019355B4, '"DragInfoName" -> CuiDragInfo::name'),
    ('PROP_CmdStr',           0x019355B0, '"CmdStr" -> CuiDragInfo::str, on a live item this holds "/ache"'),
    ('PROP_DragCommandValue', 0x019355AC, '"DragCommandValue" -> CuiDragInfo::commandValue'),
]
VTABLES = ['UIWidget', 'UIPage', 'UIVolumePage', 'UIButton', 'UIImage', 'UIText', 'UIManager',
           'UIPopupMenu', 'UIRadialMenu', 'UITabbedPane', 'UIList', 'UICursor', 'CuiIoWin', 'CuiMediator',
           'CuiWorkspace', 'SwgCuiToolbar', 'SwgCuiInventory', 'SwgCuiInventoryContainerIcons']
# CuiMediator slots, counted by CuiMediator.h: maximize, restore, close, test, update,
# saveSettings. Verified with a disassembler on 9 Sep 2026 - slot 3 is the debug stub
# (xor al,al / ret 4), slot 4 takes an argument (update(float)), slot 5 takes none and
# has a big frame, so it is saveSettings. The client's own pane save after a drop is
# setToolbarItem(pane, slot, item); saveSettings();
MEDIATOR_SLOTS = [('CuiMediator_saveSettings', 5)]

SLOTS = [
    ('IsA', 0), ('GetTypeName', 1), ('Clone', 2), ('Destroy', 3), ('Attach', 4), ('Detach', 5),
    ('GetPropertyNames', 6), ('GetLinkPropertyNames', 7), ('RemoveProperty_c', 8), ('RemoveProperty', 9),
    ('SetProperty_c', 10), ('SetProperty', 11), ('GetProperty_c', 12), ('GetProperty', 13),
    ('ResetLocalizedStrings', 14), ('CopyPropertiesFrom', 15), ('AddChild', 16), ('RemoveChild', 17),
    ('SelectChild', 18), ('GetChild', 19), ('GetChildren', 20), ('GetChildCount', 21), ('RemoveFromParent', 22),
    ('MinimizeResources', 23), ('CanChildMove', 24), ('MoveChild', 25), ('Link', 26), ('DuplicateObject', 27),
    ('dtor', 28), ('SetRect', 29), ('SetSize', 30), ('SetWidth', 31), ('SetHeight', 32), ('SetScrollLocation', 33),
    ('SetScrollExtent', 34), ('GetScrollExtent', 35), ('GetScrollSizes', 36), ('GetMouseCursor_c', 37),
    ('GetMouseCursor', 38), ('GetLocalTooltip', 39), ('GetWidgetFromPoint', 40), ('CanSelect', 41),
    ('SetSelected', 42), ('SetSelectable', 43), ('SetTabRoot', 44), ('SetVisible', 45), ('SetUnderMouse', 46),
    ('GetFocusedLeafWidget', 47), ('WantsMessage', 48), ('ProcessMessage', 49), ('ProcessChildNotificationMessage', 50),
    ('GetStyle', 51), ('Render', 52), ('IsDropOk', 53), ('GetCustomDragWidget', 54), ('OnSizeChanged', 55),
    ('OnLocationChanged', 56),
    ('Page_InsertChildBefore', 57), ('Page_InsertChildAfter', 58), ('Page_Pack', 59), ('Page_slot60', 60),
]
OFFSETS = [
    # The group: cycleTargetsGroup (0x8C87F0) reads [player+0x7B8] as a CachedNetworkId,
    # gets the GroupObject and walks [group+0x2B8]..[group+0x2BC], stride 0x18. Each
    # element is a pair<NetworkId, Unicode::String>: id at +0 (8 bytes), name at +8.
    ('CreatureObject_group', 0x7B8, 'CachedNetworkId; lea ecx,[esi+0x7B8] before getObject'),
    ('GroupObject_membersBegin', 0x2B8, 'std::vector<pair<NetworkId,Unicode::String>>::begin'),
    ('GroupObject_membersEnd', 0x2BC, 'the same vector: end; element stride 0x18, the name at +8'),
    ('GroupMember_stride', 0x18, 'add esi,0x18 in the loop over group members'),
    ('GroupMember_name', 0x08, 'a Unicode::String right after the NetworkId in the pair'),
    # pvpFlags comes from virtual slot 40: on TangibleObject that is mov eax,[ecx+0x3A0],
    # but CreatureObject overrides it, so it is read through the vtable, not the field.
    ('TangibleObject_pvpFlags_slot', 40, 'call [vtbl+0xA0] in isAttackable (0x640238)'),
    ('TangibleObject_condition', 0x38C, 'the 0x100 bit (invulnerable) check in isAttackable'),
    ('CreatureObject_lookAtTarget', 0x598, 'CachedNetworkId; lea ebx,[player+0x598] in cycleTargetsForPlayer, compared in isTargetCycleOk'),
    ('Object_transform_posX', 0x0C, 'Transform 3x4 in row order: position at 0x0C/0x1C/0x2C, forward vector at 0x08/0x18/0x28'),
    ('UIBaseObject_refcount', 0x04, 'word; Attach/Detach'), ('UIBaseObject_name', 0x08, 'std::string (MSVC7)'),
    ('UIBaseObject_parent', 0x14, 'RemoveFromParent'), ('UIWidget_location', 0x2C, 'SetLocation'),
    ('UIWidget_size', 0x34, 'SetWidth/SetHeight'), ('UIWidget_scrollLocation', 0x3C, 'SetScrollLocation'),
    ('UIWidget_scrollExtent', 0x44, 'GetScrollExtent'),
    ('UIWidget_flags', 0x7C, 'CanSelect: bits 0|1 visible, 2 enabled, 8 getsInput'),
    ('UIPage_children', 0x104, 'std::list<UIBaseObject*> (ResetLocalizedStrings/Pack)'),
    ('UIPage_pageFlags', 0x120, 'Pack: bit 2 = DoNotPackChildren'),
    ('UIManager_rootPage', 0x04, 'GetObjectFromPath/GetFocusedLeaf'), ('UIManager_contextPage', 0xD8, 'PushContextWidget'),
    # AbortDrag reads [this+0x3C] and [this+0x40] and sends message 0x1B; a non-zero
    # 0x40 means an icon is being dragged right now
    ('UIManager_dragObject', 0x40, 'AbortDrag: non-zero means a drag is in progress'),
    ('CuiIoWin_mouseCursor', 0x10, 'warpCursor'),
    ('SwgCuiToolbar_volumePage', 0x94, 'ctor 0xF64AE0: getCodeDataObject(0x27, [esi+0x94], "volumePage")'),
    ('SwgCuiToolbar_volumeKeyBindings', 0xE4, 'updateKeyBindings: mov ecx,[ecx+0xE4]; GetChildrenRef'),
    ('UIMessage_Type', 0x00, ''), ('UIMessage_Modifiers', 0x04, '9 bytes'), ('UIMessage_Keystroke', 0x0E, 'word'),
    ('UIMessage_Data', 0x10, 'word'), ('UIMessage_MouseX', 0x14, ''), ('UIMessage_MouseY', 0x18, ''),
    ('UIMessage_DragSource', 0x1C, ''), ('UIMessage_DragObject', 0x20, ''), ('UIMessage_DragTarget', 0x24, ''),
    ('UIMessage_size', 0x28, 'the 0x1124BF0 constructor'),
]
MSGTYPES = [('KeyFirst', 0), ('KeyDown', 1), ('KeyUp', 2), ('KeyRepeat', 3), ('Character', 4), ('KeyLast', 5),
            ('MouseFirst', 6), ('LeftMouseDown', 7), ('MiddleMouseDown', 8), ('RightMouseDown', 9),
            ('MouseLastFocusChanger', 10), ('LeftMouseDoubleClick', 11), ('MiddleMouseDoubleClick', 12),
            ('RightMouseDoubleClick', 13), ('LeftMouseUp', 14), ('MiddleMouseUp', 15), ('RightMouseUp', 16),
            ('MouseLastButton', 17), ('MouseMove', 18), ('MouseEnter', 19), ('MouseExit', 20), ('MouseWheel', 21),
            ('ContextRequest', 22), ('MouseLast', 23), ('DragFirst', 24), ('DragStart', 25), ('DragEnd', 26),
            ('DragCancel', 27), ('DragOver', 28), ('DragLast', 29)]


def sig8(va):
    p = A.pe(); o = p.va2off(va)
    return p.b[o:o + 8]


def gen(out_dir, report_path):
    os.makedirs(out_dir, exist_ok=True)
    p = A.pe(); md5 = hashlib.md5(p.b).hexdigest()
    h = ['// Generated by tools/abitable.py from swgemu.exe md5 %s. DO NOT EDIT BY HAND.' % md5,
         '#pragma once', '#include <cstdint>', '', 'namespace cp { namespace abi {', '',
         'static const char EXE_MD5[] = "%s";' % md5, '',
         'struct FuncSig { const char* name; uint32_t va; unsigned char sig[8]; };',
         'static const FuncSig FUNCS[] = {']
    for name, va, cc, why in FUNCS:
        s = sig8(va)
        h.append('    {"%s", 0x%08X, {%s}},  // %s' % (name, va, ','.join('0x%02X' % c for c in s), cc))
    h.append('};')
    for name, va, cc, why in FUNCS:
        h.append('static const uint32_t %s = 0x%08X;' % (name, va))
    h.append('')
    for name, va, why in GLOBALS:
        h.append('static const uint32_t %s = 0x%08X;  // %s' % (name, va, why))
    h.append('')
    vt = {}
    for cls in VTABLES:
        v = A.primary(cls); vt[cls] = v
        h.append('static const uint32_t VT_%s = 0x%08X;  // %d methods' % (cls, v, len(A.vtable_entries(v))))
    h.append('')
    for name, i in SLOTS:
        h.append('static const int SLOT_%s = %d;' % (name, i))
    for name, i in MEDIATOR_SLOTS:
        h.append('static const int SLOT_%s = %d;' % (name, i))
    h.append('')
    for name, off, why in OFFSETS:
        h.append('static const int OFF_%s = 0x%02X;  // %s' % (name, off, why))
    h.append('')
    h.append('enum MsgType {')
    for name, v in MSGTYPES:
        h.append('    MSG_%s = %d,' % (name, v))
    h.append('};')
    h.append('')
    keys = {}
    for name, va, why in GLOBALS:
        if name.startswith('UIMessage_key_'):
            v = struct.unpack_from('<H', p.b, p.va2off(va))[0]; keys[name] = v
            h.append('static const uint16_t %s_VALUE = 0x%04X;' % (name, v))
    h.append('')
    h.append('}} // namespace cp::abi')
    open(os.path.join(out_dir, 'addresses.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(h) + '\n')
    js = dict(md5=md5,
              funcs={n: dict(va=va, cc=cc, sig=sig8(va).hex(), why=why) for n, va, cc, why in FUNCS},
              globals={n: dict(va=va, why=why) for n, va, why in GLOBALS},
              vtables=vt, slots=dict(SLOTS), offsets={n: o for n, o, w in OFFSETS},
              msgtypes=dict(MSGTYPES), keys=keys)
    open(os.path.join(out_dir, 'abi.json'), 'w', encoding='utf-8', newline='\n').write(json.dumps(js, indent=1, ensure_ascii=False))
    r = ['# The ABI of the swgemu.exe binary (stage.119798, md5 %s)' % md5, '',
         'Taken with tools/abigen.py: RTTI -> vtable, strings -> xref, a disassembler (capstone). Every row with its evidence.', '',
         '## Functions', '', '| Name | VA | Convention | Evidence |', '|---|---|---|---|']
    r += ['| %s | 0x%08X | %s | %s |' % (n, va, cc, why) for n, va, cc, why in FUNCS]
    r += ['', '## Globals', '', '| Name | VA | What |', '|---|---|---|']
    r += ['| %s | 0x%08X | %s |' % (n, va, why) for n, va, why in GLOBALS]
    r += ['', '## Vtables (primary)', '', '| Class | VA | Methods |', '|---|---|---|']
    r += ['| %s | 0x%08X | %d |' % (c, v, len(A.vtable_entries(v))) for c, v in vt.items()]
    r += ['', '## The vtable slots of UIBaseObject -> UIWidget -> UIPage', '',
          'The anchors: 0 IsA (comparing type tags); 1 GetTypeName (returns a global holding the string "Page"); 2/51/52 pure',
          'virtuals on UIWidget (Clone, GetStyle, Render); 3 Destroy calls slot 28 with flag 1 (the deleting destructor);',
          '4/5 Attach/Detach (the +4 counter); 10/12 stubs `xor al,al; ret 8` (the const char* overloads); 11/13',
          'SetProperty/GetProperty and 19 GetChild taken earlier; 22 RemoveFromParent (parent +0x14); 26 Link calls the base',
          'Link and walks the children; 29 SetRect calls SetLocation(x,y,0) and SetSize; 31/32 SetWidth/SetHeight; 33',
          'SetScrollLocation (+0x3C); 35 GetScrollExtent (+0x44); 37/38 GetMouseCursor (two overloads); 41 CanSelect',
          '(the +0x7C flags: 3, 4, 0x100); 45 SetVisible (SetAttribute 1, the OnShow/OnHide effectors); 48 WantsMessage;',
          '50/56 stubs `ret 8`; 53 IsDropOk (`ret 0xC`); 59 UIPage::Pack (first thing it does is bit 2 in [+0x120] = DoNotPackChildren).', '',
          '| Slot | Name |', '|---|---|'] + ['| %d | %s |' % (i, n) for n, i in SLOTS]
    r += ['', '## Offsets', '', '| Name | Offset | From what |', '|---|---|---|']
    r += ['| %s | 0x%02X | %s |' % (n, o, w) for n, o, w in OFFSETS]
    r += ['', '## UIMessage::Type', '', ', '.join('%s=%d' % (n, v) for n, v in MSGTYPES), '',
          'MouseMove=18 confirmed in warpCursor, MouseWheel=21 in processEvent (the delta multiplication), DragCancel=27 in AbortDrag.', '',
          '## UIMessage keys (the values of the statics)', '',
          ', '.join('%s=0x%04X' % (n[len('UIMessage_key_'):], v) for n, v in keys.items()), '',
          '## Not resolved yet (as the need arises)', '',
          '- CuiWorkspace: getGameWorkspace / getFocusMediator / focusMediator (the cursor stage);',
          '- CuiMediator: s_mediators, getMediatorDebugName, m_thePage;',
          '- UIVolumePage: SetSelectionIndex / FindCell (the cursor walks the geometry, these are not required);',
          '- CuiInputNames::getInputValueString (the binding badges - stage 2);',
          '- UIManager: GetLastMouseCoord, DrawCursor; 0x10EABC0 returns [this+0xEC] (purpose not established).']
    open(report_path, 'w', encoding='utf-8', newline='\n').write('\n'.join(r) + '\n')
    print('addresses.h, abi.json ->', out_dir)
    print('report ->', report_path)


if __name__ == '__main__':
    root = os.path.dirname(HERE)
    gen(os.path.join(root, 'src', 'consoleport', 'abi'), os.path.join(root, 'docs', '03-abi.md'))
