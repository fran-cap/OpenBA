// ============================================================================
// tests/unit/k1_abi_test.cpp — ENDGAME B.3.8 block K1 (B38_RULES.md B8-14)
//
// The ABI claims block K1 makes for the exe `Description` leaves,
// `ComponentDescription`, `EnvironmentDescription` and `ComponentEntity`,
// measured on the SHIPPED HEADERS (the real run and the resave gate verify
// the bodies):
//   - sizeof / alignof against B38_RULES §1.3 (the retail create's MemAlloc);
//   - polymorphic, concrete, single inheritance from the proved parent, a
//     virtual destructor (slot_check::DtorSlotOk);
//   - SLOT ORDER, read off pointers to virtual members (Itanium PMF = 1 +
//     vtable byte offset, adjustment 0): every leaf's own slots land on
//     retail's indices, and the leaf-introduced `Construct` is the LAST slot
//     (Description leaves: slot 9, +0x48; direct External leaves: slot 8);
//   - member offsets are static_asserted in each class header.
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "Component/BoatDescription.h"
#include "Component/ClearResourceDescription.h"
#include "Component/CreatePathDescription.h"
#include "Component/HealerDescription.h"
#include "Component/MarketLocationDescription.h"
#include "Component/MineDescription.h"
#include "Component/MultiObjectDescription.h"
#include "Component/StorageDisplayDescription.h"
#include "Component/TrackerDescription.h"
#include "Component/WildAnimalDescription.h"
#include "Component/ZoneDescription.h"
#include "Component/AbsorbResourceDescription.h"
#include "Component/CitizenAIDescription.h"
#include "Component/StateMachineDescription.h"
#include "Component/CharacterDescription.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/CitizenDescription.h"
#include "Component/AttendanceDescription.h"
#include "Component/AutoPickupDescription.h"
#include "Component/CemeteryDescription.h"
#include "Component/TombstoneDescription.h"
#include "Component/PastureDescription.h"
#include "Component/CropFieldDescription.h"
#include "Component/HuntDescription.h"
#include "Component/ForesterDescription.h"
#include "Component/NomadDescription.h"
#include "Component/QuarryDescription.h"
#include "Component/FishingDescription.h"
#include "Component/FloorDescription.h"
#include "Component/LivestockDescription.h"
#include "Component/FruitDescription.h"
#include "Component/TerraformDescription.h"
#include "Component/HappinessDescription.h"
#include "Component/OrchardDescription.h"
#include "Component/HerdDescription.h"
#include "Component/TornadoDescription.h"
#include "Component/DiseaseDescription.h"
#include "Component/StorageLocationDescription.h"
#include "Component/ResidenceDescription.h"
#include "Component/WorkPlaceDescription.h"
#include "Component/FireDescription.h"
#include "Component/GrowthDescription.h"
#include "Component/HerbalistDescription.h"
#include "Component/MarketDescription.h"
#include "Component/PopulationDescription.h"
#include "Component/MapDescription.h"
#include "Component/StatusIconDescription.h"
#include "Component/RadiusDescription.h"
#include "Component/ValueDescription.h"
#include "Component/SpawnDescription.h"
#include "Component/WorkDescription.h"
#include "Component/SelectDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/CreateBridgeDescription.h"
#include "Component/CreateDragDescription.h"
#include "Component/CreateFencedDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/CreateRemovalDescription.h"
#include "Component/AmbientEnvironmentDescription.h"
#include "Component/AnimalDescription.h"
#include "Component/AmbientEmitterDescription.h"
#include "Component/ClearAreaDescription.h"
#include "Component/DestroyDescription.h"
#include "Component/InteractDescription.h"
#include "Component/HighlightDescription.h"
#include "Component/PickingDescription.h"
#include "Component/DecalDescription.h"
#include "Component/ModelDescription.h"
#include "Component/StorageDescription.h"
#include "Component/ParticleDescription.h"
#include "Component/ImmigrationDescription.h"
#include "Component/ConsumeProduceDescription.h"
#include "Component/BuildDescription.h"
#include "Component/TradeDescription.h"
#include "Component/UIDescription.h"
#include "Component/ToolbarDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/EnvironmentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Component/RawMaterialDescription.h"
#include "Component/DiseaseComponent.h"

