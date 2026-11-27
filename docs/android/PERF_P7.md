# Android P7 perf survey (Galaxy S25)

NOT RETAIL. Research and first measurements for phase P7 "Android fit" (`docs/vulkan/PLAN.md` §5).
Measured 2026-10-25 on the reference device (Galaxy S25 SM-S931U, Adreno 830, driver 1.3.284 /
`0x80320040`, Android 17, user build, 120 Hz panel 2340x1080), tree `80c1b31f1`. Nothing in `src/`
changed for this survey. Raw logs, CSVs and profiles are not in the tree (session scratchpad).

## 1. Profiling toolchain

### 1.1 What works for this app (SDL3 + native Vulkan, no Java rendering)

| Tool | Works here? | Needs | Verified 2026-10-25 |
|---|---|---|---|
| simpleperf (NDK r30 `simpleperf/`) | Yes: CPU sampling of every native thread, call graphs by frame pointer | a debuggable apk, OR `<profileable android:shell="true"/>` in the manifest (Android 10+) on a release apk | **Yes**, on the perf-flavour apk with profileable added (a temporary edit, reverted) |
| kgsl sysfs (`/sys/class/kgsl/kgsl-3d0/`) | Yes, no root: `gpu_busy_percentage`, `gpubusy`, `clock_mhz`, `devfreq/cur_freq`, `max_gpuclk`, `temp` | nothing | **Yes**. `gpuclk` is permission-denied; `clock_mhz` has the same value |
| `dumpsys SurfaceFlinger --timestats` | Yes: present-to-present histogram, missed frames, jank, for any layer | nothing | **Yes** |
| `dumpsys meminfo <pkg>` | Yes: PSS / RSS, `Graphics` (GL mtrack = the Vulkan driver's memory) | nothing | **Yes** |
| `dumpsys thermalservice`, `dumpsys battery` | Yes: thermal status 0..6, AP / SKIN / BAT sensors, battery tenths of °C | nothing | **Yes** |
| `dumpsys gfxinfo <pkg>` | No useful data: it measures HWUI (Java views); our frames bypass HWUI | - | Ran; no frame data, as expected |
| The game's own lines | Yes: `[Stress] t=` (fps, update, draw, ms/iter), `[Vulkan] frame=` every 300 frames (draws, passes, resolves, sets), boot-state frame counter | `BANISHED_STRESS=1` for the stress lines | **Yes** |
| RenderDoc Android | Yes in principle (Vulkan, debuggable apk, the RenderDoc GUI's replay-context dropdown starts its server apk on the device) | a debuggable apk and the qrenderdoc GUI; RenderDoc's `plugins/android/org.renderdoc.renderdoccmd.arm64.apk` is already in `C:\Program Files\RenderDoc` | **Yes, unattended** since 2026-11-26: `tools/trace_oracle/android_capture.sh` (debug apk; S25 verified). Per-draw GPU time reads ~0 on Adreno, so it gives pass structure, not GPU cost |
| Android GPU Inspector (AGI) | Frame profiler (per-pass GPU time + counters) and system profile (Perfetto) | debuggable apk; Vulkan apps must be validation-clean; install AGI on Windows | Not installed, not tried |
| Snapdragon Profiler | GPU counters, render stages, per-pass timing on Adreno | install from Qualcomm; Vulkan metrics are documented as wanting root on some versions (FAQ) | Not installed, not tried |
| Perfetto (`adb shell perfetto` / ui.perfetto.dev) | CPU scheduling, frequencies, thermal; GPU counters through the Qualcomm producer where exposed | nothing for the system trace | Not tried |
| Khronos validation layer | Yes, injected with the GPU debug-layer settings | debuggable apk (or `com.android.graphics.injectLayers.enable` meta-data, API 30+) | Not done (P7 item, §5) |

### 1.2 One-time setup on this machine
- NDK r30 already has simpleperf: device binary `%LOCALAPPDATA%/Android/Sdk/ndk/30.0.16248370/simpleperf/bin/android/arm64/simpleperf`,
  host binary `.../simpleperf/bin/windows/x86_64/simpleperf.exe`, scripts (`binary_cache_builder.py`,
  `report_html.py`, `inferno.bat`) beside them; Python 3 on PATH runs them.
- The unstripped library for symbols: `android/app/build/intermediates/cxx/RelWithDebInfo/<hash>/obj/arm64-v8a/libmain.so`
  (perf flavour; `.../Debug/...` for debug).
- A release apk is **unsigned** (`android/app/build.gradle` has no signing config): `adb install` refuses
  it. For a local measurement it was aligned and signed with the debug keystore:
  ```bash
  BT="$LOCALAPPDATA/Android/Sdk/build-tools/36.0.0"
  "$BT/zipalign.exe" -f -P 16 4 app-release-unsigned.apk aligned.apk
  "$BT/apksigner.bat" sign --ks ~/.android/debug.keystore --ks-pass pass:android --key-pass pass:android \
      --ks-key-alias androiddebugkey --out signed.apk aligned.apk
  adb install -r signed.apk        # replaces the debug install (same key); the data folder stays
  ```
- A fresh worktree has empty submodule folders and `git submodule update` refuses the local `file`
  transport; this survey junctioned `third_party/<module>` to the main checkout's copies (same commits) for
  the build and removed the junctions after (they break `git status` in the worktree).

### 1.3 Commands (as run)
```bash
# CPU profile, 30 s, of the running app (profileable or debuggable apk)
adb push $NDK/simpleperf/bin/android/arm64/simpleperf /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/simpleperf
adb shell /data/local/tmp/simpleperf record --app org.openba.game -e cpu-clock -f 2000 --call-graph fp \
          --duration 30 -o /data/local/tmp/perf.data
adb pull /data/local/tmp/perf.data .
python $NDK/simpleperf/binary_cache_builder.py -i perf.data -lib <dir of the unstripped libmain.so>
$NDK/simpleperf/bin/windows/x86_64/simpleperf.exe report -i perf.data --symfs binary_cache --sort comm,tid
$NDK/simpleperf/bin/windows/x86_64/simpleperf.exe report -i perf.data --symfs binary_cache --tids <tid> \
          --children --sort symbol --percent-limit 3
python $NDK/simpleperf/report_html.py -i perf.data      # html + flame graph (inferno.bat for the graph alone)
# frame pacing (any app): clear, run, dump
adb shell dumpsys SurfaceFlinger --timestats -clear -enable
adb shell dumpsys SurfaceFlinger --timestats -dump      # presentToPresent histogram, missedFrames
adb shell dumpsys SurfaceFlinger --timestats -disable
# GPU, thermal, memory
adb shell cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage /sys/class/kgsl/kgsl-3d0/clock_mhz /sys/class/kgsl/kgsl-3d0/temp
adb shell dumpsys thermalservice | grep -E "Thermal Status|Temperature\{"
adb shell dumpsys battery | grep temperature            # tenths of a degree
adb shell dumpsys meminfo org.openba.game
# repeatable clocks (Android 11+): fixed CPU/GPU operating point for A/Bs; cool the phone first
adb shell cmd power set-fixed-performance-mode-enabled true    # ... false after
# validation layer (debuggable apk), Android 9+
adb push libVkLayer_khronos_validation.so /data/local/tmp
adb shell run-as org.openba.game cp /data/local/tmp/libVkLayer_khronos_validation.so .
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app org.openba.game
adb shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
adb shell settings put global gpu_debug_layer_app org.openba.game   # messages: logcat tag VALIDATION
```
Notes: the perf flavour strips nothing from stderr's `[Stress]` lines. `security.perf_harden` is 1 on this
user build, so only `--app` profiling of a profileable/debuggable process works (no system-wide record).
Kernel symbols are hidden (`kptr_restrict`), harmless for user-space work. Thermal tests should run 15+
minutes to reach steady state (Google's ADPF guidance); the runs below are 2-7 minutes.

## 2. Adreno tiler checklist against this backend

Sources: Qualcomm's Adreno mobile best practices, the Khronos render-pass sample, the
`VK_EXT_multisampled_render_to_single_sampled` proposal, Android's pre-rotation and validation pages (§6).
"Done" = already in the tree; "cheap" = a local change in one or two files; "redesign" = changes the pass
or binding model.

| # | Recommendation | This backend today | Verdict |
|---|---|---|---|
| 1 | CLEAR / DONT_CARE load ops, never a mid-pass clear | Lazy pass begin, clears folded into load ops, DONT_CARE for an undefined image (`draw.cpp:447-468`) | Done |
| 2 | STORE_OP_DONT_CARE for depth not read after the pass | Colour and depth are always `STORE` (`draw.cpp:450`, `:464`); the main depth is never read after the frame unless AO asks | Cheap for passes that end at `DrawEndFrame`, but the store op is fixed at begin: needs a "last main pass" decision (e.g. depth DONT_CARE on the pass after the frame copy) - medium |
| 3 | Transient + LAZILY_ALLOCATED for attachments not read outside the pass (MSAA colour, single-pass Z) | Supported by `CreateImage` (`resources.cpp:693`) but never requested: `MakeMainImage` passes no `TRANSIENT` bit (`draw.cpp:265-284`) | Cheap only where the pass is not split; today the main pass is split (row 4), so the depth must be stored |
| 4 | Few render passes; no pass breaks | 5 passes a frame in game: shadow, reflection, and 3 on the main target, split by the per-frame `FrameCopyImageBuffer` blit (`DrawResolve`, `draw.cpp:1218`); plus the present pass. Each split stores and reloads 2340x1080 colour + depth | Redesign (copy at a pass boundary is what retail asks for; the split could be avoided only by moving the copy or making the copy a sampled draw at half size) |
| 5 | MSAA on the tile (`multisampled_render_to_single_sampled`); 2x "practically free" (Qualcomm) | msrtss chosen when present (`draw.cpp:290`, `pipelines.cpp:445`); phone default is MSAA off; msrtss frames end with a full-screen `vkCmdCopyImage` into the swap-chain source (`draw.cpp:1082`) | Done, unmeasured in game; the end copy could be the present pass's input instead (cheap) |
| 6 | `VK_DEPENDENCY_BY_REGION_BIT` on barriers that can run per tile | Not set (sync2 image barriers at pass boundaries, `resources.cpp:100-136`) | Cheap, low value (no subpasses) |
| 7 | Descriptor cost: combined image samplers (separate samplers cost 2-5% fill rate on Adreno) | Separate `SAMPLED_IMAGE` + `SAMPLER` bindings, as dxbc-spirv emits them (`draw.cpp:601-602`) | Redesign (converter mapping + layout) |
| 8 | Cheap per-draw binding | Per draw and stage: `StageSet` hashes a ~0.5 KB `SetKey`, looks it up, allocates and writes a set on a miss, then `vkCmdBindDescriptorSets` with 8 dynamic offsets per set on 40-binding layouts (`draw.cpp:594-745`). `VK_KHR_push_descriptor` is enabled (`device.cpp:984`) but never used. Measured: 37-42% of the render thread (§3.3) | **Biggest CPU item.** Push descriptors or smaller layouts: medium |
| 9 | UBOs over push constants; per-shader UBO total under ~7.3 KB for constant RAM | Dynamic UBOs on a per-frame ring, `kConstantBufferRange` 16 KB per binding (`pipelines.h:61`) | Check: the range declared per descriptor could be the cbuffer's real size (cheap) |
| 10 | Pipelines built at init; persistent cache | Persistent `VkPipelineCache` with header check + warm list on a worker (`pipelines.cpp:170-240`, `:486-600`); written only at device shutdown (`pipelines.cpp:735`) | Done; write at `WILL_ENTER_BACKGROUND` is the open P7 item (cheap) |
| 11 | Avoid `VK_EXT_vertex_input_dynamic_state` (Qualcomm) | Enabled when present (`device.cpp:985`) but no pipeline uses the dynamic state (only core binding stride, `pipelines.cpp:431-433`) | Cheap: stop enabling it |
| 12 | FIFO with 3 images; mailbox costs battery for no benefit | FIFO unless vsync is off or `Opt().stress` (`device.cpp:394-408`); the S25 swap chain gets 5 images (`max(kFramesInFlight, minImageCount)`, `device.cpp:446`) | Done for play; the stress run uses mailbox on purpose |
| 13 | Pre-rotation | Done (`present.cpp`): a full-screen sampled pass turns the landscape frame into the portrait image, DONT_CARE / STORE (`present.cpp:275`) | Done; costs one 2340x1080 read + write a frame, which is also the natural upscale for row 14 |
| 14 | Render at lower resolution and upscale (blit, or let SurfaceFlinger scale a smaller swap chain) | Full resolution (known issue "Android: full-resolution rendering") | Medium: back buffer at half size + the present pass (or swap-chain extent) upscales; mouse mapping by density |
| 15 | Optimal tiling, no LINEAR / MUTABLE_FORMAT / host-readback images (UBWC) | Optimal tiling everywhere (`resources.cpp:684`); read-back goes through a buffer | Done |
| 16 | Dynamic rendering | Used (core 1.3) | Done (works on the 830 driver) |
| 17 | Memory types | Static buffers device-local through a staging batch; dynamic data host-visible persistently mapped (`resources.cpp:248`, `:585`). The 830 heap is unified and every host type coherent | Done; skipping the staging copy on UMA is a load-time item, not a frame item |
| 18 | Big shadow maps cost bandwidth | Phone default `VideoShadowScalar=2` gives a **512x512** sun map (log); the cost measured is CPU (shadow pass draws, §3.3), not bandwidth | n/a at the phone default; `ext.max_shadow_res` 8192 stays off |
| 19 | Depth read-back | Only for AO's sampled view (off on the phone); `ImageBufferData::Read` refuses depth | n/a |

## 3. S25 measurements

### 3.1 Builds and runs
- **Flavour:** perf (`tools/android.sh build release --jobs 2`, `-DBANISHED_FLAVOUR=perf`, RelWithDebInfo,
  3 m 17 s from a fresh worktree), signed locally with the debug key (§1.2), with
  `<profileable android:shell="true"/>` added to the manifest for the build only. Non-debuggable.
- **Settings:** the phone defaults of `app_android.cpp` (MSAA 0, `VideoShadowScalar=2`, AO / better_trees /
  foliage_aa / billboard_shadow / max_shadow_res off) plus the game defaults: profile per-entry (no
  `/profile:` on Android), so the fixes and the default-on class A rows (render_pipe, draw_gather,
  draw_batch, walk_batch, gather_threads, terrain_batch, render_culling, ...) are **on**. Not comparable 1:1
  with the Windows baseline, which runs `/profile:faithful`.
- **Stress town:** `tools/android.sh run BANISHED_STRESS=1` (800 citizens, 3 sets, compact, speed 10,
  seed 31337, medium map; mailbox because `Opt().stress`). Run A 6000 frames (100 s), run B
  `BANISHED_STRESS_FRAMES=24000` (400 s). Both quit cleanly (`EXITED clean-shutdown`, crash log empty).
- **Plain game:** `tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town` (FIFO at 120 Hz),
  204 s. A first run without the pin is discarded: with `GameEdgeScroll=true` the camera scrolled to a map
  corner and drew empty terrain (seen on the device by the owner).

### 3.2 Numbers

| Metric | Stress A (6000 fr) | Stress B (24000 fr) | Plain game, pinned | Windows baseline (stress) |
|---|---|---|---|---|
| fps (game) | 130 (min window 107) | 129 (min 107) | **120.1, locked** (frame counter) | 893 |
| frame ms mean / p95 | 7.81 / 16.9 | 7.88 / 17.8 | 8.33 (vsync) | 1.12 |
| **sim ms / iteration** | **0.51** | **0.53** | - | 0.15 |
| update ms / frame | 5.10 (EntityList 4.05) | 5.27 (EntityList 4.22) | - | 1.48 |
| draw ms / frame (CPU) | 5.77 | 5.41 | - | 1.00 |
| render_pipe: main / packet / emit / hand-off wait | - | 2.11 / 3.30 / 5.62 ms / 3.2 ms | - | - |
| presents (SurfaceFlinger, 30 s) | - | 3502: 3241 at 8 ms, 255 at 16 ms, 4 at 24 ms; 56 missed | 3726 at 8 ms, 0 missed | - |
| draws / triangles a frame | 380-415 / 1.78-1.92 M | same | 173 / 0.74 M | - |
| passes a frame (other targets) / resolves | 5 (2) / 1 | 5 (2) / 1 | 5 (2) / 1 | - |
| descriptor sets allocated a frame | 93-99 | same | 50 | - |
| constant ring a frame | 0.8 MB | 0.8 MB | 0.3 MB | - |
| GPU busy / clock | 75-82% / 607-1200 MHz | 74-94% / 1050 -> 525 MHz | 85-86% / 525 MHz | - |
| GPU temp (kgsl) | 57-77 °C | 58 -> 75 peak -> 65 °C | 53 °C | - |
| battery temp | 28.8 -> 34.1 °C | 31.0 -> 43.8 °C | 36.1 -> 38.7 °C (started warm) | - |
| thermal status | 0 | 0 -> 1 (~120 s) -> 2 (~150 s), stays 2 | 0 | - |
| memory (stress harness) | ws 450 -> 454 MB, priv 198 -> 200 | ws 450 -> 480, priv 197 -> 228 (4.7 MB/min) | - | priv 724 |
| `dumpsys meminfo` PSS / RSS | - | 830 / 958 MB (at ~60 s) | 759 / 886 MB | - |
| ... Graphics (GL mtrack + EGL mtrack) | - | 568 MB (423 + 71) | 513 MB (397 + 60) | - |
| ... Native heap | - | 58 MB | 51 MB | - |

Render targets in game (log): main 2340x1080 RGBA8 + D24S8, reflection 1170x540 BGRA8 + depth, frame copy
1170x540 BGRA8, sun shadow 512x512 depth (two), minimap 384x384. Swap chain 1080x2340, 5 images,
ROTATE_90. The device enables push_descriptor, vertex_input_dynamic, eds2, load_store_none, d24s8, msrtss.

Pathfinding on the phone (stress A against the Windows baseline): search p95 3.0 ms (+170%), wait p95
11 ms (+349%), worker busy 17.4% (+192%). The CPU is slower; nothing failed or starved.

### 3.3 Where the CPU goes (simpleperf, 30 s at 2000 Hz, frame-pointer call graphs)

| Thread | Stress B (46.6 s CPU in 30 s) | Plain game (32.0 s in 30 s) |
|---|---|---|
| main (game update + render_pipe front half: gather, cull, sort) | 38.3% (60% of a core) | 30.4% (~2.7 ms a frame) |
| render thread (`ext.render_pipe`: Vulkan recording) | 31.8% (49% of a core) | 45.8% (~4.1 ms a frame) |
| `ext.gather_threads` helper | 16.1% (`HelperLoop` alone 10.3%: spin-waiting) | 20.3% (`HelperLoop` 10.0%) |
| path worker | 11.7% | - |

Render thread, inclusive: `SunLight::DrawLight` 50% / 44%, of which `DrawShadowMap` 36% / 33%;
`VideoVulkan::StageSet` 22% / 22% (18% self: hashing and comparing the set key); `vkCmdBindDescriptorSets`
19% / 14% (the Adreno driver copies the set into the command stream: `__memmove_aarch64_nt` under it);
`Math::Transform::AsMatrix` + bones 10-15% (stress); `PresentFrame` 7.5% / 14% (includes the FIFO wait in
the plain game); `vkCmdDrawIndexed` 6%. The Adreno driver as a whole is 6% of all samples.
Main thread: `DrawGather::BoxPasses` 10.7%, `EntityList::UpdateWalkBatch` 9.0%, `DrawSort` 8.6% + 2.9%.

### 3.4 Pass structure (log + `draw.cpp`, no capture)
Per frame: shadow (512x512, depth only), reflection (1170x540), main pass 1 -> `DrawResolve` ends it and
blits 2340x1080 into the 1170x540 frame copy -> main pass 2 (LOAD colour + depth) -> a third main-target
pass (split not attributed without a capture) -> `DrawEndFrame` -> present pass (sample the landscape
frame, write the rotated swap-chain image). Every main pass stores colour and depth. At 2340x1080 one
colour or depth surface is 10.1 MB; the main target alone moves roughly 3 x 20 MB stored + 2 x 20 MB
loaded + 10 MB copy read + 20 MB present read/write = about 130 MB a frame, about 16 GB/s at 120 fps
(arithmetic, not measured; the phone has about 85 GB/s). RenderDoc or AGI should confirm it (§5 item 1).

## 4. Ranked perf work

Ordered by expected gain per cost on this phone. "Gain" is an estimate from §3, to be measured.

| Rank | Item | Expected gain | Cost | Where |
|---|---|---|---|---|
| 1 | **Descriptor path**: push descriptors (`vkCmdPushDescriptorSetKHR`, already enabled) for the per-draw sets, or a set layout sized to the shader (not 40 bindings + 8 dynamic offsets per set); keep the hash only where sets are reused | render thread -25..35% (StageSet + bind = 37-42% today): about 1-1.5 ms a frame in game, 2 ms in stress | medium (draw.cpp descriptor section, layout flag in pipelines.cpp) | `draw.cpp:594-745`, `pipelines.cpp:667` |
| 2 | **Frame cap on the phone**: 60 fps default (frame limiter cap, or `ANativeWindow_setFrameRate`) instead of 120 Hz FIFO | about half the CPU and GPU work a second; the cheapest thermal lever (status 2 within 150 s at 120+ fps) | cheap; needs a ruling (it is a phone default) | `app_android.cpp` seed, frame limiter |
| 3 | **Half-resolution render scale** (ruled phone default): back buffer at half size, upscale in the existing present pass or by a smaller swap chain (SurfaceFlinger scales) | GPU fill and main-target bandwidth / 4; lets the governor drop clocks further | medium (back buffer vs window density, mouse mapping) | known issue "full-resolution rendering", `present.cpp` |
| 4 | **Main-target store ops**: depth DONT_CARE on the last main pass; DONT_CARE colour store where the present pass is the next reader is not possible (it reads), but the msrtss end copy (`draw.cpp:1082`) can be dropped by sampling the msrtss image in the present pass | about 10-30 MB a frame less traffic | cheap to medium | `draw.cpp:447-468`, `:1082` |
| 5 | **Gather helper spin**: `HelperLoop` spins ~10% of all samples; on a phone that is power and heat with nothing to show | 0.5-1 core of spinning less; lower clocks | cheap (`ext.gather_threads`, park after N spins) | `src/ext/gather_threads/` |
| 6 | **Shadow pass CPU**: 33-36% of the render thread draws the 512x512 sun map; a phone "shadows off" or fewer casters | render thread -30% when off | none: retail's Shadow Quality = None is the off (P8_SHADOWS_RELEASE.md) | `SunLight::DrawShadowMap` |
| 7 | Frame copy split: make the 2340->1170 copy a sampled half-size draw inside the pass, or move it to a pass boundary that already exists | removes one store + load of 20 MB | redesign | `draw.cpp:1218` |
| 8 | Stop enabling `VK_EXT_vertex_input_dynamic_state` (unused; Qualcomm says avoid) | none measured; hygiene | cheap | `device.cpp:985` |
| 9 | Combined image samplers in the converter mapping | 2-5% fill (Qualcomm) | redesign | `shader_convert.cpp`, layouts |
| 10 | Swap chain 3 images on Android (5 today) | ~20 MB of swap images, a little latency | cheap | `device.cpp:446` |

## 5. Graphics features skipped or answered "not supported", with an order

| Order | Item | State today | Recommendation |
|---|---|---|---|
| 1 | Validation layer on the device | Not pushed (`validation=0`); the debug flavour only looks for it (`device.cpp:254`) | First: the debug apk with the layer (§1.3) before any perf change, so every later change is checked; also the AGI precondition |
| 2 | Pipeline-cache write at background | Written at device shutdown only; a recents-kill keeps the old file | Cheap: `SaveWarmList` + `SaveCacheBlob` from the `WILL_ENTER_BACKGROUND` watch (`device.cpp:163`) |
| 3 | Save on `SDL_EVENT_LOW_MEMORY` / `TERMINATING` | Not mapped; a background kill loses the unsaved game | Map to the pause-menu save (PLAN §4.6) |
| 4 | Watchdog in the background | Background counts as HUNG past 20 s; unattended runs exit 4 | Pause the boot-state watchdog between WILL_ENTER_BACKGROUND and DID_ENTER_FOREGROUND |
| 5 | Half-resolution render scale | Full resolution (§4 rank 3) | With the frame cap, the main phone setting |
| 6 | MSAA default | Off; msrtss 2x rendered the menu in P6, never measured in game | Measure 2x msrtss in the pinned town (fps, GPU busy, clock) after rank 3; Qualcomm calls 2x nearly free on the tile |
| 7 | AO on the tiler MSAA path | Skipped and logged (depth never stored multisampled) | Leave off on the phone; revisit only if MSAA becomes the default |
| 8 | Shadows off | Retail has one: Shadow Quality = None (`VideoShaderDetail=0`; verified in P8, P8_SHADOWS_RELEASE.md); phone seed keeps shadows on (512 map) | none (ruled 2026-10-25: no ext row) |
| 9 | Edge scroll on a phone | `GameEdgeScroll=true` scrolled an unattended game to the map corner | Seed `GameEdgeScroll=false` in the phone defaults, or ignore edge scroll for touch-sourced mouse positions; ruling |
| 10 | Font rasteriser | Absent: looks beside `/proc/self/exe`; `ext.sharp_ui` keeps its retail fallback | Look in the data folder (`files/`) for the user's TTF |
| 11 | HWASan run | Not done | Once per phase via `wrap.sh` on a debuggable apk (PLAN §4.6) |
| 12 | Release signing / apk in `release.yml` | Release apk unsigned | P7 row; a keystore is an owner decision |
| 13 | Tweak overlay | DX11 only | Leave |
| 14 | Process after a clean quit | `pidof` still showed the process ~60 s after `EXITED` | Note for harnesses: wait on the boot state, not on `pidof` |

Open measurements: RenderDoc / AGI capture of one in-game frame (pass and resolve count, per-pass GPU
time); a 30-minute soak at the chosen defaults (P7's done-when); the same stress town with
`/profile:faithful` settings for a like-for-like ms/iter against Windows.

## 6. Sources
- Qualcomm, Adreno GPU on Mobile: Best Practices: https://docs.qualcomm.com/bundle/publicresource/80-78185-2/topics/mobile_best_practices.md
- Qualcomm, Tiling architecture: https://developer.qualcomm.com/sites/default/files/docs/adreno-gpu/snapdragon-game-toolkit/gdg/gpu/best_practices_tiling.html
- Qualcomm, Snapdragon Profiler: https://developer.qualcomm.com/software/snapdragon-profiler ; FAQ: https://docs.qualcomm.com/bundle/publicresource/80-71528-1/topics/profiler_faq.md
- Qualcomm, Anti-aliasing with Adreno: https://developer.qualcomm.com/software/snapdragon-profiler/app-notes/anti-aliasing-with-adreno
- Khronos sample, render passes (load/store bandwidth): https://docs.vulkan.org/samples/latest/samples/performance/render_passes/README.html
- `VK_EXT_multisampled_render_to_single_sampled` proposal: https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_multisampled_render_to_single_sampled.html
- `vkCreateAndroidSurfaceKHR` (compositor scales a swap chain whose extent differs): https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateAndroidSurfaceKHR.html
- Arm, Vulkan mobile samples (transient attachments, depth DONT_CARE): https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/vulkan-samples
- simpleperf, Android application profiling: https://android.googlesource.com/platform/system/extras/+/main/simpleperf/doc/android_application_profiling.md ; scripts: https://chromium.googlesource.com/android_ndk/+/refs/heads/main/simpleperf/doc/scripts_reference.md
- Android GPU Inspector: https://developer.android.com/agi/start
- RenderDoc on Android: https://renderdoc.org/docs/how/how_android_capture.html
- Android Vulkan validation layer: https://developer.android.com/ndk/guides/graphics/validation-layer
- ADPF thermal API and testing length: https://developer.android.com/games/optimize/adpf/thermal ; fixed performance mode: https://developer.android.com/games/optimize/adpf/fixed-performance-mode
- Frame rate / SurfaceFlinger timestats: https://developer.android.com/games/optimize/framerate
