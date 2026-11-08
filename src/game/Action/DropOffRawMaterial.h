#pragma once
// ============================================================================
// src/game/Action/DropOffRawMaterial.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `DropOffRawMaterial`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "DropOffRawMaterial", parent "Action", ti 0x1401c5f30
//   create 0x140022600: MemAlloc(0x40, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fd80, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;       // Component/CitizenComponent.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h
class StorageComponent;       // Component/StorageComponent.h

class DropOffRawMaterial final : public Action {
    DECLARE_EXE_TYPE(DropOffRawMaterial)

public:
    // The ctor's `count` (0x1400c7cd0, stored at _maxCount) that drops every unit carried.
    static constexpr uint32_t kAllUnits = 0xffffffffu;

    DropOffRawMaterial();                                                   // create 0x140022600
    DropOffRawMaterial(CitizenComponent* const* citizen, ComponentEntity* const* destination,
                       ComponentDescription* material, uint32_t count, bool flag);   // 0x1400c7cd0
    ~DropOffRawMaterial() override = default;                  // [0] 0x140022670
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c7b10
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c7300
    void CheckEntity() override;                               // [8] 0x1400c72f0 (JMP 0x1400c7570)
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c7270

    // Bodies in action_pf_roots_drop.cpp.
    void     SpillAllStorage();                                    // 0x1400c7570
    uint64_t Deposit(ActionStack* stack, StorageComponent* const* target,
                     ComponentDescription* material);              // 0x1400c7620
    void     DropOnGround(ComponentDescription* material, int count);   // 0x1400c78c0

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    bool      _searchOtherStorage;          // +0x20 search for another storage when full (Persist: the bool overload)
    uint8_t   _pad21[3];         // +0x21 padding
    int32_t   _maxCount;          // +0x24 count cap (-1 = all)
    ComponentDescription* _material;   // +0x28 ExternalPtr<ComponentDescription>: the material (null = every stack)
    Object::InternalPtr<CitizenComponent> _citizen;   // +0x30 the carrying citizen (Persist checks CitizenComponent)
    Object::InternalPtr<ComponentEntity>  _destination;   // +0x38 the destination entity (Persist checks ComponentEntity)
};

static_assert(sizeof(DropOffRawMaterial) == 0x40, "DropOffRawMaterial is 0x40 (create 0x140022600 MemAlloc)");
static_assert(offsetof(DropOffRawMaterial, _pad21) == 0x21, "DropOffRawMaterial::_pad21");
static_assert(alignof(DropOffRawMaterial) == 8, "DropOffRawMaterial is 8-aligned");
static_assert(offsetof(DropOffRawMaterial, _searchOtherStorage) == 0x20, "DropOffRawMaterial +0x20");
static_assert(offsetof(DropOffRawMaterial, _maxCount) == 0x24, "DropOffRawMaterial +0x24");
static_assert(offsetof(DropOffRawMaterial, _material) == 0x28, "DropOffRawMaterial +0x28");
static_assert(offsetof(DropOffRawMaterial, _citizen) == 0x30, "DropOffRawMaterial +0x30");
static_assert(offsetof(DropOffRawMaterial, _destination) == 0x38, "DropOffRawMaterial +0x38");
