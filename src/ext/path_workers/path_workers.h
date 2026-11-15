// ============================================================================
// src/ext/path_workers/path_workers.h -- ext.path_workers (class B, default ON)
// ============================================================================
// NOT RETAIL. The game glue around pool.h: the once-per-game selection, the
// dispatcher that replaces the worker loop's body, the pool threads and their
// scratch, the locked publish, and the teardown. PATHRESEARCH.md §4.2 is the
// spec; docs/extensions/handins/PATHWORKERS.md the hand-in.
//
// THE SEAMS (each one relaxed byte load and a not-taken branch in faithful):
//   PathfinderThread::Run 0x1400a6360 (Game/pathfinder_seed.cpp), at the top
//          of the loop, once per command: `if (Engaged()) { Dispatch(*this);
//          return; }` -- the rest of the drain runs here.
//   PathfinderThread::~PathfinderThread 0x1400a6720 (Game/keystone_a_gamedtor.cpp),
//          first statement: `if (Engaged()) ReleaseFor(this);` -- the pool
//          finishes its jobs (publishing into the results table, still alive)
//          and its threads are joined BEFORE the results, the cell bytes and
//          the queue are freed. No pool thread outlives the grid it reads.
//   GameUpdateLoop::Load (Game/gameupdateloop_load.cpp), beside both
//          Ext::FastPaths::SelectForGame calls: SelectForGame().
// Retail's ApplyCommand, search bodies, TakePath and enqueuers are untouched.
//
// THE MODE. g_mode bit 0 = selected for this game (SelectForGame), bit 1 = a
// pool is alive (set by the dispatcher that builds it, cleared by ReleaseFor).
// Engaged() is true while either is set, so a pool built in one game is still
// drained (barrier, then serial commands) if a later selection turns the
// extension off before its pathfinder is destroyed.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>

class PathfinderThread;

namespace Ext {
namespace PathWorkers {

enum : uint8_t {
    kModeSelected  = 1,
    kModePoolAlive = 2,
};
extern std::atomic<uint8_t> g_mode;

inline bool Engaged() { return g_mode.load(std::memory_order_relaxed) != 0; }

// Reads Ext::Ext().path_workers (final once the class B lock is) into bit 0
// and fixes the worker count for the game (Cpu::PathWorkers of the physical
// cores inside the affinity mask, src/platform/common/cpu/roster.h). One stderr line when selected:
//   [PathWorkers] selected for this game: workers=<N>
void SelectForGame();

// The worker loop's body when Engaged(), on the pathfinder's own thread:
// pops the FIFO exactly as Run does and dispatches each command (pool.h).
// Returns when the queue is empty; jobs may still be running (their results
// are published by the pool threads; the next command 2 / 3 waits for them,
// and ReleaseFor drains them at teardown).
void Dispatch(PathfinderThread& pf);

// The pathfinder is being destroyed (its dtor's first statement): finish every
// job, stop and join the pool threads, free their scratch. A no-op when no
// pool belongs to `pf`.
void ReleaseFor(PathfinderThread* pf);

}  // namespace PathWorkers
}  // namespace Ext
