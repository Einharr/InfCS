// The crossbar on the client's stock toolbar (7 Sep 2026 model).
//
// The client keeps the slots - SwgCuiToolbar owns icons, drops and execution. We move
// the ten cells of 'volume' (children '0'..'9') and add decor: a backdrop under each
// cross and under the L3/R3 block, a glyph in the centre of each cross, and the
// modifier glyph under the bar.
//
// A SET IS A PANE, not a slot range: a pane holds 24 slots and as many CuiDragInfo, so
// the forty assignments live across panes (rest 0, L2 1, R2 2, both 3). The input map
// switches the pane on hold, the client repopulates the cells, we only hold geometry.
//
// populateSlot DESTROYS a cell's widget and puts a new one under the same name, so our
// pointers go stale on every pane change. The guard watches cell '0''s handle and
// re-takes all of them when it changes.
//
// ASSIGNING INTO A FLYOUT goes through 'mailboxes': under each flyout cell sits a real
// toolbar cell from the free slots 10..12. The client drops into it by its own route
// (OnWidgetDragDropEnd -> setToolbarItem on the active pane); we see the cell turn into
// a button and move the item to the pane it belongs to. We never assemble a CuiDragInfo
// ourselves - every move and clear is the client's own operator= copying an item.
//
// THE FLYOUTS show other panes, and nothing lets us read those: the data is in
// m_toolbarItemPanes inside the mediator. Hence the SNAPSHOT - the client draws any pane
// into cells 0..9 itself, so within one frame we switch pane by its own toolbarPaneNN
// action, clone the ten cells and switch back. Nothing intermediate reaches the screen
// and no new ABI is needed.
#pragma once
#include "geometry.h"
#include "../core/node.h"
#include "../core/tick.h"
#include <string>

namespace cp { class Ui; }

namespace cp { namespace crossbar {

// Where a slot's binding label comes from (the client: InputMap via CuiInputNames).
class BindSource {
public:
    virtual ~BindSource() {}
    virtual bool label(const char* cmdName, std::u16string& out) = 0;   // "CMD_uiToolbarSlot07" -> "L2+TRI"
};

// The tools/glyphs.py atlas: 256x256, four 64 px columns. Not a cosmetic choice -
// 256x320 kills the client on load and 512x512 loads silently empty.
struct Glyph {
    // Order must match ORDER in tools/glyphs.py - the index picks the tile. STICK_L/R
    // are the sticks themselves (deflection), not the L3/R3 presses.
    enum Key { DPAD_UP, DPAD_LEFT, DPAD_RIGHT, DPAD_DOWN, FACE_TOP, FACE_LEFT, FACE_RIGHT, FACE_BOTTOM,
               SH_L, SH_R, TR_L, TR_R, DPAD, FACE_ALL, L3, R3, STICK_L, STICK_R,
               SOLID,                          // 18: solid fill, the frame's bars come from it
               // 19..21: the right cross in context mode.
               ACT_RADIAL, ACT_USE, ACT_CLEAR };
    // 32 px tiles, not 64: the atlas is stuck at 256x256 and sixteen tiles ran out.
    // 8x8 gives 64 places, and the glyphs are drawn at 24-28 px anyway.
    static const int COLS = 8, TILE = 32;
    static UIRect rect(int key) { int x = (key % COLS) * TILE, y = (key / COLS) * TILE; UIRect r = {x, y, x + TILE, y + TILE}; return r; }
    // The name depends on the pad: through Steam Input the game sees a virtual Xbox
    // controller, and PlayStation faces would be a lie. Both atlases ship.
    static const char* atlas();
    static void setFamily(const char* profile);   // "steam" -> xbox, otherwise ps
    // Inset from the tile edges: stretched edges pull the neighbours in through
    // filtering. The frame's bars used to borrow a dense spot of a neighbouring sprite
    // at (26,8); the tile resize made it transparent and the frame vanished
    // (9 Sep 2026). The fill has its own tile now.
    static UIRect solidRect() { UIRect r = rect(SOLID); const int in = TILE / 4;
                                r.left += in; r.top += in; r.right -= in; r.bottom -= in; return r; }
    static Key forButton(gen::Button b);
    static Key forCross(int cross) { return cross == 0 ? DPAD : FACE_ALL; }
};

class Crossbar {
public:
    explicit Crossbar(const Metrics& m = Metrics());
    // toolbar: the toolbar page (/GroundHUD.Toolbar). imageTemplate/textTemplate: any
    // Image/Text to clone decor from, frameTemplate: any Page for the backdrops.
    // 0 in either means no decor / no backdrops.
    bool attach(NodePtr toolbar, NodePtr imageTemplate, NodePtr textTemplate, NodePtr frameTemplate = NodePtr());
    void detach();
    bool attached() const { return volume_ != nullptr; }
    void setBindSource(BindSource* s) { binds_ = s; bindsDirty_ = true; }
    void refreshBindings() { bindsDirty_ = true; }
    // Rig knob: hold a cluster's flyout open regardless of the mouse (-1 = on hover).
    // Working remotely there is nothing to point with.
    void forceReveal(int cluster) { forced_ = cluster; }
    // Context mode: on a peaceful target the right cross's faces mean actions on it,
    // not slots - green backdrop, icons and labels over the cells, and the pad holds
    // those buttons back (Pad::suppressButtons) so the client never sees them.
    // A hostile target only recolours the bar: in combat the faces stay abilities.
    enum Context { CTX_OFF = 0, CTX_PEACEFUL, CTX_HOSTILE };
    void setContext(int mode);
    int  context() const { return ctx_; }
    // Rig: make every flyout read one given pane instead of L2/L2+R2/R2 (-1 = normal).
    void forceFanPane(int pane) { fanPane_ = pane; }
    // Rig: move a cluster's main item into the L2 layer and back. Nothing to drag with
    // remotely, and the mediator + setToolbarItem pair needs checking on a live client.
    bool copyMainToLayer(int cluster);
    // Put a command from the command browser into pane/slot. With no CuiDragInfo of our
    // own we use the client's drag onto a live main-set cell, then move the item where it
    // belongs and put back whatever stood there. Same trick as collectDrops.
    bool bindFrom(Ui& ui, const NodePtr& source, int pane, int slot);
    bool clearLayer(int cluster);

