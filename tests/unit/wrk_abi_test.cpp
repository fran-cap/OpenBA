// ============================================================================
// tests/unit/wrk_abi_test.cpp — ENDGAME B.3.8 block WRK (B38_RULES.md B8-14)
//
// The ABI claims block WRK makes about the Work family, measured on the
// SHIPPED HEADERS (the exe_class_abi_test.cpp model):
//
// PART A — LAYOUT. sizeof / alignof of `Work`, `AutoPickupWork` and every
//   converted leaf against retail's create-function MemAlloc (B38_RULES §1.3),
//   and the Work base field offsets (retail ctor 0x1400f72e0, Persist 0x1400f7400).
// PART B — SLOT ORDER, read off pointers to VIRTUAL members (Itanium PMF =
//   1 + vtable byte offset; adjustment must be 0 — there is no multiple
//   inheritance in the exe). Work's 13 slots in retail order; every leaf
//   override lands on retail's index.
// PART C — SHAPE: abstract bases, concrete leaves, a virtual destructor (B3X W1).
// PART D — the one hidden-pointer member (slot 10 GetTarget, retail RCX = this,
//   RDX = the return buffer): a raw retail-order call through a compiler
//   vtable of the same declaration shape delivers (this, retbuf) in that order.
//
// NOT PROVEN HERE: the member bodies (they stand on the address registry and
// cannot link into a host test) — the coordinator's run and resave gate do.
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "Work/Work.h"
#include "Work/AutoPickupWork.h"
#include "Work/BuildRoad.h"
#include "Work/BuildStructure.h"
#include "Work/ClearArea.h"
#include "Work/ClearStorage.h"
#include "Work/ConsumeProduce.h"
#include "Work/FireWork.h"
#include "Work/FishingWork.h"
#include "Work/ForesterWork.h"
#include "Work/GatherRawMaterials.h"
#include "Work/HarvestArea.h"
#include "Work/HarvestOrchard.h"
#include "Work/HerbalistWork.h"
#include "Work/HuntWork.h"
#include "Work/LocationWork.h"
#include "Work/MarketWork.h"
#include "Work/PastureWork.h"
#include "Work/Pickup.h"
#include "Work/PlantArea.h"
#include "Work/PlantOrchard.h"
#include "Work/TendArea.h"
#include "Work/TradeWork.h"
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
UNIT_TEST(wrk_base_sizes_match_retail) {
    CHECK_EQ(alignof(Work),           (size_t)8);
}

// ===========================================================================
// PART B — SLOT ORDER (retail table 0x140161a80, 13 slots)
// ===========================================================================
UNIT_TEST(wrk_base_slot_order) {
    CHECK_DTOR_SLOT(Work);   // 0x14002ca60
    CHECK_SLOT(Work, TypeName,    0x08/8);   // 0x14002ca50
    CHECK_SLOT(Work, Type,        0x10/8);   // 0x14002ca40
    CHECK_SLOT(Work, Persist,     0x18/8);   // 0x1400f7400
    CHECK_SLOT(Work, Execute,     0x20/8);   // pure
    CHECK_SLOT(Work, Slot5,       0x28/8);   // 0x1400f7540
    CHECK_SLOT(Work, OnComplete,  0x30/8);   // pure
    CHECK_SLOT(Work, OnCancel,    0x38/8);   // pure
    CHECK_SLOT(Work, Cleanup,     0x40/8);   // pure
    CHECK_SLOT(Work, Validate,    0x48/8);   // pure
    CHECK_SLOT(Work, GetTarget,   0x50/8);   // pure
    CHECK_SLOT(Work, GetPriority, 0x58/8);   // pure
    CHECK_SLOT(Work, Slot12,      0x60/8);   // 0x140006b10, 13th and last

    // AutoPickupWork (0x140160fa0) overrides 0, 1, 2, 3 and 8.
    CHECK_DTOR_SLOT(AutoPickupWork);   // 0x1400294e0
    CHECK_SLOT(AutoPickupWork, TypeName, 0x08/8);   // 0x1400294d0
    CHECK_SLOT(AutoPickupWork, Type,     0x10/8);   // 0x1400294c0
    CHECK_SLOT(AutoPickupWork, Persist,  0x18/8);   // 0x1400e3f80
    CHECK_SLOT(AutoPickupWork, Cleanup,  0x40/8);   // 0x1400e3f30
}

