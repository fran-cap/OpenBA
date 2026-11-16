#pragma once
// ============================================================================
// src/game/Component/WildAnimalComponent.h — ENDGAME B.3.8 block CIT
//
// `WildAnimalComponent` (a wild animal's herd back-ref) as a real C++ class with
// a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "WildAnimalComponent", parent "Component", ti 0x1401c8000
//   create 0x140039c20: MemAlloc(0x30, 8) + ctor 0x14012e3c0
//   ctor   0x14012e3c0: Component ctor, stamp 0x140164af0, +0x28 = 0,
//                       flags |= 0x02
//   dtor   0x140039df0 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140164af0, 16 slots:
//     [0] 0x140039df0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x14012e300 Persist                [7] 0x14012e2b0 OnRemove
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class HerdComponent;   // Component/HerdComponent.h

class WildAnimalComponent final : public Component {
    DECLARE_EXE_TYPE(WildAnimalComponent)

public:
    WildAnimalComponent();                                        // 0x14012e3c0

    // no own work
    ~WildAnimalComponent() override = default;                    // [0] 0x140039df0
    void Persist(IO::Stream& stream) override;                          // [3] 0x14012e300
    void OnRemove() override;                                     // [7] 0x14012e2b0
    void SetHerd(HerdComponent* const* herd);                     // 0x14012e510 (body: herdcomponent.cpp)

    // PUBLIC while the ported bodies read them (B38 decision 1).
    Object::InternalPtr<HerdComponent> herd;   // +0x28 the herd this animal belongs to (Persist
                                               //       0x14012e300: InternalBasePtr + IsDerivedFrom HerdComponent)
};

static_assert(sizeof(WildAnimalComponent) == 0x30, "WildAnimalComponent is 0x30 (create 0x140039c20)");
static_assert(alignof(WildAnimalComponent) == 8, "WildAnimalComponent alignment");
static_assert(offsetof(WildAnimalComponent, herd) == 0x28, "WildAnimalComponent +0x28 (ctor 0x14012e3c0)");
