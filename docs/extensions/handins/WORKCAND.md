# WORKCAND: the idle work search's candidates in a sorted flat array (`ext.work_cand`, 2026-11-09)

> **Built: `ext.work_cand`, class B, default ON (off under `/profile:faithful`).** The plain-cost arm of the
> citizen's work walk (`Helper_sub_79b0` 0x1400f79b0 with its builder `Helper_sub_8000` 0x1400f8000, the idle
> state's bucket probe) puts the bucket's jobs into one sorted array instead of a transient AVL of one
> `MemAlloc(0x30, 8)` node per job, and visits that array. The same jobs are visited in the same order with the
> same calls. It is class B only because the skipped allocations move later heap addresses (section 2); a
> ruling on "faithful perf work, no row" is requested there.

Candidate 1 of `PROFILE_20261106.md` section 2: 9.9% of the spread town's sim update (5.5% in `MemAlloc`, 2.4%
in the rebalance), plus the frees in 79b0. Branch `perf/work-candidates`, base main `bc4e7bc7e`.

## 1. What changed

| Where | What |
|---|---|
| `src/ext/work_cand/work_cand.h` (new) | `Ext::WorkCand::CandidateList`: standard C++, header only. Retail's key as one signed 64-bit compare (`Key(cost, id)`), `Stage` per insert in retail's order, `Commit` per build (sorts only when the input is not already ascending, keeps the first of equal keys, merges into what an earlier build left) |
| `src/game/Component/citizenai_housing_idle.cpp` | the seam at the top of `Helper_sub_79b0` (`param_5 == 0 && (Ext().work_cand \|\| g_verifyOn)`); the class B body `Helper_sub_79b0_Flat` with `BuildFlat` (8000's source walk and filter), `WalkFlat` (7b60's visit), `VerifyCandidates` (the shadow) and one list per nesting level (`WorkCandidateLease`). Retail's 8000, 7c90, 7b60 and 79b0 bodies are unchanged |
| `src/ext/registry.{h,cpp}` | the `work_cand` bool and row (appended last), class B, `formatVersion` 1, default ON |
| `tests/unit/ext_work_cand_test.cpp` (new) | 5 tests (section 5); `tests/unit/ext_registry_test.cpp`: `work_cand` added to `kBcOn` |
| row icon | `tools/ext_icons/workcand.py`, `Entry_work_cand.png`, one line in `ExtensionsSprites.rsc`; `ext_sprites_data.h` re-baked in its own last commit |

The cost-from-path-distance arm (`Helper_sub_7c90`, `param_5 == 1`: the 7550 candidate roll of FindHousing and
the idle fallback) is untouched (section 7).

## 2. The allocation-sequence check: something observes it

