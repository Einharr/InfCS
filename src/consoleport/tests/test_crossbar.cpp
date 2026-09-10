// Crossbar tests, 7 Sep 2026 model: a set is a PANE. Ten positions in slots 0..9 of
// every pane, and a held modifier changes the pane, not the slot.
#include "test.h"
#include "../core/binary.h"
#include "mock_node.h"
#include "../crossbar/crossbar.h"
#include "../core/node.h"
#include <algorithm>
#include <cstdio>

using namespace cp;
using namespace cp::mock;
using namespace cp::crossbar;
using namespace cp::gen;

static bool overlap(const UIRect& a, const UIRect& b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }
static bool inside(const UIRect& a, const UIRect& b) { return a.left >= b.left && a.top >= b.top && a.right <= b.right && a.bottom <= b.bottom; }
static UIPoint mid(const UIRect& r) { return UIPoint{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }

TEST(model_ten_positions_one_slot_each)
{
    CHECK_EQ(CLUSTER_COUNT, 10);
    CHECK_EQ(SLOT_COUNT, 10);
    // one slot per position, equal to its number - the pane carries the set
    for (int k = 0; k < CLUSTER_COUNT; ++k) CHECK_EQ(CLUSTERS[k].slot, k);
    // two crosses of four positions each, plus the L3/R3 block
    int perCross[3] = {0, 0, 0};
    for (int k = 0; k < CLUSTER_COUNT; ++k) ++perCross[CLUSTERS[k].cross];
    CHECK_EQ(perCross[0], 4); CHECK_EQ(perCross[1], 4); CHECK_EQ(perCross[EXTRA_CROSS], EXTRA_COUNT);
    CHECK_EQ(PANE_MAIN, 0); CHECK_EQ(PANE_L2R2, 3); CHECK(PANE_SET_COUNT <= 6);
}

TEST(geometry_rest_cross_is_a_plus)
{
    Metrics m; BarLayout bl = barLayout(m);
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        const Cluster& c = CLUSTERS[k];
        if (c.cross == EXTRA_CROSS) continue;
        UIPoint p = mid(mainRect(bl, m, k));
        int dx, dy, px, py; fanAxes(c.fan, dx, dy, px, py);
        CHECK_EQ(p.x, bl.crossCenter[c.cross].x + dx * m.arm);
        CHECK_EQ(p.y, bl.crossCenter[c.cross].y + dy * m.arm);
    }
    for (int a = 0; a < CLUSTER_COUNT; ++a)
        for (int b = a + 1; b < CLUSTER_COUNT; ++b)
            if (CLUSTERS[a].cross == CLUSTERS[b].cross)
                CHECK(!overlap(mainRect(bl, m, a), mainRect(bl, m, b)));
    // the center of a cross is free for the glyph
    for (int cross = 0; cross < CROSS_COUNT; ++cross) {
        UIRect g = rectAt(bl.crossCenter[cross], m.glyph);
        for (int k = 0; k < CLUSTER_COUNT; ++k)
            if (CLUSTERS[k].cross == cross) CHECK(!overlap(g, mainRect(bl, m, k)));
    }
}

TEST(geometry_extra_block_sits_between_crosses)
{
    Metrics m; BarLayout bl = barLayout(m);
    // L3/R3 sit side by side inside their own backdrop rather than as a plus
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        if (CLUSTERS[k].cross != EXTRA_CROSS) continue;
        int e = extraIndexOf(k);
        CHECK(e >= 0 && e < EXTRA_COUNT);
        UIRect r = mainRect(bl, m, k);
        CHECK_EQ(r.left, extraRect(bl, m, e).left);
        CHECK(inside(r, bl.extraFrame));
    }
    CHECK(bl.crossFrame[0].right < bl.extraFrame.left);
    CHECK(bl.extraFrame.right < bl.crossFrame[1].left);
    CHECK(!overlap(bl.crossFrame[0], bl.crossFrame[1]));
}

