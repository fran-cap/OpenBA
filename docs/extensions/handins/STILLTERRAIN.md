# STILLTERRAIN -- the exact memo of a still character's terrain query on the last iteration

Block STILLTERRAIN, 2026-10-13. Candidate #4 of `IDEAS_20261012.md` (§2.4, "E1-last"), built as
faithful perf work in the style of SMMEMO / STILLPATH. There is no registry entry, no settings key,
no `BANISHED_*` flag, no `Opt()` field, no `getenv` and no door.
`static constexpr bool StillTerrain::kEnabled = true` (`src/game/Component/still_terrain.h`) is the
kill switch. **The game was not run by this block.** The coordinator runs the COMPPROF=4 shadow,
the COMPPROF=2 row and the A/B.

**Short version.** On the last speed iteration, a character with desc `_b3c` clear and a cell
that is not 0x0b calls `Map::GetTerrainPos(pos, flag 0, no normal)`. When the cell carries no
floor (`floorCount <= 0`), the answer is `{x, y, h(x, y)}`, where h reads only the int16
heightmap. The memo is one entry per CharacterGrid index: `{x bits, y bits, epoch} -> z bits`.
A global 64-bit `StillTerrain::g_epoch` is bumped right after **every** write of the heightmap
or of a `floorCount`, and on every world create, load and teardown. **The census is complete**
(§1.3): four heightmap writers in the sim, two floorCount writers, and the build, load and
teardown paths. Each has a bump. Unlike the ideas file's sketch, the memo **never stands in
for a floor cell**. `Picking::Scene::Intersect` mutates the scene (its query stamp), so
skipping it would not be bit-exact. As a result the picking scene needs no census at all
(§1.2). The trig of the same tick also comes from STILLPATH's pure `s_stillMemo` when it holds
the yaw (§2.2).

## 1. The proof

### 1.1 What the last iteration's terrain block does

`CharacterComponent_MoveTick_1094c0` (`citizenai_executor.cpp`), with `rootOnly == 0`:
1. the 0x0b test reads the terrain sidecar byte of the cell (unchanged; a 0x0b cell sets z = 0 and
   skips the rest);
2. `if (!desc->_b3c)`: `pf = gameWorld->GetTerrainPos(triOut, pos, '\0', nullptr)`, then
   `fx = pf[0]; fy = pf[1]; pos.z = max(pf[2], desc->_f44)`.

The memo replaces only the call in step 2. `_f44`'s `max` still runs live on the memo's z,
so `_f44` is not part of the key (the ideas file listed it). `_b3c` is not part of the key
either: with `_b3c` set, the query is never made.

### 1.2 What `Map::GetTerrainPos` (0x14009f3e0, `mapquery.cpp`) reads for flag 0, outTri null

| input | where | in the memo |
|---|---|---|
| `inXY2[0]`, `inXY2[1]` | the call's argument (`pos`) | **the key** (bits) |
| `GameUpdateLoop::_singleton->m_worldGeometry` | `MapQuery_SampleHeight` | epoch: bumped where it is assigned (load step 23) and at teardown |
| `WorldGeometry::_gridSize`, `_fieldD0`, `_heights.data` and the four words around (x, y) | `MapQuery_SampleHeight` | epoch: every writer (§1.3) |
| `_singleton->m_game` (`CanonicalMapGrid`; the tick passes the same pointer) | the floor branch | epoch: bumped at `m_game = map` and at teardown |
| `Map::size`, `Map::cells.data`, `cells[idx].floorCount` | the floor branch's test | epoch: every writer (§1.3); **plus** the memo is only recorded when `MapQuery_MayReachFloor` is false |
| `Map::pickingScene` (Map +0x08) `Intersect` | only when `floorCount > 0` | **never skipped**: a query that may reach it is never recorded |
| MXCSR (the float rounding mode) | the interpolation | fixed for the process (STILLPATH §8) |

