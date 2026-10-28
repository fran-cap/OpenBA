# Android P13: the render thread on the Galaxy S10 (Vulkan 1.1 path)

The render thread (`ext.render_pipe`'s emit: pass 2, the post-scene draws, EndCommands, Present) profiled
on the Galaxy S10 (SM-G973U, Snapdragon 855, Adreno 640, Android 12, Vulkan 1.1.128: the backend's 1.1 path,
`src/render/vulkan/vulkan11.h`), then a ranked plan. Findings first; the prototypes and their A/Bs are in §4.

## 1. How it was taken
- Build: `tools/android.sh build release --jobs 4` (release flavour, PGO from `perf/pgo/android/openba.profdata`,
  ThinLTO), `<profileable android:shell="true"/>` added to the manifest for this build only (not committed),
  signed with the debug key.
- Run: `tools/android.sh run BANISHED_STRESS=1 BANISHED_FPS_CAP=60` (800 citizens, speed 10), battery 29.5 C,
  thermal status 0 at the start. The P12 pin is a no-op here (one CPU of the top capacity).
- Profile: `simpleperf record --app org.openba.game -e cpu-clock -f 2000 --call-graph fp --duration 30` from
  about t=10 s to t=40 s of the stress run (~1100 frames at 35-44 fps); symbolised against the unstripped
  `libmain.so`; per-thread and per-CPU splits through `simpleperf_report_lib`.
- GPU: `/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage` and `clock_mhz` once a second over the same 30 s.

## 2. What the frame is (stress log, the profiled run)
`[Stress]` fps 35-44, frame 22.7-29.1 ms (p95 33-35), update 9.1-10.2 ms, draw 8.8-9.8 ms;
`[Stress::pipe]` draw main 4.2-4.6, packet 4.6-5.3, emit 7.5-8.3 ms, main-thread wait 6.2-6.8 ms a frame,
latency 7.8-8.5 ms; `[Vulkan]` ~390 draws, 4 passes (+ present), 14 descriptor sets allocated and ~225 pushes
a frame, 0.8 MB of constant ring.

At speed 10 the fence joins the emit before the sim steps, so the main thread waits for the render thread
every frame: **the render thread's time up to the Release is on the frame's critical path**, and a
millisecond off it is close to a millisecond off the frame here (it is power, not frame time, where the
main thread is the slower side, as on the S25).

## 3. The profile

### 3.1 Threads (CPU seconds in the 30 s window, 36.5 s in all)
| Thread | Share | Per frame | CPUs |
|---|---|---|---|
| main (sim + render_pipe front half) | 59.8% (73% of a core) | ~19.8 ms | gold 4-6 (98.6%), prime 7 1.4% |
| render thread (the emit) | 20.6% (25% of a core) | **~6.8 ms CPU** | gold 4-6 (97%), silver 1.4%, prime 0.6% |
| path worker | 13.1% | - | - |
| everything else | 6.5% | - | - |

The prime core (cpu7, 2.84 GHz) is nearly idle: the scheduler keeps both critical threads on the gold
cluster (2.42 GHz).

### 3.2 Render thread, by where the time is spent (leaf DSO)
libmain 42.1%, kernel 22.5%, the Adreno driver (`vulkan.adreno.so`) 21.1%, libc 10.2%, libgui / binder /
other 4%. **The driver plus the kernel under it is 44%**: the render thread is dominated by driver calls,
not by our recording code.

### 3.3 Render thread, inclusive (each sample charged to the innermost libmain function; 15043 samples)
| Function | Share | ms / frame | What it is |
|---|---|---|---|
| **vkCreateFramebuffer + vkDestroyFramebuffer** | **22.0%** (9.75 + 12.28) | **~1.5** | the 1.1 path makes a VkFramebuffer per pass every frame and retires it; the Adreno driver backs each one with kgsl shared memory: `ioctl_kgsl_sharedmem_alloc` / `_free` (kernel 10.9% under CollectRetired alone). ~5 a frame, ~0.3 ms each |
| `ExtDrawItemList` self | 17.8% | ~1.2 | 40% of it (7.1% of the thread, ~0.5 ms) on ONE load: the vtable load of `cur._geometry` for the virtual `GetInstanceTransform` in the instanced loop (`draw_batch.cpp:424`): a cache miss per instanced item on the scattered MeshInstance objects |
| `IssueDraw` (driver + memcpy under it) | 16.5% | ~1.1 | ~90% of it `vkCmdPushDescriptorSetKHR` (the driver copies the writes into the command stream: `__memcpy`); ~225 pushes a frame, ~4 us each |
| driver / kernel with no libmain frame | 10.8% | ~0.7 | unwound frames lost in the driver (no frame pointers); most of it sits beside the push and framebuffer calls |
| PresentFrame | 5.1% | ~0.35 | queueBuffer, BLAST transaction: system cost |
| skinning: `GetInstanceTransform`, `Quaternion::Lerp`, `GetBones`, `Sqrtf`, `SkinMemo::Bones` | ~11.6% | ~0.8 | faithful engine math (SkinMemo already de-duplicates) |
| BeginCommands, SubmitCommands (fence per submit) | 4.5% | ~0.3 | command-pool reset, vkQueueSubmit ioctl |
| BindResources, SetTransform, Setup, Bind* | ~4% | ~0.3 | the recording itself |

Not found to be costly: render-pass lookups (`PassForLocked` below 0.1%), the barrier folding
(`Vulkan11Detail::PipelineBarrier` not in the top 60), pipeline / vertex / index binds (already
de-duplicated in `IssueDraw` / `BindStreams`; `vkCmdBindVertexBuffers` 1.2% self), the constant ring's
writes (`ResolveConstants` 0.3%, `ConstantBufferData::Bind` 0.3%), descriptor-set allocation
(`vkAllocateDescriptorSets` 0.2%: 14 sets a frame, the vertex sets are cached per slot).

### 3.4 Is the frame GPU-bound? No.
GPU busy 58-76% (mostly 65-71%) at **345 MHz**, with 427 and 585 MHz available and `thermal_pwrlevel` 0:
the governor holds a low clock because the GPU has slack. The frame is CPU-bound: the main thread's
~20 ms of work plus its ~6.6 ms wait on the render thread.

## 4. Ranked plan (expected savings from the profile, to be confirmed by A/B)
1. **Keep the 1.1 path's framebuffers across frames** (`vulkan11.cpp`): cache them by (pass, views, size)
   for as long as their views live, and forget a view's framebuffers when the view is destroyed (the only
   reason they were per frame: a destroyed view's handle can come back). Expected **-1.3 to -1.5 ms** of
   render-thread CPU a frame, both halves before the Release, so about the same off the main thread's wait.
   1.1 path only: the 1.3 path (S25, desktop) records dynamic rendering and never reaches the cache.
2. **Prefetch the instanced loop's next geometries** (`ext/draw_batch`): touch the MeshInstance a few items
   ahead so its vtable / transform lines are in cache when `GetInstanceTransform` is called. Expected
   **-0.2 to -0.5 ms**. Shared code (desktop too): a prefetch changes no result; it costs a few
   instructions per item where the data already hits.
3. **The pixel set's push** (`draw.cpp PushPixelSet`): ~1.1 ms + part of the unattributed driver time. The
   alternative (a cached set with a dynamic constant-buffer offset, as set 0 already does) trades a push for
   a bind, which the Adreno driver also copies (P7: `vkCmdBindDescriptorSets` 19% on the S25 before pushes).
   Uncertain; tried only after 1 and 2, and kept only on a clear A/B.
4. **The main thread on the prime core** (platform, `perf_hint_android.cpp`): the S10 has one prime CPU and
   it idles; the main thread (the critical path, 73% busy) runs on gold at 2.42 GHz. Pinning main alone to
   the single largest-capacity CPU when there is exactly one (the render thread left to the scheduler) is a
   platform change outside the render thread; the S25 (two prime CPUs) keeps its pair path untouched.
   Expected up to -2 ms of main-thread work if it scales with the clock (P12 found the draw work
   clock-insensitive, the sim not). Measure, do not assume.
5. Not planned: the skinning math (faithful), Present / submit (system), the render-pass and barrier code
   (not visible in the profile).

## 5. Results (S10, release + PGO, debug-key signed, not profileable; ABBA)
Every run: `tools/android.sh run BANISHED_STRESS=1 BANISHED_FPS_CAP=60`, 6000 stress frames (100 s), battery
<= 32.0 C and thermal status 0 at the start (31.5-32.0 C), the app stopped between runs. Means of the
`[Stress]` windows t=20..100 s and of the `[Stress::pipe]` windows after the first. Battery at the end
32.4-33.7 C; thermal status never left 0.

| run | build | fps | frame ms (p95) | update | draw | pipe main / packet / emit | main wait | latency |
|---|---|---|---|---|---|---|---|---|
| 1 | A base | 35.7 | 28.05 (34.75) | 10.00 | 9.58 | 4.37 / 5.21 / 8.03 | 6.65 | 8.27 |
| 2 | B1 framebuffers | 44.6 | 22.54 (30.78) | 9.70 | 8.66 | 4.22 / 4.44 / 6.12 | 5.02 | 6.36 |
| 3 | B1 framebuffers | 49.0 | 20.44 (29.43) | 9.15 | 8.50 | 4.29 / 4.21 / 5.79 | 4.71 | 6.03 |
| 4 | A base | 35.5 | 28.23 (34.71) | 10.06 | 9.55 | 4.36 / 5.19 / 7.95 | 6.63 | 8.19 |
| 5 | B2 = B1 + prefetch | 51.3 | 19.59 (28.51) | 9.47 | 7.77 | 4.10 / 3.66 / 5.20 | 4.18 | 5.42 |
| 6 | B1 | 44.8 | 22.43 (30.51) | 9.69 | 8.62 | 4.21 / 4.42 / 6.09 | 4.99 | 6.32 |
| 7 | B1 | 44.5 | 22.64 (31.00) | 9.76 | 8.61 | 4.19 / 4.42 / 6.14 | 5.00 | 6.37 |
| 8 | B2 | 51.4 | 19.58 (27.98) | 9.45 | 7.78 | 4.14 / 3.64 / 5.19 | 4.15 | 5.41 |
| 9 | B3 = B2 + main on prime | 51.0 | 19.69 (28.44) | 9.44 | 7.84 | 4.16 / 3.68 / 5.28 | 4.20 | 5.51 |
| 10 | B2 | 51.2 | 19.61 (28.53) | 9.42 | 7.83 | 4.15 / 3.68 / 5.26 | 4.20 | 5.48 |

- **Framebuffers kept (B1 vs A): KEPT.** Emit -1.9 ms, packet -0.8 ms, main-thread wait -1.6 ms; fps 35.6 ->
  44.6-49.0, frame -5.6 to -7.7 ms. The frame gains more than the emit loses: frame - update - draw fell
  from ~8.5 to ~4 ms; not attributed (the fence's wait before the steps is not inside update / draw; a
  kernel-side contention of the kgsl alloc / free with the main thread's own driver calls is possible,
  not shown).
- **Instancing prefetch (B2 vs B1): KEPT.** Packet -0.77 ms, emit -0.9 ms, main draw -0.85 ms, fps 44.6 ->
  51.3 (both B2 runs above all three B1 runs). Twice the profile's estimate: GetInstanceTransform's own
  loads hit as well.
- **Main thread alone on the prime core (B3 vs B2): DROPPED.** `[Pin] main thread ... -> CPUs 0x80:
  pinned`, cpu7 uncapped (2841600 kHz max), and nothing moved (update 9.44 vs 9.42 ms). Why the 17% clock
  did not show was not investigated (cpu7's clock during the run was not sampled).
- B2 in all: **fps 35.6 -> 51.3 (+44%), frame 28.1 -> 19.6 ms, emit 8.0 -> 5.2 ms, render-thread CPU
  ~6.8 -> ~3.9 ms a frame** (the B2 profile: 11846 samples in 30 s at ~51 fps). GPU busy rose from 66% at
  ~370 MHz to 73% at ~430 MHz (runs 1-4); 585 MHz still unused, so still not GPU-bound.
- **Rendering**: frame-matched screenshots (`adb exec-out screencap`, real frame 1524 on A, 1501 on B2: at
  speed 10 the same frame is the same game time on any build) show the same image: lighting, weather,
  water, shadows, UI. (A first pair taken at the same wall time differed in weather: B2 had run more
  frames, so more game time.) The Windows Vulkan build (`BANISHED_RENDER_BACKEND=vulkan tools/build.sh
  run`, the 1.3 path, debug) boots to a game: 228 draws, 4 passes at frame 900, crash log empty.

### 5.1 The render thread after B2 (simpleperf, same method, 11846 samples)
`IssueDraw` 26% (~1.0 ms): `vkCmdPushDescriptorSetKHR` 57% of it (~0.58 ms a frame, ~2.8 us a push, ~210
pushes), `vkCmdDrawIndexed` 10%, `vkCmdBindVertexBuffers` 7%, `vkCmdBindIndexBuffer` 6%;
`ExtDrawItemList` self 18% (~0.7 ms, was ~1.2); PresentFrame 7%; skinning (Lerp, GetBones,
GetInstanceTransform, SkinMemo, Sqrtf) ~14%; SubmitCommands 3%; BeginCommands 3%. Framebuffer calls gone.
`vkCmdBindDescriptorSets` (the vertex set, pooled, dynamic offsets) is 14 samples, 0.1%.

## 6. Next
1. **The pixel set as a pooled set with dynamic constant-buffer offsets** (as set 0), on the 1.1 path at
   least: the profile prices a push at ~2.8 us and the vertex set's bind at almost nothing on this driver,
   so up to ~0.5 ms a frame. It needs a non-push set-1 layout per pair, a per-slot set cache keyed by the
   pixel stage's buffers / views / samplers (as `VertexSetKey`) and its allocation + write cost measured
   (P7 found the opposite trade on the S25's 1.3 driver, so keep it to the device where it pays).
2. The prime core is idle on the S10 and pinning the main thread to it changed nothing: sample cpu7's
   clock in such a run before ruling it out.
