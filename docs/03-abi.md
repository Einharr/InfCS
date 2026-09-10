# The ABI of the swgemu.exe binary (stage.119798, md5 d84ee5565c24420858972edda9dddbf4)

Taken with tools/abigen.py: RTTI -> vtable, strings -> xref, a disassembler (capstone). Every row with its evidence.

## Functions

| Name | VA | Convention | Evidence |
|---|---|---|---|
| UIManager_gUIManager | 0x0087FF60 | cdecl | warpCursor calls it before ProcessMessage; reads [0x1996E98], creates on zero |
| UIManager_ProcessMessage | 0x010E77C0 | thiscall | 10 calls from CuiIoWin::processEvent with a UIMessage on the stack |
| UIManager_GetObjectFromPath | 0x010EA7E0 | thiscall | ecx=[this+4] (root), two arguments (path, type) |
| UIManager_GetFocusedLeaf | 0x010EA810 | thiscall | ecx=[this+4]; jmp [vtbl+0xBC] = slot 47 GetFocusedLeafWidget |
| UIManager_PushContextWidget | 0x010EA050 | thiscall | creates a 0x128 UIPage in [this+0xD8], 21 calls |
| UIManager_PopContextWidgets | 0x010EA220 | thiscall | guard 0x1996E9C, [this+0xD8] |
| UIManager_AbortDrag | 0x010EA8F0 | thiscall | builds a UIMessage Type 27 (DragCancel) for [this+0x3C]/[this+0x40] |
| UIBaseObject_GetObjectFromPath | 0x010F4FA0 | thiscall | taken earlier from SwgCuiDebugInfoPage (getCodeDataObject) |
| UIWidget_SetLocation | 0x01106160 | thiscall | SetRect calls it with (x, y, 0); writes [this+0x2C]/[this+0x30] |
| UIText_SetLocalText | 0x0110F580 | thiscall | QoL NumberCommas hook |
| UIMessage_ctor | 0x01124BF0 | thiscall | zeroes 0x28 bytes; 21 calls, warpCursor among them |
| CuiIoWin_warpCursor | 0x0093D1F0 | thiscall | gotoXY([this+0x10]) + UIMessage(MouseMove=18) + ProcessMessage |
| InputScheme_resetFromType | 0x006E1B40 | cdecl | (const std::string& type, bool confirmed); its own WARNING strings inside |
| CuiActionManager_performAction | 0x008CC460 | cdecl | (const std::string& id, const Unicode::String& params): map[index]/map[-1] -> vtbl[1] |
| CuiActionManager_addAction | 0x008CC650 | cdecl | QoL Toolbar3 hook (action registration) |
| CuiMediator_getCodeDataObject | 0x00A68210 | cdecl | taken earlier |
| runGameLoopOnce | 0x004237C0 | cdecl | QoL CrashGuard hook (the frame) |
| Game_getGameInputMap | 0x00425B80 | cdecl | SwgCuiToolbar::updateKeyBindings (0xF696D0) calls it before "No inputmap" |
| CuiInputNames_getInputValueString | 0x009EB770 | cdecl | (InputMap*, std::string* cmd, Unicode::String* out) - the call at 0xF697FD with add esp,0xC |
| Stlp_allocate | 0x012EA770 | cdecl | node_alloc::allocate(bytes): updateKeyBindings pushes 0x10 for an empty Unicode::String |
| Stlp_deallocate | 0x00424B00 | thiscall | alloc_proxy::deallocate(p, nchars): the end of updateKeyBindings; over 0x80 bytes goes to 0xAC1640 |
| UIText_SetPreLocalized | 0x01112660 | thiscall | (bool): updateKeyBindings pushes 1 before SetLocalText |
| UIVolumePage_Pack | 0x0113C6C0 | thiscall | UIVolumePage vtable slot 59; does not check DoNotPackChildren (only the +0x154 guard), forces CellSize onto the children |
| SwgCuiToolbar_getToolbarItem | 0x00F67890 | thiscall | xref WARNING "the pane request (%d) for the toolbar was > data vector size"; ret 8 -> (int pane, int slot), returns a CuiDragInfo* |
| SwgCuiToolbar_setToolbarItem | 0x00F667C0 | thiscall | the only caller of getToolbarItem with pane<0 and slot<0 checks and an inline operator= for CuiDragInfo; ret 0xC -> (int pane, int slot, const CuiDragInfo*) |
| Game_getPlayerCreature | 0x00425200 | cdecl | the first call in cycleTargetsForPlayer (0x8C7395) and in isTargetCycleOk (0x8C8AB8); reads global 0x190885C and runs it through a type check |
| ClientWorld_findObjectsInRange | 0x00561980 | cdecl | (Vector const* pos, float range, ObjectVector* out): the call at 0x8C73F5 with add esp,0xC; reads [pos],[pos+4],[pos+8] and a float |
| CuiCombatManager_targetIsAttackable | 0x008C72F0 | cdecl | the predicate cycleTargetsOutward (0x8C8A10) passes into cycleTargetsForPlayer; asClientObject vtbl+0x40, asTangibleObject vtbl+0x6C, isTargetCycleOk 0x8C8AB0, isAttackable 0x640220; ret with no number |
| CuiCombatManager_isTargetCycleOk | 0x008C8AB0 | cdecl | called from targetIsAttackable; takes getPlayerCreature itself and compares against [player+0x598]; contains a camera frustum test |
| TangibleObject_isAttackable | 0x00640220 | thiscall | the second call in targetIsAttackable, ecx = tangible |
| CreatureObject_setLookAtTarget | 0x00434AB0 | thiscall | the tail of cycleTargetsForPlayer: ecx=player, the argument is the NetworkId* from getNetworkId; a mouse click makes the same call (SwgCuiHud::targetAtCursor) |
| ClientObject_getLocalizedName | 0x00556EC0 | thiscall | 131 calls; cmp byte [this+0x184] (dirty) -> call 0x556000 (update) -> lea eax,[this+0x178] (Unicode::String) |
| Object_getNetworkId | 0x00B23C60 | thiscall | lea eax,[ecx+0x20]; ret - the NetworkId field |
| Object_getTransform | 0x00B22C80 | thiscall | the call at 0x8C73BE before the position is read; lazy recompute driven by [this+0x34] and the [this+0xC]&0x20 flag; a 3x4 matrix in row order |
| CachedNetworkId_getObject | 0x00B30160 | thiscall | the call at 0x8C73FF in cycleTargetsForPlayer and 0x8C8849 in cycleTargetsGroup: ecx = &CachedNetworkId, returns an Object* |
| ConfigClientGame_getTargetingRange | 0x005034F0 | cdecl | call 0x8C73D5 in cycleTargetsForPlayer; picks the ground or space radius itself from isSpace (0x426170) |
| Stlp_free_big | 0x00AC1640 | cdecl | the vector release branch in cycleTargetsForPlayer for a capacity over 0x80 bytes (0x8C745D) |
| Stlp_free_small | 0x012EA920 | cdecl | (ptr, bytes): the second branch of the same release (0x8C747B) |

