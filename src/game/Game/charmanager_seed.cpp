// src/game/Game/charmanager_seed.cpp
// Phase 3 Keystone D / move-tick tail — seed the CHARACTER SPATIAL-HASH MANAGER.
//
// The manager lives at *(GameWorld+0x88) (GameWorld = *g_pGameWorldSingleton). The real
// game builds it during world setup; the Game ctor (FUN_14009ee70) only ZEROES +0x88, so
// in standalone it is null and CharacterComponent_OnAdd's sub_83f0 call would crash. We
// replicate the ctor here: allocate the manager + its implicit-quadtree arrays, store at
// GameWorld+0x88. Once seeded, OnAdd assigns each character a non-negative char-index
// (CharacterComponent+0x40), which un-gates the move-tick TAIL (transform write).
//
// CTOR UNCONFIRMED: the real constructor was not located in Ghidra (the manager methods
// are split across the 0x140018xxx / 0x140092xxx clusters and the ctor wasn't isolated).
// The struct semantics are fully determined by sub_83f0/sub_8660/8c40/8d00, so this seed
// is built from first principles and is internally consistent + crash-safe:
//   * worldSize (+0x00) = (float)mapDim, center (+0x04/+0x08) = mapDim/2 — so a world X in
//     [-dim/2, dim/2] maps to cell coords [0, dim) exactly as the move-tick's own
//     (fx + dim>>1) mapping does.
//   * levels (+0x0c) chosen so the finest level resolves ~1 cell per map tile (capped).
//   * levelOffsetTable (+0x40)[L] = (4^L - 1)/3 — the implicit-quadtree base index per
//     level (PROVEN consistent with the parent = (g-1)>>2 walk in 8c40/8d00).
//   * cellHead (+0x30) all -1 (empty), cellOccupancy (+0x50) all 0.
//   * slot array / free-list empty (count=cap=0) — the first sub_83f0 append grows them.
// The spatial bucketing does NOT affect the transform write; it only governs proximity
// queries + culling. If the real ctor is later found, replace levels/worldSize here.
//
// Called from gameupdateloop_load.cpp with no flag test: before the worldgen citizen spawn and,
// on the load-from-save path, before SaveRestore.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Component/CharacterGrid.h"  // m_characterGrid's class (B.4 GAME-a)
#include "Game/Map.h"                // m_game's class (B.4 GAME-a)
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kMaxLevels = 10;   // cap (4^10 -> ~350K cells per array); maps are <= 1024 tiles
} // namespace

extern "C" void CharManager_Seed() {
    if (GameUpdateLoop::_singleton == nullptr) return;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (gw->m_characterGrid != nullptr) return;   // already seeded

    Map* map = gw->m_game;
    if (!map) { fprintf(stderr, "[CharMgr] no map @ GW+0xf8 — skip\n"); fflush(stderr); return; }
    int dim = map->size;
    if (dim <= 0) { fprintf(stderr, "[CharMgr] bad map dim=%d — skip\n", dim); fflush(stderr); return; }

    // levels: smallest L with (1<<(L-1)) >= dim, i.e. finest cell ~= 1 tile; capped.
    int levels = 1;
    while ((1 << (levels - 1)) < dim && levels < kMaxLevels) ++levels;

    // implicit-quadtree level base-offset table: offset[L] = (4^L - 1)/3
    int* offTbl = static_cast<int*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(levels) * sizeof(int), 4));
    if (!offTbl) return;
    int64_t acc = 0;
    for (int L = 0; L < levels; ++L) {
        offTbl[L] = static_cast<int>(acc);
        acc += static_cast<int64_t>(1) << (2 * L);          // += 4^L
    }
    int totalCells = static_cast<int>(acc);                 // = (4^levels - 1)/3

    int* cellHead = static_cast<int*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(totalCells) * sizeof(int), 4));
    int* cellOcc  = static_cast<int*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(totalCells) * sizeof(int), 4));
    if (!cellHead || !cellOcc) return;
    for (int i = 0; i < totalCells; ++i) { cellHead[i] = -1; cellOcc[i] = 0; }

    // allocate + zero the manager struct (0x60 covers all known fields up to +0x58)
    void* mem = System::MemoryAllocator::MemAlloc(0x60, 8);
    if (!mem) return;
    CharacterGrid* m = new (mem) CharacterGrid();   // every field zero (the seed's old memset)
    m->worldSize = static_cast<float>(dim);
    m->centerX = static_cast<float>(dim) * 0.5f;
    m->centerY = static_cast<float>(dim) * 0.5f;
    m->levels = levels;
    m->slots._data = nullptr;          // slot array (grown on first append)
    m->slots._size = 0;
    m->slots._capacity = 0;
    m->freeList._data = nullptr;
    m->freeList._size = 0;
    m->freeList._capacity = 0;
    m->cellHead._data = cellHead;
    m->levelOffsets._data = offTbl;
    m->cellOccupancy._data = cellOcc;

    gw->m_characterGrid = m;

    fprintf(stderr, "[CharMgr] seeded @ GW+0x88 = %p (dim=%d, levels=%d, cells=%d)\n",
            static_cast<void*>(m), dim, levels, totalCells);
    fflush(stderr);
}
