// A mock widget tree and input sink for tests with no client. It mirrors the client
// wherever the module depends on it: children in draw order (first on top), relative
// geometry, CanSelect = WillDraw && Enabled && GetsInput, and a Pack that leaves the
// children alone under DoNotPackChildren.
#pragma once
#include "../core/node.h"
#include "../core/ui.h"
#include "../abi/props.h"
#include <algorithm>
#include <map>
#include <memory>

namespace cp { namespace mock {

class MockNode;
typedef std::shared_ptr<MockNode> MockPtr;

class MockNode : public Node, public std::enable_shared_from_this<MockNode> {
public:
    MockNode(const std::string& name, const std::string& type) : name_(name), type_(type) {
        flags_ = BF_Visible | BF_Enabled | (type == "Page" || type == "VolumePage" ? 0 : BF_GetsInput);
    }
    static MockPtr make(const std::string& name, const std::string& type, int x = 0, int y = 0, int w = 0, int h = 0) {
        MockPtr n = std::make_shared<MockNode>(name, type); n->loc_ = UIPoint{x, y}; n->size_ = UISize{w, h}; return n;
    }
    MockPtr add(const MockPtr& c) { c->parent_ = shared_from_this(); kids_.push_back(c); return c; }
    MockPtr add(const std::string& name, const std::string& type, int x = 0, int y = 0, int w = 0, int h = 0) { return add(make(name, type, x, y, w, h)); }

    // Node identity: the module spots recreated cells by it, so a mock handle has to
    // be unique.
    void* handle() const override { return const_cast<MockNode*>(this); }
    std::string name() const override { return name_; }
    std::string typeName() const override { return type_; }
    bool isA(int tag) const override {
        switch (tag) {
        case T_BaseObject: return true;
        case T_Widget: return true;
        case T_Page: return type_ == "Page" || type_ == "VolumePage" || type_ == "PopupMenu" || type_ == "RadialMenu" || type_ == "TabbedPane" || type_ == "Composite";
        case T_VolumePage: return type_ == "VolumePage";
        case T_Button: return type_ == "Button" || type_ == "Checkbox";
        case T_Checkbox: return type_ == "Checkbox";
        case T_Image: return type_ == "Image";
        case T_Text: return type_ == "Text";
        case T_Textbox: return type_ == "Textbox";
        case T_List: return type_ == "List";
        case T_Listbox: return type_ == "Listbox";
        case T_Dropdownbox: return type_ == "Dropdownbox";
        case T_Sliderbar: return type_ == "Sliderbar";
        case T_Scrollbar: return type_ == "Scrollbar";
        case T_PopupMenu: return type_ == "PopupMenu";
        case T_RadialMenu: return type_ == "RadialMenu";
        case T_TabbedPane: return type_ == "TabbedPane";
        case T_3DViewer: return type_ == "Viewer";
        case T_Composite: return type_ == "Composite";
        }
        return false;
    }
    NodePtr parent() const override { return parent_.lock(); }
    std::vector<NodePtr> children() const override { return std::vector<NodePtr>(kids_.begin(), kids_.end()); }
    NodePtr child(const char* n) const override { for (const MockPtr& k : kids_) if (k->name_ == n) return k; return NodePtr(); }
    NodePtr byPath(const char* path) const override {
        std::string p(path); size_t dot = p.find('.');
        std::string head = p.substr(0, dot);
        for (const MockPtr& k : kids_) if (k->name_ == head) return dot == std::string::npos ? NodePtr(k) : k->byPath(p.c_str() + dot + 1);
        return NodePtr();
    }
    UIPoint location() const override { return loc_; }
    UISize size() const override { return size_; }
    UIPoint scrollLocation() const override { return scroll_; }
    uint32_t flags() const override { return flags_; }
    // The client's CanSelect is stricter than ours - it refuses inventory buttons - so
    // the mock can be switched into that behaviour.
    bool canSelect() const override {
        if (canSelect_ >= 0) return canSelect_ != 0;
        return willDraw() && enabled() && getsInput() && parentsDraw();
    }
    void setCanSelect(int v) { canSelect_ = v; }   // -1 as usual, 0 no, 1 yes
    void setLocation(int x, int y) override { loc_ = UIPoint{x, y}; }
    void setSize(int w, int h) override { size_ = UISize{w, h}; }
    void setVisible(bool v) override { if (v) flags_ |= BF_Visible; else flags_ &= ~BF_Visible; }
    void setLocalText(const char16_t* t) override { text_ = t; }
    std::u16string text() const { return text_; }
    bool setProp(uint32_t prop, const char16_t* v) override {
        std::u16string s(v); props_[prop] = s;
        if (prop == abi::PROP_Visible) setVisible(s == u"true");
        if (prop == abi::PROP_Name) { name_.clear(); for (char16_t ch : s) name_.push_back(static_cast<char>(ch)); }
        if (prop == abi::PROP_Enabled) { if (s == u"true") flags_ |= BF_Enabled; else flags_ &= ~BF_Enabled; }
        if (prop == abi::PROP_GetsInput) { if (s == u"true") flags_ |= BF_GetsInput; else flags_ &= ~BF_GetsInput; }
        return true;
    }
    bool getProp(uint32_t prop, std::u16string& out) const override {
        auto it = props_.find(prop);
        if (it == props_.end()) return false;
        out = it->second; return true;
    }
    NodePtr clone() override {
        MockPtr c = std::make_shared<MockNode>(name_, type_); c->loc_ = loc_; c->size_ = size_; c->flags_ = flags_; c->props_ = props_;
        for (const MockPtr& k : kids_) c->add(std::static_pointer_cast<MockNode>(k->clone()));
        return c;
    }
    bool addChild(const NodePtr& c) override { MockPtr m = std::dynamic_pointer_cast<MockNode>(c); if (!m) return false; add(m); return true; }
    bool removeChild(const NodePtr& c) override {
        for (size_t i = 0; i < kids_.size(); ++i) if (kids_[i].get() == c.get()) { kids_[i]->parent_.reset(); kids_.erase(kids_.begin() + i); return true; }
        return false;
    }
    bool moveChild(const NodePtr& c, int dir) override {
        size_t i = 0; for (; i < kids_.size(); ++i) if (kids_[i].get() == c.get()) break;
        if (i == kids_.size()) return false;
        MockPtr k = kids_[i]; kids_.erase(kids_.begin() + i);
        size_t j = dir == 0 ? (i ? i - 1 : 0) : dir == 1 ? std::min(i + 1, kids_.size()) : dir == 2 ? 0 : kids_.size();
        kids_.insert(kids_.begin() + j, k); return true;
    }
    void link() override {}
    void pack() override { packCount++; }

