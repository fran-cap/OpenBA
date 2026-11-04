// src/ext/sharp_ui/enhanced_entry.cpp -- ext.sharp_ui's row of the Enhanced tab (SHARP_SPRITES)
//
// NOT RETAIL (docs/extensions/handins/SHARP_SPRITES.md). The combo "Sharp UI" Off / Text / Text + icons:
// Off = the bool row ext.sharp_ui off (ext.sharp_ui_icons kept), Text = the row on with ext.sharp_ui_icons 0
// (scale-matched fonts, whole-pixel quads), Text + icons = the row on with ext.sharp_ui_icons 1 (also the
// magnified sprite sheets with our art). Live: sharp_ui.cpp selects again when either changes.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboSharpUi");
    const Entry* row = FindEntry("sharp_ui");
    const int item = row != nullptr && Requested(*row) ? (Val().sharp_ui_icons != 0 ? 2 : 1) : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[SharpUI] Enhanced combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("sharp_ui");
    if (row == nullptr) return;
    const int item = g_combo->_currentItem;
    const bool on = item >= 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    // The level is kept while the row is off, so Off and back restores it.
    if (on) {
        const int icons = item >= 2 ? 1 : 0;
        const ValueKey* key = FindValueKey("sharp_ui_icons");
        if (key != nullptr && Val().sharp_ui_icons != icons) SetValueAndRecord(*key, icons);
    }
    std::fprintf(stderr, "[SharpUI] Enhanced Apply: item=%d requested=%d icons=%d\n", item,
                 Requested(*row) ? 1 : 0, Val().sharp_ui_icons);
}

void Close() {
    g_combo = nullptr;
}

}  // namespace

const Contribution& SharpUiEntry() {
    static const Contribution c = { "sharp_ui", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