## Globals

| Name | VA | What |
|---|---|---|
| UIManager_gSingleton | 0x01996E98 | UIManager*; CuiManager::remove deletes through vtbl[1] |
| UIManager_gIsPopping | 0x01996E9C | the guard in PopContextWidgets |
| CuiManager_ms_theIoWin | 0x0192613C | a store after the CuiIoWin constructor (0x93ADB0, size 0x68) |
| UIMessage_key_BackSpace | 0x018D98E0 | word |
| UIMessage_key_Insert | 0x018D98E2 | word |
| UIMessage_key_Delete | 0x018D98E4 | word |
| UIMessage_key_LeftArrow | 0x018D98E6 | word |
| UIMessage_key_RightArrow | 0x018D98E8 | word |
| UIMessage_key_UpArrow | 0x018D98EA | word |
| UIMessage_key_DownArrow | 0x018D98EC | word |
| UIMessage_key_Home | 0x018D98EE | word |
| UIMessage_key_End | 0x018D98F0 | word |
| UIMessage_key_PageUp | 0x018D98F2 | word |
| UIMessage_key_PageDown | 0x018D98F4 | word |
| UIMessage_key_Tab | 0x018D98F6 | word |
| UIMessage_key_Space | 0x018D98F8 | word |
| UIMessage_key_Enter | 0x018D98FA | word |
| UIMessage_key_Escape | 0x018D98FC | word |
| PROP_CmdName | 0x019355B8 | "CmdName" -> CuiDragInfo::cmd |
| PROP_DragInfoName | 0x019355B4 | "DragInfoName" -> CuiDragInfo::name |
| PROP_CmdStr | 0x019355B0 | "CmdStr" -> CuiDragInfo::str, on a live item this holds "/ache" |
| PROP_DragCommandValue | 0x019355AC | "DragCommandValue" -> CuiDragInfo::commandValue |

## Vtables (primary)

