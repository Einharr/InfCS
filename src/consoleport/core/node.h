// A UI tree node - the abstraction over a client widget. Everything else (flyouts,
// cursor, hints, window profiles) goes through this interface only, which is what makes
// the module testable end to end on a mock.
#pragma once
#include "types.h"
#include <memory>
#include <string>
#include <vector>

namespace cp {

class Node;
typedef std::shared_ptr<Node> NodePtr;

class Node {
public:
    virtual ~Node() {}
    virtual void*        handle() const = 0;                 // pointer to the client widget (0 on the mock)
    virtual std::string  name() const = 0;
    virtual std::string  typeName() const = 0;               // "Page", "Button", "VolumePage", "Image", "Text" ...
    virtual bool         isA(int typeTag) const = 0;
    virtual NodePtr      parent() const = 0;
    virtual std::vector<NodePtr> children() const = 0;
    virtual NodePtr      child(const char* name) const = 0;  // direct child by name (dots make a path)
    virtual NodePtr      byPath(const char* path) const = 0; // GetObjectFromPath relative to the node

    virtual UIPoint      location() const = 0;               // relative to the parent
    virtual UISize       size() const = 0;
    virtual UIPoint      scrollLocation() const = 0;
    virtual uint32_t     flags() const = 0;                  // BF_*
    virtual bool         canSelect() const = 0;              // WillDraw && Enabled && GetsInput

    virtual void         setLocation(int x, int y) = 0;
    virtual void         setSize(int w, int h) = 0;
    virtual void         setVisible(bool v) = 0;
    virtual void         setLocalText(const char16_t* utf16) = 0;   // UIText: PreLocalized + SetLocalText
    virtual bool         setProp(uint32_t propGlobal, const char16_t* value) = 0;   // any property by its name global
    virtual bool         getProp(uint32_t propGlobal, std::u16string& out) const = 0;
    virtual bool         setPropA(uint32_t propGlobal, const char* ascii);          // convenience: ASCII -> UTF-16
    // Numbers through properties: the client keeps angles and fractions as strings and
    // the widget's SetProperty parses them (CameraYaw on the character viewer).
    bool                 getPropF(uint32_t propGlobal, float& out) const;
    bool                 setPropF(uint32_t propGlobal, float v);
    // Press without a mouse: SetProperty("Press") calls UIButton::Press(), which sends
    // OnButtonPressed and NotifyActionListener. In game mode the client takes no mouse
    // messages at all - see Cursor::activate.
    bool                 press();
    virtual NodePtr      clone() = 0;                        // DuplicateObject (without Link/AddChild)
    virtual bool         addChild(const NodePtr& c) = 0;
    virtual bool         removeChild(const NodePtr& c) = 0;
    virtual bool         moveChild(const NodePtr& c, int dir) = 0;   // 0 Up, 1 Down, 2 Top, 3 Bottom
    virtual void         link() = 0;
    virtual void         pack() = 0;

    // screen coordinates: the parents' locations summed, minus their scroll
    UIPoint worldLocation() const;
    UIRect  worldRect() const;
    UIPoint center() const;
    bool    willDraw() const { return (flags() & (BF_Visible | BF_ForceVisible)) != 0; }
    bool    enabled() const  { return (flags() & BF_Enabled) != 0; }
    bool    getsInput() const{ return (flags() & BF_GetsInput) != 0; }
    bool    underMouse() const { return (flags() & BF_UnderMouse) != 0; }
    bool    sameAs(const Node& o) const { return handle() ? handle() == o.handle() : this == &o; }
};

// A node over a real client widget (implemented in node.cpp through bin::*).
class BinaryNode : public Node {
public:
    explicit BinaryNode(void* h) : h_(h) {}
    void* handle() const override { return h_; }
    std::string name() const override;
    std::string typeName() const override;
    bool isA(int typeTag) const override;
    NodePtr parent() const override;
    std::vector<NodePtr> children() const override;
    NodePtr child(const char* name) const override;
    NodePtr byPath(const char* path) const override;
    UIPoint location() const override;
    UISize size() const override;
    UIPoint scrollLocation() const override;
    uint32_t flags() const override;
    bool canSelect() const override;
    void setLocation(int x, int y) override;
    void setSize(int w, int h) override;
    void setVisible(bool v) override;
    void setLocalText(const char16_t* utf16) override;
    bool setProp(uint32_t propGlobal, const char16_t* value) override;
    bool getProp(uint32_t propGlobal, std::u16string& out) const override;
    NodePtr clone() override;
    bool addChild(const NodePtr& c) override;
    bool removeChild(const NodePtr& c) override;
    bool moveChild(const NodePtr& c, int dir) override;
    void link() override;
    void pack() override;
    static NodePtr wrap(void* h) { return h ? std::make_shared<BinaryNode>(h) : NodePtr(); }
private:
    void* h_;
};

// First node of a type in the subtree (BFS, bounded depth) - for cloning templates.
NodePtr findFirst(const NodePtr& root, int typeTag, int maxDepth = 6);

// Rig diagnostics: walks the object's offsets and reports the ones that look like the
// head of the children list (it closes on its sentinel). Used to confirm
// OFF_UIPage_children against the live binary.
void probeChildList(void* handle);

} // namespace cp
