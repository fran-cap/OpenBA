#pragma once
// ============================================================================
// src/game/Action/RemoveNaturalResource.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `RemoveNaturalResource`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "RemoveNaturalResource", parent "Action", ti 0x1401c60b8
//   create 0x140022f70: MemAlloc(0x38, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x140160050, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;   // Component/CitizenComponent.h
class ComponentEntity;    // Entity/ComponentEntity.h

class RemoveNaturalResource final : public Action {
    DECLARE_EXE_TYPE(RemoveNaturalResource)

public:
    RemoveNaturalResource();                                                   // create 0x140022f70
    RemoveNaturalResource(ComponentEntity* const* resource, CitizenComponent* const* citizen,
                          bool flag);   // 0x1400cadf0
    ~RemoveNaturalResource() override = default;               // [0] 0x140022fe0
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400cac50
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400cab10
    void CheckEntity() override;                               // [8] 0x1400caaa0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400caa20

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<CitizenComponent> _citizen;   // +0x20 the working citizen (Persist checks CitizenComponent)
    Object::InternalPtr<ComponentEntity>  _resource;   // +0x28 the natural-resource entity (Persist checks ComponentEntity)
    bool      _harvestFlag;          // +0x30 harvest flag (Persist: the bool overload)
    uint8_t   _pad31[7];         // +0x31 padding
};

static_assert(sizeof(RemoveNaturalResource) == 0x38, "RemoveNaturalResource is 0x38 (create 0x140022f70 MemAlloc)");
static_assert(offsetof(RemoveNaturalResource, _pad31) == 0x31, "RemoveNaturalResource::_pad31");
static_assert(alignof(RemoveNaturalResource) == 8, "RemoveNaturalResource is 8-aligned");
static_assert(offsetof(RemoveNaturalResource, _citizen) == 0x20, "RemoveNaturalResource +0x20");
static_assert(offsetof(RemoveNaturalResource, _resource) == 0x28, "RemoveNaturalResource +0x28");
static_assert(offsetof(RemoveNaturalResource, _harvestFlag) == 0x30, "RemoveNaturalResource +0x30");
