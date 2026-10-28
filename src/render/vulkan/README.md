# src/render/vulkan/ — the Vulkan renderer backend

NOT RETAIL. The third backend behind `src/render/common/README.md`'s seam, picked with
`BANISHED_RENDER_BACKEND=vulkan`. Targets: Linux (the default renderer there since P5) and Android
(Adreno 8xx); Windows is the development bed. The design and the phase plan are
`docs/vulkan/PLAN.md`; this file is the map of the directory.

| File | What |
|---|---|
| `backend.cmake` | The CMake fragment: sources, the Vulkan-Headers include (`-isystem`), ready flag; the `dxbc_spv` static library (dxbc-spirv + SPIRV-Headers submodules); the unit additions; the `vulkan-shader-check` target. |
| `shader_convert.h`, `shader_convert.cpp` | DXBC to SPIR-V through dxbc-spirv, the binding layout (set 0 vertex, set 1 pixel; constant buffers, then textures, then samplers), the reflection, the VS -> PS semantic link, the cache key and payload. No Vulkan header. |
| `spirv_combine.h`, `spirv_combine.cpp` | P8: the SPIR-V pass that turns the separate texture / sampler bindings into one combined image sampler per sampled pair, recording the pairs in the reflection. No Vulkan header. |
| `spirv_legalize.h`, `spirv_legalize.cpp` | P8 round 2: lowers `OpBitFieldInsert` to shifts and masks in every converted module (the Adreno 830 driver refuses a vertex shader holding one: AO's fullscreen VS). P13, the 1.1 path: `DowngradeSpirv` (SPIR-V 1.6 to 1.3, the demote to `OpKill`, the debug names dropped for the Adreno 640 linker) and `WidenInputs` (a pixel input widened to its vertex output, what maintenance4 otherwise allows). No Vulkan header. |
| `vulkan11.h`, `vulkan11.cpp` | P13: THE 1.1 PATH, chosen from the caps when the device lacks dynamic rendering, synchronization2 or timeline semaphores (the Galaxy S10). The wrappers the backend calls instead of the 1.3 entry points: `CmdBeginRendering` (a cached `VkRenderPass`, a per-frame `VkFramebuffer`), `CmdPipelineBarrier2` (folded onto `vkCmdPipelineBarrier`), `CmdBindVertexBuffers2` (strides baked into the pipeline), `SetPipelineTargets`. One branch each on the 1.3 path. |
| `depth_discard.h` | P8: THE DEPTH DISCARD's prediction by frame-structure signature (pure; `tests/unit/vulkan_depth_discard_test.cpp`). |
| `shader_cache.cpp` | `LoadOrConvert`: the SPIR-V cache over the per-user derived-data cache (`src/ext/derived_cache`). |
| `device_objects.h` | What the other files read from the device file: the live device, queue, properties, `Caps`, the depth and swap-chain formats; shader modules (tracked, destroyed at Shutdown). |
| `vk_mem.h`, `vma_impl.cpp` | VMA (`third_party/VulkanMemoryAllocator`), configured once (no static or dynamic function fetching, API 1.3, or 1.1 on the 1.1 path); the implementation is compiled in `vma_impl.cpp`. |
| `resources.h`, `resources.cpp` | GPU memory (VMA buffers and images, multisampled and attachment-only ones for the main targets), submission (one queue, the timeline semaphore; a fence per submit on the 1.1 path), the upload batch, the frame-indexed retire queue, shared samplers, the placeholders, the read-back buffer, and `ImageState` / `TransitionImage`: per-image layout tracking with sync2 barriers. |
| `pipelines.h`, `pipelines.cpp` | The pipeline layout, the shader registry, vertex layouts, the render-state block, the pipeline key and cache, the persistent `VkPipelineCache` and the warm list with its worker. |
| `draw.h`, `draw.cpp` | The bound state, the render targets (the main targets: swap-chain image, MSAA colour + depth; every render buffer's own images), the lazily-begun pass with its clears as load ops, layout transitions at pass boundaries, Resolve (the frame copy) and the image read-back, the constant ring, descriptor sets, and the ContextData draw slots (Clear, the Bind* slots, DrawIndexed, the dynamic-vertex slots). |
| `vk_api.h`, `vk_api.cpp` | The entry points the backend calls, as function pointers resolved at run time from SDL's `vkGetInstanceProcAddr` (nothing links the loader). Four X-lists: global, instance, device, and the 1.2 / 1.3 device entry points loaded only on the 1.3 path. |
| `VideoVulkanData.h` | The twelve companion subclasses of `src/engine/video/CompanionData.h`. |
| `device.cpp` | Instance, physical device, logical device, SDL surface, swap chain, the frame loop (`BeginCommands` / `Finish`, and `SubmitFrameSoFar` for a mid-frame read-back), the multisample count (dx11 ResolveMultisampleMode), the adapter enumeration, the main-target attach slots, `RenderBufferData::Bind` / `Resolve`, the game cursor (`SetImageBuffer`), the start and stop of the resource files. |
| `companions.cpp` | Buffers (static device-local, dynamic renamed on DISCARD), images (every retail format, mips, cubes; BC1 / BC3 decoded to RGBA8 on a device without `textureCompressionBC`; a render-buffer image's attachment view and layout state), the image read-back slot, constant buffers, programs (through the shader registry), vertex layouts, render states. |
| `integration.cpp` | The backend-neutral seams (`AoBackend::`, `FaaBackend::`, … , `Ext::RenderPipe::`, `Graphics::ExtAoAfterOpaque`): every answer is "not supported" until P4. |
| `render_backend_vulkan.cpp` | `Render::` entry points and the compatibility-slot placeholder. |
| `vulkan_entry.h` | What the plain entry file needs from `device.cpp` without a Vulkan header. |
| `present.h`, `present.cpp`, `present/` | The pre-rotated present pass (P6): a swap chain made with a rotated `currentTransform` gets portrait images, and the landscape frame (the backend's own image) is drawn into them turned. `present/present.vert` / `.frag` are OpenBA's own shaders; `present/present_spirv.h` is generated from them by `tools/vulkan/gen_present_spirv.sh`. |

## Where it stands
P0 (scaffold): the device comes up, the swap chain presents the engine's clear colour at the
engine's back-buffer size, resizes and fullscreen flips rebuild it, and the debug flavour runs
the validation layer when one is installed.
P1 (shaders, done): every vertex and pixel program permutation is converted from the user's
DXBC to SPIR-V with dxbc-spirv and made a shader module; the SPIR-V is kept in the per-user
derived-data cache (`<Documents>\Banished\Cache`, none under the faithful profile). All 465
retail permutations convert and pass `spirv-val` (`vulkan-shader-check`, below).
P2 (resources and draws, done): buffers, textures, samplers, vertex layouts, render states, pipelines
(persistent cache plus a warm list) and descriptors are real, and the main menu renders into the
swap-chain image with the backend's depth image. A RenderDoc capture of the menu under
`/profile:faithful` has the same main-target draws as the DX11 exe's. Render buffers other than the
main one (shadow map, water reflection, frame copy) were skipped in P2.
P3 (render targets, done): every render buffer renders into its own colour / depth images (the 1264x672
reflection, the depth-only shadow map, the minimap capture), the frame copy's Resolve copies the main
colour, `ImageBufferData::Read` reads back (the game cursor, the minimap, `TakeScreenshot`'s BMP), the
Options multisample count drives the main targets (resolve attachments on desktop; the tiler path
`VK_EXT_multisampled_render_to_single_sampled` where the device has it), and a settings change or a
window resize remakes the main targets. A new game boots, plays and quits through the pause menu on
the debug and perf flavours; an in-game RenderDoc capture under `/profile:faithful` at a 60 fps cap
has the DX11 exe's draw count per target. The divergences left are in `docs/known_port_issues.md`
("Vulkan backend"). Next: P4 ext seams and the parity table.

P4 (ext seams): every backend-neutral seam answers "supported" (`docs/vulkan/PARITY.md` has the table
beside DX11 and the retail features). `ext.ambient_occlusion` runs ASSAO from the committed DXBC through
the same converter (`ao/assao_pass.cpp`; typed UAVs map to binding 40 + u, `kMappingVersion` 3) over a
main depth that gets a sampled view on request. foliage_aa, billboard_shadow and draw_batch's smoke VS
convert their rewritten DXBC into second registry entries (`shader_rewrites.cpp`; foliage_aa's A2C clone
is two bits of the pipeline key's render state). async_gpu_create, share_gpu_data, create_on_demand,
compress_ao and texture_quality hook the companions as on DX11 (`seeded_create.h` is the load's one
create: BC4, then create-or-share; seeded static buffers are immutable, as D3D11 makes them).
`VK_ERROR_DEVICE_LOST` from submit, wait, acquire or present begins an empty frame whose Finish runs
dx11's RecoverLostDevice sequence (OnVideoError, device rebuilt, OnVideoRestart); `ext.render_pipe`
splits it into `PresentFrame` and `RecoverLostDevice`, and `fix.boot_device_lost` sends a swap-chain
rebuild that fails on a lost device into the same recovery. `BANISHED_FORCE_DEVICE_LOST` injects the loss
on this backend too. Next: P5 (Linux).

P5 (Linux cutover): this backend is the Linux renderer. CMake picks it by default off Windows and Apple,
so `tools/build.sh build` on Linux needs no flag (Windows keeps dx11, macOS metal); the dx11 backend is
Windows-only and the D3D11-over-Vulkan route Linux used before is deleted (fetch script, the dlopen
resolvers in `src/platform/posix/gfx_posix.cpp`, its run-script environment and its libraries in the game
dir and the release package). `tools/linux_run.sh` pins lavapipe on the WSL bed and passes
`/profile:faithful`, as every Windows route does. The Linux build and unit run pass in CI and on the WSL
bed. The four lavapipe routes ran and passed on 2026-10-25 (WSL2 Ubuntu 24.04, Mesa 26.2.4, tree
`7c06b5184`, debug, faithful profile): `menu`, `newgame`, `world` and `resave` each exit 0 with crash log 0
and no hang, on `[Vulkan] device llvmpipe (LLVM 21.1.8, 256 bits) (cpu) api 1.4.354`, at 7 to 12 fps.
No validation layer is installed on the bed, so the validation count is 0 by absence, not a clean
layer run. `world.txt` for `BANISHED_SEED=12345` is byte-identical to the Windows DX11 debug exe's (46
lines, md5 `585781c8`); `resave` writes the golden image (md5 `971aaec2cf95`, 23,453,453 bytes). The
real-GPU run is the owner's, below. Next: P6 (Android).

P6 (Android bring-up, 2026-10-25): the same backend runs on the Galaxy S25 (Adreno 830, driver 1.3.284)
from `android/` (`docs/android/README.md`). A pre-rotated swap chain (`present.h`): the surface reports
`currentTransform` ROTATE_90 in landscape, so the swap chain takes that transform and portrait images, the
frame renders landscape into the backend's own image and the frame's end draws it turned into the
swap-chain image (OpenBA's own two shaders, `present/`, compiled by `tools/vulkan/gen_present_spirv.sh`);
with IDENTITY instead, every present answered `VK_SUBOPTIMAL_KHR` and the swap chain was rebuilt every
frame. An SDL event watch destroys the swap chain and the surface at `SDL_EVENT_WILL_ENTER_BACKGROUND`
(after the frame the render thread may be presenting) and the next frame remakes both after
`SDL_EVENT_DID_ENTER_FOREGROUND` (`device.cpp`, THE MOBILE LIFECYCLE). Desktop surfaces are IDENTITY:
nothing of either runs there. On the S25 (debug build, FIFO at 120 Hz): the menu and an autostarted new
game render, the pause-menu quit is clean; the first boot converts 127 programs on the phone (menu open at
2.57 s), the second takes all from the cache (2.32 s); `vulkan_pipelines.bin` (292 KB) is written at the
clean quit and loaded at the next boot. The tiler MSAA path (`msrtss=1`) rendered the menu at 2x before
the phone defaults turned MSAA off. No validation layer on the phone yet. Next: P7 (Android fit).

P7 first pass (Android fit, 2026-10-25; `docs/android/P7_FIRST_PASS.md`): the Khronos validation layer runs
on the phone (`tools/android.sh validation on|off`; 4 errors -> 0). Descriptors: with
`VK_KHR_push_descriptor` every shader pair gets its own pipeline layout (`pipelines.h` PairLayout), the
pixel set pushed and the vertex set pooled with dynamic constant buffers and cached per bound state
(Adreno allows one push set per layout); render-thread CPU -25 % on the S25. THE DEPTH DISCARD
(`draw.cpp`): the last depth-using main pass, predicted from the last frame, stores depth DONT_CARE, a
misprediction clears and pauses it for 60 frames. Tiler MSAA renders straight into the present source (no
end copy), its `resolveMode` set, its struct gone from pipeline `pNext`. The present pass also scales: a
frame smaller than the images (the render scale, `Platform::Window::RenderScale`) is drawn into them with
linear filtering (`PresentActive`). `VK_EXT_vertex_input_dynamic_state` is no longer enabled. The pipeline
cache is written at WILL_ENTER_BACKGROUND too. S25 at a fixed 443 MHz: stress 98 -> 162 fps (half res),
seeded town GPU busy 70 % -> 32-40 % at 120 fps.

P8 (2026-10-25; `docs/android/P8_VULKAN.md`):
- **The depth discard** predicts the last main-depth pass by the frame's structure signature
  (`depth_discard.h`). A structure change before the pass (the game after the menu) starts unarmed, with no
  wrong frame; one miss still turns it off for the session. An empty clear pass attaches only what it clears.
- **The main target is split twice, no longer three times.** Render buffers drawn before a main pass, and the
  frame copy, move to SHADER_READ_ONLY between passes. Game frame: 4 passes, depth 1/2 discarded, 0 misses,
  on the S25 and on Windows.
- **The game's programs use combined image samplers** (`spirv_combine.{h,cpp}`; shader_convert.h COMBINED
  IMAGE SAMPLERS): one descriptor per sampled texture. The retail pairs are all (t_i, s_i).
  - `kMappingVersion` 5;
  - the pipeline-cache magic `OBAVKPC2`;
  - the AO pass keeps separate bindings.
- **The sampler key rule** (P9, `docs/android/P9_RENDER.md`). The sampler cache (`GetSampler`) is keyed by the
  whole `SamplerDesc` (filters, compare, address modes, anisotropy, `maxLod`), and every other
  `VkSamplerCreateInfo` field is a constant there: `mipLodBias` 0, `minLod` 0, the border colour. A field that
  ever varies goes into `SamplerDesc`, never into `GetSampler` as a constant or a global read, so equal keys
  always mean equal samplers and a shared `VkSampler` (the game's, the AO pass's, the present pass's) is safe.
  A texture's two samplers are made when it is created (DX11's rule). A combined descriptor takes the sampler
  of the image bound at the pair's slot (`TextureSampler`). After the AO pass, `ResetCommandState` makes the
  next draw push its sets again from `g_bound`. Measured on the S25 and on Windows: AO on / off or changed
  live leaves every texel AO does not darken bit-identical.

## Validation on the Windows bed
The debug flavour enables `VK_LAYER_KHRONOS_validation` when the loader lists it. With the LunarG Vulkan
SDK installed, its layer is registered with the loader and used as is (no `VK_ADD_LAYER_PATH`; the
SDK's `Bin\spirv-val.exe` serves `vulkan-shader-check --spirv-val`). With no Vulkan SDK
installed, a layer built from Khronos's source with the MinGW toolchain works: build
`KhronosGroup/Vulkan-ValidationLayers` (tag matching the loader) with `scripts/update_deps.py` and
CMake + Ninja in a SHORT path (the dependency builds exceed MAX_PATH under the session scratchpad),
link it with `-DCMAKE_MODULE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"` (the loader does
not find MinGW's runtime DLLs beside the layer), then run with `VK_ADD_LAYER_PATH=<build>\layers`;
`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1` adds synchronization validation. Messages land in
`standalone_error.log` as `[Vulkan] validation ...`.

## Checking the shaders
```bash
cmake --build build/cmake-debug-vulkan -t vulkan-shader-check
build/cmake-debug-vulkan/vulkan-shader-check.exe "C:/GOG Games/Banished"   # [--spirv-val <exe>]
```
It reads `WinData/*.pkg` of the install named (parked mods in subfolders stay out), converts every
permutation, round-trips each cache payload and validates each module with `spirv-val` from the
Vulkan SDK or SPIRV-Tools when one is found (a structural check otherwise), then prints the table
and exits non-zero on any failure. It writes nothing but one temporary `.spv` in the system temp
folder.

## Build and run (Windows dev bed)
```bash
BANISHED_RENDER_BACKEND=vulkan tools/build.sh run-menu      # build/cmake-debug-vulkan/OpenBA.exe
```
`tools/build.sh` keys the build directory on the backend, so the DX11 build is untouched. The
exe lands in the same game directory as the DX11 one (`BANISHED_DIR`), so one or the other is
deployed at a time. Log lines are tagged `[Vulkan]` in `standalone_error.log`.

## Rules of the directory
- Vulkan 1.3 core + `VK_KHR_swapchain` is the floor; anything else is probed into `caps` and
  used only when present (PLAN.md §4.1). The reference device is a Galaxy S25 (PLAN.md §2.1).
- No game content: shaders are converted from the user's packages at load and cached under the
  user's profile (PLAN.md §4.2).
- Nothing outside this directory names `VideoVulkan::`; a feature the backend cannot do answers
  "not supported" through the seam header, never a silent stub.
- One statement per line (`.claude/rules/style.md`).

## Linux lavapipe routes on the WSL bed
Run and passed on 2026-10-25 (the P5 paragraph above has the results). WSL2 needs the hypervisor on: with
`hypervisorlaunchtype off` (the uProf setup) it does not start, and turning it on is an admin
`bcdedit /set hypervisorlaunchtype auto` and a reboot. From Git Bash in the checkout, one command per step:

1. The driver and the loader are there (lavapipe must be listed at 1.3 or higher; if `vulkaninfo` is
   missing: `sudo apt install mesa-vulkan-drivers libvulkan1 vulkan-tools`):
   ```bash
   wsl -e bash -c "vulkaninfo --summary 2>&1 | grep -E 'deviceName|apiVersion|driverName'; ls /usr/share/vulkan/icd.d/"
   ```
2. Copy the working tree, submodules included, into the WSL home (a build over `/mnt/c` is slow):
   ```bash
   wsl -e bash -c "rsync -a --delete --exclude .git --exclude build --exclude runs /mnt/c/code/GIT/untitled_rev_eng_project/ ~/banished-main/"
   ```
3. Build and test (the default configure is Vulkan, no flag; the scripts are CRLF in the checkout):
   ```bash
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/build.sh) build debug --jobs 4 && bash <(tr -d '\r' < tools/build.sh) unit debug --jobs 4"
   ```
4. Rebuild the game dir (`OpenBA` + `libSDL3.so.0` + the WinData link, nothing else):
   ```bash
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/linux_gamedir.sh) build/cmake-debug ~/banished_release '/mnt/c/GOG Games/Banished'"
   ```
5. The four routes, lavapipe pinned by the script (`VK_ICD_FILENAMES` / `VK_DRIVER_FILES`); `resave` needs
   `~/.local/share/Banished/Save/Gate.sav` (`tools/resave_gate/README.md`):
   ```bash
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/linux_run.sh) menu    ~/banished_release ~/lx_runs/p5_menu    3600"
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/linux_run.sh) newgame ~/banished_release ~/lx_runs/p5_newgame 3600"
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/linux_run.sh) world   ~/banished_release ~/lx_runs/p5_world   3600"
   wsl -e bash -c "cd ~/banished-main && bash <(tr -d '\r' < tools/linux_run.sh) resave  ~/banished_release ~/lx_runs/p5_resave  3600"
   ```
6. What to grep in each `~/lx_runs/p5_<route>/route.txt`:
   ```bash
   wsl -e bash -c "grep -h -E '^\[linux_run\] [a-z]+: exit|^\[Vulkan\] (instance|device|swap chain)|validation messages|fps|RESULT=|world:' ~/lx_runs/p5_*/route.txt"
   ```
   A pass is `exit 0 crashlog=0 hang=no`; the device line names `llvmpipe` (lavapipe), never `Microsoft
   Direct3D12` (dzn). `world`: compare `~/lx_runs/p5_world/new.world.txt` with the Windows DX11 exe's for
   the same seed (`BANISHED_SEED=12345 BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_QUIT=120 tools/build.sh run`
   on the functional game dir, the `world` route's environment; then the same greps and seds as
   `linux_run.sh` over its `standalone_error.log` and `standalone_debug.log`); report byte-identical or the
   first differing line, never fix silently. `resave`: the `[Roundtrip] RESULT=` line (DIFF against the
   loaded image is normal; the verdict is against the golden) and the resaved md5 against
   `docs/runtime_port/resave_gate/golden/Gate.resaved.md5` (`tools/resave_gate/README.md`).
   Lavapipe is slow and WSLg throttles an unfocused window to about 5 fps; the routes count frames, not
   simulation, so GameAutoPause does not stall them (only `stress` switches it off).

## Linux real-GPU checklist (the owner's run, an Ubuntu live boot)
Ubuntu 24.04 or newer with the GPU's own Vulkan 1.3 driver (Mesa RADV / ANV, or NVIDIA's).

1. Packages:
   ```bash
   sudo apt install git build-essential cmake ninja-build ccache python3 vulkan-tools libvulkan1 \
     mesa-vulkan-drivers libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
     libxtst-dev libxfixes-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev \
     libasound2-dev libpulse-dev
   vulkaninfo --summary          # the GPU must be listed with apiVersion 1.3 or higher
   ```
   (`vulkan-validationlayers` adds the validation layer the debug flavour turns on when it is listed.)
2. Build (the checkout with its submodules; a non-recursive `git submodule update --init` is enough):
   ```bash
   tools/build.sh build debug && tools/build.sh unit debug     # build/cmake-debug/OpenBA, unit
   bash tools/linux_gamedir.sh build/cmake-debug ~/banished_linux "/path/to/Banished"
   ```
   The last argument is a Banished install holding `WinData` (a mounted Windows partition works; it
   is only read). `resave` also needs `Gate.sav` in `~/.local/share/Banished/Save/`
   (`tools/resave_gate/README.md`).
3. One command per route, with the system's driver instead of lavapipe:
   ```bash
   LINUX_VK_ICD=system bash tools/linux_run.sh menu    ~/banished_linux ~/lx_runs/menu
   LINUX_VK_ICD=system bash tools/linux_run.sh newgame ~/banished_linux ~/lx_runs/newgame
   LINUX_VK_ICD=system bash tools/linux_run.sh world   ~/banished_linux ~/lx_runs/world
   LINUX_VK_ICD=system bash tools/linux_run.sh resave  ~/banished_linux ~/lx_runs/resave
   ```
4. What to look for, in each `route.txt` (copied from `standalone_error.log`):
   - the result line `[linux_run] <route>: exit 0 crashlog=0 hang=no`;
   - `[Vulkan] instance: loader 1.x.y, N adapter(s), validation on` (`validation on` only in the debug
     flavour with the layer installed);
   - `[Vulkan] device <the GPU's name> (discrete|integrated) api 1.x.y ... validation=0|1; multisample Nx`:
     the GPU, not `llvmpipe` (lavapipe) and not `Microsoft Direct3D12` (dzn);
   - `[Vulkan] swap chain WxH, 3 images, format F, present mode P` at the window's size;
   - `[Vulkan] validation messages: N`; the known residue is in `docs/known_port_issues.md` ("Vulkan
     backend": the byte-4 vertex attribute and the depth-only pass's unbound colour output);
   - `world`: `new.world.txt` compared with the Windows DX11 exe's for `BANISHED_SEED=12345`;
     `resave`: `[Roundtrip] RESULT=` and the resaved md5 against the golden in `tools/resave_gate/`.
