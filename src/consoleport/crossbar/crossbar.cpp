#include "crossbar.h"
#include "../core/theme.h"
#include "../abi/props.h"
#include "../core/runtime.h"   // cp::tracef - the test rig trace
#include "../core/binary.h"    // reading properties back for the trace
#include "../core/ui.h"        // the client's normal drag and drop
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cp { namespace crossbar {

using namespace gen;

static const char* g_atlasName = "consoleport_glyphs";
const char* Glyph::atlas() { return g_atlasName; }
void Glyph::setFamily(const char* profile)
{
    const bool steam = profile && std::strcmp(profile, "steam") == 0;
    g_atlasName = steam ? "consoleport_glyphs_xbox" : "consoleport_glyphs";
}

Glyph::Key Glyph::forButton(Button b)
{
    switch (b) {
    case B_DPAD_UP: return DPAD_UP; case B_DPAD_LEFT: return DPAD_LEFT; case B_DPAD_RIGHT: return DPAD_RIGHT; case B_DPAD_DOWN: return DPAD_DOWN;
    case B_TRIANGLE: return FACE_TOP; case B_SQUARE: return FACE_LEFT; case B_CIRCLE: return FACE_RIGHT; case B_CROSS: return FACE_BOTTOM;
    case B_L1: return SH_L; case B_R1: return SH_R; case B_L2: return TR_L; case B_R2: return TR_R;
    case B_L3: return L3; case B_R3: return R3;
    // No atlas art for Share/Options/PS/touchpad. Falling back to the cross glyph drew
    // three identical crosses on Select/Open/Close (9 Sep 2026), so those are left out.
    default: return FACE_BOTTOM;
    }
}

// Palette entries (core/theme.h), not shades of our own: the player switches theme in
// the options and the bar has to follow. The backdrop stays neutral - it is shared by a
// whole cross of differently coloured face buttons.
static const char COLOR_FRAME[] = "line1";
static const char COLOR_GLYPH[] = "icondefault";
// Context on the right cross: a green backdrop says the faces are about the target, not
// about slots. The client's own "good"/"bad" entries, the ones the ring rims use.
static const char COLOR_CTX_FRAME[] = "IconPositive";
static const char COLOR_CTX_GLYPH[] = "text2";     // near-white: a pale one drowned on the green frame
// Hostile target: the same backdrop, reddish. The faces are left alone.
static const char COLOR_HOSTILE_FRAME[] = "IconNegative";

Crossbar::Crossbar(const Metrics& m) : m_(m), bar_(barLayout(m))
{
    for (int i = 0; i < SLOT_COUNT; ++i) { last_[i] = UIRect{0, 0, 0, 0}; shown_[i] = false; }
}

const char* Crossbar::slotCommand(int slot, char* buf, size_t n) { std::snprintf(buf, n, "CMD_uiToolbarSlot%02d", slot); return buf; }

static void fmt2(char* buf, int a, int b) { std::snprintf(buf, 32, "%d,%d", a, b); }

void Crossbar::setRect(const NodePtr& n, int x, int y, int w, int h)
{
    char b[32];
    // Constraints off first, size second. Pinning min/max to the wanted size in one
    // pass left the cell clamped by the markup's old MaximumSize whenever the write did
    // not land: you order 44 and get 30. 16384 is the UI's "no constraint" sentinel.
    n->setPropA(abi::PROP_MinimumSize, "0,0");
    n->setPropA(abi::PROP_MaximumSize, "16384,16384");
    fmt2(b, w, h);
    n->setSize(w, h); n->setPropA(abi::PROP_ScrollExtent, b);
    n->setLocation(x, y);
}

// The client owns the cell size - our setSize does not reach it (order 44, get 30). So
// the geometry is built from the real size, or the cross sprawls inside an oversized
// backdrop.
void Crossbar::adaptToCell(int c)
{
    if (c < 12 || c > 128) return;            // garbage - we keep our own sizes
    m_.main = c;
    m_.arm = c + 2;                           // the arm is no shorter than a cell, or the plus corners overlap
    // Flyout cells come out the same size as the main ones, so the step is computed
    // from the full size too - otherwise they climb over each other.
    m_.fly = c;
    m_.fanStep = c + 4;
    m_.glyph = c;                             // it fits whole into the cross's free center
    m_.extra = c;
    m_.extraGlyph = (c * 2) / 3;
    m_.modGlyph = c;
    m_.fanBadge = (c * 5) / 9;
    bar_ = barLayout(m_);
}

// Flyout cells outside in: 0 L2, 1 L2+R2, 2 R2, as in fanRect. Each reads the pane its
// hold leads to.
static const int FAN_PANE[3] = {PANE_L2, PANE_L2R2, PANE_R2};
// Layer glyphs per flyout cell, -1 = no second one. The middle cell is L2+R2 and needs
// both, placed on the diagonal so they stay full size even on a 30 px cell.
static const int FAN_BADGE[3][2] = {{Glyph::TR_L, -1}, {Glyph::TR_L, Glyph::TR_R}, {Glyph::TR_R, -1}};
// Receivers: the free toolbar cells right after our ten. Executable and persisted, but
// never shown by the crossbar - so they can serve as the flyout mailboxes.
static const int DROP_SLOT[3] = {10, 11, 12};

void Crossbar::setVisible(const NodePtr& n, bool v)
{
    if (!n) return;
    if (n->willDraw() != v) n->setVisible(v);
}

bool Crossbar::attach(NodePtr toolbar, NodePtr imageTemplate, NodePtr textTemplate, NodePtr frameTemplate)
{
    detach();
    if (!toolbar) return false;
    NodePtr vol = toolbar->child("volume");
    if (!vol) return false;
    toolbar_ = toolbar; volume_ = vol; imageTpl_ = imageTemplate; textTpl_ = textTemplate; frameTpl_ = frameTemplate;
    // CellMax first: without it neither we nor the client can put a cell in the volume
    // at all (see the note at DoNotPackChildren below).
    vol->setPropA(abi::PROP_CellMax, "16384");
    if (!refreshCells()) { detach(); return false; }
    adaptToCell(cell_[0]->size().x);
    cp::tracef("bar.attach: volume %p, %d children, cell %d -> arm %d, bar %dx%d",
               vol->handle(), (int)vol->children().size(), m_.main, m_.arm, bar_.size.x, bar_.size.y);

    keyBindings_ = toolbar->child("volumeKeyBindings");
    if (keyBindings_) keyBindings_->setVisible(false);
    cp::tracef("bar.attach: binding labels %s", keyBindings_ ? "found and hidden" : "NOT found");

    // The volume is sized for the whole bar, flyouts included. Pack ignores
    // DoNotPackChildren, hence packguard.
    // Why CellMax is raised, and before the cells: InsertChildAfter refuses at
    // mWidgetCount >= mCellMax, and populateSlot does RemoveChild anyway, so the cell is
    // gone for good - getToolbarItemWidget stops finding it and populateSlot returns on
    // its first line. mWidgetCount is only recomputed in Pack(), which packguard mutes,
    // so the counter freezes at capture (42) and never falls. Every pane change ate a
    // cell and panes 1..3 stayed empty (8 Sep 2026).
    vol->setPropA(abi::PROP_DoNotPackChildren, "true");
    vol->setPropA(abi::PROP_PackSize, "f,f"); vol->setPropA(abi::PROP_PackLocation, "nfn,nfn");
    UIPoint vl = vol->location(); volumeAt_ = vl;
    setRect(vol, vl.x, vl.y, bar_.size.x, bar_.size.y);
    UISize ts = toolbar->size();
    int tw = vl.x + bar_.size.x + 8, th = vl.y + bar_.size.y + 4;
    if (ts.x < tw || ts.y < th) { UIPoint tl = toolbar->location(); setRect(toolbar, tl.x, tl.y, tw > ts.x ? tw : ts.x, th > ts.y ? th : ts.y); }
    cp::tracef("bar.attach: volume geometry set to %dx%d", bar_.size.x, bar_.size.y);

    placeDecor();
    cp::tracef("bar.attach: decor created");
    mod_ = 0; reveal_ = -1; drag_ = false;
    update(0, 0, false);
    cp::tracef("bar.attach: the first update went through");
    // Backdrops last: the last child is drawn at the bottom.
    for (int c = 0; c < CROSS_COUNT; ++c) if (frame_[c]) toolbar->moveChild(frame_[c], 3);
    if (frameExtra_) toolbar->moveChild(frameExtra_, 3);
    cp::tracef("bar.attach: draw order set");
    return true;
}

// Re-take the cells. Needed at capture and after every pane change: populateSlot
// replaces a cell's widget under the same name, so old pointers point at freed memory.
NodePtr Crossbar::restoreCell(int slot)
{
    if (!volume_) return NodePtr();
    NodePtr src;                                   // the donor: any surviving cell
    for (int i = 0; i < SLOT_COUNT && !src; ++i) if (cell_[i]) src = cell_[i];
    if (!src) for (const NodePtr& k : volume_->children()) {
        std::string nm = k->name();
        bool digits = !nm.empty();
        for (char ch : nm) if (ch < '0' || ch > '9') digits = false;
        if (digits && k->isA(T_Widget)) { src = k; break; }
    }
    if (!src) { cp::tracef("restoreCell: no donor, cell %d cannot be brought back", slot); return NodePtr(); }
    NodePtr made = src->clone();
    if (!made) { cp::tracef("restoreCell: the clone failed"); return NodePtr(); }
    char nm[8]; std::snprintf(nm, sizeof nm, "%d", slot);
    made->setPropA(abi::PROP_Name, nm);
    if (!volume_->addChild(made)) { cp::tracef("restoreCell: the volume did not accept cell %d", slot); return NodePtr(); }
    made->link();
    cp::tracef("restoreCell: cell %d restored with a clone %p of donor %s", slot, made->handle(), src->name().c_str());
    return made;
}

bool Crossbar::refreshCells()
{
    if (!volume_) return false;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        char nm[8]; std::snprintf(nm, sizeof nm, "%d", i);
        cell_[i] = volume_->child(nm);
        if (!cell_[i]) {
            // The client finds cells by name too, so "no cell" means the names changed
            // or the list is empty.
            std::string names; int cnt = 0;
            for (const NodePtr& k : volume_->children()) {
                if (cnt < 14) { if (cnt) names += ','; names += k->name(); }
                ++cnt;
            }
            cp::tracef("refreshCells: no cell %d; %d children: %s", i, cnt, names.c_str());
            // A cell eaten by an InsertChildAfter refusal never comes back by itself -
            // populateSlot with no widget returns immediately. We put a clone of a
            // neighbour there under the right name; the client replaces it on its next
            // repopulateSlots.
            cell_[i] = restoreCell(i);
            if (!cell_[i]) return false;
        }
        last_[i] = UIRect{0, 0, 0, 0}; shown_[i] = false;   // the geometry will be laid on again
    }
    cellProbe_ = cell_[0];
    refreshDrops();
    hoverSettle_ = 3;
    // The client never fills the cells above ours (and QoL's third row is dead), so
    // they are hidden.
    for (const NodePtr& k : volume_->children()) {
        std::string nm = k->name(); bool digits = !nm.empty();
        for (char ch : nm) if (ch < '0' || ch > '9') digits = false;
        if (digits && std::atoi(nm.c_str()) >= SLOT_COUNT && k->isA(T_Widget) && k->willDraw())
            k->setVisible(false);
    }
    return true;
}

void Crossbar::detach()
{
    if (toolbar_) {
        for (int c = 0; c < CROSS_COUNT; ++c) {
            if (frame_[c]) toolbar_->removeChild(frame_[c]);
            if (glyphCross_[c]) toolbar_->removeChild(glyphCross_[c]);
        }
        if (frameExtra_) toolbar_->removeChild(frameExtra_);
        for (int e = 0; e < EXTRA_COUNT; ++e) if (glyphExtra_[e]) toolbar_->removeChild(glyphExtra_[e]);
        for (int j = 0; j < 2; ++j) if (glyphMod_[j]) toolbar_->removeChild(glyphMod_[j]);
        for (int i = 0; i < SLOT_COUNT; ++i) if (bindBadge_[i]) toolbar_->removeChild(bindBadge_[i]);
        for (int i = 0; i < 3; ++i) {
            if (fanFrame_[i]) toolbar_->removeChild(fanFrame_[i]);
            for (int j = 0; j < 2; ++j) if (fanBadge_[i][j]) toolbar_->removeChild(fanBadge_[i][j]);
        }
        for (int p = 0; p < PANE_SET_COUNT; ++p)
            for (int i = 0; i < SLOT_COUNT; ++i) if (art_[p][i]) toolbar_->removeChild(art_[p][i]);
        if (volume_) volume_->setPropA(abi::PROP_DoNotPackChildren, "false");
    }
    toolbar_.reset(); volume_.reset(); imageTpl_.reset(); textTpl_.reset(); frameTpl_.reset(); keyBindings_.reset();
    for (int i = 0; i < SLOT_COUNT; ++i) { cell_[i].reset(); bindBadge_[i].reset(); last_[i] = UIRect{0, 0, 0, 0}; shown_[i] = false; }
    for (int c = 0; c < CROSS_COUNT; ++c) { frame_[c].reset(); glyphCross_[c].reset(); }
    frameExtra_.reset(); glyphMod_[0].reset(); glyphMod_[1].reset();
    for (int e = 0; e < EXTRA_COUNT; ++e) glyphExtra_[e].reset();
    for (int i = 0; i < 3; ++i) {
        fanFrame_[i].reset(); fanShown_[i].reset(); drop_[i].reset();
        for (int j = 0; j < 2; ++j) fanBadge_[i][j].reset();
    }
    emptyPane_ = emptySlot_ = -1;
    for (int p = 0; p < PANE_SET_COUNT; ++p)
        for (int i = 0; i < SLOT_COUNT; ++i) art_[p][i].reset();
    mod_ = 0; reveal_ = -1; prevReveal_ = -1; drag_ = false; warmed_ = false;
}

NodePtr Crossbar::makeImage(const char* name, int glyphKey, const char* color, int size)
{
    if (!imageTpl_) return NodePtr();
    NodePtr n = imageTpl_->clone(); if (!n) return NodePtr();
    UIRect r = Glyph::rect(glyphKey); char b[48];
    n->setPropA(abi::PROP_Name, name);
    n->setPropA(abi::PROP_SourceResource, Glyph::atlas());
    std::snprintf(b, sizeof b, "%d,%d,%d,%d", r.left, r.top, r.right, r.bottom); n->setPropA(abi::PROP_SourceRect, b);
    theme::paint(n, theme::ROLE_COLOR, color);
    n->setPropA(abi::PROP_GetsInput, "false"); n->setPropA(abi::PROP_AbsorbsInput, "false");
    n->setPropA(abi::PROP_PackLocation, "nfn,nfn"); n->setPropA(abi::PROP_PackSize, "f,f");
    n->setPropA(abi::PROP_Opacity, "1.00"); n->setPropA(abi::PROP_Visible, "true");
    fmt2(b, size, size); n->setPropA(abi::PROP_ScrollExtent, b); n->setSize(size, size);
    toolbar_->addChild(n); n->link();
    return n;
}

NodePtr Crossbar::makeText(const char* name, int size)
{
    if (!textTpl_) return NodePtr();
    NodePtr n = textTpl_->clone(); if (!n) return NodePtr();
    char b[32];
    n->setPropA(abi::PROP_Name, name);
    n->setPropA(abi::PROP_GetsInput, "false"); n->setPropA(abi::PROP_AbsorbsInput, "false");
    n->setPropA(abi::PROP_PackLocation, "nfn,nfn"); n->setPropA(abi::PROP_PackSize, "f,f");
    n->setPropA(abi::PROP_BackgroundOpacity, "0.60"); n->setPropA(abi::PROP_TextAlignment, "Center");
    n->setPropA(abi::PROP_Font, "bold_11"); n->setPropA(abi::PROP_Visible, "true");
    fmt2(b, size * 3, size); n->setPropA(abi::PROP_ScrollExtent, b); n->setSize(size * 3, size);
    toolbar_->addChild(n); n->link();
    return n;
}

NodePtr Crossbar::makeFrame(const char* name, const char* color)
{
    if (!frameTpl_) return NodePtr();
    NodePtr n = frameTpl_->clone(); if (!n) return NodePtr();
    n->setPropA(abi::PROP_Name, name);
    n->setPropA(abi::PROP_GetsInput, "false"); n->setPropA(abi::PROP_AbsorbsInput, "false");
    n->setPropA(abi::PROP_PackLocation, "nfn,nfn"); n->setPropA(abi::PROP_PackSize, "f,f");
    n->setPropA(abi::PROP_RStyleDefault, "/Styles.New.fatFrameInside_palW.rs_default");
    theme::paint(n, theme::ROLE_COLOR, color); n->setPropA(abi::PROP_BackgroundOpacity, "0.45");
    n->setPropA(abi::PROP_OpacityRelativeMin, "1.00"); n->setPropA(abi::PROP_Visible, "true");
    // The backdrop template is a toolbar corner: it carries the pane number and the
    // paging arrows, which used to multiply once per backdrop. We only want the ground.
    for (const NodePtr& c : n->children()) if (c->isA(T_Widget)) c->setVisible(false);
    toolbar_->addChild(n); n->link();
    return n;
}

void Crossbar::placeDecor()
{
    char nm[32];
    for (int c = 0; c < CROSS_COUNT; ++c) {
        std::snprintf(nm, sizeof nm, "cpFrame%d", c);
        frame_[c] = makeFrame(nm, COLOR_FRAME);
        std::snprintf(nm, sizeof nm, "cpCrossGlyph%d", c);
        glyphCross_[c] = makeImage(nm, Glyph::forCross(c), COLOR_GLYPH, m_.glyph);
    }
    frameExtra_ = makeFrame("cpFrameExtra", COLOR_FRAME);
    for (int e = 0; e < EXTRA_COUNT; ++e) {
        std::snprintf(nm, sizeof nm, "cpExtraGlyph%d", e);
        glyphExtra_[e] = makeImage(nm, e == 0 ? Glyph::L3 : Glyph::R3, COLOR_GLYPH, m_.extraGlyph);
    }
    for (int i = 0; i < 3; ++i) {
        std::snprintf(nm, sizeof nm, "cpFan%d", i);
        fanFrame_[i] = makeFrame(nm, COLOR_FRAME);
        if (fanFrame_[i]) fanFrame_[i]->setVisible(false);
        for (int j = 0; j < 2; ++j) {
            const int key = FAN_BADGE[i][j];
            if (key < 0) continue;
            std::snprintf(nm, sizeof nm, "cpFanTag%d_%d", i, j);
            fanBadge_[i][j] = makeImage(nm, key, COLOR_MOD, m_.fanBadge);
            if (fanBadge_[i][j]) fanBadge_[i][j]->setVisible(false);
        }
    }
    glyphMod_[0] = makeImage("cpModL", Glyph::TR_L, COLOR_MOD, m_.modGlyph);
    glyphMod_[1] = makeImage("cpModR", Glyph::TR_R, COLOR_MOD, m_.modGlyph);
    // Binding badges start hidden: the label source arrives after attach and a cloned
    // text template carries its original caption.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        std::snprintf(nm, sizeof nm, "cpBind%d", i);
        bindBadge_[i] = makeText(nm, 14);
        if (bindBadge_[i]) bindBadge_[i]->setVisible(false);
    }
}

