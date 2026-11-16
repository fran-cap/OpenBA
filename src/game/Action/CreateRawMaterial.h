#pragma once
// ============================================================================
// src/game/Action/CreateRawMaterial.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `CreateRawMaterial`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateRawMaterial", parent "Action", ti 0x1401c5ef8
//   create 0x1400222d0: MemAlloc(0x58, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fd10, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;       // Component/CitizenComponent.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h

class CreateRawMaterial final : public Action {
    DECLARE_EXE_TYPE(CreateRawMaterial)

public:
    CreateRawMaterial();                                                   // create 0x1400222d0
    CreateRawMaterial(ComponentDescription* desc, float yield, CitizenComponent* const* source,
                      ComponentEntity* const* secondary, uint64_t unused6, uint32_t quantity,
                      uint32_t cellIdx, bool naturalRes, ComponentEntity* const* tertiary,
                      uint32_t placementFlags);   // 0x1400c71a0
    ~CreateRawMaterial() override = default;                   // [0] 0x140022350
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c6f30
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c6b40
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c6aa0

    // ---- fields, retail order (PUBLIC) ----
    ComponentDescription* descEntity;   // +0x20 ExternalPtr<ComponentDescription>: the material template
    // +0x28: the WORKING CITIZEN, not a building (Persist checks CitizenComponent; AbstractImpl
    // compares its education with its description's maxEducation). The name is the tree's.
    Object::InternalPtr<CitizenComponent> sourceBuilding;
    Object::InternalPtr<ComponentEntity>  secondaryEntity;   // +0x30 optional secondary entity
    Object::InternalPtr<ComponentEntity>  tertiaryEntity;    // +0x38 optional tertiary entity
    int32_t   totalQuantity;     // +0x40 total quantity to produce
    int32_t   materialCellIdx;   // +0x44 material type index / cell index
    uint32_t  placementFlags;    // +0x48 placement flags
    float     yieldMultiplier;   // +0x4C yield multiplier
    bool      naturalResFlag;    // +0x50 natural resource flag (Persist: the bool overload)
    bool      placeNearFlag;     // +0x51 place near flag (Persist: the bool overload)
    uint8_t   _pad52[6];         // +0x52 padding
};

static_assert(sizeof(CreateRawMaterial) == 0x58, "CreateRawMaterial is 0x58 (create 0x1400222d0 MemAlloc)");
static_assert(offsetof(CreateRawMaterial, _pad52) == 0x52, "CreateRawMaterial::_pad52");
static_assert(alignof(CreateRawMaterial) == 8, "CreateRawMaterial is 8-aligned");
static_assert(offsetof(CreateRawMaterial, descEntity) == 0x20, "CreateRawMaterial +0x20");
static_assert(offsetof(CreateRawMaterial, sourceBuilding) == 0x28, "CreateRawMaterial +0x28");
static_assert(offsetof(CreateRawMaterial, secondaryEntity) == 0x30, "CreateRawMaterial +0x30");
static_assert(offsetof(CreateRawMaterial, tertiaryEntity) == 0x38, "CreateRawMaterial +0x38");
static_assert(offsetof(CreateRawMaterial, totalQuantity) == 0x40, "CreateRawMaterial +0x40");
static_assert(offsetof(CreateRawMaterial, materialCellIdx) == 0x44, "CreateRawMaterial +0x44");
static_assert(offsetof(CreateRawMaterial, placementFlags) == 0x48, "CreateRawMaterial +0x48");
static_assert(offsetof(CreateRawMaterial, yieldMultiplier) == 0x4c, "CreateRawMaterial +0x4c");
static_assert(offsetof(CreateRawMaterial, naturalResFlag) == 0x50, "CreateRawMaterial +0x50");
static_assert(offsetof(CreateRawMaterial, placeNearFlag) == 0x51, "CreateRawMaterial +0x51");
