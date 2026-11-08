// src/game/Component/charmanager.cpp
// Phase 3 Keystone D / move-tick tail — the CHARACTER SPATIAL-HASH MANAGER.
//
// The object at *(GameWorld+0x88) (GameWorld = *g_pGameWorldSingleton) is a loose
// implicit-quadtree spatial hash of all live characters. CharacterComponent_OnAdd
// (0x1401099c0) registers a character into it via sub_83f0, which returns the char
// render-index written to CharacterComponent+0x40 — the value that GATES the move-tick
// TAIL (the tail early-returns while +0x40 < 0). sub_8660 (re-)places a character into
// its spatial cell on every move-tick. The manager backs proximity queries
// (FUN_140092260/990) + culling; it does NOT itself touch the transform write, but the
// char-index it produces is the tail's enabling key.
//
// MANAGER STRUCT (byte offsets; verified vs disasm of sub_83f0/sub_8660/8c40/8d00/8560):
//   +0x00 float  worldSize        (full world extent in world units)
//   +0x04 float  center.x         (= worldSize/2, so worldX+center maps to [0,worldSize))
//   +0x08 float  center.y
//   +0x0c int    levels           (quadtree depth; sub_8660 clamps the computed level to this)
//   +0x10 void*  slotArray        (stride 0x18 entry, indexed by char-index)
//   +0x18 int    slotCount
//   +0x1c int    slotCapacity
//   +0x20 int*   freeList         (recycled char-indices)
//   +0x28 int    freeCount
//   +0x2c int    freeCapacity
//   +0x30 int*   cellHead         (per global cell: head char-index, -1 = empty)
//   +0x40 int*   levelOffsetTable (cell base index per level = (4^L-1)/3)
//   +0x50 int*   cellOccupancy    (per global cell: subtree occupancy count; walked up the tree)
// SLOT ENTRY (0x18 bytes): +0x00 InternalBasePtr(8) -> CharacterComponent;
//   +0x08 int prevLink; +0x0c int nextLink (circular list within a cell); +0x10 int cellIdx.
//
// Implicit-quadtree layout (PROVEN): a node's children are 4i+1..4i+4, parent = (i-1)>>2,
// level-L base = (4^L-1)/3. Identity: levelBase[L] = 4*levelBase[L-1]+1, so for a global
// index g = levelBase[L]+morton, (g-1)>>2 = levelBase[L-1] + (morton>>2) — the parent walk
// in 8c40/8d00 is consistent with the per-level morton indexing in sub_8660.
//
// Faithful 1:1 ports (extern "C", INT-width returns where the original reads EAX). My
// reimpls call each other directly by name; reg_charmanager.cpp registers each exe addr so
// external callers (OnAdd, the move-tick, the radius queries) route here via ResolveAddr.
//
// Inlined trivial ops (mathematically exact; avoids resolving 5 extra Runtime exports —
// flagged for lead review): Volume::Rectangle::Center/Size (rect = {min.xy,max.xy}),
// Math::Vector2::operator+=/operator*= (component-wise), Math::Scalar::Max.
//


#include <new>
#include <cstdint>
#include <vector>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Component/CharacterGrid.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Component/ModelComponent.h"
#include "math/Math.h"
#include "system/MemoryAllocator.h"
#include "Game/exe_constants.h"

namespace {

// --- exe .rdata consts (exe_constants.h) ---
//   g_fUVScale @ 0x14017214c (= 1.0f), DAT_1401720b8 (radius bias, 0.001),
//   DAT_140172114 (= 0.5f)
inline float RadiusEps() { float f; const uint32_t b = kRdata_1401720b8; std::memcpy(&f, &b, sizeof f); return f; }


} // namespace

// [PREFETCHCOV] Place's rect memo, one entry per character index (CharacterGrid.h).
std::vector<CharacterGrid::RectMemo> CharacterGrid::s_rectMemo;

// ===========================================================================
// 0x1400928c0  helper_28c0 — character bounding rectangle.
//   param_1 = &slot (slot[0] = InternalBasePtr -> CharacterComponent)
//   param_2 = out Rectangle {min.x, min.y, max.x, max.y} (4 floats)
//   rect = char model pos (+0x158/+0x15c) +/- CharacterDescription radius (+0x20).
// Reads its argument's pointee only: stays free, typed (B1).
// ===========================================================================
static float* CharMgr_helper_28c0(const Object::InternalPtr<CharacterComponent>* slotPtr, float* outRect) {
    CharacterComponent* ch = slotPtr->Get();
    ModelComponent* model = ch->modelComponent.Get();
    const Math::Vector3& pos = model->transform.translation;
    float px = pos.x;
    float py = pos.y;
    float r = ch->characterDescription->_radius;
    outRect[0] = px - r;   // min.x
    outRect[1] = py - r;   // min.y
    outRect[2] = px + r;   // max.x
    outRect[3] = py + r;   // max.y
    return outRect;
}

