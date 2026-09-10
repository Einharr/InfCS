// The on-screen keyboard - the one thing the pad could not do at all.
//
// The client has none: no osk, no VirtualKeyboard, no onscreen anywhere in the binary,
// only AFK strings and WinAPI imports (checked 10 Sep 2026). So we draw it out of
// widget clones, the way GameBar draws the radial.
//
// TYPING. The UI library takes a character in one message: UIMessage with Type=Character
// and Keystroke = the UTF-16 code (UITextBox.cpp:484 inserts anything >= 0x20). That is
// what Ui::character sends. Backspace and Enter are not Character but KeyDown/KeyUp with
// the UIMessage constants, and in a single-line box Enter is handled by
// CuiConsoleHelper, which is what actually sends the chat line.
//
// NAVIGATION IS OURS. The cursor scanner only takes interactive types
// (cursor/nodes.cpp) or cells inside a VolumePage, so our page clones never become
// nodes. The keyboard keeps its own (row, col), reads the D-pad and the stick itself and
// paints the current key itself. Hence usesStick() == true: the stack must not walk a
// frame over an empty list.
//
// LATIN-1 ONLY - that is the whole coverage of the client's stock fonts
// (stock/ui/font/verdana_bold_12.inc: 191 glyphs, Latin-1 plus the euro sign).
#pragma once
#include "../cursor/stack.h"
#include <string>
#include <vector>

namespace cp { namespace windows {

class Keyboard : public cursor::Window {
public:
    // overlay: the HUD overlay. frameTemplate: a Page for the backdrop and the keys.
    // textTemplate: a Text for the captions. imageTemplate: for the legend glyphs off
    // our atlas.
    void attach(NodePtr overlay, NodePtr frameTemplate, NodePtr textTemplate, NodePtr imageTemplate = NodePtr());
    void detach();
    // No template, no keys - and a keyboard like that must not open and take the pad.
    bool attached() const { return overlay_ != nullptr && frameTpl_ != nullptr; }

    // target: where to type; empty means whatever the client has focused, which is how
    // chat opens. swallowCross eats the first cross, since cross on an input field is
    // what opened us (nothing to eat on the L3+R3 chord). preferBottom keeps us at the
    // bottom when there is no target: in the world people type into chat, which is
    // itself at the bottom, so we go above it - but a screen with no chat wants the
    // bottom centre.
    void open(NodePtr target, bool swallowCross, bool preferBottom = false);
    void close(Ui* ui);          // ui is needed to fold the chat line away if we opened it
    bool openedChat() const { return openedChat_; }

    const char* id() const override { return "keyboard"; }
    NodePtr page() const override { return backdrop_; }
    bool isOpen() const override { return open_; }
    // Above everyone while typing: GameBar 120, Popup 100, desktop and profiles 10.
    int priority() const override { return 140; }
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onTick(cursor::Context&, float dt) override;
    void onBlur(cursor::Context&) override;
    bool usesStick() const override { return true; }

    // tests and debugging
    int row() const { return row_; }
    int col() const { return col_; }
    bool symbols() const { return symbols_; }
    bool shift() const { return shift_ || caps_; }
    bool caps() const { return caps_; }
    NodePtr target() const { return target_; }
    size_t keyCount() const { return keys_.size(); }
    std::u16string label(int row, int col) const;

private:
    struct Key { NodePtr frame, text; };
    void build();
    void teardown();
    void paint();                       // highlight the current key
    void relabel();                     // the layer or the case changed - rewrite the captions
    void legend();                      // two hint lines under the keyboard
    void cycleShift();                  // one case ladder, for the Shift key and for triangle
    void move(int dr, int dc);
    bool pressCurrent(cursor::Context& c);
    void type(cursor::Context& c, char16_t ch);
    void sendEnter(cursor::Context& c);
    void focusTarget();
    static char16_t charAt(int row, int col, bool symbols);

    NodePtr overlay_, frameTpl_, textTpl_, imageTpl_, backdrop_, target_;
    // Our own legend, in two lines: nine hints in one row run off the screen and the
    // stack bar does one line only.
    cursor::HintBar legend1_, legend2_;
    std::vector<Key> keys_;
    cursor::ScanRules rules_;
    bool open_ = false, symbols_ = false, shift_ = false, caps_ = false, openedChat_ = false;
    int row_ = 1, col_ = 0, painted_ = -1;
    // The first cross after opening is swallowed: it is the press that opened us.
    bool swallowCross_ = false;
    bool preferBottom_ = false;   // keep to the bottom when there is no target
    int stickDir_ = -1; float stickWait_ = 0.f;
};

}} // namespace cp::windows
