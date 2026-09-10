// Tests for reading the client's active theme and for the game menu radial captions.
#include "test.h"
#include "mock_node.h"
#include "../core/theme.h"
#include "../windows/gamebar.h"

using namespace cp; using namespace cp::mock; using namespace cp::windows;

TEST(theme_reads_colours_the_client_already_applied)
{
    // The client expands a palette entry name into an ordinary color property on
    // load and on a theme change. We read the result: name -> color.
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1024, 768);
    MockPtr a = root->add("a", "Page", 0, 0, 10, 10);
    a->setPropA(abi::PROP_PalColor, "line1");
    a->setPropA(abi::PROP_Color, "#123456");
    MockPtr b = a->add("b", "Text", 0, 0, 10, 10);
    b->setPropA(abi::PROP_PalText, "text1");
    b->setPropA(abi::PROP_TextColor, "#ABCDEF");
    MockPtr c = root->add("c", "Page", 0, 0, 10, 10);
    c->setPropA(abi::PROP_PalBgTint, "back3");
    c->setPropA(abi::PROP_BackgroundTint, "#0A0B0C");

    theme::scan(root);
    CHECK(std::string(theme::color("line1")) == "#123456");
    CHECK(std::string(theme::color("text1")) == "#ABCDEF");
    CHECK(std::string(theme::color("back3")) == "#0A0B0C");
    // The client does not care about the case of a name: UILowerString lowers it.
    CHECK(std::string(theme::color("Line1")) == "#123456");
    // What was not found in the tree comes from the default palette, not as white.
    CHECK(std::string(theme::color("header")) == "#94D6D6");
    CHECK(std::string(theme::color("no such entry")) == "#FFFFFF");
}

TEST(theme_paint_sets_both_the_name_and_the_colour)
{
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1024, 768);
    MockPtr src = root->add("src", "Page", 0, 0, 10, 10);
    src->setPropA(abi::PROP_PalColor, "line1");
    src->setPropA(abi::PROP_Color, "#112233");
    theme::scan(root);

    MockPtr w = root->add("w", "Page", 0, 0, 10, 10);
    theme::paint(w, theme::ROLE_COLOR, "line1");
    // The name is so the widget follows the theme; the color is so it is right now,
    // rather than from the next theme change onwards.
    CHECK(w->prop(abi::PROP_PalColor) == u"line1");
    CHECK(w->prop(abi::PROP_Color) == u"#112233");

    // An explicit color: the name is CLEARED, or the palette would repaint the widget back.
    theme::paint(w, theme::ROLE_COLOR, "#FF0000");
    CHECK(w->prop(abi::PROP_Color) == u"#FF0000");
    CHECK(w->prop(abi::PROP_PalColor).empty());
}

TEST(gamebar_labels_name_the_menu_items)
{
    // A button tooltip is an already localized client string, and that is what we show.
    MockPtr root = MockNode::make("root", "Page", 0, 0, 1024, 768);
    MockPtr withTip = root->add("inventory", "Button", 0, 0, 22, 22);
    withTip->setProp(abi::PROP_LocalTooltip, u"Inventory");
    CHECK(GameBar::labelOf(withTip) == u"Inventory");

    // With no tooltip, the widget name: the panel's are meaningful.
    MockPtr plain = root->add("datapad", "Button", 0, 0, 22, 22);
    CHECK(GameBar::labelOf(plain) == u"Datapad");
    MockPtr prefixed = root->add("buttonJournal", "Button", 0, 0, 22, 22);
    CHECK(GameBar::labelOf(prefixed) == u"Journal");

    // A long tooltip cannot be squeezed under an icon - we fall back to the name.
    MockPtr longTip = root->add("service", "Button", 0, 0, 22, 22);
    longTip->setProp(abi::PROP_LocalTooltip, u"Customer service and help request");
    CHECK(GameBar::labelOf(longTip) == u"Service");

    CHECK(GameBar::labelOf(NodePtr()).empty());
}
