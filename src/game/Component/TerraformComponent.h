#pragma once
// ============================================================================
// src/game/Component/TerraformComponent.h — ENDGAME B.3.8 block VIS
//
// `TerraformComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "TerraformComponent", parent "Component", ti 0x1401c49c0
//   create 0x14000cc90: MemAlloc(0x48, 8) + ctor 0x140057620
//   ctor   0x140057620: Component ctor, stamp 0x14015a880, +0x28/+0x30/+0x38/
//                       +0x40 = 0, flags |= 3 (0x83)
//   dtor   0x1400575b0: MemFree(+0x38), +0x38 = +0x40 = 0, MemFree(+0x28),
//                       +0x28 = +0x30 = 0, restamp, ~Internal: ~tileRecord then
//                       ~heightRecord (reverse declaration order), both inlined ~Array
//   table  0x14015a880, 16 slots:
//     [0]  0x14000ce90 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x140057500 Persist (the undo record)
//     [6]  0x1400574f0 own `RET`             [7] 0x140057350 OnRemove (replay the undo)
//     [12] 0x140057310 OnEvent (msg 4: apply the deformation)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "math/Math.h"   // Math::Vector3
#include "collection/Collection.h"   // Collection::Array (the two undo records)

class TerraformComponent final : public Component {
    DECLARE_EXE_TYPE(TerraformComponent)

public:
    TerraformComponent();                                         // 0x140057620

    // 0x1400575b0: the body is empty; ~tileRecord (+0x38) then ~heightRecord (+0x28)
    // free and zero the two undo arrays in retail's order (reverse declaration order).
    ~TerraformComponent() override = default;                     // [0]  0x14000ce90
    void Persist(IO::Stream& stream) override;                          // [3]  0x140057500
    void OnAdded() override;                                      // [6]  0x1400574f0 own RET
    void OnRemove() override;                                     // [7]  0x140057350
    void OnEvent(int eventId) override;                           // [12] 0x140057310

    void ApplyMesh(bool ceiling);                                 // 0x140056b50 one heightmap pass over a description mesh
    void ApplyFootprint();                                        // 0x140057120 footprint cells type 10 -> 6

    // PUBLIC while the ported bodies read them (B38 decision 1).
    // Math::Vector3's ctor zeroes (0x180015070): the load resize 0x14000d030 zeroes its
    // new tail, as the stride-12 row the bridge bound did.
    Collection::Array<Math::Vector3> heightRecord; // +0x28 original terrain points (count +0x30, capacity +0x34)
    Collection::Array<int32_t>       tileRecord;   // +0x38 retyped map cell indices (count +0x40, capacity +0x44)
};

static_assert(sizeof(TerraformComponent) == 0x48, "TerraformComponent is 0x48 (create 0x14000cc90)");
static_assert(alignof(TerraformComponent) == 8, "TerraformComponent alignment");
static_assert(offsetof(TerraformComponent, heightRecord) == 0x28, "");
static_assert(offsetof(TerraformComponent, heightRecord) + offsetof(Collection::Array<Math::Vector3>, _size) == 0x30, "");
static_assert(offsetof(TerraformComponent, heightRecord) + offsetof(Collection::Array<Math::Vector3>, _capacity) == 0x34, "");
static_assert(offsetof(TerraformComponent, tileRecord) == 0x38, "");
static_assert(offsetof(TerraformComponent, tileRecord) + offsetof(Collection::Array<int32_t>, _size) == 0x40, "");
static_assert(offsetof(TerraformComponent, tileRecord) + offsetof(Collection::Array<int32_t>, _capacity) == 0x44, "");
