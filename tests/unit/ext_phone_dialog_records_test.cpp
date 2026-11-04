// ============================================================================
// tests/unit/ext_phone_dialog_records_test.cpp -- ext.phone_menu's dialog records
//   (src/ext/phone_menu/dialog_records.h, review F-257)
// ============================================================================
// The menus' descriptions are evicted between a menu and a game and come back at new addresses
// (a stock run logged five main menus at five addresses). The old tables kept three addresses per
// kind and never dropped one: the fourth menu was not recorded and lost its layout, and a recorded
// address reused by another resource would have been taken for a menu. A record is now the
// address with its path's hash, in a ring.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/dialog_records.h"

#include <cstdint>

using Ext::PhoneMenu::DialogRecords;
using Ext::PhoneMenu::PathHash;

namespace {
int g_slots[8];
}  // namespace

UNIT_TEST(ext_phone_dialog_records_path_hash_folds_case) {
    CHECK(PathHash("Dialog/MainMenu.rsc") == PathHash("dialog/mainmenu.RSC"));
    CHECK(PathHash("Dialog/MainMenu.rsc") != PathHash("Dialog/Pause.rsc"));
    CHECK(PathHash(nullptr) == PathHash(""));
}

UNIT_TEST(ext_phone_dialog_records_newest_is_always_recorded) {
    DialogRecords<3> menus;
    const char* const menu = "Dialog/MainMenu.rsc";
    for (int i = 0; i < 5; ++i) {
        menus.Record(&g_slots[i], menu);
        CHECK(menus.Has(&g_slots[i], menu));
    }
    // The ring keeps the last three.
    CHECK(!menus.Has(&g_slots[0], menu));
    CHECK(!menus.Has(&g_slots[1], menu));
    CHECK(menus.Has(&g_slots[2], menu));
    CHECK(menus.Has(&g_slots[3], menu));
    CHECK(menus.Has(&g_slots[4], menu));
    // Recording a recorded address again takes no new slot.
    menus.Record(&g_slots[4], menu);
    CHECK(menus.Has(&g_slots[2], menu));
}

UNIT_TEST(ext_phone_dialog_records_reused_address_is_not_the_menu) {
    DialogRecords<3> menus;
    const char* const menu = "Dialog/MainMenu.rsc";
    const char* const other = "Dialog/SaveLoad.rsc";
    menus.Record(&g_slots[0], menu);
    // Another description at the evicted menu's address.
    CHECK(!menus.Has(&g_slots[0], other));
    CHECK(!menus.Has(nullptr, menu));
    // The address recorded again for another path answers for that path only.
    menus.Record(&g_slots[0], other);
    CHECK(menus.Has(&g_slots[0], other));
    CHECK(!menus.Has(&g_slots[0], menu));
    // Case does not matter (resource paths compare with _stricmp).
    CHECK(menus.Has(&g_slots[0], "dialog/saveload.RSC"));
}
