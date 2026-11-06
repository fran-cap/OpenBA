// ============================================================================
// src/ext/fast_paths/fast_paths.h -- ext.fast_paths (class B, default OFF)
// ============================================================================
// NOT RETAIL. The game glue around the grid search (src/game/Action/grid_search.h,
// run at the extension's heuristic scale): the once-per-game selection,
// the case-5 dispatch, the per-type cell counts the heuristic scale needs, and
// the shadow check the stress harness compares the two searches with.
// docs/extensions/handins/FASTPATHS.md is the hand-in.
//
// THE SEAMS (src/game/Action/action_pf_astar.cpp, PathfinderThread::ApplyCommand
// 0x1400a6430; both on the pathfinder's worker thread):
//   case 5 (a path request): `Engaged() ? Ext::FastPaths::Run(grid, ...)
//                                       : grid.Run(...)` -- retail's line when
//          the game did not select the extension. The result publish after it
//          stays retail's (row 961's race untouched).
//   case 2 (a cell's terrain type): `if (Engaged()) NoteCellTerrain(...)`
//          before retail's store, so the per-type counts follow the grid.
// Faithful cost: one relaxed byte load and a not-taken branch per command 5
// and per command 2. Nothing else; retail's search bodies are untouched.
//
// THE SELECTION. `g_mode` lives here, outside the pinned 0x1F8 PathfinderThread
// (INVENTORY §5.2). GameUpdateLoop::Load calls SelectForGame() twice: right
// after Ext::LockForGame (final for a new game) and again after step 31
// (worldgen or SaveRestore), where a load's manifest has replaced the requested
// set (EXTSAVE §2, Ext::LockForGameWithSet) -- the second call is the one that
// counts. Every command 5 of a game is issued by the running sim or the player
// tools (MoveTo::PathSetup, the road tool, boat routes), all after Load
// returns, so every request of the game sees the final selection. runInline
// (+0x5C) is never set in the tree; were it set, the inline caller would read
// the same global.
// ============================================================================
#pragma once

#include "Action/grid_search.h"   // GridSearch::Searcher (RunWith)

#include <atomic>
#include <cstdint>

struct GridPathSearch;

namespace Ext {
namespace FastPaths {

enum : uint8_t {
    kModeFast   = 1,   // the game runs the scaled grid search (ext.fast_paths on)
    kModeShadow = 2,   // the stress harness compares both searches per request
};
// 0 = retail's line. Written by SelectForGame / SetShadowArmed (the loader /
// main thread, never while a game's worker is busy), read per command 5 / 2.
extern std::atomic<uint8_t> g_mode;

inline bool Engaged() { return g_mode.load(std::memory_order_relaxed) != 0; }

// Reads Ext::Ext().fast_paths (final once the class B lock is) and the shadow
// switch into g_mode; marks the per-type counts for a rescan. One stderr line
// (standalone_error.log) per call when anything but retail is selected (Load
// calls it twice, so a game with the extension logs it twice):
//   [FastPaths] selected for this game: fast=<0|1> shadow=<0|1>
void SelectForGame();

// The stress harness's switch (block PFBENCH). Takes effect at once and at
// every later SelectForGame.
void SetShadowArmed(bool armed);

// Command 5 when Engaged(): fills `path` and returns the count exactly as
// GridPathSearch::Run 0x14001ce00 would (retail's own Run when ext.fast_paths
// is off for this game, or when the grid search reports Unsupported), then, in
// shadow mode, calls FastPaths_ShadowHook.
int Run(GridPathSearch& grid, int start, int goal, int32_t* path);

// For the stress harness's expansion column (block PFBENCH): the last Run's
// figure on this worker -- the fast search's pops, or retail's
// `sequence - open.count` when retail's body ran. Worker thread only.
int LastRunExpanded();

// Command 2 when Engaged(): the cell's flag byte before retail's store and
// command 2's arg2 (the new type: retail stores (old & 0x80) | (uint8)arg2).
void NoteCellTerrain(uint8_t oldFlags, int newType);

// ---------------------------------------------------------------------------
// Composition with ext.path_workers (docs/extensions/handins/PATHWORKERS.md).
// The pool splits Run in two. The per-type counts stay on the thread that
// applies command 2 (the dispatcher: the one Worker() state above, unchanged),
// which reads the present-type mask at each request's queue position; each
// pool worker searches with its OWN Searcher on that mask. The single-worker
// Run above is untouched.
// ---------------------------------------------------------------------------
inline bool FastSelected() { return (g_mode.load(std::memory_order_relaxed) & kModeFast) != 0; }

// The terrain types the grid holds now (bit t, t < 32): the dispatcher's
// counts, rescanned after a SelectForGame exactly as Run's first request does.
// The command thread only.
uint32_t PresentTypesNow(const GridPathSearch& grid);

// Command 5 on a pool worker: the fast search on `searcher` with the mask the
// dispatcher took, or retail's grid.RunAvl on `grid` (the worker's own copy) when
// the fast search reports Unsupported (logged once per game). `*expanded` gets
// LastRunExpanded's figure. The shadow check does not run here (PATHWORKERS.md).
int RunWith(GridSearch::Searcher& searcher, GridPathSearch& grid, int start, int goal, int32_t* path,
            uint32_t presentTypes, int* expanded);

// ---------------------------------------------------------------------------
// The shadow check (PATHRESEARCH §5): both searches on the same inputs.
// ---------------------------------------------------------------------------
struct Request {
    GridPathSearch* grid;
    int start;
    int goal;
};

struct ShadowResult {
    int     retailCount = 0, ourCount = 0;       // Run's return values
    int64_t retailCost = -1, ourCost = -1;       // g of the goal, -1 = no path
    bool    sameCells = false;                   // same count and same cells
    bool    ourUnsupported = false;              // the grid search refused (ours = retail's)
    int     retailExpanded = 0, ourExpanded = 0; // cells closed
    int     retailInserted = 0, ourInserted = 0; // relaxations (PathSearch::sequence)
    int32_t scale = 0;                           // the heuristic scale used
    long long retailTicks = 0, ourTicks = 0;     // RDTSC ticks of each search (the harness's unit)
};

// Runs retail's AVL GridPathSearch::RunAvl and the scaled grid search on the
// request, on their own path buffers (retail's search state is left as its own run leaves it;
// nothing in it outlives a request). Worker thread only. Retail's expansion
// count is read after its run from the node stamps (closedStamp == generation),
// outside the timed span.
ShadowResult Shadow(const Request& req);

}  // namespace FastPaths
}  // namespace Ext

// The hook point in command 5 (shadow mode only): runs Shadow(req) and hands
// the result to the stress harness (StressHarness_PathShadow, fast_paths.cpp;
// the harness arms the mode at BANISHED_STRESS_COMPPROF=3). This header does
// not include the harness.
void FastPaths_ShadowHook(const Ext::FastPaths::Request& req);
