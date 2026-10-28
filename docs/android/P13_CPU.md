# Android P13 pass 3: CPU time per frame on the Galaxy S10

NOT RETAIL. Pass 3 of the S10 session: the main thread's and the render thread's pre-Release CPU time,
from the ranked list of `P13_PROFILE.md` §9.1. Branch `android/p13-cpu` (from `android/p12` at
`2a5c2c481`). Raw logs and scripts are in the session scratchpad, not the tree.

## 1. Baseline (P13_PROFILE §2-4, release + PGO, stress town, cap 60)
53.6 fps, frame 18.7 ms = main thread 14.3 ms on CPU + 4.45 ms waiting on the render thread's emit. The
two are serial at speed 10 (the render_pipe fence), so a millisecond off either is a millisecond off the
frame. Main-thread tops: the walk 4.0 ms, `ExtCulledGather` 3.16 ms, `ExtClean` 0.91, DrawSort 0.79,
`MeshInstance::Update` 0.61, `MoveTo::Enter` 0.42, `CharacterComponent::Tick` 0.37. Render thread 4.4 ms:
`ExtDrawItemList` 1.27 self, `IssueDraw` 1.13 (`vkCmdPushDescriptorSetKHR` 0.65).

## 2. Plan, in this order (gains estimated, none measured yet)
1. **The gather's two misses** (ext.draw_gather, class A; main thread, -0.5..-0.9 ms).
   - `draw_gather.cpp:182`, the live `_materialTypeFlags` / `_detailId` read (27% of the gather): today a
     node whose hints fail reads its Geometry first, and a node whose box passes reads it second, inside
     the walk, one dependent miss at a time (`[Stress::gather]`: 21.4k nodes, 4.8k visible, 9.9k
     Geometries touched a frame). Prototype: the walk tests the side record's box only and collects the
     candidates; a second pass over that array evaluates the live flag / detail test with the Geometries
     prefetched a few entries ahead; then the dispatch, as now. Retail's test is
     `flagsOK && detailOK && boxOK`, three pure reads, all of them still made before the first
     AddDrawItems, in the walk's order: the visible list is the same by construction (the COMPPROF=4
     `DrawGather` shadow checks it). The hints become unused on the sequential path.
   - `Renderer::AddDrawItem`'s `material->_shaderClass` (13%): the dispatch prefetches the Geometry
     lines four entries ahead but not the MaterialInstances. Prototype: two more stages, as cull.cpp's
     GATHERPF arm 2 does for the faithful gather: at i+2 a MeshInstance's `_materials._data` line, at i+1
     the first MaterialInstances' `_shaderClass` / `_material` line. Hints only.
2. **The pixel set as a pooled set with dynamic constant-buffer offsets on the 1.1 path only** (render
   thread, -0.4..-0.5 ms): the vertex set's model (a set from the frame's pools, found again by key,
   bound with dynamic offsets) for set 1 too, when `Vulkan11Path()`. The 1.3 path keeps its pushes
   (P7 measured the opposite trade on the S25's 1.3 driver). Same descriptors, so the same image; a
   frame-matched screenshot pair checks it.
3. **Walk prefetch leftovers** (~0.3 ms together): `MoveTo::Enter`, `CharacterComponent::Tick`,
   `MoveTo::path[pathIndex]`, the terrain cell, the CharacterGrid slot. One A/B each, ARM-only arms
   (`__aarch64__`), kept only when they pay.
4. **draw_batch's instanced loop** (more prefetch, -0.2..-0.4, uncertain) and the vertex / index bind
   de-dup (-0.1).
5. If time remains: a line profile of `ExtClean` and `MeshInstance::Update`, reported, no change unless
   clearly justified.

## 3. Protocol
Each prototype: one ABBA on the S10 stress town (`tools/android.sh run BANISHED_STRESS=1
BANISHED_FPS_CAP=60`, 6000 frames), release + PGO, debug-key signed, battery <= 32 C and thermal 0 at
every start, means of the `[Stress]` windows t = 20..100 s and of `[Stress::pipe]`, numbers per run. Kept
only beyond noise (pass 2: three runs of one apk agree within ~1%). A changed function runs without PGO
in B (its profile no longer matches), which works against B. Sim results identical (prefetches and
layout-preserving side state only); rendering identical (a frame-matched screenshot pair for a render
change). Shared or sim code also runs the Windows checks (unit, the gate, the resave gate, the Vulkan
backend for a backend change).

## 4. Results (2026-10-28)

Every run: one stress run per hold of the phone lock, battery 31.5-32.0 C and thermal status 0 at the start
(28.2 C for run 9, after an idle hour), thermal 0 at every end, cpu4 / cpu7 / GPU caps 2419 / 2842 MHz /
585 MHz at every start. Figures are the MEDIANS of the `[Stress]` windows t >= 20 s and of the
`[Stress::pipe]` windows: the session had two runs with a ~12 s throttle event mid-run (runs 2 and 10:
fps ~26-40 in one or two windows, update 13-16 ms, the vendor thermal engine capping gold after
back-to-back runs, Android's thermal status still 0), which a mean of ten windows cannot absorb.
"main" is the main thread's draw half, "emit" the render thread's.

### 4.1 The gather (B1): KEPT
Live test after the walk + the dispatch's MaterialInstance stages, both `__aarch64__` only (commit
`perf: ext.draw_gather's live test ...`).

| run | build | fps | frame ms (p95) | update | draw | main / packet / emit | wait |
|---|---|---|---|---|---|---|---|
| 1 | A base | 53.1 | 18.82 (27.51) | 9.01 | 7.97 | 4.22 / 3.72 / 5.31 | 4.23 |
| 2 | B1 (throttle event) | 53.7 | 18.60 (28.16) | 9.45 | 7.20 | 3.62 / 3.59 / 5.26 | 3.94 |
| 3 | B1 | 53.8 | 18.60 (27.66) | 9.27 | 7.47 | 3.70 / 3.75 / 5.34 | 4.27 |
| 4 | A base | 53.8 | 18.58 (27.33) | 8.99 | 7.90 | 4.24 / 3.64 / 5.21 | 4.15 |

`[Stress::gather]`: Geometries touched a frame 10.6k -> 8.1k (21.4k nodes, 5.2k visible). The main
thread's draw half fell 0.55 ms in every B run, but update rose 0.25-0.45 ms (treeWalk 5.84 -> 5.99 in
the clean run) and the frame did not move beyond noise. The update rise is not explained: the B1 code
runs only in the draw; the candidates are `ExtClean` (in update, it reads every Geometry's flag line,
which the old gather had partly left in cache) and code layout (ThinLTO + PGO on a changed TU). With
the render thread shortened (4.2), the same change measured on top of B2:

