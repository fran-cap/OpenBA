#pragma once
// ============================================================================
// src/game/Component/TombstoneComponent.h — ENDGAME B.3.8 block CIT
//
// `TombstoneComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "TombstoneComponent", parent "Component", ti 0x1401c4a30
//   create 0x14000d280: MemAlloc(0x30, 8) + ctor 0x1400579a0
//   ctor   0x1400579a0: Component ctor, stamp 0x14015a980, +0x28 = 0, flags |= 0x88
//   dtor   0x140057960: stamp 0x14015a980, stamp 0x140162920, ~Internal (no own work)
//   table  0x14015a980, 16 slots:
//     [0] 0x14000d480 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x1400578e0 Persist                [4] 0x140057860 Startup (lifetime roll)
//     [9] 0x1400577d0 Update (countdown)     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time

class TombstoneComponent final : public Component {
    DECLARE_EXE_TYPE(TombstoneComponent)

public:
    TombstoneComponent();                                         // 0x1400579a0

    // 0x140057960
    ~TombstoneComponent() override = default;                     // [0] 0x14000d480
    void Persist(IO::Stream& stream) override;                          // [3] 0x1400578e0
    void Startup() override;                                      // [4] 0x140057860
    void Update(const System::Time* time) override;                             // [9] 0x1400577d0

    // PUBLIC while the ported bodies read them (B38 decision 1).
    System::Time lifetime;   // +0x28 the countdown (lifetime remaining; Persist 0x1400578e0)
};

static_assert(sizeof(TombstoneComponent) == 0x30, "TombstoneComponent is 0x30 (create 0x14000d280)");
static_assert(alignof(TombstoneComponent) == 8, "TombstoneComponent alignment");
static_assert(offsetof(TombstoneComponent, lifetime) == 0x28, "TombstoneComponent +0x28 timer");