// ===========================================================================
// 0x140018c40  helper_8c40 — insert char-index into its cell's circular list +
//   bump the subtree occupancy counts up the tree.
// ===========================================================================
void CharacterGrid::Link(int idx) {
    CharacterGridSlot* s = slots._data;
    int*     occ      = cellOccupancy._data;
    CharacterGridSlot* slot = &s[idx];
    int cellIdx = slot->cell;
    int head    = cellHead[cellIdx];
    if (head < 0) {
        cellHead[cellIdx] = idx;
        slot->prev = idx;   // prev = self
        slot->next = idx;   // next = self
    } else {
        CharacterGridSlot* headSlot = &s[head];
        int headNext = headSlot->next;
        slot->next = headNext;   // next = head.next
        slot->prev = head;       // prev = head
        s[headNext].prev = idx;  // head.next.prev = idx
        s[head].next = idx;      // head.next = idx
    }
    uint32_t c = static_cast<uint32_t>(slot->cell);
    occ[static_cast<int>(c)] += 1;
    while (c != 0) {
        c = static_cast<uint32_t>((static_cast<int>(c) - 1) >> 2);
        occ[static_cast<int>(c)] += 1;
    }
}

// ===========================================================================
// 0x140018d00  helper_8d00 — unlink char-index from its cell's circular list +
//   decrement the subtree occupancy counts up the tree.
// ===========================================================================
void CharacterGrid::Unlink(int idx) {
    CharacterGridSlot* s = slots._data;
    int*     occ      = cellOccupancy._data;
    CharacterGridSlot* slot = &s[idx];

    int cellIdx = slot->cell;
    int* headP = &cellHead[cellIdx];
    if (*headP == idx) {
        *headP = s[idx].prev;   // head = self.prev
    }
    // self.prev.next = self.next ; self.next.prev = self.prev
    int prev = slot->prev;
    int next = slot->next;
    s[prev].next = next;
    s[next].prev = prev;
    slot->next = -1;
    slot->prev = -1;

    // if this cell's head still points at idx, clear it (single-element cell)
    int cIdx2 = slot->cell;
    if (cellHead[cIdx2] == idx) cellHead[cIdx2] = -1;

    uint32_t c = static_cast<uint32_t>(slot->cell);
    occ[static_cast<int>(c)] -= 1;
    while (c != 0) {
        c = static_cast<uint32_t>((static_cast<int>(c) - 1) >> 2);
        occ[static_cast<int>(c)] -= 1;
    }
}

