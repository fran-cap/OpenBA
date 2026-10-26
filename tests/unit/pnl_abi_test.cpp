// ============================================================================
// tests/unit/pnl_abi_test.cpp — ENDGAME B.3.8 block PNL (RULE B8-14)
//
// The exe panel classes (UI::DialogController subclasses) and the four
// UI::DialogControllerConfig subclasses, on their shipped headers:
//   PART A — LAYOUT: sizeof / alignof and every field offset against retail's
//            create-function MemAlloc and the bodies that read the fields.
//   PART B — SLOT ORDER: read off pointers to VIRTUAL members (Itanium PMF =
//            1 + vtable byte offset, adjustment 0 = single inheritance), so a
//            slot declared out of retail order fails here, not in a run.
//   PART C — SHAPE: polymorphic, a virtual destructor (B3X W1), abstract exactly where
//            retail is (SelectResourceUI, slots 11-13 _purecall).
// The panel bodies stand on the port registry and do not link into a host test.
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "Dialog/ControlUIConfig.h"
#include "Dialog/ResourceLimitUIConfig.h"
#include "Dialog/StatusIconUIConfig.h"
#include "Dialog/UpgradeUIConfig.h"
// @@PANEL-INCLUDES-BEGIN
#include "Dialog/AttendanceUI.h"
#include "Dialog/BuildRequirementUI.h"
#include "Dialog/BuildUI.h"
#include "Dialog/CemeteryUI.h"
#include "Dialog/CitizenUI.h"
#include "Dialog/ConsumeProduceUI.h"
#include "Dialog/ControlUI.h"
#include "Dialog/CropUI.h"
#include "Dialog/DebugUI.h"
#include "Dialog/DestroyUI.h"
#include "Dialog/EntityUI.h"
#include "Dialog/EventLogUI.h"
#include "Dialog/HelpUI.h"
#include "Dialog/HotKeyToolTipUI.h"
#include "Dialog/ImmigrationUI.h"
#include "Dialog/MapUI.h"
#include "Dialog/ModBrowseUI.h"
#include "Dialog/NoiseUI.h"
#include "Dialog/OrchardUI.h"
#include "Dialog/PageToggleUI.h"
#include "Dialog/PastureUI.h"
#include "Dialog/ProductionUI.h"
#include "Dialog/ProfessionUI.h"
#include "Dialog/RadiusUI.h"
#include "Dialog/ResidenceUI.h"
#include "Dialog/ResourceLimitUI.h"
#include "Dialog/SpriteCaptureUI.h"
#include "Dialog/StatusBarUI.h"
#include "Dialog/StatusIconUI.h"
#include "Dialog/StorageCapacityUI.h"
#include "Dialog/StorageUI.h"
#include "Dialog/TitleDescToolTipUI.h"
#include "Dialog/ToolUI.h"
#include "Dialog/TownHallUI.h"
#include "Dialog/TradeUI.h"
#include "Dialog/UpgradeUI.h"
#include "Dialog/WorkPlaceUI.h"
// @@PANEL-INCLUDES-END

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace {

// ===========================================================================
// The four DialogControllerConfig leaves (9 slots each, sizeof 0x28)
// ===========================================================================
template <typename C>
void CheckConfigSlots() {
    CHECK_DTOR_SLOT(C);
    CHECK_SLOT(C, TypeName,       0x08/8);
    CHECK_SLOT(C, Type,           0x10/8);
    CHECK_SLOT(C, GetFileVersion, 0x18/8);
    CHECK_SLOT(C, Compile,        0x20/8);
    CHECK_SLOT(C, Serialize,      0x28/8);
    CHECK_SLOT(C, Reconstruct,    0x30/8);
    CHECK_SLOT(C, Destruct,       0x38/8);
    CHECK_SLOT(C, Construct,      0x40/8);   // 9th and last
    CHECK(std::is_polymorphic<C>::value);
    CHECK(!std::is_abstract<C>::value);
    CHECK(std::has_virtual_destructor<C>::value);
    CHECK((std::is_base_of<UI::DialogControllerConfig, C>::value));
    CHECK_EQ(alignof(C), (size_t)8);
}

UNIT_TEST(pnl_config_layout_and_slots) {
    CheckConfigSlots<ControlUIConfig>();
    CheckConfigSlots<ResourceLimitUIConfig>();
    CheckConfigSlots<StatusIconUIConfig>();
    CheckConfigSlots<UpgradeUIConfig>();
}

// ===========================================================================
// The UI::DialogController leaves (11 slots; the base's slot list is
// src/engine/ui/DialogController.h's). Each class's declared overrides land
// on retail's index; the base slots are pinned once here.
// ===========================================================================
UNIT_TEST(pnl_dialogcontroller_base_slots) {
    using DC = UI::DialogController;
    CHECK_DTOR_SLOT(DC);
    CHECK_SLOT(DC, TypeName,            0x08/8);
    CHECK_SLOT(DC, Type,                0x10/8);
    CHECK_SLOT(DC, Persist,             0x18/8);
    CHECK_SLOT(DC, OnCreated,           0x20/8);
    CHECK_SLOT(DC, OnOpened,            0x28/8);
    CHECK_SLOT(DC, OnClosed,            0x30/8);
    CHECK_SLOT(DC, OnUpdateLayout,      0x38/8);
    CHECK_SLOT(DC, OnUpdate,            0x40/8);
    CHECK_SLOT(DC, OnElementEvent,      0x48/8);
    CHECK_SLOT(DC, OnSerializeSettings, 0x50/8);
}

template <typename C>
void CheckPanelShape(size_t size) {
    CHECK_EQ(sizeof(C), size);
    CHECK_EQ(alignof(C), (size_t)8);
    CHECK(std::is_polymorphic<C>::value);
    CHECK(!std::is_abstract<C>::value);
    CHECK(std::has_virtual_destructor<C>::value);
    CHECK((std::is_base_of<UI::DialogController, C>::value));
    CHECK_DTOR_SLOT(C);
    CHECK_SLOT(C, TypeName, 0x08/8);
    CHECK_SLOT(C, Type,     0x10/8);
}

// SelectResourceUI: abstract (slots 11-13 _purecall in 0x14015d330), 14 slots.
UNIT_TEST(pnl_selectresourceui_abstract_base) {
    using S = SelectResourceUI;
    CHECK(std::is_abstract<S>::value);
    CHECK(std::has_virtual_destructor<S>::value);
    CHECK((std::is_base_of<UI::DialogController, S>::value));
    CHECK_DTOR_SLOT(S);
    CHECK_SLOT(S, OnCreated,      0x20/8);   // 0x140078be0
    CHECK_SLOT(S, OnUpdateLayout, 0x38/8);   // 0x140078cb0
    CHECK_SLOT(S, OnUpdate,       0x40/8);   // 0x140078d30
    CHECK_SLOT(S, OnElementEvent, 0x48/8);   // 0x140078d80
    CHECK_SLOT(S, SetResource,    0x58/8);   // [11]
    CHECK_SLOT(S, GetResource,    0x60/8);   // [12]
    CHECK_SLOT(S, GetCategory,    0x68/8);   // [13] last
    // CropUI inherits slot 9 (retail's CropUI table holds 0x140078d80).
    CHECK_SLOT(CropUI, OnElementEvent, 0x48/8);
}

// @@PANEL-TESTS-BEGIN (generated from the per-class slot lists)
UNIT_TEST(pnl_AttendanceUI_layout_and_slots) {
    CheckPanelShape<AttendanceUI>((size_t)0x38);
    CHECK_SLOT(AttendanceUI, OnCreated,      0x20/8);   // 0x14005dd40
    CHECK_SLOT(AttendanceUI, OnUpdateLayout, 0x38/8);   // 0x14005dc50
}

UNIT_TEST(pnl_BuildRequirementUI_layout_and_slots) {
    CheckPanelShape<BuildRequirementUI>((size_t)0x60);
    CHECK_SLOT(BuildRequirementUI, Persist,   0x18/8);   // 0x14005ee80
    CHECK_SLOT(BuildRequirementUI, OnCreated, 0x20/8);   // 0x14005e7c0
}

UNIT_TEST(pnl_BuildUI_layout_and_slots) {
    CheckPanelShape<BuildUI>((size_t)0x50);
    CHECK_SLOT(BuildUI, OnCreated,      0x20/8);   // 0x1400b1f40
    CHECK_SLOT(BuildUI, OnUpdateLayout, 0x38/8);   // 0x1400b1a50
    CHECK_SLOT(BuildUI, OnElementEvent, 0x48/8);   // 0x1400b1ea0
}

UNIT_TEST(pnl_CemeteryUI_layout_and_slots) {
    CheckPanelShape<CemeteryUI>((size_t)0x38);
    CHECK_SLOT(CemeteryUI, OnCreated,      0x20/8);   // 0x14005f090
    CHECK_SLOT(CemeteryUI, OnUpdateLayout, 0x38/8);   // 0x14005ef50
}

UNIT_TEST(pnl_CitizenUI_layout_and_slots) {
    CheckPanelShape<CitizenUI>((size_t)0xe8);
    CHECK_SLOT(CitizenUI, OnCreated,      0x20/8);   // 0x1400651a0
    CHECK_SLOT(CitizenUI, OnClosed,       0x30/8);   // 0x140065130
    CHECK_SLOT(CitizenUI, OnUpdateLayout, 0x38/8);   // 0x140064ae0
    CHECK_SLOT(CitizenUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(CitizenUI, OnElementEvent, 0x48/8);   // 0x1400650f0
}

UNIT_TEST(pnl_ConsumeProduceUI_layout_and_slots) {
    CheckPanelShape<ConsumeProduceUI>((size_t)0x40);
    CHECK_SLOT(ConsumeProduceUI, OnCreated,      0x20/8);   // 0x14005f3d0
    CHECK_SLOT(ConsumeProduceUI, OnUpdateLayout, 0x38/8);   // 0x14005f1f0
    CHECK_SLOT(ConsumeProduceUI, OnElementEvent, 0x48/8);   // 0x14005f330
}

UNIT_TEST(pnl_ControlUI_layout_and_slots) {
    CheckPanelShape<ControlUI>((size_t)0x40);
    CHECK_SLOT(ControlUI, OnCreated,      0x20/8);   // 0x14005f9b0
    CHECK_SLOT(ControlUI, OnUpdateLayout, 0x38/8);   // 0x14005f870
    CHECK_SLOT(ControlUI, OnElementEvent, 0x48/8);   // 0x14005f7f0
}

UNIT_TEST(pnl_CropUI_layout_and_slots) {
    CheckPanelShape<CropUI>((size_t)0x60);
    CHECK_SLOT(CropUI, OnCreated,      0x20/8);   // 0x1400b2b50
    CHECK_SLOT(CropUI, OnUpdateLayout, 0x38/8);   // 0x1400b2a50
    CHECK_SLOT(CropUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(CropUI, SetResource,    0x58/8);   // 0x1400b2920
    CHECK_SLOT(CropUI, GetResource,    0x60/8);   // 0x1400b29d0
    CHECK_SLOT(CropUI, GetCategory,    0x68/8);   // 0x1400b29c0
}

UNIT_TEST(pnl_DebugUI_layout_and_slots) {
    CheckPanelShape<DebugUI>((size_t)0x130);
    CHECK_SLOT(DebugUI, OnCreated,      0x20/8);   // 0x1400619e0
    CHECK_SLOT(DebugUI, OnClosed,       0x30/8);   // 0x1400645f0
    CHECK_SLOT(DebugUI, OnUpdateLayout, 0x38/8);   // 0x140061280
    CHECK_SLOT(DebugUI, OnUpdate,       0x40/8);   // 0x1400610c0
    CHECK_SLOT(DebugUI, OnElementEvent, 0x48/8);   // 0x140060bc0
}

UNIT_TEST(pnl_DestroyUI_layout_and_slots) {
    CheckPanelShape<DestroyUI>((size_t)0x78);
    CHECK_SLOT(DestroyUI, OnCreated,      0x20/8);   // 0x140066150
    CHECK_SLOT(DestroyUI, OnUpdateLayout, 0x38/8);   // 0x140065b00
    CHECK_SLOT(DestroyUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(DestroyUI, OnElementEvent, 0x48/8);   // 0x140066080
}

UNIT_TEST(pnl_EntityUI_layout_and_slots) {
    CheckPanelShape<EntityUI>((size_t)0x48);
    CHECK_SLOT(EntityUI, Persist,        0x18/8);   // 0x1400672a0
    CHECK_SLOT(EntityUI, OnCreated,      0x20/8);   // 0x1400670d0
    CHECK_SLOT(EntityUI, OnClosed,       0x30/8);   // 0x140067090
    CHECK_SLOT(EntityUI, OnUpdateLayout, 0x38/8);   // 0x140066d80
    CHECK_SLOT(EntityUI, OnUpdate,       0x40/8);   // 0x140066ad0
    CHECK_SLOT(EntityUI, OnElementEvent, 0x48/8);   // 0x140067050
}

UNIT_TEST(pnl_EventLogUI_layout_and_slots) {
    CheckPanelShape<EventLogUI>((size_t)0x80);
    CHECK_SLOT(EventLogUI, OnOpened,       0x28/8);   // 0x140067640
    CHECK_SLOT(EventLogUI, OnClosed,       0x30/8);   // 0x140067630
    CHECK_SLOT(EventLogUI, OnUpdateLayout, 0x38/8);   // 0x140067440
    CHECK_SLOT(EventLogUI, OnElementEvent, 0x48/8);   // 0x1400675d0
}

UNIT_TEST(pnl_HelpUI_layout_and_slots) {
    CheckPanelShape<HelpUI>((size_t)0x78);
    CHECK_SLOT(HelpUI, OnCreated,      0x20/8);   // 0x14006a500
    CHECK_SLOT(HelpUI, OnOpened,       0x28/8);   // 0x14006a4f0
    CHECK_SLOT(HelpUI, OnUpdateLayout, 0x38/8);   // 0x14006a230
    CHECK_SLOT(HelpUI, OnElementEvent, 0x48/8);   // 0x14006a290
}

UNIT_TEST(pnl_HotKeyToolTipUI_layout_and_slots) {
    CheckPanelShape<HotKeyToolTipUI>((size_t)0x30);
    CHECK_SLOT(HotKeyToolTipUI, Persist,   0x18/8);   // 0x14006ab80
    CHECK_SLOT(HotKeyToolTipUI, OnCreated, 0x20/8);   // 0x14006a980
}

UNIT_TEST(pnl_ImmigrationUI_layout_and_slots) {
    CheckPanelShape<ImmigrationUI>((size_t)0x50);
    CHECK_SLOT(ImmigrationUI, OnCreated,      0x20/8);   // 0x14006b330
    CHECK_SLOT(ImmigrationUI, OnUpdateLayout, 0x38/8);   // 0x14006b130
    CHECK_SLOT(ImmigrationUI, OnElementEvent, 0x48/8);   // 0x14006b300
}

UNIT_TEST(pnl_MapUI_layout_and_slots) {
    CheckPanelShape<MapUI>((size_t)0x50);
    CHECK_SLOT(MapUI, Persist,        0x18/8);   // 0x14006ce80
    CHECK_SLOT(MapUI, OnCreated,      0x20/8);   // 0x14006ca80
    CHECK_SLOT(MapUI, OnElementEvent, 0x48/8);   // 0x14006c9e0
}

UNIT_TEST(pnl_ModBrowseUI_layout_and_slots) {
    CheckPanelShape<ModBrowseUI>((size_t)0x30);
    CHECK_SLOT(ModBrowseUI, Persist, 0x18/8);   // 0x14006d390
}

UNIT_TEST(pnl_NoiseUI_layout_and_slots) {
    CheckPanelShape<NoiseUI>((size_t)0x70);
    CHECK_SLOT(NoiseUI, Persist,        0x18/8);   // 0x140071bb0
    CHECK_SLOT(NoiseUI, OnCreated,      0x20/8);   // 0x1400714c0
    CHECK_SLOT(NoiseUI, OnElementEvent, 0x48/8);   // 0x1400714a0
}

UNIT_TEST(pnl_OrchardUI_layout_and_slots) {
    CheckPanelShape<OrchardUI>((size_t)0x48);
    CHECK_SLOT(OrchardUI, OnCreated,      0x20/8);   // 0x1400755b0
    CHECK_SLOT(OrchardUI, OnUpdateLayout, 0x38/8);   // 0x1400754b0
    CHECK_SLOT(OrchardUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(OrchardUI, OnElementEvent, 0x48/8);   // standalone deviation
    CHECK_SLOT(OrchardUI, SetResource,    0x58/8);   // 0x140075380
    CHECK_SLOT(OrchardUI, GetResource,    0x60/8);   // 0x140075430
    CHECK_SLOT(OrchardUI, GetCategory,    0x68/8);   // 0x140075420
}

UNIT_TEST(pnl_PageToggleUI_layout_and_slots) {
    CheckPanelShape<PageToggleUI>((size_t)0x48);
    CHECK_SLOT(PageToggleUI, Persist,        0x18/8);   // 0x1400759a0
    CHECK_SLOT(PageToggleUI, OnCreated,      0x20/8);   // 0x140075850
    CHECK_SLOT(PageToggleUI, OnUpdateLayout, 0x38/8);   // 0x140075780
    CHECK_SLOT(PageToggleUI, OnElementEvent, 0x48/8);   // 0x140075710
}

UNIT_TEST(pnl_PastureUI_layout_and_slots) {
    CheckPanelShape<PastureUI>((size_t)0x40);
    CHECK_SLOT(PastureUI, OnCreated,      0x20/8);   // 0x1400b5090
    CHECK_SLOT(PastureUI, OnUpdateLayout, 0x38/8);   // 0x1400b4e50
    CHECK_SLOT(PastureUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(PastureUI, OnElementEvent, 0x48/8);   // 0x1400b4e20
    CHECK_SLOT(PastureUI, SetResource,    0x58/8);   // 0x1400b4c40
    CHECK_SLOT(PastureUI, GetResource,    0x60/8);   // 0x1400b4d00
    CHECK_SLOT(PastureUI, GetCategory,    0x68/8);   // 0x1400b4cf0
}

UNIT_TEST(pnl_ProductionUI_layout_and_slots) {
    CheckPanelShape<ProductionUI>((size_t)0x50);
    CHECK_SLOT(ProductionUI, Persist,        0x18/8);   // 0x140076300
    CHECK_SLOT(ProductionUI, OnCreated,      0x20/8);   // 0x140075da0
    CHECK_SLOT(ProductionUI, OnUpdateLayout, 0x38/8);   // 0x140075a20
}

UNIT_TEST(pnl_ProfessionUI_layout_and_slots) {
    CheckPanelShape<ProfessionUI>((size_t)0x78);
    CHECK_SLOT(ProfessionUI, OnOpened,       0x28/8);   // 0x140076ca0
    CHECK_SLOT(ProfessionUI, OnUpdate,       0x40/8);   // 0x1400765a0
    CHECK_SLOT(ProfessionUI, OnElementEvent, 0x48/8);   // 0x140076c10
}

UNIT_TEST(pnl_RadiusUI_layout_and_slots) {
    CheckPanelShape<RadiusUI>((size_t)0x38);
    CHECK_SLOT(RadiusUI, OnOpened, 0x28/8);   // 0x140077650
    CHECK_SLOT(RadiusUI, OnClosed, 0x30/8);   // 0x140077600
}

UNIT_TEST(pnl_ResidenceUI_layout_and_slots) {
    CheckPanelShape<ResidenceUI>((size_t)0x40);
    CHECK_SLOT(ResidenceUI, Persist,        0x18/8);   // 0x1400b5db0
    CHECK_SLOT(ResidenceUI, OnCreated,      0x20/8);   // 0x1400b5850
    CHECK_SLOT(ResidenceUI, OnUpdateLayout, 0x38/8);   // 0x1400b51f0
    CHECK_SLOT(ResidenceUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(ResidenceUI, OnElementEvent, 0x48/8);   // 0x1400b5820
}

UNIT_TEST(pnl_ResourceLimitUI_layout_and_slots) {
    CheckPanelShape<ResourceLimitUI>((size_t)0x40);
    CHECK_SLOT(ResourceLimitUI, OnCreated,      0x20/8);   // 0x140077970
    CHECK_SLOT(ResourceLimitUI, OnUpdateLayout, 0x38/8);   // 0x1400778d0
    CHECK_SLOT(ResourceLimitUI, OnElementEvent, 0x48/8);   // 0x1400777e0
}

UNIT_TEST(pnl_SpriteCaptureUI_layout_and_slots) {
    CheckPanelShape<SpriteCaptureUI>((size_t)0x70);
    CHECK_SLOT(SpriteCaptureUI, Persist,        0x18/8);   // 0x140079850
    CHECK_SLOT(SpriteCaptureUI, OnCreated,      0x20/8);   // 0x140079640
    CHECK_SLOT(SpriteCaptureUI, OnUpdate,       0x40/8);   // 0x1400793a0
    CHECK_SLOT(SpriteCaptureUI, OnElementEvent, 0x48/8);   // 0x140079620
}

UNIT_TEST(pnl_StatusBarUI_layout_and_slots) {
    CheckPanelShape<StatusBarUI>((size_t)0x1d0);
    CHECK_SLOT(StatusBarUI, OnCreated,      0x20/8);   // standalone deviation
    CHECK_SLOT(StatusBarUI, OnOpened,       0x28/8);   // 0x14007a3d0
    CHECK_SLOT(StatusBarUI, OnClosed,       0x30/8);   // 0x14007a3c0
    CHECK_SLOT(StatusBarUI, OnUpdateLayout, 0x38/8);   // 0x1400798d0
    CHECK_SLOT(StatusBarUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(StatusBarUI, OnElementEvent, 0x48/8);   // 0x14007a3a0
}

UNIT_TEST(pnl_StatusIconUI_layout_and_slots) {
    CheckPanelShape<StatusIconUI>((size_t)0x40);
    CHECK_SLOT(StatusIconUI, OnCreated,      0x20/8);   // 0x14007b270
    CHECK_SLOT(StatusIconUI, OnUpdateLayout, 0x38/8);   // 0x14007b0a0
    CHECK_SLOT(StatusIconUI, OnUpdate,       0x40/8);   // standalone deviation
}

UNIT_TEST(pnl_StorageCapacityUI_layout_and_slots) {
    CheckPanelShape<StorageCapacityUI>((size_t)0x38);
    CHECK_SLOT(StorageCapacityUI, Persist,        0x18/8);   // 0x14007b6b0
    CHECK_SLOT(StorageCapacityUI, OnCreated,      0x20/8);   // 0x14007b600
    CHECK_SLOT(StorageCapacityUI, OnUpdateLayout, 0x38/8);   // 0x14007b4d0
    CHECK_SLOT(StorageCapacityUI, OnUpdate,       0x40/8);   // standalone deviation
}

UNIT_TEST(pnl_StorageUI_layout_and_slots) {
    CheckPanelShape<StorageUI>((size_t)0x40);
    CHECK_SLOT(StorageUI, Persist,        0x18/8);   // 0x1400b8ca0
    CHECK_SLOT(StorageUI, OnCreated,      0x20/8);   // 0x1400b8820
    CHECK_SLOT(StorageUI, OnUpdateLayout, 0x38/8);   // 0x1400b8450
    CHECK_SLOT(StorageUI, OnUpdate,       0x40/8);   // standalone deviation
}

UNIT_TEST(pnl_TitleDescToolTipUI_layout_and_slots) {
    CheckPanelShape<TitleDescToolTipUI>((size_t)0x30);
    CHECK_SLOT(TitleDescToolTipUI, Persist,   0x18/8);   // 0x14007b8a0
    CHECK_SLOT(TitleDescToolTipUI, OnCreated, 0x20/8);   // 0x14007b730
}

UNIT_TEST(pnl_ToolUI_layout_and_slots) {
    CheckPanelShape<ToolUI>((size_t)0x58);
    CHECK_SLOT(ToolUI, Persist,        0x18/8);   // 0x14007c070
    CHECK_SLOT(ToolUI, OnCreated,      0x20/8);   // 0x14007bd70
    CHECK_SLOT(ToolUI, OnUpdate,       0x40/8);   // 0x14007b9b0
    CHECK_SLOT(ToolUI, OnElementEvent, 0x48/8);   // 0x14007bc30
}

UNIT_TEST(pnl_TownHallUI_layout_and_slots) {
    CheckPanelShape<TownHallUI>((size_t)0x1e8);
    CHECK_SLOT(TownHallUI, OnCreated,           0x20/8);   // 0x140081000
    CHECK_SLOT(TownHallUI, OnUpdateLayout,      0x38/8);   // 0x14007d550
    CHECK_SLOT(TownHallUI, OnUpdate,            0x40/8);   // 0x14007e550
    CHECK_SLOT(TownHallUI, OnElementEvent,      0x48/8);   // 0x14007c180
    CHECK_SLOT(TownHallUI, OnSerializeSettings, 0x50/8);   // 0x140080db0
}

UNIT_TEST(pnl_TradeUI_layout_and_slots) {
    CheckPanelShape<TradeUI>((size_t)0x148);
    CHECK_SLOT(TradeUI, OnCreated,           0x20/8);   // 0x140084dd0
    CHECK_SLOT(TradeUI, OnOpened,            0x28/8);   // 0x140082a70
    CHECK_SLOT(TradeUI, OnClosed,            0x30/8);   // 0x140082a00
    CHECK_SLOT(TradeUI, OnUpdateLayout,      0x38/8);   // 0x140082150
    CHECK_SLOT(TradeUI, OnUpdate,            0x40/8);   // 0x1400827b0
    CHECK_SLOT(TradeUI, OnElementEvent,      0x48/8);   // 0x140081f20
    CHECK_SLOT(TradeUI, OnSerializeSettings, 0x50/8);   // 0x1400868c0
}

UNIT_TEST(pnl_UpgradeUI_layout_and_slots) {
    CheckPanelShape<UpgradeUI>((size_t)0x38);
    CHECK_SLOT(UpgradeUI, OnCreated,      0x20/8);   // 0x140086d90
    CHECK_SLOT(UpgradeUI, OnUpdateLayout, 0x38/8);   // 0x140086d80
    CHECK_SLOT(UpgradeUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(UpgradeUI, OnElementEvent, 0x48/8);   // 0x140086c90
}

UNIT_TEST(pnl_WorkPlaceUI_layout_and_slots) {
    CheckPanelShape<WorkPlaceUI>((size_t)0x68);
    CHECK_SLOT(WorkPlaceUI, OnCreated,      0x20/8);   // 0x140087940
    CHECK_SLOT(WorkPlaceUI, OnUpdateLayout, 0x38/8);   // 0x140087080
    CHECK_SLOT(WorkPlaceUI, OnUpdate,       0x40/8);   // standalone deviation
    CHECK_SLOT(WorkPlaceUI, OnElementEvent, 0x48/8);   // 0x1400877e0
}
// @@PANEL-TESTS-END

}  // namespace
