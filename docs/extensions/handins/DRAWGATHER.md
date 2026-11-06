# DRAWGATHER -- `ext.draw_gather` (class A): the per-frame gather over a dense side array

Block DRAWGATHER, 2026-10-12. Asked for by IBS_20261012 §2.3 / §3.2: on the main thread at uncapped
fps, `Graphics::Scene::AddCulledDrawItems` is 6.2% of ops with a 41.8% L1 load-miss rate (5.7% of
its cycles waiting), `Scene::Clean` 0.3% / 33.7%, `RtLerpTransformPair` 1.0% / 39.8%,
`MeshInstance::AddDrawItems` 2.0% / 19.5%, `GetInstanceTransform` 1.7% / 10.9%. Static gate only:
the game was NOT run by this block (the coordinator runs the stress A/B and the COMPPROF=4 shadows).

## 1. Design against the real bodies

### 1.1 What each walk touches, per node, per frame

`Renderer::Draw` (wave27_lane_gfxbig.cpp) calls `AddCulledDrawItems` once per scene group, and
`SunLight::AddDrawItems` / `SpotLight::AddDrawItems` call it again for each shadow group: ~4 walks
per frame over the same grid.

| walk | file | per node it reads | which are misses |
|---|---|---|---|
| `Scene::Clean` (0x1801023f0) | scene.cpp | `nodes[i]` (0x18 B, dense), then `geo->_dirtyFlags` (+0x2c) for EVERY live node; `Geometry::Clean` + `GridReindex` for dirty ones | the Geometry line (+0x00..+0x3f): ~49k scattered 0xC0..0x120-byte heap objects, one load each |
| `AddCulledDrawItems` cull (0x1801017a0) | cull.cpp | per surviving cell: `_cellCounts[cell]`, `_cellHeads[cell]` (dense), the cell chain `nodes[cur]._object/_next` (dense array); per object `geo->_materialTypeFlags` (+0x30), `geo->_detailId` (+0x28), then `geo->_worldBounds` (+0x4c..+0x63) for the frustum test(s) | **the two Geometry lines** (+0x28..+0x33 and +0x4c..+0x63) of every object in a cell that is partially or wholly in view, rejected or not |
| `AddCulledDrawItems` dispatch | cull.cpp | the visible list (dense), then virtual slot 3 `AddDrawItems` per geometry | the vptr line, then everything `MeshInstance::AddDrawItems` reads |
| `MeshInstance::AddDrawItems` (0x180100f20) | mesh.cpp | `_mesh` (+0xc0), both boxes (+0x4c..+0x7b, the LOD distance), `_detailId`, `_opacity` pair (+0x7c/+0x80), `_materials._data[i]` (a per-instance heap array), `_animation` (+0xd8), `_sortBias` (+0x11c) -- all five lines of the 0x120-byte object -- then `Renderer::AddDrawItem` per triangle list (material -> passes -> program chain: shared objects, cache-resident) | the instance's own lines and its materials array |
| emit: `GetInstanceTransform` / `RtLerpTransformPair` | batchc_gfx_leaves.cpp:771, mesh.cpp:2093/1197 | per SORTED draw item: `geo->_transform` pair (+0x84..+0xbb) or `_nodes._data[node]` (heap) | the transform lines, visited in material order, not node order |

So the gather's misses are the per-object Geometry reads of the cull test (two lines per tested
object, most of them rejected by the frustum) and the dispatch's first touch of each visible object;
Clean's are one line per live node. The emit pass (`GetInstanceTransform`, the lerp pair) is NOT the
gather: it runs after the sort, in draw-item order, behind `Geometry::Draw` -- a different seam (§8).

### 1.2 The replacement

