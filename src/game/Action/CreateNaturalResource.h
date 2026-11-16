#pragma once
// ============================================================================
// src/game/Action/CreateNaturalResource.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `CreateNaturalResource`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateNaturalResource", parent "Action", ti 0x1401c5ec0
//   create 0x1400221d0: MemAlloc(0x38, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fca0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h

class CreateNaturalResource final : public Action {
    DECLARE_EXE_TYPE(CreateNaturalResource)

public:
    CreateNaturalResource();                                                   // create 0x1400221d0
    CreateNaturalResource(ComponentDescription* desc, ComponentEntity* const* parent,
                          uint32_t cellIndex);                                 // 0x1400c6a20
    ~CreateNaturalResource() override = default;               // [0] 0x140022240
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c6890
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c6650
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c6610

    // ---- fields, retail order (PUBLIC) ----
    ComponentDescription* descEntity;   // +0x20 ExternalPtr<ComponentDescription>: the resource template
    Object::InternalPtr<ComponentEntity> parentEntity;   // +0x28 parent entity (Persist checks ComponentEntity)
    int32_t   cellIndex;         // +0x30 map cell index / resource slot
    uint8_t   _pad34[4];         // +0x34 padding
};

static_assert(sizeof(CreateNaturalResource) == 0x38, "CreateNaturalResource is 0x38 (create 0x1400221d0 MemAlloc)");
static_assert(offsetof(CreateNaturalResource, _pad34) == 0x34, "CreateNaturalResource::_pad34");
static_assert(alignof(CreateNaturalResource) == 8, "CreateNaturalResource is 8-aligned");
static_assert(offsetof(CreateNaturalResource, descEntity) == 0x20, "CreateNaturalResource +0x20");
static_assert(offsetof(CreateNaturalResource, parentEntity) == 0x28, "CreateNaturalResource +0x28");
static_assert(offsetof(CreateNaturalResource, cellIndex) == 0x30, "CreateNaturalResource +0x30");
