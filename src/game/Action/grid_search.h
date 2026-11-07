// ============================================================================
// src/game/Action/grid_search.h -- the grid A* (GridPathSearch::Run 0x14001ce00)
// ============================================================================
// FAITHFUL PERF WORK (docs/extensions/handins/GRIDSEARCH.md). Standard C++ only:
// the unit tests link grid_search.cpp alone.
//
// The search the pathfinder worker runs for a path request (command 5,
// PathfinderThread::ApplyCommand 0x1400a6430). With heuristicScale 1 it is
// retail's GridPathSearch::Run / FindPath 0x14001d040 / Relax 0x14001d320 /
// Neighbour 0x1400a5ec0 / Heuristic 0x1400a6030 (action_pf_astar.cpp,
// action_pf_geom.cpp) over a bucket open set instead of the AVL PathOpenSet:
// the same pops in the same order, so the same path, count, cost, expansions,
// `search.sequence` and `search.open.count`. ext.fast_paths
// (src/ext/fast_paths/) runs the same search with a larger scale.
//
// It reads what retail's search reads from the worker's GridPathSearch -- the
// square grid's width, the per-cell flag bytes (terrain type bits 0-6, blocked
// bit 7), the 32 step costs, the passable-type mask, the three search flags,
// start and goal -- and writes retail's output: the path's cells, start first,
// at most kPathCap of them, and the count (0 = no path).
//
// THE RETAIL RULES REPRODUCED (each mirrored line is cited in grid_search.cpp):
//   R1  moves: 4 (allowDiagonal 0) or 8 neighbours, in Neighbour 0x1400a5ec0's
//       direction order 0..7, bounds-checked, no corner-cut test
//   R2  a neighbour is walkable when (respectBlocked == 0 or its bit 7 is clear)
//       and bit (type & 0x1f) of passableTypes is set
//   R3  step cost = stepCost[type & 0x7f] * 10 orthogonal, * 14 diagonal
//   R4  the goal test is on POP, not on insert; start == goal is a 1-cell path
//   R5  a closed cell is reopened when a strictly cheaper g reaches it
//   R6  an open cell is re-keyed only on a strictly cheaper g (new seq)
//   R7  pop order: min f, ties to the NEWEST insert (seq desc, LIFO)
//   R8  no node budget, no time limit, no partial path: an exhausted open set
//       is "no path" (count 0)
//   R9  the path is the parent chain, written start-first, the first kPathCap
//       cells kept and the count clamped to kPathCap (Run 0x14001ce00); with a
//       null buffer the UNclamped chain length is returned
//   R10 the endpoints' own types are OR-ed into passableTypes by the requester
//       (Map::RequestPath 0x14009cfb0, action_base_move.cpp) BEFORE command 0
//       reaches the worker: the mask this search receives already holds them
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace GridSearch {

constexpr int kPathCap   = 0x4000;   // Run 0x14001ce00: the 64 KB path buffer, 0x4000 cells
constexpr int kStepCosts = 0x20;     // GridPathSearch::stepCost[0x20] (+0x50..+0xD0)

// The per-cell byte of GridPathSearch::cellFlags (+0xE0). PathfinderThread::ApplyCommand
// (0x1400a6430) command 2 writes the type and keeps bit 7; command 3 writes bit 7 and
// keeps the type.
constexpr uint8_t kCellTypeMask  = 0x7f;   // bits 0-6: the terrain type, FindPath's stepCost index
constexpr uint8_t kCellBlocked   = 0x80;   // bit 7: unwalkable while respectBlocked (Neighbour 0x1400a5ec0)
constexpr int     kCellTypeCount = 0x80;   // the values the type field holds (a per-type table's size)

struct SearchInput {
    int            width = 0;             // the grid is width x width (GridPathSearch +0xD8)
    const uint8_t* cellFlags = nullptr;   // width*width bytes (+0xE0): type bits 0-6, blocked bit 7
    const int32_t* stepCost = nullptr;    // kStepCosts entries (+0x50)
    uint32_t       passableTypes = 0;     // (+0xD0) bit per type & 0x1f
    bool           allowDiagonal = false; // (+0xD4) 8 neighbours, octile heuristic
    bool           manhattan = false;     // (+0xD5) 4-neighbour heuristic: Manhattan (else Chebyshev)
    bool           respectBlocked = false;// (+0xD6) bit 7 makes a cell unwalkable
    int            start = 0;
    int            goal = 0;
    // The multiplier on retail's unit-cost heuristic. 1 = retail's search
    // (identical cells); ext.fast_paths passes HeuristicScale(). Must not exceed
    // the cheapest step cost of any cell the search can enter (consistency,
    // see grid_search.cpp); a violation the search notices is Unsupported.
    int32_t        heuristicScale = 1;
};

enum class Status : uint8_t {
    NotFound,      // retail's "no path": count 0
    Found,
    Unsupported,   // inputs outside what this search reproduces (a passable cell
                   // whose type indexes past stepCost[0x20], a negative cost, a
                   // too-wide f range, a heuristic the costs make inconsistent):
                   // the caller must run retail's AVL search instead. Nothing was
                   // written to `path` that the caller may use.
};

struct SearchResult {
    Status  status = Status::NotFound;
    int     count = 0;        // what Run 0x14001ce00 returns (0 unless Found)
    int64_t cost = -1;        // g of the goal (sum of the step costs), -1 unless Found
    int     expanded = 0;     // cells popped and closed (the goal's pop not counted)
    int     inserted = 0;     // relaxations: retail's PathSearch::sequence after the run
    int     openLeft = 0;     // retail's PathOpenSet::count after the run (the goal included)
};

// The cheapest step cost the search can pay: min stepCost[t] over the types t
// < 32 that are passable AND present (bit t of `presentTypes`; pass ~0u when
// the caller does not know which types the grid holds). Returns -1 when that
// set has a negative cost, and 0 when it is empty (no cell can be entered:
// any scale is consistent, 0 is the honest one).
int32_t HeuristicScale(const int32_t* stepCost, uint32_t passableTypes, uint32_t presentTypes);

// Bit t set when a cell of type t (type = flag byte & 0x7f, t < 32) is in the
// grid. A type >= 32 is not recorded: such a cell indexes past the cost table,
// and the search reports Unsupported if it ever enters one.
uint32_t PresentTypes(const uint8_t* cellFlags, int cellCount);

// The search, with its scratch. Not thread-safe: one per thread that searches.
class Searcher {
public:
    // Retail's Run(start, goal, path): `path` may be null (R9).
    SearchResult Run(const SearchInput& in, int32_t* path);

    // Bytes of scratch held (the cell array, the bucket heads, the entry pool).
    size_t ScratchBytes() const;

    // One cell's search state, 8 bytes. `tag` = (generation << 5) | (state << 3)
    // | the direction (Neighbour's 0..7) the cell was reached in from its parent.
    // Valid only where tag >> 5 is this search's generation.
    struct CellState {
        int32_t  g;
        uint32_t tag;
    };
    struct Entry {            // one bucket-stack entry
        int32_t cell;
        int32_t g;            // the cell's g when pushed: stale once the cell's g moved on
        int32_t next;         // the entry below it in its bucket, -1 = bottom
    };

private:
    void Prepare(int cellCount, int bucketCount);
    template <int kMode>
    SearchResult RunMode(const SearchInput& in, int32_t* path, const int32_t* cost, int buckets,
                         uint32_t span);

    std::vector<CellState> cells_;
    std::vector<int32_t>   head_;     // bucket f & (size-1) -> top entry, -1 = empty
    std::vector<Entry>     entries_;
    uint32_t               gen_ = 0;
};

}  // namespace GridSearch