TEST(geometry_fans_go_outside_by_direction)
{
    // The flyouts are not drawn yet, but their geometry still has to be right: measured
    // from the cell's own centre, so it covers the L3/R3 block too.
    Metrics m; BarLayout bl = barLayout(m);
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        const Cluster& c = CLUSTERS[k];
        int dx, dy, px, py; fanAxes(c.fan, dx, dy, px, py);
        UIPoint mc = mid(mainRect(bl, m, k));
        UIPoint prev{0, 0};
        for (int i = 0; i < 3; ++i) {
            UIRect f = fanRect(bl, m, k, i);
            UIPoint p = mid(f);
            CHECK_EQ(p.x - mc.x, dx * m.fanOut() + px * (i - 1) * m.fanStep);
            CHECK_EQ(p.y - mc.y, dy * m.fanOut() + py * (i - 1) * m.fanStep);
            for (int j = 0; j < CLUSTER_COUNT; ++j) CHECK(!overlap(f, mainRect(bl, m, j)));
            if (i) { CHECK_EQ((p.x != prev.x) ? 1 : 0, px); CHECK_EQ((p.y != prev.y) ? 1 : 0, py); }
            prev = p;
        }
        // the middle flyout cell lines up with its own slot
        CHECK_EQ(mid(fanRect(bl, m, k, 1)).x - mc.x, dx * m.fanOut());
        CHECK_EQ(mid(fanRect(bl, m, k, 1)).y - mc.y, dy * m.fanOut());
        // and the flyout does not climb onto the neighboring cross's backdrop
        for (int cr = 0; cr < CROSS_COUNT; ++cr)
            if (cr != c.cross)
                for (int i = 0; i < 3; ++i) CHECK(!overlap(fanRect(bl, m, k, i), bl.crossFrame[cr]));
        if (c.cross != EXTRA_CROSS)
            for (int i = 0; i < 3; ++i) CHECK(!overlap(fanRect(bl, m, k, i), bl.crossFrame[c.cross]));
    }
    // Nor onto the L3/R3 block's backdrop - as permanent as the crosses'.
    for (int k = 0; k < CLUSTER_COUNT; ++k)
        if (CLUSTERS[k].cross != EXTRA_CROSS)
            for (int i = 0; i < 3; ++i) CHECK(!overlap(fanRect(bl, m, k, i), bl.extraFrame));

    // Flyouts of different clusters may overlap, and the narrow bar depends on it -
    // only one is ever open. So the real rule is checked instead: the three cells of
    // ONE flyout do not run into each other.
    for (int k = 0; k < CLUSTER_COUNT; ++k)
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
            CHECK(!overlap(fanRect(bl, m, k, i), fanRect(bl, m, k, j)));
}

TEST(geometry_bar_holds_everything)
{
    Metrics m; BarLayout bl = barLayout(m);
    UIRect bar = {0, 0, bl.size.x, bl.size.y};
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        CHECK(inside(mainRect(bl, m, k), bar));
        for (int i = 0; i < 3; ++i) CHECK(inside(fanRect(bl, m, k, i), bar));
    }
    CHECK(inside(bl.crossFrame[0], bar)); CHECK(inside(bl.crossFrame[1], bar)); CHECK(inside(bl.extraFrame, bar));
    // The modifier glyph goes under the backdrops, not the flyout band - the two are
    // never shown at once.
    CHECK(bl.modGlyphAt.y > bl.crossFrame[0].bottom);
    CHECK(bl.modGlyphAt.y - m.modGlyph / 2 <= bl.crossFrame[0].bottom + m.modGap);
    CHECK(inside(rectAt(bl.modGlyphAt, m.modGlyph), bar));
}

