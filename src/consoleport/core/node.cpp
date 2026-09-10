#include "node.h"
#include "../abi/props.h"
#include "binary.h"
#include "runtime.h"     // tracef
#include <cstdio>
#include <cstdlib>
#ifndef CP_MOCK_BINARY
#include <windows.h>
#endif

namespace cp {

#ifndef CP_MOCK_BINARY
// Never read the client's memory blind: a wrong offset takes the list walk into garbage
// and the client dies on a call through a random pointer. Every page is checked mapped
// and readable before it is dereferenced.
static bool readable(const void* p, size_t n)
{
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & ok) || (mbi.Protect & PAGE_GUARD)) return false;
    const char* end = static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
    return static_cast<const char*>(p) + n <= end;
}

// Walk an STLport std::list from its sentinel: node = {next, prev, data}. Returns the
// element count if the list closed on the sentinel, else -1.
static int walkList(void** sentinel, std::vector<void*>* out, int limit)
{
    if (!readable(sentinel, sizeof(void*) * 2)) return -1;
    int n = 0;
    for (void** p = static_cast<void**>(sentinel[0]); p != sentinel; ++n) {
        if (n > limit || !readable(p, sizeof(void*) * 3)) return -1;
        if (out) out->push_back(p[2]);
        p = static_cast<void**>(p[0]);
    }
    return n;
}

// One-off diagnostics: which offsets look like the head of the children list.
void probeChildList(void* h)
{
    if (!readable(h, 0x200)) { tracef("probe: object is unreadable"); return; }
    char* base = static_cast<char*>(h);
    for (int off = 0x40; off <= 0x220; off += 4) {
        if (!readable(base + off, sizeof(void*))) continue;
        void** cand = *reinterpret_cast<void***>(base + off);   // mObjects is a pointer to the list
        if (!cand) continue;
        // A: the head is in the UIObjectList object itself (node by value)
        int n = walkList(cand, nullptr, 4096);
        if (n > 0) tracef("probe: 0x%X (A) -> list of %d", off, n);
        // B: STLport keeps a pointer to the sentinel - one more dereference
        if (readable(cand, sizeof(void*))) {
            void** node = static_cast<void**>(cand[0]);
            if (node) {
                int m = walkList(node, nullptr, 4096);
                if (m > 0) tracef("probe: 0x%X (B) -> list of %d", off, m);
            }
        }
    }
}
#else
void probeChildList(void*) {}
#endif

UIPoint Node::worldLocation() const
{
    UIPoint p = location();
    for (NodePtr q = parent(); q; q = q->parent()) {
        UIPoint l = q->location(); UIPoint s = q->scrollLocation();
        p.x += l.x - s.x; p.y += l.y - s.y;
    }
    return p;
}

UIRect Node::worldRect() const
{
    UIPoint p = worldLocation(); UISize s = size();
    UIRect r = {p.x, p.y, p.x + s.x, p.y + s.y};
    return r;
}

UIPoint Node::center() const
{
    UIRect r = worldRect(); UIPoint c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    return c;
}

bool Node::press() { return isA(T_Button) && setPropA(abi::PROP_Press, ""); }

bool Node::setPropA(uint32_t prop, const char* ascii)
{
    char16_t buf[256]; size_t n = 0;
    while (ascii[n] && n < 255) { buf[n] = static_cast<char16_t>(static_cast<unsigned char>(ascii[n])); ++n; }
    buf[n] = 0;
    return setProp(prop, buf);
}

// Numeric properties are STRINGS to the client: SetProperty and GetProperty parse them
// themselves (CuiWidget3dObjectListViewer::SetProperty -> setCameraYaw), so there is no
// separate float slot to look for in the vtable.
bool Node::getPropF(uint32_t prop, float& out) const
{
    std::u16string s;
    if (!getProp(prop, s)) return false;
    char buf[64]; size_t n = 0;
    for (char16_t ch : s) { if (n + 1 >= sizeof buf) break; buf[n++] = ch < 128 ? static_cast<char>(ch) : '?'; }
    buf[n] = 0;
    char* end = nullptr;
    const float v = std::strtof(buf, &end);
    if (end == buf) return false;
    out = v; return true;
}

bool Node::setPropF(uint32_t prop, float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
    return setPropA(prop, buf);
}

// ------------------------------------------------------------ BinaryNode
std::string BinaryNode::name() const     { return bin::nameOf(h_); }
std::string BinaryNode::typeName() const { const char* t = bin::typeName(h_); return t ? t : ""; }
bool BinaryNode::isA(int tag) const      { return bin::isA(h_, tag); }
NodePtr BinaryNode::parent() const       { return wrap(bin::parentOf(h_)); }

