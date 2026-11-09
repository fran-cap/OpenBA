// src/ext/depth_of_field/enhanced_entry.cpp -- ext.depth_of_field's row of the Enhanced tab (DOF.md)
//
// NOT RETAIL. The combo "Depth blur": Off / On = the valued key `ext.depth_blur` (0 / 1, default 0) of the row
// `ext.depth_of_field` (ON by default). On also turns the row on when it was turned off in the Extensions
// dialog; Off leaves the row alone. Live: the scene draw asks for the pass every frame
// (DepthOfField::SceneDraw), so the next frame draws or drops it.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboDepthBlur");
    const Entry* row = FindEntry("depth_of_field");
    const int item = row != nullptr && Requested(*row) && Val().depth_blur != 0 ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[DepthOfField] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("depth_of_field");
    const ValueKey* key = FindValueKey("depth_blur");
    if (row == nullptr || key == nullptr) return;
    const int value = g_combo->_currentItem == 1 ? 1 : 0;
    if (value == 1 && !Requested(*row)) SetAndRecord(*row, true);
    if (Val().depth_blur != value) SetValueAndRecord(*key, value);
    std::fprintf(stderr, "[DepthOfField] Apply: item=%d requested=%d depth_blur=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0, Val().depth_blur);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& DepthOfFieldEntry() {
    static const Contribution c = { "depth_of_field", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