// the root has to stay alive: parents in the mock are weak_ptr, as in the client
static MockPtr makeHud()
{
    MockPtr hud = MockNode::make("GroundHUD", "Page", 0, 0, 1920, 1080);
    MockPtr tb = hud->add("Toolbar", "Page", 274, 1, 476, 84);
    MockPtr tl = tb->add("cornerTL", "Page", 0, 0, 19, 46);
    tl->add("textPane", "Text", 2, 3, 15, 20);
    MockPtr vol = tb->add("volume", "VolumePage", 21, 6, 450, 34);
    for (int i = 0; i < 36; ++i) { char nm[4]; std::snprintf(nm, 4, "%d", i); vol->add(nm, "Page", 2 + i * 32, 2, 30, 30); }
    tb->add("volumeKeyBindings", "VolumePage", 21, 40, 450, 14);
    hud->add("someImage", "Image", 0, 0, 16, 16);
    return hud;
}

static void attachTo(MockPtr tb, Crossbar& cb)
{
    NodePtr img = findFirst(tb->parent(), T_Image), txt = findFirst(tb, T_Text);
    CHECK(cb.attach(tb, img, txt, tb->child("cornerTL")));
}

// A recreated cell needs a few frames before its "under mouse" means anything (see
// hoverSettle_), so hover tests have to spin that out - otherwise they check behaviour
// that does not exist in game.
static void tick(Crossbar& cb, int n = 4, int pane = PANE_MAIN, bool drag = false)
{
    for (int i = 0; i < n; ++i) cb.update(0.016f, pane, drag);
}

TEST(crossbar_places_ten_cells_and_hides_the_rest)
{
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    CHECK(vol->prop(abi::PROP_DoNotPackChildren) == u"true");
    CHECK_EQ(vol->size().x, cb.layout().size.x);
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        CHECK(cb.cellShown(CLUSTERS[k].slot));
        UIRect want = mainRect(cb.layout(), cb.metrics(), k);
        NodePtr c = cb.cell(CLUSTERS[k].slot);
        // cells are the volume's children: coordinates from it, no offset
        CHECK_EQ(c->location().x, want.left); CHECK_EQ(c->location().y, want.top);
    }
    // everything above our ten is not filled by the client - we hide it
    CHECK(!vol->child("10")->willDraw());
    CHECK(!vol->child("23")->willDraw());
    CHECK(!vol->child("35")->willDraw());
    CHECK(!tb->child("volumeKeyBindings")->willDraw());
}

TEST(crossbar_reacquires_cells_after_pane_switch)
{
    // switchToPane -> repopulateSlots -> populateSlot replaces a cell's widget under
    // the same name, so the guard has to re-take them or we move freed memory.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    NodePtr before = cb.cell(0);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        char nm[4]; std::snprintf(nm, 4, "%d", i);
        vol->removeChild(vol->child(nm));
        vol->add(nm, "Page", 0, 0, 30, 30);
    }
    // Caught on the very next frame, not on the guard: otherwise freed widgets get
    // poked for a quarter of a second (c0000096, a jump to 0x55555555).
    cb.update(0.016f, PANE_MAIN, false);
    CHECK(cb.cell(0) != before);
    for (int k = 0; k < CLUSTER_COUNT; ++k) {
        NodePtr c = cb.cell(CLUSTERS[k].slot);
        CHECK(c && c->willDraw());
        UIRect want = mainRect(cb.layout(), cb.metrics(), k);
        CHECK_EQ(c->location().x, want.left);  // the geometry was laid on again
        CHECK_EQ(c->location().y, want.top);
    }
}

