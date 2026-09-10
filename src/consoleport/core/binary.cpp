#include "binary.h"
#include <string>
#include "runtime.h"      // tracef: dumpItem writes the CuiDragInfo breakdown to the log
#include <cstring>

#if !defined(_M_IX86) && !defined(CP_MOCK_BINARY)
#error "the module only works in the client 32-bit process (or with CP_MOCK_BINARY for the tests)"
#endif

namespace cp { namespace bin {

using namespace abi;

// ------------------------------------------------------------ memory reading
// In the client the addresses are just memory. The tests (CP_MOCK_BINARY) swap in a
// reader of their own, see tests/mock_binary.cpp.
#ifdef CP_MOCK_BINARY
extern const unsigned char* mockRead(uint32_t va, size_t n);
static const unsigned char* rd(uint32_t va, size_t n) { return mockRead(va, n); }
#else
static const unsigned char* rd(uint32_t va, size_t) { return reinterpret_cast<const unsigned char*>(va); }
#endif

std::string verify()
{
    for (const FuncSig& f : FUNCS) {
        const unsigned char* p = rd(f.va, 8);
        if (!p) return std::string("signature mismatch: ") + f.name;
        if (std::memcmp(p, f.sig, 8) == 0) continue;
        // QoL may have hooked the function (SetLocalText for NumberCommas, say): the
        // prologue in memory becomes a jump to its detour, though a call still lands in
        // the original. So E9, EB and FF 25 prologues are accepted.
        if (p[0] == 0xE9 || p[0] == 0xEB || (p[0] == 0xFF && p[1] == 0x25))
            continue;
        return std::string("signature mismatch: ") + f.name;
    }
    struct K { uint32_t va; uint16_t v; } keys[] = {
        {UIMessage_key_Escape, UIMessage_key_Escape_VALUE}, {UIMessage_key_Enter, UIMessage_key_Enter_VALUE},
        {UIMessage_key_UpArrow, UIMessage_key_UpArrow_VALUE}, {UIMessage_key_DownArrow, UIMessage_key_DownArrow_VALUE},
        {UIMessage_key_LeftArrow, UIMessage_key_LeftArrow_VALUE}, {UIMessage_key_RightArrow, UIMessage_key_RightArrow_VALUE},
        {UIMessage_key_Tab, UIMessage_key_Tab_VALUE}, {UIMessage_key_Space, UIMessage_key_Space_VALUE},
        {UIMessage_key_BackSpace, UIMessage_key_BackSpace_VALUE},
    };
    for (const K& k : keys) {
        const unsigned char* p = rd(k.va, 2);
        if (!p || *reinterpret_cast<const uint16_t*>(p) != k.v)
            return "UIMessage static mismatch";
    }
    return "";
}

#ifndef CP_MOCK_BINARY
// ------------------------------------------------------------ calls
typedef void* (__cdecl*    FnGetMgr)();
typedef bool  (__thiscall* FnProcMsg)(void* self, UIMessage* msg);
typedef void* (__thiscall* FnObjPath)(void* self, const char* path, int type);
typedef void* (__thiscall* FnFocused)(void* self);
typedef void  (__thiscall* FnPop)(void* self, void* w);
typedef void  (__thiscall* FnWarp)(void* self, int x, int y);
typedef bool  (__cdecl*    FnAction)(const StlpString* id, const StlpUString* params);
typedef void* (__thiscall* FnObjPathRel)(void* self, const char* path);
typedef void  (__thiscall* FnSetLoc)(void* self, int x, int y, bool center);

static inline void** vt(void* obj) { return *reinterpret_cast<void***>(obj); }
template <class F> static inline F slot(void* obj, int i) { return reinterpret_cast<F>(vt(obj)[i]); }

void* uiManager()   { return reinterpret_cast<FnGetMgr>(UIManager_gUIManager)(); }
void* rootPage()    { void* m = uiManager(); return m ? *reinterpret_cast<void**>(reinterpret_cast<char*>(m) + OFF_UIManager_rootPage) : nullptr; }
void* contextPage() { void* m = uiManager(); return m ? *reinterpret_cast<void**>(reinterpret_cast<char*>(m) + OFF_UIManager_contextPage) : nullptr; }
// Mid-drag UIManager keeps the drag object at +0x40 (see AbortDrag). The crossbar opens
// its flyouts on that cue, so there is somewhere to drop.
bool  dragActive() { void* m = uiManager(); return m && *reinterpret_cast<void**>(reinterpret_cast<char*>(m) + OFF_UIManager_dragObject) != nullptr; }
bool  processMessage(UIMessage& msg) { void* m = uiManager(); return m && reinterpret_cast<FnProcMsg>(UIManager_ProcessMessage)(m, &msg); }
void* objectFromPath(const char* path, int typeTag) { void* m = uiManager(); return m ? reinterpret_cast<FnObjPath>(UIManager_GetObjectFromPath)(m, path, typeTag) : nullptr; }
void* focusedLeaf() { void* m = uiManager(); return m ? reinterpret_cast<FnFocused>(UIManager_GetFocusedLeaf)(m) : nullptr; }
void  popContextWidgets(void* w) { void* m = uiManager(); if (m) reinterpret_cast<FnPop>(UIManager_PopContextWidgets)(m, w); }
void  abortDrag(void* w) { void* m = uiManager(); if (m) reinterpret_cast<FnPop>(UIManager_AbortDrag)(m, w); }

typedef bool  (__thiscall* FnIsA)(void* self, int tag);
typedef const char* (__thiscall* FnTypeName)(void* self);
typedef bool  (__thiscall* FnSetProp)(void* self, const void* lowerName, const StlpUString* value);
typedef void* (__thiscall* FnGetChild)(void* self, const char* name);
typedef bool  (__thiscall* FnAddChild)(void* self, void* child);
typedef bool  (__thiscall* FnMoveChild)(void* self, void* child, int dir);
typedef void  (__thiscall* FnVoid)(void* self);
typedef void* (__thiscall* FnDup)(void* self);
typedef void  (__thiscall* FnSetVis)(void* self, bool v);
typedef bool  (__thiscall* FnCanSel)(void* self);
typedef void* (__thiscall* FnWfp)(void* self, const UIPoint* pt, bool mustGetInput);
typedef void  (__thiscall* FnSetSize)(void* self, const UISize* sz);

bool isA(void* o, int tag)                 { return slot<FnIsA>(o, SLOT_IsA)(o, tag); }
const char* typeName(void* o)              { return slot<FnTypeName>(o, SLOT_GetTypeName)(o); }
bool setProperty(void* o, uint32_t prop, const uint16_t* v) { StlpUString s(v); return slot<FnSetProp>(o, SLOT_SetProperty)(o, reinterpret_cast<const void*>(prop), &s); }
void* getChild(void* page, const char* n)  { return slot<FnGetChild>(page, SLOT_GetChild)(page, n); }
bool addChild(void* page, void* c)         { return slot<FnAddChild>(page, SLOT_AddChild)(page, c); }
bool removeChild(void* page, void* c)      { return slot<FnAddChild>(page, SLOT_RemoveChild)(page, c); }
bool moveChild(void* page, void* c, int d) { return slot<FnMoveChild>(page, SLOT_MoveChild)(page, c, d); }
void link(void* o)                         { slot<FnVoid>(o, SLOT_Link)(o); }
void* duplicateObject(void* o)             { return slot<FnDup>(o, SLOT_DuplicateObject)(o); }
void setVisible(void* w, bool v)           { slot<FnSetVis>(w, SLOT_SetVisible)(w, v); }
bool canSelect(void* w)                    { return slot<FnCanSel>(w, SLOT_CanSelect)(w); }
void* widgetFromPoint(void* page, int x, int y, bool mgi) { UIPoint p = {x, y}; return slot<FnWfp>(page, SLOT_GetWidgetFromPoint)(page, &p, mgi); }
void pack(void* page)                      { slot<FnVoid>(page, SLOT_Page_Pack)(page); }
void* objectFromPathRel(void* o, const char* path) { return reinterpret_cast<FnObjPathRel>(UIBaseObject_GetObjectFromPath)(o, path); }
void setLocation(void* w, int x, int y)    { reinterpret_cast<FnSetLoc>(UIWidget_SetLocation)(w, x, y, false); }
void setSize(void* w, int x, int y)        { UISize s = {x, y}; slot<FnSetSize>(w, SLOT_SetSize)(w, &s); }

static inline char* at(void* o, int off) { return reinterpret_cast<char*>(o) + off; }
void* parentOf(void* o)          { return *reinterpret_cast<void**>(at(o, OFF_UIBaseObject_parent)); }
UIPoint locationOf(void* w)      { return *reinterpret_cast<UIPoint*>(at(w, OFF_UIWidget_location)); }
UISize sizeOf(void* w)           { return *reinterpret_cast<UISize*>(at(w, OFF_UIWidget_size)); }
UIPoint scrollLocationOf(void* w){ return *reinterpret_cast<UIPoint*>(at(w, OFF_UIWidget_scrollLocation)); }
uint32_t flagsOf(void* w)        { return *reinterpret_cast<uint32_t*>(at(w, OFF_UIWidget_flags)); }
const char* nameOf(void* o)      { const char* b = *reinterpret_cast<const char**>(at(o, OFF_UIBaseObject_name)); return b ? b : ""; }

void* ioWin()                    { return *reinterpret_cast<void**>(CuiManager_ms_theIoWin); }
void warpCursor(int x, int y)    { void* io = ioWin(); if (io) reinterpret_cast<FnWarp>(CuiIoWin_warpCursor)(io, x, y); }
bool performAction(const char* id, const uint16_t* params)
{
    StlpString sid(id); StlpUString sp = params ? StlpUString(params) : StlpUString();
    return reinterpret_cast<FnAction>(CuiActionManager_performAction)(&sid, &sp);
}

namespace game {
typedef void*   (__cdecl*    FnPlayer)();
typedef float   (__cdecl*    FnRange)();
typedef void    (__cdecl*    FnFindInRange)(const float* pos, float range, void* outVector);
typedef bool    (__cdecl*    FnAttackable)(void* actor, void* target);
typedef void*   (__thiscall* FnAsClient)(void* self);
typedef void*   (__thiscall* FnGetName)(void* self);
typedef void*   (__thiscall* FnGetTransform)(void* self);
typedef void    (__thiscall* FnSetLookAt)(void* self, const void* networkId);
typedef void*   (__thiscall* FnGetNetId)(void* self);
typedef void    (__cdecl*    FnFreeBig)(void* p);
typedef void*   (__thiscall* FnCachedGet)(void* self);

// std::vector<Object*>, STLport layout: three pointers, exactly the fields
// cycleTargetsForPlayer reads and writes.
struct ObjVector { void** begin; void** finish; void** cap; };

void* playerCreature()  { return reinterpret_cast<FnPlayer>(Game_getPlayerCreature)(); }
float targetingRange()  { return reinterpret_cast<FnRange>(ConfigClientGame_getTargetingRange)(); }
void* asClientObject(void* o) { return o ? slot<FnAsClient>(o, 16)(o) : nullptr; }

bool positionOf(void* object, float xyz[3])
{
    if (!object) return false;
    void* t = reinterpret_cast<FnGetTransform>(Object_getTransform)(object);
    if (!t) return false;
    const char* m = reinterpret_cast<const char*>(t);
    // Transform 3x4, row order: the translation is each row's fourth column.
    xyz[0] = *reinterpret_cast<const float*>(m + 0x0C);
    xyz[1] = *reinterpret_cast<const float*>(m + 0x1C);
    xyz[2] = *reinterpret_cast<const float*>(m + 0x2C);
    return true;
}

bool forwardOf(void* object, float xyz[3])
{
    if (!object) return false;
    void* t = reinterpret_cast<FnGetTransform>(Object_getTransform)(object);
    if (!t) return false;
    const char* m = reinterpret_cast<const char*>(t);
    xyz[0] = *reinterpret_cast<const float*>(m + 0x08);
    xyz[1] = *reinterpret_cast<const float*>(m + 0x18);
    xyz[2] = *reinterpret_cast<const float*>(m + 0x28);
    return true;
}

int attackableAround(void** out, int max)
{
    if (!out || max <= 0) return 0;
    void* player = playerCreature();
    if (!player) return 0;
    float pos[3];
    if (!positionOf(player, pos)) return 0;

    // Our buffer, with room to spare so the client has no reason to reallocate and we
    // have nothing to free. If more objects turn up it allocates its own, which we
    // release the way cycleTargetsForPlayer does.
    static void* s_buf[512];
    ObjVector v = {s_buf, s_buf, s_buf + (sizeof s_buf / sizeof s_buf[0])};
    reinterpret_cast<FnFindInRange>(ClientWorld_findObjectsInRange)(pos, targetingRange(), &v);

    int n = 0;
    for (void** it = v.begin; it != v.finish && n < max; ++it) {
        // The Tab cycle's own predicate: frustum, attackability, no corpses, skips
        // the current target.
        if (reinterpret_cast<FnAttackable>(CuiCombatManager_targetIsAttackable)(player, *it))
            out[n++] = *it;
    }
    if (v.begin != s_buf && v.begin)
        reinterpret_cast<FnFreeBig>(Stlp_free_big)(v.begin);
    return n;
}

typedef bool  (__cdecl*    FnCycleOk)(void* tangible);
// Kept apart from FnAttackable above: that one is __cdecl (actor, target), this one
// __thiscall (tangible).
typedef bool  (__thiscall* FnTangibleAttackable)(void* tangible);
typedef void* (__thiscall* FnAsTangible)(void* self);

// peacefulOnly=false: everything that passes the client's filter.
static int aroundFiltered(void** out, int max, float range, bool peacefulOnly)
{
    if (!out || max <= 0) return 0;
    void* player = playerCreature();
    if (!player) return 0;
    float pos[3];
    if (!positionOf(player, pos)) return 0;

    static void* s_buf[512];
    ObjVector v = {s_buf, s_buf, s_buf + (sizeof s_buf / sizeof s_buf[0])};
    reinterpret_cast<FnFindInRange>(ClientWorld_findObjectsInRange)(pos, range, &v);

    int n = 0;
    for (void** it = v.begin; it != v.finish && n < max; ++it) {
        void* co = asClientObject(*it);
        if (!co) continue;
        // asTangibleObject: virtual slot 27, as targetIsAttackable calls it.
        void* tan = slot<FnAsTangible>(co, 27)(co);
        if (!tan) continue;
        // The client's filter in full: frustum, the template's targettable flag, no
        // corpses, skips the current target. Not creatures only - terminals pass.
        if (!reinterpret_cast<FnCycleOk>(CuiCombatManager_isTargetCycleOk)(tan)) continue;
        // Attackables belong to the combat ring, not here.
        if (reinterpret_cast<FnTangibleAttackable>(TangibleObject_isAttackable)(tan)) continue;
        out[n++] = *it;
    }
    if (v.begin != s_buf && v.begin)
        reinterpret_cast<FnFreeBig>(Stlp_free_big)(v.begin);
    return n;
}

int peacefulAround(void** out, int max, float range) { return aroundFiltered(out, max, range, true); }
int anythingAround(void** out, int max, float range) { return aroundFiltered(out, max, range, false); }

bool localizedName(void* clientObject, std::u16string& out)
{
    if (!clientObject) return false;
    void* str = reinterpret_cast<FnGetName>(ClientObject_getLocalizedName)(clientObject);
    if (!str) return false;
    // Unicode::String is an STLport basic_string<unsigned short>: begin, end, cap.
    const uint16_t* const* f = reinterpret_cast<const uint16_t* const*>(str);
    const uint16_t* b = f[0]; const uint16_t* e = f[1];
    if (!b || !e || e < b || (e - b) > 128) return false;
    out.assign(reinterpret_cast<const char16_t*>(b), static_cast<size_t>(e - b));
    return true;
}

void* lookAtTarget(void* player)
{
    if (!player) return nullptr;
    // A CachedNetworkId starts with the NetworkId itself (8 bytes), not a pointer, so
    // the field cannot be read as void* - getObject fetches the object.
    return reinterpret_cast<FnCachedGet>(CachedNetworkId_getObject)(
        reinterpret_cast<char*>(player) + OFF_CreatureObject_lookAtTarget);
}

bool setLookAtTarget(void* player, void* object)
{
    if (!player || !object) return false;
    void* id = reinterpret_cast<FnGetNetId>(Object_getNetworkId)(object);
    if (!id) return false;
    reinterpret_cast<FnSetLookAt>(CreatureObject_setLookAtTarget)(player, id);
    return true;
}

typedef uint32_t (__thiscall* FnPvp)(void* self);

uint32_t pvpFlags(void* object)
{
    if (!object) return 0;
    void* co = asClientObject(object);
    if (!co) return 0;
    return slot<FnPvp>(co, OFF_TangibleObject_pvpFlags_slot)(co);
}

Threat threatOf(void* object)
{
    const uint32_t f = pvpFlags(object);
    // CuiGameColorManager::findTypeForObject's order: enemy, then can-attack-you, then
    // merely attackable.
    if (f & 0x20u) return THREAT_HOSTILE;            // IsEnemy
    if (f & 0x02u) return THREAT_HOSTILE;            // CanAttackYou
    if (f & 0x01u) return THREAT_ATTACKABLE;         // YouCanAttack
    return THREAT_NEUTRAL;
}

TargetKind targetKind()
{
    void* player = playerCreature();
    if (!player) return TARGET_NONE;
    void* target = lookAtTarget(player);
    if (!target || target == player) return TARGET_NONE;
    return threatOf(target) == THREAT_NEUTRAL ? TARGET_PEACEFUL : TARGET_HOSTILE;
}

int groupMemberNames(std::u16string* out, int max)
{
    if (!out || max <= 0) return 0;
    void* player = playerCreature();
    if (!player) return 0;
    char* p = reinterpret_cast<char*>(player);
    void* group = reinterpret_cast<FnCachedGet>(CachedNetworkId_getObject)(p + OFF_CreatureObject_group);
    if (!group) return 0;

    const void* myId = reinterpret_cast<FnGetNetId>(Object_getNetworkId)(player);
    if (!myId) return 0;
    const uint32_t myLo = reinterpret_cast<const uint32_t*>(myId)[0];
    const uint32_t myHi = reinterpret_cast<const uint32_t*>(myId)[1];

    char* g = reinterpret_cast<char*>(group);
    char* it = *reinterpret_cast<char**>(g + OFF_GroupObject_membersBegin);
    char* end = *reinterpret_cast<char**>(g + OFF_GroupObject_membersEnd);
    if (!it || !end || end < it) return 0;
    // The vector is the client's: the walk is bounded so a broken pointer cannot send
    // us wandering through its memory.
    const size_t bytes = static_cast<size_t>(end - it);
    if (bytes % OFF_GroupMember_stride != 0 || bytes > 64u * OFF_GroupMember_stride) return 0;

    int n = 0;
    for (; it != end && n < max; it += OFF_GroupMember_stride) {
        const uint32_t lo = reinterpret_cast<const uint32_t*>(it)[0];
        const uint32_t hi = reinterpret_cast<const uint32_t*>(it)[1];
        if (lo == myLo && hi == myHi) continue;      // skip ourselves - findMemberByIndex does the same
        const uint16_t* const* f = reinterpret_cast<const uint16_t* const*>(it + OFF_GroupMember_name);
        const uint16_t* b = f[0]; const uint16_t* e = f[1];
        if (!b || !e || e < b || (e - b) > 64) { out[n++].clear(); continue; }
        out[n++].assign(reinterpret_cast<const char16_t*>(b), static_cast<size_t>(e - b));
    }
    return n;
}
}

namespace toolbar {
typedef void* (__thiscall* FnGetItem)(void* self, int pane, int slot);
typedef void  (__thiscall* FnSetItem)(void* self, int pane, int slot, const void* item);
static void* g_self = nullptr;

void  setSelf(void* m) { if (m) g_self = m; else g_self = nullptr; }
void* self() { return g_self; }

void* getItem(int pane, int slot)
{
    if (!g_self) return nullptr;
    return reinterpret_cast<FnGetItem>(SwgCuiToolbar_getToolbarItem)(g_self, pane, slot);
}

bool setItem(int pane, int slot, void* item)
{
    // Zero is checked here: setToolbarItem dereferences item through operator= and
    // would take a c0000005 on the spot.
    if (!g_self || !item) return false;
    reinterpret_cast<FnSetItem>(SwgCuiToolbar_setToolbarItem)(g_self, pane, slot, item);
    return true;
}

// Every pointer is sanity-checked before it is followed. A mistake here kills the
// client, and it has been killed twice already.
static bool looksLikePtr(uint32_t v) { return v > 0x10000u && v < 0x80000000u; }

void dumpItem(int pane, int slot)
{
    void* it = getItem(pane, slot);
    if (!it) { cp::tracef("dumpItem: pane %d slot %d is empty", pane, slot); return; }
    const uint32_t* w = reinterpret_cast<const uint32_t*>(it);
    for (int i = 0; i < 0x60 / 4; i += 4)
        cp::tracef("dumpItem +%02X: %08X %08X %08X %08X", i * 4, w[i], w[i + 1], w[i + 2], w[i + 3]);
    // STLport strings are triples (begin, end, cap). Find the triples, print what is
    // behind them: that gives both the offset and narrow vs wide.
    for (int i = 0; i + 1 < 0x60 / 4; ++i) {
        const uint32_t b = w[i], e = w[i + 1];
        if (!looksLikePtr(b) || e < b || (e - b) > 512) continue;
        const uint32_t len = e - b;
        char narrow[80] = {0}, wide[80] = {0};
        const char* cb = reinterpret_cast<const char*>(b);
        for (uint32_t k = 0; k < len && k < 78; ++k) narrow[k] = (cb[k] >= 32 && cb[k] < 127) ? cb[k] : '.';
        const uint16_t* wb = reinterpret_cast<const uint16_t*>(b);
        for (uint32_t k = 0; k * 2 < len && k < 78; ++k) wide[k] = (wb[k] >= 32 && wb[k] < 127) ? char(wb[k]) : '.';
        cp::tracef("dumpItem +%02X: length %u | ascii \"%s\" | utf16 \"%s\"", i * 4, len, narrow, wide);
    }
}

} // namespace toolbar
static inline uint16_t rdw(uint32_t va) { return *reinterpret_cast<const uint16_t*>(va); }

typedef void* (__cdecl* FnAlloc)(uint32_t bytes);
typedef void  (__thiscall* FnDealloc)(void* proxy, void* p, uint32_t nchars);
typedef void* (__cdecl* FnGetImap)();
typedef bool  (__cdecl* FnBindStr)(void* imap, const StlpString* cmd, BinUString* out);
typedef void  (__thiscall* FnSetPreLoc)(void* self, bool v);
typedef void  (__thiscall* FnSetLocalText)(void* self, const StlpUString* s);
typedef bool  (__thiscall* FnGetProp)(void* self, const void* lowerName, BinUString* out);

// The STLport node allocator takes small blocks only: bin = (size-1)/8, the array
// starts at 0x19A8F08 and holds sixteen, so the ceiling is 128 bytes. The old 256
// characters (512 bytes) asked for bin 63, the read ran off the array and the free-list
// head was garbage - both crashes on 9 Sep 2026 had ECX=0x019A9004, the address of that
// 63rd bin. Anything longer the client relocates by its own full path.
static const uint32_t BINSTR_CAP = 56;    // characters = 112 bytes, bin 13
// The buffer is never freed by us. We allocate in bytes (BINSTR_CAP*2) while the old
// destructor freed in characters - the block landed in the wrong STLport bin, the free
// list rotted, and the crash came later, on the client's next allocation (9 Sep 2026,
// twice, taking a block off a list whose head was 1). Neither the property nor the
// widget was at fault.
//
// So: one buffer for the whole run, taken once and never given back. The client writes
// into it in place while there is room (it checks finish+2 <= cap itself). If a value
// does not fit, the client relocates the string and frees our buffer along the way -
// begin changes, and we take a new one next time.
static uint16_t* g_binstrBuf = nullptr;

BinUString::BinUString()
{
    // Never ask this allocator for more than 128 bytes.
    static_assert(BINSTR_CAP * 2 <= 128, "the STLport node allocator only holds 128 bytes");
    if (!g_binstrBuf)
        g_binstrBuf = static_cast<uint16_t*>(reinterpret_cast<FnAlloc>(Stlp_allocate)(BINSTR_CAP * 2));
    uint16_t* p = g_binstrBuf;
    if (!p) { begin = end = cap = nullptr; return; }
    p[0] = 0; begin = end = p; cap = p + BINSTR_CAP;
}
BinUString::~BinUString()
{
    // The client relocated the string and freed our buffer with it. Forget it.
    if (begin != g_binstrBuf) g_binstrBuf = nullptr;
    begin = end = cap = nullptr;
}

namespace toolbar {
// BinUString's narrow twin: for the str and cmd fields of an item we assemble.
struct BinNString {
    const char *begin, *end, *cap;
    bool ok;
    explicit BinNString(const char* text)
    {
        uint32_t n = 0; while (text && text[n]) ++n;
        const uint32_t room = n + 1;
        // Same 128-byte ceiling as the wide string: past it the bin index runs off
        // the end of the array.
        if (room > 128) { begin = end = cap = nullptr; ok = false; return; }
        char* p = static_cast<char*>(reinterpret_cast<FnAlloc>(Stlp_allocate)(room));
        if (!p) { begin = end = cap = nullptr; ok = false; return; }
        for (uint32_t i = 0; i < n; ++i) p[i] = text[i];
        p[n] = 0; begin = p; end = p + n; cap = p + room; ok = true;
    }
    // The memory is not returned: setToolbarItem only reads our string. Under two
    // hundred bytes per BIND press is not worth another chance to get the units wrong.
    ~BinNString() { begin = end = cap = nullptr; }
};

void saveSettings()
{
    if (!g_self) return;
    reinterpret_cast<FnVoid>(slot<FnVoid>(g_self, abi::SLOT_CuiMediator_saveSettings))(g_self);
}

} // namespace toolbar

namespace scheme {
typedef bool (__cdecl* FnResetScheme)(const StlpString* type, bool confirmed);
static char g_current[32] = "";

const char* current() { return g_current; }

bool reset(const char* type)
{
    if (!type || !*type) return false;
    // The call Options > Controls makes: the client re-reads the scheme file and its
    // flags. confirmed=true skips the dialog, or the switch waits for the player.
    StlpString s(type);
    const bool ok = reinterpret_cast<FnResetScheme>(InputScheme_resetFromType)(&s, true);
    if (ok) { uint32_t i = 0; while (type[i] && i < sizeof g_current - 1) { g_current[i] = type[i]; ++i; } g_current[i] = 0; }
    cp::tracef("input scheme: %s -> %s", type, ok ? "accepted" : "REFUSED");
    return ok;
}
} // namespace scheme

namespace toolbar {

bool setCommandItem(int pane, int slot, const char* cmdStr, const char* cmdName)
{
    if (!g_self || !cmdStr) return false;
    // The donor has to BE a command (type == 2). The first version took any live item
    // and zeroed objectId on top, and the client tripped over the half-product while
    // tearing the pane down - by then with our DLL nowhere on the stack (9 Sep 2026, a
    // read at 0xFC7956E2 inside CuiDragInfo). The opaque middle is never invented, only
    // copied from a real command.
    void* donor = nullptr;
    for (int p2 = 0; !donor && p2 < 4; ++p2)
        for (int s2 = 0; !donor && s2 < 10; ++s2) {
            void* it2 = getItem(p2, s2);
            if (it2 && static_cast<const uint32_t*>(it2)[14] == 2) donor = it2;
        }
    if (!donor) { cp::tracef("setCommandItem: no command sample on the pane - drop any command with the mouse once"); return false; }

    // The object is bigger than 0x44: live dumps have data at +0x48 and +0x4C, and the
    // destructor frees AttachmentData - an owning pointer past that line. With a 0x44
    // buffer operator= read uninitialised stack, the toolbar got a garbage pointer and
    // died deleting it (9 Sep 2026, three times with the same stack). Room to spare, and
    // everything copied off a real command.
    enum { ITEM_BYTES = 0x60 };
    unsigned char buf[ITEM_BYTES];
    const unsigned char* src = static_cast<const unsigned char*>(donor);
    for (int i = 0; i < ITEM_BYTES; ++i) buf[i] = src[i];

    BinNString sStr(cmdStr), sCmd(cmdName ? cmdName : "");
    if (!sStr.ok || !sCmd.ok) { cp::tracef("setCommandItem: the string did not fit the node allocator"); return false; }
    uint32_t* w = reinterpret_cast<uint32_t*>(buf);
    // name (wide) is left as it came from the donor: fewer hand-made fields.
    w[3] = reinterpret_cast<uint32_t>(sStr.begin);   // str  +0x0C
    w[4] = reinterpret_cast<uint32_t>(sStr.end);
    w[5] = reinterpret_cast<uint32_t>(sStr.cap);
    w[6] = reinterpret_cast<uint32_t>(sCmd.begin);   // cmd  +0x18
    w[7] = reinterpret_cast<uint32_t>(sCmd.end);
    w[8] = reinterpret_cast<uint32_t>(sCmd.cap);
    // objectId is zeroed whole, from the end of cmd to type. It holds a mutable
    // Watcher<Object>, a list node that registers itself with the watched object; a byte
    // copy is an unregistered duplicate that unlinks through stale pointers on teardown
    // (9 Sep 2026, three deaths reading garbage at +0x28). An empty Watcher is zeros,
    // and a command needs no object anyway.
    for (int i = 9; i <= 13; ++i) w[i] = 0;          // +0x24 .. +0x37
    w[15] = 0;                                       // commandValue
    buf[0x40] = 0;                                   // commandValueValid

    setItem(pane, slot, buf);
    // Re-read the slot and see whether the string actually landed.
    void* back = getItem(pane, slot);
    bool ok = false;
    if (back) {
        const uint32_t* r = static_cast<const uint32_t*>(back);
        const char* b = reinterpret_cast<const char*>(r[3]);
        const char* e = reinterpret_cast<const char*>(r[4]);
        if (b && e >= b && (e - b) < 256) {
            std::string got(b, e);
            ok = (got == cmdStr);
            cp::tracef("setCommandItem: pane %d slot %d, wrote \"%s\", read \"%s\", match=%d",
                       pane, slot, cmdStr, got.c_str(), (int)ok);
        }
    }
    if (!ok) { cp::tracef("setCommandItem: pane %d slot %d - the check did not add up", pane, slot); return false; }
    // A normal drop's second step: without it the assignment dies on relog - panes
    // live on the server.
    saveSettings();
    cp::tracef("setCommandItem: pane saved (saveSettings)");
    return true;
}

} // namespace toolbar

bool getProperty(void* o, uint32_t prop, std::u16string& out)
{
    BinUString s;
    bool ok = slot<FnGetProp>(o, SLOT_GetProperty)(o, reinterpret_cast<const void*>(prop), &s);
    if (ok) out = s.str();
    return ok;
}

void setLocalText(void* text, const uint16_t* v)
{
    reinterpret_cast<FnSetPreLoc>(UIText_SetPreLocalized)(text, true);
    StlpUString s(v); reinterpret_cast<FnSetLocalText>(UIText_SetLocalText)(text, &s);
}

bool bindingLabel(const char* cmd, std::u16string& out)
{
    void* imap = reinterpret_cast<FnGetImap>(Game_getGameInputMap)();
    if (!imap) return false;
    BinUString s; StlpString c(cmd);
    bool ok = reinterpret_cast<FnBindStr>(CuiInputNames_getInputValueString)(imap, &c, &s);
    if (ok) out = s.str();
    return ok;
}
#else
// Mock build: never called, still has to link.
void* uiManager() { return nullptr; } void* rootPage() { return nullptr; } void* contextPage() { return nullptr; }
bool  dragActive() { return false; }
bool processMessage(UIMessage&) { return false; } void* objectFromPath(const char*, int) { return nullptr; }
void* focusedLeaf() { return nullptr; } void popContextWidgets(void*) {} void abortDrag(void*) {}
bool isA(void*, int) { return false; } const char* typeName(void*) { return ""; }
bool setProperty(void*, uint32_t, const uint16_t*) { return false; } bool getProperty(void*, uint32_t, std::u16string&) { return false; }
void* getChild(void*, const char*) { return nullptr; } bool addChild(void*, void*) { return false; } bool removeChild(void*, void*) { return false; }
bool moveChild(void*, void*, int) { return false; } void link(void*) {} void* duplicateObject(void*) { return nullptr; }
void setVisible(void*, bool) {} bool canSelect(void*) { return false; } void* widgetFromPoint(void*, int, int, bool) { return nullptr; }
void pack(void*) {} void* objectFromPathRel(void*, const char*) { return nullptr; } void setLocation(void*, int, int) {} void setSize(void*, int, int) {}
void* parentOf(void*) { return nullptr; } UIPoint locationOf(void*) { UIPoint p = {0, 0}; return p; } UISize sizeOf(void*) { UISize s = {0, 0}; return s; }
UIPoint scrollLocationOf(void*) { UIPoint p = {0, 0}; return p; } uint32_t flagsOf(void*) { return 0; } const char* nameOf(void*) { return ""; }
void* ioWin() { return nullptr; } void warpCursor(int, int) {} bool performAction(const char*, const uint16_t*) { return false; }
namespace toolbar {
// Nothing to read a layout off in the mock - a real CuiDragInfo only exists in the
// client. Stub for the linker.
void dumpItem(int, int) {}
bool setCommandItem(int, int, const char*, const char*) { return false; }
void saveSettings() {}
} // namespace toolbar
namespace scheme { const char* current() { return ""; } bool reset(const char*) { return false; } }
namespace toolbar {
// One tag per cell, 0 = empty. 'item' is the address of the cell itself, so setItem
// copies a tag where the client copies a CuiDragInfo.
static void* g_self = nullptr;
static const int MOCK_PANES = 6, MOCK_SLOTS = 24;
static int g_items[MOCK_PANES][MOCK_SLOTS];
void  setSelf(void* m) { g_self = m; }
void* self() { return g_self; }
static bool inRange(int p, int s) { return p >= 0 && p < MOCK_PANES && s >= 0 && s < MOCK_SLOTS; }
// As in the real thing: no mediator caught, no access to items.
void* getItem(int pane, int slot) { return (g_self && inRange(pane, slot)) ? &g_items[pane][slot] : nullptr; }
bool  setItem(int pane, int slot, void* item)
{
    if (!g_self || !inRange(pane, slot) || !item) return false;
    g_items[pane][slot] = *static_cast<int*>(item);
    return true;
}
void  mockReset() { for (int p = 0; p < MOCK_PANES; ++p) for (int s = 0; s < MOCK_SLOTS; ++s) g_items[p][s] = 0; }
void  mockPut(int pane, int slot, int tag) { if (inRange(pane, slot)) g_items[pane][slot] = tag; }
int   mockTag(int pane, int slot) { return inRange(pane, slot) ? g_items[pane][slot] : 0; }
}
namespace game {
// Mock: the test supplies the 'foes' and we remember the choice. The whole ring logic
// is tested without touching the client.
static void* g_foes[32]; static std::u16string g_names[32]; static int g_count = 0;
static void* g_lastTarget = nullptr;
void  mockSetFoes(void** objs, const char16_t* const* names, int n)
{
    g_count = n > 32 ? 32 : (n < 0 ? 0 : n);
    for (int i = 0; i < g_count; ++i) { g_foes[i] = objs[i]; g_names[i] = names[i]; }
}
void* mockLastTarget() { return g_lastTarget; }
void  mockReset() { g_count = 0; g_lastTarget = nullptr; }
void* playerCreature() { return reinterpret_cast<void*>(1); }
float targetingRange() { return 128.f; }
void* asClientObject(void* o) { return o; }
int   attackableAround(void** out, int max)
{
    int n = g_count < max ? g_count : max;
    for (int i = 0; i < n; ++i) out[i] = g_foes[i];
    return n;
}
int   peacefulAround(void** out, int max, float) { return attackableAround(out, max); }
int   anythingAround(void** out, int max, float) { return attackableAround(out, max); }
bool  localizedName(void* o, std::u16string& out)
{
    for (int i = 0; i < g_count; ++i) if (g_foes[i] == o) { out = g_names[i]; return true; }
    return false;
}
bool  positionOf(void*, float xyz[3]) { xyz[0] = xyz[1] = xyz[2] = 0.f; return true; }
static std::u16string g_group[8]; static int g_groupN = 0;
void  mockSetGroup(const char16_t* const* names, int n)
{
    g_groupN = n > 8 ? 8 : (n < 0 ? 0 : n);
    for (int i = 0; i < g_groupN; ++i) g_group[i] = names[i];
}
uint32_t pvpFlags(void*) { return 1u; }
Threat threatOf(void* o) { return o ? THREAT_ATTACKABLE : THREAT_NEUTRAL; }
static TargetKind g_kind = TARGET_NONE;
TargetKind targetKind() { return g_kind; }
void  mockSetPeacefulTarget(bool on) { g_kind = on ? TARGET_PEACEFUL : TARGET_NONE; }
int   groupMemberNames(std::u16string* out, int max)
{
    int n = g_groupN < max ? g_groupN : max;
    for (int i = 0; i < n; ++i) out[i] = g_group[i];
    return n;
}
bool  forwardOf(void*, float xyz[3]) { xyz[0] = 0.f; xyz[1] = 0.f; xyz[2] = 1.f; return true; }
void* lookAtTarget(void*) { return g_lastTarget; }
bool  setLookAtTarget(void*, void* object) { g_lastTarget = object; return object != nullptr; }
}
void setLocalText(void*, const uint16_t*) {} bool bindingLabel(const char*, std::u16string&) { return false; }
BinUString::BinUString() { static const uint16_t z = 0; begin = end = &z; cap = &z + 1; } BinUString::~BinUString() {}
static inline uint16_t rdw(uint32_t va) { const unsigned char* p = rd(va, 2); return p ? *reinterpret_cast<const uint16_t*>(p) : 0; }
#endif

uint16_t keyEscape()   { return rdw(UIMessage_key_Escape); }
uint16_t keyEnter()    { return rdw(UIMessage_key_Enter); }
uint16_t keyTab()      { return rdw(UIMessage_key_Tab); }
uint16_t keySpace()    { return rdw(UIMessage_key_Space); }
uint16_t keyUp()       { return rdw(UIMessage_key_UpArrow); }
uint16_t keyDown()     { return rdw(UIMessage_key_DownArrow); }
uint16_t keyLeft()     { return rdw(UIMessage_key_LeftArrow); }
uint16_t keyRight()    { return rdw(UIMessage_key_RightArrow); }
uint16_t keyPageUp()   { return rdw(UIMessage_key_PageUp); }
uint16_t keyPageDown() { return rdw(UIMessage_key_PageDown); }
uint16_t keyHome()     { return rdw(UIMessage_key_Home); }
uint16_t keyEnd()      { return rdw(UIMessage_key_End); }
uint16_t keyBackSpace() { return rdw(UIMessage_key_BackSpace); }

}} // namespace cp::bin
