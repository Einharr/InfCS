#include "keyboard.h"
#include "../core/theme.h"
#include "../abi/props.h"
#include "../core/runtime.h"
#include "../crossbar/crossbar.h"
#include <cmath>
#include <cstdio>

namespace cp { namespace windows {

using namespace cursor; using namespace gen;

namespace {
// 10x4 plus a service row. QWERTY rows, so letters are found by eye.
const int COLS = 10, ROWS = 5, SPECIAL_ROW = 4;
const char* const LETTERS[4] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL'",
    "ZXCVBNM,.?",
};
const char* const SYMBOLS[4] = {
    "!@#$%^&*()",
    "-_=+[]{}<>",
    ".,:;/\\|~`\"",
    // Digits on both layers on purpose: coordinates and credit sums are typed mixed
    // with symbols, and a layer switch costs more than ten repeated keys.
    "1234567890",
};
// The service row: five keys, two columns each.
enum Special { SP_SPACE = 0, SP_BACK, SP_ENTER, SP_SHIFT, SP_CLOSE, SP_COUNT };
const char* const SPECIAL_LABEL[SP_COUNT] = {"Space", "Back", "Enter", "Shift", "Close"};

const int KEY_W = 40, KEY_H = 30, GAP = 3, PAD = 10;
// Palette entry names, not numbers - the ones the stock interface paints itself with
// (PalColor='icondefault' appears 254 times in the HUD markup). Change the theme and
// the keyboard follows, with nothing to rebuild.
const char* PAL_FRAME = "icondefault";     // frames and outlines
const char* PAL_PANEL_BG = "back1";        // the panel background
const char* PAL_KEY_BG = "back3";          // a key background, a little lighter than the panel
const char* PAL_TEXT = "text1";            // a key caption
// The selection stays an explicit colour: no palette name for "selected" works across
// every theme, and this used to be dark on dark - the key under the cursor invisible.
const char* COLOR_PICK = "#FFDD55";
const char* COLOR_PICK_TEXT = "#FFFFFF";
// The backdrop reuses the thick frame already proven on the live client by the ring
// labels and the crossbar. On a 40x30 key that is too much, so keys get a thin outline;
// if it does not take, the background and the caption still read.
const char* STYLE_PANEL = "/Styles.New.fatFrameInside_palW.rs_default";
const char* STYLE_KEY = "/Styles.New.whiteOutline_palW.rs_default";


int gridW() { return COLS * KEY_W + (COLS - 1) * GAP; }
int gridH() { return ROWS * KEY_H + (ROWS - 1) * GAP; }
} // namespace

// The tables are upper case; typing and captioning lower it.
char16_t Keyboard::charAt(int row, int col, bool symbols)
{
    if (row < 0 || row >= 4 || col < 0 || col >= COLS) return 0;
    return static_cast<char16_t>((symbols ? SYMBOLS : LETTERS)[row][col]);
}

std::u16string Keyboard::label(int row, int col) const
{
    std::u16string s;
    if (row == SPECIAL_ROW) {
        const int i = col / 2;
        if (i >= 0 && i < SP_COUNT)
            for (const char* p = SPECIAL_LABEL[i]; *p; ++p) s.push_back(static_cast<char16_t>(*p));
        return s;
    }
    char16_t ch = charAt(row, col, symbols_);
    if (!ch) return s;
    // Lower case until Shift: the player has to see what will actually be typed.
    if (!symbols_ && !shift() && ch >= u'A' && ch <= u'Z')
        ch = static_cast<char16_t>(ch - u'A' + u'a');
    s.push_back(ch);
    return s;
}

void Keyboard::attach(NodePtr overlay, NodePtr frameTemplate, NodePtr textTemplate, NodePtr imageTemplate)
{
    detach();
    overlay_ = overlay; frameTpl_ = frameTemplate; textTpl_ = textTemplate; imageTpl_ = imageTemplate;
    // Different prefixes: the client collapses same-named clones under one parent.
    legend1_.setPrefix("cpKbdL1"); legend2_.setPrefix("cpKbdL2");
    if (overlay_ && imageTpl_ && textTpl_) {
        legend1_.attach(overlay_, imageTpl_, textTpl_, 0, 0);
        legend2_.attach(overlay_, imageTpl_, textTpl_, 0, 0);
    }
}

void Keyboard::detach()
{
    teardown();
    legend1_.detach(); legend2_.detach();
    overlay_.reset(); frameTpl_.reset(); textTpl_.reset(); imageTpl_.reset();
}

void Keyboard::teardown()
{
    if (overlay_) {
        for (const Key& k : keys_) {
            if (k.frame) overlay_->removeChild(k.frame);
            if (k.text) overlay_->removeChild(k.text);
        }
        if (backdrop_) overlay_->removeChild(backdrop_);
    }
    keys_.clear(); backdrop_.reset();
    legend1_.clear(); legend2_.clear();
    open_ = false; painted_ = -1; shift_ = false; caps_ = false; symbols_ = false;
    swallowCross_ = false; stickDir_ = -1; stickWait_ = 0.f;
    openedChat_ = false; target_.reset();
}

void Keyboard::build()
{
    if (!overlay_ || !frameTpl_) return;
    const UISize screen = overlay_->size();
    const int w = gridW() + 2 * PAD, h = gridH() + 2 * PAD;
    const int x = (screen.x - w) / 2;
    // Do not cover what is being typed into. The chat line sits at the bottom
    // (176,623 as shipped), so a low target pushes us up.
    int y = screen.y - h - 40;
    if (target_) {
        const UIRect r = target_->worldRect();
        const UIPoint o = overlay_->worldLocation();
        if (r.top - o.y > screen.y / 2) y = 40;
    } else if (!preferBottom_) {
        // With no target in the world people type into chat, which is at the bottom -
        // so we go up. On screens with no chat that rule put the keyboard at the top,
        // over the screen itself.
        y = 40;
    }

    char nm[32];
    backdrop_ = frameTpl_->clone();
    if (backdrop_) {
        backdrop_->setPropA(abi::PROP_Name, "cpKeyBack");
        backdrop_->setPropA(abi::PROP_GetsInput, "false"); backdrop_->setPropA(abi::PROP_AbsorbsInput, "false");
        backdrop_->setPropA(abi::PROP_PackLocation, "nfn,nfn"); backdrop_->setPropA(abi::PROP_PackSize, "f,f");
        backdrop_->setPropA(abi::PROP_RStyleDefault, STYLE_PANEL);
        theme::paint(backdrop_, theme::ROLE_COLOR, PAL_FRAME);
        backdrop_->setPropA(abi::PROP_BackgroundOpacity, "0.85");
        std::snprintf(nm, sizeof nm, "%d,%d", w, h); backdrop_->setPropA(abi::PROP_ScrollExtent, nm);
        backdrop_->setSize(w, h);
        backdrop_->setLocation(x, y);
        // The template is a toolbar corner with captions of its own; we want the
        // ground only.
        for (const NodePtr& c : backdrop_->children()) if (c->isA(T_Widget)) c->setVisible(false);
        theme::paint(backdrop_, theme::ROLE_BG, PAL_PANEL_BG);
        // Visibility set explicitly, as for the keys below: a clone inherits it from
        // the template and the templates are hidden - which is why on character select
        // the keys showed up and the backdrop did not.
        backdrop_->setVisible(true);
        overlay_->addChild(backdrop_); backdrop_->link(); overlay_->moveChild(backdrop_, 2);
    }

    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            // Five double-width keys: only the even columns are drawn.
            const bool special = r == SPECIAL_ROW;
            if (special && (c % 2) != 0) { keys_.push_back(Key()); continue; }
            const int kw = special ? KEY_W * 2 + GAP : KEY_W;
            const int kx = x + PAD + c * (KEY_W + GAP);
            const int ky = y + PAD + r * (KEY_H + GAP);

            Key k;
            k.frame = frameTpl_->clone();
            if (k.frame) {
                std::snprintf(nm, sizeof nm, "cpKeyF%d_%d", r, c);
                k.frame->setPropA(abi::PROP_Name, nm);
                k.frame->setPropA(abi::PROP_GetsInput, "false"); k.frame->setPropA(abi::PROP_AbsorbsInput, "false");
                k.frame->setPropA(abi::PROP_PackLocation, "nfn,nfn"); k.frame->setPropA(abi::PROP_PackSize, "f,f");
                k.frame->setPropA(abi::PROP_RStyleDefault, STYLE_KEY);
                theme::paint(k.frame, theme::ROLE_COLOR, PAL_FRAME);
                theme::paint(k.frame, theme::ROLE_BG, PAL_KEY_BG);
                k.frame->setPropA(abi::PROP_BackgroundOpacity, "0.45");
                std::snprintf(nm, sizeof nm, "%d,%d", kw, KEY_H); k.frame->setPropA(abi::PROP_ScrollExtent, nm);
                k.frame->setSize(kw, KEY_H);
                k.frame->setLocation(kx, ky);
                for (const NodePtr& ch : k.frame->children()) if (ch->isA(T_Widget)) ch->setVisible(false);
                k.frame->setVisible(true);
                overlay_->addChild(k.frame); k.frame->link(); overlay_->moveChild(k.frame, 2);
            }
            if (textTpl_) {
                k.text = textTpl_->clone();
                if (k.text) {
                    std::snprintf(nm, sizeof nm, "cpKeyT%d_%d", r, c);
                    k.text->setPropA(abi::PROP_Name, nm);
                    k.text->setPropA(abi::PROP_GetsInput, "false"); k.text->setPropA(abi::PROP_AbsorbsInput, "false");
                    k.text->setPropA(abi::PROP_PackLocation, "nfn,nfn"); k.text->setPropA(abi::PROP_PackSize, "f,f");
                    k.text->setPropA(abi::PROP_BackgroundOpacity, "0.00");
                    k.text->setPropA(abi::PROP_TextAlignment, "Center");
                    k.text->setPropA(abi::PROP_Font, special ? "bold_11" : "bold_13");
                    theme::paint(k.text, theme::ROLE_TEXT, PAL_TEXT);
                    std::snprintf(nm, sizeof nm, "%d,%d", kw, KEY_H); k.text->setPropA(abi::PROP_ScrollExtent, nm);
                    k.text->setSize(kw, KEY_H);
                    k.text->setLocation(kx, ky + 4);
                    k.text->setLocalText(label(r, c).c_str());
                    k.text->setVisible(true);
                    overlay_->addChild(k.text); k.text->link(); overlay_->moveChild(k.text, 2);
                }
            }
            keys_.push_back(k);
        }
    }
    painted_ = -1;
    paint();
    legend();
}

