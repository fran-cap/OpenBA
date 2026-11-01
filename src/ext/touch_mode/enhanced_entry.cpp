// src/ext/touch_mode/enhanced_entry.cpp -- ext.touch_mode's row of the Enhanced tab (TOUCHMODE)
//
// NOT RETAIL (docs/android/TOUCH_MODE.md section 5). The combo "Touch controls" Off / Toolbar 100 % ..
// 200 %: Off = the bool row ext.touch_mode off (the stored ext.touch_mode_toolbar_scale is kept), a size =
// the row on with the toolbar scale key at that percent. SetTouchModeAndRecord records the keys; the
// controls and the toolbar follow from the next frame (live).
// Issue #96: two more combos of the same row, each one value key, item i = the key's i-th choice
// (registry.cpp kTouchPadScales / kTouchActionBarScales): "Camera pad" Off / 50 % .. 125 %
// (ext.touch_mode_pad_scale) and "Placement bar" 50 % .. 125 % (ext.touch_mode_action_bar_scale). Neither
// touches the row's bool; both are live.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;
UI::ComboElement* g_pad = nullptr;
UI::ComboElement* g_actionBar = nullptr;

constexpr int32_t kScales[] = { 0, 100, 125, 150, 175, 200 };   // the combo's items, in order
constexpr int kItemCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

int ItemOf(int32_t percent) {
    for (int i = 0; i < kItemCount; ++i) {
        if (kScales[i] == percent) return i;
    }
    return 0;
}


void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboTouchMode");
    const int item = ItemOf(TouchModeChoice());
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    g_pad = FindElement<UI::ComboElement>(page, "comboTouchPad");
    OpenChoiceCombo(g_pad, "touch_mode_pad_scale");
    g_actionBar = FindElement<UI::ComboElement>(page, "comboTouchActionBar");
    OpenChoiceCombo(g_actionBar, "touch_mode_action_bar_scale");
    std::fprintf(stderr, "[TouchMode] combo=%p items=%d item=%d, pad %p, placement bar %p\n",
                 static_cast<void*>(g_combo), g_combo != nullptr ? g_combo->GetItemCount() : 0, item,
                 static_cast<void*>(g_pad), static_cast<void*>(g_actionBar));
}

void Apply() {
    ApplyChoiceCombo(g_pad, "touch_mode_pad_scale", "[TouchMode]");
    ApplyChoiceCombo(g_actionBar, "touch_mode_action_bar_scale", "[TouchMode]");
    if (g_combo == nullptr) return;
    const int item = g_combo->_currentItem;
    const int32_t percent = SetTouchModeAndRecord(item >= 0 && item < kItemCount ? kScales[item] : 0);
    std::fprintf(stderr, "[TouchMode] Apply: item=%d toolbar %d %% (0 = off)\n", item, percent);
}

void Close() {
    g_combo = nullptr;
    g_pad = nullptr;
    g_actionBar = nullptr;
}

}  // namespace

const Contribution& TouchModeEntry() {
    static const Contribution c = { "touch_mode", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
