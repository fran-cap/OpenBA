#pragma once
// ============================================================================
// src/game/Component/FireComponent.h — ENDGAME B.3.8 block CIT
//
// `FireComponent` (a building's fire: ignition roll, burn, spread, buckets) as a
// real C++ class with a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "FireComponent", parent "Component", ti 0x1401c41e0
//   create 0x1400055e0: MemAlloc(0x48, 8) + ctor 0x140049b80
//   ctor   0x140049b80: Component ctor, stamp 0x140159380, +0x28..+0x40 = 0,
//                       flags |= 0x08
//   dtor   0x140005720 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140159380, 16 slots (FireDescription's table starts at 0x140159400):
//     [0] 0x140005720 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140049a90 Persist                [9] 0x140048fb0 Update
//     [15] 0x140049090 OnMessage (water bucket applied — SendComponentMessage)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time

class FireDescription;   // Component/FireDescription.h

class FireComponent final : public Component {
    DECLARE_EXE_TYPE(FireComponent)

public:
    FireComponent();                                              // 0x140049b80

    // no own work
    ~FireComponent() override = default;                          // [0]  0x140005720
    void Persist(IO::Stream& stream) override;                          // [3]  0x140049a90
    void Update(const System::Time* time) override;                             // [9]  0x140048fb0
    void OnMessage(int message, void* data,
                   uint32_t arg0, uint32_t arg1) override;        // [15] 0x140049090

    // ---- non-virtual members (retail addresses; RCX = this) ----
    void Ignite();                                                // 0x140049610
    void SpreadToNeighbors();                                     // 0x140049130
    void Extinguish();                                            // 0x1400494c0
    void MarkDanger(short delta);                                 // 0x1400499c0

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist 0x140049a90:
    // Time::Serialize +0x30 / +0x38, Serialize(int&) +0x28 and (version > 0) +0x2c.
    int32_t  bucketsApplied;    // +0x28 water buckets delivered since ignition
    int32_t  bucketsAtSpread;   // +0x2c snapshot of +0x28 at the last spread pass
    System::Time fireTimer;     // +0x30 burning while > 0
    System::Time spreadTimer;   // +0x38 next spread pass when <= 0
    System::Time igniteAccum;   // +0x40 idle accumulator for the roll
};

static_assert(sizeof(FireComponent) == 0x48, "FireComponent is 0x48 (create 0x1400055e0)");
static_assert(alignof(FireComponent) == 8, "FireComponent alignment");
static_assert(offsetof(FireComponent, bucketsApplied)  == 0x28, "");
static_assert(offsetof(FireComponent, bucketsAtSpread) == 0x2c, "");
static_assert(offsetof(FireComponent, fireTimer)       == 0x30, "");
static_assert(offsetof(FireComponent, spreadTimer)     == 0x38, "");
static_assert(offsetof(FireComponent, igniteAccum)     == 0x40, "");
