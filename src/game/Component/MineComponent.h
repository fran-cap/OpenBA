#pragma once
// ============================================================================
// src/game/Component/MineComponent.h — ENDGAME B.3.8 block VIS
//
// `MineComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "MineComponent", parent "Component", ti 0x1401c45d0
//   create 0x140008dd0: MemAlloc(0x28, 8) + ctor 0x14004f2e0
//   ctor   0x14004f2e0: Component ctor, stamp 0x140159ed0, no own store (flags OR 0)
//   dtor   0x14004f2a0: stamp 0x140159ed0, stamp 0x140162920, ~Internal (no own work)
//   table  0x140159ed0, 16 slots:
//     [0] 0x140008fa0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x14004f260 Persist                [6] 0x14004f250 own `RET`
//     [7] 0x14004f240 own `RET`              every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class MineComponent final : public Component {
    DECLARE_EXE_TYPE(MineComponent)

public:
    MineComponent();                                              // 0x14004f2e0

    // 0x14004f2a0
    ~MineComponent() override = default;                          // [0] 0x140008fa0
    void Persist(IO::Stream& stream) override;                          // [3] 0x14004f260
    void OnAdded() override;                                      // [6] 0x14004f250 own RET
    void OnRemove() override;                                     // [7] 0x14004f240 own RET
};

static_assert(sizeof(MineComponent) == 0x28, "MineComponent is 0x28 (create 0x140008dd0)");
static_assert(alignof(MineComponent) == 8, "MineComponent alignment");
