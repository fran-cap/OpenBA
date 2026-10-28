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

## 4. Results
(filled in as the A/Bs land)
