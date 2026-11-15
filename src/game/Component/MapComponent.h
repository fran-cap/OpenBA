#pragma once
// ============================================================================
// src/game/Component/MapComponent.h — ENDGAME B.3.8 block VIS
//
// `MapComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "MapComponent", parent "Component", ti 0x1401c79e0
//   create 0x140035700: MemAlloc(0x38, 8) + ctor 0x1401244d0
//   ctor   0x1401244d0: Component ctor, stamp 0x140163aa0, +0x28/+0x2a/+0x2c/
//                       +0x2e = 0xFFFF (the tree wrote 0 until B6 row 28; see the
//                       ctor), flags |= 3 (0x83)
//   dtor   0x140124490: stamp 0x140163aa0, stamp 0x140162920, ~Internal (no own work)
//   table  0x140163aa0, 16 slots:
//     [0]  0x140035850 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x1401243e0 Persist               [6] 0x140124370 OnAdded (Ghidra "Draw")
//     [7]  0x1401242f0 OnRemove (Ghidra "OnCreate")
//     [12] 0x1401243c0 OnEvent (`CMP EDX,1; JZ 0x1401241b0; RET`)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class MapComponent final : public Component {
    DECLARE_EXE_TYPE(MapComponent)

public:
    MapComponent();                                               // 0x1401244d0

    // 0x140124490
    ~MapComponent() override = default;                           // [0]  0x140035850
    void Persist(IO::Stream& stream) override;                          // [3]  0x1401243e0
    void OnAdded() override;                                      // [6]  0x140124370
    void OnRemove() override;                                     // [7]  0x1401242f0
    void OnEvent(int eventId) override;                           // [12] 0x1401243c0

    // ---- non-virtual members (retail addresses) ----
    void AddToMap();                                              // 0x1401241b0 (keystone_a_map.cpp)
    void RemoveFromMap();                                         // 0x140124750 (the tree's body; mapcomponent.cpp)

    // PUBLIC while the ported bodies read them (B38 decision 1).
    int16_t  tileX0;       // +0x28 footprint rect x (retail ctor 0xFFFF)
    int16_t  tileY0;       // +0x2A footprint rect y (retail ctor 0xFFFF)
    int16_t  tileX1;       // +0x2C footprint rect w (retail ctor 0xFFFF)
    int16_t  tileY1;       // +0x2E footprint rect h (retail ctor 0xFFFF)
    int32_t  extraField;   // +0x30 serialized int (rotation)
    uint8_t  _pad34[4];    // +0x34
};

static_assert(sizeof(MapComponent) == 0x38, "MapComponent is 0x38 (create 0x140035700)");
static_assert(offsetof(MapComponent, _pad34) == 0x34, "MapComponent::_pad34");
static_assert(alignof(MapComponent) == 8, "MapComponent alignment");
static_assert(offsetof(MapComponent, tileX0) == 0x28, "");
static_assert(offsetof(MapComponent, tileY0) == 0x2A, "");
static_assert(offsetof(MapComponent, tileX1) == 0x2C, "");
static_assert(offsetof(MapComponent, tileY1) == 0x2E, "");
static_assert(offsetof(MapComponent, extraField) == 0x30, "");
