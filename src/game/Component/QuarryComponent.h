#pragma once
// ============================================================================
// src/game/Component/QuarryComponent.h — ENDGAME B.3.8 block VIS
//
// `QuarryComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "QuarryComponent", parent "Component", ti 0x1401c4800
//   create 0x14000b3d0: MemAlloc(0x28, 8) + ctor 0x1400552b0
//   ctor   0x1400552b0: Component ctor, stamp 0x14015a490, flags |= 3 (0x83)
//   dtor   0x140055270: stamp 0x14015a490, stamp 0x140162920, ~Internal (no own work)
//   table  0x14015a490, 16 slots:
//     [0]  0x14000b5d0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x140055230 Persist               [6] 0x140055220 own `RET`
//     [7]  0x140055210 own `RET`             [12] 0x1400550f0 message sink (carve)
//     [15] 0x140054b80 message sink (progressive carve)
//     every other slot = Component's
// The [12]/[15] bodies are QuarryComponent::OnEvent / OnMessage in
// quarrycomponent_deform.cpp.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class QuarryComponent final : public Component {
    DECLARE_EXE_TYPE(QuarryComponent)

public:
    QuarryComponent();                                            // 0x1400552b0

    // 0x140055270
    ~QuarryComponent() override = default;                        // [0]  0x14000b5d0
    void Persist(IO::Stream& stream) override;                          // [3]  0x140055230
    void OnAdded() override;                                      // [6]  0x140055220 own RET
    void OnRemove() override;                                     // [7]  0x140055210 own RET
    void OnEvent(int eventId) override;                           // [12] 0x1400550f0
    void OnMessage(int message, void* data,
                   uint32_t arg0, uint32_t arg1) override;        // [15] 0x140054b80

    // 0x1400549c0: carve the description's floor mesh into the heightmap, clamped to `depth`.
    void ApplyDepth(float depth);
};

static_assert(sizeof(QuarryComponent) == 0x28, "QuarryComponent is 0x28 (create 0x14000b3d0)");
static_assert(alignof(QuarryComponent) == 8, "QuarryComponent alignment");