- **Side array** (`src/ext/draw_gather/gather_side.h`), one per Scene, beside it (a table keyed by
  the Scene's address, 4 scenes LRU), never inside a retail struct. Per node id a 32-byte record:
  the world box (six floats, the exact bits of `Geometry::_worldBounds`) + two HINTS
  (`_materialTypeFlags`, `_detailId`); a parallel `const void*` array holds the owner geometry the
  record was taken from. Two records per cache line, indexed by the node id the cell chain already
  yields.
- **The gather** (`ExtCulledGather`): retail's quadtree walk unchanged (same CullStack, same node
  boxes with their two different max-X/Y groupings, same frusta, same per-cell masks), and per
  object `NodeVisible(hint, live, box)`: retail's test is `flagsOK && detailOK && boxOK`, all three
  pure, so the order cannot change the answer. When the hints say the flag test passes, the box
  tests run first from the record and a frustum reject never touches the Geometry; when they say
  it fails, the live flags are read first (a flag reject skips the frustum work, as retail does).
  The live flags/detail always decide (hints may be stale: `MeshInstance::SetMesh` ORs flag bits in
  without dirtying). Then the same virtual `AddDrawItems` over the visible list in its order, with
  a prefetch of the geometry 4 entries ahead (lines +0x00, +0x4c, +0x7c, +0xa0, +0xc0). The visible
  list is a reused `std::vector` per nesting depth instead of retail's 16384-entry
  `MemAlloc`/`MemFree` per call (a dispatch can re-enter the gather through
  `MaterialInstance::RenderTextures`).
- **Clean** (`ExtClean`): retail's body line for line (the same `dirty && Clean()` per live node,
  the same `GridReindex`, the same re-read of the node count, the same global-list loop), plus a
  prefetch of the Geometry 8 nodes ahead and the record upkeep (`UpkeepAfterClean`): full refresh
  when `Clean()` reported a change or the record is not this owner's; otherwise only the two hints,
  which sit in the line Clean already loaded for `_dirtyFlags` (the box line is not loaded).

Same visible list, same order, same virtual call per geometry: the same draw items (sort key
`_sortBias + _detailId`, flags, `_arg5`, the material, the user data), the same transforms and bone
sets at emit. The pick scene (`Picking::Scene`, a different class) and the sim never read any of it.

### 1.3 Every writer of what a record holds

| field | writers on a geometry that is in a grid | how the record follows |
|---|---|---|
| `_worldBounds` (the box, EXACT) | `Geometry::Clean` (scene.cpp, 0x1800a4420) is the only one; its callers are `Scene::Clean` (both loops) and `Scene::AddObject`. Also the ctor and the copy ctor (closure_batch2b.cpp:113) -- construction, before AddObject. `Geometry::operator=` is declared, not ported, no caller. No subclass overrides slot 8 | `Scene::Clean` = `ExtClean`: a box recompute always returns "changed", so the record is refreshed exactly then. `AddObject`: the new id goes into `Graphics::SceneAddLog` (the third seam), drained at the top of every ext Clean and gather: the record is invalidated and refreshed from the Geometry before use. A re-add of the same geometry, or of a new one at the freed address, at the freed id (the free list is LIFO) is exactly that case -- `ext_draw_gather_add_log_is_needed_for_a_readd_at_the_same_id` shows the stale record the log prevents |
| `_materialTypeFlags`, `_detailId` (HINTS only) | `MeshInstance::SetMesh` (mesh.cpp:2025), `EdgeMeshInstance::SetMesh` (2301), the particle / weather / status-icon / light ctors, `serialize_arrays.cpp:110`, the loaders' `_detailId` stores on sky / sun / world (globals, not grid) | refreshed at every Clean; never trusted: the live fields decide every push |
| node identity | `GridAdd` / `RemoveObject` (`_object`), `GridReindex` (moves cells, same id) | the owner pointer is compared on every read (`Valid`), and the add log covers the same-pointer case |

Grep spellings used: `_worldBounds =`, `_worldBounds.min/.max.? =`, `_materialTypeFlags [|]=`,
`_detailId =`, `->Clean()` / `.Clean()`, `SetDetailId` (no caller). A raw-offset write (+0x4c..)
from game code would escape the grep; the `DrawGatherSide` shadow (§4) is the net.

**Threading.** `AddObject` runs on the `Engine::Loader` thread during the world build while the main
thread draws (menu / loading screen) and drains the log: the log has its own spin lock and a
4096-entry capacity; past it, `overflow` makes the drain forget every record of every scene (a
rebuild, counted). The side arrays themselves are main-thread only (Clean and the gather).