| run | build | fps | frame ms (p95) | update | draw | main / packet / emit | wait |
|---|---|---|---|---|---|---|---|
| 9 | B2 alone | 56.9 | 17.58 (24.60) | 8.83 | 7.59 | 4.30 / 3.30 / 4.77 | 3.84 |
| 10 | B2 + B1 (throttle event) | 58.1 | 17.20 (24.63) | 9.04 | 7.05 | 3.65 / 3.34 / 5.00 | 3.71 |
| 11 | B2 + B1 | 58.8 | 17.01 (19.77) | 9.07 | 7.08 | 3.68 / 3.35 / 4.72 | 3.89 |
| 12 | B2 alone | 56.7 | 17.67 (25.90) | 8.83 | 7.49 | 4.26 / 3.27 / 4.70 | 3.81 |

**Frame -0.5 ms, fps +1.6, p95 -1..-6 ms**: main -0.6 ms, update again +0.2 ms. Kept. B1's functions
run without PGO in B (the profile predates them), which works against B.

### 4.2 The pooled pixel set on the 1.1 path (B2): KEPT
Commit `perf: the Vulkan 1.1 path binds a pooled pixel set ...`. A/B on top of B1:

| run | build | fps | frame ms (p95) | update | draw | main / packet / emit | wait |
|---|---|---|---|---|---|---|---|
| 5 | B2 | 58.0 | 17.25 (20.38) | 9.08 | 7.10 | 3.71 / 3.37 / 4.88 | 3.93 |
| 6 | B1 | 53.8 | 18.56 (27.64) | 9.22 | 7.55 | 3.73 / 3.79 / 5.42 | 4.32 |
| 7 | B1 | 53.5 | 18.72 (28.09) | 9.36 | 7.51 | 3.73 / 3.78 / 5.34 | 4.28 |
| 8 | B2 | 58.1 | 17.22 (20.08) | 9.04 | 7.08 | 3.71 / 3.35 / 4.68 | 3.91 |

**Frame -1.4 ms (18.64 -> 17.24), fps 53.6 -> 58.0, p95 27.9 -> 20.2 ms**, emit -0.6 ms, packet -0.4 ms,
wait -0.4 ms; update and draw also fell ~0.2-0.4 ms. More than the 0.4-0.5 ms the push's own cost
predicted: the p95 drop says the push also made the render thread's frame-to-frame time irregular.
`[Vulkan]` frame line: pushes 241 -> 0, pixel binds ~230-244, sets allocated 14 -> 102 a frame, failed 0.
The render thread after it (profile of B2 + B1, 30 s): `IssueDraw` 14.3% of the thread (~0.6 ms a
frame, was 1.13), `vkUpdateDescriptorSets` 1.6%, `vkCmdBindDescriptorSets` < 0.1%.
- **Rendering**: screenshots at t = 30 s after RUNNING (frame 2308 on B2, 2187 on B1): the same image
  (terrain, water, rain, fog, buildings, UI); only the smoke plumes and rain streaks moved.
- The 1.3 path is untouched: Windows Vulkan (RTX 5080, path 1.3) logs "pixel set pushed, vertex set
  pooled", pushes 154, pixel binds 0.

