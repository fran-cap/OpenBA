#pragma once
// ============================================================================
// src/game/Action/AnimateNaturalResource.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `AnimateNaturalResource`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AnimateNaturalResource", parent "Action", ti 0x1401c5de0
//   create 0x140021dc0: MemAlloc(0x30, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fb10, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class ComponentEntity;   // Entity/ComponentEntity.h

class AnimateNaturalResource final : public Action {
    DECLARE_EXE_TYPE(AnimateNaturalResource)

public:
    AnimateNaturalResource();                                                   // create 0x140021dc0
    AnimateNaturalResource(ComponentEntity* const* entity, uint32_t animIndex, bool waitForAnim);   // 0x1400c57d0
    ~AnimateNaturalResource() override = default;              // [0] 0x140021e30
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c5660
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c5600
    int  Execute(ActionStack* actionStack, const System::Time* time) override;      // [5] 0x1400c55b0
    void CheckEntity() override;                               // [8] 0x1400c55a0 own `RET 0` (same bytes as the base body)
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c5560

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<ComponentEntity> resourceEntity;   // +0x20 the natural-resource entity (Persist checks ComponentEntity)
    int32_t   animIndex;         // +0x28 animation index
    bool      waitForAnim;       // +0x2C wait for the animation (Persist 0x1400c5707: the bool overload)
    uint8_t   _pad2D[3];         // +0x2D padding
};

static_assert(sizeof(AnimateNaturalResource) == 0x30, "AnimateNaturalResource is 0x30 (create 0x140021dc0 MemAlloc)");
static_assert(offsetof(AnimateNaturalResource, _pad2D) == 0x2D, "AnimateNaturalResource::_pad2D");
static_assert(alignof(AnimateNaturalResource) == 8, "AnimateNaturalResource is 8-aligned");
static_assert(offsetof(AnimateNaturalResource, resourceEntity) == 0x20, "AnimateNaturalResource +0x20");
static_assert(offsetof(AnimateNaturalResource, animIndex) == 0x28, "AnimateNaturalResource +0x28");
static_assert(offsetof(AnimateNaturalResource, waitForAnim) == 0x2c, "AnimateNaturalResource +0x2c");