### 1.4 Selection and the toggle (hook rules 2, 4, 5)

Three null-by-default statics on `Graphics::Scene` (static data: the 0x78-byte layout is
untouched), the engine end in `src/engine/graphics/scene_ext.h`:

| seam | where | faithful cost |
|---|---|---|
| `Scene::_extCulledGather` | top of `AddCulledDrawItems` (cull.cpp:340): `if (p) { p(*this, r); return; }` | one null test per call (~4 per frame) |
| `Scene::_extClean` | top of `Scene::Clean` (scene.cpp:1417), same shape | one null test per call |
| `Scene::_extAddLog` | `Scene::AddObject` after the grid insert (scene.cpp:1321) | one null test per add |

Each is ONE pointer, no table, no virtual (the `_extExtraMods` shape); `src/engine` never includes
`src/ext`. `DrawGather::Sync()` sets or clears all three from `Ext::Ext().draw_gather`; it runs at
boot (`InitFromSettings`) and after every Extensions-dialog toggle (`SetAndRecord`), never per frame
or in a loop. **Toggle off**: the three pointers go null, every side array and the visible-list
pool are freed; retail's bodies run from the next call. **Toggle on**: the log is drained, every
scene starts with no valid record, and the first Clean (or the gather itself) fills them. Class A:
nothing serialized, no sim read, live.

## 2. The registry row

`draw_gather`, Tier Extension, **class A**, default OFF, formatVersion 0, display name "Faster scene
gathering", description "The list of things to draw each frame is gathered from a compact copy of
where every object is, so objects out of view are skipped without being looked at. Exactly the same
picture; the game itself plays exactly the same." Field `Ext::Extensions::draw_gather`.

## 3. Files

| file | what |
|---|---|
| `src/ext/draw_gather/gather_side.{h,cpp}` | the side array, `NodeVisible`, `HintPasses`, `UpkeepAfterClean`, the scene table, the counters (standard C++, unit-tested) |
| `src/ext/draw_gather/draw_gather.{h,cpp}` | `Sync`, `ExtClean`, `ExtCulledGather`, the walk (cull.cpp's CullStack copied record for record), the shadows, `TakeWindow` |
| `src/engine/graphics/scene_ext.h` | NOT RETAIL: `Graphics::SceneAddLog` |
| `src/engine/graphics/Scene.h`, `scene.cpp`, `cull.cpp` | the three statics and their three null tests |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | the row, the field, the two `Sync` calls |
| `src/diag/stress_harness.cpp` | the `[Stress::gather]` line |
| `tests/unit/ext_draw_gather_test.cpp`, `tests/unit/sources.cmake` | 9 tests |

## 4. Shadows (`BANISHED_STRESS_COMPPROF=4`, extension on)

| name | what | expected |
|---|---|---|
| `DrawGather` | every gather also runs the same walk with retail's per-object reads (`RetailTest`: flags, detail, box from the Geometry, retail's order) into a scratch list and compares the two Geometry sequences: count and every element in order; detail = retail's count in the high 32 bits, the first differing index in the low. The dispatch is the same virtual call over the same sequence, so equal lists are equal draw items (record, sort key, transform, bones) | 0 |
| `DrawGatherSide` | every record the gather trusts (valid) compared bit for bit with the live `_worldBounds`; detail = node id | 0 |
| `DrawGatherFused` | issue #253: `DrawGather`'s check on a list that came out of a fused walk (section 10): the shadow group's at its gather, the main group's at the main gather that took it; a run where it counts 0 checks never fused | 0 |

## 5. The counter

One `[Stress::gather]` line per stress row in `standalone_error.log`, printed while the extension is
on (absent in a faithful run), per DRAW frame of the window:

`gathers` (calls, ~4) · `nodes` (objects tested in surviving cells = records read) · `visible`
(dispatches) · `geoTouched` and its % of nodes (objects whose Geometry the walk still read: live
flags after a box pass, or a refresh) · `refreshes` (full record refreshes, Clean + gather) ·
`cleans` · window totals `logged` (AddObject ids drained) and `rebuilds` (whole-table forgets:
toggle-on, log overflow).

