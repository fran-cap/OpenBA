// ============================================================================
// tests/unit/dlg_abi_test.cpp — ENDGAME B.3.8 block DLG (B38_RULES D1-D5, B8-14)
//
// The ABI claims block DLG makes for the exe `UI::DialogInstance` subclasses,
// measured on the SHIPPED class headers under src/game/Dialog/:
//
// PART A — LAYOUT. sizeof / alignof and the field offsets past +0xb8 are each
//   header's static_asserts (src/game/Dialog/*.h); the Array-count sub-field
//   offsets the bodies read raw are pinned here.
// PART B — SLOT ORDER, read off pointers to VIRTUAL members (Itanium PMF =
//   1 + vtable byte offset, adjustment 0: single inheritance). The retail
//   table is DialogInstance's 10 slots ([0] dtor, [1] TypeName, [2] Type,
//   [3] Persist, [4] OnOpened, [5] OnClosed, [6] OnUpdateLayout, [7] OnUpdate,
//   [8] OnElementEvent, [9] OnSerializeSettings); MessageDialog adds 10-13.
// PART C — SHAPE. polymorphic, NOT abstract (every class here is concrete in
//   retail), a virtual destructor (B3X W1; was "none", ENDGAME decision 2), the proved parent.
//
// NOT PROVEN HERE: the member bodies (they stand on the address registry and
// the UI runtime and cannot link into a host test). The real run and the resave
// gate verify those.
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "Dialog/MessageDialog.h"
#include "Dialog/OptionInputBindDialog.h"
#include "Dialog/EventNotifyDialog.h"
#include "Dialog/EventPopupDialog.h"
#include "Dialog/LoaderDialog.h"
#include "Dialog/AutoSaveDialog.h"
#include "Dialog/MainMenuDialog.h"
#include "Dialog/MapParamsDialog.h"
#include "Dialog/AchievementDialog.h"
#include "Dialog/GoalDialog.h"
#include "Dialog/ScenarioDialog.h"
#include "Dialog/FollowDialog.h"
#include "Dialog/NewGameDialog.h"
#include "Dialog/OptionsDialog.h"
#include "Dialog/SaveLoadDialog.h"
#include "Dialog/ModDialog.h"
#include "Dialog/ModDetailsDialog.h"
#include "Dialog/ModUploadDialog.h"
#include "Dialog/ToolbarDialog.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace {

template <typename T>
void CheckShape() {
    CHECK(std::is_polymorphic<T>::value);
    CHECK(!std::is_abstract<T>::value);
    CHECK(std::has_virtual_destructor<T>::value);
    CHECK((std::is_base_of<UI::DialogInstance, T>::value));
}

// The DialogInstance slots every dialog carries, read through class T (an
// override or the inherited base body answers at the same offset).
template <typename T>
void CheckDialogInstanceSlots() {
    CHECK_DTOR_SLOT(T);
    CHECK_SLOT(T, TypeName,            0x08/8);
    CHECK_SLOT(T, Type,                0x10/8);
    CHECK_SLOT(T, Persist,             0x18/8);
    CHECK_SLOT(T, OnOpened,            0x20/8);
    CHECK_SLOT(T, OnClosed,            0x28/8);
    CHECK_SLOT(T, OnUpdateLayout,      0x30/8);
    CHECK_SLOT(T, OnUpdate,            0x38/8);
    CHECK_SLOT(T, OnElementEvent,      0x40/8);
    CHECK_SLOT(T, OnSerializeSettings, 0x48/8);
}

}  // namespace

// ===========================================================================
// MessageDialog (0x14015c480, 14 slots, 0xc0) -> OptionInputBindDialog
// (0x14015ce40, 14 slots, 0xc8)
// ===========================================================================
UNIT_TEST(dlg_message_dialog_slots) {
    CheckShape<MessageDialog>();
    CheckShape<OptionInputBindDialog>();
    CHECK((std::is_base_of<MessageDialog, OptionInputBindDialog>::value));
    CheckDialogInstanceSlots<MessageDialog>();
    CheckDialogInstanceSlots<OptionInputBindDialog>();
    CHECK_SLOT(MessageDialog, OnOk,         0x50/8);
    CHECK_SLOT(MessageDialog, OnCancel,     0x58/8);
    CHECK_SLOT(MessageDialog, OnYes,        0x60/8);
    CHECK_SLOT(MessageDialog, OnNo,         0x68/8);
    CHECK_SLOT(OptionInputBindDialog, OnOk, 0x50/8);
    CHECK_SLOT(OptionInputBindDialog, OnNo, 0x68/8);
}

// ===========================================================================
// EventNotifyDialog (0x14015bca0, 10 slots, 0xf0), EventPopupDialog
// (0x14015bd80, 10 slots, 0xc0)
// ===========================================================================
UNIT_TEST(dlg_event_dialogs) {
    CheckShape<EventNotifyDialog>();
    CheckShape<EventPopupDialog>();
    CheckDialogInstanceSlots<EventNotifyDialog>();
    CheckDialogInstanceSlots<EventPopupDialog>();
}

