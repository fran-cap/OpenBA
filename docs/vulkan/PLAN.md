# Vulkan backend (Linux + Android) — plan

Status: RULED 2026-10-24, no code yet. Owner rulings in §1 (the §6 questions are answered there);
§4 holds the design, §5 the phases. Sources: `SOURCES.md` alongside.

## 1. Scope rulings (2026-10-24)
- Targets: Linux desktop (any Vulkan 1.3 driver) and Android on **new Adreno parts only**
  (Adreno 8xx; the Galaxy S25 / Snapdragon 8 Elite is the reference device). Mali, PowerVR
  and older Adreno are out of scope: no workarounds are written for them.
- Input: **mouse and keyboard are required** on Android too. The UI is not redone for touch;
  the existing UI-scale and window-scaling work is what the phone gets.
- Development bed: **Windows first.** The exe already asks SDL for a Vulkan window on Windows
  and the dev box reports Vulkan 1.4 (NVIDIA). The backend is built and gated with the normal
  `tools/build.sh run` loop and RenderDoc, then the same code is configured on Linux and Android.
  The WSL bed is lavapipe only (dzn exposes 1.2), so it is a smoke bed, not a dev bed.
- **Dependencies**: pinned submodules with permissive licences (Vulkan-Headers, VMA, dxbc-spirv,
  SPIRV-Headers), built into the exe. Ruled 2026-10-24.
- **Linux endgame**: once the Vulkan backend is the Linux renderer, dxvk-native goes (fetch script,
  WSI code, docs). Ruled 2026-10-24. Done in P5 (2026-10-25).
- **Android identity**: none exists yet; to be made in P6. Proposal to ratify then: package
  `org.openba.game`, app name "OpenBA", saves under `Android/data/<pkg>/files/Documents/Banished`.
- **Phone defaults** (P6/P7 starting point, tuned after the soak): MSAA off, render at **half the
  screen resolution**, shadows off, graphics extensions (AO and the rest) off. Ruled 2026-10-24.
- **Tooling**: install cmdline-tools, NDK r30 and the SDK CMake now (ruled 2026-10-24).
- **Linux verification**: lavapipe plus the phone are the witnesses during P5; the real-GPU Linux
  check is last, on an Ubuntu live environment booted by the owner. Ruled 2026-10-24.

## 2. What already exists (facts, read from the tree 2026-10-24)
- **The seam.** A backend is `src/render/<name>/` with `backend.cmake` and
  `render_backend_<name>.cpp`, picked by `BANISHED_RENDER_BACKEND` (`src/render/common/README.md`).
  It implements the eleven `Video::*Data` companion classes of `src/engine/video/CompanionData.h`
  (112 virtual slots in all; the retail plugin ABI) plus the small ext seams (`AoBackend::`,
  `FaaBackend::`, `BbsBackend::`, `Ext::RenderPipe::FinishPresent/FinishRecover`,
  `Graphics::ExtAoAfterOpaque`). Everything above that line (passes, render targets, shadow maps,
  MSAA policy, draw ordering) is engine code and is backend-neutral.
- **Two precedents in-tree.** `src/render/dx11/` (8,100 lines, the retail D3D11 plugin ported
  one-to-one) and `src/render/metal/` (5,300 lines, a from-scratch backend: Objective-C++, its own
  pipeline cache keyed on a descriptor JSON, a persisted pipeline list for warm-up, a retire pool
  for in-flight resources, a shader cache keyed on DXBC hash + converter fingerprint + OS version).
  The Metal layout is the template for Vulkan; the DX11 files are the semantic reference for what
  each slot must do.
- **Linux before P5 = the DX11 code over dxvk-native 3.1.1** (headers only; the .so files were
  dlopened by `src/platform/posix/gfx_posix.cpp`). The Vulkan backend replaced that route on Linux in
  P5 (2026-10-25); it was not a first port.
- **Shaders.** Retail requires feature level 10_0, so every shipped program is **SM4 DXBC** (the
  better_trees rewrites already parse SM4 tokens in `src/render/common/better_trees/dxbc.{h,cpp}`).
  Metal converts DXBC at load through DXMT's airconv, dlopened (LGPL kept out of the exe) and
  caches the result per user. No shader source or bytecode is ever shipped (AGENTS.md §1): the
  Vulkan backend must also convert at load, from the user's own packages, and cache under the
  user's profile.