// ===========================================================================
// 0x140018660  sub_8660 — compute the global cell for a character's current world
//   position and, if it changed, move it (Unlink from the old cell, Link into the new).
// ===========================================================================
void CharacterGrid::Place(int idx) {
    CharacterGridSlot* slot = &slots[idx];

    float rect[4];
    CharMgr_helper_28c0(&slot->character, rect);            // {min.xy, max.xy}

    // PERF (2026-09-05): the cell is a pure function of (rect, manager constants). Every
    // character re-derives it every tick, but an idle one's rect is bit-identical to the
    // last tick's, and the only effect of the body below is "move the slot to the cell it
    // is already in". Remember the last rect per slot (keyed with the slot's character so
    // a reused index never matches; Release also resets the cell to -1 on free)
    // and return early while it is unchanged AND the slot is still registered (cell >= 0).
    // Identical registration, no divides/Morton/relink for a standing citizen.
    // [PREFETCHCOV] the memo is CharacterGrid::s_rectMemo (file scope below, was a
    // function-local static here).
    {
        std::vector<RectMemo>& s_memo = s_rectMemo;
        if (static_cast<size_t>(idx) >= s_memo.size()) s_memo.resize(static_cast<size_t>(idx) + 256, RectMemo{nullptr, 0, 0, 0, 0});
        RectMemo& m = s_memo[static_cast<size_t>(idx)];
        uint32_t b0, b1, b2, b3;
        std::memcpy(&b0, &rect[0], 4); std::memcpy(&b1, &rect[1], 4);
        std::memcpy(&b2, &rect[2], 4); std::memcpy(&b3, &rect[3], 4);
        const CharacterComponent* who = slot->character.Get();
        if (m.who == who && m.b0 == b0 && m.b1 == b1 && m.b2 == b2 && m.b3 == b3 &&
            slot->cell >= 0)
            return;
        m.who = who; m.b0 = b0; m.b1 = b1; m.b2 = b2; m.b3 = b3;
    }

    // Rectangle::Center / Size (inlined) ; radius = max(half-extent.x, half-extent.y)
    float half = kRdata_140172114;
    float cx = (rect[0] + rect[2]) * half;                 // center.x
    float cy = (rect[1] + rect[3]) * half;                 // center.y
    float hx = (rect[2] - rect[0]) * half;                 // half-extent.x
    float hy = (rect[3] - rect[1]) * half;                 // half-extent.y
    float radius = hx > hy ? hx : hy;                      // Scalar::Max

    // center += manager center offset (so worldX+offset maps into [0,worldSize))
    cx += centerX;
    cy += centerY;

    float world = worldSize;
    int lv = levels;

    // PERF (2026-09-04): level / cellsPerSide / cellsPerUnit are a pure function of
    // (radius, worldSize, levels) — three float divides that every one of the ~1000
    // characters recomputes every tick with the SAME inputs (one description radius, one
    // world). Memoise the last inputs; the cached values are the identical float results,
    // so cell assignment is bit-for-bit what the divides produce.
    static uint32_t s_mRadiusBits = 0xffffffffu, s_mWorldBits = 0xffffffffu;
    static int s_mLevels = -1, s_mLevel = 0, s_mCellsPerSide = 1;
    static float s_mCellsPerUnit = 0.0f;
    uint32_t radiusBits, worldBits;
    std::memcpy(&radiusBits, &radius, 4);
    std::memcpy(&worldBits, &world, 4);
    if (radiusBits != s_mRadiusBits || worldBits != s_mWorldBits || lv != s_mLevels) {
        float eps = RadiusEps();
        float lr = world / (radius + eps);

        // level = floor(log2(lr)) via float-exponent extraction, clamped to [0, levels-1]
        uint32_t lrBits;
        std::memcpy(&lrBits, &lr, 4);
        int level = static_cast<int>((lrBits >> 0x17) - 0x7f);
        if (lv <= level) level = lv - 1;
        if (level < 0) level = 0;

        int cellsPerSide = 1 << level;
        float uvScale = kRdata_14017214c;
        s_mCellsPerUnit = uvScale / (world / static_cast<float>(cellsPerSide));
        s_mLevel = level; s_mCellsPerSide = cellsPerSide;
        s_mRadiusBits = radiusBits; s_mWorldBits = worldBits; s_mLevels = lv;
    }
    const int level = s_mLevel;
    const int cellsPerSide = s_mCellsPerSide;
    const float cellsPerUnit = s_mCellsPerUnit;
    cx *= cellsPerUnit;
    cy *= cellsPerUnit;

    uint32_t ux = static_cast<uint32_t>(static_cast<int>(cx));
    uint32_t uy = static_cast<uint32_t>(static_cast<int>(cy));
    if (static_cast<int>(ux) < 0) ux = 0;
    if (static_cast<int>(uy) < 0) uy = 0;
    if (cellsPerSide <= static_cast<int>(ux)) ux = cellsPerSide - 1;
    if (cellsPerSide <= static_cast<int>(uy)) uy = cellsPerSide - 1;

    // Morton interleave (transcribed bit-for-bit from the decompile).
    ux = (ux << 8 | ux) & 0xff00ff;
    ux = (ux << 4 | ux) & 0xf0f0f0f;
    ux = ux * 4 | ux;                       // NOT masked here (matches disasm)
    uy = (uy << 8 | uy) & 0xff00ff;
    uy = (uy << 4 | uy) & 0xf0f0f0f;
    uy = (uy * 4 | uy) & 0x33333333;
    // result = (((A ^ B) & 0x55555555) ^ A) | E   where:
    uint32_t A = (uy * 2 | uy) * 2;
    uint32_t B = ux & 0x33333333;
    uint32_t E = (ux & 0x22222222) * 2;
    uint32_t morton = ((((A ^ B) & 0x55555555) ^ A) | E);
    int globalCell = static_cast<int>(morton) + levelOffsets[level];

    if (slot->cell != globalCell) {
        if (-1 < slot->cell)
            Unlink(idx);
        slot->cell = globalCell;
        Link(idx);
    }
}

