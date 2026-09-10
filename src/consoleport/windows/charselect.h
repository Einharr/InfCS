// The character selection screen (the AvSel page): a cursor frame, a hint bar and a pad
// layout. Outside the window stack - the screen lives before the world exists, with no
// toolbar and no UI mode.
//
// The client does much of it already: it holds the keyboard
// (SwgCuiAvatarSelection::performActivate - setKeyboardInputActive(true),
// table->SetFocus()), walks the table rows on the arrow keys (UITable::ProcessMessage)
// and draws the selected row. Our job is to show what is under control, and hand out
// the buttons:
//
//   D-pad       frame across the widgets, up/down on the list walk the rows
//   left stick  the frame always, including out of the list to the buttons
//   cross       on the list enter the world (as a double click does), on a button press it
//   circle      Prev, or Cancel in the delete dialog
//   square      Create
//   right stick rotate the model (the viewer's CameraYaw)
//
// Triangle is left free on purpose: deleting a character means TYPING A NAME, and there
// is no on-screen keyboard here. The pad gets you to the dialog, a human finishes at
// the keyboard, circle cancels. The other buttons go quiet while the dialog is open -
// cross would otherwise reach its OK, which forwards to Next.
//
// Buttons are pressed through the Press property, not Enter: Enter goes to
// IsDefaultButton (i.e. Next) and depends on who holds focus. Only the arrows go as
// messages, because table rows are not widgets and the table moves them over the
// FOCUSED list - hence focus before every arrow.
//
// The frame and the glyphs live in the screen's own page: there is no HUD of ours here,
// and the templates come with the markup (ui_consoleport_charselect.inc, included into
// our ui_avatar_selection.inc). A clone cannot be given SourceResource at runtime, so
// there is nowhere else to get the atlas.
#pragma once
#include "../core/node.h"
#include "../core/ui.h"
#include "../cursor/cursor.h"
#include "../cursor/hints.h"
#include "keyboard.h"
#include "../input/pad.h"
#include <string>
#include <vector>

namespace cp { namespace windows {

class CharSelect {
public:
    CharSelect(NodePtr root, Ui& ui);

    // The tree root may not exist on the first frame; the runtime recreates us until
    // it does.
    bool hasRoot() const { return root_ != nullptr; }

    // The screen page if it is being drawn, otherwise empty.
    NodePtr page() const;
    bool active() const { return page() != nullptr; }

    // Once per frame; true means the screen is ours and the buttons were parsed.
    bool update(input::Pad& pad, float dt);

    // Take our widgets off the page. Idempotent; called when the screen goes away and
    // from the runtime once the world has replaced it.
    void detach();
    bool attached() const { return attached_; }

    // One trace line per widget found, printed when the screen appears. Same reason as
    // Desktop::survey: cheaper than deducing names from the markup.
    std::vector<std::string> survey() const;

    // the screen breakdown, for the tests and the trace
    static NodePtr findNamed(const NodePtr& page, const char* name, int depth = 6);
    NodePtr table() const;                       // the character list (UITable)
    NodePtr viewer() const;                      // the character model
    NodePtr button(const char* name) const;      // a screen button by name
    bool blocked() const;                        // the delete dialog is open
    bool onList() const;                         // the frame stands on the character list
    bool listMode() const;                       // the D-pad leads the rows (frame or no frame)
    std::vector<cursor::Hint> hints() const;
    const cursor::Cursor& cursor() const { return cur_; }

private:
    bool looksLikeCharSelect(const NodePtr& p) const;
    void attach(const NodePtr& p);
    void placeHints(const NodePtr& p);
    void navigate(input::Pad& pad, float dt);
    void ringStep(int dir);                      // 0 up 1 down 2 left 3 right
    void rowArrow(int dir);                      // a row up or down
    bool pressNamed(const char* name, const char* what);
    void rotate(float x, float dt);

    NodePtr root_;
    Ui& ui_;
    cursor::Cursor cur_;
    cursor::HintBar hints_;
    cursor::ScanRules rules_;
    void* curHandle_ = nullptr;                  // the screen page: for comparison only
    bool attached_ = false;
    int rowHeld_ = 0; float rowRepeat_ = 0;      // holding the D-pad on the list
    int stickDir_ = -1; float stickWait_ = 0;    // stick deflection: a repeat of its own
    bool noYaw_ = false;                         // the viewer did not report CameraYaw - we stop trying
    bool wasBlocked_ = false;
    Keyboard kb_;                                // our own, on the screen page
};

}} // namespace cp::windows