- **API surface the backend actually needs** (tally of `src/render/dx11/*.cpp`): device + context,
  buffers (vertex / index / constant, dynamic ring with Lock/Unlock), 2D textures with mips,
  render target + depth-stencil views, SRVs, samplers, rasterizer / blend / depth-stencil state
  tables (`renderstatedata.cpp` has the engine-enum → API maps), input layouts, VS + PS only (no
  GS / HS / DS / CS in the retail path; ASSAO is the one ext with its own shaders). Formats:
  BGRA8 / RGBA8 UNORM, RGBA8 UINT, D24S8 (R24G8 typeless), R16 UINT indices, BC1 / BC3 / BC4,
  R8 / A8, and the float / sint vertex formats. MSAA on the main targets, single-sample shadows.
- **arm64 is solved.** The macOS port builds the whole tree with clang on arm64; the `_mm_*`
  intrinsics and `_mm_getcsr/setcsr` already have `__aarch64__` arms. What Android adds is bionic
  (not glibc) and the Android lifecycle, not a new CPU.
- **Threading.** The engine has a render thread and parallel cull (threads session 2026-10-14);
  Metal had to fix a BeginCommands-before-Lock race. Vulkan command recording follows the same
  ownership rules the Metal backend settled, not new ones.
- **Platform rule.** Android becomes a platform set under `src/platform/` (whole-file twins over
  posix + sdl, same rule as darwin), never `#if __ANDROID__` in app / game / engine.

### 2.1 The reference device (read over adb 2026-10-24, `adb shell cmd gpu vkjson`)
Galaxy S25 (SM-S931U, Android 17 / API 37, 12 GB, 4 KB pages, arm64-v8a only). Adreno 830,
Qualcomm driver (updatable through Play: `ro.gfx.driver.1`), **Vulkan 1.3.284** (conformance
1.3.8), instance 1.4. What matters for the design:
- **BC1 / BC3 / BC4 / BC5 are supported** (`textureCompressionBC` = 1, sampled + linear filter in
  optimal tiling), as are ASTC and ETC2. No texture transcoding is needed for the retail packages.
- **D24_UNORM_S8_UINT is a depth-stencil attachment format**, as are D32, D32S8, D16, S8. The
  retail D24S8 choice carries over unchanged (the Metal port had to argue D32F vs D24S8).
- MSAA: 1 / 2 / 4 samples on colour and depth (no 8x). `VK_EXT_multisampled_render_to_single_sampled`
  is present (tiler-native MSAA resolve), as are `VK_QCOM_tile_properties` and both
  `load_store_op_none` extensions.
- Core 1.3 features on: dynamic rendering, synchronization2, maintenance4, pipeline creation cache
  control, inline uniform block; 1.2: descriptor indexing, timeline semaphores, buffer device
  address, host query reset, separate depth-stencil layouts. Also present: push descriptors (32),
  vertex input dynamic state, extended dynamic state 1 + 2, custom border colours, depth clip
  enable, maintenance 5 + 6, pipeline executable properties, swapchain_maintenance1.
- **Absent:** graphics pipeline library, shader object, extended dynamic state 3, memory budget.
  So the pipeline design is "monolithic VkPipeline + a persistent VkPipelineCache + warm-up list"
  (the Metal design), not GPL.
- Limits: push constants 256 B, **minUniformBufferOffsetAlignment 256**, UBO range 64 KB, 7
  descriptor sets, 32 vertex attributes / bindings, 8 colour attachments, anisotropy 16, timestamps
  on graphics, subgroup 64. One unified heap (11.1 GB, device-local) plus a 4 GB heap; every host
  type is coherent (`nonCoherentAtomSize` 1), so a persistently mapped ring needs no flushes.
- Queues: 3 graphics+compute, 1 compute, 1 transfer. Geometry and tessellation shaders exist but
  the retail path uses neither.

## 3. Precedent survey (web survey 2026-10-24; sources in docs/vulkan/SOURCES.md)

### 3.1 Keep the D3D11 code, layer Vulkan under it (DXVK / dxvk-native)
- Proven on Linux: Valve's Source 1 Linux builds (Portal 2, L4D2, HL2) moved to DXVK 2.0 in 2022;
  Ys VIII/IX, FNA 24.08. Source 2 is a native Vulkan renderer, not dxvk-native. This is the route
  OpenBA already takes on Linux.
