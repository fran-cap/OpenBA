// src/ext/map_edge_fade/enhanced_entry.cpp -- ext.map_edge_fade's row of the Enhanced tab (MAPEDGE)
//
// NOT RETAIL. The combo "Map edge fade": Off / On = the bool row `ext.map_edge_fade` (class A, ON by
// default). Live: the scene draw asks for the pass every frame (MapEdgeFade::SceneDraw), so the next frame
// draws or drops it (docs/extensions/handins/MAPEDGE.md).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboMapEdgeFade");
    const Entry* row = FindEntry("map_edge_fade");
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[MapEdgeFade] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("map_edge_fade");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[MapEdgeFade] Apply: item=%d requested=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& MapEdgeFadeEntry() {
    static const Contribution c = { "map_edge_fade", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
