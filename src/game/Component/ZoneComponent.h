#pragma once
// ============================================================================
// src/game/Component/ZoneComponent.h — ENDGAME B.3.8 block VIS
//
// `ZoneComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ZoneComponent", parent "Component", ti 0x1401c8150
//   create 0x14003a6a0: MemAlloc(0x28, 8) + ctor 0x1401303c0
//   ctor   0x1401303c0: Component ctor, stamp 0x140164e30, flags |= 3 (0x83)
//   dtor   0x140130380: stamp 0x140164e30, stamp 0x140162920, ~Internal (no own work)
//   table  0x140164e30, 16 slots:
//     [0] 0x14003a870 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x1401302f0 Persist                [6] 0x1401301f0 OnAdded (Ghidra "Draw")
//     [7] 0x140130080 OnRemove (Ghidra "OnCreate")   every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class ZoneComponent final : public Component {
    DECLARE_EXE_TYPE(ZoneComponent)

public:
    ZoneComponent();                                              // 0x1401303c0

    // 0x140130380
    ~ZoneComponent() override = default;                          // [0] 0x14003a870
    void Persist(IO::Stream& stream) override;                          // [3] 0x1401302f0
    void OnAdded() override;                                      // [6] 0x1401301f0
    void OnRemove() override;                                     // [7] 0x140130080
};

static_assert(sizeof(ZoneComponent) == 0x28, "ZoneComponent is 0x28 (create 0x14003a6a0)");
static_assert(alignof(ZoneComponent) == 8, "ZoneComponent alignment");
