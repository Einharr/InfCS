// Direct calls into swgemu.exe at the addresses from abi/addresses.h. The only place
// that knows about calling conventions and field offsets. verify() checks the function
// signatures and the vtable RTTI names at startup; on a mismatch we stay disabled.
#pragma once
#include "types.h"
#include "../abi/addresses.h"
#include <string>

namespace cp { namespace bin {

// Empty string means everything matched; otherwise the first mismatch.
std::string verify();

// --- UIManager ---
void*    uiManager();                                    // UIManager::gUIManager()
void*    rootPage();                                     // [UIManager+4]
void*    contextPage();                                  // [UIManager+0xD8], 0 if no popups
bool     dragActive();                                   // [UIManager+0x40] != 0 - an icon is being dragged
bool     processMessage(UIMessage& msg);                 // UIManager::ProcessMessage
void*    objectFromPath(const char* path, int typeTag);  // UIManager::GetObjectFromPath(path, type)
void*    focusedLeaf();                                  // UIManager::GetFocusedLeafWidget
void     popContextWidgets(void* widgetOrNull);          // UIManager::PopContextWidgets
void     abortDrag(void* widgetOrNull);                  // UIManager::AbortDrag (DragCancel)

// --- UIBaseObject / UIWidget: virtuals by slot, non-virtuals by address ---
bool     isA(void* obj, int typeTag);                    // slot 0
const char* typeName(void* obj);                         // slot 1
bool     setProperty(void* obj, uint32_t propGlobal, const uint16_t* valueUtf16); // slot 11 (UILowerString*, UIString&)
// CAREFUL: builds a client STLport string for the result. On 7 Sep 2026 that killed the
// live client inside its own allocator (0x012EA89D, a read at address 1). Leave alone
// until string ownership is settled.
bool     getProperty(void* obj, uint32_t propGlobal, std::u16string& out);       // slot 13
void*    getChild(void* page, const char* name);         // slot 19
bool     addChild(void* page, void* child);              // slot 16
bool     removeChild(void* page, void* child);           // slot 17
bool     moveChild(void* page, void* child, int dir);    // slot 25 (0 Up,1 Down,2 Top,3 Bottom - like ChildMovementDirection)
void     link(void* obj);                                // slot 26
void*    duplicateObject(void* obj);                     // slot 27
void     setVisible(void* widget, bool v);               // slot 45
bool     canSelect(void* widget);                        // slot 41
void*    widgetFromPoint(void* page, int x, int y, bool mustGetInput); // slot 40
void     pack(void* page);                               // slot 59 (UIPage::Pack)
void*    objectFromPathRel(void* obj, const char* path); // UIBaseObject::GetObjectFromPath
void     setLocation(void* widget, int x, int y);        // UIWidget::SetLocation(x, y, false)
void     setSize(void* widget, int w, int h);            // slot 30

// --- raw field access (offsets from abi) ---
void*    parentOf(void* obj);
UIPoint  locationOf(void* widget);
UISize   sizeOf(void* widget);
UIPoint  scrollLocationOf(void* widget);
uint32_t flagsOf(void* widget);
const char* nameOf(void* obj);                            // std::string at +0x08 (STLport: begin)

// --- CuiIoWin / actions ---
void*    ioWin();                                        // CuiManager::ms_theIoWin
void     warpCursor(int x, int y);                       // CuiIoWin::warpCursor
bool     performAction(const char* id, const uint16_t* params = nullptr); // CuiActionManager::performAction

// Toolbar pane items. Dead until our toolbarself hook catches the mediator pointer:
// before that self() == 0 and everything returns zero. CuiDragInfo is passed BY POINTER
// only and copied by the client's operator= inside setItem - it holds STLport strings
// and a byte copy tears them.
namespace toolbar {
void  setSelf(void* mediator);
void* self();
void* getItem(int pane, int slot);                 // CuiDragInfo* or 0
bool  setItem(int pane, int slot, void* item);     // item is the client's own CuiDragInfo*
// Read the layout off a live CuiDragInfo. The tail comes from CuiDragInfo::setWidget
// (0x009D2800): commandValue +0x3C, commandValueValid +0x40. The rest are STLport
// strings, whose offsets we take off an item whose text we know. Read only.
void  dumpItem(int pane, int slot);
// Put a command into a slot by assembling a CuiDragInfo. Layout read off a live item
// on 9 Sep 2026: three 12-byte STLport strings in a row - name +0x00 (wide), str +0x0C,
// cmd +0x18 - then objectId, type +0x38, commandValue +0x3C, commandValueValid +0x40.
// Built on a copy of a live item rather than from scratch: inventing the opaque middle
// is riskier than borrowing it. setToolbarItem only reads our object, so the strings
// are ours to free.
bool  setCommandItem(int pane, int slot, const char* cmdStr, const char* cmdName);
// Save the pane, as the client does after a normal drop. Without it the assignment
// lives in memory only and dies on relog - panes are stored on the server.
void  saveSettings();
}

namespace scheme {
// An input scheme is a layout file plus behaviour flags; the player's choice sits in
// options.cfg, [ClientGame] lastInputSchemeType. This binary has seven ground types -
// swg, iso, mmo, fps, ja101, mmo2, swg2 - not the three in ref/client-src. Ours are
// swg/swg_modern (pad without Steam) and swg2 (Steam Input).
const char* current();                     // what is set now, "" if unknown
bool  reset(const char* type);             // switch; true means the client accepted it
}

namespace toolbar {
#ifdef CP_MOCK_BINARY
// Mock: pane items in a table so the tests can see what went where; non-zero tag
// means occupied.
void  mockReset();
void  mockPut(int pane, int slot, int tag);
int   mockTag(int pane, int slot);
#endif
}

// --- game objects: the target list for the enemy ring ---
//
// Reads only, except setLookAtTarget - nothing is asked of the server, we take what the
// client already holds. Filtering is the client's own targetIsAttackable, the predicate
// behind the Tab cycle, so the ring's set matches the cycle's exactly.
namespace game {
void* playerCreature();                              // Game::getPlayerCreature
float targetingRange();                              // ConfigClientGame::getTargetingRange
void* asClientObject(void* object);                  // Object vtable slot 16
// Attackables as the target cycle sees them: findObjectsInRange over the targeting
// radius, then the client's predicate. Returns how many were written.
int   attackableAround(void** out, int max);
// Peaceful things: anything the client lets you target (isTargetCycleOk - frustum,
// targettable in the template, not a corpse) that is not attackable. Terminals,
// vendors, containers, dropped items, vehicles. Own radius: a terminal a hundred
// metres away is no use.
int   peacefulAround(void** out, int max, float range);
// Everything targetable, friend and foe alike.
int   anythingAround(void** out, int max, float range);
bool  localizedName(void* clientObject, std::u16string& out);   // ClientObject::getLocalizedName
bool  positionOf(void* object, float xyz[3]);        // Object::getTransform, rows 0x0C/0x1C/0x2C
bool  forwardOf(void* object, float xyz[3]);         // the same transform, column k
// The only call with consequences: what a left click on a target does
// (SwgCuiHud::targetAtCursor). One press, one call.
bool  setLookAtTarget(void* player, void* object);
void* lookAtTarget(void* player);                    // the current target, [player+0x598]

// PvP flags, virtual slot 40 - through the vtable because CreatureObject overrides it
// (on TangibleObject it is just [this+0x3A0]). Bits per PvpData.h: 1 YouCanAttack,
// 2 CanAttackYou, 0x20 IsEnemy.
uint32_t pvpFlags(void* object);
enum Threat { THREAT_NEUTRAL = 0, THREAT_ATTACKABLE, THREAT_HOSTILE };
Threat threatOf(void* object);                       // how the client colors the name overhead
// What is targeted now. A peaceful target changes what the right cross's faces mean;
// a hostile one only reddens the backdrop - in combat the faces stay abilities.
enum TargetKind { TARGET_NONE = 0, TARGET_PEACEFUL, TARGET_HOSTILE };
TargetKind targetKind();
inline bool targetIsPeaceful() { return targetKind() == TARGET_PEACEFUL; }

// Group mates in targetGroupMember's order: the group vector minus the player. The
// names are in the group itself, so the mates' objects are not needed. Empty if solo.
int   groupMemberNames(std::u16string* out, int max);
#ifdef CP_MOCK_BINARY
void  mockSetGroup(const char16_t* const* names, int n);
void  mockSetPeacefulTarget(bool on);
#endif
#ifdef CP_MOCK_BINARY
// Mock: the test feeds "objects" and reads back which one was picked.
void  mockSetFoes(void** objs, const char16_t* const* names, int n);
void* mockLastTarget();
void  mockReset();
#endif
}

// --- UIText ---
void     setLocalText(void* text, const uint16_t* utf16);    // SetPreLocalized(true) + SetLocalText
// --- bindings: the client's InputMap through CuiInputNames ---
bool     bindingLabel(const char* cmdName, std::u16string& out);   // "CMD_uiToolbarSlot07" -> "L2+TRI"

// An STLport string in the binary's allocator, safe to hand to client functions as an
// out parameter - they may reallocate the buffer with their own allocator.
struct BinUString {
    const uint16_t* begin; const uint16_t* end; const uint16_t* cap;
    BinUString(); ~BinUString();
    void clear() { end = begin; }
    std::u16string str() const { return std::u16string(reinterpret_cast<const char16_t*>(begin), reinterpret_cast<const char16_t*>(end)); }
};

// --- UIMessage keys (static values, checked at startup) ---
uint16_t keyEscape(); uint16_t keyEnter(); uint16_t keyTab(); uint16_t keySpace();
uint16_t keyUp(); uint16_t keyDown(); uint16_t keyLeft(); uint16_t keyRight();
uint16_t keyPageUp(); uint16_t keyPageDown(); uint16_t keyHome(); uint16_t keyEnd();
uint16_t keyBackSpace();

}} // namespace cp::bin