// ===========================================================================
// LoaderDialog (0x14015f300, 10 slots, 0xc8), AutoSaveDialog (0x14015b120,
// 10 slots, 0xd0)
// ===========================================================================
UNIT_TEST(dlg_loader_autosave_dialogs) {
    CheckShape<LoaderDialog>();
    CheckShape<AutoSaveDialog>();
    CheckDialogInstanceSlots<LoaderDialog>();
    CheckDialogInstanceSlots<AutoSaveDialog>();
}

UNIT_TEST(dlg_mainmenu_mapparams_dialogs) {
    CheckShape<MainMenuDialog>();
    CheckDialogInstanceSlots<MainMenuDialog>();
    CheckShape<MapParamsDialog>();
    CheckDialogInstanceSlots<MapParamsDialog>();
}

UNIT_TEST(dlg_achievement_goal_dialogs) {
    CHECK_EQ(offsetof(AchievementDialog, entries._size), (size_t)0xc8);
    CheckShape<AchievementDialog>();
    CheckDialogInstanceSlots<AchievementDialog>();
    CHECK_EQ(offsetof(GoalDialog, goals) + 8, (size_t)0xc0);
    CheckShape<GoalDialog>();
    CheckDialogInstanceSlots<GoalDialog>();
}

UNIT_TEST(dlg_scenario_dialog) {
    CHECK_EQ(offsetof(ScenarioDialog, entries) + offsetof(Collection::Array<ScenarioDialogEntry>, _size), (size_t)0xe0);
    CheckShape<ScenarioDialog>();
    CheckDialogInstanceSlots<ScenarioDialog>();
}

UNIT_TEST(dlg_follow_dialog) {
    CheckShape<FollowDialog>();
    CheckDialogInstanceSlots<FollowDialog>();
}

UNIT_TEST(dlg_newgame_dialog) {
    CheckShape<NewGameDialog>();
    CheckDialogInstanceSlots<NewGameDialog>();
}

UNIT_TEST(dlg_options_dialog) {
    CheckShape<OptionsDialog>();
    CheckDialogInstanceSlots<OptionsDialog>();
}

UNIT_TEST(dlg_saveload_dialog) {
    CheckShape<SaveLoadDialog>();
    CheckDialogInstanceSlots<SaveLoadDialog>();
}

UNIT_TEST(dlg_mod_dialogs) {
    CheckShape<ModDialog>();
    CheckDialogInstanceSlots<ModDialog>();
    CheckShape<ModDetailsDialog>();
    CheckDialogInstanceSlots<ModDetailsDialog>();
}

UNIT_TEST(dlg_modupload_dialog) {
    CheckShape<ModUploadDialog>();
    CheckDialogInstanceSlots<ModUploadDialog>();
}

UNIT_TEST(dlg_toolbar_dialog) {
    CHECK_EQ(offsetof(ToolbarDialog, speedValues) + offsetof(Collection::Array<int32_t>, _size), (size_t)0x100);
    CheckShape<ToolbarDialog>();
    CheckDialogInstanceSlots<ToolbarDialog>();
}

// B38 D3.1: the slots retail fills with DialogInstance's body are INHERITED,
// not overridden. For an inherited member, &T::M names DialogInstance's member,
// so its pointer type is `R (UI::DialogInstance::*)(...)`.
namespace {
template <typename T> constexpr bool InheritsOnClosed =
    std::is_same<decltype(&T::OnClosed), void (UI::DialogInstance::*)()>::value;
template <typename T> constexpr bool InheritsOnUpdateLayout =
    std::is_same<decltype(&T::OnUpdateLayout), void (UI::DialogInstance::*)()>::value;
template <typename T> constexpr bool InheritsOnUpdate =
    std::is_same<decltype(&T::OnUpdate), void (UI::DialogInstance::*)(const System::Time&)>::value;
template <typename T> constexpr bool InheritsOnElementEvent =
    std::is_same<decltype(&T::OnElementEvent),
                 void (UI::DialogInstance::*)(Object::InternalPtr<UI::Element>*, int32_t, int32_t,
                                              int32_t, int32_t, int32_t, void*)>::value;
}  // namespace

UNIT_TEST(dlg_d31_inherited_slots) {
    CHECK(InheritsOnUpdateLayout<MessageDialog>);
    CHECK(InheritsOnUpdate<MessageDialog>);
    CHECK(InheritsOnClosed<EventPopupDialog>);
    CHECK(InheritsOnUpdateLayout<EventPopupDialog>);
    CHECK(InheritsOnUpdate<EventPopupDialog>);
    CHECK(InheritsOnClosed<LoaderDialog>);
    CHECK(InheritsOnElementEvent<LoaderDialog>);
    CHECK(InheritsOnClosed<AutoSaveDialog>);
    CHECK(InheritsOnClosed<AchievementDialog>);
    CHECK(InheritsOnClosed<GoalDialog>);
    CHECK(InheritsOnClosed<ScenarioDialog>);
    CHECK(InheritsOnUpdateLayout<OptionsDialog>);
    CHECK(InheritsOnUpdate<OptionsDialog>);
    CHECK(InheritsOnClosed<SaveLoadDialog>);
    CHECK(InheritsOnClosed<ModUploadDialog>);
    CHECK(InheritsOnUpdate<ToolbarDialog>);
    // and an own slot is NOT inherited
    CHECK(!InheritsOnClosed<ToolbarDialog>);
    CHECK(!InheritsOnClosed<MessageDialog>);
}
