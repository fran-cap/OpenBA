// src/ext/max_shadow_res/enhanced_entry.cpp -- ext.max_shadow_res's rows of the Enhanced tab (FAA-MENU, FAA-SHADOW2, SHADOWZOOM)
//
// NOT RETAIL. The combo "Max shadow resolution" Off / 4096 / 8192: Off = the bool row
// `ext.max_shadow_res` off (the stored `ext.max_shadow_size` is kept), 4096 / 8192 = the row on and
// the valued key `ext.max_shadow_size` (the sun map's size while the retail Shadow Resolution combo
// is at High; Medium / Low stay 1024 / 512). This directory holds only the tab row: the seam is
// Graphics::SunLight::_extMaxShadowRes / _extMaxShadowSize (docs/extensions/handins/FAA-SHADOW.md,
// FAA-SHADOW2.md). SetAndRecord / SetValueAndRecord record the keys and request the map's rebuild on
// this (the main) thread, so the next frame follows Apply / OK.
// The combo "Adaptive shadow map" Off / On = the valued key `ext.max_shadow_zoom` 0 / 1 (SHADOWZOOM.md
// section 6): the casters drawn into a share of the map sized to the view. It takes effect while the row
// is on; with the row off its key is stored as chosen and waits. Live: the sun light reads it every frame.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;
UI::ComboElement* g_zoom = nullptr;

// Combo item <-> the choice (registry.h SetMaxShadowResAndRecord): 0 Off, 1 = 4096, 2 = 8192.
constexpr int32_t kItemSize[] = { 0, 4096, 8192 };

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboMaxShadowRes");
    g_zoom = FindElement<UI::ComboElement>(page, "comboShadowZoom");
    const int32_t choice = MaxShadowResChoice();
    const int item = choice == 8192 ? 2 : (choice == 4096 ? 1 : 0);
    const int zoom = Val().max_shadow_zoom != 0 ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    if (g_zoom != nullptr) g_zoom->SetCurrentItem(zoom);
    std::fprintf(stderr, "[MaxShadowRes] combo=%p item=%d (choice %d) zoom combo=%p zoom=%d\n",
                 static_cast<void*>(g_combo), item, choice, static_cast<void*>(g_zoom), zoom);
}

void Apply() {
    if (g_combo == nullptr) return;
    const int item = g_combo->_currentItem;
    const int32_t choice = SetMaxShadowResAndRecord(item >= 0 && item <= 2 ? kItemSize[item] : 0);
    std::fprintf(stderr, "[MaxShadowRes] Apply: item=%d choice=%d\n", item, choice);
    const ValueKey* zoomKey = FindValueKey("max_shadow_zoom");
    if (g_zoom == nullptr || zoomKey == nullptr) return;
    const int32_t zoom = g_zoom->_currentItem != 0 ? 1 : 0;
    if (Value(*zoomKey) == zoom) return;
    const int32_t stored = SetValueAndRecord(*zoomKey, zoom);
    std::fprintf(stderr, "[MaxShadowRes] Apply: ext.max_shadow_zoom=%d\n", stored);
}

void Close() {
    g_combo = nullptr;
    g_zoom = nullptr;
}

}  // namespace

const Contribution& MaxShadowResEntry() {
    static const Contribution c = { "max_shadow_res", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
