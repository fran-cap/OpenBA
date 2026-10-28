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