TEST(crossbar_restores_cell_eaten_by_the_client)
{
    // InsertChildAfter refuses at mWidgetCount >= mCellMax and populateSlot does
    // RemoveChild anyway, so the cell is gone for good - with no widget populateSlot
    // stops working for that slot. We have to put it back, or the crossbar stays empty
    // until a relog.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));

    vol->removeChild(vol->child("0"));            // the client ate cell 0
    CHECK(!vol->child("0"));
    for (int i = 1; i < SLOT_COUNT; ++i) {        // the rest it swapped, as usual
        char nm[4]; std::snprintf(nm, 4, "%d", i);
        vol->removeChild(vol->child(nm));
        vol->add(nm, "Page", 0, 0, 30, 30);
    }
    cb.update(0.016f, PANE_MAIN, false);

    CHECK(vol->child("0"));                       // brought back
    CHECK(cb.cell(0) && cb.cell(0)->willDraw());
    UIRect want = mainRect(cb.layout(), cb.metrics(), 0);
    CHECK_EQ(cb.cell(0)->location().x, want.left);
    CHECK_EQ(cb.cell(0)->location().y, want.top);
}

TEST(crossbar_fan_shows_other_panes_on_hover)
{
    // A flyout is three cells of other panes outside the backdrop. Nothing can read
    // another pane, so the module snapshots one while the client is showing it. Checked:
    // the snapshots exist, nothing shows at rest, and hover opens exactly three in
    // their places.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    const UIPoint at = vol->location();

    cb.update(0.016f, PANE_MAIN, false);
    CHECK_EQ(cb.revealed(), -1);
    CHECK(tb->child("cpArt1_0"));                       // the snapshot of pane L2, slot 0
    CHECK(tb->child("cpArt3_0"));                       // and of pane L2+R2
    CHECK(!tb->child("cpArt1_0")->willDraw());          // but at rest it is hidden
    for (int i = 0; i < 3; ++i) {
        char nm[8]; std::snprintf(nm, 8, "cpFan%d", i);
        CHECK(tb->child(nm) && !tb->child(nm)->willDraw());
    }

    // the mouse on the D-pad's top cell is cluster 0
    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);
    CHECK_EQ(cb.revealed(), 0);

    static const int pane[3] = {PANE_L2, PANE_L2R2, PANE_R2};
    for (int i = 0; i < 3; ++i) {
        UIRect want = fanRect(cb.layout(), cb.metrics(), 0, i);
        char nm[8]; std::snprintf(nm, 8, "cpFan%d", i);
        NodePtr f = tb->child(nm);
        CHECK(f && f->willDraw());
        CHECK_EQ(f->location().x, at.x + want.left);    // the decor are children of the toolbar: with the volume offset
        CHECK_EQ(f->location().y, at.y + want.top);
        char an[16]; std::snprintf(an, 16, "cpArt%d_%d", pane[i], CLUSTERS[0].slot);
        NodePtr a = tb->child(an);
        CHECK(a && a->willDraw());
        CHECK_EQ(a->location().x, at.x + want.left);
        CHECK_EQ(a->location().y, at.y + want.top);
    }

    // the mouse was taken away - the flyout closed along with the snapshots
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput);
    tick(cb);
    CHECK_EQ(cb.revealed(), -1);
    CHECK(!tb->child("cpFan0")->willDraw());
    CHECK(!tb->child("cpArt1_0")->willDraw());
}

