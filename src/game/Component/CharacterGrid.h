#pragma once
// ============================================================================
// src/game/Component/CharacterGrid.h — B.4 COMP-a
//
// The character spatial hash at GameUpdateLoop+0x88 (`m_characterGrid`): a loose
// implicit quadtree over every live CharacterComponent. Retail has no vtable and
// no reflected name for it (MemAlloc(0x60, 8) in the world load); the name is the
// GameUpdateLoop member's. Its members are the bodies charmanager.cpp ports:
//   0x1400183f0 Register   0x140018560 Release   0x140018660 Place
//   0x140018c40 Link       0x140018d00 Unlink
// Layout read off those bodies' disassembly (see charmanager.cpp); the slot and
// free-list arrays are Collection::Array headers {data, int count, int capacity}.
//
// Implicit-quadtree layout: a node's children are 4i+1..4i+4, parent = (i-1)>>2,
// level-L base = (4^L-1)/3.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <vector>                     // [PREFETCHCOV] Place's rect memo (s_rectMemo)
#include "object/Internal.h"
#include "collection/Collection.h"   // Collection::Array (the five arrays)

class CharacterComponent;   // Component/CharacterComponent.h
template <class T> struct RawArray;   // Action/PathSearch.h
namespace Math { class Scalar; }

// One slot per registered character (0x18 bytes, indexed by the character index
// CharacterComponent+0x40 holds).
struct CharacterGridSlot {
    Object::InternalPtr<CharacterComponent> character;  // +0x00
    int32_t prev;                                        // +0x08 circular list within a cell
    int32_t next;                                        // +0x0C
    int32_t cell;                                        // +0x10 global cell index (-1 = none)
    uint8_t _pad14[4];                                   // +0x14
};
static_assert(sizeof(CharacterGridSlot) == 0x18, "slot stride 0x18 (0x140018c40)");
static_assert(offsetof(CharacterGridSlot, character) == 0x0, "CharacterGridSlot::character");
static_assert(offsetof(CharacterGridSlot, _pad14) == 0x14, "CharacterGridSlot::_pad14");
static_assert(offsetof(CharacterGridSlot, prev) == 0x08, "CharacterGridSlot::prev @ +0x08");
static_assert(offsetof(CharacterGridSlot, next) == 0x0C, "CharacterGridSlot::next @ +0x0C");
static_assert(offsetof(CharacterGridSlot, cell) == 0x10, "CharacterGridSlot::cell @ +0x10");

class CharacterGrid {
public:
    // ---- members (retail addresses) ----
    int64_t Register(Object::InternalPtr<CharacterComponent>* character);   // 0x1400183f0
    void    Release(int index);                                            // 0x140018560
    void    Place(int index);                                              // 0x140018660
    void    Link(int index);                                               // 0x140018c40
    void    Unlink(int index);                                             // 0x140018d00
    // 0x140092260: the character nearest `point` within `radius` (type filter `typeId`,
    // 0 = none; `exclude` skipped) into `best` (action_pf_roots_misc.cpp).
    void    QueryNearestCharacter(CharacterComponent** best, const float* point, const Math::Scalar* radius,
                                  int typeId, CharacterComponent* const* exclude) const;
    // BESTTARGET: retail's walker body of 0x140092260, kept for the COMPPROF=4 shadow and as
    // the fallback; QueryNearestCharacter runs the byte-identical fast walk
    // (Component/charactergrid_nearest.h).
    void    QueryNearestCharacterRetail(CharacterComponent** best, const float* point,
                                        const Math::Scalar* radius, int typeId,
                                        CharacterComponent* const* exclude) const;
    // 0x140092990: every character within `radius` of `pos` (type filter `typeId`) appended
    // to `outArr`; *out = 0 (citizen_updatehealth.cpp).
    void*   RadiusQuery(void** out, float* pos, float* radius, int typeId,
                        RawArray<CharacterComponent*>* outArr) const;

    float              worldSize;         // +0x00 full world extent in world units
    float              centerX;           // +0x04 worldSize / 2
    float              centerY;           // +0x08
    int32_t            levels;            // +0x0C quadtree depth
    Collection::Array<CharacterGridSlot> slots;      // +0x10 the character slots (count +0x18, capacity +0x1C)
    Collection::Array<int32_t>     freeList;          // +0x20 recycled character indices (count +0x28, capacity +0x2C)
    Collection::Array<int32_t>     cellHead;          // +0x30 per cell: the head character index, -1 = empty
    Collection::Array<int32_t>     levelOffsets;      // +0x40 the first cell of each level, (4^L-1)/3
    Collection::Array<int32_t>     cellOccupancy;     // +0x50 per cell: characters in its subtree

