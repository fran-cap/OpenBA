# Android P13 pass 2: the Galaxy S10 profiled end to end, after the PGO retrain

The combined build of pass 1 (`P13_RENDER.md`: the 1.1 path's framebuffer cache and the draw_batch
prefetch; `P13_WALK.md`: the ARM MoveTo prefetch) with the Android PGO profile retrained on it, measured
and profiled on the Galaxy S10 (SM-G973U, Snapdragon 855, Adreno 640 on the backend's Vulkan 1.1 path,
Android 12, 7.6 GB). Numbers first, then a ranked list of targets for pass 3 (section 8). No code changed
in this pass apart from the profile; raw logs, profiles and scripts are in the session scratchpad.

## 1. Builds and protocol
- **PGO retrain** (`perf/pgo/android/README.md`, unchanged recipe): `build release --pgo generate`, the 10x
  town (3000 frames, StorageComponent held from frame 1200) and the 50x town (3000 frames, MarketComponent
  held from frame 1500), both ended `EXITED clean-shutdown`, `[PGO] profile written (0)`; `pgo-merge`: 11551
  functions, 88429 blocks. The release `.cxx` dir was removed before the rebuild; every compile line carries
  `-fprofile-use=.../openba.profdata`.
- **Measured build**: `tools/android.sh build release` (-O3, ThinLTO, the new profile), signed with the
  debug key, not profileable.
- **Profiling build**: the same tree plus, for that build only and never committed,
  `<profileable android:shell="true"/>` and a counter of the BC decode's output (`[P13BC]`, section 6).
- **Stress runs**: `tools/android.sh run BANISHED_STRESS=1 BANISHED_FPS_CAP=60` (6000 frames, speed 10,
  ~803 citizens), battery <= 32.0 C and thermal status 0 at the start, the app stopped between runs, screen
  kept on as configured. Means of the `[Stress]` / `[COMPPROF]` windows t = 20..100 s and of the
  `[Stress::pipe]` windows after the first. A sampler read the GPU busy / clock, the three clusters' clocks,
  the busiest threads' CPUs and core_ctl's isolated count every ~6 s.
- **Profiles**: NDK r30 `simpleperf record --app org.openba.game -e cpu-clock -f 2000 --call-graph fp
  --trace-offcpu --duration 30` (whole app, t ~ 35-65 s of the stress run), read in `on-off-cpu` mode;
  each sample charged to its innermost `libmain.so` function, inlined frames split with
  `llvm-addr2line -i`. Kernel frames do not symbolise as the shell user (no kallsyms): off-CPU time is
  attributed by the innermost libmain function that blocked, not by the kernel path.

## 2. Baseline of the combined build (release + new PGO)

| run | start / end C | fps | frame ms (p95) | update | treeWalk | draw | pipe main / packet / emit | wait | latency |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 28.0 / 30.4 | 54.0 | 18.57 (27.22) | 9.09 | 5.74 | 7.70 | 4.08 / 3.62 / 5.13 | 4.13 | 5.36 |
| 2 | 30.5 / 31.9 | 53.3 | 18.85 (26.61) | 9.16 | 5.77 | 7.75 | 4.08 / 3.67 / 5.32 | 4.18 | 5.54 |
| 3 | 31.9 / 33.1 | 53.6 | 18.71 (26.78) | 9.14 | 5.76 | 7.72 | 4.07 / 3.66 / 5.19 | 4.17 | 5.41 |

Thermal status 0 at every start and end. buckets 1.43 ms, 13,961 walk nodes a frame.
- Against the last measured builds (other sessions, so indicative, not an A/B): pass 1's B2 51.3 fps,
  update 9.45 ms; p13walk's MoveTo build treeWalk 6.15 ms / update 9.78 ms, whose walk ran without PGO.
  With the retrained profile treeWalk is **5.75 ms (-6.5%)**, update 9.13 ms, **fps 53.6**.
- Load: `gul.load.done` at 11.4-11.7 s after process start (pass 1: 12.2-12.4 s).

## 3. The main thread: the frame's limit, with the render thread's emit serial on it

Profile (30 s, 1611 frames at 53.7 fps):

| thread | on-CPU per frame | share of a core | CPUs (on-CPU time) |
|---|---|---|---|
| main (`SDLThread`, sim + render_pipe main half) | **14.3 ms** | 77% | gold 4 35%, 5 37%, 6 26%; prime 7 2% |
| render thread (the emit) | **4.4 ms** | 24% | gold 4-6 99%, cpu7 1% |
| path worker (`GridPathSearch::FindPath`) | 3.2 ms | 17% | gold 4-6 |
| everything else | < 0.15 ms each | | |