void Crossbar::fit(const NodePtr& n, const UIRect& r)
{
    if (!n) return;
    setRect(n, r.left, r.top, r.right - r.left, r.bottom - r.top);
    // Widgets only: a page can hold UIData children, and SetLocation/SetSize are
    // UIWidget vtable slots - through those the call lands on garbage.
    for (const NodePtr& c : n->children()) {
        if (!c->isA(T_Widget)) continue;
        c->setLocation(1, 2); c->setSize(r.right - r.left - 3, r.bottom - r.top - 4);
    }
}

bool Crossbar::snapshotPane(int pane)
{
    if (pane < 0 || pane >= PANE_SET_COUNT || !toolbar_) return false;
    int taken = 0, withIcon = 0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        NodePtr& dst = art_[pane][i];
        if (dst) {
            // The snapshot may be on screen: off the flyout first, then delete, or
            // applyFans pokes a dead widget.
            for (int f = 0; f < 3; ++f) if (fanShown_[f] == dst) fanShown_[f].reset();
            toolbar_->removeChild(dst); dst.reset();
        }
        const NodePtr& src = cell_[i];
        if (!src) continue;
        // Item icons are 3D viewers registered in the client's icon manager. A clone
        // never gets in there: it stays empty or hangs on a removed object.
        if (src->isA(T_3DViewer)) continue;
        NodePtr c = src->clone(); if (!c) continue;
        char nm[24]; std::snprintf(nm, sizeof nm, "cpArt%d_%d", pane, i);
        c->setPropA(abi::PROP_Name, nm);
        c->setPropA(abi::PROP_GetsInput, "false"); c->setPropA(abi::PROP_AbsorbsInput, "false");
        c->setPropA(abi::PROP_PackLocation, "nfn,nfn"); c->setPropA(abi::PROP_PackSize, "f,f");
        if (!toolbar_->addChild(c)) continue;
        c->link(); c->setVisible(false);
        dst = c; ++taken;
        if (src->isA(T_Button)) ++withIcon;
    }
    // Occupancy shows in the TYPE, not in the children (there are none either way): an
    // assigned ability is a sampleItemButton clone, an empty slot a sampleItemPage one.
    cp::tracef("snapshotPane: pane %d, %d cells taken, %d occupied", pane, taken, withIcon);
    return taken > 0;
}

