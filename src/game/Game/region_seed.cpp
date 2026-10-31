// src/game/Game/region_seed.cpp
// Keystone A — build the pathfinder COARSE REGION-CONNECTIVITY MAP (Map+0x70 graph,
// per-cell regionId at cell+0x18, Map+0x78 region-pair cache).
//
// The real game builds this inside CreateNaturalResource_impl_sub_e8c0 (0x14009e8c0)
// the first time a natural resource is created during worldgen. e8c0 also spins up the
// async region-rebuild WORKER thread (Map+0x68) — but the INITIAL region build is fully
// SYNCHRONOUS and does NOT touch the worker: it allocates the region graph (8670),
// flushes the all-dirty chunk lists (8a90 -> per-8x8-block flood-fill -> stamps
// regionId into the Map cell array), then builds the region-pair cache. We replicate
// exactly that synchronous subset, skipping the worker (already seeded separately by
// pathfinder_seed.cpp when MOVETICK is on).
//
// Why it matters: Map_CalcDistance (0x140008830) -> Pf_IsDstReachable (f810). f810 reads
// each cell's regionId (cell+0x18); when unbuilt it spirals (9d0a0) and, finding all -1,
// returns -1, so Map_CalcDistance falls back to euclidean + gridW (= +256 on a 256 map),
// which ALWAYS exceeds work radii -> e.g. ForesterWork's "tree within radius*2" check
// fails for every cell. With the region map built, f810 returns 1 for same-region cells
// and runs the (already ported+registered) region A* (a8ba0/dcd0) for cross-region pairs,
// so distances are realistic and range checks pass.
//
// Dependencies (ALL already ported + registered): the chunk family 8a90/80a0/7f20/7ad0/
// 7950/7670/7750/8200/8350/8420/7480 (action_pf_region.cpp + reg_pf_region.cpp), the
// region A* dcd0/dc00/d320/d450/d550/3480/d400 + entry a8ba0 (action_pf_astar.cpp /
// action_pf_pools.cpp + their registrars). NEW here: 8670 (graph ctor) + e060 (coarse
// array) in region_graph_build.cpp, and the pair cache (cca0) in region_paircache.cpp.
//
// Gated behind BANISHED_LIVE_REGIONMAP (default OFF until A/B-verified); when off this is
// a no-op and Map+0x70/+0x78 stay 0 (zeroed by the Game ctor), preserving the euclidean
// fallback exactly as before.

#include "Game/Map.h"
#include "Action/PathSearch.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Action/exe_bodies.h"   // Pf_RegionGraphCtor_8670, Pf_RegionPairCache_Build, Pf_FlushDirtyChunks_8a90
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Ported region-graph builders and chunk flush, called by name through Action/exe_bodies.h:
// Pf_RegionGraphCtor_8670 (0x1400a8670), Pf_RegionPairCache_Build (the e8c0 pair-cache
// tail), Pf_FlushDirtyChunks_8a90 (0x1400a8a90).

extern "C" void Region_Seed(Map* map, int dim) {   // map: GameUpdateLoop::m_game
    if (!map || dim <= 0) return;

    if (map->regionGraph != nullptr) return;       // already built

    // coarse-grid dims = dim/8, a signed TRUNCATING divide (e8c0, 0x14009eb37:
    // CDQ; AND EDX,7; ADD; SAR EAX,3 -- the sign fix-up only matters for dim < 0):
    //   (int)(dim + ((dim >> 0x1f) & 7)) >> 3
    int coarseDim = static_cast<int>((static_cast<int>(dim) + ((static_cast<int>(dim) >> 31) & 7)) >> 3);
    if (coarseDim <= 0) return;

    // 1) allocate + construct the region graph (e8c0: MemAlloc(0x108,4) -> 8670).
    RegionGraph* graph = static_cast<RegionGraph*>(System::MemoryAllocator::MemAlloc(0x108, 4));
    if (!graph) return;
    graph->Init(map, static_cast<uint32_t>(coarseDim), coarseDim);   // 0x1400a8670

    // 2) link the graph (Map::regionGraph) BEFORE flushing — Flush stamps regionId via graph->map.
    map->regionGraph = graph;

    // 3) flush all (8670 marked every coarse cell dirty in both lists) -> flood-fill each
    //    8x8 block, compute centroids/edges, and STAMP regionId into the Map cell array.
    graph->Flush();   // 0x1400a8a90

    // 4) region-pair length cache (Map::regionPairCache) — needed by f810's cross-region path (a8ba0).
    RegionPairCache* pcV = Pf_RegionPairCache_Build();
    map->regionPairCache = pcV;

    int regionCount = graph->regions.size;
    fprintf(stderr, "[RegionSeed] region map built: graph@%p coarseDim=%d regionNodes=%d pairCache=%p\n",
            (void*)graph, coarseDim, regionCount, static_cast<void*>(pcV));
    fflush(stderr);
}