- **Dead end for the S25.** DXVK 3.0 (2026-06) requires Vulkan 1.4 plus robustness2, transform
  feedback, maintenance5/6; the stock Adreno 830 driver is 1.3.284, and upstream states "no
  first-party support for Android or mobile graphics drivers". No bionic build of DXVK or
  dxvk-native exists anywhere (not upstream, Termux, or a fork). Every "DXVK on Android" setup
  (Winlator and friends) runs Windows PE DXVK under Wine + FEX on a bundled Mesa Turnip, because
  the stock Qualcomm driver lacks what DXVK needs. Turnip only works if the app ships it
  (libadrenotools, loader hooking); that is an emulator escape hatch, not a shipping path.
- wined3d's Vulkan renderer (Wine 6.0+) is the second D3D11-over-Vulkan implementation; it
  compiles DXBC with vkd3d-shader and is slower than DXVK.

### 3.2 Projects that wrote a real Vulkan backend beside a D3D11 one
- **Dolphin** (D3D11 / D3D12 dropped 2017 / OpenGL / Vulkan / Metal): the unit of the backend
  interface became the immutable **pipeline**, not shader stages (VideoCommon pipeline cache
  2018); a backend-neutral pipeline UID is the cache key, blobs persist to disk; Android needed
  libadrenotools + Turnip because of stock Adreno bugs.
- **PCSX2** (`GSDevice`: D3D11 / D3D12 / OpenGL / Vulkan / Metal) and **DuckStation**
  (`GPUDevice`, same author): one abstract device, pipelines indexed by a state key, UID-keyed
  shader and pipeline caches, multithreaded pipeline compilation on every backend including
  Android (2026).
- **Xenia**: one IR, two emitters (DXBC for D3D12, SPIR-V for Vulkan). **RPCS3**: D3D12 path
  abandoned for Vulkan. **Godot** `RenderingDevice`: regretted baking Vulkan's up-front render-pass
  rules into the abstraction, fixed later with an acyclic render graph; native Metal beat MoltenVK.
- **Croteam** (GDC 2016): "Vulkan is hard to code for"; either port the engine outright and refine,
  or wrap the graphics layer. id Tech 6 went OpenGL to Vulkan, not D3D11 (premise corrected).
- **Unreal**: PSO precaching + bundled caches per RHI; still hitches on global shaders in 5.5.

**Convergent shape** of every successful multi-backend project: a thin device interface whose
unit is the immutable pipeline, a backend-neutral state hash as its key, a persisted pipeline
and shader cache keyed on that hash, compilation off the render thread, and explicit but cheap
render-target begin / end. Nobody who kept "set rasterizer state, then draw" at the backend
boundary reports being happy with it. OpenBA's seam is at the D3D11-object level
(`RenderStateData`, `VertexLayoutData`, `*ProgramData`), so the pipeline key is assembled
inside the backend from those objects at draw time, exactly as the Metal backend does.

### 3.3 DXBC to SPIR-V at load time
- **vkd3d-shader** (Wine, LGPL 2.1+, C): DXBC SM1-5 and DXIL to SPIR-V / GLSL / MSL;
  `vkd3d_shader_interface_info` remaps (register class, index) to (set, binding) or push
  constants, combined samplers optional; production user is wined3d. Same dlopen posture the
  Metal backend uses for airconv. Bionic build unverified.
- **dxbc-spirv** (doitsujin, **MIT**, C++17, Meson, no dependencies, static library; the compiler
  inside DXVK 3.0): SM5.1 and older, SPIR-V 1.6; the authors say standalone output needs custom
  lowering passes for resources and shader I/O.
- **The pre-3.0 DXVK DXBC compiler** (zlib, `src/dxbc` of dxvk <= 2.7): eight years of every SM4/5
  game; a standalone extraction exists (Codeberg redstrate/dxbc). Comparison of these two is a
  separate research item (§4.2).
- HLSLcc (Unity, MIT) emits GLSL, needs glslang after it; SPIRV-Cross is reflection only here.
  DXC needs HLSL source: irrelevant for retail bytecode.

### 3.4 Off-the-shelf layers
SDL3 GPU (Vulkan / D3D12 / Metal, SPIR-V in, Android-capable) caps at 4 colour targets, fixes the
descriptor-set convention, has no geometry stage and no documented pipeline cache. NVRHI has no
Android. Diligent (Apache 2.0) is Android-proven and takes SPIR-V but is a second large engine
under ours. bgfx wants its own offline shader container. None of them replaces the 112-slot
companion interface; each would sit under it as a fourth layer. See §4.1.