void Keyboard::legend()
{
    if (!backdrop_) return;
    // Nine hints in one row run off the screen: typing above, modes and exit below.
    std::vector<Hint> a, b;
    a.push_back(Hint{B_CROSS, row_ == SPECIAL_ROW ? label(row_, col_) : std::u16string(u"Type")});
    a.push_back(Hint{B_SQUARE, u"Back"});
    a.push_back(Hint{B_L1, u"Space"});
    a.push_back(Hint{B_R2, u"Send"});
    // Navigation goes in our own legend, not the stack bar: the bar has a fixed place
    // by the crossbar while the keyboard lands wherever it fits, and in game the two
    // overlapped (10 Sep 2026). One window, one legend - hence an empty hints().
    b.push_back(Hint{B_DPAD_UP, u"", crossbar::Glyph::DPAD});
    b.push_back(Hint{B_R3, u"Move", crossbar::Glyph::STICK_R});
    b.push_back(Hint{B_TRIANGLE, caps_ ? u"Caps" : u"Shift"});
    b.push_back(Hint{B_R1, symbols_ ? u"Letters" : u"Symbols"});
    b.push_back(Hint{B_CIRCLE, u"Close"});
    legend1_.set(a); legend2_.set(b);

    const UIPoint at = backdrop_->location();
    const UISize sz = backdrop_->size();
    const int y = at.y + sz.y + 4, step = legend1_.glyphSize() + 4;
    legend1_.moveTo(at.x + (sz.x - legend1_.contentWidth()) / 2, y);
    legend2_.moveTo(at.x + (sz.x - legend2_.contentWidth()) / 2, y + step);
    legend1_.raise(); legend2_.raise();
}

