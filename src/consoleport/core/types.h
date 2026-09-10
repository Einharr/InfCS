// Types whose layout matches the swgemu.exe binary, taken with a disassembler (see
// docs/03-abi.md). Nothing comes from the client headers: the binary is older.
#pragma once
#include <cstdint>
#include <cstring>

namespace cp {

struct UIPoint { int32_t x, y; };
struct UISize  { int32_t x, y; };
struct UIRect  { int32_t left, top, right, bottom; };

// UIMessage: 0x28 bytes, the constructor at 0x1124BF0 zeroes all of it.
struct UIMessage {
    int32_t  type;          // +0x00  abi::MsgType
    uint8_t  modifiers[9];  // +0x04  LShift RShift LCtrl RCtrl LAlt RAlt LMouse MMouse RMouse
    uint8_t  pad_[1];       // +0x0D
    uint16_t keystroke;     // +0x0E  UIMessage::* (see abi::UIMessage_key_*_VALUE) or a character
    uint16_t data;          // +0x10  wheel delta / double-click flag
    int32_t  mouseX;        // +0x14
    int32_t  mouseY;        // +0x18
    void*    dragSource;    // +0x1C
    void*    dragObject;    // +0x20
    void*    dragTarget;    // +0x24
    UIMessage() { std::memset(this, 0, sizeof(*this)); }
};
static_assert(sizeof(UIMessage) == 0x28, "UIMessage layout");

// STLport basic_string<char>: {begin, end, end_of_storage}, no SSO.
// Enough to pass a string into functions that only read it
// (CuiActionManager::performAction looks it up in a map and compares).
struct StlpString {
    const char* begin; const char* end; const char* cap;
    explicit StlpString(const char* s) { begin = s; end = s + std::strlen(s); cap = end + 1; }
};
struct StlpUString {
    const uint16_t* begin; const uint16_t* end; const uint16_t* cap;
    StlpUString() { static const uint16_t z = 0; begin = end = &z; cap = &z + 1; }
    explicit StlpUString(const uint16_t* s) { size_t n = 0; while (s[n]) ++n; begin = s; end = s + n; cap = end + 1; }
};

// UIBaseObject::IsA type tags, off vtable slot 0. The order is the sources' UITypeID,
// and the match was checked through IsA for Widget, Button, Image, Page, Text and
// VolumePage.
enum TypeTag {
    T_BaseObject = 0x00, T_Widget = 0x0B, T_3DViewer = 0x0C, T_Button = 0x0E, T_Checkbox = 0x0F,
    T_ComboBox = 0x10, T_Composite = 0x11, T_Dropdownbox = 0x12, T_Image = 0x16, T_List = 0x17,
    T_Listbox = 0x18, T_Page = 0x19, T_PopupMenu = 0x1A, T_RadialMenu = 0x1C, T_Scrollbar = 0x1D,
    T_Sliderbar = 0x1E, T_TabSet = 0x21, T_TabbedPane = 0x22, T_Text = 0x23, T_Textbox = 0x24,
    T_TreeView = 0x25, T_VolumePage = 0x27,
};

// UIWidget flags (+0x7C), from CanSelect/WantsMessage. BF_UnderMouse is the client's
// own - UIWidget::SetUnderMouse (slot 46) sets mask 0x10 - and cell hover is read from
// it.
enum WidgetFlag { BF_Visible = 0x1, BF_ForceVisible = 0x2, BF_Enabled = 0x4,
                  BF_UnderMouse = 0x10, BF_GetsInput = 0x100 };

} // namespace cp