### 3.5 Tiler / Android lessons (Adreno)
- Load / store ops decide bandwidth: LOAD_OP_CLEAR instead of LOAD saved 600 MiB/s, depth
  STORE_OP_DONT_CARE 555 MiB/s (Khronos samples). A D3D11 "bind targets, then clear" sequence
  must fold into one pass begin with CLEAR, never a mid-pass clear.
- MSAA colour: transient + lazily allocated, resolved by the pass (never `vkCmdResolveImage`);
  the S25 also has `multisampled_render_to_single_sampled`.
- Persist `VkPipelineCache` (24 ms vs 50 ms per pipeline); reuse descriptor sets across frames;
  no ALL_COMMANDS barriers; FIFO, 3 swap-chain images, 2 frames in flight.
- Pre-rotation: without it the compositor rotates every frame at real power cost; read
  `currentTransform`, swap extent for 90 / 270, rotate the projection. Landscape-locked games still
  must handle `VK_SUBOPTIMAL_KHR`.
- Lifecycle: backgrounding destroys the surface (`VK_ERROR_SURFACE_LOST_KHR`); surface and
  swapchain are recreated on resume; one `VkSurfaceKHR` per `ANativeWindow`.
- Qualcomm FlexRender switches between binning (GMEM) and direct mode per pass; both must be fine.
- Known stock-Adreno defects are on 5xx / 6xx / 8 Gen 1-2 (framebuffer fetch + clear leak,
  conditional-discard miscompile); nothing 830-specific is published yet. The 830's descriptor
  indexing bits were read directly from the device (§2.1), so that item is confirmed, not assumed.


### 3.6 Android plumbing (survey 2026-10-24 + on-device checks)
- **SDL3** (`third_party/SDL` is 3.4-dev, which has `android-project/`): the game becomes
  `libmain.so` with `SDL_main`, launched by a subclass of `SDLActivity` through Gradle +
  `externalNativeBuild` over the existing CMakeLists. SDL's own Android notes want SDK 35+ and
  a modern NDK; the current LTS is **NDK r30** (clang 20 or 21, C++17 is routine).
  `minSdk 29` (Android 10: Vulkan 1.1 guaranteed, HWASan, uniform scoped storage), arm64-v8a only.
- **Vulkan surface**: SDL dlopens `libvulkan.so` and creates the surface with
  `vkCreateAndroidSurfaceKHR` from its `ANativeWindow`. **Lifecycle is the sharp edge**: on pause
  SDL queues WILL_ENTER_BACKGROUND / DID_ENTER_BACKGROUND and then blocks the event loop; when
  Java tears the surface down it releases the window with **no wait for a Vulkan app** (the wait
  is guarded by `SDL_WINDOW_OPENGL`). So the backend must, inside the WILL_ENTER_BACKGROUND
  handler (an event watch, not the next pump), idle the device, destroy swapchain and surface and
  park the render thread; on DID_ENTER_FOREGROUND recreate both. Never call `exit()` on Android.
- **Input**: touches already arrive as mouse events (`SDL_HINT_TOUCH_MOUSE_EVENTS` = 1), so the
  mouse / keyboard requirement is met by a Bluetooth mouse + keyboard or DeX, and a finger still
  clicks. Back button is Android's unless trapped. `SDL_WINDOW_FULLSCREEN` gives immersive mode;
  keep-screen-on is SDL's default.
- **Data files**: the app-specific folder `/sdcard/Android/data/<pkg>/files`
  (`SDL_GetAndroidExternalStoragePath()`), the DevilutionX recipe: no permission, deleted on
  uninstall, filled by MTP or `adb push`. **Verified on this S25 (One UI, Android 17): the adb
  shell can create and write under `/sdcard/Android/data/<pkg>/files`.** Correction found in P6
  (2026-10-25): a FOLDER the adb shell makes there belongs to the shell user and the app process can
  neither list nor open anything inside it (EACCES); files adb pushes into a folder the app made are
  readable. The app makes `WinData/` itself (`docs/android/README.md`). SAF import is a later
  nicety; MANAGE_EXTERNAL_STORAGE is not needed.
