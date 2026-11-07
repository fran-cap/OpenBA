// src/ext/better_clouds/enhanced_entry.cpp -- ext.better_clouds' row of the Enhanced tab (BETTERCLOUDS)
//
// NOT RETAIL. The combo "Better clouds": Off / On / Fancy. Off is the bool row `ext.better_clouds` off;
// On and Fancy turn it on and set the valued key `ext.better_clouds_mode` 1 / 2 (Fancy adds the clouds'
// shadows on the ground). Live: the next frame draws the new level (docs/extensions/handins/BETTERCLOUDS.md).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterClouds");
    const Entry* row = FindEntry("better_clouds");
    const int item = row != nullptr && Requested(*row) ? (Val().better_clouds_mode >= 2 ? 2 : 1) : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[BetterClouds] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("better_clouds");
    if (row == nullptr) return;
    const int item = g_combo->_currentItem;
    const bool on = item >= 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    // The level is kept while the row is off, so Off and back restores it.
    if (on) {
        const int mode = item >= 2 ? 2 : 1;
        const ValueKey* key = FindValueKey("better_clouds_mode");
        if (key != nullptr && Val().better_clouds_mode != mode) SetValueAndRecord(*key, mode);
    }
    std::fprintf(stderr, "[BetterClouds] Apply: item=%d requested=%d mode=%d\n", item, Requested(*row) ? 1 : 0,
                 Val().better_clouds_mode);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& BetterCloudsEntry() {
    static const Contribution c = { "better_clouds", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