void Keyboard::relabel()
{
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            const size_t i = static_cast<size_t>(r) * COLS + c;
            if (i < keys_.size() && keys_[i].text) keys_[i].text->setLocalText(label(r, c).c_str());
        }
    legend();
}

void Keyboard::paint()
{
    const int now = row_ * COLS + col_;
    if (now == painted_) return;
    const int was = painted_;
    painted_ = now;
    struct { int idx; bool on; } upd[2] = {{was, false}, {now, true}};
    for (int u = 0; u < 2; ++u) {
        const int i = upd[u].idx;
        if (i < 0 || i >= static_cast<int>(keys_.size())) continue;
        if (keys_[i].frame) {
            // The selected key takes a number over the palette: Color overrides
            // PalColor, and going back to the theme means setting PalColor again.
            if (upd[u].on) {
                theme::paint(keys_[i].frame, theme::ROLE_COLOR, COLOR_PICK);
                keys_[i].frame->setPropA(abi::PROP_BackgroundTint, COLOR_PICK);
                keys_[i].frame->setPropA(abi::PROP_BackgroundOpacity, "0.85");
            } else {
                theme::paint(keys_[i].frame, theme::ROLE_COLOR, PAL_FRAME);
                theme::paint(keys_[i].frame, theme::ROLE_BG, PAL_KEY_BG);
                keys_[i].frame->setPropA(abi::PROP_BackgroundOpacity, "0.45");
            }
        }
        if (keys_[i].text) {
            // White on yellow reads on any theme.
            if (upd[u].on) theme::paint(keys_[i].text, theme::ROLE_TEXT, COLOR_PICK_TEXT);
            else theme::paint(keys_[i].text, theme::ROLE_TEXT, PAL_TEXT);
        }
    }
}