- **Build / deploy loop**: `sdkmanager` for cmdline-tools, platform, build-tools, NDK and CMake;
  `./gradlew installDebug`; `adb logcat -s SDL SDL/APP DEBUG AndroidRuntime libc`; `ndk-stack`
  over the unstripped `.so`; HWASan via `wrap.sh` on a debuggable apk. Expect roughly 30-60 s per
  touch-build-install cycle (unmeasured). This machine today: SDK platform 35, build-tools 35,
  Android Studio + its JBR, Gradle caches. **Installed 2026-10-24 (ruling §1):** cmdline-tools
  (`%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest`), NDK r30 LTS (`ndk/30.0.16248370`), SDK CMake
  3.31.6 (with its ninja), platform 36, build-tools 36.0.0, platform-tools 37.0.1. `sdkmanager` runs
  with `JAVA_HOME` = Android Studio's `jbr`; no `ANDROID_HOME` is set system-wide yet.
- **Pipeline cache**: Android persists nothing for you; serialise `VkPipelineCache` under the
  app's internal storage keyed by vendor / device / driver / cache UUID, validate the header.
- **Tools**: RenderDoc on Android (debuggable apk) for captures; Snapdragon Profiler for GPU
  timing. Validation layers are pushed into the debuggable app and enabled with the
  `enable_gpu_debug_layers` settings.
- **Envelope**: 12 GB RAM, 2340x1080 at 120 Hz (`wm density` 450, so SDL content scale about 2.8),
  about 85 GB/s LPDDR5X, GTX 1050-class peak that throttles to about half within minutes. The
  risks are draw-call CPU time and render-target bandwidth, not raw throughput. Plan for a render
  scale setting and 30 fps native or 60 at scale. The derived-data cache (ruled allowed 2026-10-22)
  matters more here: the packages sit on UFS and first-run transforms dominate boot.
- **UI**: UI scale 2-3 through the existing uiscale work; no touch layer (owner ruling §1). What a
  finger cannot do (hover, right-click, wheel zoom, edge scroll) is simply the mouse's job.

## 4. Decisions (proposed 2026-10-24; owner rulings go in §1)

### 4.1 A real backend, `src/render/vulkan/`, Vulkan 1.3 core
- Not dxvk-native (impossible on the stock Adreno driver, §3.1), not SDL3 GPU / Diligent / bgfx
  (a fourth layer under a 112-slot interface that is already the abstraction, with caps the
  retail renderer would hit, §3.4). The Metal backend proved the seam carries a from-scratch
  backend in about 5,300 lines.
- **Floor: Vulkan 1.3 core** (dynamic rendering, synchronization2, timeline semaphores,
  maintenance4), plus `VK_KHR_swapchain` and the platform surface extension. Nothing else is
  required. Used when present: push descriptors, vertex-input dynamic state, extended dynamic
  state 1 + 2 (fewer pipeline keys), load/store-op-none, multisampled-render-to-single-sampled.
  Absent on the S25 and therefore never relied on: graphics pipeline library, shader object,
  EDS3, memory budget, descriptor buffer.
- **Linux moves onto this backend**; dxvk-native stays only as the bring-up oracle (same D3D11
  code, same frames) and is removed once parity is gated. One backend per platform. (Removed in P5.)
- **Windows is the dev bed**; Vulkan never ships on Windows unless asked. `BANISHED_RENDER_BACKEND`
  is a configure-time choice, so it is a second exe, not a launcher option.

### 4.2 Shader translation: dxbc-spirv, statically linked, converted at load, cached per user
- **dxbc-spirv** (doitsujin, MIT, C++17, no deps beyond SPIRV-Headers; the compiler inside DXVK
  3.0). Verified from a clone 2026-10-24: SM4.0 is in range, the API is three calls
  (`dxbc::Converter` to IR, `ir::legalizeIr`, `spirv::SpirvBuilder` with a `ResourceMapping`),
  samplers and images are separate (`OpSampledImage` at use), fragment origin upper-left,
  depth [0,1], explicit-offset vec4 cbuffers. What we write: a ResourceMapping of about 40 lines
  (§4.3), DXVK's own `LowerIoPass` sequence (semantic VS-to-PS linkage; skipping it silently
  mislinks), and DXVK's ~30-line `CompileOptions`. Its SPIR-V 1.6 needs `vulkanMemoryModel`,
  `bufferDeviceAddress` and (when SV_VertexID is read) `shaderDrawParameters`: **all three
  confirmed on the S25** and all core-required or ubiquitous on desktop 1.3 drivers.
