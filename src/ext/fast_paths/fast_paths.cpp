// ============================================================================
// src/ext/fast_paths/fast_paths.cpp -- ext.fast_paths: the game glue
// ============================================================================
// NOT RETAIL. fast_paths.h has the contract and the seams.
//
// THE HEURISTIC SCALE NEEDS THE TYPES PRESENT. Citizens search with the
// walkable mask 0xe1f (MoveTo's passFlags), which holds type 11; no movement
// profile sets type 11's cost (Map::SetMovementCosts 0x14009e550), so it keeps
// the grid ctor's 1. The minimum over the passable TYPES is therefore 1 -- the
// retail heuristic, no gain. Over the types the grid actually HOLDS it is the
// cheapest present road (3) or ground (4). So the worker keeps a count per
// terrain type: a full scan of cellFlags on the first request after each
// SelectForGame (the grid was primed by direct stores in Pathfinder_Seed,
// pathfinder_seed.cpp, which no command reports), then command 2's hook keeps
// it current. Requests, command 2 and the scan all run on the worker thread,
// in queue order, so the counts always describe the grid the request sees.
// ============================================================================
#include "ext/fast_paths/fast_paths.h"
#include "stress_harness.h"   // StressHarness_PathShadow (block PFBENCH; src/diag)
#include "Action/grid_search.h"
#include "ext/registry.h"