void Crossbar::warmPanes()
{
    // All panes inside one frame: performAction redraws the volume synchronously, so
    // nothing intermediate reaches the screen. We end on pane 0, where a release lands.
    static const int ORDER[4] = {PANE_L2, PANE_R2, PANE_L2R2, PANE_MAIN};
    for (int k = 0; k < 4; ++k) {
        char act[24]; std::snprintf(act, sizeof act, "toolbarPane%02d", ORDER[k]);
        bin::performAction(act);
        if (!refreshCells()) { cp::tracef("warmPanes: no cells on pane %d", ORDER[k]); return; }
        snapshotPane(ORDER[k]);
        if (ORDER[k] == PANE_L2R2 && emptySlot_ < 0) pickEmptyDonor();
    }
    for (int k = 0; k < CLUSTER_COUNT; ++k) place(CLUSTERS[k].slot, mainRect(bar_, m_, k), true);
    warmed_ = true;
}

void Crossbar::pickEmptyDonor()
{
    // Clearing a mailbox needs an empty CuiDragInfo. We cannot build one (STLport
    // strings inside), so we borrow one from a slot known to be empty - by cell type,
    // among the slots the crossbar never touches on the current pane.
    if (!volume_) return;
    for (int slot = 23; slot > DROP_SLOT[2]; --slot) {
        char nm[8]; std::snprintf(nm, sizeof nm, "%d", slot);
        NodePtr c = volume_->child(nm);
        if (!c || c->isA(T_Button)) continue;
        emptyPane_ = PANE_L2R2; emptySlot_ = slot;
        cp::tracef("pickEmptyDonor: an empty sample - pane %d slot %d", emptyPane_, emptySlot_);
        return;
    }
    cp::tracef("pickEmptyDonor: no free slot was found, clearing a mailbox will be unavailable");
}