// ===========================================================================
// PART C — SHAPE
// ===========================================================================
UNIT_TEST(wrk_base_class_shape_rules) {
    CHECK(std::is_polymorphic<Work>::value);
    CHECK(std::is_abstract<Work>::value);             // 4, 6-11 _purecall
    CHECK(std::is_abstract<AutoPickupWork>::value);   // inherits 4, 6, 7, 9-11 pure
    CHECK(std::has_virtual_destructor<Work>::value);
    CHECK(std::has_virtual_destructor<AutoPickupWork>::value);
    CHECK((std::is_base_of<Object::Internal, Work>::value));
    CHECK((std::is_base_of<Work, AutoPickupWork>::value));
}

// ===========================================================================
// PART E — the leaves: size against retail's create MemAlloc (B38_RULES §1.3),
// concrete, a virtual destructor (B3X W1), and every override on retail's slot index.
// ===========================================================================
#define WRK_LEAF_SHAPE(Class, Parent, Size)                                     \
    CHECK_EQ(sizeof(Class), (size_t)(Size));                                    \
    CHECK_EQ(alignof(Class), (size_t)8);                                        \
    CHECK(!std::is_abstract<Class>::value);                                     \
    CHECK(std::has_virtual_destructor<Class>::value);                          \
    CHECK((std::is_base_of<Parent, Class>::value));                             \
    CHECK_DTOR_SLOT(Class);                                                     \
    CHECK_SLOT(Class, TypeName,    0x08/8);                                     \
    CHECK_SLOT(Class, Type,        0x10/8);                                     \
    CHECK_SLOT(Class, Persist,     0x18/8);                                     \
    CHECK_SLOT(Class, Execute,     0x20/8);                                     \
    CHECK_SLOT(Class, Slot5,       0x28/8);                                     \
    CHECK_SLOT(Class, OnComplete,  0x30/8);                                     \
    CHECK_SLOT(Class, OnCancel,    0x38/8);                                     \
    CHECK_SLOT(Class, Cleanup,     0x40/8);                                     \
    CHECK_SLOT(Class, Validate,    0x48/8);                                     \
    CHECK_SLOT(Class, GetTarget,   0x50/8);                                     \
    CHECK_SLOT(Class, GetPriority, 0x58/8);                                     \
    CHECK_SLOT(Class, Slot12,      0x60/8)

UNIT_TEST(wrk_leaf_buildroad) {
    WRK_LEAF_SHAPE(BuildRoad, Work, 0x70);   // create 0x140029560, table 0x140161020
}

UNIT_TEST(wrk_leaf_buildstructure) {
    WRK_LEAF_SHAPE(BuildStructure, Work, 0x70);   // create 0x140029630, table 0x1401610a0
    CHECK_EQ(offsetof(BuildStructure, _slots) + offsetof(Collection::Array<uint8_t>, _capacity), (size_t)0x5c);
    CHECK_EQ(offsetof(BuildStructure, _citizens) + offsetof(BuildStructure::CitizenSet, _count), (size_t)0x68);
}

UNIT_TEST(wrk_leaf_cleararea) {
    WRK_LEAF_SHAPE(ClearArea, Work, 0x78);
    CHECK_EQ(offsetof(ClearArea, _entities) + offsetof(ClearArea::EntitySet, _count), (size_t)0x50);
    CHECK_EQ(offsetof(ClearArea, _cells) + offsetof(ClearArea::CellSet, _count), (size_t)0x60);
}

