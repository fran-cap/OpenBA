// src/ext/better_water/enhanced_entry.cpp -- ext.better_water's row of the Enhanced tab (BETTERWATER)
//
// NOT RETAIL. The combo "Better water": Off / Plain = the bool row `ext.better_water`, live (the next
// frame's water draws the shore pass). Fancy joins as a third item with its own valued key when it is
// built (docs/extensions/handins/BETTERWATER.md).

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
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[BetterWater] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("better_water");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[BetterWater] Apply: item=%d requested=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& BetterWaterEntry() {
    static const Contribution c = { "better_water", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
