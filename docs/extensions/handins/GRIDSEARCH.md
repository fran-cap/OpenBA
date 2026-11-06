# GRIDSEARCH hand-in (2026-11-05)

Block GRIDSEARCH, base main `c18b578c3`. The cell grid's A* (`GridPathSearch::Run` 0x14001ce00 /
`FindPath` 0x14001d040, the search behind every citizen path request) on a bucket open set, with results
identical to retail's. Faithful perf work, as PATHDIST did for the region graph: no toggle, no registry row,
no settings line, no new flag. The idea of an exact grid search (bucket queue with LIFO ties = the AVL's
leftmost node, a cell re-queued only on a strictly lower g, 8 bytes per cell) is also described by the
community `banished-perf` mod for the retail exe; nothing of its code is used (it carries no licence).

## 1. What changed

| Where | What |
|---|---|
| `src/game/Action/grid_search.{h,cpp}` (new) | `GridSearch::Searcher`: the grid A* over a ring of LIFO buckets, standard C++ only. Moved from `src/ext/fast_paths/fast_search.{h,cpp}` (deleted) and rewritten (§3). `heuristicScale` 1 = retail's search; ext.fast_paths passes its scale |
| `src/game/Action/action_pf_astar.cpp` | retail's `Run` body renamed `RunAvl`, verbatim; the new `Run` runs the bucket search at scale 1 (thread-local scratch), writes retail's `search.sequence` / `search.open.count`, falls back to `RunAvl` on Unsupported; `VerifyRun`, the COMPPROF=4 shadow |
| `src/game/Action/PathSearch.h` | `RunAvl`, `VerifyRun` declared beside `Run` |
| `src/ext/fast_paths/*`, `src/ext/path_workers/path_workers.cpp` | use `GridSearch::`; the fallback and the shadow's "retail" side call `RunAvl` (so the shadow still times retail's AVL) |
| `tests/unit/grid_search_test.cpp` (was `ext_fast_paths_test.cpp`) | the scale-1 test also checks the open-set count and runs on 97- and 128-wide grids too |

Nothing reads `search.nodes` but `RunAvl` (and the shadow after it), so the new `Run` does not write it;
`RunAvl` stamps its own generation, and `Run` still bumps `search.generation`, so the stamps it leaves
behind can never alias. The node array keeps its retail allocation (the pinned `PathfinderThread` layout).

## 2. Why the result is retail's

Unchanged from FASTPATHS §3-§4 and PATHDIST §1: retail's open set is a strict total order on
(f ascending, seq descending), so any structure that pops its minimum pops the same cell. One LIFO stack
per f holds its entries in seq order, so its top live entry is the largest seq. New here:

- **The stale test without a seq.** A cell is relaxed again only on a strictly smaller g (R5, R6), so its g
  strictly decreases over a search. An entry is live iff its cell is open in this generation and its g is the
  cell's g (the region search's rule, `action_pf_region_search.cpp`).
- **The parent as a direction.** The chain is walked back by Neighbour's offsets until the start cell, which
  nothing can relax again (g 0, no negative step cost: Unsupported), i.e. retail's chain to parent -1.
- **The counters.** `sequence` = relaxations, `open.count` = live entries at the end, the goal included
  (retail returns with it still in the tree).

## 3. The core

- 8 bytes per cell (g, and a tag = generation << 5 | open/closed << 3 | parent direction), against 16 in
  fast_search (g, parent, seq, mark) and retail's 24-byte `PathSearchNode`.
- No bounds test for an interior cell's neighbours; the heuristic mode (octile 8-way, Manhattan or
  Chebyshev 4-way) is a template parameter.
- Tried and dropped (neutral on the bench, §4): `cell / width` by a reciprocal multiply instead of a
  division per expansion. `-O3` instead of `-O2` gained 3-7%.

## 4. Measurements

All on the perf build, this machine idle (every stress report `machine load: CLEAN`).

**Exactness.**
- Unit (`grid_search_scale1_identical_to_retail`): 4000 random grids, widths 3-128, all four movement
  profiles: 0 mismatches in cells, count, cost, expansions, relaxations and open-set count.
- Offline replay of 23,028 real requests recorded from the spread town (large map, 1600 citizens; a
  local-only dump, not in the tree) against the same reference port: 0 mismatches, 0 Unsupported.
- In game, `BANISHED_STRESS_COMPPROF=4` (spread, large, 1600): `[VERIFY] GridSearch checks=2964
  mismatches=0` (`perf/results/20261105_232451_gs_verify`; every other shadow also 0).

**Offline, us per request over the 23,028 requests (best of 3, -O2):**

| | old core (fast_search) | new core |
|---|---|---|
| scale 1 (the faithful search) | 296.6 | 220.6 (1.34x) |
| real scale (ext.fast_paths) | 192.2 | 145.5 (1.32x) |

Expansions: mean 6854 at scale 1; the real scale expands 0.652 of that, which is why the exact search stays
behind ext.fast_paths (0.87x of today's, 0.66x of the new core's). The in-game shadow
(`BANISHED_STRESS_COMPPROF=3`, same town) puts retail's AVL search at 6.45x ext.fast_paths' old time, so the
faithful search is about 5-6x retail's per request.

**Faithful A/B, `STRESS_PROFILE=faithful --spread --map large --citizens 1600 --speed 10`, alternating
A (main `c18b578c3`) / B (this branch), 3 runs each:**

| | A | B |
|---|---|---|
| worker busy | 16.3 / 15.7 / 24.6 % | 4.35 / 4.79 / 3.77 % |
| search us p50 | 128 / 140 / 166 | 45.8 / 44.7 / 44.0 |
| search us p95 | 3411 / 3244 / 5312 | 982 / 1171 / 843 |
| wait us p95 | 13269 / 14906 / 28402 | 2550 / 2099 / 2014 |
| e2e us p50 (enqueue -> TakePath) | 2847 / 2769 / 6142 | 372 / 425 / 375 |
| sim ms / iteration | 0.29 / 0.30 / 0.31 | 0.30 / 0.32 / 0.30 |

The search runs on the pathfinder worker, so the sim's own cost does not move; the player-visible part is the
queue: a path request is answered about 7x sooner at the median, and the backlog behind a synchronous query
(the merchant-boat wait, `fix.path_query_first`) is a quarter of the worker time it was. Retail's pathfinder
is asynchronous, so answering sooner changes which tick a walker starts on, as any change in worker speed
does in retail (thread timing); the paths themselves are retail's.

## 5. Gates

- `tools/build.sh unit`: 2176 tests, 0 failures, 3 skipped (the kit-resource skips).
- `tools/build.sh run` (debug, `/profile:faithful`): autostart fired, in game for ~2.5 min, crash log empty,
  no hang, no `[FastPaths]` or `[VERIFY]` line.
- `tools/resave_gate/resave_gate.sh Gate`: PASS (resave == golden, 23,453,453 bytes).