The function is otherwise pure. The `CanonicalMapGrid` warning (`static bool warned`) can fire
only when `this != m_game`. In the tick, `this` *is* `m_game`, and `MapQuery_MayReachFloor`
answers "may reach" (do not record) in that case anyway.

**Why floor cells are excluded.** `Picking::Scene::Intersect` is declared `const`, but it
increments the scene's `_queryStamp` and stamps every entry it visits (`src/engine/picking/
scene.cpp:357`). Those stamps only dedupe one query, and their absolute value matters only at
the 2^32 wrap. Even so, a memo that skipped Intersect would leave the scene in a different state
from retail's. A floor cell's result also depends on every mesh in the scene. So the memo records
only queries for which `MapQuery_MayReachFloor(map, x, y)` is false. That function is the floor
branch's own condition, copied without its side effects. Because floorCount is in the epoch
census, a cell that was floorless at record time is still floorless at any hit in the same
epoch. The full query would therefore not reach Intersect either. The cost is that fishermen on
docks and walkers on bridges are never memoised (§8).

**Claim.** On a hit, `GetTerrainPos` would return exactly `{pos[0], pos[1], memo.z}` and
touch nothing.

*Proof.* A hit means the entry was written in the current epoch E, for the same x / y bits. It
was written (`StillTerrainFull`) straight after a full query Q0 in epoch E, with
`MayReachFloor == false`. That means floorCount <= 0 at Q0's cell, and Q0 returned
`{x, y, h}` with `h = memo.z`. Since Q0 the epoch has not changed, so no write of any input in
§1.2 happened (§1.3). The floor test therefore still fails, so no Intersect runs, and `h` reads
the same words through the same pointers and sizes. The result has the same bits. ∎

### 1.3 Every writer of every input, and where the epoch bumps

Found by grepping each field name (`_heights`, `floorCount`, `_gridSize`, `_fieldD0`,
`m_worldGeometry =`, `m_game =`, `cells.SetSize`, `size =`), every `int16_t*` alias taken from
`_heights.data`, raw `+0x2d` / `0x2D` offsets (none) and whole-cell copies. Only the array's own
resize does whole-cell copies (`CopyMapCell` / `ZeroMapCell`). `castmetric` stays at 2, so no
raw-offset writer hides behind a cast.

| input | writer (file:line, after this block) | when | bump |
|---|---|---|---|
| heightmap word | `Map::ResetTerrainHeights` `action_pf_roots_road.cpp:175` (0x14009e0e0) | road completion (`FinishRoadCell` :212, via `completeroad.cpp:49`), ClearArea `FinishClear` (`clearareacomponent_states.cpp:427`, the build site flatten; starttown's instant build) | :177, after each store |
| heightmap word | `QuarryComponent::ApplyDepth` `quarrycomponent_deform.cpp:193` | quarry dig progress (:216, :255) | :195, after each store |
| heightmap word | `TerraformComponent::ApplyMesh` `terraformcomponent.cpp:226` | building placement terraform (:409 / :410) | :228, after each store |
| heightmap word | `TerraformComponent::OnRemove` `terraformcomponent.cpp:362` | undo of a terraform on removal | :363, after each store |
| heightmap (whole) | `WorldGeometry::BuildFromHeightfield` `worldgen.cpp:521` (SetSize), `:534` (quantize) | new game | :540, after the quantize |
| heightmap (whole), `_gridSize`, `_fieldD0` | `WorldGeometry::Serialize` `geometry_serialize.cpp:158-176` | load | :177, after the words |
| heightmap pointer / sizes | `WorldGeometry::WorldGeometry` `serialize_arrays.cpp:82-86` | world object create | :87 |
| `m_worldGeometry` | `gameupdateloop_load.cpp:932` (create); `gameupdateloop.cpp:2284` Shutdown (`= nullptr`) | create / teardown | :933; Shutdown :1840 |
| `m_game` | `gameupdateloop_load.cpp:823` (create); Shutdown | create / teardown (`gameupdateloop.cpp:2279`) | :824; Shutdown :1840 (its head, before the frees) |
| `Map::size`, `cells` (array, every floorCount = 0) | `MapTileGrid_Build` `maptilegrid.cpp:72-74` | new game | :79 (beside the sidecar's `Invalidate`) |
| `Map::size`, `cells`, every floorCount | `Map::Serialize` `gamegrid_serialize.cpp:212-223` (+ `GameGrid_SerializeCellArray_ac10` :199) | load | :224 (`if (!isSaving)`) |
| floorCount (+1 per footprint cell), scene AddMesh | `FloorComponent::PlaceFloor` `floorcomponent.cpp:180` (AddMesh :185) | dock / bridge build (msg 1, slot 6 `_addOnCreate`, slot 11 load re-place, `multiobjectcomponent.cpp:87`) | :187, after the AddMesh |
| floorCount (-1), scene RemoveMesh | `Map::RemoveFloor` `floorcomponent.cpp:119` (RemoveMesh :123) | floor removal (slot 7, also the head of PlaceFloor) | :124, after the RemoveMesh |

**Not writers** (checked): `tool_base_render.cpp:197`, `tool_base_tilerender.cpp:125`,
`serialize_heightmap_mesh.cpp:129/187`, `water_contour_mesh.cpp:650`, `action_pf_road.cpp:164/302`
and `action_pf_roots_road.cpp`'s read in the same loop are all reads (`const int16_t*` or a
sampler). `_heightBytes` (+0x140) is the RGBA image copy, which GetTerrainPos does not read.
Tornado, fire, disease, rivers and lakes write no heights and no floorCount after worldgen (no
hit for either field outside the table). `src/ext` and `src/diag` write neither.

**The picking scene census was also done, though exactness no longer needs it.** Map +0x08 is
changed only by `PlaceFloor` (AddMesh) and `RemoveFloor` (RemoveMesh). Both bump. Nothing calls
`SetTransform` on it. `GameUpdateLoop::m_pickingScene` (+0x70, the citizens' and buildings' pick
meshes, `PickingComponent`, the move tick's own pick push) is a **different** Scene, allocated
in load step 20.

**Threads.** Every writer runs on the main thread, in the sim (component / action / state code)
or in the world build / load / teardown, which never overlaps a move tick. The pathfinder thread
writes neither field. `g_epoch` is a plain `uint64_t`, like the other counters.

**Epoch wrap.** 64 bits, starting at 1. At a billion bumps per second it would take ~580 years to
wrap. The value 0 marks an empty entry.

### 1.4 What stays exactly as before

- The 0x0b sidecar test, `_b3c`'s z, `max(z, _f44)`, the transform stores, the Geometry push, the
  pick push and `CharacterGrid::Place`. Every push and `Place` runs as today.
- Characters that stepped this tick (`0 < speed`), `_b3c` characters and floor cells always run
  the query.
- No guard was added. The hit dereferences only the memo table.

## 2. Design

### 2.1 The terrain memo

`StillTerrain::MemoTable s_terrainMemo` (citizenai_executor.cpp, beside `s_stillMemo`) holds one
24-byte entry `{x, y, z, pad, uint64 epoch}` per CharacterGrid index: dense, ~48 KB for 2,000
characters. It is written in exactly one place: `StillTerrainFull`, after a full query by a
still character, if the query could not reach the floor branch. It is never scanned and never
cleared. A stale entry, an index reused by another character or a teleported model fails the
x / y / epoch compare, so the result can only be a decline.

The hit path, inline in the tail:
```
if (kEnabled && !(0 < speed)) {
    ++g_queries;
    tm = s_terrainMemo.Find(idx);
    if (tm && tm->epoch == g_epoch && tm->x == bits(pos.x) && tm->y == bits(pos.y)) {
        ++g_hits;
        if (!g_verifyOn) { triOut = {pos.x, pos.y, tm->z}; pf = triOut; }   // the hit
        else pf = StillTerrainFull(..., shadow = true);
    } else pf = StillTerrainFull(..., shadow = false);                       // query + record
} else pf = GetTerrainPos(...);
```
`StillTerrainFull` is `noinline`: the full query, the record, and the shadow are out of line.

**Why per index, not per cell.** This is the ideas file's shape. A still character asks for the
same bits frame after frame. A cell-keyed cache would need its own invalidation per cell, and it
would still need the x / y bits for the interpolation.

**Why a global epoch.** It is what the ideas file asked for, and it makes the census a list of
bump sites with no region bookkeeping. The cost is that any terrain write anywhere costs every
still character one full query (a re-record). The /still-z row reports bumps per frame (§5). If a
busy quarry or a builder bumps every frame, the hit rate collapses and per-chunk epochs would
be the next step (§8).

### 2.2 The trig on the last iteration

The tail's trig maps yaw bits to `(-sin, cos)` bits, a pure function (STILLPATH §1.3). On the last
iteration, a character that did not step now looks up `s_stillMemo[idx]`. If the entry is valid
and holds this exact yaw, it takes the two results from it and skips the `sinf` / `cosf` pair.
On a miss it calls the trig and `Put`s the result, so speed 1, which has no intermediate
iteration, also fills the memo. No invalidation is needed (STILLPATH §1.3). This is the "trig
comes from the existing s_stillMemo" of the ideas file.

## 3. Decline rules (the query runs)

- the character stepped this tick (`0 < speed`), or the iteration is intermediate (no query at all)
- desc `_b3c` set, or the cell is 0x0b (no query at all, as before)
- no entry for the index, an entry from another epoch (any heightmap / floorCount write, world
  create / load / teardown since), or different x / y bits
- the cell may carry a floor (`MapQuery_MayReachFloor`): such queries are never recorded
- trig: no valid `s_stillMemo` entry with this yaw

## 4. The shadows (BANISHED_STRESS_COMPPROF=4)

- **`StillTerrain`**: on a hit with `g_verifyOn`, the full `GetTerrainPos` runs on the live world
  and its `{x, y, z}` bits are compared with the memo's claim `{pos.x, pos.y, memo.z}`. Detail bit 0
  x, bit 1 y, bit 2 z. The shadow arm does not re-record, so decisions are the same with the shadow
  on or off. **Expected mismatches: 0.** A missed writer shows up here as a z mismatch.
- **`StillTrig`**: on a trig-memo hit with `g_verifyOn`, `SinCosf` runs and its bits are compared
  with the memo's. Bit 0 -sin, bit 1 cos. **Expected 0.**

Read: `[VERIFY] StillTerrain checks=N mismatches=M` and `[VERIFY] StillTrig ...` in the
[COMPPROF] report / standalone_error.log. Run: `BANISHED_STRESS_COMPPROF=4 tools/build.sh stress`
(heavy town, as STILLPATH). The existing `StillFastPath`, `SmTickNoop` and `TerrainSidecar` shadows
must stay at 0.

**What the stress town will and will not exercise.** It has quarries, roads and builders, so
heightmap bumps happen. A dock may not exist, in which case no floor-cell decline is seen. The
unit test (§9) covers floors.

## 5. The counter: the `[COMPPROF] /still-z` row (BANISHED_STRESS_COMPPROF=2)

`CharProf_Begin` notes `g_queries` / `g_hits`. `CharProf_End` charges a character tick that
reached the memo test to `s_tzWin`, as a hit or a full tick. The window also records the floor
declines and the epoch delta. The row is printed after `/sm`:

```
[COMPPROF]   /still-z terrain memo (STILLTERRAIN) hits/frame= N of M still last-iteration queries (a%); hit ticks X ms/frame Y ns/call; full F/frame Z ns/call (floor cells R/frame); epoch bumps/frame B; est. saved S ms/frame = P% of EntityList
```

- `a%`: expect > 90% when `B` is small (a still character re-records once per epoch). If `B` is
  around 1 per frame or more, the hit rate tells how much a global epoch costs.
- `Y` vs `Z`: whole character ticks (the executor, the tail, `Place`, the profiler), hit vs
  full. The difference is the query + trig the hit removes, plus population differences (the full
  population holds the first tick after each stop / bump). This is indicative only.
- **The authoritative number is the unprofiled A/B**: `kEnabled` false vs true, interleaved
  pairs, heavy and compact, CLEAN machine, treeWalk / EntityList ms/frame.

## 6. Prediction

Removed per hit: the `GetTerrainPos` call (the `_singleton` / world loads, two heightmap rows,
four int16 loads, the interpolation, the cell record's floorCount line in the 7 MB cell array)
and, when the trig memo also hits, `SinCosf` (~6 ns, STILLPATH §6). Added: one 24-byte memo
line (dense, L1/L2) and three compares. A full query records one entry and pays one extra read
of the already-resident floorCount line.

- Still characters on the last iteration: ~880 per frame in the heavy town (ideas file).
- Per hit ~30-45 ns when the cell line and the heightmap rows miss, ~10-15 ns when they are
  resident.
- **Predicted: ~0.02-0.04 ms/frame heavy, i.e. ~1-1.5% of EntityList**, inside the ideas file's
  ceiling of 0.045 ms (1.7%). This assumes epoch bumps are rare in the stress town (one bump per
  frame or more makes it ~0).
- **After PREFETCH (rank 1, already merged for the action-line stages)**: if the terrain arm of
  IDEAS §2.1 (the cell line and the two heightmap rows) lands, it hides most of the miss part.
  STILLTERRAIN's share would then drop to the compute and trig, ~0.01-0.02 ms/frame (~0.5%),
  around the A/B noise edge. With STILLTERRAIN on, that prefetch arm should only be issued for
  characters that will miss, or re-measured.
- The compact town has fewer still characters per frame. Expect a similar or smaller share.

## 7. Files

| file | change |
|---|---|
| `src/game/Component/still_terrain.h` (new) | `kEnabled`, `g_epoch`, `Bump`, counters (C++17 inline variables), `Bits` / `FromBits`, `Memo` / `MemoTable`, `Hit`, `ShadowDiff`, `ProfWindow`. Header-only, no game types. |
| `src/game/Component/citizenai_executor.cpp` | `s_terrainMemo`, `StillTerrainFull` (noinline: query + record / shadow), the hit test in the last-iteration terrain arm, the trig-memo read and record |
| `src/game/Game/mapquery.cpp`, `src/game/Game/Map.h` | `MapQuery_MayReachFloor` (NOT RETAIL, side-effect-free copy of the floor branch's condition). `GetTerrainPos` itself is unchanged. |
| the 13 bump sites of §1.3 | `StillTerrain::Bump()` + the include: `action_pf_roots_road.cpp`, `quarrycomponent_deform.cpp`, `terraformcomponent.cpp` (x2), `floorcomponent.cpp` (x2), `worldgen.cpp`, `geometry_serialize.cpp`, `serialize_arrays.cpp`, `gamegrid_serialize.cpp`, `maptilegrid.cpp`, `gameupdateloop.cpp`, `gameupdateloop_load.cpp` (x2) |
| `src/diag/stress_harness.cpp` | `s_tzWin` / `s_tzEpoch0` / `s_tzFloor0`, the tick split in `CharProf_End`, `StillTerrainReport` (the /still-z row), reset per window |
| `tests/unit/still_terrain_test.cpp` (new) | 4 tests (§9) |

## 8. Doubts

- **The global epoch's hit rate.** Any heightmap write on the map (a quarry deepening, a road
  tile, a building site flatten) invalidates every entry. If the /still-z row shows bumps on most
  frames, the memo pays only between bursts. Per-chunk epochs (the bump site already knows the
  word, and `MarkChunksDirty` the chunk) would fix it. They need a key on the character's chunk
  and a proof that the 2x2 corner words never straddle a chunk the bump did not mark.
- **The census relies on grep.** The terrain sidecar's census missed two writers until PERFXEXT.
  Here the field names are distinctive (`_heights`, `floorCount`), there are no raw-offset
  casts (castmetric 2), and every post-worldgen height writer also calls `MarkChunksDirty`, which
  is an independent cross-check (all four found). A future writer that forgets the bump would show
  up as a `StillTerrain` shadow mismatch only if a still character stands on the changed words.
  Each bump sits beside its store with a `[STILLTERRAIN]` comment, so the pattern is visible.
- **Floor cells excluded.** Docks and bridges are never memoised (the Intersect stamp, §1.2). If
  a dock-heavy town shows many `floor cells/frame`, a variant that also memoises floor cells would
  be exact in every value the game reads, but not in the scene's query stamp. That is a decision
  for the user, not this block.
- **`MapQuery_MayReachFloor` duplicates the floor branch's condition.** If someone edits
  `GetTerrainPos`'s floor test and not this copy, the memo could record a floor cell. A comment at
  both says so, and the shadow's z / x / y compare would catch a floor answer whose bits
  differ.
- **Resource reload.** A floor's PickingMesh is a resource. Nothing reloads resources in a
  running game. Since floor cells are excluded, this does not matter anyway.
- **MXCSR**: as STILLPATH §8.
- Not run in the game by this block. First the coordinator's COMPPROF=4 heavy run (`StillTerrain`
  and `StillTrig` mismatches = 0, the other shadows still 0), then the resave gate /
  `tools/build.sh run` (the sim is claimed unchanged, so the golden must hold), then the
  COMPPROF=2 row and the A/B.

## 9. Build results (this block)

- **The hit path as built** (perf exe, `objdump` of `CharacterComponent_MoveTick_1094c0`, the
  last-iteration `!_b3c` arm). `StillTerrainFull` is out of line (`.constprop.0`, the shadow arm
  folded in):
  ```
  comiss %xmm0,%xmm6 ; ja  <stepped: plain GetTerrainPos>            ; !(0 < speed)
  mov s_terrainMemo.begin,%rcx ; mov s_terrainMemo.end,%rax ; mov 0x40(%rbx),%r8d   ; characterIndex
  addq $1,g_queries
  sub/sar/imul (size = (end-begin)/24) ; cmp %rax,%r8 ; jae miss      ; Find's bound
  lea (idx*3) ; lea (%rcx,%rax,8),%rcx ; mov g_epoch,%rax
  cmp 0x10(%rcx),%rax ; je hit2 ...                                   ; epoch
  hit2: cmp pos.x,(%rcx) ; jne miss ; cmp pos.y,0x4(%rcx) ; jne miss  ; x / y bits
        addq $1,g_hits ; load g_verifyOn ; test ; jne shadow
        movss 0x8(%rcx),%xmm0 ; (x, y from registers) ; jmp <maxss desc+0x44, the shared tail>
  miss: call StillTerrainFull                                         ; query + record
  ```
  The 24-byte entry costs one `imul` in the bound (a 32-byte entry would make it a shift; not
  worth the table growth). With `kEnabled = false` the arm is the old single call.
- `tools/build.sh build perf --jobs 2`: OK, no warning (touched files recompiled and grepped).
- `tools/build.sh unit perf --jobs 2`: **1130 tests, 0 failures** (4 new):
  - `still_terrain_memo_table_and_hit`: bits not values (-0.0), the epoch, the empty entry never
    hits (+0, +0), negative index ignored.
  - `still_terrain_shadow_diff_and_window`
  - **`still_terrain_two_worlds_bit_identical`**: a literal copy of `GetTerrainPos` (flag 0) over
    a fake 33x33 int16 heightmap, a 32x32 cell array with floorCount and a stub picking scene that
    counts its Intersect queries (the stamp). World A always queries; world B runs the tick's
    memo (Find / Hit / FromBits; full + `Put` unless the cell may reach the floor). 40
    characters, 6 seeds x 4,000 steps. The script does heightmap words near characters (the
    Reset / Quarry / Terraform idiom), floor place / remove (floorCount + scene), world reloads
    with the table kept, moves, still teleports, and two characters sharing a grid index. Every
    query's three result bits and the scene's query count must match after every step. Result:
    identical; 840,427 still queries, 528,386 memo hits (the script bumps ~every 2nd step),
    35,201 on floor cells, 10,267 bumps.
  - **`still_terrain_broken_variants_diverge`**: a heightmap writer without its bump, a floor writer
    without its bump, and a memo that also records floor cells (it skips Intersect, so the
    stamp diverges). All three diverge: the test can tell.
- `python tools/abi_audit.py`: OK.
- `tools/acceptance/acceptance.sh`: 0 lines off target.
- `python tools/castmetric.py src`: **total 2**. No casts added (bits via `memcpy`).
- Not run: the game, the stress town, the resave gate (the coordinator's).

## 10. Results (coordinator, 2026-10-13 morning) -- SHIPPED ON

Merged tree `ideas345` (DIRECTTICK + STILLTERRAIN + the PARTREPEAT /part row), perf exe, mods
parked, every run under the game-dir lock, `machine load: CLEAN` on all sixteen A/B runs and exit 0
on every run. RTSS / Afterburner were left running: they cap only the fps column, and the verdict
reads treeWalk / EntityList / sim.

- **Shadows** (COMPPROF=4, heavy, `20261013_091032_st_shadow_heavy`): `StillTerrain` 6,298,517
  checks / 0 mismatches, `StillTrig` 6,350,665 / 0; every older shadow still 0 (StillFastPath 57.1M,
  SmTickNoop 37.1M, TickList 155.8M, TerrainSidecar 68.4M, FrustumCull 460M).
- **The /still-z row** (COMPPROF=2, heavy, `20261013_..._st_row_heavy`, windows 9 and 10): 921 / 969
  and 1,040 / 1,074 still last-iteration queries answered by the memo (95.1% / 96.9%), 39-41 ns per hit
  against 160-180 ns per full query, 27-31 floor-cell declines per frame, **epoch bumps 1.3-5.9 per
  frame**: the global epoch is not the problem section 8 feared.
- **Unit** on the merged tree: 1130 tests, 0 failures.
- **A/B** (kEnabled false / true, the four kill-switch variants off / direct / still / both
  interleaved twice per town, `perf/results/20261013_*_ab_*`):

| town | treeWalk ms/frame off | still | both | EntityList off | still | both | sim off | still | both |
|---|---|---|---|---|---|---|---|---|---|
| heavy | 1.922 / 1.930 | 1.882 / 1.902 | 1.879 / 1.948 | 2.32 / 2.31 | 2.27 / 2.28 | 2.26 / 2.35 | 0.27 / 0.27 | 0.26 / 0.26 | 0.26 / 0.27 |
| compact | 1.048 / 1.034 | 1.020 / 1.046 | 1.030 / 1.020 | 1.33 / 1.24 | 1.24 / 1.26 | 1.29 / 1.25 | 0.16 / 0.15 | 0.15 / 0.15 | 0.15 / 0.15 |

Heavy treeWalk -1.8% (both pairs the same sign), EntityList -1.9%; compact neutral-or-better
(-2.7% / +1.2% alone, -1.7% / -1.4% in the both variant). That is section 6's prediction (1-1.5% of
EntityList). Verdict: kept ON as exact faithful perf work; the kill switch stays.
