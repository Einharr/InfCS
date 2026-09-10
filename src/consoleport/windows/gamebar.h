// The game menu radial: the twelve /GroundHUD.ButtonBar buttons, on a ring in the
// middle of the screen. No UIRadialMenu of our own and nothing done to
// CuiRadialMenuManager - its createMenu wants a REFERENCE to an object, the contents
// come from the server, and the client cannot do an empty radial at all. The panel
// buttons already exist and know how to press themselves, so we use those.
//
// ButtonBar holds a UIVolumePage 'vs' (CellCount 12x1) - the same class as the toolbar
// volume, so the trick is the crossbar's: lay the cells out ourselves and skip Pack for
// that volume through packguard, or it puts them back in a row every frame.
#pragma once
#include "../cursor/stack.h"
#include "../world/ringdecor.h"
#include <functional>
#include <string>
#include <vector>

namespace cp { namespace windows {

class GameBar : public cursor::Window {
public:
    typedef std::function<NodePtr()> PageFn;               // /GroundHUD.ButtonBar or 0
    explicit GameBar(PageFn fn);
    const char* id() const override { return "gamebar"; }
    NodePtr page() const override { return fn_(); }
    // The panel is always visible, so "open" is our own flag, not the page's willDraw -
    // otherwise UI mode would never turn off.
    bool isOpen() const override { return open_; }
    int priority() const override { return 120; }          // above the popup (100)
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onTick(cursor::Context&, float dt) override;
    bool usesStick() const override { return true; }   // it turns the sectors with the stick itself
    void onFocus(cursor::Context&) override;
    void onBlur(cursor::Context&) override;

    // Same styling as the target ring - backdrop, rim, wedge: two rings that look
    // different are two interfaces. overlay is the HUD, imageTemplate an Image off our
    // texture.
    void attachDecor(NodePtr overlay, NodePtr imageTemplate, NodePtr textTemplate = NodePtr())
    { decor_.attach(overlay, imageTemplate); textTpl_ = textTemplate; }

    // An item's caption. The panel buttons have meaningful names (inventory,
    // buttonJournal) but show only an icon, with the meaning in a mouse tooltip we do
    // not have - so the caption is the tooltip, or the name if there is none.
    static std::u16string labelOf(const NodePtr& cell);

    bool toggle();                                          // on Options; true = we opened it
    bool open() const { return open_; }
    NodePtr volume() const;                                 // 'vs' inside the panel
    std::vector<NodePtr> cells() const;                     // the volume's cells
    int hoverSector() const { return sector_; }
    // Offset of sector i's centre from the ring's. Sector 0 at the top, clockwise, as in
    // Popup::sectorOf - otherwise the highlight drifts off the selection.
    static void ringPoint(int i, int count, int radius, int& dx, int& dy);
    // The radius at which count cells of side cell clear each other, gap included.
    static int  ringRadius(int count, int cell, int gap);
private:
    PageFn fn_; cursor::ScanRules rules_; int sector_ = -1; bool open_ = false;
    world::RingDecor decor_; int radius_ = 0;
    // The ButtonBar panel is never moved: SwgCuiButtonBar declares
    // setSettingsAutoSizeLocation(true, true), so the client saves the page's Location
    // and Size into the player's settings. Move it to the centre and the next save
    // (logout, character change, UI reload) writes our coordinates into the profile and
    // the panel stays mid-screen for good. So the ring is built from CLONES in the HUD
    // overlay, and the press goes to the original - the clone is only drawn.
    NodePtr overlay_, textTpl_;
    std::vector<NodePtr> clones_, origin_, labels_;
    bool laid_ = false;
    void layout(); void restore();
};

}} // namespace cp::windows
