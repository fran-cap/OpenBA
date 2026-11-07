// src/ext/winter_ice/enhanced_entry.cpp -- ext.winter_ice's row of the Enhanced tab (WINTERICE)
//
// NOT RETAIL. The combo "Better ice": Off / On = the bool row `ext.winter_ice`. Live: the ice pass reads
// the row every frame (WinterIce::Enabled), so the next frame draws or drops the ice
// (docs/extensions/handins/WINTERICE.md).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterIce");
    const Entry* row = FindEntry("winter_ice");
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[WinterIce] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("winter_ice");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[WinterIce] Apply: item=%d requested=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& WinterIceEntry() {
    static const Contribution c = { "winter_ice", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