    // The tick. activeMod (0 rest, 1 L2, 2 R2, 3 both) only drives the modifier glyph -
    // the layout follows the pane. Returns the number of cells corrected.
    int update(float dt, int activeMod, bool dragActive = false);

    // tests and debugging
    UIRect cellRect(int slot) const { return last_[slot]; }        // relative to the volume
    bool   cellShown(int slot) const { return shown_[slot]; }
    const BarLayout& layout() const { return bar_; }
    const Metrics& metrics() const { return m_; }
    NodePtr cell(int slot) const { return cell_[slot]; }
    NodePtr volume() const { return volume_; }
    int activeMod() const { return mod_; }
    // Screen centre of the modifier glyphs. The targeting hints line up either side,
    // so triggers and bumpers stay in one place instead of spread over the HUD.
    UIPoint modGlyphWorld() const;
    int revealed() const { return reveal_; }                       // a cluster, or -1
    static const char* slotCommand(int slot, char* buf, size_t n);

private:
    void adaptToCell(int cellSize);   // fit the metrics to the client's real cell size
    NodePtr restoreCell(int slot);    // bring back a cell eaten by an InsertChildAfter refusal
    bool refreshCells();              // take the cells again (after populateSlot) and hide the extras
    void placeDecor();
    void fit(const NodePtr& n, const UIRect& r);   // fit a cell and its contents into a rectangle
    bool snapshotPane(int pane);                   // clones of the current pane's ten cells -> art_[pane]
    void warmPanes();                              // run through panes 1,2,3 and come back to 0
    void applyFans();
    void refreshDrops();               // re-read the receiver cells: a drop recreates them
    bool fanUnderMouse() const;        // the cursor is on the flyout itself, not on a main cell
    void pickEmptyDonor();             // find a slot known to be empty - the source of 'emptiness'
    void collectDrops();               // take what landed in the mailbox into the pane it belongs to
    void place(int slot, const UIRect& r, bool force);
    void hide(int slot);
    void applyDecor();
    int  hoveredCluster() const;
    NodePtr makeImage(const char* name, int glyphKey, const char* color, int size);
    NodePtr makeText(const char* name, int size);
    NodePtr makeFrame(const char* name, const char* color);
    void setRect(const NodePtr& n, int x, int y, int w, int h);
    void setVisible(const NodePtr& n, bool v);

    Metrics m_; BarLayout bar_;
    NodePtr toolbar_, volume_, imageTpl_, textTpl_, frameTpl_;
    NodePtr keyBindings_;                          // the stock binding labels - we keep them hidden
    NodePtr cell_[gen::SLOT_COUNT];
    NodePtr cellProbe_;                            // cell '0' as of capture: if it changed, populateSlot ran
    NodePtr frame_[gen::CROSS_COUNT];              // a cross's backdrop
    NodePtr frameExtra_;                           // the L3/R3 block's backdrop
    NodePtr glyphCross_[gen::CROSS_COUNT];         // the glyph at a cross's center
    NodePtr glyphExtra_[gen::EXTRA_COUNT];         // an L3/R3 glyph above its own cell
    NodePtr glyphMod_[2];                          // the L2 / R2 glyphs under the bar
    NodePtr bindBadge_[gen::SLOT_COUNT];           // a binding label (hidden by default)
    // Context: icon and label on the right cross's square/circle/cross cells.
    NodePtr ctxGlyph_[3], ctxLabel_[3], ctxFrame_[3];
    int ctx_ = 0; bool ctxBuilt_ = false;
    NodePtr art_[gen::PANE_SET_COUNT][gen::SLOT_COUNT];  // snapshots of each pane's cells
    NodePtr fanFrame_[3];                          // the frames of the three flyout cells
    NodePtr fanShown_[3];                          // which snapshots are shown in the flyout right now
    // The layer glyph on the flyout cell - otherwise there is no telling which set you
    // are assigning into. The middle cell needs two (L2+R2), placed diagonally so both
    // stay full size.
    NodePtr fanBadge_[3][2];
    NodePtr drop_[3];                  // the receiver cells 10..12 under the flyout
    int emptyPane_ = -1, emptySlot_ = -1;   // where a CuiDragInfo known to be empty lives
    UIRect last_[gen::SLOT_COUNT];
    bool   shown_[gen::SLOT_COUNT];
    UIPoint volumeAt_ = {0, 0};
    int mod_ = 0, reveal_ = -1; bool drag_ = false;
    BindSource* binds_ = nullptr; bool bindsDirty_ = true;
    float guardTimer_ = 0;
    int prevReveal_ = -1, forced_ = -1, fanPane_ = -1; bool warmed_ = false;
    // A recreated cell loses its 'under mouse' bit for a frame or two - the widget is
    // new. Until the counter runs out we keep the flyout that was open.
    int hoverSettle_ = 0;
    float warmCooldown_ = 0;
    unsigned diagTick_ = 0;         // a counter for the infrequent diagnostics
};

}} // namespace cp::crossbar