void Crossbar::collectDrops()
{
    // The mailbox turned into a button: the client just dropped an item there. Move it
    // to the layer's pane and clear the mailbox.
    if (reveal_ < 0) return;
    const int slot = CLUSTERS[reveal_].slot;
    for (int i = 0; i < 3; ++i) {
        if (!drop_[i] || !drop_[i]->isA(T_Button)) continue;
        void* item = bin::toolbar::getItem(PANE_MAIN, DROP_SLOT[i]);
        if (!item) { cp::tracef("collectDrops: the mediator is not caught yet, the item stayed in the mailbox"); return; }
        const bool ok = bin::toolbar::setItem(FAN_PANE[i], slot, item);
        void* blank = (emptySlot_ >= 0) ? bin::toolbar::getItem(emptyPane_, emptySlot_) : nullptr;
        if (blank) bin::toolbar::setItem(PANE_MAIN, DROP_SLOT[i], blank);
        cp::tracef("collectDrops: mailbox %d -> pane %d slot %d, moved=%d, mailbox cleared=%d",
                   DROP_SLOT[i], FAN_PANE[i], slot, (int)ok, (int)(blank != nullptr));
        if (ok) warmPanes();          // re-snapshot so the flyout shows the new thing at once
        return;                        // one drop per frame
    }
}