TEST(crossbar_fan_cells_carry_their_layer_glyph)
{
    // Without a layer glyph a flyout is three identical cells and no way to tell which
    // set you are aiming at. It goes over the icon, or an occupied cell hides it exactly
    // when it matters.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    const UIPoint at = vol->location();

    cb.update(0.016f, PANE_MAIN, false);
    CHECK(tb->child("cpFanTag0_0") && !tb->child("cpFanTag0_0")->willDraw());
    CHECK(!tb->child("cpFanTag0_1"));            // the outer cells have one glyph
    CHECK(!tb->child("cpFanTag2_1"));
    CHECK(tb->child("cpFanTag1_0") && tb->child("cpFanTag1_1"));   // the middle one has two: L2+R2

    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);

    const int b = cb.metrics().fanBadge;
    for (int i = 0; i < 3; ++i) {
        UIRect f = fanRect(cb.layout(), cb.metrics(), 0, i);
        UIRect cell = {at.x + f.left, at.y + f.top, at.x + f.right, at.y + f.bottom};
        char nm[16]; std::snprintf(nm, 16, "cpFanTag%d_0", i);
        NodePtr t = tb->child(nm);
        CHECK(t && t->willDraw());
        CHECK_EQ(t->location().x, cell.left + 1);          // the first one - the top left corner
        CHECK_EQ(t->location().y, cell.top + 1);
        CHECK(t->location().x + b <= cell.right);          // and it does not spill out of the cell
        CHECK(t->location().y + b <= cell.bottom);
    }
    // the middle cell's second glyph is diagonal, in the bottom right corner
    UIRect f1 = fanRect(cb.layout(), cb.metrics(), 0, 1);
    NodePtr t1 = tb->child("cpFanTag1_1");
    CHECK(t1 && t1->willDraw());
    CHECK_EQ(t1->location().x, at.x + f1.right - b - 1);
    CHECK_EQ(t1->location().y, at.y + f1.bottom - b - 1);
    CHECK(t1->location().x >= at.x + f1.left);             // the two glyphs do not run into each other

    // the flyout closed - the captions went with it
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput);
    tick(cb);
    CHECK(!tb->child("cpFanTag0_0")->willDraw());
    CHECK(!tb->child("cpFanTag1_1")->willDraw());
}

TEST(crossbar_fan_carries_the_icon_assigned_in_that_layer)
{
    // A flyout shows what is assigned in another set, and the snapshot is a clone of
    // the cell, so the contents have to come with it.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    // In the client an occupied cell is the button itself and has no children; the
    // child here only proves the clone carries contents and size.
    std::static_pointer_cast<MockNode>(vol->child("0"))->add("icon", "Button", 0, 0, 24, 24);

    Crossbar cb; attachTo(tb, cb);
    cb.update(0.016f, PANE_MAIN, false);

    CHECK(tb->child("cpArt1_0") && tb->child("cpArt1_0")->child("icon"));   // the icon came along

    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);

    // Opening re-snapshotted the panes, so the old clone is gone - take it again.
    NodePtr art = tb->child("cpArt1_0");
    UIRect f = fanRect(cb.layout(), cb.metrics(), 0, 0);
    const UIPoint at = vol->location();
    CHECK(art && art->willDraw());
    CHECK_EQ(art->location().x, at.x + f.left);
    NodePtr icon = art->child("icon");                    // and the contents were fitted to the flyout cell
    CHECK(icon && icon->size().x == f.right - f.left - 3);
}

TEST(crossbar_fan_keeps_open_while_the_cursor_is_on_it)
{
    // Without this a flyout cell is unreachable: the cursor leaves the main cell, the
    // flyout collapses, and there is nowhere left to drop.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));

    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);
    CHECK_EQ(cb.revealed(), 0);

    // the cursor left the cell for the flyout's middle receiver
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput);
    MockPtr box = std::static_pointer_cast<MockNode>(vol->child("11"));
    box->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb, 4, PANE_MAIN, true);
    CHECK_EQ(cb.revealed(), 0);                       // the flyout stayed open

    box->setFlags(BF_Visible | BF_Enabled | BF_GetsInput);
    tick(cb);
    CHECK_EQ(cb.revealed(), -1);                      // gone entirely - it closed
}