// ===========================================================================
// 0x1400021d0  FUN_1400021d0 — generic int-array resize to `newCount` (grow x3 on
//   first realloc, copy survivors, free old). Used by Register to pop the free-list.
//   The array is the {int* data, int count, int capacity} triple; its one caller hands
//   it CharacterGrid's freeList / freeCount / freeCapacity (+0x20 / +0x28 / +0x2C).
// A Collection::Array<int> instance the tree has not matched to the template (kept
// hand-expanded: the template's first allocation and growth are not proven equal).
// ===========================================================================
extern "C" void CharMgr_IntArrayResize_21d0(int32_t*& data, int32_t& count, int32_t& capacity,
                                           int newCount) {
    int oldCount = count;
    if (newCount == oldCount) return;
    int32_t* oldData = data;
    count = newCount;
    if (capacity < newCount) {
        capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0)
            capacity = newCount * 3;
        int cap = capacity;
        data = static_cast<int32_t*>(::System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(cap) << 2, 4));
    }
    int start = (oldData == data) ? oldCount : 0;
    int32_t* dst = data;
    for (int i = start; i < newCount; ++i) {
        if (i < oldCount && dst)
            dst[i] = oldData[i];
    }
    if (oldData != data)
        ::System::MemoryAllocator::MemFree(oldData);
}

// ===========================================================================
// 0x1400183f0  sub_83f0 — register a character into the grid and return its
//   char-index. Pops the free-list if non-empty, else appends (growing the slot
//   array). Then spatial-places it via Place.
// ===========================================================================
int64_t CharacterGrid::Register(Object::InternalPtr<CharacterComponent>* ibp) {
    int fc = freeList._size;
    uint64_t index;

    if (fc < 1) {
        // ---- append path ----
        int count = slots._size;
        index = static_cast<uint32_t>(count);
        int newCount = count + 1;
        if (newCount != count) {
            CharacterGridSlot* oldSlots = slots._data;
            slots._size = newCount;
            if (slots._capacity < newCount) {
                slots._capacity = newCount;
                if (count != 0) slots._capacity = newCount * 3;
                slots._data = static_cast<CharacterGridSlot*>(::System::MemoryAllocator::MemAlloc(
                    static_cast<uint64_t>(slots._capacity) * 0x18, 8));
            }
            int start = (oldSlots == slots._data) ? count : 0;
            CharacterGridSlot* s = slots._data;
            for (int i = start; i < newCount; ++i) {
                CharacterGridSlot* dst = &s[i];
                // source: existing slot (copy) for survivors, else the {ibp,0,-1} template
                Object::InternalPtr<CharacterComponent> srcIbp; int srcPrev; int srcNext; int srcCell;
                if (i < count) {
                    const CharacterGridSlot* src = &oldSlots[i];
                    srcIbp  = src->character;
                    srcPrev = src->prev;
                    srcNext = src->next;
                    srcCell = src->cell;
                } else {
                    srcIbp  = *ibp;      // the new IBP
                    srcPrev = 0;         // prev/next = 0
                    srcNext = 0;
                    srcCell = -1;        // cellIdx = -1
                }
                dst->character = nullptr;
                dst->character = srcIbp;
                dst->prev = srcPrev;
                dst->next = srcNext;
                dst->cell = srcCell;
            }
            if (oldSlots != slots._data)
                ::System::MemoryAllocator::MemFree(oldSlots);
        }
    } else {
        // ---- free-list pop path ----
        index = static_cast<uint32_t>(freeList[fc - 1]);
        CharMgr_IntArrayResize_21d0(freeList._data, freeList._size, freeList._capacity, fc - 1);
        CharacterGridSlot* slot = &slots[index];
        slot->character = *ibp;
        slot->prev = 0;
        slot->next = 0;
        slot->cell = -1;
    }

    Place(static_cast<int>(index));
    return static_cast<int64_t>(index & 0xffffffff);
}

// ===========================================================================
// 0x140018560  impl_8560 — release a char-index back to the grid (OnRemove path):
//   unlink from its cell, reset the slot to a null IBP, push index onto free-list.
// ===========================================================================
void CharacterGrid::Release(int idx) {
    CharacterGridSlot* slot = &slots[idx];
    Unlink(idx);
    slot->character = Object::InternalPtr<CharacterComponent>();
    slot->prev = 0;
    slot->next = 0;
    slot->cell = -1;

    // push idx onto the free-list (grow by 3x on first realloc)
    int fc = freeList._size;
    int newCount = fc + 1;
    if (newCount != fc) {
        int32_t* oldFL = freeList._data;
        freeList._size = newCount;
        if (freeList._capacity < newCount) {
            freeList._capacity = newCount;
            if (fc != 0) freeList._capacity = newCount * 3;
            freeList._data = static_cast<int32_t*>(::System::MemoryAllocator::MemAlloc(
                static_cast<uint64_t>(freeList._capacity) << 2, 4));
        }
        int start = (oldFL == freeList._data) ? fc : 0;
        int32_t* fl = freeList._data;
        for (int i = start; i < newCount; ++i) {
            if (fl) fl[i] = (i < fc) ? oldFL[i] : idx;
        }
        if (oldFL != freeList._data)
            ::System::MemoryAllocator::MemFree(oldFL);
    }
}
