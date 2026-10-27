// src/ext/touch_pointer/enhanced_entry.cpp -- ext.touch_pointer's row of the Enhanced tab (P11b)
//
// NOT RETAIL (docs/android/P11B_POINTER.md §3; the owner's ruling of 2026-10-26: ONE combo for the three
// keys). The combo "Touch pointer" Off / Default / Close aim / Far aim / Tap twice / Custom: Off = the bool
// row ext.touch_pointer off (its keys kept), a preset = the row on with ext.touch_pointer_lift /
// _pick_radius / _tap_twice set together (touch_pointer_math.h kTouchPresets), Custom = settings.cfg
// values no preset matches (choosing it turns the row on and keeps them). Live: the pointer reads the keys
// every poll.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"
#include "ext/touch_pointer/touch_pointer_math.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

struct Keys {
    const Entry* row;
    const ValueKey* lift;
    const ValueKey* radius;
    const ValueKey* tapTwice;
};

Keys FindKeys() {
    return Keys{FindEntry("touch_pointer"), FindValueKey("touch_pointer_lift"),
                FindValueKey("touch_pointer_pick_radius"), FindValueKey("touch_pointer_tap_twice")};
}

int32_t CurrentItem(const Keys& k) {
    if (k.row == nullptr || k.lift == nullptr || k.radius == nullptr || k.tapTwice == nullptr) return 0;
    return TouchPointer::TouchPresetItem(Requested(*k.row), Value(*k.lift), Value(*k.radius), Value(*k.tapTwice));
}

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboTouchPointer");
    const int32_t item = CurrentItem(FindKeys());
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[TouchPointer] combo=%p items=%d item=%d\n", static_cast<void*>(g_combo),
                 g_combo != nullptr ? g_combo->GetItemCount() : 0, item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Keys k = FindKeys();
    if (k.row == nullptr || k.lift == nullptr || k.radius == nullptr || k.tapTwice == nullptr) return;
    const int32_t item = g_combo->_currentItem;
    if (item <= TouchPointer::kTouchItemOff || item > TouchPointer::kTouchItemCustom) {
        if (Requested(*k.row)) SetAndRecord(*k.row, false);
    } else {
        if (item != TouchPointer::kTouchItemCustom) {
            const TouchPointer::TouchPreset& p = TouchPointer::kTouchPresets[item - 1];
            if (Value(*k.lift) != p.lift) SetValueAndRecord(*k.lift, p.lift);
            if (Value(*k.radius) != p.pickRadius) SetValueAndRecord(*k.radius, p.pickRadius);
            if (Value(*k.tapTwice) != p.tapTwice) SetValueAndRecord(*k.tapTwice, p.tapTwice);
        }
        if (!Requested(*k.row)) SetAndRecord(*k.row, true);
    }
    std::fprintf(stderr, "[TouchPointer] Apply: item=%d -> row %d lift %d radius %d tap twice %d (item now %d)\n", item,
                 Requested(*k.row) ? 1 : 0, Value(*k.lift), Value(*k.radius), Value(*k.tapTwice), CurrentItem(k));
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& TouchPointerEntry() {
    static const Contribution c = { "touch_pointer", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