TEST(crossbar_moves_a_drop_from_the_box_into_its_layer)
{
    // The client drops into a real cell 10..12 of the active pane. We have to spot that
    // by the cell's type, move the item to the layer's pane and clear the mailbox, or
    // the next drop looks exactly like the last.
    bin::toolbar::mockReset();
    bin::toolbar::setSelf(reinterpret_cast<void*>(1));
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));

    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);
    CHECK_EQ(cb.revealed(), 0);

    // a drop into the middle mailbox: pane L2+R2, cluster 0's slot
    bin::toolbar::mockPut(PANE_MAIN, 11, 77);
    vol->removeChild(vol->child("11"));
    vol->add("11", "Button", 0, 0, 30, 30);           // the client recreated the cell as a button
    cb.update(0.016f, PANE_MAIN, false);

    CHECK_EQ(bin::toolbar::mockTag(PANE_L2R2, CLUSTERS[0].slot), 77);
    CHECK_EQ(bin::toolbar::mockTag(PANE_MAIN, 11), 0);   // the mailbox was cleared
    bin::toolbar::setSelf(nullptr);
}

TEST(crossbar_leaves_the_drop_alone_without_the_mediator)
{
    // With no mediator pointer there is nothing to write another pane with, so the item
    // has to STAY in the mailbox rather than vanish.
    bin::toolbar::mockReset();
    bin::toolbar::setSelf(nullptr);
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));
    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);

    bin::toolbar::mockPut(PANE_MAIN, 10, 55);
    vol->removeChild(vol->child("10"));
    vol->add("10", "Button", 0, 0, 30, 30);
    cb.update(0.016f, PANE_MAIN, false);

    CHECK_EQ(bin::toolbar::mockTag(PANE_MAIN, 10), 55);      // untouched
    CHECK_EQ(bin::toolbar::mockTag(PANE_L2, CLUSTERS[0].slot), 0);
}

TEST(crossbar_does_not_loop_warming_while_the_cursor_rests_on_it)
{
    // Warming switches panes, the client recreates the cells, and a fresh one loses
    // "under mouse" for a frame. Treat that as a close and the next frame counts a fresh
    // open, so the warm-up loops while the cursor rests on the crossbar. The flyout has
    // to stay open and the panes still.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr vol = std::static_pointer_cast<MockNode>(tb->child("volume"));

    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb);
    CHECK_EQ(cb.revealed(), 0);

    // the client recreated every cell (as on a pane change) - the hover bit is gone
    for (int i = 0; i < SLOT_COUNT; ++i) {
        char nm[4]; std::snprintf(nm, 4, "%d", i);
        vol->removeChild(vol->child(nm));
        vol->add(nm, "Page", 0, 0, 30, 30);
    }
    cb.update(0.016f, PANE_MAIN, false);
    CHECK_EQ(cb.revealed(), 0);        // the flyout held, which means there will be no new warm-up
}

TEST(crossbar_hides_fans_while_a_modifier_is_held)
{
    // A held modifier already shows that set: no flyout there.
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    MockPtr top = std::static_pointer_cast<MockNode>(cb.cell(CLUSTERS[0].slot));
    top->setFlags(BF_Visible | BF_Enabled | BF_GetsInput | BF_UnderMouse);
    tick(cb, 4, PANE_L2);
    CHECK_EQ(cb.revealed(), -1);
    CHECK(!tb->child("cpFan0")->willDraw());
}

