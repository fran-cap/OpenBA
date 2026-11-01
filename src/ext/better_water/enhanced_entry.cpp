// src/ext/better_water/enhanced_entry.cpp -- ext.better_water's row of the Enhanced tab (BETTERWATER)
//
// NOT RETAIL. The combo "Better water": Off / Plain / Fancy. Off is the bool row `ext.better_water` off;
// Plain and Fancy turn it on and set the valued key `ext.better_water_mode` 1 / 2 (Fancy adds the shore
// strip). Live: the next frame draws the new level (docs/extensions/handins/BETTERWATER.md).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterWater");
    const Entry* row = FindEntry("better_water");
    const int item = row != nullptr && Requested(*row) ? (Val().better_water_mode >= 2 ? 2 : 1) : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[BetterWater] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("better_water");
    if (row == nullptr) return;
    const int item = g_combo->_currentItem;
    const bool on = item >= 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    // The level is kept while the row is off, so Off and back restores it.
    if (on) {
        const int mode = item >= 2 ? 2 : 1;
        const ValueKey* key = FindValueKey("better_water_mode");
        if (key != nullptr && Val().better_water_mode != mode) SetValueAndRecord(*key, mode);
    }
    std::fprintf(stderr, "[BetterWater] Apply: item=%d requested=%d mode=%d\n", item, Requested(*row) ? 1 : 0,
                 Val().better_water_mode);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& BetterWaterEntry() {
    static const Contribution c = { "better_water", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
