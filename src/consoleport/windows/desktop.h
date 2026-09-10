// Any interface window, not a list known in advance.
//
// Windows live in two places. Most are on the desktop: CuiWorkspace::addMediator makes
// the window page a direct child of GroundHUD (HudSpace in space), and the focused one
// is kept FIRST in the child list (MoveChild(Top) on raise; the client finds focus by
// the same "first visible mediator" walk in CuiWorkspace::updateGlow). A minority never
// gets there and stays where the markup put it: GameMenu is a page of the root, the
// message box in MsgBox, options and maps inside their own folder pages (Opt,
// PlanetMap, pda and the other dotted paths from the mediator factory).
//
// So the search runs from the ROOT in draw order, first child topmost: the desktop is
// taken apart by children, a folder page is passed through, anything else is tried as a
// window. Root order matters - ui_root.ui includes the game menu before the ground hud,
// i.e. above it, and the backdrop last, i.e. below everything.
//
// The default layout, shared by every window:
//   D-pad across the widgets; cross presses (on a volume cell a double click =
//   performDefaultAction); square the item radial; triangle pick up / put down;
//   circle cancel a move or close; L1/R1 tabs; L2/R2 scroll; R3 examine.
//
// Lists (journal, mail, datapad, options) have no row widgets: UIList draws the rows
// itself and moves them with the arrow keys over the FOCUSED list (UIList.cpp: KeyDown
// Up/Down/PageUp/PageDown/Home/End). Hence a list mode: cross enters the list (we set
// the Focus property, as the markup does), the D-pad then walks the rows, cross picks
// one with the same Enter a double click sends, circle steps back out to the widgets.
// Left/right leave the list on their own. The hint bar shows when the mode is on.
//
// A window that needs more gets a profile (addProfile) - an ordinary cursor::Window
// whose page() matched the current one. It is asked for the rules, the hints and the
// buttons, with the generic layout as the fallback.
#pragma once
#include "../cursor/stack.h"
#include <memory>
#include <string>
#include <vector>

namespace cp { namespace windows {

class Desktop : public cursor::Window {
public:
    explicit Desktop(NodePtr root);
    void addHud(const char* name);                       // the desktop name (several are allowed)
    void addProfile(std::unique_ptr<cursor::Window> p);  // a refinement for one specific window
    void addSkip(const std::string& csvNames);           // ini: uiSkip=name,name - do not treat as windows

    const char* id() const override { return id_.c_str(); }
    NodePtr page() const override;
    int priority() const override { return 10; }
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onTick(cursor::Context&, float) override;
    void onBlur(cursor::Context&) override;

    cursor::Window* profile() const { return profile_; }  // the current window's profile, if any
    bool inList() const { return list_ != nullptr; }      // the D-pad is leading list rows
    // What the desktop sees and why it skips things - one trace line per root and
    // folder page. Cheaper than deducing names and paths from the sources.
    std::vector<std::string> survey() const;

    // Generic helpers, the profiles use them too.
    // Liveness is judged by us, not by the client: its CanSelect refuses visible
    // inventory buttons (8 Sep 2026). The test is the node scanner's - drawn, enabled,
    // takes input.
    static bool selectable(const NodePtr& n);
    static NodePtr closeButton(const NodePtr& page);      // the window's close box or a cancel button
    static std::vector<NodePtr> tabs(const NodePtr& page);// the buttons of a tab set
    static bool hasInteractive(const NodePtr& page);      // whether there is a single navigation node inside
    bool isWindowName(const std::string& name) const;     // the name is not one of the HUD furniture
    bool isWindow(const NodePtr& page) const;             // the page looks like a window

private:
    NodePtr find() const;                                 // the topmost open window
    // A window inside a page: the page itself first, then depth levels down. Once a
    // window is found we stop, or we would return its inner panel instead of it.
    NodePtr findIn(const NodePtr& page, int depth) const;
    NodePtr pick(const NodePtr& page) const;              // a folder page -> the window inside it
    bool isHudName(const std::string& name) const;
    void retune(const NodePtr& page) const;               // the window changed: profile, rules, name
    bool clickNode(cursor::Context& c, const NodePtr& n);
    bool enterList(cursor::Context& c, const cursor::NodeInfo& n);
    void leaveList() { list_ = nullptr; listHeld_ = 0; }
    void syncList(cursor::Context& c);                    // we left the list node - so we left the list

    NodePtr root_;
    std::vector<std::string> huds_, skip_;
    std::vector<std::unique_ptr<cursor::Window>> profiles_;
    // The window page is never kept between frames: the client frees it and there is
    // no way to tell a stale pointer from a live one - the inventory died on that
    // (9 Sep 2026). Only the raw handle survives, for comparison, never dereferenced.
    mutable void* curHandle_ = nullptr;
    // Tabs are looked up on a window change, not per frame: the hints rebuild every
    // frame and finding a tab set walks the whole window.
    mutable bool hasTabs_ = false;
    mutable cursor::Window* profile_ = nullptr;
    mutable cursor::ScanRules rules_;
    mutable std::string id_ = "desktop";
    mutable std::string window_;                          // the current window's name
    mutable int tab_ = 0;                                 // the selected tab (the client does not report it)
    // The list we entered: raw handle only, same caution as the window page.
    mutable void* list_ = nullptr;
    // D-pad held in a list: the client moves the rows, we drive the repeat - synthetic
    // arrows arrive one at a time and nothing sends us KeyRepeat.
    int listHeld_ = 0;                    // -1 up, +1 down, 0 released
    float listRepeat_ = 0;
    int scrollHeld_ = 0; float scrollRepeat_ = 0;   // we keep scrolling while the trigger is held
};

}} // namespace cp::windows