TEST(crossbar_decor_frames_and_glyphs)
{
    // The atlas name is global state switched by the recognized pad, so it is set
    // before the crossbar is built - otherwise the glyphs get the wrong resource.
    crossbar::Glyph::setFamily("ds4");
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    int frames = 0, crossGlyphs = 0, extraGlyphs = 0, mods = 0; bool inputFree = true;
    std::vector<NodePtr> kids = tb->children();
    for (const NodePtr& k : kids) {
        std::string n = k->name();
        if (n.compare(0, 7, "cpFrame") == 0) ++frames;
        if (n.compare(0, 12, "cpCrossGlyph") == 0) ++crossGlyphs;
        if (n.compare(0, 12, "cpExtraGlyph") == 0) ++extraGlyphs;
        if (n.compare(0, 5, "cpMod") == 0) ++mods;
        if (n.compare(0, 2, "cp") == 0 && k->getsInput()) inputFree = false;
    }
    CHECK_EQ(frames, 3);                        // two cross backdrops plus the L3/R3 backdrop
    CHECK_EQ(crossGlyphs, 2); CHECK_EQ(extraGlyphs, EXTRA_COUNT); CHECK_EQ(mods, 2);
    CHECK(inputFree);
    CHECK(kids.back()->name().compare(0, 7, "cpFrame") == 0);   // the backdrops at the bottom
    MockPtr g0 = std::static_pointer_cast<MockNode>(tb->child("cpCrossGlyph0"));
    CHECK(g0->prop(abi::PROP_SourceResource) == u"consoleport_glyphs");
    CHECK(g0->prop(abi::PROP_SourceRect) == u"128,32,160,64");    // dpad
    CHECK_EQ(g0->location().x + g0->size().x / 2, 21 + cb.layout().crossCenter[0].x);
    MockPtr g1 = std::static_pointer_cast<MockNode>(tb->child("cpCrossGlyph1"));
    CHECK(g1->prop(abi::PROP_SourceRect) == u"160,32,192,64");  // face_all
    MockPtr e0 = std::static_pointer_cast<MockNode>(tb->child("cpExtraGlyph0"));
    CHECK(e0->prop(abi::PROP_SourceRect) == u"192,32,224,64"); // l3
    CHECK_EQ(e0->location().x + e0->size().x / 2, 21 + cb.layout().extraCenter[0].x);
}

TEST(crossbar_mod_glyph_follows_held_pane)
{
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    CHECK(!tb->child("cpModL")->willDraw()); CHECK(!tb->child("cpModR")->willDraw());
    struct Case { int pane; bool l, r; };
    const Case cases[] = {{PANE_L2, true, false}, {PANE_R2, false, true},
                          {PANE_L2R2, true, true}, {PANE_MAIN, false, false}};
    for (const Case& c : cases) {
        cb.update(0.016f, c.pane, false);
        CHECK_EQ(cb.activeMod(), c.pane);
        CHECK_EQ(tb->child("cpModL")->willDraw() ? 1 : 0, c.l ? 1 : 0);
        CHECK_EQ(tb->child("cpModR")->willDraw() ? 1 : 0, c.r ? 1 : 0);
        // the layout is moved by the pane, not by the module: the cells stay in place
        for (int k = 0; k < CLUSTER_COUNT; ++k) CHECK(cb.cellShown(CLUSTERS[k].slot));
    }
}

TEST(crossbar_guard_returns_cell_moved_by_client)
{
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    NodePtr c = cb.cell(CLUSTERS[2].slot);
    UIPoint was = c->location();
    c->setLocation(was.x + 77, was.y + 55);              // the client moved the cell (Pack)
    cb.update(0.30f, PANE_MAIN, false);
    CHECK_EQ(c->location().x, was.x); CHECK_EQ(c->location().y, was.y);
}

struct FakeBinds : BindSource {
    bool label(const char* cmd, std::u16string& out) override
    {
        if (std::string(cmd) == "CMD_uiToolbarSlot07") { out = u"CRS"; return true; }
        return false;
    }
};

TEST(crossbar_bind_badges_from_inputmap)
{
    MockPtr hud = makeHud(); MockPtr tb = std::static_pointer_cast<MockNode>(hud->child("Toolbar"));
    Crossbar cb; attachTo(tb, cb);
    FakeBinds fb; cb.setBindSource(&fb);
    cb.update(0.016f, PANE_MAIN, false);
    MockPtr b7 = std::static_pointer_cast<MockNode>(tb->child("cpBind7"));
    MockPtr b0 = std::static_pointer_cast<MockNode>(tb->child("cpBind0"));
    CHECK(b7->text() == u"CRS"); CHECK(b7->willDraw());
    CHECK(!b0->willDraw());                              // no binding - the badge is hidden
}

