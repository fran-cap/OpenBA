// src/ext/phone_menu/enhanced_entry.cpp -- ext.phone_menu's row of the Enhanced tab (issue #96)
//
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md "Safe area"). The combo "Screen edge margin" None / 2 % /
// 3.4 % (phone) / 4.5 % / 6 %: item i = ext.phone_menu_safe_margin's i-th choice (registry.cpp
// kPhoneSafeMargins, thousandths of a percent of the screen height). The key only; the row's bool stays the
// Extensions dialog's. Live: every safe-area reader goes through phone_math.h SafeMargin, so the menus and
// the game's dialogs are placed in the new margin from their next layout.

#include "ext/enhanced_tab/enhanced_tab.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboPhoneSafeMargin");
    OpenChoiceCombo(g_combo, "phone_menu_safe_margin");
    std::fprintf(stderr, "[PhoneMenu] margin combo=%p item=%d\n", static_cast<void*>(g_combo),
                 g_combo != nullptr ? g_combo->_currentItem : -1);
}

void Apply() {
    ApplyChoiceCombo(g_combo, "phone_menu_safe_margin", "[PhoneMenu]");
}

void Close() {
    g_combo = nullptr;
}

}  // namespace

const Contribution& PhoneMenuEntry() {
    static const Contribution c = { "phone_menu", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