bool Crossbar::bindFrom(Ui& ui, const NodePtr& source, int pane, int slot)
{
    // No synthetic drags here. Measured twice on 9 Sep 2026: the slot did not change,
    // and the client was left mid-drag - the next real mouse drag killed it
    // (Crossbar::place -> setVisible over a demolished cell). The proper route is to
    // assemble a CuiDragInfo and pass it to setToolbarItem, which copies through the
    // client's operator=. Until that exists we answer "no".
    // The crashes themselves were a bug of ours, fixed 9 Sep 2026: BinUString freed its
    // buffer in characters while taking it in bytes and wrecked the STLport free list.
    // The buffer is shared now and never freed.
    // So: the known-good case first (a toolbar cell whose text we know), and only then
    // the browser row.
    (void)ui;
    if (!source || slot < 0 || slot >= SLOT_COUNT) return false;
    NodePtr known = cell(slot);
    if (!known) { cp::tracef("bindFrom: there is no cell for slot %d", slot); return false; }
    std::u16string got;
    const bool okKnown = bin::getProperty(known->handle(), abi::PROP_Name, got);
    std::string got8(got.begin(), got.end());
    cp::tracef("bindFrom: step 1 - cell of slot %d, name by node \"%s\", Name property: read=%d, value \"%s\"",
               slot, known->name().c_str(), (int)okKnown, got8.c_str());
    if (!okKnown) { cp::tracef("bindFrom: reading properties still does not work, going no further"); return false; }

    std::u16string str16;
    NodePtr src = source;
    bool okCmd = bin::getProperty(src->handle(), abi::PROP_CmdStr, str16);
    if (!okCmd || str16.empty()) {
        NodePtr icon = source->child("icon");
        if (icon) { src = icon; okCmd = bin::getProperty(src->handle(), abi::PROP_CmdStr, str16); }
    }
    std::string str(str16.begin(), str16.end());
    cp::tracef("bindFrom: step 2 - %s (%s), CmdStr: read=%d, value \"%s\"",
               src->name().c_str(), src->typeName().c_str(), (int)okCmd, str.c_str());
    if (str.empty()) { cp::tracef("bindFrom: there is no command text, not assembling an item"); return false; }

    std::u16string name16;
    bin::getProperty(src->handle(), abi::PROP_CmdName, name16);
    std::string name(name16.begin(), name16.end());
    const bool ok = bin::toolbar::setCommandItem(pane, slot, str.c_str(), name.c_str());
    if (ok) warmPanes();                              // the flyout will show the new thing at once
    return ok;
}

bool Crossbar::copyMainToLayer(int cluster)
{
    if (cluster < 0 || cluster >= CLUSTER_COUNT) return false;
    const int slot = CLUSTERS[cluster].slot;
    void* item = bin::toolbar::getItem(PANE_MAIN, slot);
    const bool ok = item && bin::toolbar::setItem(PANE_L2, slot, item);
    cp::tracef("copyMainToLayer: cluster %d slot %d, item %p, written=%d", cluster, slot, item, (int)ok);
    if (ok) warmPanes();
    return ok;
}

bool Crossbar::clearLayer(int cluster)
{
    if (cluster < 0 || cluster >= CLUSTER_COUNT || emptySlot_ < 0) return false;
    const int slot = CLUSTERS[cluster].slot;
    void* blank = bin::toolbar::getItem(emptyPane_, emptySlot_);
    const bool ok = blank && bin::toolbar::setItem(PANE_L2, slot, blank);
    cp::tracef("clearLayer: cluster %d slot %d, sample %p, cleared=%d", cluster, slot, blank, (int)ok);
    if (ok) warmPanes();
    return ok;
}