TEST(glyph_has_art_for_stick_presses)
{
    using G = crossbar::Glyph;
    // L3/R3 are in the atlas: until 9 Sep 2026 forButton did not know that and returned
    // the cross, which is why the legend drew three identical crosses.
    CHECK_EQ((int)G::forButton(B_L3), (int)G::L3);
    CHECK_EQ((int)G::forButton(B_R3), (int)G::R3);
    CHECK_EQ((int)G::forButton(B_CROSS), (int)G::FACE_BOTTOM);
    CHECK_EQ((int)G::forButton(B_CIRCLE), (int)G::FACE_RIGHT);
    CHECK_EQ((int)G::forButton(B_L2), (int)G::TR_L);
    // Every key gets its own atlas tile, or two buttons would show one picture.
    const int keys[] = {G::DPAD_UP, G::FACE_BOTTOM, G::SH_L, G::TR_R, G::L3, G::R3};
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j) {
            UIRect a = G::rect(keys[i]), b = G::rect(keys[j]);
            CHECK(a.left != b.left || a.top != b.top);
        }
}

TEST(glyph_atlas_geometry_matches_generator)
{
    using G = crossbar::Glyph;
    // Checked against tools/glyphs.py: its ORDER and Key here have to match, or a
    // button shows the wrong picture.
    UIRect r = G::rect(G::DPAD_UP);   CHECK_EQ(r.left, 0);   CHECK_EQ(r.top, 0);
    r = G::rect(G::FACE_BOTTOM);      CHECK_EQ(r.left, 224); CHECK_EQ(r.top, 0);
    r = G::rect(G::L3);               CHECK_EQ(r.left, 192); CHECK_EQ(r.top, 32);
    r = G::rect(G::STICK_R);          CHECK_EQ(r.left, 32);  CHECK_EQ(r.top, 64);
    // All within 256x256: the atlas must not be made bigger, 512 loads empty.
    for (int k = G::DPAD_UP; k <= G::SOLID; ++k) {
        UIRect c = G::rect(k);
        CHECK(c.right <= 256); CHECK(c.bottom <= 256);
    }
    // A stick deflection and a press on the stick are different pictures.
    CHECK(G::rect(G::STICK_R).left != G::rect(G::R3).left || G::rect(G::STICK_R).top != G::rect(G::R3).top);
    // The frame bars come from their own fill tile, inset from the edges: borrowed off a
    // neighbouring sprite, the frame vanished the moment the tile size changed.
    UIRect sr = G::solidRect(), cell = G::rect(G::SOLID);
    CHECK(sr.left > cell.left); CHECK(sr.top > cell.top);
    CHECK(sr.right < cell.right); CHECK(sr.bottom < cell.bottom);
    CHECK(sr.right > sr.left); CHECK(sr.bottom > sr.top);
    for (int k = G::DPAD_UP; k < G::SOLID; ++k) {      // the fill does not overlap the glyphs
        UIRect c = G::rect(k);
        CHECK(c.left != cell.left || c.top != cell.top);
    }
}

TEST(glyph_family_follows_the_recognised_pad)
{
    using G = crossbar::Glyph;
    G::setFamily("ds4");
    CHECK(std::string(G::atlas()) == "consoleport_glyphs");
    G::setFamily("steam");                    // Steam Input -> the game sees an Xbox pad
    CHECK(std::string(G::atlas()) == "consoleport_glyphs_xbox");
    G::setFamily(nullptr);                    // an unknown pad - the default set
    CHECK(std::string(G::atlas()) == "consoleport_glyphs");
    // The tiles do not depend on the set - the generator's ORDER is shared, or the
    // glyphs would move on every switch.
    UIRect a = G::rect(G::FACE_BOTTOM);
    G::setFamily("steam");
    UIRect b = G::rect(G::FACE_BOTTOM);
    CHECK_EQ(a.left, b.left); CHECK_EQ(a.top, b.top);
    G::setFamily("ds4");                      // put it back so as not to affect the neighbors
}
