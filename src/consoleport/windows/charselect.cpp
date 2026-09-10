#include "charselect.h"
#include "../abi/props.h"
#include "../core/runtime.h"
#include "../crossbar/crossbar.h"
#include <cstdio>
#include <cstring>

namespace cp { namespace windows {

using namespace gen;
using namespace cursor;

namespace {

// Stick deflection gets its own repeat, as Stack::dispatch does in game; the D-pad is
// the cursor's business (Cursor::input) and has its own delays.
const float STICK_FIRST = 0.35f, STICK_NEXT = 0.12f;
const float STICK_ON = 0.5f, STICK_OFF = 0.35f;
// Rotating the model with the right stick: radians per second at full deflection.
const float YAW_SPEED = 2.0f;
const float YAW_DEAD = 0.25f;

bool sameName(const std::string& a, const char* b)
{
    return a.size() == std::strlen(b) && _strnicmp(a.c_str(), b, a.size()) == 0;
}

void findNamedRec(const NodePtr& n, const char* name, int depth, NodePtr& out)
{
    if (out || !n || depth < 0 || !n->willDraw()) return;
    for (const NodePtr& k : n->children()) {
        if (out) return;
        if (!k->willDraw()) continue;
        if (sameName(k->name(), name)) { out = k; return; }
        findNamedRec(k, name, depth - 1, out);
    }
}

// By name, with no visibility check - the delete dialog is looked up precisely to find
// out whether it is shown.
void findAnyRec(const NodePtr& n, const char* name, int depth, NodePtr& out)
{
    if (out || !n || depth < 0) return;
    for (const NodePtr& k : n->children()) {
        if (out) return;
        if (sameName(k->name(), name)) { out = k; return; }
        findAnyRec(k, name, depth - 1, out);
    }
}

void findTypeRec(const NodePtr& n, const char* type, int depth, NodePtr& out)
{
    if (out || !n || depth < 0 || !n->willDraw()) return;
    for (const NodePtr& k : n->children()) {
        if (out) return;
        if (!k->willDraw()) continue;
        if (sameName(k->typeName(), type)) { out = k; return; }
        findTypeRec(k, type, depth - 1, out);
    }
}

void findTagRec(const NodePtr& n, int tag, int depth, NodePtr& out)
{
    if (out || !n || depth < 0 || !n->willDraw()) return;
    for (const NodePtr& k : n->children()) {
        if (out) return;
        if (!k->willDraw()) continue;
        if (k->isA(tag)) { out = k; return; }
        findTagRec(k, tag, depth - 1, out);
    }
}

// Really drawn: a child's own flag stays raised under a hidden parent (the delete
// dialog is muted through an outer page), so the whole chain is checked.
bool drawsWithin(const NodePtr& n, const NodePtr& top)
{
    for (NodePtr p = n; p; p = p->parent()) {
        if (!p->willDraw()) return false;
        if (top && p->handle() == top->handle()) break;
    }
    return true;
}

// Stick deflection with hysteresis: -1 is nowhere, the rest as Dir - 0 up, 1 down,
// 2 left, 3 right.
int stickDirOf(const input::Stick& s, bool held)
{
    const float t = held ? STICK_OFF : STICK_ON;
    const float ax = s.x < 0 ? -s.x : s.x, ay = s.y < 0 ? -s.y : s.y;
    if (ax < t && ay < t) return -1;
    if (ay >= ax) return s.y < 0 ? 0 : 1;
    return s.x < 0 ? 2 : 3;
}

const Dir DIRS[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
const Button DPAD[4] = { B_DPAD_UP, B_DPAD_DOWN, B_DPAD_LEFT, B_DPAD_RIGHT };

} // namespace

CharSelect::CharSelect(NodePtr root, Ui& ui) : root_(root), ui_(ui), cur_(ui)
{
    // Scrollbar and column header are not navigation: their buttons would clog the
    // frame with stops around the same list.
    rules_.pass.push_back("scroll");
    rules_.pass.push_back("header");
    rules_.pass.push_back("deleteConfirmationBackground");
    rules_.maxDepth = 8;
    // Start on the list - it is the point of the screen.
    rules_.priority["table"] = 10;
    hints_.setPrefix("cpAvSel");
}

NodePtr CharSelect::findNamed(const NodePtr& page, const char* name, int depth)
{
    NodePtr out;
    findNamedRec(page, name, depth, out);
    return out;
}

// The screen is recognized by signature, not by name: the name belongs to the server
// build, the panels and buttons outlive a rename.
//
// The order of the checks matters more than their contents - cheap ones first, the name
// or two direct panels. Otherwise entering the world (a frame or two before the crossbar
// attaches) would walk the whole GroundHUD looking for a button that is not there.
bool CharSelect::looksLikeCharSelect(const NodePtr& p) const
{
    if (!p || !p->isA(T_Page) || !p->willDraw()) return false;
    UISize s = p->size();
    if (s.x < 200 || s.y < 200) return false;
    if (!sameName(p->name(), "AvSel") && !(p->child("pageSelection") && p->child("pageButtonsBottom")))
        return false;
    return findNamed(p, "buttonNext") && findNamed(p, "buttonCreate");
}

NodePtr CharSelect::page() const
{
    if (!root_) return NodePtr();
    NodePtr guess;
    for (const NodePtr& k : root_->children()) {
        if (!k->isA(T_Page) || !k->willDraw()) continue;
        if (sameName(k->name(), "AvSel") && looksLikeCharSelect(k)) return k;
        if (!guess && looksLikeCharSelect(k)) guess = k;
    }
    return guess;
}

NodePtr CharSelect::table() const
{
    NodePtr p = page(); if (!p) return NodePtr();
    // The binary's table type is not the sources' enum value (the binary is older), but
    // the widget reports its type NAME - so we match on that, not on the tag.
    NodePtr t; findTypeRec(p, "Table", 6, t);
    if (!t) t = p->byPath("pageSelection.pageTable.table");
    return t;
}

NodePtr CharSelect::viewer() const
{
    NodePtr p = page(); if (!p) return NodePtr();
    NodePtr v; findTagRec(p, T_3DViewer, 4, v);
    if (!v) v = findNamed(p, "viewer", 4);
    return v;
}

NodePtr CharSelect::button(const char* name) const
{
    NodePtr p = page(); if (!p) return NodePtr();
    NodePtr b = findNamed(p, name);
    return b && b->isA(T_Button) ? b : NodePtr();
}

// The delete dialog wants the character NAME typed, and its OK hangs on the same Enter
// as Next. While it is open only circle works - the cancel.
bool CharSelect::blocked() const
{
    NodePtr p = page(); if (!p) return false;
    static const char* const NAMES[] = { "deleteConfirmationBackground", "deleteConfirmation" };
    for (const char* n : NAMES) {
        NodePtr d; findAnyRec(p, n, 4, d);
        if (d && drawsWithin(d, p)) return true;
    }
    return false;
}

bool CharSelect::onList() const
{
    const NodeInfo* n = cur_.current();
    return n && n->kind == NK_List;
}

// With no frame (markup not installed) the screen behaves as it did before it: D-pad
// walks the rows, cross enters the world. An update must not break what worked.
bool CharSelect::listMode() const { return !attached_ || onList(); }

std::vector<Hint> CharSelect::hints() const
{
    std::vector<Hint> h;
    if (blocked()) {
        // The name is typed on our own keyboard, the one on the screen page. Nothing
        // else to do in that dialog.
        h.push_back(Hint{B_TRIANGLE, u"Type"});
        h.push_back(Hint{B_CIRCLE, u"Cancel"});
        return h;
    }
    // On the list the D-pad and the stick do different things, and the legend has to
    // say so - otherwise there is no visible way out to the buttons.
    const bool list = onList();
    h.push_back(Hint{B_DPAD_UP, list ? u"Rows" : u"", crossbar::Glyph::DPAD});
    h.push_back(Hint{B_L3, u"Move", crossbar::Glyph::STICK_L});
    h.push_back(Hint{B_CROSS, list ? u"Enter world" : u"Press"});
    h.push_back(Hint{B_CIRCLE, u"Back"});
    h.push_back(Hint{B_SQUARE, u"Create"});
    h.push_back(Hint{B_TRIANGLE, u"Type"});
    h.push_back(Hint{B_R3, u"Turn", crossbar::Glyph::STICK_R});
    return h;
}

std::vector<std::string> CharSelect::survey() const
{
    std::vector<std::string> out;
    NodePtr p = page();
    if (!p) { out.push_back("charselect: no screen"); return out; }
    char buf[256];
    NodePtr t = table(), v = viewer();
    std::snprintf(buf, sizeof buf, "charselect: page %s, list %s, model %s",
                  p->name().c_str(),
                  t ? t->typeName().c_str() : "MISSING",
                  v ? v->typeName().c_str() : "MISSING");
    out.push_back(buf);
    static const char* const BUTTONS[] = { "buttonNext", "buttonPrev", "buttonCreate", "buttonDelete" };
    std::string line = "charselect: buttons";
    for (const char* n : BUTTONS) {
        NodePtr b = button(n);
        line += ' '; line += n; line += '=';
        line += !b ? "MISSING" : b->enabled() ? "present" : "disabled";
    }
    out.push_back(line);
    return out;
}

// Templates for the frame and the glyphs. The HUD first - if its tree is already built
// the screen markup need not be installed at all - then ours, from
// ui_consoleport_charselect.inc inside the page.
void CharSelect::attach(const NodePtr& p)
{
    NodePtr img, txt, frame;
    NodePtr hudTpl = nodeByPath("/GroundHUD.cpDebug");
    if (hudTpl) img = hudTpl->child("cpGlyphTemplate");
    const char* src = img ? "HUD" : "the screen";
    NodePtr own = p->child("cpTpl");
    if (own) {
        if (!img) img = own->child("cpGlyphTemplate");
        txt = own->child("cpTextTemplate");
        frame = own->child("cpFrameTemplate");
    }
    // Last resort for a text: the big character name caption. HintBar overrides the
    // clone's font and size anyway and only needs the widget type.
    if (!txt) txt = findNamed(p, "textName", 3);
    if (!frame) {
        tracef("charselect: no templates (ui_consoleport_charselect.inc is not installed) - "
               "there will be no frame and no hints, the buttons work");
        return;
    }
    cur_.attachVisuals(p, img, frame);
    hints_.attach(p, img, txt, 0, 0);
    // A keyboard of its own: the in-world one lives on GroundHUD, which this screen has
    // not got. Parent is the screen page, templates the same three.
    kb_.attach(p, frame, txt, img);
    cur_.setRoot(p, rules_);
    attached_ = true;
    tracef("charselect: frame and hints in place (glyph from %s, %d nodes)",
           src, (int)cur_.nodeCount());
}

void CharSelect::detach()
{
    if (!attached_) return;
    kb_.close(&ui_); kb_.detach();
    hints_.detach();
    cur_.clearRoot();
    cur_.detachVisuals();
    attached_ = false;
    rowHeld_ = 0; stickDir_ = -1; wasBlocked_ = false;
}

// The hint bar sits centred above the screen's button row: the legend is about the whole
// screen, and the bottom is where the eye looks for it.
void CharSelect::placeHints(const NodePtr& p)
{
    UISize sz = p->size();
    NodePtr bottom = p->child("pageButtonsBottom");
    const int y = bottom ? bottom->location().y - hints_.glyphSize() - 12
                         : sz.y - hints_.glyphSize() - 96;
    hints_.moveTo((sz.x - hints_.contentWidth()) / 2, y);
}

void CharSelect::rowArrow(int dir)
{
    NodePtr t = table();
    if (!t) return;
    // Focus before EVERY arrow: the client may have handed it to a button (a click, or
    // its own SetFocus after a repopulate) and the arrow would go elsewhere. One
    // property per press is cheaper than tracking focus.
    t->setPropA(abi::PROP_Focus, "true");
    ui_.keyTap(dir < 0 ? K_Up : K_Down);
}

// One frame step. A deflection is not an edge, so this fires once per press.
void CharSelect::ringStep(int dir)
{
    cur_.input(DIRS[dir], true);
    cur_.input(DIRS[dir], false);
}

// On the list the D-pad walks the ROWS and the stick always moves the frame.
//
// Not cosmetic: make both do the same and the only way out of the list is left-right,
// which the legend cannot show. It also matches the desktop in game - the stack sends
// stick deflection straight to the cursor while a list takes the D-pad only.
void CharSelect::navigate(input::Pad& pad, float dt)
{
    for (int i = 0; i < 4; ++i) {
        const bool rows = listMode() && i < 2;
        if (pad.pressed(DPAD[i])) {
            if (rows) { rowArrow(i == 0 ? -1 : 1); rowHeld_ = i == 0 ? -1 : 1; rowRepeat_ = cur_.config().repeatFirst; }
            else cur_.input(DIRS[i], true);
        }
        if (pad.released(DPAD[i])) {
            if (i < 2) rowHeld_ = 0;
            cur_.input(DIRS[i], false);
        }
    }
    if (rowHeld_) {
        rowRepeat_ -= dt;
        while (rowRepeat_ <= 0) { rowArrow(rowHeld_); rowRepeat_ += cur_.config().repeat; }
    }
    // The left stick always moves the frame: own threshold and repeat.
    const int d = stickDirOf(pad.leftStick(), stickDir_ >= 0);
    if (d < 0) stickDir_ = -1;
    else if (d != stickDir_) { stickDir_ = d; stickWait_ = STICK_FIRST; ringStep(d); }
    else if ((stickWait_ -= dt) <= 0) { stickWait_ = STICK_NEXT; ringStep(d); }
}

bool CharSelect::pressNamed(const char* name, const char* what)
{
    NodePtr b = button(name);
    if (!b || !b->enabled()) { tracef("charselect: %s - there is no %s button", what, name); return false; }
    const bool ok = b->press();
    tracef("charselect: %s (%s)", what, ok ? "pressed" : "did NOT press");
    return ok;
}

void CharSelect::rotate(float x, float dt)
{
    if (noYaw_) return;
    NodePtr v = viewer();
    if (!v) return;
    float yaw = 0;
    if (!v->getPropF(abi::PROP_CameraYaw, yaw)) {
        noYaw_ = true;
        tracef("charselect: the viewer did not report CameraYaw, rotation is off");
        return;
    }
    v->setPropF(abi::PROP_CameraYaw, yaw + x * YAW_SPEED * dt);
}

bool CharSelect::update(input::Pad& pad, float dt)
{
    NodePtr p = page();
    void* h = p ? p->handle() : nullptr;
    if (h != curHandle_) {
        detach();
        curHandle_ = h; noYaw_ = false;
        if (p) {
            for (const std::string& l : survey()) tracef("%s", l.c_str());
            attach(p);
        }
    }
    if (!p) return false;

    const bool block = blocked();
    if (block != wasBlocked_) {
        wasBlocked_ = block;
        rowHeld_ = 0; stickDir_ = -1;
        tracef("charselect: the delete dialog %s", block ? "is open, only cancel works" : "is closed");
    }

    // While typing the pad is the keyboard's. There is no window stack here, so the
    // buttons are handed out by us, in Stack::dispatch's set.
    if (kb_.isOpen()) {
        cursor::Context ctx{cur_, ui_, pad, hints_};
        static const gen::Button KEYS[] = {
            B_DPAD_UP, B_DPAD_DOWN, B_DPAD_LEFT, B_DPAD_RIGHT,
            B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1, B_R1, B_L2, B_R2, B_L3, B_R3
        };
        for (gen::Button b : KEYS) {
            if (pad.pressed(b)) kb_.onButton(ctx, b, true);
            if (pad.released(b)) kb_.onButton(ctx, b, false);
        }
        kb_.onTick(ctx, dt);
        if (attached_) { cur_.tick(dt); hints_.tick(dt); }
        return true;
    }
    // Triangle opens the keyboard where there is something to type into: in the delete
    // dialog the client keeps focus in the textbox, so no target is named and we type
    // into whatever has focus.
    if (pad.pressed(B_TRIANGLE) && kb_.attached()) {
        kb_.open(NodePtr(), false, true);      // there is no chat here - bottom center
        tracef("charselect: the on-screen keyboard is open%s", block ? " (delete dialog)" : "");
    }

    if (!block) navigate(pad, dt);

    if (pad.pressed(B_CROSS) && !block) {
        // On the list cross enters the world, as a double click does: UITable
        // synthesizes Enter, and Enter is IsDefaultButton, i.e. Next.
        if (listMode()) pressNamed("buttonNext", "entering the world");
        else if (!cur_.activate()) tracef("charselect: the node did not press");
    }
    if (pad.pressed(B_CIRCLE)) {
        if (block) pressNamed("buttonCancel", "cancelling the deletion");
        else pressNamed("buttonPrev", "back to the galaxies");
    }
    if (pad.pressed(B_SQUARE) && !block) pressNamed("buttonCreate", "creating a character");

    const float rx = pad.rightStick().x;
    if (!block && (rx > YAW_DEAD || rx < -YAW_DEAD)) rotate(rx, dt);

    if (attached_) {
        cur_.tick(dt);
        hints_.set(hints());
        placeHints(p);
        hints_.tick(dt);
    }
    return true;
}

}} // namespace cp::windows