void Crossbar::applyFans()
{
    for (int i = 0; i < 3; ++i) {
        if (reveal_ < 0) {
            setVisible(fanFrame_[i], false);
            setVisible(drop_[i], false);
            for (int j = 0; j < 2; ++j) setVisible(fanBadge_[i][j], false);
            if (fanShown_[i]) { setVisible(fanShown_[i], false); fanShown_[i].reset(); }
            continue;
        }
        const UIRect r = fanRect(bar_, m_, reveal_, i);
        const UIRect at = {volumeAt_.x + r.left, volumeAt_.y + r.top,
                           volumeAt_.x + r.right, volumeAt_.y + r.bottom};
        if (fanFrame_[i]) {
            setRect(fanFrame_[i], at.left, at.top, at.right - at.left, at.bottom - at.top);
            setVisible(fanFrame_[i], true);
            toolbar_->moveChild(fanFrame_[i], 2);
        }
        // A receiver is a real volume cell, so its coordinates are relative to the
        // volume, without volumeAt_. It only has to catch the cursor - the snapshot and
        // the frame do the drawing. Input is set by hand: populateSlot copies
        // GetsInput/AbsorbsInput from visibility, and 10..12 are hidden.
        if (drop_[i]) {
            setRect(drop_[i], r.left, r.top, r.right - r.left, r.bottom - r.top);
            drop_[i]->setPropA(abi::PROP_GetsInput, "true");
            drop_[i]->setPropA(abi::PROP_AbsorbsInput, "true");
            drop_[i]->setPropA(abi::PROP_Opacity, "0.00");
            setVisible(drop_[i], true);
        }
        // Rig knob: all three flyout cells read one pane, so the snapshot can be seen
        // carrying icons over (on a fresh character L2/R2 are empty and prove nothing).
        // All three then point at ONE widget, and a widget draws in one place - the two
        // neighbours show a bare frame. An artifact of the knob, not a bug.
        NodePtr want = art_[fanPane_ >= 0 ? fanPane_ : FAN_PANE[i]][CLUSTERS[reveal_].slot];
        if (fanShown_[i] && fanShown_[i] != want) setVisible(fanShown_[i], false);
        fanShown_[i] = want;
        if (want) {
            fit(want, at);
            setVisible(want, true);
            toolbar_->moveChild(want, 2);
        }
        // The layer glyph goes over the icon - underneath it would hide on exactly the
        // occupied cells that need it.
        const int b = m_.fanBadge;
        for (int j = 0; j < 2; ++j) {
            const NodePtr& tag = fanBadge_[i][j];
            if (!tag) continue;
            const int x = (j == 0) ? at.left + 1 : at.right - b - 1;
            const int y = (j == 0) ? at.top + 1 : at.bottom - b - 1;
            tag->setLocation(x, y);
            setVisible(tag, true);
            toolbar_->moveChild(tag, 2);
        }
    }
}

void Crossbar::place(int slot, const UIRect& r, bool force)
{
    NodePtr& n = cell_[slot]; if (!n) return;
    // In context mode the faces show our icon, so a cell with its own is hidden.
    if (ctx_ == CTX_PEACEFUL && (slot == 5 || slot == 6 || slot == 7)) {
        last_[slot] = r;                       // we remember the geometry: the icon will need it
        hide(slot);
        return;
    }
    if (force || !shown_[slot] || std::memcmp(&r, &last_[slot], sizeof r) != 0) {
        fit(n, r);
        last_[slot] = r;
    }
    setVisible(n, true);
    shown_[slot] = true;
}

void Crossbar::hide(int slot)
{
    if (!shown_[slot] && cell_[slot] && !cell_[slot]->willDraw()) return;
    setVisible(cell_[slot], false);
    shown_[slot] = false;
}

void Crossbar::refreshDrops()
{
    // Mailboxes are re-read every frame, not on the cell '0' guard: a drop recreates
    // only its own cell, cell '0' does not move with it, and the old pointer is freed
    // memory. Three GetChild calls a frame is a cheap price.
    if (!volume_) return;
    for (int i = 0; i < 3; ++i) {
        char nm[8]; std::snprintf(nm, sizeof nm, "%d", DROP_SLOT[i]);
        drop_[i] = volume_->child(nm);
    }
}

bool Crossbar::fanUnderMouse() const
{
    // Moving off the main cell onto the flyout must not collapse it, or the flyout
    // cells are unreachable.
    for (int i = 0; i < 3; ++i) {
        if (drop_[i] && drop_[i]->underMouse()) return true;
        if (fanFrame_[i] && fanFrame_[i]->underMouse()) return true;
    }
    return false;
}

int Crossbar::hoveredCluster() const
{
    // Hover counts on the main cell only - see above.
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        const NodePtr& n = cell_[CLUSTERS[k].slot];
        if (n && n->underMouse()) return k;
    }
    return -1;
}