namespace {

// One Description leaf: 10 retail slots, the leaf's Construct at slot 9.
template <class D>
int CheckDescriptionLeaf(size_t size) {
    int bad = 0;
    bad += sizeof(D) != size;
    bad += alignof(D) != 8;
    bad += !std::is_polymorphic<D>::value;
    bad += std::is_abstract<D>::value;
    bad += !std::has_virtual_destructor<D>::value;
    bad += !(std::is_base_of<Description, D>::value);
    bad += !slot_check::DtorSlotOk<D>();
    bad += !slot_check::SlotOk(&D::TypeName,         0x08/8);
    bad += !slot_check::SlotOk(&D::Type,             0x10/8);
    bad += !slot_check::SlotOk(&D::GetFileVersion,   0x18/8);
    bad += !slot_check::SlotOk(&D::Compile,          0x20/8);
    bad += !slot_check::SlotOk(&D::Serialize,        0x28/8);
    bad += !slot_check::SlotOk(&D::Reconstruct,      0x30/8);
    bad += !slot_check::SlotOk(&D::Destruct,         0x38/8);
    bad += !slot_check::SlotOk(&D::GetComponentType, 0x40/8);
    bad += !slot_check::SlotOk(&D::Construct,        0x48/8);
    return bad;
}

UNIT_TEST(k1_fieldless_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<BoatDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<ClearResourceDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreatePathDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<HealerDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<MarketLocationDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<MineDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<MultiObjectDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<StorageDisplayDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<TrackerDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<WildAnimalDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<ZoneDescription>(0x20), 0);
}

UNIT_TEST(k1_description_leaves_that_now_run_external_dtor) {
    CHECK_EQ(CheckDescriptionLeaf<AbsorbResourceDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<CitizenAIDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<StateMachineDescription>(0x20), 0);
    CHECK_EQ(CheckDescriptionLeaf<CharacterDescription>(0x48), 0);
    CHECK_EQ(CheckDescriptionLeaf<NaturalResourceDescription>(0x88), 0);
    CHECK_EQ(CheckDescriptionLeaf<CitizenDescription>(0x148), 0);
}

UNIT_TEST(k1_scalar_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<AttendanceDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<AutoPickupDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<CemeteryDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<TombstoneDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<PastureDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<CropFieldDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<HuntDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<ForesterDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<NomadDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<QuarryDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<FishingDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<FloorDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<LivestockDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<FruitDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<TerraformDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<HappinessDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<OrchardDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<HerdDescription>(0x40), 0);
    CHECK_EQ(CheckDescriptionLeaf<TornadoDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<DiseaseDescription>(0x48), 0);
    CHECK_EQ(CheckDescriptionLeaf<StorageLocationDescription>(0x28), 0);
    CHECK_EQ(CheckDescriptionLeaf<ResidenceDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<WorkPlaceDescription>(0x30), 0);
}

UNIT_TEST(k1_array_and_string_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<FireDescription>(0x50), 0);
    CHECK_EQ(CheckDescriptionLeaf<GrowthDescription>(0x68), 0);
    CHECK_EQ(CheckDescriptionLeaf<HerbalistDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<MarketDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<PopulationDescription>(0x58), 0);
    CHECK_EQ(CheckDescriptionLeaf<MapDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<StatusIconDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<RadiusDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<ValueDescription>(0x50), 0);
}

// A road's type is a byte (a mod such as MegaMod numbers its roads 0..175; the road tool keys the road
// texture batches by it). The bool reader keeps only byte != 0 (review F-048), so a bool member would
// make every road type 1.
UNIT_TEST(k1_create_road_index_is_a_byte) {
    CHECK((std::is_same<decltype(CreateRoadDescription::_index), uint8_t>::value));
}

UNIT_TEST(k1_tool_and_work_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<SpawnDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<WorkDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<SelectDescription>(0x48), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreateRoadDescription>(0x30), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreateBridgeDescription>(0x60), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreateDragDescription>(0x40), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreateFencedDescription>(0x78), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreatePlacedDescription>(0x58), 0);
    CHECK_EQ(CheckDescriptionLeaf<CreateRemovalDescription>(0x48), 0);
}

