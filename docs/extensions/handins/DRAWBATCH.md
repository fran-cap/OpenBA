# DRAWBATCH hand-in (2026-10-20)

Branch `drawbatch`, based on `gpuframe` 208c2a787. Not merged. The spec is
`gpuframe/ideas_batching.md`: Idea A (decals and roads) and Idea B (smoke).
**Result: `ext.draw_batch`, one class A row (live, no save effect), on by default like the other class A
rows. Both ideas are built. The perf exe and the unit suite build (unit 1200 / 0 failures / 1 skipped).
Static build only: nothing was run or measured. The A/B in section 6 is the coordinator's.**

## 1. What changed
| file | change |
|---|---|
| `src/engine/graphics/Camera.h`, `batchc_gfx_leaves.cpp` | THE engine seam. `Graphics::Renderer::_extDrawItemList` is a null-by-default static that `Renderer::DrawItemList` tests once at its top (one test per pass list, never per item). Retail's body below it is unchanged. |
| `src/ext/draw_batch/draw_batch.{h,cpp}` | The replacement body: retail's DrawItemList loop line for line, plus two merges (`MergeDecals`, `MergeSmoke`) tried at the top of each drawn item. `Sync()` sets or clears the seam and the VS stage. |
| `src/ext/draw_batch/batch_fill.{h,cpp}` | Pure helpers. `RetailAccepts` / `RunFits` apply retail's capacity tests plus the 65536-vertex cut. `FillRun` does the decal fill. `WriteParticles` writes the smoke quads. |
| `src/render/common/draw_batch/smoke_vs_rewrite.{h,cpp}` | Pure. The particle VS match and rewrite, built on `better_trees/dxbc.{h,cpp}`. |
| `src/render/common/draw_batch/smoke_vs_backend.h`, `src/render/dx11/draw_batch/smoke_vs_stage.{h,cpp}` | The D3D stage that holds the rewritten copies, in billboard_shadow's shape. Its seam is `VideoDX11::g_extDrawBatchVs`, tested in `VertexProgramData`'s dtor, `PlatformCreate` and `Bind` (`shaderprograms.cpp`, ahead of billboard_shadow's). |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | The row and its two `Sync` calls (boot, dialog toggle). |
| `tools/ext_icons/drawbatch.py`, `Entry_draw_batch.png`, `ExtensionsSprites.rsc`, `ext_sprites_data.h` | The dialog row icon. The PNG was written with zlib from the same tile and pictogram code because Pillow is not on this box. |
| `tests/unit/ext_draw_batch_test.cpp`, `sources.cmake` | 11 tests (section 5). |
| `docs/extensions/DESIGN.md` (Names table, seam row), `LIST.MD` | The two seam names, and the list row. |

Nothing serializes. No `getenv`, no flag and no instrument were added. No game content was added: the VS
copy is made at load from the user's installed byte code.

## 2. Idea A: decal and road runs
A run is at least two consecutive items, and every item in it must:
- be the same `WorldGeometry` (checked by dynamic type; the class is `final`);
- have the same mode (`_arg5 & 0xE000`, either 0x2000 or 0x4000), `_arg8 <= 1`, and the same
  `MaterialInstance` and `Material`;
- have no render texture;
- name a batch with vertices that retail's capacity test accepts on its own.

The run is cut where the merged request would fail retail's two capacity tests (including the sic test
of the index bytes against the vertex buffer's capacity) or pass 65536 vertices. For a run it does this:
1. `Setup` once.
2. `WorldGeometry::Draw`'s `SetTransform(0, lerp)` and `SetColor` once, the same three steps copied
   from `geometry_draw.cpp`.
3. One `BeginIndexedVertices`.
4. Every batch's vertex bytes in item order, and its indices plus the vertices before it.
5. One `EndIndexedVertices(0)`.

**Why bit-exact.** Faithfully, every item of the run runs `Setup` with the same instance, then
`SetTransform` / `SetColor` with the same values: one geometry, one `_alphaT`. After the first item those
calls change nothing (each setter compares before writing). The GPU then gets:
- the same state;
- the same vertex bytes in the same order;
- indices that name the same vertices;
- triangles in the same order. D3D11 rasterises and blends the primitives of one draw in submission
  order, so the blend over overlapping translucent footprints is the same sequence.

Only non-visual things differ: the dynamic-buffer byte offsets (when the ring discards), the Context's
draw / object counters, and the draw count.

## 3. Idea B: smoke runs
**The gate (passed before any B code).** In `gpu_allon_zoom130.rdc`, eid 6857 (vs575 / ps579):
- The layout is stride 52, with TEXCOORD0 R32G32B32A32_FLOAT at +0. So the dword at +0x0C is
  TEXCOORD0.w and the IA fetches it.
- The ISGN gives TEXCOORD0 four components (mask xyzw). The program declares only `dcl_input v0.xyz`.
- The instance id is read exactly once, `ishl r0.x, v4.x, l(2)`, which indexes
  `cb3[208] _TransformConstants_` (dynamicIndexed).
- The opacity is `cb3[r0.x + 3].w`.

So +0x0C does reach the VS as an input. Only the dcl mask and the ISGN read mask need widening; the
layout stays as it is.

**The rewrite** (`smoke_vs_rewrite`, matched structurally, no fingerprint). The match needs all of these:
- vs_4_x;
- an ISGN of exactly four float elements with masks F, F, F, 1, plus SV_InstanceID (uint);
- v0.w unread (the dcl and the read mask);
- a dynamically indexed cb of 208 float4;
- the instance id read once, by `ishl rT.c, vI, l(2)`.

The rewrite:
- replaces the `ishl` with three instructions: `ftou rT.c, v0.wwww` / `iadd rT.c, rT.cccc, vI` /
  `ishl rT.c, rT.cccc, l(2)`;
- declares `dcl_input v0.xyzw`;
- adds w to v0's ISGN read mask;
- re-emits the container with its checksum and self-checks the copy.

Retail's particle writer (`ParticleInstance::Draw` part C) stores the int 0 at +0x0C, which is 0.0f's
bits. With the copy bound, every draw that is not merged therefore computes retail's values (unit test:
index == iid * 4 for every iid). That is why the stage binds the copy for every bind of a matched
program.

