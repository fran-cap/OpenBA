// src/ext/fancy_terrain/enhanced_entry.cpp -- ext.fancy_terrain's row of the Enhanced tab (FANCYTERRAIN)
//
// NOT RETAIL. The combo "Fancy terrain": Off / On = the bool row `ext.fancy_terrain`. Live: the ground draw
// asks for the pass every frame (FancyTerrain::TerrainDraw), so the next frame draws or drops it.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboFancyTerrain");
    const Entry* row = FindEntry("fancy_terrain");
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[FancyTerrain] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("fancy_terrain");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[FancyTerrain] Apply: item=%d requested=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& FancyTerrainEntry() {
    static const Contribution c = { "fancy_terrain", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