void Crossbar::applyDecor()
{
    for (int c = 0; c < CROSS_COUNT; ++c) {
        if (frame_[c]) {
            const UIRect& f = bar_.crossFrame[c];
            setRect(frame_[c], volumeAt_.x + f.left, volumeAt_.y + f.top, f.right - f.left, f.bottom - f.top);
            setVisible(frame_[c], true);
        }
        if (glyphCross_[c]) {
            UIRect g = rectAt(bar_.crossCenter[c], m_.glyph);
            glyphCross_[c]->setLocation(volumeAt_.x + g.left, volumeAt_.y + g.top);
            setVisible(glyphCross_[c], true);
        }
    }
    if (frameExtra_) {
        const UIRect& f = bar_.extraFrame;
        setRect(frameExtra_, volumeAt_.x + f.left, volumeAt_.y + f.top, f.right - f.left, f.bottom - f.top);
        setVisible(frameExtra_, true);
    }
    for (int e = 0; e < EXTRA_COUNT; ++e) if (glyphExtra_[e]) {
        UIRect g = rectAt(bar_.extraGlyphAt[e], m_.extraGlyph);
        glyphExtra_[e]->setLocation(volumeAt_.x + g.left, volumeAt_.y + g.top);
        setVisible(glyphExtra_[e], true);
    }
    // Context: icon over the cell, label under it. Slots SQUARE(5), CROSS(7),
    // CIRCLE(6), in setContext's icon order.
    if (ctx_ == CTX_PEACEFUL) {
        static const int SLOTS[3] = {5, 7, 6};
        for (int i = 0; i < 3; ++i) {
            const UIRect r = last_[SLOTS[i]];
            if (ctxFrame_[i]) {
                setRect(ctxFrame_[i], volumeAt_.x + r.left, volumeAt_.y + r.top,
                        r.right - r.left, r.bottom - r.top);
            }
            if (ctxGlyph_[i]) {
                const int g = m_.main - 4;
                ctxGlyph_[i]->setLocation(volumeAt_.x + (r.left + r.right) / 2 - g / 2,
                                          volumeAt_.y + (r.top + r.bottom) / 2 - g / 2);
            }
            if (ctxLabel_[i]) {
                const int w = ctxLabel_[i]->size().x;
                ctxLabel_[i]->setLocation(volumeAt_.x + (r.left + r.right) / 2 - w / 2,
                                          volumeAt_.y + r.bottom + 1);
            }
        }
    }

    // Modifier glyph: L2 left, R2 right, both held - both side by side.
    const bool showL = (mod_ == PANE_L2 || mod_ == PANE_L2R2);
    const bool showR = (mod_ == PANE_R2 || mod_ == PANE_L2R2);
    const int half = m_.modGlyph / 2 + 2;
    for (int j = 0; j < 2; ++j) {
        const bool on = j == 0 ? showL : showR;
        if (!glyphMod_[j]) continue;
        if (on) {
            UIPoint c = bar_.modGlyphAt;
            if (mod_ == PANE_L2R2) c.x += (j == 0 ? -half : half);
            UIRect g = rectAt(c, m_.modGlyph);
            glyphMod_[j]->setLocation(volumeAt_.x + g.left, volumeAt_.y + g.top);
        }
        setVisible(glyphMod_[j], on);
    }
}

void Crossbar::setContext(int mode)
{
    if (ctx_ == mode) return;
    ctx_ = mode;
    // A hostile target colours the whole bar: that is a state of the player. A peaceful
    // one greens the right cross alone, where the faces really change meaning.
    const char* face = mode == CTX_PEACEFUL ? COLOR_CTX_FRAME
                     : (mode == CTX_HOSTILE ? COLOR_HOSTILE_FRAME : COLOR_FRAME);
    const char* rest = mode == CTX_HOSTILE ? COLOR_HOSTILE_FRAME : COLOR_FRAME;
    theme::paint(frame_[1], theme::ROLE_COLOR, face);
    theme::paint(frame_[0], theme::ROLE_COLOR, rest);
    theme::paint(frameExtra_, theme::ROLE_COLOR, rest);
    // The centre and L3/R3 glyphs take the same colour, or the bar comes out half red
    // and half white.
    const char* faceGlyph = mode == CTX_OFF ? COLOR_GLYPH : face;
    const char* restGlyph = mode == CTX_HOSTILE ? rest : COLOR_GLYPH;
    for (int c = 0; c < gen::CROSS_COUNT; ++c)
        theme::paint(glyphCross_[c], theme::ROLE_COLOR, c == 1 ? faceGlyph : restGlyph);
    for (int e = 0; e < gen::EXTRA_COUNT; ++e)
        theme::paint(glyphExtra_[e], theme::ROLE_COLOR, restGlyph);
    // Icons only on a peaceful target: on a hostile one the faces are still slots.
    if (mode != CTX_PEACEFUL) {
        for (int i = 0; i < 3; ++i) {
            setVisible(ctxGlyph_[i], false);
            setVisible(ctxLabel_[i], false);
            setVisible(ctxFrame_[i], false);
        }
        return;
    }
    if (!ctxBuilt_) {
        // square = radial, cross = interact, circle = untarget. Face slots run
        // TRIANGLE(4) SQUARE(5) CIRCLE(6) CROSS(7).
        static const struct { int key; const char* name; const char* label; } A[3] = {
            {Glyph::ACT_RADIAL, "cpCtxG0", "Radial"},
            {Glyph::ACT_USE,    "cpCtxG1", "Use"},
            {Glyph::ACT_CLEAR,  "cpCtxG2", "Clear"},
        };
        char nm[32];
        for (int i = 0; i < 3; ++i) {
            // The icon fills nearly the whole cell: with the slot's own cell hidden, a
            // small picture hung in mid-air.
            ctxGlyph_[i] = makeImage(A[i].name, A[i].key, COLOR_CTX_GLYPH, m_.main - 4);
            std::snprintf(nm, sizeof nm, "cpCtxL%d", i);
            ctxLabel_[i] = makeText(nm, 12);
            if (ctxLabel_[i]) {
                theme::paint(ctxLabel_[i], theme::ROLE_TEXT, COLOR_CTX_FRAME);
                std::u16string t;
                for (const char* p = A[i].label; *p; ++p) t.push_back(static_cast<char16_t>(*p));
                ctxLabel_[i]->setLocalText(t.c_str());
            }
        }
        // A fake slot under the icon, created after the icons and labels: child order is
        // draw order and the last one lands at the bottom. Otherwise it covers the icon.
        for (int i = 0; i < 3; ++i) {
            std::snprintf(nm, sizeof nm, "cpCtxF%d", i);
            ctxFrame_[i] = makeFrame(nm, COLOR_CTX_FRAME);
        }
        ctxBuilt_ = true;
    }
    for (int i = 0; i < 3; ++i) {
        setVisible(ctxGlyph_[i], true);
        setVisible(ctxLabel_[i], true);
        setVisible(ctxFrame_[i], true);
    }
}

