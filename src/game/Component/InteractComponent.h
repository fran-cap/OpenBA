#pragma once
// ============================================================================
// src/game/Component/InteractComponent.h — ENDGAME B.3.8 block VIS
//
// `InteractComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "InteractComponent", parent "Component", ti 0x1401c7900
//   create 0x1400351a0: MemAlloc(0x38, 8) + ctor 0x140123480
//   ctor   0x140123480: Component ctor, stamp 0x1401638b0, +0x28 = +0x30 = 0,
//                       flags |= 1 (0x81)
//   dtor   0x140123420: MemFree(+0x28), +0x28 = +0x30 = 0 (~useLocations, the
//                       inlined ~Array), restamp, ~Internal
//   table  0x1401638b0, 16 slots:
//     [0] 0x1400352f0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140123340 Persist                [6] 0x140123110 OnAdded (Ghidra "Draw")
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (useLocations)
#include "object/Internal.h"         // Object::InternalPtr<T>
#include "Component/InteractDescription.h"   // InteractDescription::NodeArray (FindWalkablePos), Math::Vector3

// The use-location list grows through retail's stride-8 row (0x1400032e0: stride 8,
// align 8, the new tail ZEROED, no element destroy) and is freed by the inlined ~Array
// (dtor 0x140123440..0x140123450): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class InteractComponent final : public Component {
    DECLARE_EXE_TYPE(InteractComponent)

public:
    InteractComponent();                                          // 0x140123480

    // 0x140123420: the body is empty; ~useLocations frees and zeroes +0x28 / +0x30.
    ~InteractComponent() override = default;                      // [0] 0x1400352f0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140123340
    void OnAdded() override;                                      // [6] 0x140123110
    int  NodeCount(int kind) const;                               // 0x140123070 nodes of `kind` here and in the parts
    Math::Vector3* NodePosition(Math::Vector3* out, int kind, int index, float* heading) const;   // 0x140122e20 the index-th node of `kind`, world space
    bool FindWalkablePos(const InteractDescription::NodeArray* nodes, float* param_3, float* param_4, int* param_5);   // 0x140122be0
    uint8_t FindWalkablePosRecursive(int param_2, float* param_3, float* param_4, int* param_5);   // 0x140122b00

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x140123340 (int count +0x30 with the +0x28 IBP array, each validated against
    // InteractComponent 0x1401c7900) and OnAdded 0x140123110.
    using UseLocationList = Collection::Array<Object::InternalPtr<InteractComponent>, Collection::DefaultAllocator,
                                              Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    UseLocationList useLocations;   // +0x28 the parts' InteractComponents (count +0x30, capacity +0x34)
};

static_assert(sizeof(InteractComponent) == 0x38, "InteractComponent is 0x38 (create 0x1400351a0)");
static_assert(alignof(InteractComponent) == 8, "InteractComponent alignment");
static_assert(offsetof(InteractComponent, useLocations) == 0x28, "");
static_assert(offsetof(InteractComponent, useLocations) + offsetof(InteractComponent::UseLocationList, _size) == 0x30, "");
static_assert(offsetof(InteractComponent, useLocations) + offsetof(InteractComponent::UseLocationList, _capacity) == 0x34, "");