    // 0x1400182b0 (Load 0x1400970dc, worldSize 2048): the qword zero stores over
    // +0x04..+0x5F (the five Arrays' ctors), the scalars, then the level table and the
    // two 0x5555-cell arrays (gameupdateloop_load.cpp).
    explicit CharacterGrid(float worldSize);
    // The port-only seed (charmanager_seed.cpp): every field zero, as its memset was.
    CharacterGrid() : worldSize(0.0f), centerX(0.0f), centerY(0.0f), levels(0) {}
    // 0x140018220: the five ~Array, +0x50 down to +0x10 (reverse declaration order; trivial
    // elements: MemFree, data = 0, the size + capacity qword = 0). No body.
    ~CharacterGrid() = default;

    // [PREFETCHCOV] Place's rect memo (PERF 2026-09-05, charmanager.cpp), one entry per
    // character index. It was a function-local static in Place; PREFETCHCOV moved it here
    // for a walk prefetch arm that measured neutral and was removed (CLEANUP); the memo
    // stays here so Place compiles as it shipped.
    struct RectMemo { const CharacterComponent* who; uint32_t b0, b1, b2, b3; };

private:
    static std::vector<RectMemo> s_rectMemo;   // charmanager.cpp; written by Place only
};

static_assert(sizeof(CharacterGrid) == 0x60, "CharacterGrid is MemAlloc(0x60, 8)");
static_assert(offsetof(CharacterGrid, worldSize) == 0x00, "CharacterGrid::worldSize @ +0x00");
static_assert(offsetof(CharacterGrid, centerX) == 0x04, "CharacterGrid::centerX @ +0x04");
static_assert(offsetof(CharacterGrid, centerY) == 0x08, "CharacterGrid::centerY @ +0x08");
static_assert(offsetof(CharacterGrid, levels) == 0x0C, "CharacterGrid::levels @ +0x0C");
static_assert(offsetof(CharacterGrid, slots) == 0x10, "CharacterGrid::slots @ +0x10");
static_assert(offsetof(CharacterGrid, slots) + offsetof(Collection::Array<int32_t>, _size) == 0x18, "CharacterGrid +0x18");
static_assert(offsetof(CharacterGrid, slots) + offsetof(Collection::Array<int32_t>, _capacity) == 0x1C, "CharacterGrid +0x1C");
static_assert(offsetof(CharacterGrid, freeList) == 0x20, "CharacterGrid::freeList @ +0x20");
static_assert(offsetof(CharacterGrid, freeList) + offsetof(Collection::Array<int32_t>, _size) == 0x28, "CharacterGrid +0x28");
static_assert(offsetof(CharacterGrid, freeList) + offsetof(Collection::Array<int32_t>, _capacity) == 0x2C, "CharacterGrid +0x2C");
static_assert(offsetof(CharacterGrid, cellHead) == 0x30, "CharacterGrid::cellHead @ +0x30");
static_assert(offsetof(CharacterGrid, cellHead) + offsetof(Collection::Array<int32_t>, _size) == 0x38, "CharacterGrid +0x38");
static_assert(offsetof(CharacterGrid, cellHead) + offsetof(Collection::Array<int32_t>, _capacity) == 0x3C, "CharacterGrid +0x3C");
static_assert(offsetof(CharacterGrid, levelOffsets) == 0x40, "CharacterGrid::levelOffsets @ +0x40");
static_assert(offsetof(CharacterGrid, levelOffsets) + offsetof(Collection::Array<int32_t>, _size) == 0x48, "CharacterGrid +0x48");
static_assert(offsetof(CharacterGrid, levelOffsets) + offsetof(Collection::Array<int32_t>, _capacity) == 0x4C, "CharacterGrid +0x4C");
static_assert(offsetof(CharacterGrid, cellOccupancy) == 0x50, "CharacterGrid::cellOccupancy @ +0x50");
static_assert(offsetof(CharacterGrid, cellOccupancy) + offsetof(Collection::Array<int32_t>, _size) == 0x58, "CharacterGrid +0x58");
static_assert(offsetof(CharacterGrid, cellOccupancy) + offsetof(Collection::Array<int32_t>, _capacity) == 0x5C, "CharacterGrid +0x5C");
