#pragma once
// ============================================================================
// src/game/Action/CompleteRoad.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `CompleteRoad`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CompleteRoad", parent "Action", ti 0x1401c5e18
//   create 0x140021ec0: MemAlloc(0x30, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fb70, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"

class CompleteRoad final : public Action {
    DECLARE_EXE_TYPE(CompleteRoad)

public:
    CompleteRoad();                                                   // create 0x140021ec0
    CompleteRoad(uint32_t roadType, uint32_t tileIndex, uint8_t direction);   // 0x1400c59d0
    ~CompleteRoad() override = default;                        // [0] 0x140021f30
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c5880
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c5850

    // ---- fields, retail order (PUBLIC) ----
    int32_t   roadTileIndex;     // +0x20 terrain tile index
    int32_t   roadType;          // +0x24 road type id
    uint8_t   roadDirection;     // +0x28 direction
    uint8_t   _pad29[7];         // +0x29 padding
};

static_assert(sizeof(CompleteRoad) == 0x30, "CompleteRoad is 0x30 (create 0x140021ec0 MemAlloc)");
static_assert(offsetof(CompleteRoad, _pad29) == 0x29, "CompleteRoad::_pad29");
static_assert(alignof(CompleteRoad) == 8, "CompleteRoad is 8-aligned");
static_assert(offsetof(CompleteRoad, roadTileIndex) == 0x20, "CompleteRoad +0x20");
static_assert(offsetof(CompleteRoad, roadType) == 0x24, "CompleteRoad +0x24");
static_assert(offsetof(CompleteRoad, roadDirection) == 0x28, "CompleteRoad +0x28");
