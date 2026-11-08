#pragma once
// ============================================================================
// src/game/Action/RemoveAnimal.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `RemoveAnimal`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "RemoveAnimal", parent "Action", ti 0x1401c6080
//   create 0x140022e70: MemAlloc(0x28, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015ffe0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class ComponentEntity;   // Entity/ComponentEntity.h

class RemoveAnimal final : public Action {
    DECLARE_EXE_TYPE(RemoveAnimal)

public:
    RemoveAnimal();                                                   // create 0x140022e70
    explicit RemoveAnimal(ComponentEntity* animal);   // inlined in 0x1400bbea0 (action_pf_roots_drop.cpp)
    ~RemoveAnimal() override = default;                        // [0] 0x140022ee0
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400ca870
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400ca7f0
    void CheckEntity() override;                               // [8] 0x1400ca760
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400ca720

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<ComponentEntity> _animal;   // +0x20 the animal entity
};

static_assert(sizeof(RemoveAnimal) == 0x28, "RemoveAnimal is 0x28 (create 0x140022e70 MemAlloc)");
static_assert(alignof(RemoveAnimal) == 8, "RemoveAnimal is 8-aligned");
static_assert(offsetof(RemoveAnimal, _animal) == 0x20, "RemoveAnimal +0x20");