**The merge.** A run is consecutive particle items of one material. The classes are
`ParticleInstance` / `ParticleSystemInstance`; both use Draw, GetInstanceTransform and the base's
SetSharedInstanceData, which returns 0. Each item has no render texture, and the pass's VS has its copy
for every variant (`SmokeVsBackend::Rewritten`). The steps:
1. The run is split into retail's instancing groups by retail's own rule (same instance, `_arg5`,
   `_userData` and `_arg10`, at most `_arg8`, never past the list's end).
2. Groups are added while the run uses at most 52 slots. Each group alone must pass retail's capacity
   test, and the run is cut at the capacity tests and at 65536 vertices.
3. `Setup` once.
4. Group g's members get `GetInstanceTransform` + `SetTransform` in slots s_g..s_g+n-1. A group of one
   gets the same matrix that Draw's `SetTransformScale(0, m, (1,1,1,opacity))` would build: same code,
   same order, `-ffp-contract=off`.
5. One `BeginInstancedIndexedVertices(V, 0x34, I, 1)`.
6. The head's particles are written once per instance, in instance order, by `WriteParticles`. That is
   part C's math: backward walk, four corners, the six 16-bit indices from the run's vertex offset +
   2, with k = s_g + j at +0x0C.
7. One End.

With one instance SV_InstanceID is 0, so the VS reads transforms[k]. A D3D instanced draw rasterises
all of instance 0 before instance 1, which keeps the triangle order retail's. The sort is untouched:
only consecutive items merge, so the back-to-front order stays.

**Limits and assumptions.**
- k stays at or below 51: the slot count, the 52-matrix staging and the banked cb rungs.
- The 16-bit base never wraps. A merged run has at most 65536 vertices, and an emitter big enough to
  wrap retail's base (over 16384 particles) is left to the copy.
- The particle pools are read once per run for the counts, then written. The faithful Draw re-reads
  `_liveCount` per particle; both read the same live pools on the same thread (the emit), so they agree
  while nothing writes the pools during the emit. Retail has the same exposure.
- Other particle programs (weather, stride 0x14) do not match the ISGN and keep the retail VS.
- A device change (device lost) releases every copy. They are re-made at the next bind, and until
  then `Rewritten` is false and nothing merges.

## 4. Toggle and threading
- Class A, live. The dialog toggle runs `Sync`. On: the seam is set and `SmokeVsBackend::Request(true)`
  is called. Off: the seam is cleared at once. The VS stage clears its own seam at the next VS bind,
  after releasing the copies. Until then a still-bound copy is harmless, because without the
  replacement body every vertex has k = 0.
- The replacement runs where DrawItemList runs: on the render thread with ext.render_pipe, otherwise
  on the main thread, never both at once. Its scratch (spans, groups, the type cache) is used only
  there. The VS stage's state is under one mutex, as billboard_shadow's is: one lock per vertex bind
  while on, and one per smoke run head.
- The faithful profile turns the row off. The engine then pays one null test per DrawItemList call and
  one per vertex-program bind.

## 5. Tests (`tools/build.sh unit perf`: 1200 / 0 failures / 1 skipped)
- `fill_is_the_concatenation_rebased`: every merged triangle corner reads the bytes its batch read alone.
  Nothing past the run is written.
- `the_largest_index_is_65535`, `retail_capacity_test`, `split_runs_cover_every_item_once`: the limits
  and the cut.
- `particles_are_retails_bytes_at_slot_0`: `WriteParticles` against the Draw banner's formulas,
  bit for bit, plus the backward walk and the index pattern.
- `particles_in_a_run_only_move_the_slot_and_the_base`: a run differs from retail only at +0x0C and
  by the index offset.
- `smoke_vs_match_and_meaning`, using our own vs_4_0 blob:
  - the copy parses, checksums and has two more instructions;
  - every other token is unchanged (only the dcl mask differs);
  - an evaluator gives index iid*4 at k = 0 and k*4 at k;
  - the copy does not match a second time.
- `smoke_vs_refusals`: v0.w read, v0 declared xyzw, iid read twice, shift not 2, cb not 208, a
  different signature, junk.
- `registry_row`: class A, on by default, off by `ext.draw_batch=0` and by the faithful profile.

## 6. How to A/B (the coordinator: the game dir is yours; nothing here was run)
**CPU, the verdict.** Run on a clean machine with the CPU as the bound. Use the 4096 sun map, so the GPU
is not the limit at zoom 130:
```
BANISHED_CAMERA_PIN=town,130 STRESS_EXT_VALUES=max_shadow_size=4096 \
  STRESS_EXT=<the all-on set without draw_batch> tools/build.sh stress      # arm X
BANISHED_CAMERA_PIN=town,130 STRESS_EXT_VALUES=max_shadow_size=4096 \
  STRESS_EXT=<the same set>,draw_batch tools/build.sh stress                 # arm Y
```
- The set must include `max_shadow_res`, because `max_shadow_size` is that row's value, and
  `render_pipe`.
- Run each arm 3 times.
- Read `draw_emit_ms` (the render-thread emit), `pipe_wait_us` (the main thread's hand-off wait) and fps.
- Expected: emit down 0.3-0.6 ms for A and as much again for B, and the wait down by about the same.
- `standalone_error.log` should show `[DrawBatch] installed`, `[DrawBatch] smoke VS request on`, one
  `rewrote a particle VS v<n>` per matched variant, and `first rewritten particle VS bind`.
  - If no `rewrote` line appears, the real program did not match (section 7). B is then inert and only
    A runs.

**Correctness.**
- `tools/trace_oracle/` X vs Y with a control pair: **S4 = 0, S6 = 0** (the strides stay 0x14 and 0x34),
  and **S3 = 0** after the control floor. S3 aligns draws by sequence; if it shows residue only on
  draw-count or index-count fields of the vs342 / vs575 groups, that is the merge itself (fewer,
  larger draws), not a finding.
- S1 / S2 should show the decal and smoke signatures with far fewer draws: vs342 / ps346 from ~307
  to ~2, vs575 / ps579 from ~244 to ~9.
- Decals are static between runs, so the ordered triangle lists of the vs342 draws (RenderDoc
  `get_buffer_data`) can be compared byte for byte; ideas_batching.md section 5 has the procedure.
- Smoke moves every frame: check the structure, per-run triangle counts and k in 0..51.
- The resave gate is untouched (nothing serializes) but should be run as usual.

## 7. Not done / open
- Nothing was run: not the game, not the gate, not the stress legs, not trace_oracle. The A/B and the
  correctness screens above are the coordinator's.
- **The match was checked on the real byte code, off-tree.** The VS byte code of eid 6857 (vs575) and
  eid 4508 (vs342, the decals) was dumped from the capture into the scratchpad with RenderDoc's Python
  module. A scratch driver ran `SmokeVs::Analyse` / `Transform` on it and created the copy on a WARP
  D3D11 device. The dumped blobs were deleted afterwards and are not in the tree.
  - vs575: matched; the copy is 2220 B -> 2268 B; `CreateVertexShader` accepts both the copy and the
    original (hr 0).
  - The copy's `D3DDisassemble` shows exactly `dcl_input v0.xyzw`, `ftou r0.x, v0.wwww` /
    `iadd r0.x, r0.xxxx, v4.x` / `ishl r0.x, r0.xxxx, l(2)`, and the ISGN "Used" column xyzw for
    TEXCOORD0. Every other line is the original's.
  - vs342 is refused ("input signature is not 5 elements").
  - Only the in-game binding path (the stage) is unexercised. The first run's `[DrawBatch] rewrote`
    line confirms it.
- `docs/runtime_port/STATUS.md` has no row yet (the coordinator's file).
- Idea C (wide instancing) was not in scope of this hand-in; section 8 has it.

## 8. Idea C: wide instancing batches (issue #253, Vulkan only)

What pins retail's batch at 52:
- the mesh level's `_maxInstances` (`GraphicsMesh::DetailLevel` +0xa8, from the game data), handed to
  `AddDrawItem` as `_arg8` (`src/engine/graphics/mesh.cpp` `MeshInstance::AddDrawItems`) and tested by the
  batch loop (`batchc_gfx_leaves.cpp` DrawItemList, this extension's copy in `draw_batch.cpp`);
- `Video::Context::_transforms[52]` (`src/engine/video/Context.h`, +0x10d8, 0xd00 bytes; retail layout, so it
  cannot grow) and the `TransformConstants` buffer `AssignConstants` declares as `transforms[52]`
  (`constant_buffer.cpp`, 208 float4, bank stride 0x40);
- `ContextData::GetMaxDynamicInstances` returns retail's literal 52 (DX11 and Vulkan) - only the dynamic-vertex
  writers ask it, not the batch loop.
Not a limit: the vertex shaders index the bank dynamically by the instance id, and dxbc-spirv declares every
dynamically indexed constant buffer at D3D's full 4096 float4 (`dxbc_resources.cpp` handleDclConstantBuffer),
so the SPIR-V needs no rewrite. On Vulkan the descriptor range was 16 KiB (256 matrices).

The change: `InstanceBank::Cap()` (`src/render/common/draw_batch/instance_bank.h`) is 52 on DX11 / Metal and
`kVulkanInstanceCap` on Vulkan (`src/render/vulkan/instance_bank.h`, a multiple of 52, at most 988 = 19 x 52 in
64 KiB, clamped to `maxUniformBufferRange`). The batch loop goes past a retail cut only where the retail batch
starting there would draw the same instances with the same state (`batch_fill.h` `ContinuesPastCut`: an exact
`MeshInstance` head and cut item with a rigid list, no render texture, the same join keys, `_arg8` and opacity
pair, and a retail batch of at least two there); slots 52.. go to the Vulkan `ContextData::wideBank` and
`BindTransforms` uploads the whole bank; every constant-buffer descriptor's range becomes
`max(16 KiB, cap x 64)`. At `kVulkanInstanceCap = 52` the path is today's: the cut test fails at once and the
range stays 16 KiB. Skinned instanced lists (`_f14 < 0`, citizens) and particles keep retail's batches.

Checks: COMPPROF=4 `DrawBatchWide` expands every list as drawn and with retail's batches and compares, per
item, how it was drawn, the head's state and the slot's transform. The COMPPROF `draw batch` rows carry the
`seam` break and `at cap 52/104/208/988`, the draws the same lists take at those caps (from the retail
batch chains, whatever the build's cap). Windows, RTX 5080, Vulkan debug with validation, stress town 3000
frames: `DrawBatchWide` 0 mismatches at 52 and 988 (default zoom and zoom 200); no validation error (the one
warning, a depth-only pass's unused `SV_Target`, is the same at 52). Far zoom (`town,200`), per frame:
main 362 / 222 / 151 / 94 draws at 52 / 104 / 208 / 988, shadow 358 / 218 / 147 / 90, refl 312 / 173 / 103 /
46, for about 15.9 k instances per pass. Timing is the phone bench's (`docs/android/I253_RENDER.md`).
