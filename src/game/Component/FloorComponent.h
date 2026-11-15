#pragma once
// ============================================================================
// src/game/Component/FloorComponent.h — ENDGAME B.3.8 block VIS
//
// `FloorComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "FloorComponent", parent "Component", ti 0x1401c76d0
//   create 0x140034600: MemAlloc(0x30, 8) + ctor 0x14011f600
//   ctor   0x14011f600: Component ctor (flags = 0x80), stamp 0x140163360,
//                       +0x28 = 0 (byte), +0x2c = -1, flags |= 0x43 (=> 0xc3)
//   dtor   0x14011f5c0 (slot 0 0x1400347f0, 83 B: the plain restamp + ~Internal shape)
//   table  0x140163360, 16 slots:
//     [0]  0x1400347f0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14011f560 Persist               [6] 0x14011f340 OnAdded (Ghidra "Draw")
//     [7]  0x14011f2a0 OnRemove (Ghidra "OnCreate")
//     [11] 0x14011f540 Slot11 (the load-time re-place)
//     [12] 0x14011f520 OnEvent (msg 1: place)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class FloorComponent final : public Component {
    DECLARE_EXE_TYPE(FloorComponent)

public:
    FloorComponent();                                             // 0x14011f600

    // 0x14011f5c0
    ~FloorComponent() override = default;                         // [0]  0x1400347f0
    void Persist(IO::Stream& stream) override;                          // [3]  0x14011f560
    void OnAdded() override;                                      // [6]  0x14011f340
    void OnRemove() override;                                     // [7]  0x14011f2a0
    void Slot11() override;                                       // [11] 0x14011f540
    void OnEvent(int eventId) override;                           // [12] 0x14011f520

    // ---- non-virtual members (retail addresses; RCX = this) ----
    void PlaceFloor();                                            // 0x14011f390

    // PUBLIC while the ported bodies read them (B38 decision 1).
    bool     floorFlag;    // +0x28 "a floor is placed" (the one persisted field, Serialize(bool&); ctor 0)
    uint8_t  _pad29[3];    // +0x29
    int32_t  floorId;      // +0x2C picking-mesh id (ctor -1 = none)
};

static_assert(sizeof(FloorComponent) == 0x30, "FloorComponent is 0x30 (create 0x140034600)");
static_assert(offsetof(FloorComponent, _pad29) == 0x29, "FloorComponent::_pad29");
static_assert(alignof(FloorComponent) == 8, "FloorComponent alignment");
static_assert(offsetof(FloorComponent, floorFlag) == 0x28, "");
static_assert(offsetof(FloorComponent, floorId) == 0x2C, "");