| Class | VA | Methods |
|---|---|---|
| UIWidget | 0x015FA0CC | 57 |
| UIPage | 0x015F9DA4 | 61 |
| UIVolumePage | 0x015FB39C | 61 |
| UIButton | 0x015FA2CC | 57 |
| UIImage | 0x015FA4DC | 57 |
| UIText | 0x015FA1D4 | 57 |
| UIManager | 0x015F9BBC | 2 |
| UIPopupMenu | 0x015FAAC4 | 61 |
| UIRadialMenu | 0x015FA924 | 61 |
| UITabbedPane | 0x015FBAA4 | 61 |
| UIList | 0x015FB2B4 | 57 |
| UICursor | 0x015FABE4 | 29 |
| CuiIoWin | 0x015E8618 | 3 |
| CuiMediator | 0x015E8EE4 | 12 |
| CuiWorkspace | 0x015E9094 | 2 |
| SwgCuiToolbar | 0x015F6798 | 12 |
| SwgCuiInventory | 0x015F03C0 | 12 |
| SwgCuiInventoryContainerIcons | 0x015F73B8 | 12 |

## The vtable slots of UIBaseObject -> UIWidget -> UIPage

The anchors: 0 IsA (comparing type tags); 1 GetTypeName (returns a global holding the string "Page"); 2/51/52 pure
virtuals on UIWidget (Clone, GetStyle, Render); 3 Destroy calls slot 28 with flag 1 (the deleting destructor);
4/5 Attach/Detach (the +4 counter); 10/12 stubs `xor al,al; ret 8` (the const char* overloads); 11/13
SetProperty/GetProperty and 19 GetChild taken earlier; 22 RemoveFromParent (parent +0x14); 26 Link calls the base
Link and walks the children; 29 SetRect calls SetLocation(x,y,0) and SetSize; 31/32 SetWidth/SetHeight; 33
SetScrollLocation (+0x3C); 35 GetScrollExtent (+0x44); 37/38 GetMouseCursor (two overloads); 41 CanSelect
(the +0x7C flags: 3, 4, 0x100); 45 SetVisible (SetAttribute 1, the OnShow/OnHide effectors); 48 WantsMessage;
50/56 stubs `ret 8`; 53 IsDropOk (`ret 0xC`); 59 UIPage::Pack (first thing it does is bit 2 in [+0x120] = DoNotPackChildren).

| Slot | Name |
|---|---|
| 0 | IsA |
| 1 | GetTypeName |
| 2 | Clone |
| 3 | Destroy |
| 4 | Attach |
| 5 | Detach |
| 6 | GetPropertyNames |
| 7 | GetLinkPropertyNames |
| 8 | RemoveProperty_c |
| 9 | RemoveProperty |
| 10 | SetProperty_c |
| 11 | SetProperty |
| 12 | GetProperty_c |
| 13 | GetProperty |
| 14 | ResetLocalizedStrings |
| 15 | CopyPropertiesFrom |
| 16 | AddChild |
| 17 | RemoveChild |
| 18 | SelectChild |
| 19 | GetChild |
| 20 | GetChildren |
| 21 | GetChildCount |
| 22 | RemoveFromParent |
| 23 | MinimizeResources |
| 24 | CanChildMove |
| 25 | MoveChild |
| 26 | Link |
| 27 | DuplicateObject |
| 28 | dtor |
| 29 | SetRect |
| 30 | SetSize |
| 31 | SetWidth |
| 32 | SetHeight |
| 33 | SetScrollLocation |
| 34 | SetScrollExtent |
| 35 | GetScrollExtent |
| 36 | GetScrollSizes |
| 37 | GetMouseCursor_c |
| 38 | GetMouseCursor |
| 39 | GetLocalTooltip |
| 40 | GetWidgetFromPoint |
| 41 | CanSelect |
| 42 | SetSelected |
| 43 | SetSelectable |
| 44 | SetTabRoot |
| 45 | SetVisible |
| 46 | SetUnderMouse |
| 47 | GetFocusedLeafWidget |
| 48 | WantsMessage |
| 49 | ProcessMessage |
| 50 | ProcessChildNotificationMessage |
| 51 | GetStyle |
| 52 | Render |
| 53 | IsDropOk |
| 54 | GetCustomDragWidget |
| 55 | OnSizeChanged |
| 56 | OnLocationChanged |
| 57 | Page_InsertChildBefore |
| 58 | Page_InsertChildAfter |
| 59 | Page_Pack |
| 60 | Page_slot60 |

## Offsets