**The allocator.** `MemAlloc` is one boundary-tag arena for every size (`src/engine/system/MemoryAllocator.h`):
`GetFreeTagForSize` (`allocator.cpp:233`) takes the first fitting block searching up from the request's bin,
splitting a larger one when bin 14 (0x38 bytes, a 0x30 node plus its tag) is empty, and `LinkFreeTag` (`:253`) pushes a
freed block on the HEAD of its bin after coalescing it with free neighbours. So a walk's N allocations and N
frees (79b0's `e320` post-order) are not neutral: they reorder bin heads, split and re-merge blocks of other
sizes, and the next allocation of a given size can get another address. Dropping them changes the addresses
of later allocations.

**What reads addresses.** `EntityList::updateTree` (`Set<ComponentEntity*>`, compared unsigned) is the per-frame
tick list: `PreUpdate` inserts every entity with the Tick flag (`entitylist.cpp:1247`), and `Update` ticks them in
the tree's in-order walk (`RebuildWalkSnapshot`, `:420-453`), i.e. in address order. The post-update tree is the
same. Smaller ones: each Work's `CitizenSet` (`Set<CitizenComponent*>`), the leisure `homeBox`, the
`CitizenBehavior` bucket trees on `Profession*` / template pointers, and the ties of `PathGraph::AssignCitizens` /
`ReassignWorkers` (retail_bugs.md section 14) when `fix.alloc_tiebreak` is off.

**What can differ, and how much.** Only objects allocated after the first walk the extension skips can land
elsewhere: a newborn, a new building or work, a spawned resource. Everything loaded from a save keeps its
address (the walk never runs before the load ends). Such an object can take another place in the tick order;
from that tick on, the order of draws from the shared `System::Random` and of first claims on a job or an item
differs, and the town's history diverges into another valid town.

| Difference | Class | Evidence |
|---|---|---|
| The walk itself: which jobs, their order, every `Validate` / `Execute` call and reject stamp, the return | unobservable (identical) | the key and duplicate rule from the disassembly (section 3); unit tests; the COMPPROF=4 shadow |
| The tree's `count`, shape, node addresses | unobservable | nothing reads them after 79b0 frees the tree |
| Later heap addresses, hence the tick order of entities created later and the other pointer-keyed orders above | observable but harmless | retail's own pointer order already varies with the heap layout: `known_port_issues.md` row 1280 (the same save writes its profession buckets in another order from an install path of another length, "true of retail and of the Windows exe on any other install path"); the pathfinder worker allocates and frees on the same arena at thread-timed moments (`pathfinder_seed.cpp` `Run`: `MemFree(node)`; `action_pf_astar.cpp` case 5: `results.data[slot].path.SetSize`), more so with `ext.path_workers`; and main already left retail's layout with PATHDIST (the region open set's pool chunks are never allocated, `PATHDIST.md` section 6) |
| Saves | observable but harmless | no format change; a save's byte order of a pointer-keyed tree can differ, as between two installs, and each loader re-sorts it; the resave gate resaves before the first tick and cannot see it |
| Determinism | unobservable | no new source of variation: no thread, clock or uninitialised read; the array lives on the CRT heap, not the arena; the run is exactly as reproducible as main's |
| Invariants, crashes | not breaking | every pointer-keyed container is correct for any addresses; the array is bounded by the bucket size and kept per nesting level (no leak, no per-walk allocation once grown); a job freed during the walk is the same dangling read in both |
| The class B row itself | observable (UI) | a new save lists `ext:work_cand` as a pseudo-mod, so retail shows its modsMissing Yes/No modal on that save; the row is locked per game |

**Class: B**, as briefed: the town can diverge from retail's because heap addresses order the tick list.

**Ruling requested: faithful perf work, no row?** Recommended. The divergence is of the kind retail shows
between two installs and between two runs, it is not tied to what the change computes (the walk is
retail's), and main already accepted it in PATHDIST. The row's only effect a player can see is the
pseudo-mod modal in retail. If ruled so: drop the row, the bool, the icon commits, the `kBcOn` token and the
DESIGN.md line; the seam becomes `if (param_5 == '\0') return Helper_sub_79b0_Flat(...)` and the body moves
from the ext comment to a faithful-perf one (as `GridPathSearch::Run`). AMBMEMO kept its own `MemAlloc` /
`MemFree` pair for this reason and named the same ruling as its follow-up.

## 3. Why the walk is retail's

Read in Ghidra (Application-x64.exe) against the port, not from the decompile alone:

- **The key** (0x1400f80d0..0x1400f8130). `CMP ECX,EAX` new cost against node cost: `JL` left, `JG` right. On
  equal costs, `CMP EBX,[RAX+0x20]` new id against node id (both read from the Works, Work +0x20): `JL` left.
  Then `JL` (node cost < new cost) right; on equal costs `JGE` (node id >= new id) skips the insert, the INC
  (0x1400f81f8) and the rebalance. So the order is (cost, id) ascending, signed, and an equal pair is dropped with
  the node keeping its job. Both compares are strict, so the in-order walk depends only on the set of keys.
- **The cost** is Work +0x24 (`MOV ECX,[RAX+0x24]`), read once per insert and stored in the node (+0x08); the
  compares read the stored cost and the live ids. `CandidateList` stores the cost, makes the key with the id at
  the insert, and re-reads the ids of the existing entries before a second build's merge, as retail's second
  build reads them.
- **Two builds, one tree** (0x1400f79b0). The gate build (the workplace's jobs, when the profession matches) and
  the whole-bucket build both write the same `[RBP-0x18]` tree; the second walk visits the first build's jobs
  again (their reject stamps decide whether they run again). `Commit` merges the same way.
- **The walk** (0x1400f7b60) is a plain in-order successor walk; `WalkFlat` visits the array in order with the
  same test (`Work +0x2c` against the scheduler's step), the same calls (slot 9, then slot 4 with a copy of the
  citizen and `&scheduler->time`) and the same stamps and returns.
- **The source walk and filter** (0x1400f8031..0x1400f8056, 0x1400f81fc..0x1400f8249) are 8000's: the id tree in
  order, a job taken when the skip key is null or equals Work +0x10.
- `CheckPtr` (0x140152ab0) is a bare RET; the alloc-failure arms never run. No Work body re-posts a job
  (no `PostWork` under `src/game/Work/`), so the ids do not change between the two builds in practice; the
  re-read is there so the merge reads what retail reads if they ever did.

## 4. The shadow (`BANISHED_STRESS_COMPPROF=4`, `g_verifyOn`)

With the shadow on, the seam takes the flat body whatever the row says. Each build fills the array AND runs
retail's `Helper_sub_8000` into a real tree from the same source node; `VerifyCandidates` walks the tree in order
against the array (job pointer and stored cost per entry, the insert count against the size) and reports once
per build: `StressHarness_Verify("WorkCandidates", ok, detail)`, `detail` = entries matched << 32 | the tree's
count. The walk then follows the row: the array with `ext.work_cand` on, retail's tree (`Helper_sub_7b60`) with it
off. The tree is freed as 79b0 frees its own, so under the shadow the arena sees retail's allocations in
either profile, and a faithful shadow run plays retail's town. Off the shadow, the faithful path pays one call
to `Ext::Ext()` and three tests per plain-cost walk, nothing per job.

## 5. Tests and what ran

- `tests/unit/ext_work_cand_test.cpp`, against retail's descent transcribed compare for compare into a plain
  binary tree (rotations keep the in-order sequence; a search under a strict order meets an equal key
  whatever the shape):
  - `key_is_retails_cost_then_id_order`: `Key` against the lexicographic order, INT_MIN..INT_MAX on both fields;
  - `one_build_walks_and_drops_as_retail`: 2000 random builds, costs and ids from small ranges (many ties and
    duplicates), walk order, costs and insert count: 0 mismatches;
  - `input_shapes`: ascending (the fast path), descending, one key 50 times (the first job stays), the
    extremes, an ascending run with re-posted jobs mixed in;
  - `second_build_merges_as_retail`: 1500 rounds of a subset build then a whole build into the same list, a
    tenth of the costs moved between the builds (a job then appears twice, as in retail), and an empty first build;
  - `clear_forgets_the_previous_walk`.
- Mutation check (run once from a scratch copy, not in the tree): keeping the LAST of equal keys, letting the
  second build replace an existing job, comparing ids unsigned, and dropping the id from the key each fail
  1 to 4 of the 5 tests.
- `tools/build.sh unit --jobs 2` (debug, sprites re-baked): 2385 tests, 0 failures, 1 skipped
  (`ext_sheet_kit_crosscheck`, no kit output). `tools/build.sh build debug` and `build perf` clean (no warning
  in the new code; the two `-Wmisleading-indentation` lines in 7c90 are retail's clamps, untouched);
  `tools/acceptance/acceptance.sh`: 0 lines off target.
- Not run (agent rule: the coordinator owns the game and the quiet box): the game, the shadow, the gate, the
  resave check, the A/B.

## 6. For the coordinator

- **Shadow, row on** (spread town):
  `BANISHED_STRESS_COMPPROF=4 STRESS_PROFILE=all STRESS_ARGS="--spread --map large --citizens 1600" tools/build.sh stress`.
  Read `[VERIFY] WorkCandidates checks=N mismatches=0` in `standalone_error.log` (also in
  `standalone_stress_compprof.txt`, cumulative per report window); a mismatch prints
  `[VERIFY] MISMATCH WorkCandidates #k detail=...` (first five); `detail` names how far the walks agreed.
- **Shadow, faithful**: the same with `STRESS_PROFILE=faithful` (the default): the array is still built and
  compared, the walk is retail's tree.
- **A/B** (machine clean, alternating, 3 runs each, perf exe): A = main `bc4e7bc7e`, B = this branch, both
  `STRESS_PROFILE=all STRESS_ARGS="--spread --map large --citizens 1600 --speed 10" tools/build.sh stress`, so the
  only difference is the row. Read sim ms / iteration and the `CitizenAIComponent` COMPPROF row. Also a
  faithful pair (`STRESS_PROFILE=faithful`) to confirm the faithful path did not get slower. Do not read perf
  from a COMPPROF=4 run (both structures are built there).
- Gate `tools/build.sh run` and `tools/resave_gate/resave_gate.sh Gate`: both run faithful, so they prove the
  off path (the gate) and the format (resave; unaffected either way, section 2).

## 7. Open choices

1. **Faithful perf, no row** (section 2). Recommendation: yes.
2. **The path-distance arm** (`Helper_sub_7c90`, the FindHousing / idle-fallback roll). It builds the same tree
   type and could use the same list (its cost would be computed in the flat body). Not done: the profile
   attributes the time to 8000, and 7c90's per-job `PathDistance` likely dominates its own share.
   Recommendation: only if the A/B shows the 8000 arm paying and a profile of the roll shows its AVL share.
3. **A neutral A/B removes the body** (the user's rule): the seam, `Helper_sub_79b0_Flat` and its helpers, the
   row and the header go together.