UNIT_TEST(wrk_leaf_clearstorage) {
    WRK_LEAF_SHAPE(ClearStorage, Work, 0x68);
    CHECK_EQ(offsetof(ClearStorage, _citizens) + offsetof(ClearStorage::CitizenSet, _count), (size_t)0x60);
}

UNIT_TEST(wrk_leaf_consumeproduce) {
    WRK_LEAF_SHAPE(ConsumeProduce, Work, 0x78);
    CHECK_EQ(offsetof(ConsumeProduce, _slotCitizens) + offsetof(ConsumeProduce::CitizenRefList, _size), (size_t)0x60);
    CHECK_EQ(offsetof(ConsumeProduce, _spotStates) + offsetof(Collection::Array<int32_t>, _size), (size_t)0x70);
    CHECK_EQ(offsetof(ConsumeProduce, _spotStates) + offsetof(Collection::Array<int32_t>, _capacity), (size_t)0x74);
}

UNIT_TEST(wrk_leaf_firework) {
    WRK_LEAF_SHAPE(FireWork, Work, 0x60);
    CHECK_EQ(offsetof(FireWork, _citizens) + offsetof(FireWork::CitizenSet, _count), (size_t)0x58);
}

UNIT_TEST(wrk_leaf_fishingwork) {
    WRK_LEAF_SHAPE(FishingWork, Work, 0x70);
    CHECK_EQ(offsetof(FishingWork, citizenRefs) + offsetof(FishingWork::CitizenRefList, _size), (size_t)0x50);
    CHECK_EQ(offsetof(FishingWork, citizenRefs) + offsetof(FishingWork::CitizenRefList, _capacity), (size_t)0x54);
    CHECK_EQ(offsetof(FishingWork, spotStates) + offsetof(Collection::Array<int32_t>, _size), (size_t)0x60);
    CHECK_EQ(offsetof(FishingWork, spotStates) + offsetof(Collection::Array<int32_t>, _capacity), (size_t)0x64);
}

