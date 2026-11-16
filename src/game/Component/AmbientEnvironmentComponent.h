#pragma once
// ============================================================================
// src/game/Component/AmbientEnvironmentComponent.h — ENDGAME B.3.8 block CIT
//
// `AmbientEnvironmentComponent` as a real C++ class with a compiler-generated
// vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AmbientEnvironmentComponent", parent "Component", ti 0x1401c3ed0
//   create 0x140001ee0: MemAlloc(0x48, 8) + ctor 0x1400438d0
//   ctor   0x1400438d0: Component ctor, stamp 0x140158b70, +0x38 = 0,
//                       +0x40 (dword) = 0, flags |= 0x88. +0x28/+0x30 are NOT
//                       written (the tree zeroes them; so does this ctor)
//   dtor   0x140002170 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140158b70, 16 slots:
//     [0] 0x140002170 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x1400437e0 Persist                [9] 0x140043690 Update (JMP 0x140043310)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "math/Math.h"

class AmbientEnvironmentComponent final : public Component {
    DECLARE_EXE_TYPE(AmbientEnvironmentComponent)

public:
    AmbientEnvironmentComponent();                                // 0x1400438d0

    // Retail's dtor does no own work; ours also drops this component's AMBMEMO entry
    // (NOT RETAIL: the side table in ambientenvironmentcomponent.cpp, no field here).
    ~AmbientEnvironmentComponent() override;                      // [0] 0x140002170
    void Persist(IO::Stream& stream) override;                          // [3] 0x1400437e0
    void Update(const System::Time* time) override;                             // [9] 0x140043690

    // ---- non-virtual members (retail addresses) ----
    // The region/position half of 0x1400436a0: the grid region the biome is
    // sampled over, the region's centre, pushed onto the sibling emitter.
    void InitRegion(int gridX, int gridY, int width, int height);

    // AMBMEMO (NOT RETAIL): one stderr line with the memo's hit / miss / shadow totals
    // at world teardown, when the stress profile ran (docs/extensions/handins/AMBMEMO.md).
    static void ReportMemo();

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist
    // 0x1400437e0: Vector3::Serialize(+0x38), then four ints +0x28..+0x34.
    int32_t       regionX;        // +0x28 not written by the retail ctor
    int32_t       regionY;        // +0x2C
    int32_t       regionWidth;    // +0x30 not written by the retail ctor
    int32_t       regionHeight;   // +0x34
    Math::Vector3 position;       // +0x38 the region's centre
    uint8_t       _pad44[4];      // +0x44
};

static_assert(sizeof(AmbientEnvironmentComponent) == 0x48, "AmbientEnvironmentComponent is 0x48 (create 0x140001ee0)");
static_assert(offsetof(AmbientEnvironmentComponent, _pad44) == 0x44, "AmbientEnvironmentComponent::_pad44");
static_assert(alignof(AmbientEnvironmentComponent) == 8, "AmbientEnvironmentComponent alignment");
static_assert(offsetof(AmbientEnvironmentComponent, regionX) == 0x28, "AmbientEnvironmentComponent::regionX @ +0x28 (Serialize int&)");
static_assert(offsetof(AmbientEnvironmentComponent, regionY) == 0x2C, "AmbientEnvironmentComponent::regionY @ +0x2C (Serialize int&)");
static_assert(offsetof(AmbientEnvironmentComponent, regionWidth) == 0x30, "AmbientEnvironmentComponent::regionWidth @ +0x30 (Serialize int&)");
static_assert(offsetof(AmbientEnvironmentComponent, regionHeight) == 0x34, "AmbientEnvironmentComponent::regionHeight @ +0x34 (Serialize int&)");
static_assert(offsetof(AmbientEnvironmentComponent, position) == 0x38, "AmbientEnvironmentComponent::position @ +0x38 (Vector3::Serialize)");