    // test conveniences
    std::u16string prop(uint32_t p) const { auto it = props_.find(p); return it == props_.end() ? std::u16string() : it->second; }
    // Whether the button was pressed without a mouse: Node::press() sets the Press method-property.
    bool pressed() const { return props_.find(abi::PROP_Press) != props_.end(); }
    void setScroll(int x, int y) { scroll_ = UIPoint{x, y}; }
    void setFlags(uint32_t f) { flags_ = f; }
    int packCount = 0;
    int canSelect_ = -1;
private:
    bool parentsDraw() const { for (NodePtr p = parent(); p; p = p->parent()) if (!p->willDraw()) return false; return true; }
    std::string name_, type_;
    std::weak_ptr<MockNode> parent_;
    std::vector<MockPtr> kids_;
    UIPoint loc_ = {0, 0}, scroll_ = {0, 0}; UISize size_ = {0, 0};
    uint32_t flags_;
    std::map<uint32_t, std::u16string> props_;
    std::u16string text_;
};

// An input sink that records everything the module would have sent the client.
struct Event { enum Kind { Msg, Warp, Action } kind; UIMessage m; int x, y; std::string id; };
class MockInput : public InputSink {
public:
    bool send(UIMessage& m) override { Event e; e.kind = Event::Msg; e.m = m; e.x = m.mouseX; e.y = m.mouseY; log.push_back(e); return true; }
    void warp(int x, int y) override { Event e; e.kind = Event::Warp; e.x = x; e.y = y; log.push_back(e); UIMessage m; m.type = abi::MSG_MouseMove; m.mouseX = x; m.mouseY = y; e.kind = Event::Msg; e.m = m; log.push_back(e); }
    bool action(const char* id, const char16_t*) override { Event e; e.kind = Event::Action; e.id = id; e.x = e.y = 0; log.push_back(e); return true; }
    uint16_t key(int which) override {
        static const uint16_t v[] = { abi::UIMessage_key_Escape_VALUE, abi::UIMessage_key_Enter_VALUE, abi::UIMessage_key_Tab_VALUE, abi::UIMessage_key_Space_VALUE,
            abi::UIMessage_key_UpArrow_VALUE, abi::UIMessage_key_DownArrow_VALUE, abi::UIMessage_key_LeftArrow_VALUE, abi::UIMessage_key_RightArrow_VALUE,
            abi::UIMessage_key_PageUp_VALUE, abi::UIMessage_key_PageDown_VALUE, abi::UIMessage_key_Home_VALUE, abi::UIMessage_key_End_VALUE,
            abi::UIMessage_key_BackSpace_VALUE };
        return which >= 0 && which < static_cast<int>(sizeof v / sizeof v[0]) ? v[which] : 0;
    }
    std::vector<Event> log;
    // the sequence of message types (without the MouseMove from a warp) - handy to compare
    std::vector<int> types(bool withMoves = false) const {
        std::vector<int> t; for (const Event& e : log) if (e.kind == Event::Msg && (withMoves || e.m.type != abi::MSG_MouseMove)) t.push_back(e.m.type); return t;
    }
    void clear() { log.clear(); }
};

}} // namespace cp::mock