- Rejected: the pre-3.0 DXVK compiler (zlib: dead upstream, not standalone, 2.7 forces a
  push-constant sampler heap; same SPIR-V 1.6 requirements so no compatibility gain) and
  vkd3d-shader (LGPL: the airconv dlopen dance again, for no technical gain).
- Permissive licence, so it is a pinned submodule built into the exe and a row in
  `THIRD_PARTY_NOTICES.md`. Its version is young (0.1.0): pin a commit, expect API churn.
- **Content rule unchanged**: the DXBC comes from the user's packages at run time; the SPIR-V
  goes to the per-user derived-data cache (ruling 2026-10-22) keyed on DXBC hash + compiler
  commit + mapping / options version, like the Metal shader cache. Nothing is shipped.
- **better_trees comes free**: its rewrites edit DXBC bytes before any conversion, so the Vulkan
  path runs them unchanged ahead of dxbc-spirv (Metal had to re-port them as passes).
- A `vulkan-shader-check` tool (the `metal-shader-check` twin) converts every retail shader
  headlessly and validates with spirv-val; it is the first thing that must pass.

### 4.3 Binding model and pipeline model
- **One pipeline layout for the whole game.** Set 0 = vertex stage, set 1 = pixel stage; within
  a set, fixed bindings per D3D register: `b0..bN`, then `t0..tM`, then `s0..sK`, with N / M / K
  sized from the retail slot usage read out of the DX11 backend (not 14 / 128 / 16 blindly).
  Constant buffers are dynamic-offset UBOs on a per-frame ring (256-byte alignment on the S25).
  Push descriptors where present, otherwise a per-frame descriptor pool with set reuse.
- **Monolithic `VkPipeline` keyed by a backend-neutral hash** of (vs, ps, vertex layout, render
  state, attachment formats, sample count, topology). Viewport and scissor dynamic; EDS1/2 state
  dynamic where present so the key shrinks. **Persistent `VkPipelineCache`** written atomically
  under the user cache dir, header-validated, plus the Metal-style `pipelines.txt` list
  recompiled on a background thread at boot. This is the convergent shape of §3.2.
- **Y-flip by negative viewport height** (core since 1.1); no shader patching.
- **Depth: D24S8** as retail where the device offers it (S25, NVIDIA, Intel); D32S8 fallback
  where it does not (AMD RADV lacks D24S8). The choice is per device, made once at CreateDevice.

### 4.4 Render passes, MSAA, barriers (the tiler rules of §3.5 become the design)
- **Dynamic rendering, begun lazily**: `BindRenderBuffer` and `Clear` only record intent; the
  pass begins at the first draw with the recorded clears as LOAD_OP_CLEAR, and ends on a target
  change, readback, or present. A mid-pass clear is never emitted.
- MSAA colour and depth are transient, lazily allocated, resolved by the pass; depth is
  STORE_OP_DONT_CARE unless something reads it (the AO seam asks for readable depth, so the
  request flips the store op). Shadow targets single-sample as retail.