void Keyboard::move(int dr, int dc)
{
    row_ = (row_ + dr + ROWS) % ROWS;
    col_ = (col_ + dc + COLS) % COLS;
    // Service row: even columns only, the keys are double width.
    if (row_ == SPECIAL_ROW && (col_ % 2) != 0) col_ = (col_ + (dc >= 0 ? 1 : -1) + COLS) % COLS;
    paint();
    legend();          // the cross caption depends on which key we are standing on
}

void Keyboard::open(NodePtr target, bool swallowCross, bool preferBottom)
{
    preferBottom_ = preferBottom;
    // No templates, nothing to draw with - and opening invisibly would take the whole
    // pad with it.
    if (!attached()) { tracef("keyboard: no templates, not opening"); return; }
    if (open_) close(nullptr);
    target_ = target;
    openedChat_ = !target;
    if (openedChat_) {
        // The client focuses the chat line itself (SwgCuiChatWindow::acceptTextInput)
        // but not always on the same frame, and the first character went nowhere. So we
        // find the field and focus it before every character. By type, not by path -
        // skinned markup nests the line differently.
        NodePtr chat = overlay_ ? overlay_->byPath("ChatWindow") : NodePtr();
        if (chat) target_ = findFirst(chat, T_Textbox, 6);
        tracef("keyboard: chat line %s", target_ ? "found" : "NOT found, typing blind");
    }
    row_ = 1; col_ = 0; symbols_ = false; shift_ = false; caps_ = false;
    swallowCross_ = swallowCross;
    build();
    open_ = true;
    tracef("keyboard: opened, target %s", target_ ? target_->name().c_str() : "(chat)");
}

void Keyboard::close(Ui* ui)
{
    if (!open_ && keys_.empty()) return;
    const bool was = open_, chat = openedChat_;
    teardown();
    // Circle has to close everything, and we opened the chat line, so folding it away
    // is ours. Chat only: in any other window Escape would close the window.
    if (was && chat && ui) ui->escape();
    if (was) tracef("keyboard: closed%s", chat ? ", the chat line was folded away" : "");
}

void Keyboard::onBlur(Context& c) { close(&c.ui); }

void Keyboard::focusTarget()
{
    // The character goes to whatever the client has focused: for chat the client sets
    // that itself, for any other field we do.
    //
    // We keep the target node between frames, which is normally forbidden - the client
    // may free a widget, and that is what the inventory died on (9 Sep 2026). It is safe
    // here for one reason: while the keyboard has focus the stack keeps UI mode and the
    // window below gets no buttons, so the player cannot close or rebuild it. close()
    // nulls the target as soon as focus leaves.
    if (!target_) return;
    // Already focused: leave it, setProperty parses the string inside the client.
    NodePtr leaf = focusedLeaf();
    if (leaf && leaf->handle() && leaf->handle() == target_->handle()) return;
    target_->setPropA(abi::PROP_Focus, "true");
}


// Enter makes the client send the line and hide the field, which would leave the
// keyboard typing into nothing - so we reopen it at once. Several lines in a row are
// easier that way, and circle still leaves.
void Keyboard::sendEnter(Context& c)
{
    focusTarget();
    c.ui.keyTap(K_Enter);
    if (openedChat_) c.ui.action("startChat");
}