**Main-thread off-CPU time: 4.62 ms a frame, of which 4.45 ms (96.4%) is inside `Ext::RenderPipe::Hook`**:
the fence before the steps and the tail's join, both waits on the render thread. The frame limiter's sleep is
0.08 ms (1.7%); every other off-CPU site is under 0.03 ms a frame. The main thread spends **0.0%** of its
on-CPU time in the Adreno driver (kernel 3.6%, libc 1.1%: page faults and syscalls), so it makes no driver
calls that could contend with the render thread's.
- **Verdict: the kgsl-contention theory is dead.** The "frame - update - draw" that fell from ~8.5 to ~4 ms
  with the framebuffer change was the main thread's wait on the render thread's emit (the emit got 1.9 ms
  shorter), not kernel contention. Today frame - update - draw is 1.9 ms.
- **The frame is 14.3 ms of main-thread work plus 4.45 ms of waiting on the emit up to the Release**
  (14.3 + 4.45 = 18.75 against 18.7 measured). At speed 10 the fence makes the two serial: a millisecond off
  either the main thread or the render thread's pre-Release emit is a millisecond off the frame.

Main thread on-CPU, by innermost libmain function (ms a frame):

| function | ms | note |
|---|---|---|
| `EntityList::UpdateWalkBatch` (the walk, inlined ticks + prefetch) | 4.00 | treeWalk 5.75 incl. out-of-line ticks |
| `Ext::DrawGather::ExtCulledGather` (cull walk + AddDrawItems) | **3.16** | section 3.1 |
| `Ext::DrawGather::ExtClean` (Scene::Clean + side upkeep) | **0.91** | |
| `Graphics::MeshInstance::Update` | 0.61 | |
| DrawSort (`SortFastSwitch`, `SortFastPath`, introsort) | 0.79 | |
| `MoveTo::Enter` | 0.42 | p13walk's open prefetch item |
| `EntityList::PostUpdate` | 0.37 | |
| `CharacterComponent::Tick` | 0.37 | |
| Geometry::UpdateAttached 0.20, ModelComponent::Tick 0.19, AnimationManager::Update 0.17, ParticleSystemInstance::Update 0.14, StdEvent::Set 0.14 | | |

### 3.1 Inside the gather (3.16 ms a frame, `llvm-addr2line -i` of its samples)
- 27% on one line, `draw_gather.cpp:182`: `geo->_materialTypeFlags & f.passMask` and `geo->_detailId`
  read from the Geometry when the side record's pass hint is inconclusive: a cache miss on the scattered
  geometry objects (~0.85 ms).