| Name | Offset | From what |
|---|---|---|
| CreatureObject_group | 0x7B8 | CachedNetworkId; lea ecx,[esi+0x7B8] before getObject |
| GroupObject_membersBegin | 0x2B8 | std::vector<pair<NetworkId,Unicode::String>>::begin |
| GroupObject_membersEnd | 0x2BC | the same vector: end; element stride 0x18, the name at +8 |
| GroupMember_stride | 0x18 | add esi,0x18 in the loop over group members |
| GroupMember_name | 0x08 | a Unicode::String right after the NetworkId in the pair |
| TangibleObject_pvpFlags_slot | 0x28 | call [vtbl+0xA0] in isAttackable (0x640238) |
| TangibleObject_condition | 0x38C | the 0x100 bit (invulnerable) check in isAttackable |
| CreatureObject_lookAtTarget | 0x598 | CachedNetworkId; lea ebx,[player+0x598] in cycleTargetsForPlayer, compared in isTargetCycleOk |
| Object_transform_posX | 0x0C | Transform 3x4 in row order: position at 0x0C/0x1C/0x2C, forward vector at 0x08/0x18/0x28 |
| UIBaseObject_refcount | 0x04 | word; Attach/Detach |
| UIBaseObject_name | 0x08 | std::string (MSVC7) |
| UIBaseObject_parent | 0x14 | RemoveFromParent |
| UIWidget_location | 0x2C | SetLocation |
| UIWidget_size | 0x34 | SetWidth/SetHeight |
| UIWidget_scrollLocation | 0x3C | SetScrollLocation |
| UIWidget_scrollExtent | 0x44 | GetScrollExtent |
| UIWidget_flags | 0x7C | CanSelect: bits 0|1 visible, 2 enabled, 8 getsInput |
| UIPage_children | 0x104 | std::list<UIBaseObject*> (ResetLocalizedStrings/Pack) |
| UIPage_pageFlags | 0x120 | Pack: bit 2 = DoNotPackChildren |
| UIManager_rootPage | 0x04 | GetObjectFromPath/GetFocusedLeaf |
| UIManager_contextPage | 0xD8 | PushContextWidget |
| UIManager_dragObject | 0x40 | AbortDrag: non-zero means a drag is in progress |
| CuiIoWin_mouseCursor | 0x10 | warpCursor |
| SwgCuiToolbar_volumePage | 0x94 | ctor 0xF64AE0: getCodeDataObject(0x27, [esi+0x94], "volumePage") |
| SwgCuiToolbar_volumeKeyBindings | 0xE4 | updateKeyBindings: mov ecx,[ecx+0xE4]; GetChildrenRef |
| UIMessage_Type | 0x00 |  |
| UIMessage_Modifiers | 0x04 | 9 bytes |
| UIMessage_Keystroke | 0x0E | word |
| UIMessage_Data | 0x10 | word |
| UIMessage_MouseX | 0x14 |  |
| UIMessage_MouseY | 0x18 |  |
| UIMessage_DragSource | 0x1C |  |
| UIMessage_DragObject | 0x20 |  |
| UIMessage_DragTarget | 0x24 |  |
| UIMessage_size | 0x28 | the 0x1124BF0 constructor |

## UIMessage::Type

KeyFirst=0, KeyDown=1, KeyUp=2, KeyRepeat=3, Character=4, KeyLast=5, MouseFirst=6, LeftMouseDown=7, MiddleMouseDown=8, RightMouseDown=9, MouseLastFocusChanger=10, LeftMouseDoubleClick=11, MiddleMouseDoubleClick=12, RightMouseDoubleClick=13, LeftMouseUp=14, MiddleMouseUp=15, RightMouseUp=16, MouseLastButton=17, MouseMove=18, MouseEnter=19, MouseExit=20, MouseWheel=21, ContextRequest=22, MouseLast=23, DragFirst=24, DragStart=25, DragEnd=26, DragCancel=27, DragOver=28, DragLast=29

MouseMove=18 confirmed in warpCursor, MouseWheel=21 in processEvent (the delta multiplication), DragCancel=27 in AbortDrag.

## UIMessage keys (the values of the statics)

BackSpace=0x1008, Insert=0x102D, Delete=0x102E, LeftArrow=0x1025, RightArrow=0x1027, UpArrow=0x1026, DownArrow=0x1028, Home=0x1024, End=0x1023, PageUp=0x1021, PageDown=0x1022, Tab=0x1009, Space=0x0020, Enter=0x000D, Escape=0x001B

## Not resolved yet (as the need arises)

- CuiWorkspace: getGameWorkspace / getFocusMediator / focusMediator (the cursor stage);
- CuiMediator: s_mediators, getMediatorDebugName, m_thePage;
- UIVolumePage: SetSelectionIndex / FindCell (the cursor walks the geometry, these are not required);
- CuiInputNames::getInputValueString (the binding badges - stage 2);
- UIManager: GetLastMouseCoord, DrawCursor; 0x10EABC0 returns [this+0xEC] (purpose not established).