UIPoint Crossbar::modGlyphWorld() const
{
    // The modifier glyphs hang off the toolbar, not the volume (a volume packs its own),
    // at volumeAt_ + modGlyphAt - that sum is the screen point.
    const UIPoint w = toolbar_ ? toolbar_->worldLocation() : UIPoint{0, 0};
    return UIPoint{w.x + volumeAt_.x + bar_.modGlyphAt.x, w.y + volumeAt_.y + bar_.modGlyphAt.y};
}

int Crossbar::update(float dt, int activeMod, bool dragActive)
{
    if (!volume_) return 0;
    mod_ = activeMod < 0 ? 0 : (activeMod > PANE_L2R2 ? PANE_L2R2 : activeMod);
    drag_ = dragActive;

    guardTimer_ += dt; bool guard = guardTimer_ >= 0.25f; if (guard) guardTimer_ = 0;
    if (guard && keyBindings_ && keyBindings_->willDraw()) keyBindings_->setVisible(false);

    // A pane change recreates the cell widgets, and that is checked every frame, not on
    // the guard: a dozen frames fit in the 0.25 s between guard ticks, all of them poking
    // freed memory. That is what happened on 7 Sep 2026 - c0000096, a jump to
    // 0x55555555, the garbage filler.
    {
        NodePtr probe = volume_->child("0");
        // No cell: an InsertChildAfter refusal ate it. A different cell: populateSlot
        // recreated it. refreshCells covers both. If even that fails the toolbar was
        // rebuilt under us - detach, and the runtime re-attaches.
        if (!probe || !cellProbe_ || probe->handle() != cellProbe_->handle()) {
            cp::tracef(probe ? "update: the cells were recreated, taking them again"
                             : "update: cell '0' is missing - trying to bring it back");
            if (!refreshCells()) {
                cp::tracef("update: the cells cannot be collected - detaching");
                detach();
                return -1;
            }
            guard = true;                       // the geometry will be laid on by force
        }
    }

    // A flyout opens on hover over a main cell, at rest and mid-drag alike (mid-drag is
    // the point of it). With a modifier held there are none: the set is already shown.
    refreshDrops();
    if (hoverSettle_ > 0) --hoverSettle_;
    int hot = forced_ >= 0 ? forced_ : hoveredCluster();
    // A flyout is held open in two cases: the cursor moved onto the flyout itself, and
    // the cell was just recreated (a new widget has no 'under mouse' bit until the mouse
    // passes again). Without the second one it looped - warming switches panes, cells are
    // recreated, the flyout blinks, that counts as a fresh open and warms again. On
    // 8 Sep 2026 the crossbar span the panes nonstop under a resting cursor.
    if (hot < 0 && prevReveal_ >= 0 && (fanUnderMouse() || hoverSettle_ > 0)) hot = prevReveal_;
    reveal_ = (mod_ == PANE_MAIN) ? hot : -1;

    // The snapshot refreshes as a flyout opens: rare, and exactly when the other panes
    // are wanted. Once right after capture too, so the first flyout is not empty. Never
    // during a drag - the client is holding the drag object.
    if (warmCooldown_ > 0) warmCooldown_ -= dt;
    if (!warmed_ || (reveal_ >= 0 && prevReveal_ < 0 && warmCooldown_ <= 0)) {
        if (mod_ == PANE_MAIN && !drag_) { warmPanes(); warmCooldown_ = 1.0f; }
    }
    if (reveal_ != prevReveal_)
        cp::tracef("flyout: hover %d -> %d (frame %p, snapshot %p)", prevReveal_, reveal_,
                   fanFrame_[0] ? fanFrame_[0]->handle() : nullptr,
                   reveal_ >= 0 ? (art_[PANE_L2][CLUSTERS[reveal_].slot] ? art_[PANE_L2][CLUSTERS[reveal_].slot]->handle() : nullptr) : nullptr);
    prevReveal_ = reveal_;

    int fixed = 0;
    for (int k = 0; k < CLUSTER_COUNT; ++k) { place(CLUSTERS[k].slot, mainRect(bar_, m_, k), guard); ++fixed; }
    applyDecor();
    applyFans();
    collectDrops();

    // Every ~10 s: cell 0 - visible, where, what is inside. For working out why a
    // non-empty pane draws nothing.
    if (++diagTick_ % 600 == 0 && cell_[0]) {
        UIPoint p = cell_[0]->location(); UISize sz = cell_[0]->size();
        int mouseOn = -1;
        for (int k = 0; k < CLUSTER_COUNT; ++k) if (cell_[CLUSTERS[k].slot] && cell_[CLUSTERS[k].slot]->underMouse()) mouseOn = k;
        cp::tracef("diag: pane%d cell0 %p visible=%d %d,%d %dx%d children=%d volume visible=%d undermouse=%d flyout=%d",
                   mod_, cell_[0]->handle(), (int)cell_[0]->willDraw(), p.x, p.y, sz.x, sz.y,
                   (int)cell_[0]->children().size(), (int)volume_->willDraw(), mouseOn, reveal_);
    }

    if (bindsDirty_ && binds_) {
        bindsDirty_ = false; char cmd[40]; std::u16string s;
        for (int i = 0; i < SLOT_COUNT; ++i) if (bindBadge_[i]) {
            if (binds_->label(slotCommand(i, cmd, sizeof cmd), s)) { bindBadge_[i]->setLocalText(s.c_str()); bindBadge_[i]->setVisible(true); }
            else bindBadge_[i]->setVisible(false);
        }
    }
    return fixed;
}

}} // namespace cp::crossbar
