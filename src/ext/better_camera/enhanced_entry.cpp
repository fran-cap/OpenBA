// src/ext/better_camera/enhanced_entry.cpp -- ext.better_camera's row of the Enhanced tab (BETTERCAM)
//
// NOT RETAIL. The combo "Wider zoom": Off / On (next game) = the bool row `ext.better_camera`. Class B: the
// row is locked for a game's lifetime, so the choice is the REQUESTED value (SetAndRecord changes only the
// request while a game holds the lock) and the next new game or load picks it up, as from the Extensions
// dialog; a loaded save keeps the set it was saved with (docs/extensions/handins/BETTERCAM.md). The combo shows
// the request, so a choice made in a game reads back as made.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterCamera");
    const Entry* row = FindEntry("better_camera");
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[BetterCamera] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("better_camera");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[BetterCamera] Apply: item=%d requested=%d active=%d\n", g_combo->_currentItem,
                 Requested(*row) ? 1 : 0, Active(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& BetterCameraEntry() {
    static const Contribution c = { "better_camera", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
