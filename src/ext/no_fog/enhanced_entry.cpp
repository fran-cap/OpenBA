// src/ext/no_fog/enhanced_entry.cpp -- ext.no_fog's row of the Enhanced tab (NOFOG)
//
// NOT RETAIL. The slider "Fog distance", 0..100: it sets the valued key `ext.fog_distance` (0 = retail's fog, 100 =
// no fog; no_fog.h has the curve). The row `ext.no_fog` is ON by default on a desktop (ruled 2026-11-08); the slider
// never turns it off (0 with the row on is retail's fog), and turns it on for any position but 0 when the player
// switched it off in the Extensions dialog. While the row is off the slider shows 0. Live: the next frame draws
// the new fog (docs/extensions/handins/NOFOG.md).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/no_fog/no_fog.h"
#include "ext/registry.h"

#include "ui/SliderElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::SliderElement* g_slider = nullptr;

int SliderValue() {
    const Entry* row = FindEntry("no_fog");
    return row != nullptr && Requested(*row) ? Val().fog_distance : NoFog::kDistanceRetail;
}

void Open(UI::Element* page) {
    g_slider = FindElement<UI::SliderElement>(page, "sliderFogDistance");
    const int value = SliderValue();
    if (g_slider != nullptr) {
        g_slider->_minimum = static_cast<float>(NoFog::kDistanceRetail);     // retail's fog
        g_slider->_maximum = static_cast<float>(NoFog::kDistanceOff);        // no fog
        g_slider->_value = static_cast<float>(value);
    }
    std::fprintf(stderr, "[NoFog] slider=%p value=%d\n", static_cast<void*>(g_slider), value);
}

void Apply() {
    if (g_slider == nullptr) return;
    const Entry* row = FindEntry("no_fog");
    if (row == nullptr) return;
    const int value = static_cast<int>(g_slider->_value + 0.5f);
    // Row off and the slider left at retail: nothing to write (the row stays as the player set it).
    if (!Requested(*row) && value == NoFog::kDistanceRetail) return;
    if (!Requested(*row)) SetAndRecord(*row, true);
    const ValueKey* key = FindValueKey("fog_distance");
    if (key != nullptr && Val().fog_distance != value) SetValueAndRecord(*key, value);
    std::fprintf(stderr, "[NoFog] Apply: value=%d requested=%d distance=%d\n", value, Requested(*row) ? 1 : 0,
                 Val().fog_distance);
}

void Close() { g_slider = nullptr; }

}  // namespace

const Contribution& FogDistanceEntry() {
    static const Contribution c = { "no_fog", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
