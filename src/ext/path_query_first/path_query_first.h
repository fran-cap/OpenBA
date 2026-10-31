#pragma once
// ============================================================================
// src/ext/path_query_first/path_query_first.h -- fix.path_query_first (fix tier, default ON)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §18, docs/extensions/handins/PATHQUERYFIRST.md).
// Retail's synchronous path query (0x1400a6e30, keystone_a_zone.cpp RunPathQuery: the
// merchant boat's water route, BoatComponent::SetDestination 0x1400456e0, and the road
// tool's cost query) queues its search at the TAIL of the one pathfinder worker's FIFO
// (RequestPath 0x1400a6c10) and spins until it is answered, so the main thread waits for
// every citizen search queued before it. In a town of ~1600 citizens that queue reaches
// 2000-4000 commands (1000-1200 searches of up to ~50 ms each), and a merchant that
// arrives then freezes the game for 15-20 s or more.
//
// With the row on, the query goes to the HEAD of the queue as command kCommand. The
// worker runs it next with the settings the query was made under (the main thread's
// staged copies: the passable mask, the step costs and the search flags the caller
// queued just before), then puts the worker's own settings back, so every command still
// in the queue runs as retail would have run it. The wait is the search the worker is
// in plus the query's own.
//
// What differs from retail: the query sees the cell grid as the worker has it, without
// the cell updates (commands 2 / 3) still queued; and the citizen searches queued ahead
// of it are answered after the query instead of during the wait, so their walkers start
// a few ticks later than in retail (sim-visible timing). Off under /profile:faithful.
// ============================================================================
#include <cstdint>

class PathfinderThread;
struct PathCommandArgs;
struct PathTicket;

namespace Ext {
namespace PathQueryFirst {

// The command type this fix queues; retail's worker knows 0..5.
constexpr int32_t kCommand = 6;

// RunPathQuery's request (main thread), when Ext::Fix().path_query_first: claims a result
// slot into `ticket` and queues the query at the head. Returns false, doing nothing, when
// the query must take retail's path (an inline pathfinder, or ext.path_workers, whose
// dispatcher drains the queue itself); the caller then calls RequestPath.
bool RequestFirst(PathfinderThread* pf, PathTicket* ticket, int start, int goal);

// PathfinderThread::ApplyCommand, command kCommand (the worker thread): runs the query
// as retail's command 5 with the settings RequestFirst captured, then restores the
// worker's settings.
void Apply(PathfinderThread& pf, const PathCommandArgs& cmd);

}  // namespace PathQueryFirst
}  // namespace Ext
