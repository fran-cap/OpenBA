// src/ext/better_trees/enhanced_entry.cpp -- Better trees' rows of the Enhanced tab
//
// NOT RETAIL. The combo "Better trees": Off / On / On + distance fill =
// BetterTrees::Current().mode 0 / 1 / 2. It is the master of the bool row `ext.better_trees`: Off turns
// the row off (the stored `ext.foliage_aa_mode` is kept), the others set it on and the mode key.
// The combo "Natural tree growth" (NATURAL_GROWTH.md): Off / On = `ext.natural_tree_growth` 0 / 1, stored
// with an on mode (it takes effect while the row is on; with the row off its key is kept as it was).
// BetterTrees::Set records the keys and Sync()s the render side, so the next frame follows Apply / OK.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/better_trees/better_trees.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;
UI::ComboElement* g_growth = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterTrees");
    g_growth = FindElement<UI::ComboElement>(page, "comboNaturalGrowth");
    const int mode = BetterTrees::Current().mode;
    const int growth = Val().natural_tree_growth != 0 ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(mode);
    if (g_growth != nullptr) g_growth->SetCurrentItem(growth);
    std::fprintf(stderr, "[BetterTrees] combo=%p mode=%d growth combo=%p growth=%d\n", static_cast<void*>(g_combo),
                 mode, static_cast<void*>(g_growth), growth);
}

void Apply() {
    if (g_combo == nullptr) return;
    BetterTrees::Params p;
    p.mode = g_combo->_currentItem;
    p.naturalGrowth = g_growth != nullptr ? g_growth->_currentItem != 0 : Val().natural_tree_growth != 0;
    BetterTrees::Set(p);
    std::fprintf(stderr, "[BetterTrees] Apply: mode=%d natural growth=%d\n", BetterTrees::Current().mode,
                 BetterTrees::Current().naturalGrowth ? 1 : 0);
}

void Close() {
    g_combo = nullptr;
    g_growth = nullptr;
}

}  // namespace

const Contribution& BetterTreesEntry() {
    static const Contribution c = { "better_trees", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
