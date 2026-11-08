#pragma once
// ============================================================================
// src/game/Action/PickupRawMaterial.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `PickupRawMaterial`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "PickupRawMaterial", parent "Action", ti 0x1401c6010
//   create 0x140022c60: MemAlloc(0x30, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015ff10, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;   // Component/CitizenComponent.h
class ComponentEntity;    // Entity/ComponentEntity.h

class PickupRawMaterial final : public Action {
    DECLARE_EXE_TYPE(PickupRawMaterial)

public:
    PickupRawMaterial();                                                   // create 0x140022c60
    PickupRawMaterial(CitizenComponent* const* citizen, ComponentEntity* const* pile);   // 0x1400c9b70 (body in Work/work_base_actionctor.cpp)

    ~PickupRawMaterial() override = default;                   // [0] 0x140022cd0
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c99e0
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c98d0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c9860

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<CitizenComponent> _citizen;   // +0x20 the carrying citizen (Persist checks CitizenComponent)
    Object::InternalPtr<ComponentEntity>  _pile;   // +0x28 the raw-material pile (Persist checks ComponentEntity)
};

static_assert(sizeof(PickupRawMaterial) == 0x30, "PickupRawMaterial is 0x30 (create 0x140022c60 MemAlloc)");
static_assert(alignof(PickupRawMaterial) == 8, "PickupRawMaterial is 8-aligned");
static_assert(offsetof(PickupRawMaterial, _citizen) == 0x20, "PickupRawMaterial +0x20");
static_assert(offsetof(PickupRawMaterial, _pile) == 0x28, "PickupRawMaterial +0x28");