- 13% at `wave9_lane_gfx.cpp:342` (`Renderer::AddDrawItem`'s `material->_shaderClass`): a miss on the
  MaterialInstance (~0.4 ms); `MeshInstance::AddDrawItems` 8%, memcpy 8%.
- The frustum tests themselves (`FrustumCull_ContainsFast` + `_Test`) are 12%.
- `[GatherThreads] 0 helpers on 4 physical cores: sequential walk`: the S10 runs no gather helpers.

## 4. The render thread after pass 1

4.4 ms on CPU a frame (by leaf: libmain 49%, Adreno driver 25%, libc 14%, kernel 7%), off-CPU in
`Handoff::WaitJob` (idle, 92.7%) and **`PresentFrame` 0.98 ms a frame** (blocked in `QueuePresentKHR`,
after the Release, so not on the frame's critical path at this town).

| innermost libmain function | share | ms / frame | below it |
|---|---|---|---|
| `ExtDrawItemList` (draw_batch) | 28.7% | **1.27** | 99% self: the instanced loop with the skinning math inlined |
| `IssueDraw` | 25.4% | **1.13** | `vkCmdPushDescriptorSetKHR` 14.7% (**0.65 ms**), DrawIndexed 2.6%, BindVertexBuffers 1.7%, BindIndexBuffer 1.6% (0.15 ms together), BindPipeline 0.5%, self 3.3% |
| no libmain frame (driver / kernel, unwound) | 8.2% | 0.36 | |
| `PresentFrame` | 6.9% | 0.31 | `QueuePresentKHR` |
| `MeshInstance::GetBones` | 3.2% | 0.14 | |
| `BeginCommands` | 3.1% | 0.14 | vkResetCommandPool 1.4%, AcquireNextImage 1.2% |
| `SubmitCommands` | 3.0% | 0.13 | `QueueSubmit` |
| `MeshInstance::Draw` 2.7%, `BindResources` 1.6%, `SkinMemo::Bones` 1.4%, atomics 1.3%, `SunLight::DrawLight` 1.1% | | | |

The push is ~2.8 us x ~230 a frame, as pass 1 found. Framebuffer calls: none.

## 5. CPU placement, clocks, GPU
- **Clocks during the baseline** (17 samples a run): gold (cpu4-6) 2.22-2.25 GHz mean (max 2.42), silver
  1.68-1.77 GHz, prime cpu7 825-913 MHz (idle). GPU **75-77% busy at 482-491 MHz** (510 MHz during the
  profile), 585 MHz unused, `thermal_pwrlevel` 0.
- **core_ctl**: cpu7 isolated in every sample; **cpu6 isolated too in 3-4 of 17 samples a run (~20%)**
  (`global_state` mid-run: cpu4-5 "Active CPUs 2", cpu6-7 isolated). In those moments the three busy
  threads (main 77%, render 24%, path worker 17%) share two gold CPUs. The main thread's off-CPU time
  outside the Hook is < 1%, so the cost of that sharing is not visible in the 30 s profile; it may be in the
  render thread's wake-up latency (not measured).
- **The GPU is not the limit.** At ~490 MHz it is ~76% busy at 18.7 ms frames, ~14 ms of GPU time a frame,
  ~12 ms at 585 MHz: a GPU ceiling near 80 fps if it scales with the clock (assumed). At the 60 cap the CPU
  frame would need to fall ~2 ms before the GPU even needs its top clock.

## 6. Memory

| | PSS (meminfo) | Graphics (meminfo) | GPU pages (kgsl `page_alloc`, app's share) |
|---|---|---|---|
| main menu | 295 MB | 107 MB | **~590 MB** |
| stress town, mid-run | 417-423 MB | 158-162 MB | **~620 MB** |
| normal play (1x town) | 392 MB | 140 MB | **~657 MB** |

- **`dumpsys meminfo` under-reports the GPU by ~4x on this driver.** The app's GPU memory is the
  system-wide `/sys/class/kgsl/kgsl/page_alloc` with the app running minus with it stopped (127-130 MB):
  717.6 -> 127.4 MB at the menu, 747 MB in the stress town, 787 MB in normal play. Pass 1's "PSS 443 MB" is
  not the footprint: the app holds **~0.9 GB** (PSS outside Graphics ~265 MB + ~620-660 MB of GPU pages).
- **The BC decode** (`[P13BC]`, profiling build only): one stress run decodes **125 textures (77 BC1, the
  rest BC3 / format 2) into 397 MB of RGBA8**, from ~66 MB of BC data (6x): 240 MB of it by the main menu
  (mostly 1024x1024, two 2048x2048 BC3), 157 MB more for the town.
- **They are resident**: with `ext.texture_quality=1` + `ext.texture_quality_drop=1` (Half, an existing
  row) the decoded total fell to 162 MB (-235 MB) and the GPU pages to 519 MB (-228 MB).
- **Avoidable: ~330 MB**, about half of the app's GPU memory: the same textures transcoded at load to a
  format the Adreno 640 samples at BC's size (ETC2 RGB8 = 4 bpp as BC1, ETC2 RGBA8 / ASTC 4x4 = 8 bpp as
  BC3) would hold ~66-69 MB instead of 397 MB. A load-time transform of the user's own data (AGENTS.md
  rule 1 holds); see 9.2.
- **Load time did not grow from the decode**: in the profile of the town load the decoders
  (`BcDecode::DecodeBC1/3`) are 97 ms of CPU on the two async-create workers for the town's 157 MB, about
  1.6 GB/s; the load is 11.4-11.7 s.
- **A limit or a cost?** On this phone a cost: during normal play the system had 235-312 MB free with
  1.26 GB in swap (other apps cached), and the app was never killed. On a 4-6 GB phone a ~0.9 GB game is a
  likely low-memory-killer victim in the background and pushes everything else out; 330 MB is a third of
  the footprint.

## 7. Thermal: a 20-minute capped soak

Same stress town, `BANISHED_STRESS_FRAMES=66000`, cap 60, sampler every ~6 s (plus the thermal status, the
caps and `thermal_pwrlevel`); start 31.9 C. **Caveat: the stress town grows over 20 minutes (803 -> 1219
citizens), so part of the late slowdown is workload**; the clocks separate the two.

| t (s) | fps | frame ms (p95) | update | draw | citizens | gold cur / cap MHz | main thread on | GPU MHz / cap | battery C |
|---|---|---|---|---|---|---|---|---|---|
| 10 | 58.9 | 17.0 (18.0) | 8.28 | 7.16 | 803 | 1920-2227 / 2419 | gold | 499 / 585 | 31.9 |
| 70 | 50.7 | 19.7 (29.2) | 9.52 | 7.80 | 803 | 1920 / 2419 | gold | 427 / 585 | 33.1 |
| 130-190 | 46-47 | 21-22 (30) | 10.1 | 7.8 | 954 | 2227-2419 / 2419 -> 2323 | gold | 427 / 585 -> 427 | 34.0 |
| 250 | 30.1 | 33.3 (45.6) | 11.48 | 9.45 | 952 | 710-1708 / 1708 | **prime (cpu7 1.4-1.7 GHz)** | 257 / 427 | 34.6 |
| 311-371 | 21-23 | 43-47 (52-54) | 12.4 | 11.0 | 1010 | **710** / 1708-2016 | prime | **257** / 427 | 34.5 |
| 431-491 | 35.6 | 28.1 (37.2) | 11.3 | 8.3 | 1027-1045 | 2419 / 2419 | gold | 345 / 585 | 34.5-35.0 |
| 551-1131 | 21.5-28 | 44-46 (54-56) | 12.2-13.7 | 10.4-11.2 | 1063-1219 | **710** / 2016 | prime (1.6-2.0 GHz) | **257** / 499 | 35.1-35.3 |

- **Throttling starts at ~155 s** (battery 34.1 C): the GPU cap drops 585 -> 499 MHz (`thermal_pwrlevel` 1),
  then 427 MHz and the gold cap 2419 -> 2323 -> 1708 MHz by 243 s. **Android's thermal status stays 0 for
  the whole 20 minutes**: the throttling is the vendor thermal engine's, below anything the
  `PowerManager` thermal API reports.
- **The cost is far larger than the caps.** fps 46-47 -> **21-23** (-53%) while the caps only took 17-30%
  off gold and 15-27% off the GPU. What happens: when gold is capped, core_ctl un-isolates cpu7 and the main
  thread migrates there (1.4-2.0 GHz); the render thread is left alone on the gold cluster at ~25%
  utilisation, schedutil drops gold to its **minimum, 710 MHz**, the emit (on the critical path) doubles
  (`[Stress::pipe]` emit 5.2 -> 8.4 ms, fence wait 4.2 -> 7.1 ms), and the GPU, now ~56-60% busy, falls to its
  minimum **257 MHz**. Each stage looks under-used to its governor because the stages are serial. When the
  caps lifted (t ~ 420-500 s) the main thread went back to gold and fps returned to 35.6 at 1030 citizens.
- In the throttled windows, frame (45 ms) - update (13) - draw (11) - fence wait (7) leaves ~14 ms not in any
  timer. Not profiled here (the soak ran the non-profileable build); the tail join waiting on a Present that
  waits on a GPU at 257 MHz is the obvious candidate, **unverified**.
- Battery at the end: **35.3 C**, GPU 52-54 C; thermal status 0.

## 8. Normal play (`BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town`, 1x, ~150 s window)
- **60 fps locked**: SurfaceFlinger timestats, 9185 presents, 0 dropped, 0 janky, present-to-present 16 ms
  for 99.7% (13-19 ms for the rest); the display at 60 Hz (`frameRate = 60.00` vote).
- CPU per thread (`top -H`, 60 s intervals): **main 33% of a core (~5.6 ms a frame), render thread 26%
  (~4.4 ms a frame)**, everything else < 3%. The render thread costs the same as in the stress town: it scales
  with the scene on screen, not the speed.
- Clocks: gold 2.19 GHz mean, silver 0.97 GHz, prime 826 MHz (isolated); GPU 67% busy at its minimum
  257 MHz. Ample headroom everywhere.
- **Latency**: post-to-present 38-42 ms for 98% of frames (desired-to-present the same): the app queues
  ~2.4 frames ahead (4 swap-chain images). Input-to-photon, not throughput; noted for the P12 survey's
  "2 frames in flight" item.
- Battery 32.1 -> 32.4 C over the window.

## 9. Ranked targets for pass 3

Gains are for the stress town at speed 10 (frame 18.7 ms, main 14.3 + emit wait 4.45), estimated from the
profiles; each needs its A/B. Where the frame is serial (section 3), main-thread and pre-Release emit
savings both count one for one.

### 9.1 Sim-faithful-safe (backend / platform / prefetch / layout; results identical)
1. **ADPF performance-hint sessions for the main and render threads** (`APerformanceHint`, API 31+, the
   P12 survey's first open item; target = the cap's frame time, actual = each thread's work per frame).
   Expected: in sustained play, keep the render thread's cluster and the GPU off their minimum clocks once
   the thermal engine caps them: the soak's 21-23 fps against the 35.6 fps of the same town with gold
   uncapped bounds it at roughly **+50% sustained fps** (unverified; the caps themselves stay). Before
   throttling, possibly a small gain from gold 2.24 -> 2.42 GHz. Platform only.
2. **The pixel set as a pooled set with dynamic constant-buffer offsets on the 1.1 path** (p13render's
   item): the push is 0.65 ms a frame on the critical path; **-0.4 to -0.5 ms**, provided the bind is as
   cheap as the vertex set's (0.1% of the thread) and the per-slot cache's allocation + write cost is small.
3. **The gather's two misses** (`ext.draw_gather`, class A): carry `_materialTypeFlags` / `_detailId` in the
   side record (kept current by the same upkeep as the box) or prefetch the geometry ahead of the test, and
   prefetch the MaterialInstance with the geometry; **-0.5 to -0.9 ms** of main-thread time (27% + 13% of
   3.16 ms). Identical lists by construction (the COMPPROF=4 `DrawGather` shadow checks it).
4. **More prefetch in draw_batch's instanced loop / skinning** (render thread, `ExtDrawItemList` 1.27 ms
   self, GetBones 0.14, SkinMemo 0.07): the pass-1 prefetch halved it; what is left is not split by line
   yet. **-0.2 to -0.4 ms**, uncertain.
5. **More walk prefetch coverage** (p13walk's list, ARM first): `MoveTo::Enter` 0.42 ms a frame,
   `CharacterComponent::Tick` 0.37, `MoveTo::path[pathIndex]`, the terrain cell (~1% of the walk), the
   CharacterGrid slot (`Place` ~1.2% of the main thread). Each **<= 0.1-0.2 ms**, ~0.3 ms together; each its
   own A/B pair. The walk now runs with PGO, so the no-PGO caveat of P13_WALK no longer applies.
6. **Vertex / index bind de-dup** (p13render's item): 0.15 ms of binds a frame; **-0.1 ms** at most.
7. `ExtClean` (0.91 ms) and `MeshInstance::Update` (0.61 ms) on the main thread: not split yet; a line
   profile first.
8. Not targets: the GPU (section 5), the prime pin (core_ctl, P13_WALK 5.1), Present / submit (system,
   after the Release), the frame limiter (0.08 ms).

### 9.2 Needs an owner ruling
1. **Overlap the emit with the steps** (`ext.render_pipe`'s fence): today the sim never runs with render
   work in flight, so the emit up to the Release (4.45 ms a frame of main-thread wait) is serial with the
   sim. Letting the steps run while the emit records needs every live read of the emit (bones, instance
   transforms: `GetBones`, `MeshInstance::Draw`, draw_batch's `GetInstanceTransform`) to come from the
   packet instead. **Up to -4.45 ms a frame (18.7 -> ~14.3 ms, the 60 cap)**: the largest single lever on
   the stress town, and a change to the extension's safety contract.
2. **BC1 / BC3 -> ETC2 / ASTC at load** on devices without `textureCompressionBC`: **-330 MB of GPU memory**
   (~half of it), no fps change expected (maybe a little GPU bandwidth). Rule 1 holds (a load-time
   transform of the user's data, an encoder in the tree, never shipped data); the ruling is the image: an
   encode is lossy where today's RGBA8 decode is exact (ASTC 4x4 with 2-bit weights reproduces BC1's
   1/3-2/3 interpolation closely, ETC2 does not), and the encode's load-time cost on a phone must be
   measured (the derived-data cache can hold the result).
3. **Texture quality Half by default on low-memory devices** (an existing row: -228 MB measured): a
   settings default per device class, visible as softer textures.
4. **Frames in flight** (latency 38-42 ms post-to-present in normal play): fewer queued frames trades
   throughput slack for input latency; a backend change with no sim effect, but a feel decision.

## 10. Verified versus assumed
- Verified (measured here): the three baseline runs; the main thread's on / off-CPU split and that its
  off-CPU time is the render-pipe wait (no driver time on the main thread); the render-thread profile; the
  clocks, placements and core_ctl samples; the decode's output total and that it is resident (Half run); the
  GPU page totals; the soak curve and its clocks; normal play's fps, per-thread CPU and latency histograms.
- Assumed or estimated: every "expected" gain in section 9; the GPU ceiling (clock scaling); the 14 ms gap in
  the throttled frames (not attributed); the ETC2 / ASTC sizes (format arithmetic, not built); that the
  late soak windows' extra cost over the uncapped t ~ 450 s window is throttling and not citizens (both
  grew); the PGO gain against pass 1 (different sessions, not ABBA).