How to read it: `nodes - geoTouched` is the per-frame count of Geometry dereferences the extension
removed from the cull; in a settled town `geoTouched` should sit near `visible` and `refreshes` near
the number of moving objects (citizens, animals, icons: their boxes change every tick). `rebuilds`
should be 1 per world (the build's thousands of adds overflow the log once) plus toggles; a steady
climb means something adds more than 4096 objects between two Cleans.

## 6. Unit tests (`tests/unit/ext_draw_gather_test.cpp`, all pass; suite 1109 / 0 on the perf flavour)

side array store / valid / invalidate / growth / clear and hints apart from the box; `NodeVisible`
equals `live && box` in both orders and skips the Geometry read on a box reject; the upkeep reads the
box only on refresh; a 20,000-step scripted scene modelling `Geometry::Clean`'s two-phase box machine,
AddObject's own Clean, LIFO id reuse, moves while out of the scene and flag-only changes, checking
every trusted record is live after every step; the same re-add case without the log fails (the log is
load-bearing); 200 random worlds where the record walk yields exactly the live walk's sequence; the
scene table's LRU; the add log's drain and overflow; the registry row.

Static gates: `tools/build.sh build perf --jobs 2` OK, `tools/build.sh unit perf --jobs 2` 1109 / 0,
`python tools/abi_audit.py` OK, `tools/acceptance/acceptance.sh` 0 lines off target,
`python tools/castmetric.py src` = 2 (no new cast).

## 7. For the coordinator: how to measure

- Shadows first: `STRESS_EXT=draw_gather` with `BANISHED_STRESS_COMPPROF=4`, heavy town; expect
  `DrawGather` and `DrawGatherSide` at 0 mismatches (checks ~ gathers x nodes).
- A/B, interleaved, RTSS / Afterburner stopped, machine load CLEAN: `tools/build.sh stress` (faithful)
  vs `STRESS_EXT=draw_gather tools/build.sh stress`, then the pair `STRESS_EXT=draw_gather,render_culling`
  vs `STRESS_EXT=render_culling`. Columns: `draw_ms_mean` and `fps` (uncapped), `frame_ms_mean`;
  the sim columns must not move (class A). Read `[Stress::gather]` for nodes / geoTouched.
- By eye (class A, live): toggle it in the Extensions dialog in a running game and pan / zoom; nothing
  may pop in or out on the toggle frame.

**Prediction.** Small. The IBS group is ~10% of main-thread ops at uncapped fps, but the misses are
L2/L3 hits (33.7-cycle average) and IBS puts only 5.7% of `AddCulledDrawItems`' cycles (4.9% of
Clean's) on load-miss latency: the out-of-order core already hides most of it. What the extension
removes is that latency on the rejected objects, most of Clean's, part of the dispatch's first
touch, and 4 x 128 KB allocate/free per frame. Expected: draw ms/frame -0.02 to -0.08 (of ~2.1 ms
CPU, i.e. 1-4%), uncapped fps +0.5..2% in the heavy town, at or below the noise edge; 0 at 60 Hz vsync
when the frame has slack. On a 16-32 MB L3 part the 49k Geometry objects (~10 MB with their arrays)
stop fitting with the sim's working set and these loads go to DRAM: there the gain should be larger
(feedback_dont_assume_x3d_l3_for_perf_verdicts). If the frustum work dominates as IBS says (34%),
the pair with FRUSTUMCULL is what moves fps; this block makes the cull walk's memory side cheap so
that one's ALU savings are not hidden behind it.

## 8. Doubts and follow-ups

- **FRUSTUMCULL** (`worktree-agent-ac99668fe1cde4afb`) rewrites the frustum test (prepared planes,
  `FrustumCull_Contains`, bit-identical to `RtVolume_Frustum_Contains`) inside the faithful
  `AddCulledDrawItems`. The ext walk calls the test through ONE function, `Contains` in
  draw_gather.cpp: once both are merged, prepare the two frusta in `ExtCulledGather` and route
  `Contains` to `FrustumCull_Contains`, or the extension loses that speedup. Merging is mechanical:
  this block adds only the 6-line seam at the top of `AddCulledDrawItems`.
- **The CullStack is copied** into draw_gather.cpp rather than shared, because FRUSTUMCULL is editing
  cull.cpp at the same time. After both merge, move `CullRecord` / `CullStack` / `NodeBox*` /
  `CullStackInit` / `PushChildren` into one engine header both include.
- The **emit pass** (`GetInstanceTransform`, `RtLerpTransformPair`, `_nodes._data[node]`) is outside
  the gather: it walks draw items in sorted order. A prefetch-ahead over the sorted items there (or
  caching the per-item matrix in the gather) needs a fourth seam in `Renderer`'s emit loop; not done.
- A geometry added to the grid TWICE (two node ids, one object) would leave the first id's record
  stale after the second add's Clean; retail never does that (it would be a double draw), and
  `DrawGatherSide` would show it.
- Order of the per-object tests: when the hints say "passes" but the live flags fail, the extension
  ran a frustum test retail skipped (ALU only, no output change). Only stale hints cause it.
- Not run: no measurement, no shadow numbers yet (§7).

## 9. Results (coordinator, 2026-10-12 night, measurement window 1)

Idle machine (RTSS / Afterburner stopped, hypervisor off, fps uncapped, `machine load: CLEAN`), the
same exe with `STRESS_EXT=none` vs `STRESS_EXT=draw_gather`, interleaved twice, `perf/results/*_dg_*`:

| town | draw ms/frame off | on | frame ms off | on | fps off | on |
|---|---|---|---|---|---|---|
| heavy | 2.04 / 2.01 | 1.93 / 1.88 | 2.50 / 2.47 | 2.39 / 2.31 | 400 / 405 | 419 / 433 |
| compact | 1.36 / 1.37 | 1.30 / 1.28 | 1.53 / 1.55 | 1.46 / 1.45 | 653 / 648 | 684 / 691 |

Draw -6% in both towns, fps +5..+7%; sim columns unmoved. Above section 7's prediction. The counter
(heavy): 3 gathers per frame over ~29k nodes, ~10.8k visible, Geometry touched for 40% of the nodes
(the other 60% rejected from the side record alone), ~350-415 record refreshes per frame, 0 rebuilds.
Shadows (COMPPROF=4, heavy): `DrawGather` 98,421 gathers / 0 mismatches (count and order of the visible
list), `DrawGatherSide` 957,525,021 record checks / 0 mismatches. Class A, default OFF, live toggle.

## 10. The fused walk (issue #253)

`SunLight::AddDrawItems` gathers the shadow group (draw camera = draw group 0's, cull camera = the
sun's, shader 1) inside the main group's globals loop; the main group's own gather follows. Both walk
the same quadtree in the same pre-order, so `ExtCulledGather` walks them together at the shadow gather
(`gather_walk.h` `FusedWalk` / `FusedParGather` on the gather_threads pool): a stack record carries
both groups' masks and a live bit per group, each cell gets each live group's own tests, each object
the box half of each live group's test from the one side record, and `FusedSplit` the live half
(flags / detail) from one read of the Geometry. The shadow list is dispatched at once; the main list
is kept and handed to the main gather when that gather's own `Frame` is the one the shadow gather
built for it (`MainFrameFor`, compared by `SameWalk` on every input the walk reads) over the same
scene, renderer, nesting depth and frame index, with no `Clean` and no invalidating add-log drain in
between. Anything else walks as before; an untaken list is dropped at the end of pass 1
(`Scene::_extDrawPass1End`, installed whenever the extension is). Each list is the one its own walk
gives (unit test `ext_gather_threads_fused_walk_equals_two_walks`, against retail's reads, 0..3
helpers), dispatched at the same point, so the draw items are unchanged. Part of `ext.draw_gather`
(no switch of its own): the retail gather is the extension off. The counter line gains
`fused walks/served/unused` per frame (expected 1 / 1 / 0 with the sun drawing its map, 0 / 0 / 0 on
an `ext.staggered_passes` held frame).