- Per-image layout tracking with sync2 barriers at pass boundaries only; no ALL_COMMANDS.
- Memory through **VMA** (MIT, header-only); dynamic vertex / index / constant data on
  per-frame-slot (kFramesInFlight) persistently-mapped rings; a retire pool (Metal's) for in-flight objects.

### 4.5 Presentation, lifecycle, device loss
- Surface from `SDL_Vulkan_CreateSurface`; loader via `SDL_Vulkan_GetVkGetInstanceProcAddr`
  and volk-style function tables (no link against `vulkan-1`; MSYS2 has no Vulkan headers, so
  `Vulkan-Headers` is a submodule regardless). FIFO on Android; FIFO / mailbox per the vsync
  setting on desktop. Three frames in flight.
- Android: an SDL event watch handles WILL_ENTER_BACKGROUND synchronously (idle, destroy
  swapchain + surface, park the render thread) and DID_ENTER_FOREGROUND (recreate), because SDL
  releases the native window without waiting for a Vulkan app (§3.6). Landscape-locked, but
  `currentTransform` is honoured and `VK_SUBOPTIMAL_KHR` triggers a rebuild (pre-rotation).
- `VK_ERROR_DEVICE_LOST` feeds the existing OnVideoError / OnVideoRestart route and
  `fix.boot_device_lost`, as the DX11 backend does.

### 4.6 Android platform set
- `src/platform/android/` whole-file twins over posix + sdl (the darwin rule): user dir and game
  dir under `SDL_GetAndroidExternalStoragePath()` (`/sdcard/Android/data/<pkg>/files`), logs to
  logcat and to the same `standalone_*.log` files in that folder (so `boot_state.sh` can read them
  back over `adb pull`), no `exit()`, `SDL_EVENT_LOW_MEMORY` and `TERMINATING` mapped to the
  pause-menu save. `src/diag/android/` twins for the crash handler (tombstone + `ndk-stack`).
- The Gradle project lives at `android/` (copied from SDL's `android-project`, subclassing
  `SDLActivity`), `externalNativeBuild` pointing at the root CMakeLists, `OpenBA` built as
  `libmain.so`, `minSdk 29`, `targetSdk 35`, arm64-v8a only, `c++_shared`. NDK r30 LTS.
- No GLES, no Turnip, no touch layer, no other mobile GPUs (§1). 16 KB page alignment is the
  NDK r28+ default and costs nothing.

### 4.7 Verification
- Windows: `tools/build.sh run` on the Vulkan configure, RenderDoc captures compared with the
  DX11 exe, `tools/trace_oracle/` (never pixel diffs), validation layers on in the debug flavour.
- Linux: `tools/linux_run.sh` routes on lavapipe (CI-shaped smoke) and on a real GPU when one is
  reachable; dxvk-native was the oracle until parity (removed in P5).
- Android: boot state over logcat / `adb pull`, RenderDoc remote, validation layers pushed with
  `run-as`, HWASan `wrap.sh` run once per phase, Snapdragon Profiler for the perf pass.

## 5. Phases
Each phase lands by PR, gate-clean on Windows; nothing below ships behind an env flag.

| # | Phase | Done when |
|---|---|---|
| P0 | **Scaffold** (Windows): `src/render/vulkan/backend.cmake`, submodules (Vulkan-Headers, VMA, dxbc-spirv + SPIRV-Headers), instance / device / swapchain / frame loop, every companion slot stubbed with "not supported" answers, notices rows, CI configures the backend on all three OSes | a cleared colour frame presents through the engine's `BeginCommands` / `EndCommands`; `run-menu` reaches the menu without drawing |
| P1 | **Shaders**: dxbc-spirv integration, ResourceMapping, IO passes, SPIR-V cache, `vulkan-shader-check`, a unit test over a synthesised SM4 blob (the better_trees tests already build those) | every retail shader converts and passes spirv-val; the cache round-trips |
| P2 | **Resources + draws**: buffers / rings, textures (BC upload, mips, samplers), vertex layouts, render-state tables, pipeline key + cache, descriptor path | the main menu renders; RenderDoc shows the DX11 draw list |
| P3 | **Render targets**: render buffers, depth, MSAA, shadows, readback / screenshots, lazy pass begin, barriers | `tools/build.sh run` boots a new game; trace_oracle agrees with DX11 |
| P4 | **Ext seams** and parity table: AO (ASSAO shaders through the same converter), finish split, texture_quality, share_gpu_data, create_on_demand, async_gpu_create, boot_device_lost; `docs/vulkan/PARITY.md` | each seam answers supported or "not supported" honestly; Gate resave PASS on the Vulkan exe |
| P5 | **Linux cutover**: Vulkan the default off Windows, dxvk-native fetch + WSI code deleted, docs and `linux_run.sh` updated | the four `linux_run.sh` routes pass on lavapipe; one run on a real GPU |
| P6 | **Android bring-up**: cmdline-tools + NDK + SDK CMake, `android/` project, platform + diag twins, data folder, lifecycle, pre-rotation, logcat boot state | the S25 boots to the menu, then a new game, from a pushed retail folder |
| P7 | **Android fit**: pipeline-cache persistence, warm-up, render-scale setting, MSAA default, thermal soak, HWASan + validation clean, apk in `release.yml`, `docs/android/README.md` | 30 minutes of town at a stated fps with no validation errors and no growth |

Order rationale: P0-P4 never leave the Windows gate, where every existing tool works. Linux
(P5) is a configure flag away once P4 is green. Android (P6) is the only phase that adds a
platform, and it starts with a renderer already known good on two desktops.

## 6. Open questions
All six questions of the 2026-10-24 draft were ruled the same day; the rulings are in §1.
Remaining: ratify the Android identity proposal when P6 starts.
