# PATHQUERYFIRST: a synchronous path query no longer waits behind every queued citizen search (`fix.path_query_first`, 2026-11-01)

> **Built: `fix.path_query_first` (tier 1, default ON, off under `/profile:faithful`), sim-visible timing.**
> The synchronous path query (a merchant boat's water route, the road tool's cost query) runs at the head
> of the pathfinder's queue instead of at its tail, so a merchant arriving in a large town no longer
> freezes the game. Nothing new is saved.

Retail entry: `docs/retail_bugs.md` §18. Inventory row F17. Found from a hang in an unattended run on a
copy of a player's save (about 1600 citizens, a 640-cell map, speed 10).

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `RunPathQuery` (0x1400a6e30, `src/game/Action/keystone_a_zone.cpp`) | `RequestPath` (0x1400a6c10): claim a slot, queue command 5 at the TAIL, spin until ready | `Ext::PathQueryFirst::RequestFirst`: claim a slot, queue command 6 at the HEAD, spin until ready |
| `PathfinderThread::ApplyCommand` (0x1400a6430, `src/game/Action/action_pf_astar.cpp`) | commands 0..5 | command 6: `Ext::PathQueryFirst::Apply` |

`RequestFirst` captures, under the queue lock, the main thread's staged settings: the passable mask, the
32 step costs and the three search flags. They are the settings the worker will hold once it reaches the
commands the caller queued just before the query (`SetStepCost` / `SetSearchFlags` /
`SetPassableTypes` stage a value and queue it only when it changed), so they are the settings retail's
command 5 would have searched with. `Apply` reads the worker's own settings, writes the captured ones,
runs retail's command 5 (the search, `ext.fast_paths`' seam included, and the publish), and writes the
worker's settings back. Every command still in the queue then runs with the settings retail gives it.

Not taken (retail's `RequestPath` instead): an inline pathfinder (`runInline`, never set in the tree) and
`ext.path_workers`, whose dispatcher drains the queue itself.

## 2. What differs from retail with the row on

- The query searches the cell grid as the worker holds it: cell updates (commands 2 / 3) still queued
  behind it are not applied yet. A water route crosses no building footprint, so the boat's route is the
  same; a road drawn across a building placed in the same frame could differ.
- The citizen searches queued before the query are answered after it, not during the wait, so those
  walkers start a few ticks later than in retail. That is the sim-visible part.
- The game does not stall. With the row off the main thread waits for the whole queue, as retail does.

## 3. Evidence

On copies of the same save, unattended, six instances at once, `BANISHED_FORCE_SPEED=10`. A temporary
stderr line (not committed) timed each boat query and logged the queue length at the request.

| Build | Queue at the boat's request | Boat query wait | Route length |
|---|---|---|---|
| row off (retail order) | 1899 commands (1063 searches) | 19353 ms | 767 |
| row off | 2509 (1199 searches) | 17576 ms | 746 |
| row off | 1 to 19 | 0 to 15 ms | |
| row on | 3776 | 49 ms, then 44 ms (return leg) | 746, 207 |
| row on | 3871 | 16 ms, then 47 ms | 767, 184 |
| row on | 641 | 35 ms, then 49 ms | 746, 207 |
| row on | 0 to 11 | 0 to 11 ms | |

The routes found have the lengths the row-off runs found for the same trading posts. One row-off run hung
(the 20 s watchdog, `standalone_hang.log`: main thread in `RunPathQuery` under
`BoatComponent::SetDestination`, the worker in `GridPathSearch::Run`); no row-on run did.

`tests/unit/ext_path_query_first_test.cpp` checks the pure parts (`src/ext/path_query_first/queue_first.h`):
the query pushed at the head is the next command the worker's pop (a copy of `PathfinderThread::Run`'s)
takes, the commands queued before and after it keep their order, and the search runs under the query's
settings with the worker's own written back. Unit tests and `tools/acceptance/acceptance.sh` pass.
`tools/build.sh run` passes `/profile:faithful`, so the gate checks the row-off path only; the row-on path is
checked by the measured runs above and a row-on autostart run.

## 4. Cost

One bool read per synchronous query. Command 6 copies 140 bytes of settings twice around the search.