UNIT_TEST(k1_ambient_and_building_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<AmbientEnvironmentDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<AnimalDescription>(0x58), 0);
    CHECK_EQ(CheckDescriptionLeaf<AmbientEmitterDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<ClearAreaDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<DestroyDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<InteractDescription>(0x58), 0);
    CHECK_EQ(sizeof(InteractDescription::NodeArray), (size_t)0x10);
}

UNIT_TEST(k1_render_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<HighlightDescription>(0x40), 0);
    CHECK_EQ(CheckDescriptionLeaf<PickingDescription>(0x40), 0);
    CHECK_EQ(CheckDescriptionLeaf<DecalDescription>(0x50), 0);
    CHECK_EQ(CheckDescriptionLeaf<ModelDescription>(0x40), 0);
    CHECK_EQ(CheckDescriptionLeaf<StorageDescription>(0x40), 0);
}

UNIT_TEST(k1_production_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<ParticleDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<ImmigrationDescription>(0x68), 0);
    CHECK_EQ(CheckDescriptionLeaf<ConsumeProduceDescription>(0x58), 0);
    CHECK_EQ(CheckDescriptionLeaf<RawMaterialDescription>(0x60), 0);
    // The field names the retired engine/types PODs gave still address the
    // retail offsets.
}

UNIT_TEST(k1_ui_trade_build_description_leaves) {
    CHECK_EQ(CheckDescriptionLeaf<BuildDescription>(0x38), 0);
    CHECK_EQ(CheckDescriptionLeaf<TradeDescription>(0x70), 0);
    CHECK_EQ(CheckDescriptionLeaf<UIDescription>(0x48), 0);
    CHECK_EQ(CheckDescriptionLeaf<ToolbarDescription>(0x68), 0);
}

// The two DIRECT Object::External leaves: 9 slots, slot 8 = Construct.
template <typename D>
int CheckExternalLeaf(size_t size) {
    int bad = 0;
    bad += sizeof(D) != size;
    bad += alignof(D) != 8;
    bad += !std::is_polymorphic<D>::value;
    bad += std::is_abstract<D>::value;
    bad += !std::has_virtual_destructor<D>::value;
    bad += std::is_base_of<Description, D>::value;
    bad += !(std::is_base_of<Object::External, D>::value);
    bad += !slot_check::DtorSlotOk<D>();
    bad += !slot_check::SlotOk(&D::TypeName,       0x08/8);
    bad += !slot_check::SlotOk(&D::Type,           0x10/8);
    bad += !slot_check::SlotOk(&D::GetFileVersion, 0x18/8);
    bad += !slot_check::SlotOk(&D::Compile,        0x20/8);
    bad += !slot_check::SlotOk(&D::Serialize,      0x28/8);
    bad += !slot_check::SlotOk(&D::Reconstruct,    0x30/8);
    bad += !slot_check::SlotOk(&D::Destruct,       0x38/8);
    bad += !slot_check::SlotOk(&D::Construct,      0x40/8);
    return bad;
}

UNIT_TEST(k1_direct_external_descriptions) {
    CHECK_EQ(CheckExternalLeaf<ComponentDescription>(0x38), 0);
    CHECK_EQ(CheckExternalLeaf<EnvironmentDescription>(0x1b8), 0);
}

// ComponentEntity: its OWN 4-slot table (D9 #2); the old 12-qword array also
// carried Action's 7 slots.
UNIT_TEST(k1_component_entity) {
    using E = ComponentEntity;
    CHECK_EQ(alignof(E), (size_t)8);
    CHECK(std::is_polymorphic<E>::value);
    CHECK(!std::is_abstract<E>::value);
    CHECK(std::has_virtual_destructor<E>::value);
    CHECK((std::is_base_of<Object::Internal, E>::value));
    CHECK_DTOR_SLOT(E);
    CHECK_SLOT(E, TypeName, 0x08/8);
    CHECK_SLOT(E, Type,     0x10/8);
    CHECK_SLOT(E, Persist,  0x18/8);
}

}  // namespace