void Keyboard::type(Context& c, char16_t ch)
{
    if (ch < 0x20) return;                 // UITextBox silently swallows control codes
    focusTarget();
    c.ui.character(ch);
    // Shift lasts one character, as on a phone; Caps holds.
    if (shift_ && !caps_) { shift_ = false; relabel(); }
}

// One ladder for both the Shift key and triangle: off -> one character -> Caps -> off.
void Keyboard::cycleShift()
{
    if (caps_) { caps_ = false; shift_ = false; }
    else if (shift_) { caps_ = true; shift_ = false; }
    else { shift_ = true; }
    relabel();
}

bool Keyboard::pressCurrent(Context& c)
{
    if (row_ == SPECIAL_ROW) {
        switch (col_ / 2) {
        case SP_SPACE: focusTarget(); c.ui.character(u' '); return true;
        case SP_BACK:  focusTarget(); c.ui.keyTap(K_BackSpace); return true;
        case SP_ENTER: sendEnter(c); return true;
        case SP_SHIFT: cycleShift(); return true;
        case SP_CLOSE: close(&c.ui); return true;
        default: return false;
        }
    }
    const char16_t ch = charAt(row_, col_, symbols_);
    if (!ch) return false;
    char16_t out = ch;
    if (!symbols_ && !shift() && ch >= u'A' && ch <= u'Z')
        out = static_cast<char16_t>(ch - u'A' + u'a');
    type(c, out);
    return true;
}

std::vector<Hint> Keyboard::hints(const Context&) const
{
    // Empty on purpose: the keyboard draws its own legend in two lines (see legend()),
    // and the stack bar overlapped it in game.
    return std::vector<Hint>();
}

bool Keyboard::onButton(Context& c, Button b, bool pressed)
{
    if (!open_) return false;
    if (!pressed) return b == B_CROSS || b == B_TRIANGLE;
    switch (b) {
    case B_DPAD_UP:    move(-1, 0); return true;
    case B_DPAD_DOWN:  move(1, 0);  return true;
    case B_DPAD_LEFT:  move(0, -1); return true;
    case B_DPAD_RIGHT: move(0, 1);  return true;
    case B_CROSS:
        // Cross also opens the keyboard, so the first press must not type.
        if (swallowCross_) { swallowCross_ = false; return true; }
        return pressCurrent(c);
    case B_SQUARE:   focusTarget(); c.ui.keyTap(K_BackSpace); return true;
    case B_TRIANGLE: cycleShift(); return true;
    case B_L1:       focusTarget(); c.ui.character(u' '); return true;
    case B_R1:       symbols_ = !symbols_; relabel(); return true;
    // Enter is duplicated on R2: under the finger, no trip to the service row.
    case B_R2:
    case B_OPTIONS:  sendEnter(c); return true;
    case B_CIRCLE:   close(&c.ui); return true;
    default: return false;
    }
}

void Keyboard::onTick(Context& c, float dt)
{
    if (!open_) return;
    // Stick walks the grid on the cursor's timings: 0.35 s to the first repeat,
    // 0.12 s after.
    const input::Stick s = input::anyStick(c.pad);
    int dir = -1;
    if (s.len() >= 0.5f) {
        if (std::fabs(s.x) > std::fabs(s.y)) dir = s.x > 0 ? 3 : 2;
        else dir = s.y > 0 ? 1 : 0;
    } else if (s.len() < 0.35f) {
        dir = -1;
    } else {
        dir = stickDir_;                   // hysteresis: between 0.35 and 0.5 we hold the previous one
    }
    if (dir < 0) { stickDir_ = -1; stickWait_ = 0.f; return; }
    if (dir != stickDir_) {
        stickDir_ = dir; stickWait_ = 0.35f;
        switch (dir) {
        case 0: move(-1, 0); break; case 1: move(1, 0); break;
        case 2: move(0, -1); break; default: move(0, 1); break;
        }
        return;
    }
    stickWait_ -= dt;
    if (stickWait_ <= 0.f) {
        stickWait_ = 0.12f;
        switch (dir) {
        case 0: move(-1, 0); break; case 1: move(1, 0); break;
        case 2: move(0, -1); break; default: move(0, 1); break;
        }
    }
}

}} // namespace cp::windows