std::vector<NodePtr> BinaryNode::children() const
{
    // STLport std::list: [page+0x104] -> sentinel; node = {next, prev, data}
    std::vector<NodePtr> out;
#ifdef CP_MOCK_BINARY
    if (!bin::isA(h_, T_Page)) return out;
#endif
#ifndef CP_MOCK_BINARY
    // No 'pages only' gate: toolbar cells are not UIPage and their children still have
    // to be read. Safety comes from the walk, not the type - the list must close on its
    // sentinel or we report no children. UIPage::mObjects is a POINTER to a
    // UIObjectList, and the list itself keeps a pointer to the sentinel: two
    // dereferences, not one. Taking the list object for the sentinel sent the walk into
    // garbage and up against the 4096 guard (probe on the live client: 0x104 with the
    // double dereference gives a closed list of 42 children).
    void** listObj = *reinterpret_cast<void***>(reinterpret_cast<char*>(h_) + abi::OFF_UIPage_children);
    if (!listObj || !readable(listObj, sizeof(void*))) return out;
    void** sentinel = static_cast<void**>(listObj[0]);
    if (!sentinel) return out;
    // The list has to close on the sentinel; if it does not, no children.
    std::vector<void*> raw;
    if (walkList(sentinel, &raw, 4096) < 0) return out;
    for (void* obj : raw) if (obj) out.push_back(wrap(obj));
#else
    void** sentinel = *reinterpret_cast<void***>(reinterpret_cast<char*>(h_) + abi::OFF_UIPage_children);
    if (!sentinel) return out;
    int guard = 0;
    for (void** n = static_cast<void**>(sentinel[0]); n && n != sentinel && guard < 4096; n = static_cast<void**>(n[0]), ++guard) {
        void* obj = n[2];
        if (obj) out.push_back(wrap(obj));
    }
#endif
    return out;
}

NodePtr BinaryNode::child(const char* n) const  { return wrap(bin::getChild(h_, n)); }
NodePtr BinaryNode::byPath(const char* p) const { return wrap(bin::objectFromPathRel(h_, p)); }
UIPoint BinaryNode::location() const       { return bin::locationOf(h_); }
UISize BinaryNode::size() const            { return bin::sizeOf(h_); }
UIPoint BinaryNode::scrollLocation() const { return bin::scrollLocationOf(h_); }
uint32_t BinaryNode::flags() const         { return bin::flagsOf(h_); }
bool BinaryNode::canSelect() const         { return bin::canSelect(h_); }
void BinaryNode::setLocation(int x, int y) { bin::setLocation(h_, x, y); }
void BinaryNode::setSize(int w, int h)     { bin::setSize(h_, w, h); }
void BinaryNode::setVisible(bool v)        { bin::setVisible(h_, v); }
void BinaryNode::setLocalText(const char16_t* t) { bin::setLocalText(h_, reinterpret_cast<const uint16_t*>(t)); }
bool BinaryNode::setProp(uint32_t prop, const char16_t* v) { return bin::setProperty(h_, prop, reinterpret_cast<const uint16_t*>(v)); }
bool BinaryNode::getProp(uint32_t prop, std::u16string& out) const { return bin::getProperty(h_, prop, out); }
NodePtr BinaryNode::clone()                { return wrap(bin::duplicateObject(h_)); }
bool BinaryNode::addChild(const NodePtr& c)    { return c && bin::addChild(h_, c->handle()); }
bool BinaryNode::removeChild(const NodePtr& c) { return c && bin::removeChild(h_, c->handle()); }
bool BinaryNode::moveChild(const NodePtr& c, int d) { return c && bin::moveChild(h_, c->handle(), d); }
void BinaryNode::link()                    { bin::link(h_); }
void BinaryNode::pack()                    { bin::pack(h_); }

NodePtr findFirst(const NodePtr& root, int tag, int maxDepth)
{
    if (!root) return NodePtr();
    std::vector<std::pair<NodePtr, int>> q; q.push_back(std::make_pair(root, 0));
    for (size_t i = 0; i < q.size() && i < 4096; ++i) {
        NodePtr n = q[i].first; int d = q[i].second;
        if (n->isA(tag) && n != root) return n;
        if (d >= maxDepth) continue;
        for (const NodePtr& c : n->children()) q.push_back(std::make_pair(c, d + 1));
    }
    return NodePtr();
}

} // namespace cp
