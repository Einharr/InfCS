#include "theme.h"
#include "runtime.h"
#include "../abi/props.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace cp { namespace theme {

namespace {

// The 'Protean' palette, first in stock/ui/ui_styles.inc, kept as a fallback: the mock
// build has no client tree at all, and in game a widget with the wanted name may not be
// among the loaded pages.
struct Entry { const char* name; const char* hex; };
const Entry DEFAULTS[] = {
    {"back1", "#417A7E"}, {"back2", "#214041"}, {"back3", "#224142"}, {"back4", "#356262"},
    {"line1", "#9AD6CE"}, {"line2", "#9AD6CE"}, {"text1", "#D5E6E3"}, {"text2", "#EEEEEE"},
    {"header", "#94D6D6"}, {"highlight", "#7BC4C4"}, {"contrast1", "#FFE21A"},
    {"icondefault", "#55D7D7"}, {"iconpositive", "#5CFF2D"}, {"iconnegative", "#FF3333"},
    {"titletext", "#172324"}, {"textdefault", "#BEC8CB"}, {"selected", "#447B78"},
    {"slot", "#73ACB0"}, {"backdrop", "#38696D"},
};

std::string lower(const char* s)
{
    std::string t(s ? s : "");
    std::transform(t.begin(), t.end(), t.begin(), [](char c) { return static_cast<char>(::tolower(c)); });
    return t;
}

std::map<std::string, std::string>& cache()
{
    static std::map<std::string, std::string> m;
    return m;
}

std::string narrow(const std::u16string& s)
{
    std::string out; out.reserve(s.size());
    for (char16_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}

// The entry name and the resolved colour live in different properties; the client
// registers the pairs in UIPaletteRegistrySetup. Only three of them are ours to set.
struct Pair { uint32_t nameProp; uint32_t colorProp; };
const Pair PAIRS[] = {
    {abi::PROP_PalColor,   abi::PROP_Color},
    {abi::PROP_PalBgTint,  abi::PROP_BackgroundTint},
    {abi::PROP_PalText,    abi::PROP_TextColor},
};

void harvest(const NodePtr& n)
{
    for (const Pair& p : PAIRS) {
        std::u16string name;
        if (!n->getProp(p.nameProp, name) || name.empty()) continue;
        const std::string key = lower(narrow(name).c_str());
        if (cache().count(key)) continue;
        std::u16string col;
        if (!n->getProp(p.colorProp, col) || col.empty()) continue;
        const std::string hex = narrow(col);
        // The client answers '#RRGGBB'. Anything else is not a colour and we do not
        // guess - the fallback table takes over.
        if (hex.size() < 7 || hex[0] != '#') continue;
        cache()[key] = hex.substr(0, 7);
    }
}

} // namespace

void scan(const NodePtr& root)
{
    cache().clear();
    if (!root) return;
    // The tree runs to thousands of nodes, but the pass is one-off and draws nothing.
    // The ceiling is insurance against a cycle.
    std::vector<NodePtr> stack; stack.push_back(root);
    int seen = 0;
    while (!stack.empty() && seen < 40000) {
        NodePtr n = stack.back(); stack.pop_back(); ++seen;
        harvest(n);
        for (const NodePtr& c : n->children()) stack.push_back(c);
    }
    tracef("client palette: walked %d nodes, learned %d colors (line1 %s, back1 %s)",
           seen, static_cast<int>(cache().size()), color("line1"), color("back1"));
}

const char* color(const char* palName)
{
    const std::string key = lower(palName);
    std::map<std::string, std::string>::const_iterator it = cache().find(key);
    if (it != cache().end()) return it->second.c_str();
    for (const Entry& e : DEFAULTS) if (key == e.name) return e.hex;
    return "#FFFFFF";
}

int known() { return static_cast<int>(cache().size()); }

void paint(const NodePtr& n, Role role, const char* palNameOrHex)
{
    if (!n || !palNameOrHex || !*palNameOrHex) return;
    const Pair& p = PAIRS[role];
    if (palNameOrHex[0] == '#') {
        n->setPropA(p.colorProp, palNameOrHex);
        n->setPropA(p.nameProp, "");        // the palette skips an empty name
        return;
    }
    n->setPropA(p.nameProp, palNameOrHex);
    n->setPropA(p.colorProp, color(palNameOrHex));
}

}} // namespace cp::theme