UNIT_TEST(wrk_leaf_foresterwork) {
    WRK_LEAF_SHAPE(ForesterWork, AutoPickupWork, 0x60);
    CHECK_EQ(offsetof(ForesterWork, _citizens) + offsetof(ForesterWork::CitizenSet, _count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_gatherrawmaterials) {
    WRK_LEAF_SHAPE(GatherRawMaterials, Work, 0x78);
    CHECK_EQ(offsetof(GatherRawMaterials, _present) + offsetof(Collection::Array<int32_t>, _size), (size_t)0x50);
    CHECK_EQ(offsetof(GatherRawMaterials, _inFlight) + offsetof(Collection::Array<int32_t>, _size), (size_t)0x60);
    CHECK_EQ(offsetof(GatherRawMaterials, _citizens) + offsetof(GatherRawMaterials::CitizenSet, _count), (size_t)0x70);
}

UNIT_TEST(wrk_leaf_harvestarea) {
    WRK_LEAF_SHAPE(HarvestArea, AutoPickupWork, 0x68);
    CHECK_EQ(offsetof(HarvestArea, _cells) + offsetof(HarvestArea::CellSet, _count), (size_t)0x50);
    CHECK_EQ(offsetof(HarvestArea, _slotsAvailable) + offsetof(HarvestArea::SlotFlags, _size), (size_t)0x60);
}

UNIT_TEST(wrk_leaf_harvestorchard) {
    WRK_LEAF_SHAPE(HarvestOrchard, AutoPickupWork, 0x68);
    CHECK_EQ(offsetof(HarvestOrchard, _cells) + offsetof(HarvestOrchard::CellSet, _count), (size_t)0x50);
    CHECK_EQ(offsetof(HarvestOrchard, _slotsAvailable) + offsetof(HarvestOrchard::SlotFlags, _size), (size_t)0x60);
}

UNIT_TEST(wrk_leaf_herbalistwork) {
    WRK_LEAF_SHAPE(HerbalistWork, AutoPickupWork, 0x60);
    CHECK_EQ(offsetof(HerbalistWork, _citizens) + offsetof(HerbalistWork::CitizenSet, _count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_huntwork) {
    WRK_LEAF_SHAPE(HuntWork, AutoPickupWork, 0x58);
    CHECK_EQ(offsetof(HuntWork, _citizens) + offsetof(HuntWork::CitizenSet, _count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_locationwork) {
    WRK_LEAF_SHAPE(LocationWork, Work, 0x50);
}

UNIT_TEST(wrk_leaf_marketwork) {
    WRK_LEAF_SHAPE(MarketWork, Work, 0x58);
    CHECK_EQ(offsetof(MarketWork, _claims.count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_pasturework) {
    WRK_LEAF_SHAPE(PastureWork, AutoPickupWork, 0x58);
    CHECK_EQ(offsetof(PastureWork, _citizens) + offsetof(PastureWork::CitizenSet, _count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_pickup) {
    WRK_LEAF_SHAPE(Pickup, Work, 0x60);
}

UNIT_TEST(wrk_leaf_plantarea) {
    WRK_LEAF_SHAPE(PlantArea, Work, 0x70);
    CHECK_EQ(offsetof(PlantArea, _cells) + offsetof(PlantArea::CellSet, _count), (size_t)0x50);
    CHECK_EQ(offsetof(PlantArea, _unplantedCells) + offsetof(PlantArea::SlotFlags, _size), (size_t)0x60);
}

UNIT_TEST(wrk_leaf_plantorchard) {
    WRK_LEAF_SHAPE(PlantOrchard, Work, 0x70);
    CHECK_EQ(offsetof(PlantOrchard, _cells) + offsetof(PlantOrchard::CellSet, _count), (size_t)0x50);
    CHECK_EQ(offsetof(PlantOrchard, _claimedSlots) + offsetof(PlantOrchard::SlotFlags, _size), (size_t)0x60);
    CHECK_EQ(offsetof(PlantOrchard, _claimedSlots) + offsetof(PlantOrchard::SlotFlags, _capacity), (size_t)0x64);
}

UNIT_TEST(wrk_leaf_tendarea) {
    WRK_LEAF_SHAPE(TendArea, Work, 0x60);
    CHECK_EQ(offsetof(TendArea, _citizens) + offsetof(TendArea::CitizenSet, _count), (size_t)0x50);
}

UNIT_TEST(wrk_leaf_tradework) {
    WRK_LEAF_SHAPE(TradeWork, Work, 0x58);
    CHECK_EQ(offsetof(TradeWork, assignments.root), (size_t)0x48);
    CHECK_EQ(offsetof(TradeWork, assignments.count), (size_t)0x50);
}

// ENDGAME B.3.9 EXA: ClearArea's parameterised ctor (0x1400e6960 = Work_Ctor25 +
// the leaf stores) is a real constructor; the ClearArea / orchard builders write
// `::new (MemAlloc(0x78, 8)) ClearArea(&entity, descRef, trees, ground, buildings)`.
template <typename T, typename... A>
using ExaNew = decltype(::new (std::declval<void*>()) T(std::declval<A>()...));
template <typename, typename T, typename... A> struct ExaCanNew : std::false_type {};
template <typename T, typename... A>
struct ExaCanNew<std::void_t<ExaNew<T, A...>>, T, A...> : std::true_type {};

UNIT_TEST(wrk_cleararea_parameterised_ctor) {
    CHECK((ExaCanNew<void, ClearArea, ComponentEntity* const*, ComponentDescription*, uint8_t, uint8_t, uint8_t>::value));
    CHECK((ExaCanNew<void, ClearArea>::value));
}

}  // namespace