### 4.3 Longer prefetch distances in the gather on ARM (B3): DROPPED
`kCleanAhead` 8 -> 16, `kDispatchAhead` 4 -> 8 with the material stages at 6 / 4, ARM only; prompted by the
B1 profile (4.5). On top of B2 + B1:

| run | build | fps | frame ms (p95) | update | draw | main / packet / emit |
|---|---|---|---|---|---|---|
| 13 | B3 | 58.8 | 17.00 (22.65) | 9.06 | 7.02 | 3.64 / 3.35 / 4.68 |
| 14 | B2 + B1 | 57.8 | 17.29 (21.20) | 9.06 | 6.97 | 3.62 / 3.31 / 4.64 |
| 15 | B2 + B1 | 58.0 | 17.24 (19.88) | 9.01 | 6.95 | 3.65 / 3.28 / 4.63 |
| 16 | B3 | 58.7 | 17.05 (21.55) | 9.08 | 7.04 | 3.62 / 3.38 / 4.68 |

The frame is 0.24 ms shorter but no component moved (update, draw, main, emit all within 0.03 ms): the
difference sits in the untimed remainder of the frame, not in the code changed. Not committed.

### 4.4 Walk leftovers and draw_batch (items 3 and 4): not prototyped
Phone time decided it: with the thermal pass's soaks the lock gave about one run per 20-25 minutes, and
each of these items is estimated at <= 0.1-0.2 ms, at or under what one ABBA resolves at the 60 cap (the
B2 + B1 town runs at 58-59 fps; a frame of ~17.2 ms against the cap's 16.7). `MoveTo::Enter` (0.46 ms a
frame, 3.5% of the main thread) is a CharacterGrid nearest-character query when a MoveTo starts: which
characters enter one is not known ahead in the walk, so it is not a prefetch target. Notes from the
profile for whoever takes them up are in 4.5.

### 4.5 Line profile of B2 + B1 (simpleperf, cpu-clock 2000 Hz, 30 s from t ~ 35 s, profileable build)
Main thread 13.1 ms on CPU a frame, render thread 4.1 ms.
- **Gather** (`ExtCulledGather` 11.6% + an out-of-line `BoxPasses` 5.9% of the main thread, ~2.3 ms a
  frame, was 3.16): the side record's box `memcpy` 19% and the node loop 22%; the live filter 7%; the
  dispatch's material stages (draw_gather.cpp, the vptr / `_materials` loads at i+3 and the array load at
  i+2) ~25%, i.e. their own demand loads still wait. B3's longer distances did not change the
  frame components, so the stages are measured as a whole (B1), not tuned.
- **`ExtClean`** (5.8%, ~0.76 ms): 61% on the `_dirtyFlags` read (retail's test, every live node every
  frame) despite the 8-ahead prefetch; 16 ahead (B3) did not show.
- **`MeshInstance::Update`** (4.4%, ~0.58 ms): ~65% in `AnimationState::Update`'s layer loop
  (wave27_lane_gfxbig.cpp 758-870), the first line (the layer array) 21%: a miss on the layer array the
  walk prefetch already names at i+1. Faithful math otherwise; no change proposed.
- **`ExtDrawItemList`** (render thread, 19.5%, ~0.8 ms): mostly the particle vertex fill
  (`WriteParticles`, batch_fill.h 96-130) and its memcpy (~65% together), the instanced loop's
  `GetInstanceTransform` call 9%. The instanced-loop prefetch of pass 1 covers what it was for; the
  particle fill is arithmetic over a dense array, not a miss.

### 4.6 Summary
A -> B2 + B1 (six B2 + B1 runs, 5 / 8 / 10 / 11 / 14 / 15): **frame 18.70 -> 17.20 ms (-1.5 ms, -8%), fps
53.5 -> 58.1, p95 27.4 -> ~20-24 ms**. The town now runs within ~0.5 ms of the 60 cap.
- x86: draw_gather's arms are `__aarch64__` constants; the desktop object of draw_gather.cpp compiled
  before and after (release flags without LTO / PGO) differs only in label numbers. The pixel-set path is
  taken only when `Vulkan11Path()`; the DX11 exe does not contain the backend.
- S25 (Vulkan 1.3): pushes unchanged (the 1.3 path); the draw_gather arms are ARM, so they run there,
  unmeasured on that phone.
- Windows checks on the final tree: unit 1558 / 0 / 3 skipped; the gate (debug, faithful profile, new
  game, 2 min, crash log 0); the resave gate PASS (golden md5 `971aaec2cf95f5f730bb1cd1a4fa8428`); the
  Vulkan backend (path 1.3, 90 s in game, crash log 0, failed draws 0).
- Next: retrain the Android PGO profile on this code (both B1 functions and `BindPixelSet` run
  unprofiled); the unexplained update rise with B1 (4.1) is worth a COMPPROF look at `ExtClean` then.