#include "Action/PathSearch.h"   // GridPathSearch (the worker's search, PathfinderThread +0x78)

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Ext {
namespace FastPaths {

using GridSearch::HeuristicScale;
using GridSearch::kCellTypeCount;
using GridSearch::kCellTypeMask;
using GridSearch::kPathCap;
using GridSearch::kStepCosts;
using GridSearch::Searcher;
using GridSearch::SearchInput;
using GridSearch::SearchResult;
using GridSearch::Status;

std::atomic<uint8_t> g_mode{0};

namespace {

std::atomic<bool> g_shadowArmed{false};
std::atomic<bool> g_selectedFast{false};
std::atomic<bool> g_rescan{true};

// Worker-thread state (one pathfinder worker; see the header comment).
struct WorkerState {
    Searcher searcher;
    int32_t  typeCount[kCellTypeCount] = {};
    bool     countsValid = false;
    bool     reportedUnsupported = false;
    int      lastExpanded = 0;   // PFBENCH's expansion figure for the last Run (see LastRunExpanded)
};
WorkerState& Worker() {
    static WorkerState w;
    return w;
}

uint8_t ComposeMode() {
    return static_cast<uint8_t>((g_selectedFast.load(std::memory_order_relaxed) ? kModeFast : 0) |
                                (g_shadowArmed.load(std::memory_order_relaxed) ? kModeShadow : 0));
}

uint32_t PresentMask(WorkerState& w, const GridPathSearch& grid) {
    if (g_rescan.load(std::memory_order_acquire) || !w.countsValid) {
        g_rescan.store(false, std::memory_order_relaxed);
        for (int32_t& c : w.typeCount) c = 0;
        const int n = grid.width * grid.width;
        for (int i = 0; i < n; ++i) ++w.typeCount[grid.cellFlags[i] & kCellTypeMask];
        w.countsValid = true;
        w.reportedUnsupported = false;
    }
    uint32_t mask = 0;
    for (int t = 0; t < kStepCosts; ++t)
        if (w.typeCount[t] > 0) mask |= 1u << t;
    return mask;
}

std::atomic<bool> g_poolReportedUnsupported{false};   // RunWith's once-per-game line

SearchResult RunFastWith(Searcher& searcher, const GridPathSearch& grid, int start, int goal,
                         int32_t* path, uint32_t presentTypes) {
    SearchInput in;
    in.width = grid.width;
    in.cellFlags = grid.cellFlags;
    in.stepCost = grid.stepCost;
    in.passableTypes = grid.passableTypes;
    in.allowDiagonal = grid.allowDiagonal != 0;
    in.manhattan = grid.manhattan != 0;
    in.respectBlocked = grid.respectBlocked != 0;
    in.start = start;
    in.goal = goal;
    in.heuristicScale = HeuristicScale(grid.stepCost, grid.passableTypes, presentTypes);
    if (in.heuristicScale < 0) {   // a negative cost: not ours to reproduce
        SearchResult r;
        r.status = Status::Unsupported;
        return r;
    }
    return searcher.Run(in, path);
}

SearchResult RunFast(WorkerState& w, const GridPathSearch& grid, int start, int goal, int32_t* path) {
    return RunFastWith(w.searcher, grid, start, goal, path, PresentMask(w, grid));
}

long long Ticks() { return CompProf_Now(); }

}  // namespace

void SelectForGame() {
    g_selectedFast.store(Ext::Ext().fast_paths, std::memory_order_relaxed);
    g_rescan.store(true, std::memory_order_release);
    g_poolReportedUnsupported.store(false, std::memory_order_relaxed);
    const uint8_t mode = ComposeMode();
    g_mode.store(mode, std::memory_order_release);
    if (mode != 0) {
        std::fprintf(stderr, "[FastPaths] selected for this game: fast=%d shadow=%d\n",
                     (mode & kModeFast) ? 1 : 0, (mode & kModeShadow) ? 1 : 0);
        std::fflush(stderr);
    }
}

void SetShadowArmed(bool armed) {
    g_shadowArmed.store(armed, std::memory_order_relaxed);
    g_mode.store(ComposeMode(), std::memory_order_release);
}

int Run(GridPathSearch& grid, int start, int goal, int32_t* path) {
    const uint8_t mode = g_mode.load(std::memory_order_relaxed);
    int count;
    if (mode & kModeFast) {
        WorkerState& w = Worker();
        const SearchResult r = RunFast(w, grid, start, goal, path);
        if (r.status == Status::Unsupported) {
            if (!w.reportedUnsupported) {
                w.reportedUnsupported = true;
                std::fprintf(stderr, "[FastPaths] a request the fast search does not reproduce "
                             "(start %d goal %d): retail's search runs it (logged once per game)\n",
                             start, goal);
                std::fflush(stderr);
            }
            count = grid.RunAvl(start, goal, path);   // retail's 0x14001ce00 (the AVL body)
            w.lastExpanded = grid.search.sequence - grid.search.open.count;
        } else {
            count = r.count;
            w.lastExpanded = r.expanded;
        }
    } else {
        count = grid.Run(start, goal, path);       // 0x14001ce00
        Worker().lastExpanded = grid.search.sequence - grid.search.open.count;
    }
    if (mode & kModeShadow) FastPaths_ShadowHook(Request{&grid, start, goal});
    return count;
}

int LastRunExpanded() { return Worker().lastExpanded; }

uint32_t PresentTypesNow(const GridPathSearch& grid) { return PresentMask(Worker(), grid); }

int RunWith(Searcher& searcher, GridPathSearch& grid, int start, int goal, int32_t* path,
            uint32_t presentTypes, int* expanded) {
    const SearchResult r = RunFastWith(searcher, grid, start, goal, path, presentTypes);
    if (r.status != Status::Unsupported) {
        *expanded = r.expanded;
        return r.count;
    }
    if (!g_poolReportedUnsupported.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[FastPaths] a request the fast search does not reproduce "
                     "(start %d goal %d): retail's search runs it (logged once per game)\n",
                     start, goal);
        std::fflush(stderr);
    }
    // Retail's 0x14001ce00 (the AVL body), on the pool worker's own grid.
    const int count = grid.RunAvl(start, goal, path);
    *expanded = grid.search.sequence - grid.search.open.count;
    return count;
}

void NoteCellTerrain(uint8_t oldFlags, int newType) {
    WorkerState& w = Worker();
    if (!w.countsValid) return;   // the next request rescans
    --w.typeCount[oldFlags & kCellTypeMask];
    ++w.typeCount[static_cast<uint8_t>(newType) & kCellTypeMask];
}

ShadowResult Shadow(const Request& req) {
    ShadowResult out;
    GridPathSearch& grid = *req.grid;
    static thread_local std::vector<int32_t> retailPath(kPathCap), ourPath(kPathCap);

    long long t0 = Ticks();
    out.retailCount = grid.RunAvl(req.start, req.goal, retailPath.data());   // retail's 0x14001ce00 (the AVL body)
    out.retailTicks = Ticks() - t0;
    out.retailInserted = grid.search.sequence;
    if (out.retailCount > 0) out.retailCost = grid.search.nodes.data[req.goal].gCost;
    {
        const int n = grid.width * grid.width;
        const PathSearchNode* nodes = grid.search.nodes.data;
        int closed = 0;
        for (int i = 0; i < n; ++i)
            if (nodes[i].closedStamp == grid.search.generation) ++closed;
        out.retailExpanded = closed;
    }

    WorkerState& w = Worker();
    t0 = Ticks();
    const SearchResult r = RunFast(w, grid, req.start, req.goal, ourPath.data());
    out.ourTicks = Ticks() - t0;
    if (r.status == Status::Unsupported) {
        out.ourUnsupported = true;
        out.ourCount = out.retailCount;
        out.ourCost = out.retailCost;
        out.sameCells = true;
        return out;
    }
    out.ourCount = r.count;
    out.ourCost = r.cost;
    out.ourExpanded = r.expanded;
    out.ourInserted = r.inserted;
    out.scale = HeuristicScale(grid.stepCost, grid.passableTypes, PresentMask(w, grid));
    out.sameCells = out.ourCount == out.retailCount &&
                    std::equal(ourPath.begin(), ourPath.begin() + out.ourCount, retailPath.begin());
    return out;
}

}  // namespace FastPaths
}  // namespace Ext

void FastPaths_ShadowHook(const Ext::FastPaths::Request& req) {
    const Ext::FastPaths::ShadowResult r = Ext::FastPaths::Shadow(req);
    if (r.ourUnsupported) return;   // retail ran both: nothing to compare
    StressHarness_PathShadow(r.retailTicks, r.ourTicks, r.retailExpanded, r.ourExpanded,
                             r.retailCost, r.ourCost, r.sameCells ? 1 : 0);
}
