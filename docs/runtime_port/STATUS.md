# Runtime port — status

The log of every close, newest first under "Endgame"; never copy it into CLAUDE.md. The open items live in
`NEXT_SESSION.md` (this directory), not here. The DLL-era tail (cutover gate, DLL-present build, held-back list) is
`docs/history/runtime_port/STATUS_dll_era_tail.md`.

## Closure (2026-09-12 close, tools/cutover_closure, roots = census + every minted vtable slot)

| | |
|---|---|
| reachable functions from our call surface | 2,871 |
| unported | 128, all C runtime (our CRT covers them) |
| **engine functions to port (static lower bound)** | **0** (206 → 856 → 145 → 0 in one day) |
| minted vtables | 0 (ENDGAME B.3.9 retired all 152: every class has its compiler vtable); the mint is deleted (2026-09-28) |
| registry doors | NONE (2026-09-28, the registry teardown): nothing registers or resolves a body by retail name or address |

Worklist: `docs/history/runtime_port/closure/WORKLIST.md` (empty). Every function the DLL's call graph
reaches from our call surface OR from any minted vtable slot is ported, and every port is
reachable both by mangled name and by retail address. From here the closure cannot help:
what the gate finds next is state, not code — objects handed to our bodies by a route that
built them wrong, and callbacks stored in data.

## Endgame (2026-09-13)

- **BETTERWATER FANCY -- 2026-11-01 (same branch; two opus agents in worktrees for the DX11 and Vulkan depth copies, merged serially; unit 1672 / 0 / 3 skipped on `main` d3b71f3d3, acceptance 0; gate PASS, resave PASS (both on the rebased tree); stress A/B at 50x CLEAN: CPU draw 1.02 ms on all three legs, fps within noise, +80 MB at Fancy for the MSAA depth copy):** `ext.better_water_mode` 1..2 (Plain / Fancy, default Plain). Fancy adds the shore strip (a mesh of the terrain cells across the waterline, on the terrain's own heights and diagonals, drawn with the river water's vertex program: a wet bank and a foam fringe where the water visibly starts) and contact foam (a third pass reading a scene depth copy at t4, `SceneDepthBackend`, made beside retail's frame copy through `FrameCopyImageBuffer::_extAfterCopy`; DX11 and Vulkan, Metal not supported). The user's tuned defaults. Hand-in `docs/extensions/handins/BETTERWATER.md` sections 5-8.
- **BETTERWATER -- 2026-10-31 (branch `ext/better-water`, based on `main` c2dd2cd5a; exe debug + perf build, unit 1660 / 0 / 3 skipped; Windows DX11 run with the user's spot checks; Linux Vulkan (lavapipe) world route clean, both shore programs converted, 0 validation messages; Android / Metal, the gate, the resave check and a stress A/B not run):** `ext.better_water`, class A, default ON, the Enhanced tab's "Better water" Off / Plain: a shore edge on rivers and lakes. Each water contour range is drawn a second time with our own pixel program (`src/render/common/better_water/shore_overlay.hlsl`, fxc via `tools/water_shaders.py`) through a twin `Video::VideoProgram` that keeps the water material's own vertex layout, vertex program and render state; retail's water shader is not touched. The shore depth is the frame copy's alpha, which retail's terrain and in-water models already write (`GetUnderWaterAmount`). Layers: a wet, darker bed under the shallows; a thin line at the waterline, white on the outside of bends (an 8-tap ring on the water plane); a wandering waterline; all keyed to the mesh's scroll-cancelled coordinate. The sixteen look values (`ShoreParams`, MaterialConstants b1) are the user's set from the new "Better water" panel of the debug overlay. Fancy (contact foam from depth, a wet line on the bank in the terrain shader, a 3D edge) is planned. Hand-in `docs/extensions/handins/BETTERWATER.md`.
- **SHOWGRID (branch `ext-show-grid`, based on `main` ddd86231; unit tests in `ext_show_grid_test.cpp`; Windows release build looked at by the author in a game 2026-10-31):** `ext.show_grid`, class A, OFF by default, live, no state: thin lines on every tile edge of the part of the map in view, following the ground and hidden behind buildings and trees (the draw copies the tile overlay's render state with the depth test on, writes off); every tenth line stronger; keys `show_grid_opacity` / `_shade` / `_width` / `_hue` / `_saturation`; the Grid tab's check box is a runtime flag (hidden at every game start, not in settings.cfg). Two seams (`GameUpdateLoop::Draw` and `ext.render_pipe`, before the tool draw). Hand-in `docs/extensions/handins/SHOWGRID.md`.
- **CITIZEN STATUS NAME -- 2026-10-30 (branch `fix-citizen-status-name`, based on `main` ac5cdd8; exe build + unit 1586 / 0 / 9 skipped, `abi_audit.py` OK):** `behaviour:` the citizen panel's status line shows the citizen's name again. `CitizenUI::OnUpdateLayout` skipped the `textNameIs` name parameter; it now does what retail `0x140064ae0` does, an unconditional `SetParameter(0, citizen name)` after `buttonHome`'s Enable (no type, address or parameter-array test). `textNameIs` is a `UI::TextElement*`. Known-issue row 1341. Seen in a running game on this branch alone (2026-10-30): the line reads "<name> is idling."; an earlier, guarded form of the set was seen on another branch (2026-10-29).
- **FAMILYTREE -- 2026-10-31 (branch `ext-family-tree`, one commit on `main` 9a253942):** `ext.family_tree` (class C, OFF). Retail keeps only the living links of a citizen (spouse, one parent, that parent's children ring) and cuts them at marriage and death, so it cannot say who a married citizen's parents were or who a dead grandparent was. The extension records births (mother and father), arrivals, marriages, deaths (date, age, cause) and the job each citizen worked longest as they happen, keeps the table in the save tail (format 1) and re-binds living people to citizens after a load. It shows them on the Family page (a people list with a search box, a relations list, a drawn tree window, Show in town), a Family button on the citizen panel, a picture of the whole town's family tree saved as PNG or SVG (an outline of one person's descendants, and each person's main job, on by default), and a Town summary window (the town's name, the date, where people came from, deaths by cause, generations, each year). Seen in a running game by the owner of the request: the page, its records, the drawn tree and the citizen panel's button (2026-10-29); the search box, Show in town, the PNG and SVG save with the descendants outline and the job line, the Town summary rows including the name and date, and the record surviving a save and reload (2026-10-31). `abi_audit.py` fails only on the existing `long` growth outside this change. `handins/FAMILYTREE.md`.
- **DEMOLITION ICON -- 2026-10-27 (branch `fix-demolish-status-icon`, based on `main` 1f9b140; unit 1553 / 0 / 9 skipped, `abi_audit.py` OK; seen in a running game: the icon appears on a house marked for demolition and goes when the demolition is cancelled):** `behaviour:` a building marked for demolition or upgrade now shows the BuildDestroy status icon, as in retail. `DestroyComponent::State3Enter` skipped retail's icon work as "visual-only"; the GOG `Application-x64.exe` `0x14011d7b0` calls `0x14012c2a0` (clear all 32 icon bits, new `StatusIconComponent::ClearAllFlags`) and, when not burning, sets icon bit 5 after requesting state 4. `CancelDemolition` already cleared it. Known-issue row 1337. No extension, no setting.
- **CI WINDOWS SHORTER -- 2026-10-26 (branch `ci/shorter-windows-debug`, based on `main` 64c4fa21d):** the cold Windows job (PR #57's first GCC 16 run) spent 30 min: two whole debug exes and two unit builds, ccache 24 hits in 2998 because a -g compile hashes its build directory. Now: `CCACHE_NOHASHDIR`; the backend's include directory and dxbc-spirv's usage requirements reach only the files that need them (`$<LINK_ONLY:dxbc_spv>` + source properties on shader_convert.cpp), so every other compile line is the same in both configures; and the vulkan configure builds the new object target `render-backend` (`tools/build.sh backend`, 23 files compiled as OpenBA compiles them) instead of a second whole exe. Measured locally on a fresh worktree with an empty cache: the vulkan unit build took 320 of 320 shared objects from the dx11 one (50 misses = dxbc-spirv + the vulkan tests); render-backend compiles 23 files and no SDL; both whole exes still build and link. The required check names are unchanged.
- **VTABLE ALIASES RETIRED -- 2026-10-26 (branch `refactor/retire-vtable-aliases`, based on `main` 64c4fa21d):** `DEFINE_EXE_VTABLE` / `X::Vtable()` (a vtable named by its assembler symbol) is gone with its two no-LTO TUs (`src/game/Object/exe_vtables.cpp`, `src/engine/graphics/mesh_vtable_alias.cpp`) and CMake's `-fno-lto` exception. The vptr readers (walk prefetch: CharacterComponent / ModelComponent / ParticleComponent; ext.walk_batch: MoveTo; gather: MeshInstance; the splash's next-loop test: GameUpdateLoop) read `Object::ClassVptr<T>::Get()` (`src/engine/object/class_vptr.h`), which the class's ctors record (NOT RETAIL, no object or result changes). The StatGoal factory self-test checks the type id; the three print-only vtable fields are dropped. Checked: debug build, unit 1522/0 (PART C of exe_class_abi_test now proves the recorded vptr), gate clean, resave PASS (= golden), LTO release exe links with no `-Wodr`; a one-off in-game count matched every Character / Model / Particle component by vptr and by type id (258 / 265 / 7) and 610 of 616 gathered geometries as MeshInstance. The edited ctors read as coverage-mismatch in the PGO profile until the next `perf/pgo/train.sh`.
- **SLIDERCLAMP -- 2026-10-25 (branch `fix/slider-drag-clamp`, based on `main` 80c1b31f1; exe build + unit, gate not run by the author):** `fix.slider_drag_clamp`, default ON: a dragged slider keeps following the pointer and clamps at its ends instead of snapping back to the drag's start when the pointer strays 32 px off it (retail `SliderElement::Update` 0x1800d67e0); a release anywhere keeps the value; the same row covers `ScrollElement::Update`'s identical snap-back. Engine seams `UI::SliderElement::_extFixDragClamp` and `UI::ScrollElement::_extFixDragClamp`. Hand-in `docs/extensions/handins/SLIDERCLAMP.md`, `docs/retail_bugs.md` §12.
- **VULKAN P7 first pass -- 2026-10-25 (Android fit; `docs/android/P7_FIRST_PASS.md`, survey `docs/android/PERF_P7.md`):** Galaxy S25, one commit per item. Validation layer on the phone (`tools/android.sh validation on|off`): 4 errors -> 0, plus 2 msrtss errors fixed. Lifecycle: pipeline cache written at WILL_ENTER_BACKGROUND, LOW_MEMORY (foreground) / TERMINATING start the game's autosave (`Platform::App::TakeSaveRequest`), the watchdog pauses in the background. Vulkan: pair layouts with a pushed pixel set (render-thread CPU -25 %), predicted depth DONT_CARE store, tiler MSAA straight into the present source, `vertex_input_dynamic` no longer enabled (3 swap images impossible: FIFO min 4). `ext.gather_threads` helpers sleep after a 50 us spin (process CPU -3.2 %, Windows stress unchanged). Phone first run seeds `GameEdgeScroll=false` (ruled) and `VideoRenderScale=50` (new `Platform::Window::RenderScale` hook, 1.0 off Android; the present pass scales): fixed 443 MHz stress 98 -> 162 fps, seeded town GPU busy 70 % -> 32-40 % at 120 fps. MSAA 2x msrtss at half res costs 9.6 % stress fps / +12 % town busy: default left off for a ruling. 60 fps cap unruled: `BANISHED_FPS_CAP` does not pace the game; at a 60 Hz panel the town drops to 44 % busy at 160 MHz and -23 % CPU, while 120 fps holds at 160-222 MHz (68 % busy, thermal 0 over 5 min). Merged tree: Windows dx11 debug build, unit 1472 / 0 (1 skipped), acceptance 0 off target, Windows Vulkan build + new-game run clean, Android debug build + validated phone run clean.
- **VULKAN P6 -- 2026-10-25 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §5 P6; `docs/android/README.md`):** Android bring-up. The Galaxy S25 (SM-S931U, Android 17, Adreno 830, driver 1.3.284) boots to the main menu and then an autostarted new game from the pushed retail `WinData/*.pkg`, and quits clean through the pause-menu harness (`STATE=EXITED ... clean-shutdown`, crash log 0 bytes). `android/` is a Gradle project (wrapper Gradle 8.12.1, AGP 8.8.1, JBR 21; NDK r30 `30.0.16248370`, SDK CMake 3.31.6; compileSdk/targetSdk 35, minSdk 29, arm64-v8a only, `c++_shared`) whose native build is the root `CMakeLists.txt`: an `ANDROID` branch applies the darwin twin rule with `_android` (`src/platform/android/`: app, file, settings, window twins; `src/diag/android/`: crash handler and boot-state watchdog, no `backtrace()` below API 33), builds `OpenBA` as `libmain.so` beside `libSDL3.so`, and turns on SDL's joystick + HIDAPI (its Java glue needs them). SDL's Java glue is compiled from the submodule in place; `OpenBAActivity` runs the game's own `main` and turns launch-intent `BANISHED_*` extras into the environment (the one sanctioned source on Android; `options.h`). New hook `Platform::App::EnterRunDirectory` (empty on Windows, Linux, macOS): Android moves to the data folder `/sdcard/Android/data/org.openba.game/files` (game dir; user dir `Documents/Banished`; logs and boot state beside `WinData/`). Found: a folder adb makes there belongs to the shell user and the app cannot read inside it (EACCES), so the app makes `WinData/` and `tools/android.sh push-data` starts it once first. Vulkan backend: pre-rotation (`present.h`: `currentTransform` ROTATE_90 taken as preTransform, portrait images, the landscape frame turned by a present pass with OpenBA's own shaders; with IDENTITY every present answered SUBOPTIMAL and the swap chain was rebuilt every frame) and the mobile lifecycle (SDL event watch: swap chain + surface destroyed at WILL_ENTER_BACKGROUND, remade after DID_ENTER_FOREGROUND; ran twice on the phone, clean). Phone defaults: a first run writes a `settings.cfg` with MSAA off, shadow size Low (retail has no off) and the graphics extensions off; immersive fullscreen. On the S25, debug build, FIFO 120 Hz: `[Vulkan] swap chain 1080x2340, 4 images, format 37, present mode 2, transform 0x2`, `device Adreno (TM) 830 ... msrtss=1 validation=0`; menu about 104 fps, in game 120 fps (the vsync cap); first boot converts 127 programs on the phone (menu open 2.57 s), second boot all from the cache (2.32 s); pipeline cache 292 KB written at the clean quit and loaded next boot (warm list 30); new game click to first drawn frame 9.4 s cold, 7.1 s warm. The tiler MSAA path rendered the menu at 2x before the defaults turned MSAA off. No validation layer pushed. Not done (P7): half-resolution render scale, LOW_MEMORY / TERMINATING save, validation + HWASan, thermal soak, apk in `release.yml`, signing. Tools: `tools/android.sh` (build / install / push-data / run / pull / logcat), `tools/boot_state.sh --android`. Windows on the final tree: dx11 debug builds, vulkan unit 1473 / 0, acceptance 0 off target, Windows Vulkan new-game boot on the functional dir clean (IDENTITY transform, no present pass).
- **VULKAN P5 -- 2026-10-25 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §5 P5):** Linux cutover. CMake defaults `BANISHED_RENDER_BACKEND` to `vulkan` off Windows and Apple (Windows keeps dx11, macOS metal), so the Linux build of `OpenBA` and `unit` configures with no flag; `src/render/dx11/backend.cmake` answers not-ready off Windows with a message (the dx11 backend is Windows-only: its POSIX arms in `devicedata.cpp` (the UTF-32 `DxgiWideText` narrowing), `swap_probe_dx11.cpp` and the ASSAO `_countof` shim are gone), and `OpenBA`'s `add_dependencies` is guarded for a not-ready backend. The D3D11-over-Vulkan route is deleted: its fetch script, `third_party/` README and `.gitignore` entry, the two dlopen resolvers in `src/platform/posix/gfx_posix.cpp` (only `ReleaseLibrary` stays), its THIRD_PARTY_NOTICES row, its environment lines in `tools/linux_run.sh` / `tools/linux_gamedir.sh` / the release launcher, its two libraries in the game dir and the Linux release package, and the CI / release Linux fetch steps. `linux_run.sh` pins lavapipe's ICD (`VK_ICD_FILENAMES` + `VK_DRIVER_FILES`, `lvp_icd.json` or `lvp_icd.x86_64.json`; `LINUX_VK_ICD=system` for a real GPU) and prints the `[Vulkan] instance / device / swap chain` lines and the validation count. The CI Linux job builds the default (Vulkan) configure once, unit included; its required-check name is kept. Windows on the final tree: dx11 debug builds, vulkan unit 1473 / 0, acceptance 0 off target, vulkan new-game boot clean on the functional dir (RTX 5080, crash log 0). A fresh non-recursive clone of the tree on Windows (submodules from `.git/modules`, the nested `dxbc-spirv/submodules/spirv_headers` left empty, configure with `-DBANISHED_RENDER_BACKEND=vulkan` and nothing else) builds `OpenBA` and `unit` (1473 / 0, 3 skipped for the absent ext kit), the shape the CI job sees; the CI Linux job gains a "Default backend is vulkan" check. **Lavapipe routes RUN AND PASSED 2026-10-25** (WSL2 Ubuntu 24.04, Mesa 26.2.4, tree `7c06b5184`, debug; steps in `src/render/vulkan/README.md`, "Linux lavapipe routes on the WSL bed"): WSL build clean, unit 1477 / 0 / 7 skipped; `menu`, `newgame`, `world`, `resave` each exit 0, crash log 0, no hang, on `[Vulkan] device llvmpipe (LLVM 21.1.8, 256 bits) (cpu) api 1.4.354` (never dzn), 7-12 fps; no validation layer on the bed (count 0 by absence). Fix on the way (`e73eadf41`): `tools/linux_run.sh` now passes `/profile:faithful` like every Windows route; without it the resave image carried the settings.cfg fixes (md5 adba6cd5, 23,453,749 bytes). World verdict: `world.txt` for seed 12345 is BYTE-IDENTICAL to the Windows DX11 debug exe's on the functional dir (46 lines, md5 `585781c8`). Resave: `[Roundtrip] RESULT=DIFF` against the loaded image as usual, resaved md5 `971aaec2cf95f5f730bb1cd1a4fa8428` = the golden, compressed hash `4127020a2ae682eb`. The real-GPU Linux run is the owner's (same README). Closed-phase records (`docs/runtime_port/phase_d/`, `docs/extensions/handins/`, released CHANGELOG sections) keep their history text.
- **VULKAN P4 -- 2026-10-24 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §5 P4):** ext seams; every seam answers "supported", parity table `docs/vulkan/PARITY.md`. `ext.ambient_occlusion`: ASSAO's committed DXBC (`src/render/common/ao/shaders/`) through the same dxbc-spirv path at load, the pass orchestrated natively after upstream `ASSAODX11.cpp` (`src/render/vulkan/ao/assao_pass.cpp`: depth resolve, depth mips + normals through the typed UAV u4, quality-2 generate and blurs, multiply apply), the converter maps `u<n>` to binding 40 + n (`kMappingVersion` 3), device creation enables `fragmentStoresAndAtomics` / `shaderStorageImageWriteWithoutFormat` and probes the AO formats, and the main depth gets a sampled view when the row asks. foliage_aa / billboard_shadow / draw_batch smoke VS: the DXBC rewrites converted into second shader-registry entries (`shader_rewrites.cpp`), the foliage_aa A2C clone as two `StateBlock::extFlags` bits. async_gpu_create (workers in the loader bracket), share_gpu_data and create_on_demand (over the common tables; the draw holds the lazy table's use gate), compress_ao (BC4), texture_quality (the drop in `ImageBufferData::Create`, both samplers' maxLod lowered) through one load create (`seeded_create.h`); seeded static buffers are immutable as on D3D11. Device loss: `VK_ERROR_DEVICE_LOST` from submit, wait, acquire or present begins an empty frame and Finish runs dx11's RecoverLostDevice sequence; `ext.render_pipe`'s split is `PresentFrame` / `RecoverLostDevice`; `fix.boot_device_lost` sends a swap-chain rebuild failing on a lost device into it; `BANISHED_FORCE_DEVICE_LOST` injects it here too. Verified on the RTX 5080 (debug, validation layer loaded, 4x): the gate under `/profile:faithful`, then with the dir's settings (AO, foliage_aa mode 2, billboard_shadow, draw_batch, share_gpu_data, create_on_demand, compress_ao, render_pipe on), then texture_quality Half (lazy and eager, the eager run with async_gpu_create), each exit 0, empty crash log, no hang log, `failed=0`; a forced in-game device loss recovers and keeps drawing on both Finish paths; a forced loss in the boot's world build crashes in the loader on both backends (the retail window, deferred); resave gate PASS on the Vulkan exe (golden `971aaec2`); validation shows only the two P2 residues; unit 1473 on vulkan with the one pre-existing `ext_sheet_kit_crosscheck` failure (the DX11 unit build fails it too), acceptance 0 off target, the DX11 debug build still builds. Next: P5 (Linux).
- **VULKAN P3 -- 2026-10-24 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §4.4):** render targets; a new game boots. Every render buffer renders into its own colour / depth images (attachment view + layout state on the image companion), the main targets are the backend's (`draw.cpp`: the swap-chain image at 1x; with MSAA a multisampled colour resolved into the swap-chain image at the end of every main pass plus a multisampled depth, both stored because the frame copy and read-backs split the main target; the tiler path `VK_EXT_multisampled_render_to_single_sampled` is probed into `caps.msrtss`, enabled and selected where present, untested here: the RTX 5080 lacks it). The sample count is dx11's ResolveMultisampleMode over the adapter list, re-read on every settings change. Per-image layout tracking (`resources.h` ImageState / TransitionImage) with sync2 barriers at pass boundaries only; every companion image the frame touched goes back to SHADER_READ_ONLY at its end; a texture that is an attachment of the bound buffer reads the placeholder. A clear with no draw after it lands as an empty pass at the target change. `RenderBuffer::Resolve` copies (or blits) the main colour into the frame copy; `ImageBufferData::Read` reads back through a staging buffer, inside a frame by submitting the frame so far (`SubmitFrameSoFar`) and between frames on its own command buffer; `DeviceData::SetImageBuffer` sets the game cursor (`Platform::Cursor::SetFromTexels`). Fixed on the way: P0's `AttachDepthBuffer` did not stamp the owner's flags 0x20 (nor addressing / filter, nor the size after a resize), so `Device::RecreateDynamic` rebuilt the default depth as a plain image after a settings change and the main passes ran without depth (decals over buildings); both attach slots now write dx11's fields in dx11's order. Device creation also enables maintenance4, shaderDemoteToHelperInvocation and shaderDrawParameters when present (validation flagged the converted shaders), and the upload batch orders a fill before a later copy into the same buffer (a synchronization-validation hazard). Verified: menu with river reflections and shadows at 4x and 1x; THE GATE (autostart new game, ~60 s, pause-menu quit) exit 0, empty crash log, on debug and perf; the game's own screenshot route writes a correct BMP; three window resizes (menu, in game) and a fullscreen boot; an in-game RenderDoc capture under `/profile:faithful` at a 60 fps cap has DX11's draw count per target (reflection 79 / 79, shadow 105 / 104, main 103 / 103); the Khronos validation layer (built from source with MinGW, recipe in the README) with synchronization validation reports no layout or hazard error; unit 1415 / 0, acceptance 0. trace_oracle cannot compare the two backends (`dump_state.py` reads the D3D11 pipeline state). Divergences left: `docs/known_port_issues.md` "Vulkan backend". Next: P4 ext seams and the parity table.
- **VULKAN P2 -- 2026-10-24 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §4.3 / §4.4):** resources and draws; the main menu renders. `third_party/VulkanMemoryAllocator` (GPUOpen, MIT, `v3.3.0`) is a pinned submodule, compiled once in `vma_impl.cpp` with static and dynamic function fetching off (`vk_mem.h` feeds it the run-time resolved table). `resources.{h,cpp}`: VMA buffers and images, one queue with a timeline semaphore (every submit through `SubmitCommands`), an upload batch any thread records into (static buffers, texture levels, `Update`; submitted before each frame, or at 64 MB), a frame-indexed retire queue, shared samplers, white / zero placeholders. `pipelines.{h,cpp}`: one pipeline layout (set 0 vertex, set 1 pixel, 40 bindings each; the first N constant-buffer bindings UNIFORM_BUFFER_DYNAMIC, N = half the device's dynamic limit, 7 on the RTX 5080), a refcounted shader registry (one module per distinct DXBC: 79 of 180 first-load permutations shared), monolithic pipelines keyed on (vs, ps, linked, layout, render state, topology, colour / depth format, samples) with viewport / scissor / stencil reference / vertex stride dynamic, the linked pixel module when `StagesCompatible` fails, a persistent `VkPipelineCache` (`<Cache>\vulkan_pipelines.bin`, header-checked against vendor / device / driver / UUID and a hash) and a warm list (`vulkan_pipelines.txt`) compiled on a worker thread (second boot: 16 / 16 menu pipelines warmed, none built on the render thread). `draw.{h,cpp}`: the D3D11-style bound state, dynamic rendering begun lazily with Clear folded into LOAD_OP_CLEAR, a per-frame constant ring (256-byte aligned) and descriptor pools, and the ContextData draw slots with dx11's counters and quirks. Dynamic vertex / index buffers are host-visible and renamed on DISCARD. P3 left: render buffers other than the main one (shadow map, the 1264x672 reflection target: their clears and draws are skipped and counted), `Resolve` (frame copy), `ImageBufferData::Read` (false: the cursor stays the platform's). Windows, RTX 5080, no validation layer installed: the menu renders (terrain, buildings, trees, UI; water black, no shadows); a RenderDoc capture of the menu's 10th frame under `/profile:faithful` has the same 188 main-target draws (307,177 triangles) as the DX11 exe's, the same multiset and the same UI tail; three boots exit 0 with no crash / hang log; unit 1415 / 0 / 0 on vulkan, acceptance 0 off target, the DX11 debug build still builds. Next: P3 (render targets).
- **VULKAN P1 -- 2026-10-24 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md` §4.2 / §4.3):** shaders. `third_party/dxbc-spirv` (MIT, commit `687a301`) and `third_party/SPIRV-Headers` (MIT, `c8ad050`, the commit dxbc-spirv pins; it stands in for dxbc-spirv's nested submodule through two generated forwarding headers) are pinned submodules, built by `src/render/vulkan/backend.cmake` as static library `dxbc_spv` (no Meson). `src/render/vulkan/shader_convert.{h,cpp}`: `dxbc::Converter` -> `ir::legalizeIr` with DXVK's CompileOptions -> `LowerIoPass` (pixel: `resolveUnwrittenPsOutputs`; with a vertex program given, DXVK's semantic link `resolveSemanticIo` + `resolveMismatchedIo`, for P2's pipeline builder when `StagesCompatible` fails) -> `SpirvBuilder` with the PLAN.md §4.3 layout (set 0 vertex, set 1 pixel; b0..b7, then t0..t15 at 8 + t, then s0..s15 at 24 + s; retail uses b0..b4, t0..t6, s0..s6). Reflection: registers per class and the input / output semantics. SPIR-V goes through the derived-data cache (`shader_cache.cpp`, producer `VKSV`, key = DXBC Hash64 + dxbc-spirv commit + `kMappingVersion` + stage + link fingerprint; none under the faithful profile, which then converts every boot). The program companions convert each permutation (the DX11 skip / abort-on-first-failure loop) into a `VkShaderModule`; `device.cpp` tracks the modules and destroys the ones still held at Shutdown (176 at the menu quit). `vulkan-shader-check <game dir>` (`tools/vulkan/shader_check.cpp`) reads the user's packages: 153 programs, 465 permutations (215 vertex, 250 pixel), all converted, all pass `spirv-val --target-env vulkan1.3` (SPIRV-Tools built locally, not in the tree; a structural check stands in when none is on PATH), cache entry round trip 465 / 465. `tests/unit/vulkan_shader_convert_test.cpp` (vulkan configure only; synthesised SM4 blobs): 6 tests, unit 1415 / 0 / 0 on vulkan, 1409 on dx11. Windows run: boots to the menu and quits with no crash / hang log; the second boot serves all 180 first-load modules from the cache. CI's Windows and Linux jobs gained a vulkan unit step. Next: P2 (resources and draws).
- **VULKAN P0 -- 2026-10-24 (branch `vulkan/p0-scaffold`; plan `docs/vulkan/PLAN.md`):** the Vulkan renderer backend's scaffold, `src/render/vulkan/` (`BANISHED_RENDER_BACKEND=vulkan`, its own build directory `build/cmake-<flavour>-vulkan`; `third_party/Vulkan-Headers` v1.4.366 pinned, entry points resolved through SDL's `vkGetInstanceProcAddr`, nothing links the loader). Instance / device / SDL surface / swap chain / three-frame loop; the engine's clear colour presents through BeginCommands / Finish; the resource companions are stubs that keep host copies (the buffers' Serialize is faithful: it consumes the inline payload, or every later stream read desynchronises -- the first run hung in `GraphicsMesh::PlatformSerialize` for that reason). Every ext seam answers "not supported". The one place outside a backend that named `VideoDX11` (main.cpp's swap-chain probes) went behind `Render::DumpSwapChainState` / `Render::BootedExclusiveFullscreen` (DX11 bodies in `src/render/dx11/swap_probe_dx11.cpp`, Metal and Vulkan answer "not probed"). Windows run on the RTX 5080: boots to the main menu (no drawing), the auto-Quit click exits 0, no crash / hang log; the DX11 debug build still builds. CI's Windows and Linux jobs gained a Vulkan configure + build step (jobs not renamed). Next: P1 (dxbc-spirv shaders), P2 (resources and draws).

- **RELEASE 1.0.1 -- 2026-10-23 (release workflow + version bump):** `.github/workflows/release.yml` builds the release flavour on Windows, Linux and macOS, runs the unit tests on that flavour, packages each platform (`tools/release/package.sh`; `tools/macos/make_app.sh` + `make_release_zip.sh`) and publishes the GitHub release from a `v*` tag with the matching `CHANGELOG.md` section as its notes; the tag must equal `v<CMake VERSION>`. A manual dispatch runs the same jobs as a dry run (artifacts, no release); pull requests do not run it. The FrancophilSans files live in `tools/release/fonts/` now (the user's call: Manfred Klein's terms allow passing them on unchanged with his notice), so a release builds from the repository alone. The Linux package is the first shipped Linux build (exe + libSDL3.so.0 + the two dxvk-native libraries + `OpenBA.sh`). `CMakeLists.txt` VERSION 1.0.1; the first `CHANGELOG.md`.
- **LOADPROGRESS -- 2026-10-22 (issue #13, branch `issue13/loadscreen`, based on `main` 30d98f24c; unit 1281 / 0 / 1 skipped, Windows runs only):** `behaviour:` the loader loops' Startup returns at once (the standalone's `WaitForEngineLoader` and `BANISHED_SYNC_ENGINE_LOADER` are gone) and the deferred window shows on the first presented frame of any loop: with MegaMod the window is up at ~2.5 s instead of ~49 s, never hung; 24 / 24 clean loader-exclusivity boots. Retail's cost comes with it: the main thread's Present holds `Video::Device`'s lock, so the MegaMod boot is ~8.6 s slower than with the parked main thread (profiled). `ext.load_progress`, class A, ON by default, only with a mod enabled: the boot's loader frames show `Dialog/Loading.rsc` with "Loading mod X of Y". Seams `LoaderLoop::_extFrame`, `UpdateLoop::_extLoadProgress`. Hand-in `docs/extensions/handins/LOADPROGRESS.md`.
- **TERRAINBATCH -- 2026-10-20 (branch `ext-terrain-batch`, based on `gpuframe` 208c2a787; static build + unit only, not run):** `ext.terrain_batch`, class A, ON by default: each grid row's run of visible terrain chunks is one draw item and one `DrawIndexed` over a copy of retail's chunk index list repeated per chunk (same vertices, indices, order and state; 479 chunk draws a frame -> about 40 at zoom 130), and the shadow pass drops chunks whose shadow box, swept along the sun, misses the view (211 -> about 134). Seams in WorldGeometry (`_extTerrainBatch`). The coordinator's A/B and trace_oracle runs are listed in the hand-in `docs/extensions/handins/TERRAINBATCH.md`.
- **TERRAINRESORT -- 2026-10-22 (branch `red-square`, main merged at 86796fd8b; unit 1286 / 0 / 0, Windows gate clean; Windows only):** `fix.terrain_resort`, default ON: a free cell stored type 10 (too steep) that the classifier now calls buildable is set to 2 after every `Map::ResetTerrainHeights` (rectangle grown by one) and once on a load. Reproduced from a player's save pairs with `tools/cell_dump/` (227 / 294 / 222 / 445 stale cells, 0 after load with the fix). Hand-in `docs/extensions/handins/TERRAINRESORT.md`, `docs/retail_bugs.md` §8.
- **NOFOG -- 2026-10-22 (branch `ext-no-fog`, based on `main` dd8a060; unit tests and the exe build are listed in the hand-in; Windows only):** `ext.no_fog`, class A, OFF by default (a second class A exception beside `eye_camera`, the maintainer's call), live: the fog maximum of the blended lighting record is forced to 0 at the end of `WeatherSimulation::BlendLighting`, so no fog is mixed into the picture when the camera is zoomed far out. One seam; both the shipped draw path and `ext.render_pipe` read that function. Hand-in `docs/extensions/handins/NOFOG.md`.
- **TOOLNAMES -- 2026-10-29 (branch `ext-tool-names`, based on `main` 38fb508; exe build + unit 1575 / 0 / 9 skipped, `abi_audit.py` OK; seen in a running game on Windows: a modded tool is named, a new pick-up is named, a citizen with no tool and the row off read as retail, and the names survive a save and a load, also after a second save made before the panel was opened; Linux and macOS not run):** `ext.tool_names`, class C, OFF by default: the citizen panel names the tool the citizen carries from the item's own string table instead of retail's two canned words. The pick-up (`CitizenAI_PerformWorkCtor5_bd070`) notes the item per citizen with its use count; `CitizenUI::OnUpdateLayout` shows it when the citizen's use count (+0xA8) matches, else retail's word. The notes are recorded in the save tail (entity id, name, gender, the item's template path, the use count; format 1) and claimed by the panel after a load. Hand-in `docs/extensions/handins/TOOLNAMES.md`.
- **HIGHLIMITS -- 2026-10-20 (branch `ext-high-limits`, based on `main` 6dfc487; unit 1191 / 0 / 6 skipped, exe builds; Windows only):** `ext.high_limits`, class B, OFF by default: the limit boxes of the Town Hall, the Limits window and the production buildings take 0..999,999,999 and nine typed digits (retail: 999,999 and six), with a wider minimum box so columns stay aligned. One seam in `ResourceLimitUI::OnCreated`. Looked at in a Windows run (typing, the arrows past 999,999, box widths, save and reload). Not run: Linux / macOS, the gate, the resave gate. Hand-in `docs/extensions/handins/HIGHLIMITS.md`.
- **BETTERCAM -- 2026-10-18 (one agent, branch `ext_better_camera`; unit 1180 / 0 / 5 skipped, exe builds clean, looked at by the user in a run on 2026-10-18):** `ext.better_camera`, class B (PR review: it can leave retail's range in two serialized fields), OFF by default: the two Banished Game Tweaks camera fixes in source. Zoom limits 20..130 -> 6..200 (the floor is 6, not the exe fix's 1, which clips the terrain at the shallow tilt; the span recomputed as max - min, as the exe fix does) and a steady tilt (the pitch blend uses the near slope pair at every zoom). One seam in `PlayerToolManager::UpdateCamera` (`camera_input.cpp`), toggle read once per frame; off = retail's constants and expression, proven bit-identical by `ext_better_camera_off_is_retails_pitch_to_the_last_bit`. With the row off the zoom target is clamped into 20..130 every frame (a save or a camera preset written with the row on can hold more; a no-op for every retail value). Icon `Entry_better_camera.png` via `tools/ext_icons/camera.py`. `handins/BETTERCAM.md`.
- **RESTRICTEDFOODS (2026-10-21, PR #11 merged as main f5dc71df6; unit 1248 / 0 failures / 3 kit skips, gate in town, resave PASS against the faithful golden, acceptance 0 lines off target; the Foods page and the save round trip looked at by the author on the merged revision; the stress run shows no measurable cost for the row; the class C resave golden `GateRestrictedFoods` is cut (md5 a5a1dc48, `tools/resave_gate/README.md`); the window reopens with the save (EXTHOSTS)):** `ext.restricted_foods`, class C, OFF by default, `formatVersion` 1: the foods checked on the Foods page are not eaten by citizens. The list is the town's own state: its record in the save tail carries it (`Ext::RestrictedFoods::Codec()`, wire v1, keyed by template path; `SaveTail::StateCodec`, `Writer`, `Reader`, `ResetAllState` and `ApplyState` are new and shared). Two eating seams (`CitizenComp_Entity_impl_ef40` category 0, `ResidenceComponent::GetFoodCount`) test one flag and scan a short flat array. The control is a Foods page in the new shared extension window: ONE toolbar button (the Reports group) opens `Dialog/ExtensionsWindow.rsc`, a utility-style window with a tab per active page (`src/ext/toolbar/`, `Ext::Toolbar::Contribution`). Hand-in `docs/extensions/handins/RESTRICTEDFOODS.md`.
- **ITEMPRIO (branch `ext-item-priority`; unit tests, the exe build and the acceptance script are listed in the hand-in; Windows only; the page and seams after the rework not yet looked at in a game; the stress run and the resave golden are not run):** `ext.item_priority`, class C, OFF by default, `formatVersion` 1: the town ranks the Fuel / Tool / Health / Clothing items its citizens fetch from a storage, and a citizen takes the best-ranked item. The same step (keep only the rows of the best rank) is added to the three copies of the item pick (`CitizenComp_Entity_impl_ef40`, the fuel fetch's inline pick, the health fetch's); across storages the four finder call sites run once per rank with a rank ceiling (`FindTiered`). The lists are the town's state: the row's record in the save tail (`Ext::ItemPriority::Codec()`, wire v1, keyed by template path). The seams scan a short flat array of ranked materials. The control is a Priority tab in the shared extension window. Hand-in `docs/extensions/handins/ITEMPRIO.md`.
- **ITEMPRIO NEVER (2026-10-27; branch `item-priority-never`; unit tests, and the button, red border and a save and reload checked in a game):** an item of a priority category can be set to **Never** (a second per-category list, the Never button on each row of the Priority page): the three item picks skip it through `Eligible`, so citizens never take it, as restricted foods does for a food. Registry `formatVersion` 2; save-tail wire v2 (entry kind 2), v1 payloads still load. Hand-in `ITEMPRIO.md` section 9.
- **TERRAINEDIT (branch `ext-terrain-edit`, PR #37; unit 1458 / 0 failures / 8 skipped on main c85086d, `abi_audit.py` OK, `acceptance.sh` 0 lines off target; Windows only; the brushes, Undo, water, textures and a save and load looked at by the author in a game):** `ext.terrain_edit`, class B, OFF by default, `formatVersion` 1, no state of its own (the save carries the heights, the cell types and the splat bytes): a Terrain page in the extension window with five brushes (raise, lower, flatten, mountain, smooth), size and strength sliders and an Undo of the last 30 strokes (and a Redo of what was undone, until a new stroke is painted or something is built on that stroke's ground), and (a follow-up PR) a resource tool that plants trees, plants, rocks and ore from a drop down of what stands on the map, by density. A stroke rewrites the height words, marks the chunks dirty and classifies the cells again; when it ends, the water mesh, the ground texture and the town's water-access list are made again for the region. No retail function is edited. Hand-in `docs/extensions/handins/TERRAINEDIT.md`.
- **TOWNSCREENSHOT (branch `ext-town-screenshot`; unit 1563 / 0 failures / 8 skipped and `abi_audit.py` OK on main 1f9b140; Windows only; screenshots of a 512-cell map looked at by the author in a game, and again after the review fixes: options per frame, pause lock, pipe timeout):** `ext.town_screenshot`, class B (the extension window's pages belong to B / C rows), OFF by default, `formatVersion` 1, no state of its own: a Town Screenshot page in the extension window takes one picture of the whole map from an isometric view (an orthographic camera at 30 or 35.264 degrees, from any of the four corners) as `Screenshots/TownScreenshot<N>.png` with the corners outside the map transparent. The game is paused, the camera visits the map a screen at a time (64 pixel margins cropped, so screen-space effects see their neighbours), each frame is read back and the middles are stitched. While it runs it switches off `ext.render_pipe`, the status icons and (as chosen) the weather and the snow on the ground, and puts them back. A second button raises the game's own screenshot request (the screen as it is). One seam, `GameUpdateLoop::Draw` step 7. Hand-in `docs/extensions/handins/TOWNSCREENSHOT.md`.
- **HITCH CLOSE -- 2026-10-18 (evening; ten opus agents in worktrees over the day; branch `hitch`, every agent branch merged; unit 1173 / 0 / 0, resave gate PASS hash 4127020a2ae682eb, acceptance 0):** to the user's bar (one 140 fps frame over the class median) the fed stress town is clean: 10x faithful PASS over 12000 frames; 50x PASS with `ext.spread_warmth` (B), `ext.drain_split` (B), `ext.background_save` (A) and `ext.async_save_compress` (A) on, the autosave at frame 6000 included (146 ms -> no row: the save is serialised on a worker while the sim is frozen and the screen keeps drawing, files byte-identical, quit / device-lost / toggle / camera closed); 50x faithful keeps only retail's cold-wave scans. The "post-drain burst" and most of the "spawn burst" were OUR unbuffered stderr traces (one WriteFile per character): one write per line tree-wide now, the harness's own rows 20 -> 1. The Extensions sheet is 512 x 512. `handins/HITCH.md` §3b.
- **HITCH -- 2026-10-18 (day; the user's stutter hunt at 10x / 20x / 50x; four opus agents in worktrees food / pathgraph / pfburst / postdrain, branch `hitch`; unit 1167 / 0 / 0, resave gate PASS, acceptance 0):** a per-frame hitch recorder in the stress harness (`standalone_stress_hitch.csv`, `tools/hitch_report.py`, criterion one 140 fps frame over the class median) found the 300 sim-second CitizenList spike to be TWO PORT DIVERGENCES in the PathGraph drain (AssignCitizens priced ~26k commutes with exact A* where retail reads the region-pair tree; BuildPathGraph kept 5 of 278 residences): fixed faithfully, 25-55 ms -> 1.7-2.8 ms, no CitizenList hitch left. The winter cold-wave scans (one PathDistance per residence per cold citizen, retail's work, 13-22 ms at 50x) got `ext.spread_warmth` (class B, OFF). The stress town no longer starves (the barns capped the seed; a harness top-up pass). The sampler ran at 64 Hz before (15.6 ms timer), fixed. 10x: no sim hitch row left; 50x: the post-drain burst (7-9 ms) open. `handins/HITCH.md`.
- **KITFREE -- 2026-10-18 (night; three opus agents in worktrees RSCTEXT / RSCSPRITE / RSCFONT, serial merges `22b520894`, `bff96ac21`, `8adf8a350`; unit 1162 / 0 / 0; the faithful gate run built a world with the seam never asked):** **the extension resources are built inside `OpenBA.exe`; the Modding ToolKit is no build prerequisite.** Design `docs/extensions/KITFREE.md`: a resource PROVIDER per path prefix (`src/ext/resources/Resources.h`), asked by the factory's FindOrLoad only after `External::Load` missed on disk (the one engine seam, `external_compiled.cpp`, armed by the first Register and never under `/profile:faithful`), the built object registered through `ExternalFactory::Add` like a loaded one. (1) RSCTEXT: our three `.rsc` sources embedded as text (`tools/ext_rsc_embed.py`, CMake `ext-rsc-text`) and built by a POSITIONAL reader -- retail stripped the field names from every Serialize, so a per-class schema in Serialize order drives an `IO::ReaderWriter` that answers only the text-named slots; derivation = fresh object + binary round trip of the base + the text; 56 objects provided, **52 / 52 with a kit `.crs` byte-identical** (`/rsccheck:1`, `tools/ext_rsc_check.py`). (2) RSCSPRITE: the 27 icons baked to RGBA (`tools/ext_sprites_bake.py`), a shelf packer with the kit's 1-texel replicated ring, SpriteSheet + MaterialInstance + ImageBuffer (BGRA8, one level, immutable) -- the sheet record 3116 bytes identical to the kit's but for placement, every sprite's pixels identical. (3) RSCFONT: the kit's rasteriser IS GDI (`UI::Font::Compile` in the profile DLL: CreateFontIndirect at the pixel height, GetGlyphOutlineW GRAY8), ported in `src/platform/win/font_raster_win.cpp` against the installed face or `FrancophilSans.ttf` beside the exe (AddFontResourceEx private, GetTextFace verified, never a substitute); bitmaps pixel-identical, 720 / 768 metric rows exact, the rest one pixel moved between pre and post bearing at the same advance; variants built ON DEMAND (four per scale, 6-9 ms each); `tools/release_package.sh` + `tools/release/FrancophilSans_README.txt`. **The user's ruling on the typeface**: Manfred Klein's terms forbid bundling in compilations / products; read as aimed at font-pack CDs, the TTF ships in the RELEASE PACKAGE ONLY (never the tree) with his readme, a credit + the Doctors Without Borders donation request in README's Legal section, the retail-glyph fallback kept. Close-out run with every kit file moved out of `WinData/`: the button and its icon, the dialog (21 rows, every icon), the Enhanced tab, and ext.sharp_ui at 83 % drawing the four in-exe variants (`Font->29 FontSmall->17 FontMedium->22 FontLarge->58`), screenshot seen. One fix found by the run: ext.sharp_ui tested the disk before loading a variant (`LooseCompiledExists`), now `|| HasProvider`. Deploy no longer runs `tools/ext_resources.sh` (kept as the cross-check oracle; the kit stays git-ignored); the stale `WinData/Dialog/AoOptions_*.crs` found among the loose files. Left: the Linux font twin answers absent (FreeType later); the exact kit rule for the 48 bearing rows (a Ghidra read of `Font::Compile`); `ExternalFactory::Clean` re-provision not exercised; the user's own look at the live slider with the in-exe fonts -- DONE 2026-10-18: menu button + icon, the dialog, the Enhanced tab, the slider through every step and a loaded game's panels all GOOD by the user; BOTH DONE the same night: (a) merge `5b8daddbc` font-compile -- `UI::Font::Compile` (profile DLL 0x180159820) takes its metrics from `GetCharABCWidthsW` on a DISPLAY DC (pre = abcA, post = abcB, kern = abcC; OUT_RASTER_PRECIS / PROOF_QUALITY), the sheet packer is a height-then-width qsort + guillotine; our GDI rasteriser now matches **5472 / 5472** kit rows; (b) merge `e992d51f5` FREETYPE -- FreeType 2.13.3 vendored as a submodule (FTL, 141 KB of text, tt / sfnt / smooth / raster1 only, environment properties OFF) as the posix rasteriser: line heights, baselines and advances GDI's exactly, 95 % of boxes equal, 3 % of pixels one level of 64 apart (GDI's scan converter, not reproducible); GDI STAYS on Windows (exact), FreeType is Linux's (not yet compiled there: WSL2 was down, the hypervisor is off); a custom `ft2build.h` replaced the leaked `-D` defines that broke windres on icon.rc; unit 1165 / 0 / 0.
- **RELEASE PREP -- 2026-10-17 (night; merges `620ab6895` exe-rename, `aafd78c8e` ao-menu; unit 1152 / 0 / 0, acceptance 0):** (1) **the exe is `OpenBA.exe`** (Linux target `OpenBA`; was `reimpl-x64.exe`): 92 live files renamed, `docs/history/` and the four recorded uProf CSVs kept as written; the two identity sites are the launcher's own-module checks in `src/app/main.cpp`; every process-by-name tool (boot_state, game_dir_lock, stress, gate, resave, captures, symbolize) follows; deployed and launched once, boot_state and the lock see the new name; the stale `reimpl-x64.exe` deleted from the game directory. (2) **AO on the main-menu backdrop** (`handins/AO-MENU.md`): `MenuDialogLoopT::DrawScene_PhaseA` arms / disarms the seam around its Draw exactly as the game loop does; the menu's own PlayerController distance feeds the radius; live Apply at the menu works; menu frame -13 % with the row on (676 -> 589 fps uncapped); the minimap is the one never-AO scene now. (3) The Extensions menu icon is a white antialiased silhouette like the kit's ButtonIcons (no rim). (4) `tools/build.sh` default `--jobs 8` (agents keep 2). README and LICENSE (GPL-3.0) in; the user finishes the README by hand, then the lead tags v1.0.0 and the user makes the GitHub release (exe only, never the compiled resources: the SharpFont .crs are the game's glyphs) and flips the repo public.
- **SHARP-SPACING -- 2026-10-17 (late; one opus agent after the user's look; merge `5d029255d`; unit 1152 / 0 / 0, acceptance 0, the faithful path untouched):** the user saw the letter spacing of ext.sharp_ui text reshuffle at every UI-scale slider step. Not the variants' advance tables (never read: layout was always the base font's and only Flush swaps the glyph image) but the per-glyph pen snap `round(pen * px)`: at a fractional ratio neighbouring pens round independently, so each letter pair gets the variant's hinted gap +-1 px in a pattern that is a function of the ratio. Fix: **word runs** (`UI::SharpRun` / `SharpRunPen` in `SharpDraw.h`): the first glyph of a word snaps, each following glyph steps by the previous glyph's VARIANT advance while the base layout is contiguous, the run held within 1 device px of retail's fractional pen by whole-pixel nudges; the native band (98-102 %) now swaps the font for itself at one texel per pixel instead of resampling retail's quad. Measured: positional unevenness sd 0.49 -> 0.28 (Font), 0.52 -> 0.27 (FontSmall), 0 in the native band; in game the stray gaps at 117 % and the touching glyphs at 99 % are gone, row widths within a pixel of retail's. Hand-in `docs/extensions/handins/SHARP-SPACING.md`. Left: the user's own look at the live slider; the label shadow pass offset is now per word; `kSharpRunTolerance = 1.0` reasoned, not tuned. Also this evening: README rewritten for the public repo, LICENSE = GPL-3.0 (the user's ruling), decomp/ and the FAA checksum keys ruled keep; the user does the v1.0.0 release and the visibility flip by hand.
- **CONTENT PURGE + HISTORY REWRITE -- 2026-10-17 (evening; the user's rulings on NEXT_SESSION 2.5; four fix agents in worktrees, serial merges; no game run; unit 1151 / 0 / 0, acceptance 0 off target):** (1) `src/ext/ui/Dialog/Extensions.rsc` re-authored as our own markup (every element derives from a game element by reference and overrides only what differs; identical non-trivial lines vs the kit's `Dialog/Mod.rsc` 84 -> 0; the controller's names kept, row template `entryRow`); the 54 `Font/SharpFont*.rsc` now derive from the game's `Font/<Kit>.rsc` / `<Kit>Material.rsc` / `<Kit>Texture.rsc` by reference (`tools/ext_fonts.py`); `tools/ext_resources.sh` compiles all of it with the kit (54 variants, ext_font_check byte-identical). The on-screen look is UNVERIFIED (no run this session): open the Extensions dialog and ext.sharp_ui text on the next run. (2) The short retail quotes out: the DXBC cutout grammar in FAA-SPIKE.md / FOLIAGE_AA.md is prose, the fire-chance table in disaster_harness.cpp a pointer, the RawMaterialWood / template values and the award text in the history docs elided, 276 `[GUL::DiseaseList]` tuning rows in 23 archived logs collapsed to one placeholder each. (3) The Sheep1 fingerprint (64 RMS / peak pairs, 65 samples, the peak) out of `xwma_decode_test.cpp`: the decode test now checks count, non-silence, no rail clipping, chunked == whole and determinism (proven to fail on a zeroed buffer). (4) The five Ghidra hash indexes untracked and gitignored (`tools/ghidra_mcp/hash_index/*.json`, `docs/history/runtime_port/runtime_hash_index.json`; local copies kept); `tools/ghidra_mcp/regen_hash_index.py` + `hash_index/README.md` regenerate them from a running Ghidra (script untested against a live server). (5) **git filter-repo over the whole history** (backup bundle in the session scratchpad): the xWMA fixture, the 14 screenshots (both their paths), every hash index (root, tools/, docs/ paths), the 59 pre-rewrite blobs of Extensions.rsc / SharpFont*.rsc, and the quoted retail lines (literal + the DiseaseList row regex + the award string, blobs and commit messages) are gone from every commit; pack 142 -> 47 MB; every commit hash changed (HEAD was `e2858d014`, the pre-rewrite tree is byte-identical but for the replaced lines). **Force-pushed the same evening after the user's look**: `main` + the 7 tags, and the 48 stale remote branches (merged wave / fix / backup branches) deleted, so origin holds only the rewritten history (GitHub's own GC of unreachable objects is the one thing left). One test fix on the way: `win_temp_file_round_trip_and_cleanup` assumed the ':' of the local time at [2] (en-US `h:mm tt` puts it at [1] after noon). Memory `project_repo_review_session_20261017.md`.
- **REPO REVIEW + CLEANUP -- 2026-10-17 (the user; three review agents, six fix agents in worktrees, two independent reviewers; merges `b410d4154`, `2ce92b599`, `1abc54811`, `22ddc2fed`, `874927a62`, `478f1bc31`; no game run, no behaviour change):** (1) **portable tooling**: env `BANISHED_DIR` is the one game-directory variable (build.sh forwards it to CMake's `BANISHED_GAME_DIR`; game_dir_lock.sh / capture_arm.sh fall back from either name; deploy / run fail with one line when it is missing), `tools/banished_dir.py` for every pefile tool, the phase_d kit and loadkit derive REPO from `${BASH_SOURCE[0]}`, `.mcp.json` relative (the renderdoc module path `${RENDERDOC_MODULE_PATH:-absolute}`: AddDllDirectory rejects a relative one), every tracked `.sh` 100755, root `.gitattributes` (`* text=auto`, LF for sh / py / cmake); fresh-clone checklist in `docs/reference/environment.md`. (2) **unit tests 1241 -> 1151 / 0, 0 skipped**: ~1,600 sizeof / offsetof checks that restated a header `static_assert` deleted (every one verified against its header by the reviewer; the PMF slot-order checks stay), every fake-class-only probe test gone (one qualified-call canary kept, graphics_material), two empty bodies and one tautology out, 13 misnamed tests renamed, `UNIT_SKIP(reason)` counts and lists skips (never `ok`); `tests/unit/README.md` rewritten for the CMake build. (3) **coverage**: `StateMachine::Tick`, `Filename::GetPathExtension`, `Map::GetTerrainPos`, the MT19937 `System::Random` and the four ported AVL cores now run on the host (bodies MOVED verbatim into `statemachine_tick.cpp`, `filename_extension.cpp`, `modification_avl8.cpp`; `BANISHED_UNIT_PORTED_AVL` admits the specialisations in one TU); `copy_drift_test.cpp` hashes the `[COPY-PINNED]` regions in moveto.cpp / citizenai_executor.cpp / mapquery.cpp so the literal copies in still_path / ext_walk_batch / still_terrain tests cannot drift unseen; new string / filename / registry-duplicate-key / Windows file layer / ParallelFor cases. (4) **content**: the Sheep1 xWMA fixture (retail bytes) and the 14 game screenshots left the tree; xwma_decode_test reads the packet run from the user's install at test time (sha256 + scan fallback) and pins an RMS / peak table plus 65 sparse signed samples; `images/ba_icon.png` is our own exe icon and stays. Still owed before public (NEXT_SESSION 2.5): git history, `decomp/`, the Ghidra hash indexes, a docs sweep. Acceptance 0 off target; the debug exe links. Memory `project_repo_review_session_20261017.md`.
- **DEVICE-LOST PORTED -- 2026-10-11 night (merge `8b89fcfd5`, opus block d5-devlost, gate 0 problems; row 890 FIXED; `PORT GAP` lines 3 -> 1, the last being a comment):** `GameUpdateLoop::OnVideoError / OnVideoRestart` = retail 0x140095970 / 0x140095930 through the new `WorldGeometry::OnVideoError / OnVideoRestart` (0x1400b0be0 / 0x1400b0c50: the four buffer companions, the height image, then the terrain mesh / height image / water mesh rebuilt and re-uploaded), `Minimap::OnVideoRestart` (0x1400a59a0) and the engine's `ImageBuffer::Release` (0x1800f33c0, declared since B5, never ported); the menu loop's twin slots 0x14009a8d0 / 0x14009a8b0 wired to the same pair. No guard added; every null test retail's. The forced route (inject at Present 12000, `runs/DEVLOST1`): INJECT=1 COMPLETE=1, **zero later hr=0x887a0005**, working set 296-323 MB flat for 220 s after the trip (2026-10-05: ~100 MB per frame, 20 GB in 600 s), the run quit itself through the pause menu -- DEVLOST_FINDING's H1 (the non-dynamic terrain / minimap objects crossing devices) was the whole cause. Hand-in `phase_d/handins/DEVLOST.md`; worktree and branch removed after the merge. Left: the user's look after a forced loss (terrain / water / minimap drawn), and the same under WSLg.
- **OPEN ITEMS STARTED -- 2026-10-11 (night; the user's order: Linux release / stress, then the CMake gate):** (1) housekeeping: the 64 merged block branches and the `oldbase` worktree deleted; device-lost RE-OPENED by the user (row 890; NEXT_SESSION section 2.4 is now a task). (2) **Linux release + perf flavours built under WSL2** (ccache, `~/banished-main` = an rsync of main; unit 1012 / 0 in both), menu / new-game / back-to-back-boot routes clean on both (exit 0, no crash log; row 1282 re-tested 2 of 2 clean: Engine::Startup 3 s and 1 s, no stall); `tools/linux_run.sh` gained a `stress` route, the `LINUX_VK_ICD` override and the AutoPause switch-off; **finding row 1320**: the unattended Linux stress town sat PAUSED (GameAutoPause on focus loss under WSLg; two runs at 0.00 sim iterations per frame while every other column looked healthy) -- the route now disables the option for the run and prints `simIters/frame` per window; **row 1321**: the first real Linux stress number (perf flavour, lavapipe): sim 0.59 ms / iteration, update 5.92, 91 fps -- polluted by the concurrent Windows gate build, re-measured below; the Windows baseline (2026-09-05, 15814 entities) is stale and needs a re-cut. **dzn (Mesa Vulkan over the host GPU, the user's ask) tried and RECORDED row 1322**: the RTX 5080 is seen (Mesa 26.2.4 from kisak), but dzn exposes Vulkan 1.2 and every DXVK since 2.0 wants 1.3 -- the Linux bed stays lavapipe. WSL is at 3.0.1 (`wsl --update`: already the latest). (3) **THE GATE IS CMAKE + NINJA** (`tools/build.sh`, the one entry point: build / unit / deploy / run / run-menu / stress / clean per flavour, exes in `build/cmake-<flavour>`): gate.sh, mergegate.sh, stepclose.sh, every route script, resave_gate.sh and stress_run.sh call it; CLAUDE.md, environment.md, the stress-test / menu-driver / triage-run skills and perf/README re-pointed; the Makefile stays as legacy (deleting it is the user's call). Proven by a full clean gate (`GATE_TABLE.md` row CMAKE-GATE): the 4 problems of the first run were path-only warning diffs (baselines re-cut: 0 new by message, 16 Makefile-era warnings gone) and +1 load on both routes = the two .pkm mods mounted for the evening's hand checks (parked again -> 5694 / 0, 5773 / 12 exact); the confirming skipbuild gate at **0 problems**, typetable `965b527a`, resave 3/3 == golden, unit 1002 / 0, castmetric 2, abi_audit OK. (4) **Stress, quiet machine, both hosts:** the Windows CMake perf exe sim 0.17 ms / iteration (0.19 on the 2026-09-05 baseline; `perf/baselines/current.csv` RE-CUT from it, old copy kept), the Linux perf exe 0.46 (row 1321; 2.7x per iteration on the same tree -- recorded, not chased). (5) The Makefile DELETED (the user's call) with `tests/unit/sources.mk`. (6) By hand, the user, the same night: sprite-capture button, bridge tint, town-hall worker add, livestock sounds -- all good; left unseen: a drawn road, worker remove, the END click on the perf exe.
- **SITREP + ARCHIVE -- 2026-10-11 (evening, after the looks):** no phase open; Endgame B's tree (`endgame_b/`, 607 tracked files, 1.8 GB with its ignored run logs) archived to `docs/history/endgame_b/` by `git mv` with every live path re-pointed (`tools/restructure/rewrite_paths.py`: 37 files, comments / READMEs / the gate kit / .gitignore); its two live scripts moved out: `tools/castmetric.py` (still 2) and `tools/acceptance/acceptance.sh` + `codegrep.py` (now counts `src`, the tree since D.1 -- its old `code runtime standalone engine dx11` list had matched nothing since the restructure; 0 off target either way). STATUS.md's DLL-era tail -> `docs/history/runtime_port/STATUS_dll_era_tail.md`. known_port_issues: the nine keep-both duplicates (1180, 1181, 1240-1242, 1247, 1260, 1262, 1263) collapsed to their resolved copies. The Phase D gate outputs COORD1..RESIDUE-2 committed. **The open items, each with its owner and its done-condition, are `NEXT_SESSION.md` beside this file** (the old `endgame_b/NEXT_SESSION.md` is history): row 1292's cosmetic fold, four by-hand checks unseen (sprite-capture button, a drawn road, the END click on the perf exe, livestock sounds), Linux (row 1282 re-test, a release exe, fullscreen / alt-tab / device-lost), device-lost DEFERRED (user). Housekeeping the user decides: 64 merged block branches (`b6-*`..`d4-*`, six `worktree-agent-*`) and the `oldbase` worktree.
- **THE PORT GAP HUNT -- 2026-10-11, same day (NEXT_SESSION section 2.5 DONE; the user directing, seven opus blocks):** `PORT GAP` lines 128 -> **3** (the two device-lost slots 0x140095970 / 0x140095930 the user DEFERRED, row 890, and one comment that says "no PORT GAP"); known_port_issues **0 rows OPEN** (21 at the start: 8 FIXED by block ROWS, 7 found fixed earlier and re-pointed, 1162 RULED, the six platform rows + 1282 RECORDED not-a-defect, 890 DEFERRED (user); the 13 new rows the hunt filed -- 1291-1296, 1300, 1301, 1310-1312 -- all FIXED or RULED but 1292's fold (two seed functions into one Map member, cosmetic) and 1295 (kept, RULED)). Blocks and gated merges (`phase_d/GATE_TABLE.md` rows COORD1, SLOTS2A, LUT, SLOTS2B, CAI, DRAW, ROWS, DRAW-2, RESIDUE, TAIL; every merge at 0 problems; 141 commits, 76 `behaviour:`): the component vtable-slot bodies (Animal, AutoPickup, Character, Growth, Picking, UI, StateMachine incl. its root table and Startup, StatusIcon, Highlight, Decal, StorageDisplay, StorageLocation, CitizenAI OnMessage / OnAdded with 26 arms through SendComponentMessage, Happiness OnMessage, Pickup Slot12), the .rdata LUTs named, the region build at the map create AND the load tail with FillZones + the shore loop (new games now hold their shore water sources), Weather::DrawUpdate, PlayerToolManager Startup / Update with UpdateHotkeys faithful, EditTool::CloseInfoDialog, GameContainer_Init at the inner loader (every boot-time stand-in deleted), GameLoadDialogLoopT::Startup's tail, ResidenceComponent::EnterBuilt whole, SpriteCaptureUI with UserInterface::BeginSpriteCapture, Dialog::GetReference, the temp-file prefix "SRS", retail's CRT qsort for the candidate list (row 290), PastureProduce's tail, and ~40 port-only tests dropped. Invariants: new game 5694 / 0, tutorial 5773 / 12, typetable `965b527a`, unit 1002 / 0, castmetric 2, abi_audit OK, worldcmp identical to the pre-hunt exe; **resave golden RE-CUT once to `971aaec2cf95`** (Growth Slot11's one PostUpdate at load steps 864 display-scale floats, measured byte by byte, deterministic over 5 runs) and PASS on every later merge. One instrument fix: the post-Startup type dump is one fwrite (the loader thread's TRACE lines spliced it once GameContainer_Init ran there). **LOOKS DONE by the user the same evening (2026-10-11, debug build with the DebugMenu mod): menu / splash / options / achievements / scenario, the new game (foundation decals, Agri tab and herd, fog, house decal at clearing, picking, the outline, tool cancel and presets, the panel close), the load (pinned panels, picking, status icons, state machines), AutoPause, pile counts, the tavern, the forced fire and tornado with their event popups and loops, the weather sounds for rain and snow -- no issue found.** The list as it was owed (one route per body in `phase_d/handins/*.md`): click a loaded building / citizen, loaded state machines resume, status icons on the first loaded frame, a disaster popup, a house decal turning opaque at site clearing, the selection outline following a moving model, an unpinned info panel closing off-screen, the menu boot (cursor, UI sounds, fade, Achievements / Scenario dialogs), fog capped at 0.95, pinned panels reopening on load, AutoPause on focus loss, tool cancel / camera presets, the loading splash's sounds / hourglass / scale, the tavern's ale, a new game's Agri tab and starting herd, a pasture dropping every pile, the sprite-capture button, the Linux WSL2 re-test of row 1282. Lessons: memory `feedback_gap_hunt_team_lessons.md`.
- **POST-CLOSE AUDIO HUNT -- 2026-10-11, same day, with the user listening (the decisions taken):** decisions: (a) the VEH exit-3 proof CLOSED as proven by history (B-phase crashes, the Linux SIGSEGV crash log); (b) `System::Error::_hideDisplay` NOT taken: a Startup-failure box on an unattended run stays a finding; (c) the music loop wrap ACCEPTED (clean by ear, no 512-sample drop wanted); (d) the LISTEN / window / GAPS by-hand checks all PASSED by the user (menu music + wrap, no-drop cursor, new game ambience, pause-menu pause, quit). The listen then found the river silent, which opened five fixes on main, every one gated at 0 problems: `e9f7a978f` row 1286 (AmbientEmitterDescription::Serialize serialized its count INTO the array header so the on-load SetSize never allocated -- no emitter description ever bound a sound; start-town passes 1 / 3 never positioned the river / global emitters; the audio listener was the camera-to-world X AXIS, never the position: retail = PlayerController target x / y + camera translation z), `46abe31cc` the gate kit (new-game invariant 5694 / 0, resave golden `9c18dd25` 23,453,453 bytes: payload byte-identical, +11 resource-table rows = the six sounds), `3be0d99f9` ANIMAUDIO row 1287 (AnimAudioEvent::Construct 0x140087e40: every animation-triggered sound -- work sounds, animals -- was silent), `1920a1a3b` CLICK row 1289 (the END path and all 56 sound tails proven clean with unit tests + an SDL pull harness; one START defect fixed: SDL's first pull omits the resampler padding, a 0.15 ms notch 21 ms into every resampled voice; row 1276 WITHDRAWN: the tornado burst was the XWMA scan reading FastLZ-compressed packets, in-game decode is 56 / 56 short=0), `8ac08ae08` AUDIOCOMP rows 1288 / 1290 (Entity_FindAudioComponent = FindComponent<AmbientEmitterComponent>; residence AddResident / RemoveResident = the CROWD loop of an occupied house -- never the town hall; fire Ignite / Extinguish = the building crackle; tornado Tick = the loop follows the funnel; OnRemove stops every slot; fire + tornado routes clean), `ce6fba40d` SetSlot records only on a zero handle as retail 0x140042a10 (the tree's on-demand PlaySlot deviation removed). Unit 1002 / 0. By ear (user): river fades with distance, wind clear, work sounds, animals, birdsong, the house murmur and its stop on demolition. STILL OWED: the END click heard once (no our-side cause shown; listen again on the perf exe), livestock sounds (need traded animals), `debugui.cpp:271`'s audio arm, Linux row 1282 (PulseAudio open stall on a back-to-back boot under WSLg), a perf / release Linux exe, device-lost / fullscreen / alt-tab on Linux.**
- **PHASE D.4 WAVE 4 CLOSED -- 2026-10-11 (GAPS `420d6cea2`, SDL-5 D1 `d5c90621c`, SDL-4 `909045eb1`, SDL-5 D2 `31a37222f`, LINUX-1 `80859a14b` + LINUX-1b `d389b2e36`, ABI C.8 `c1cac6135`, every merge at 0 problems; unit 1000 / 0 on Windows, 1010 / 0 on Linux; the SDL3 audio backend plays every shipped sound from PCM decoded at load by the vendored WMAv2 decoder and streams the music through DecodeChunk on the Stream worker; `audio_xaudio2_win.cpp`, `input_dinput_win`, `window_win.cpp` DELETED, DINPUT8 gone from the imports, no COM init of ours, no `-lole32` of ours; **the first Linux game build** (CMake non-WIN32 `OpenBA` over dxvk-native 3.1.1, SDL3 shared on Linux only) boots to the menu and into a new game under WSL2 lavapipe: 5683 / 0, the seed-12345 world byte-identical to the Windows baseline, the resave byte-identical to the golden; rows 1273 / 1275 / 1279 FIXED, 1276-1278 and 1280-1285 filed; abi_audit and castmetric are gate checks; acceptance 0 off target; stress from a CLEAN rebuild on a quiet machine: update p95 2.54 ms vs 2.40 baseline (the 3.49 / 4.16 readings were incremental-build artefacts), sim ms/iter -8 %, fps 139 -- recorded, not chased). USER DECISIONS OWED: the VEH exit-3 proof (no route forces it); `System::Error::_hideDisplay` for unattended Startup-failure boots; the music loop-wrap 512-sample drop (measure on Windows or accept); the by-hand checks (audio LISTEN list in `phase_d/handins/SDL-4.md`, SDL-5 window / Shift / cursor checks, GAPS pause-menu sound pause, drag a file onto the window = no drop cursor); Linux: row 1282 (PulseAudio open stall under WSLg on a back-to-back boot), a perf / release Linux exe, device-lost / fullscreen / alt-tab on Linux. NEXT: `docs/history/endgame_b/NEXT_SESSION.md` §2.3 (the decisions owed, asked FIRST), then its §4 "After that".**
- **PHASE D.4 WAVE 3 CLOSED -- 2026-10-11 (AUDIO-A, SDL-C + Esc fix, XWMA, POSIX at 0 problems; unit 990 / 0 on Windows, **1000 / 0 under Linux GCC 13.3 in WSL2**: every layout assert holds on Linux; input, window, cursor and the audio seam are SDL / Platform::; DirectInput gone; every shipped sound measured xWMA and the vendored WMAv2 decoder is within 1 LSB of XAudio2; row 1274 fixed (ambient loops), rows 1273 / 1275 filed). NEXT: wave 4 per `phase_d/D4_RULES.md` §2 (SDL audio over the decoder, delete the Win32 backends, the first Linux game build over dxvk-native, the gap rows, the close).**
- **PHASE D.4 WAVE 2b CLOSED -- 2026-10-10 (RENDER-3, SDL-B + its focus fix, THREADS-2, ABI-2 at 0 problems; unit 971 / 0; abi_audit pinned 0; the SDL window runs every route; the trace oracle below the noise floor; settings over char16_t paths; the file / directory / COM seams behind `Platform::`; src/diag/win). Open: the VEH exit-3 proof has no route to force it; the stress p95 deferred to the end of D.4 by the user. WAVE 3 launched: SDL-C (cursor + input), AUDIO-A (the audio seam), XWMA (the decoder), POSIX (the Linux twins + the dry run under WSL2). Rows: `phase_d/GATE_TABLE.md`.**
- **PHASE D.4 WAVE 1 CLOSED -- 2026-10-10.** Five opus planning agents wrote `phase_d/plans/{SETTINGS,THREADS,ABI,SDL,RENDER}.md` (web-verified); the coordinator's synthesis is `phase_d/D4_RULES.md` (C1-C11, waves, the user's answers: SDL3 3.4.18 static submodule, std+pthreads on POSIX, xWMA decoded AT LOAD chunked via an FFmpeg WMAv2 extract, DXVK fetched by script, WSL2 Ubuntu as the Linux bed). Findings that changed the plan: input is DirectInput 8 (not raw input); EVERY shipped sound is xWMA; the tree is already Itanium-ABI (MinGW), so Linux changes no pinned layout and no save field; d3d11 / dxgi are LoadLibrary'd. Six gated merges at 0 problems (`phase_d/GATE_TABLE.md` "Phase D.4"): SETTINGS-1 (codec + `Platform::DocumentsDir`), THREADS-1 (`src/platform/common/platform_{sync,thread,time}.h`, the 0x28 locks as in-place `LockStorage`, no layout change), ABI (`tools/abi_audit.py` + baseline, `src/engine/abi.h`, `long` -> `int32_t` B11 0 DIFF), RENDER-2 (ResolveGfxImport behind `Platform::`; output name from DXGI `DeviceName`; trace oracle S1-S6 = 0 with a control pair), SDL-A (SDL3 3.4.18 vendored at `third_party/SDL`, `SDL_Init(0)`, `Platform::Window`, WindowProc in engine/application), SETTINGS-2 (`<Documents>/Banished/settings.cfg` replaces the registry, one-time read-only HKCU import verified exact and the registry untouched across 7 runs, the launcher dialog = two one-shot entries, `IO::Registry` deleted, row 34 FIXED). RENDER step 1 done with zero code: DXVK 3.1.1 runs the game on Windows (newgame 5683 / 0, resave 3/3, 10 min menu, RenderDoc records a D3D11 capture under DXVK; `phase_d/dxvk_route.sh`). Fixed on the way: the exe imported `libwinpthread-1.dll` since before D.4 (`-static`, 938472544); the D.3 path-mapped warning baselines carried CRLF (phantom "new warnings", re-cut). Invariants: new game 5683 / 0, tutorial 5773 / 12, typetable `965b527a`, resave 5/5 == golden on every merge, unit 920 -> **953** / 0, castmetric 2, worldcmp IDENTICAL (md5 4ff6354d). sa-stress 139 fps, sim ms/iter +3 % (PASS), **update p95 2.40 -> 3.49 ms RECORDED, not bisected** (candidate: THREADS-1's out-of-line lock / timer primitives, one call per allocator lock; a header-inline Windows body needs hand-declared CRITICAL_SECTION prototypes, not `<windows.h>`: a user decision). Wave 2b launched: THREADS-2 (file I/O, dirs, COM, diag split), RENDER-3 (the 22 devicedata window / cursor sites), SDL-B (SDL window + pump, display, message boxes), ABI-2 (WideString `char16_t`, `_stricmp`). NEXT: merge wave 2b, then wave 3 (input, audio seam, POSIX impl, the Linux dry run) per `D4_RULES.md` §2.
- **PHASE D.1-D.3 + CMake DONE -- 2026-10-10: the restructure.** User decisions at the start: row 1162 RULED (keep 0.0f);
  rows 1202-1204 FIXED with the decal "material-load race" root-caused as NONE (block SLOTS: six port-only early
  returns in `DecalComponent::OnAdded`; slot 4 = retail Startup, Map slots 6 / 12 = retail AddToMap with starttown's
  0x100000 pre-sets and MarkBuildingCells deleted -- building minimap dots now painted -- RawMaterial slot 9 = retail
  Update; rows 1270-1272 filed, 1271 / 1272 LEFT); `engine/types` dissolved (block ENGTYPES: 8 forwarders deleted,
  `common.h` -> `global_types.h`, B11 IDENTICAL on 16944 functions). Both merges gated at 0 problems
  (`phase_d/GATE_TABLE.md`); the post-block worldcmp is the row-1203 shape (pickmap lines gone, tree cells Obstacle:
  buildable 73809 -> 49461, 101 fewer scattered resources) and is the Phase D world baseline (`runs/POSTBLOCKS_worldcmp`).
  Then four pure moves, one commit each, a gate after each, B11 against the pre-move -O2 objects with names mapped
  IDENTICAL on every object: `code/Game` -> `src/game`; `runtime/{types,src}` folded per namespace -> `src/engine`
  (4 private headers renamed `*_internal.h`, `runtime/funcs` at the root) + the 35 `*_win.cpp` -> `src/platform/win`;
  `dx11` -> `src/render/dx11`; `standalone` -> `src/app` (launcher) + `src/diag` (56 diagnostics / harnesses); build
  output -> `build/{debug,og,perf,unit}`. `tools/restructure/restructure.py` resolves every quoted include as GCC does
  and rewrote only the 2325 of 12922 lines that would otherwise change target (0 unresolved); `rewrite_paths.py`
  re-pointed scripts / docs / comments; the Makefile's compile rules are generated per directory. Docs history
  (WAVE*, cutover*, evidence, notes/, docs/waves, lane logs) is `docs/history/`. `CMakeLists.txt` (flavours debug /
  og / perf / release, Ninja, ccache, `unit` target; cmake 4.4 + ninja via `pip install --user`) builds the same 934
  TUs; the Makefile stays the gate's build until the kit scripts are ported. Invariants unchanged: new game 5683 / 0,
  tutorial 5773 / 12, typetable `965b527a`, resave PASS == golden on every gate, unit 920 / 0, castmetric 2, sa-stress
  139 fps. NEXT: Phase D §2.2, the portable layer (settings file, SDL, DXVK, the ABI audit) -- `docs/history/endgame_b/NEXT_SESSION.md`.
- **B9 (EXCEPTIONS + THE B8 RESIDUE) CLOSED -- 2026-10-10: ENDGAME B.1 decision 7 DONE by inventory; castmetric 3 -> 2;
  LEFT_FREE 121 -> 102 names.** Decision 7 was measured before converting (`b9_session/EH_INVENTORY.md`: a Ghidra
  `.pdata` scan of all three retail binaries): the tree has 0 setjmp / longjmp / SEH sites (the ~25 files of 2026-09-13
  were the oracle and the wave harnesses, deleted in A.4), retail's game code has 0 `catch` frames and the exe 0 throws;
  the Runtime DLL's one throw (`Debug::ForceDump`, a minidump trigger) and one `__try` (`EnumerateVideoModes`' access-
  violation swallow) are diagnostics / a hardware fault (rows 1210 / 1211). So the conversion set was EMPTY: the change
  is `-fexceptions` explicit in the Makefile and the inventory's §5 rule for any future unwinding body. Then the B8
  residue: four opus blocks (RT, RES, SURV, RT2; `docs/history/endgame_b/B9_RULES.md`, `b9_session/handins/`), four gated merges at
  0 problems (`b9_session/GATE_TABLE.md`), 54 block commits (19 `behaviour:`, 33 `B9:`). The introsort escape is gone
  (`Array16Assign` -> `Array<EmitterInstance>::operator=`, `ArrayAssignCore` header-inline: the empty-source free in
  `Scene::Update` GONE, row 880 closed); 13 LEFT rows FIXED (1120-1123, 1126, 1128, 1163, 1164, 1200, 1201, 1080, 993,
  1208, 1209; 1207 NOT A DIFFERENCE) with every port-only test dropped against the retail call sites; the measured
  survivors re-read: `Array<T>::Find` / `Remove` / `Serialize`, `IO::Serializer::Serialize<T>(int, T*, const char*)`
  (the three element-loop serializers), `ResidenceComponent::GetFoodCount` / `GetFuelCount`, `EventLog::YearString` /
  `SeasonString`, `TradeStockTree::Retrace`, the WorkBST templates over the node type, `WorkBase_TreeNodeFree` one
  template, the orchard ctor doors real ctors; 28 stay with a new measurement. Invariants unchanged (new game 5683 / 0,
  tutorial 5773 / 12, typetable `965b527a`, resave 5/5 == `994eb184`, unit 920 / 0, 0 new warnings); acceptance 1 line
  off (10 = 19), 01 = 2. New routes: `utility` (professions), `tradepanel`, `orchardpanel` (+ SELECT), a forced tornado.
  RESIDUE: the user decisions 1162 / 1202-1204 (not asked: the user was absent at the start), rows 1243 (a real missing
  call), 1260, 1230, 1220, 1240-1247, 1261-1263 -- a tracked fidelity backlog (NEXT_SESSION §2.4), not a step.
  **ENDGAME B IS CLOSED HERE (the user, 2026-10-10): no B10. NEXT: PHASE D** -- the restructure (`src/game`,
  `src/engine`, `src/platform/win`, `src/app` vs `src/diag`, `src/render/dx11`; docs history archived; venvs out of
  git; CMake to confirm), then the portable platform layer (a settings file replaces the registry AND the launcher
  dialog; SDL for input / audio; DXVK for the renderer): `ENDGAME.md` §E, `docs/history/endgame_b/NEXT_SESSION.md` §2. Numbers:
  `b9_session/CLOSE_NUMBERS.md`.
- **B8 (THE QUALITY PASS) CLOSED -- 2026-10-10: the 412 free declarations in `exe_bodies.h` / `*_funcs.h` are 121;
  castmetric 3 (unchanged).** Every member of a class in retail is that class's member (declared in its header with the
  retail address, defined where it was, every caller by name), the forwarders and erased cores are gone (`Array<T>::SetSize`
  specialisations, `Set<T>` / `Work/KeyBst.h` over the node type, `FindComponent<T>` / `FindDescription<T>`,
  `SerializeExternal<T>` / `SerializeExternalArray<T>` by retail cell), the ctor doors real ctors, 57 emptied headers
  deleted, `LEFT_FREE.md` REGENERATED by a kept script (`b8_session/leftfree/regen.py`: 121 survivors, each with its
  ruling on its line -- 54 free in retail, 49 with a measurement, the harness rows). FIRST the two user decisions of
  2026-10-09, gated (slot 8 installed as `NaturalResourceComponent::Tick`; SpawnSubResource's placement test wired -- and
  its spawn-chance constant corrected, 1.0f not the 0.98f UV scale). Ran as: the coordinator's inventory (`b8_session/
  INVENTORY.md`, generated) + rule sheet `docs/history/endgame_b/B8_RULES.md` -> seven opus blocks (COMP-A, COMP-B, GAME, ENT, RT,
  MISC, RES) + RT2 + HDR -> 9 gated merges at 0 problems (`b8_session/GATE_TABLE.md`) -> independent audit
  (`b8_session/AUDIT.md`) -> step close. 237 block commits: 108 `behaviour:` (every one a guard / test / store retail lacks
  dropped with the retail call sites quoted: the forwarders' null guards and `this`-null tests (C3), the 36 ExternalPtr
  serializer rows and 10 resize cores on the existing members with their port-only tests and tail zeroing dropped (C4,
  resave unchanged), the B7 residue rows 1000 / 1020 / 1040 / 1041 / 1060-1065 / 1104 / 1205, row 310's four files, the
  liveSeatShuffle flag deleted, ProductionUI's guard root-caused to no route), 129 `B8:`. B11 by `b11pair.py` (C1).
  Invariants unchanged (new game 5683 / 0, tutorial 5773 / 12, typetable `b983f144`, resave 5/5 == `994eb184`, 0 new
  warnings, unit 920 / 0); `worldcmp.sh` identical to the DEC run; sa-stress 139 fps; acceptance 1 line off (10 = 19).
  New routes: `harness_route.sh pick | panelclose` (row 252's finding did not fire), `disaster_route.sh`. RESIDUE: the
  user decisions 1162 / 1202-1204, the LEFT rows (1120-1123, 1126, 1128, 1163, 1164, 1200, 1201, 1080), the introsort
  escape (`Array16Assign` -> `Array<T>::operator=`), the 49 measured survivors (`docs/history/endgame_b/NEXT_SESSION.md` 2). NEXT:
  ENDGAME B.1 decision 7, exceptions. Numbers: `b8_session/CLOSE_NUMBERS.md`.
- **B7 (THE NAMED-PREREQUISITE PASS) CLOSED — 2026-10-09: castmetric 26 -> 3 (the 23 "named prerequisite" casts are
  1; the 3 are 1 named prerequisite + 2 ruled + 0 work).** Each cast waited on one thing the tree lacked, and the
  thing was SUPPLIED: `System::String`'s ctor / copy / dtor / Release / operator= and `rtcoll::ArrayDtorImpl` /
  `MemFree` header-inline (`[[gnu::always_inline]]`, the same reads and stores), the ctor DOORS real ctors
  (`System::Random`, `Engine::Engine`, `Graphics::Renderer` / `Scene`, `Volume::Frustum`'s two — which zero first,
  as retail), `IO::Persistence` / `System::Configuration` holding a real `Collection::Set<Pair<String,String>>`,
  the cell-BST core 0x14001ca60 a template over the node type; then the sites typed (GetNextItem's locals, the
  by-value `Array<uint32_t>` / `<uint8_t>` getters, `TypeRecord::children`, `MaterialTexture::name`, a typed
  `EventLogEntry`, the Engine / Renderer / Scene / Frustum / Random locals, the Vector4 pixel rows) and the two
  harness puns retired. The B6 residue rows read and settled: RES's 24 SUSPECTED 14d items (dropped, or NOT A
  DIFFERENCE with the instruction quoted; 920 / 921 / 922 ruled), LX's 992 / 942 (every `ExternalPtr` load kept
  only `if IsDerivedFrom(cell)`, as retail) / 993 / 994. Ran as: the coordinator's inventory + rule sheet
  `docs/history/endgame_b/B7_RULES.md` -> four opus blocks (RT, CTOR, LX, RES) -> 5 gated merges (`b7_session/GATE_TABLE.md`),
  two with a resave FINDING each root-caused: row 84's find-only Resolve changed the resource LOAD ORDER and moved
  the pointer-keyed profession bucket tree (golden RE-CUT `994eb184…`, C3); row 942's validation nulled callers on
  the WRONG instantiation (the two bucket-tree keys passed as function pointers, 163 `:edge` meshes, the decal
  and nomad materials — fixed on LX2, no re-cut, C4) -> independent audit (`b7_session/AUDIT.md`: 54 OK / 5
  FINDING / 3 QUESTION, applied in CLOSE-A / CLOSE-B) -> step close. Invariants unchanged (new game 5683 / 0,
  tutorial 5773 / 12, typetable `b983f144`, 0 new warnings, unit 920 / 0); `worldcmp.sh` vs `oldbase` identical
  but for the `[PfSeed]` log wording (row 962); `stepclose.sh` clean (sa-stress 139 fps). Acceptance: 1 line off
  (10 = 19, the accepted miscount); line 01 = 3 of 3. ENDGAME B.1 decision 7 DECIDED by the user (C++ exceptions).
  RESIDUE handed to the quality pass: rows 1000, 1020, 1040, 1041, 1060-1065, the introsort escape, the two
  unprobed routes (`docs/history/endgame_b/NEXT_SESSION.md` §2). Numbers: `b7_session/CLOSE_NUMBERS.md`.
- **B6 (THE RETAIL-DIFFERENCE TASK) CLOSED — 2026-10-09: castmetric 89 -> 26 (the 63 "retail-difference task"
  casts are gone; the 26 are 23 named prerequisite + 3 ruled + 0 work).** Every one of the 63 marked a place
  where the tree did something retail does not (`known_port_issues.md` §13-14); each difference is fixed in
  its own `behaviour:` commit quoting the retail instructions read (160 `behaviour:` commits, one per retail
  BODY): the 14b save-format rows first (every one byte-identical, the golden unchanged), then the wrong
  callee / object / overload rows (WORK, COMP, UI), the construction / destruction rows (C), the guards
  retail lacks (14 cast lines and ~60 KEPT bullets dropped with the instruction range that has no test;
  the kept ones ruled D4(a)/(b) with the harness route or PORT GAP named), row 13 SETTLED (TRADE: the stale
  `TradeWork` entry was the mirrored find-remove core, already fixed by a4ba1f7ed / row 742 — bisected and
  re-proven), and ten forwarders deleted once every caller called the member. Ran as: the coordinator's
  SAVE unit -> Block 0 inventory (`b6_session/INVENTORY.md`) -> rule sheet `docs/history/endgame_b/B6_RULES.md` -> five
  opus blocks by file set (WORK, COMP, UI, C; D for the no-cast guard lines) + TRADE on the stress route ->
  11 gated merges (`b6_session/GATE_TABLE.md`), 0 problems left standing -> independent audit
  (`b6_session/AUDIT.md`: 104 OK / 4 FINDING / 2 QUESTION, all documentary, applied in CLOSE-D) -> step close.
  Invariants unchanged (new game 5683 / 0, tutorial 5773 / 12, typetable `b983f144`, 0 new warnings, unit
  920 / 0); `worldcmp.sh` vs `oldbase` identical. Acceptance: 1 line off (10 = 19, the accepted miscount);
  line 01 = 26 of 26. FOUND: the resave FIXTURE held 11 citizens with a null profession (retail never makes
  one): once `CheckEducation` read it unguarded as retail, the loaded game died after a PASSed roundtrip —
  fixture and golden RE-CUT (row 990, `tools/resave_gate/README.md`), no guard. RESIDUE handed to B7
  (rows 991-994, 942, 921 / 922, the §14 rows D did not reach): `docs/history/endgame_b/NEXT_SESSION.md` §2. Numbers:
  `b6_session/CLOSE_NUMBERS.md`. Next: the named-prerequisite pass (23 casts -> 0).
- **B5 (THE DX11 SEAM) CLOSED — 2026-10-05: castmetric 336 -> 89 (src/render/dx11/ 240 -> 0; the 89 are 63
  retail-difference + 23 named prerequisite + 3 ruled + 0 work).** The twelve `VideoDX11::*Data` PODs are
  real `final` subclasses of the twelve `Video::*Data` bases, which flipped from `DeletingDtor(unsigned)` to
  `virtual ~XData()` (typed `_owner`, class-scope `operator new` = `MemAlloc(n, 4)` / `operator delete` =
  `MemFree`); every slot is an `override` with its retail address; the 84-slot hand-table seam
  (`src/render/dx11/funcs/dx11_seam.*`, `DX11_REGISTER_SLOT`, `Dx11SeamInstall`), `Dx11VtableAddress`, the `Field<>` /
  `Ib*` offset accessors, `Context.h`'s `void*` forwarders, the `VideoRenderState` mirror, the
  `RtVideo_Adapter / GraphicsInterface` doors and the DX9 placeholder's hand table are DELETED. `Adapter` /
  `GraphicsInterface` are real value types with retail's ctor / copy / dtor and a `System::String name`;
  `Device::onVideoError / onVideoRestart` and `Context`'s +0x2100 fields are named; 18 runtime
  `DeletingDtor(1)` sites are `delete`. NO class in the tree declares `DeletingDtor`. Ran as: W0 (typed
  owners, operators, the by-index device-lost dispatch made by-name FIRST) -> three Block-0 agents (two
  inventories, the device-lost finding) -> rule sheet `docs/history/endgame_b/B5_RULES.md` -> PILOT (2 classes) -> four
  opus blocks by hierarchy (BUF, RES, CTX, DEV-A), then DEV-B, then CLOSE -> independent audit -> step
  close. 8 gated merges (`b5_session/GATE_TABLE.md`), 0 problems, each with a bounded `devlost.sh`
  (INJECT=1 COMPLETE=5 crash 0); invariants unchanged (new game 5683 / 0, tutorial 5773 / 12, typetable
  `b983f144`, resave 5/5, 0 new warnings), unit 914 -> 920. Floor: `dx11_seam` category deleted,
  `gpu_map` 0 -> 5, `com_iid` 2 -> 3, `win32` 85 -> 88 measured. Acceptance: 1 line off (10 = 21, the
  accepted miscount); line 01 back in the PROD-total form. Close checks: `worldcmp.sh` vs `oldbase`
  identical; `stepclose.sh` clean (every route, in-game save, sa-stress exit 0 at 139 fps, true release + clean rebuild + gate 0 problems, stub set 0 / 0, census 16 / 16); audit `b5_session/AUDIT.md`. FOUND: the device-lost loop is
  not the harness — one injection, then real repeated removals (`GetDeviceRemovedReason` 0x887a0005, now
  printed), leading hypothesis the exe PORT GAP pair `GameUpdateLoop::OnVideoError / OnVideoRestart`
  (known_port_issues row 890, DEFERRED). Numbers: `b5_session/CLOSE_NUMBERS.md`. Next: the
  retail-difference task, `docs/history/endgame_b/NEXT_SESSION.md` §2.
- **B.4 FINISHING SESSION (S4) — 2026-10-05: 389 -> 336 (149 -> 96 outside `src/render/dx11/`, 0 of them open work);
  B.4 CLOSED.** 11 gated merges (`b4_session/GATE_TABLE.md` rows
  159-169), 0 problems; invariants unchanged (new game 5683 / 0, tutorial 5773 / 12, typetable `b983f144`,
  resave 5/5 on every merge, 0 new warnings, unit 914 / 0). Ran as: recover (39 worktrees, 320 branches;
  the memory was `cpptools`) -> `b4fin-VOID` squashed -> three rounds of opus agents by disjoint file set
  (IBP, SIG, GFX, RTUI; POS, RESOLVE; GFX2, RT2; LAST) -> the coordinator's floor rulings -> `stepclose.sh`
  (clean). The 96 left are 63 retail-difference rows, 23 named prerequisites (13 of them one blocker:
  retail open-codes `~String` / `~Array`), 7 dx11 seam, 3 ruled to stay listed. Acceptance: 5 lines off, 4
  of them `src/render/dx11/`'s, 1 the accepted template-field miscount. Close checks on the final tree: the removal tool by hand (the user: buildings and resources
  removed, no issue), `devlost.sh` both forms (INJECT=1, round trips complete, crash log empty, capped),
  `worldcmp.sh` identical. FOUND: the
  device-lost route grows to 20 GB after one injection (row 890) — the cause of both low-memory stops.
  Numbers: `b4_session/CLOSE_NUMBERS.md`. Next: the dx11 seam, `docs/history/endgame_b/NEXT_SESSION.md`.
- **B.4 THIRD SESSION — 2026-10-05: 1,051 -> 389 (811 -> 149 outside `src/render/dx11/`), NOT closed; stopped by the
  harness for low system memory during the last merge's gate.** 37 gated merges (`b4_session/GATE_TABLE.md`
  rows 122-158), 0 real problems; invariants unchanged (new game 5683 / 0, tutorial 5773 / 12, typetable
  `b983f144`, resave 5/5 on every merge, 0 new warnings; unit 918 -> 914, tests of deleted PODs). Ran as:
  wave Y rounds 2-5 by partition (the re-inventory's doable units) -> an independent read-only AUDIT
  (`b4_session/AUDIT.md`: one ruling per class, all accepted; the tier-I allocation shape grown, four floor
  categories added, `FLOOR.md` re-cut with measured ceilings) -> the closing wave (typed `FindOrLoadAs`
  wrappers, the H8 puns, the String / Filename / WideString mirrors) -> the acceptance passes. About 120
  more classes are real against retail's ctor AND dtor (TownHallUI, Map, WorldGeometry, TradeComponent,
  FileInfo, Mod, GeometryBatch, ActionStack, ParticleComponent, about forty component / work arrays ...);
  about fifteen arrays the port leaked are freed as retail; `Filename` derives from `String` as retail;
  EVERY `Rt<Ns>::` POD mirror and `pod_bridge.h` are deleted (acceptance line 11: 49 -> 0); 1,144
  compiler-derived `offsetof` asserts added (line 10: 1,231 -> 21); stale bridge comments 331 -> 69 (52 are
  `src/render/dx11/`). B.4's acceptance: 6 of 12 lines off target, 3 of them entirely `src/render/dx11/`. Close checks done:
  generated-world comparison against `b2a5434c` (identical), `devlost.sh`, the Mods route. NOT done:
  `stepclose.sh`, quiet `sa-stress`, the removal tool's CalcPath by hand. NOT merged: `b4fin-VOID`.
  What is left and the step after it (the dx11 seam): `docs/history/endgame_b/NEXT_SESSION.md`.
- **B.4 SECOND PASS — 2026-10-05: 8,950 -> 1,051 (811 outside `src/render/dx11/`), NOT closed; wave Y (the
  re-inventory by class) started and was interrupted by machine instability; the user closed the
  session there.** 77 gated merges this session (`b4_session/GATE_TABLE.md` rows 45-121), one problem
  (row 119, a new warning, fixed); invariants unchanged (new game 5683 / 0, tutorial 5773 / 12,
  typetable `b983f144`, resave 5/5, unit 918 / 0); quiet `sa-stress` exit 0, update 1.70-1.77 ms/frame.
  The pass: waves S / L / R (45 file-owning blocks, `PASS2_BRIEF.md`), wave X (declaration families,
  then rounds by partition, `PASS2X_BRIEF.md`), then the re-inventory `data/pass2y/out/`: 812 casts
  in 306 units — 408 doable (197 units, CLASS units as one `behaviour:` commit each), 223 ruling-only
  (one coordinator ruling per class at the close), about 130 blocked with a finding, about 50 erased
  cores that stay. About 60 classes became real classes this session (CitizenBehavior, EntityList, the
  14 Work leaves, Persistence, PackageFile, FileSystem, PlayerController's camera ...), several ending
  leaks retail does not have; about 150 forwarders and the by-retail-address launcher lookup were
  deleted. Kept unmerged and unverified: branches `b4y1-CW`, `b4y1-GAMEA`, `b4y1-GAMEB`. What is next:
  `docs/history/endgame_b/NEXT_SESSION.md`.
- **B.4 (THE CAST METRIC) IN PROGRESS — FIRST PASS DONE 2026-10-02, stopped there by the user.** Scope
  set by the user: the dangerous casts go completely (any spelling of a read by offset or of a `void*` /
  integer receiver reinterpreted as a class), only safe casts stay; the differences from retail found on
  the way are a separate task (`docs/known_port_issues.md` 13, 14, rows to 124). Metric
  (`tools/castmetric.py`, every spelling summed): **46,226 to remove at block 0 -> 8,950**
  at `6a3ccdca` (81% removed); tests row 813 -> 796; 44 gated merges, 0 problems; invariants unchanged
  (new game 5683 / 0, tutorial 5773 / 12, typetable `b983f144`, resave golden, unit 918 / 0). Every block
  of the file map is merged (W0, T, the six runtime blocks, Component a-f, Dialog a-e, Work a-c, Game a-c,
  Action a-b, Player, Tutorial / Goal, Standalone, Tests); nothing is running or half merged. What is left
  is cross-module: casts waiting on a header, signature or raw-storage member another file owns, plus 478
  `B4 FORWARD` forwarder lines. **Next: the second pass, cut by blocker from a fresh inventory, then the
  closing wave, the audit and the close — `docs/history/endgame_b/NEXT_SESSION.md`.** Quiet-machine `sa-stress` at the
  tip: update 1.68 ms/frame, p95 2.15 (base 1.92 / 2.40). Owed at the close: `devlost.sh`, a generated-world
  comparison, one manual run of the removal tool's CalcPath.
  Rule sheet `docs/history/endgame_b/B4_RULES.md`, kit and gate table `docs/history/endgame_b/b4_session/`.
- **REGISTRY TEARDOWN CLOSED (2026-09-28) — ENDGAME B.2 is finished:** nothing registers a body under a
  retail name or address and nothing resolves one. **Deleted:** `standalone/address_registry.{cpp,h}`, all
  127 `standalone/reg_*.cpp`, `resolve.h` / `resolve_standalone.cpp`, `src/engine/rt_port.h` and
  `rt_resolve_cache.cpp`, `rt_export_addrs.inc` (4,926 rows), the mint and its generator, `engine/funcs/`
  (227 externs, `EngineFuncsInit`), the 51 dispatch slot cells, every `X_ResolveFuncs`, the registry
  instruments and their three flags, ten registry-only tools. A call is a C++ call through the declaring
  header (`src/game/<Module>/exe_bodies.h` for the C bodies, with their retail address); a thing with no
  body is a `// PORT GAP 0x…` comment at its call site (189); the singleton cells are named objects
  (`GameUpdateLoop::_singleton`, `g_gameSettings`, `g_gameContainer`,
  `Input::Configuration::_singleton->_keyMap`); `System::MemoryAllocator` is a real class with one API and
  one instrument seat. What is left of boot init is `src/app/runtime_init.cpp` `RuntimeInit()` (five
  calls). Rule sheet `docs/history/endgame_b/B3R_RULES.md` (R1-R11, run §7, what stays §7b, corrections §8), kit and
  documents `docs/history/endgame_b/b3r_session/` (six INVENTORY files, BLOCK / W1 / STORAGE / W2 briefs, W1_AUDIT,
  GATE_TABLE, CLOSE_NUMBERS, `acceptance.sh`, the gate scripts). **Plan for the next step (B.4, the cast
  metric): `docs/history/endgame_b/NEXT_SESSION.md`.** Known defects of our port, not yet fixed:
  `docs/known_port_issues.md`.
  **Shape.** Block 0 (six read-only inventories) → the rule sheet → W0 prerequisites (the mirror, the
  mint, one allocator API, declarations) → W1 "readers first": fourteen blocks by FILE plus a leftovers
  block converted every reader while the rows still existed → an independent read-only AUDIT → STORAGE →
  W2 "rows last", serial (runtime rows, `engine_funcs`, exe rows, the machinery) → close. **31 gate rows.**
  On every merge: new game `calls=5683 openFileFail=0`, tutorial `5773/12`, typetable == baseline
  (`b983f144`), resave == golden, 0 new warnings.
  **Numbers** (`b3r_session/CLOSE_NUMBERS.md`): 1,267 files, +25,701 / −85,391 lines; every door spelling
  → 0 (`RegisterRuntime*` 2,711, `RegisterExeAddr` 696, `ResolveAddr` 2,460, `ResolveExport` 205,
  `RT_EXPORT_ONCE` 242, `RtAddr<` 430); `NOT IMPLEMENTED` addresses 10 / 8 / 1 → **0** on every route;
  unit 919 → **918**, 0 failures; B.4 cast metric **29,064 → 26,488**; bridge markers 6,698 → 6,859;
  PORT GAP 149 → 189 (null arms made explicit; behaviour unchanged).
  **Behaviour changes, each titled** (list in CLOSE_NUMBERS): the largest visible one is that fire and
  disease now read the settings' auto-slow byte as retail does (they indexed the cell pointer), so an
  outbreak drops the game to 1x speed.
  **Found on the way, all fixed:** a deleted register function carried the `[ExtLoad]` exit hook (the
  gate now fails on a MISSING line); the audit found a false PORT GAP arm that stopped the KeyMap
  deleting destructor freeing its object; the step close's stress run crashed in
  `FindAvailableBuilding`, which read RAX after a `void` body through a cast (`known_port_issues` 12).
  **Step close:** RUN 2026-09-28: Mods / Quit / Options / Achievements exit 0; GoalDialog opens; tool
  routes exit 0; in-game save `SAVE OPERATION COMPLETE`; true release compile exit 0 (942 files) then
  `make clean`; clean rebuild exit 0, 0 new warnings, unit 918 / 0; gate on the clean-built exe 0
  problems; stub set 0; tutorial census citizen=16 herd=16; `sa-stress` CRASHED on the first run (above),
  exit 0 after the fix (update 1.81 ms/frame, recorded, not chased); `acceptance.sh` at target on all 17
  lines. Owed items: NEXT_SESSION §0.
- **NORMAL-VIRTUAL CUTOVER CLOSED (2026-09-28) — ENDGAME B.1 decision 2 is finished:** every class whose
  retail slot 0 is a deleting destructor declares `virtual ~T()` — **449 classes** under five roots
  (`Object::Base` 414, `Graphics::Geometry` 13, `IO::Stream` 9, `Engine::UpdateLoop` 9, `IO::ReaderWriter`
  4); deletion is `delete p`, `Object::Destroy` is `delete`, `Destruct` / `Reconstruct` call `this->~T()`;
  each root defines ONE class-scope `operator delete` (the global one is the C runtime's `free()`), the 41
  declared-but-undefined in-class ones are defined; the 127 dead vector arms are gone. GCC spends two slots
  on the destructor, so retail slot N >= 1 is compiler slot N + 1 — and **nothing in product code reads a
  flipping class's table by literal index** (~950 reads became by-name calls; `src/engine/object/VSlot.h`
  for the few diagnostics that must look into a table). **Still on `DeletingDtor`, by decision: the twelve
  `Video::*Data` classes** (every live object wears dx11's table in retail order; they flip with the dx11
  seam). Exempt because retail has no destructor slot: the Thread family, `IO::Encoding`, the Controller
  interface, `PlatformChannel`, `IPathInfo`. Rule sheet `docs/history/endgame_b/B3X_RULES.md` (X1-X6, run §7,
  corrections §8, left §9), kit and documents `docs/history/endgame_b/b3x_session/` (five INVENTORY files, W0_AUDIT,
  W1_TESTS_DONE, GATE_TABLE, CLOSE_NUMBERS, ACCEPTANCE, `classes.py`, `flip.py`, `partc.py`,
  `acceptance.sh`, the gate scripts).
  **Shape.** Block 0 (five read-only inventories) → the rule sheet → W0 "by name first": seven blocks
  converted every slot-index read while nothing had shifted, then an independent audit (five reads no
  inventory had; 1,114 remaining sites classified; 0 wrong-root casts) → W1: one scripted flip, gated alone
  before the tests were rewritten, then the tests on top of it → one merge. **10 gated merges, 0
  problems**; on every one: new game `calls=5683 openFileFail=0`, tutorial `5773/12`, typetable == baseline
  (`b983f144`), resave 5/5 == golden, 0 new warnings. No behaviour change came from the flip itself.
  **Numbers** (`b3x_session/CLOSE_NUMBERS.md`): 959 files, +8,880 / −13,167 lines; unit 914 → **919**, 0
  failures (1,197 slot checks write RETAIL numbers through `tests/unit/slot_check.h`; the type decides the
  shift); B.4 cast metric **29,870 → 29,159**; bridge markers 6,116 → 6,397 (W0's casts on `void*`
  receivers); PORT GAP 165 → 149.
  **Behaviour changes, each titled:** SpriteCaptureUI appends the `".rsc"` literal (owed from B.3.9).
  Signatures corrected from retail's call sites: `Action::Enter(stack, time)`, `Work::Validate()`,
  `Work::Execute(citizen, context)`, `Work::Slot12(version)`; five call sites pass what retail passes where
  the raw call left a register unset.
  **Found on the way:** `partc.py` saw 190 of 1,205 runtime rows and a tenth of the tests; NEXT_SESSION's
  exempt list was partly wrong; Geometry objects were deleted through a cast to `Object::Base` (another
  root); `UI::DialogInstance::Update` passes `_dialog` to `Contains` where retail passes `_ui`
  (pre-existing, left for its own commit).
  **Step close:** RUN 2026-09-28 on `0419d0ce`: Mods / Quit / Options / Achievements exit 0; GoalDialog
  opens; tool routes exit 0; in-game save `SAVE OPERATION COMPLETE`; `sa-stress` exit 0 (update 1.73
  ms/frame, recorded, not chased); true release compile exit 0 (1,094 files) then `make clean`; clean
  rebuild exit 0, 0 new warnings, unit 919 / 0; gate on the clean-built exe 0 problems; stub set 0 new / 0
  gone; tutorial census citizen=16 herd=16; forced device loss COMPLETE with an empty crash log (killed at
  its time limit, as before — device-lost stays deferred). `acceptance.sh` at target on every line.
  Nothing owed.
- **B.3.9 CLOSED (2026-09-27) — B.3 is closed:** the migration scaffolding is gone. **The mint holds 0 vtables and
  0 `BaseTypeInfo`** (152 / 104 / 48 → 0 / 0 / 2): every Runtime class is a real C++ class with its COMPILER
  vtable, a concrete `DeletingDtor(unsigned)` with retail's arms (a `virtual ~T()` since the cutover above), a protected non-virtual dtor holding the old
  `??1` door's body and a real ctor holding the `??0` door's; every type record — the 104 Runtime ones and the
  310 exe ones — is a `DEFINE_TYPE` class static built at static init (chain **414**, ids identical,
  `b39_session/TYPETABLE_414.txt`). **Deleted:** the `RT_DELDTOR` table (135 rows) and its machinery; the three
  factory interpreters (FACTREST, FACTUI1, FACTUI2 — every class registers its own `CreateNewObject` /
  `CreateInPlaceObject` by retail name and VA); the 5 hand-built tables; every exe vtable `RegisterExeAddr`
  row; every type-id cell read (148 cells / 441 sites), 99 `g_TypeId_*` + 97 `g_tid_*` globals, 31 fill
  functions, 17 registrars; `InvokeAllRegistrations` / `REGISTER_TYPE` / `REGISTER_ABSTRACT_FACTORY` /
  `StandaloneFactoryTest`; 46 of 48 storage rows (class statics now), the D7 redirect, the cutover manifest and
  its tag file, the mint's vtable / type-info code. **Two storage rows stay minted**, as D10 said:
  `?_singleton@MemoryAllocator@System@@` (carry-over 1) and `?_disableHandler@Debug@System@@` (System::Debug
  unported). Rule sheet `docs/history/endgame_b/B39_RULES.md` (D1-D10, B9-1..16; corrections §8, run log §9, what stays
  §10, NOT VERIFIED §11), file map `B39_FILEMAP.md`, kit `docs/history/endgame_b/b39_session/` (BLOCK0 / BLOCK /
  CALLER_BATCH / CLOSING briefs, INVENTORY_A-D, ORDER_MEASUREMENT, GATE_TABLE, TYPETABLE_414, CLOSE_NUMBERS,
  gate.sh, mergegate.sh, finishmerge.sh, uiroutes.sh, toolroutes.sh, devlost.sh, stepclose.sh, acceptance.sh,
  codegrep.py, partc.py). Plan for the next step (the normal-virtual cutover): `docs/history/endgame_b/NEXT_SESSION.md`.
  **Shape.** Block 0 (four read-only inventories: mint vtables, doors / `RT_DELDTOR`, type ids / reflection,
  storage; plus a MEASURED answer that the type table is order-independent — ids are `~CRC32(name)`) → one
  owner: rule sheet + retirement mechanism (`tools/rt_mint_retired/<block>.txt` fragments, the generator
  REFUSES a retired row that still has a reader) + pilot (four dead rows, PickingMesh, SunLight) → W0 COORD-0
  → W1 namespace blocks UI-A, UI-B, UI-D, IO, VID, GFX, ENG, SYS, OBJ, EXE-1 and caller batches C1-C5, CB →
  W2 IO2, GFX2, ENG2, RF (the exe fold, run alone), VS, caller batches CG1-CG3, storage sweeps SS1 / SS2,
  type-id residue TR → W3 TI-REST, TI-UI (the Runtime folds), OBJ3 (Object's four bases, `Base` last) → W4
  CLOSE. 31 merges, each gated by the coordinator (`b39_session/GATE_TABLE.md`): 0 problems after two
  typetable re-cuts made in writing (416 → 415, the duplicate `Internal`; the order-free 414-record format)
  and two coordinator errors fixed in their own commits (a stale generated manifest; a merge commit that did
  not build — the line-matching `deleting_dtor.cpp` resolver, replaced by a structural one). Invariants held
  on every merge: new game `calls=5683 openFileFail=0`, tutorial `5773/12`, resave 5/5 == golden, typetable ==
  baseline.
  **Numbers** (`b39_session/CLOSE_NUMBERS.md`): B.4 cast metric **32,057 → 29,870** (runtime −1,126, code −551);
  bridge markers 6,627 → 6,036; ctor / dtor doors registered by name 206 / 181 → 99 / 37; `rt_export_addrs.inc`
  rows with a reader 2,794 → 1,480 (of 4,926); `RegisterExeAddr` 1,007 → 694 (vtable range 6 → 0, data
  142 → 45); `address_registry.cpp` 1,617 → 1,243 lines; `reg_*.cpp` 145 → 127; `engine_funcs` 414 → 227
  externs (4,292 + 2,576 → 3,036 + 1,652 lines); `RT_DATA_SLOT` sites 422 → 4, `ResolveExport` 443 → 205,
  `RT_EXPORT_ONCE` 391 → 242, `ResolveAddr(Exe` 2,690 → 2,185, `ResolveAddr(Runtime` 386 → 275; literal
  `GetTypeIdByName("…")` 17 → 0 and `ResolveTypeIdByNameLazy("…")` 31 → 0; `DEFINE_EXE_VTABLE` 309 → 4 used;
  unit tests 736 → 914, 0 failures. The §6 acceptance (`b39_session/acceptance.sh`): every line 0 on code.
  **Behaviour changes, each titled and gated with resave ×5** (`git log --grep=behaviour ab1e0846..`): the
  pilot's one `~SunLight` body and the exe-built SunLight wearing its class table; the exe ImageBuffer's
  slots 1-8 running retail's members; Load Game's `GameUpdateLoop` name a null String as retail; IO slot-0
  `flags & 2` arms are retail's vector delete (FileStream family, readers / writers, MemoryStream,
  CompressedFileStream, CompressedMemoryStream — were aborts / table rows); manager-built
  AchievementInstances wear the class table; `~EdgeMeshInstance` ported (its row called a NULL `~Class`); the
  one `Internal` record and then `Base` / `External` / `CompiledExternal` / `ExternalList` / the two compiler
  records are the classes'; `Particle::_rng` seeded at CRT init; `Engine::Loader` built by its class at CRT
  init, not by the mint (gated alone); exe type records built at static init (ids read 0xFFFFFFFF, not 0,
  before `Factory::Startup` — no reader there, confirmed by a run) and the chain 414; `ExternalPtr<Toolbar>`
  validated against `Toolbar::TypeId()` (the global was a vtable slot); CB's retail-read sites: the
  citizen / follow panels find `CitizenAIComponent` (the Action line shows the activity), the pasture finds
  `AutoPickupComponent` and records new piles, the Healer / Well / Happiness and three Persist arrays run
  their IBP type check, five Action `+8` name reads latch.
  **Found on the way** (§8): `System::Thread` has no deleting dtor (two slots, `Run` first); INVENTORY_C was
  wrong on three by-name `g_tid_` cells (two never filled: their readers saw 0); the C1-C5 batches missed
  ~900 `g_TypeId_*` global reads (CG1-CG3 added); `g_TypeId_ToolbarExternal` was a vtable slot; SS1 missed
  seven by-address storage readers (the close converted them); `EXTRA_SIZES` of retired rows are NOT stale
  (they size the reader refusal); the acceptance's type-id grep could not work (fixed: `acceptance.sh`).
  **Device-lost, pre-existing and reproduced on the pre-step baseline** (GATE_TABLE note; the USER's decision,
  `DEVICE_LOST_LOADER_RACE.md`): a forced loss in the front-end faults in `MainLoop::OnVideoError` on a null
  `SaveLoad::_singleton`; during the world build it is the documented loader race; in game the round trip
  COMPLETEs but real `DXGI_ERROR_DEVICE_REMOVED` recoveries then repeat. Step 9's device-lost gate (Present
  #40000): COMPLETE, crash log 0, after every seam-touching merge.
  **Deliberately left** (B39_RULES §10): the normal-virtual cutover (next); the registry teardown
  (`rt_export_addrs.inc`, `address_registry.cpp`, `engine_funcs`, the 5 non-type-id cells, the two minted
  rows, `MemoryAllocator`); the dx11 seam; `ExternalPtr` exe door sites; the PORT GAP message targets
  `kCitizenAIMessageTargetTypeId` / `kHappinessMessageTargetTypeId` (receivers unported); AutoPickupComponent's
  uninstalled Startup; the EventNotify / AutoSave dialog construction skips; the `InitFields` seams the unit
  tests' recording stand-ins define; `spritecaptureui.cpp:200`'s raw read of retail `.rdata` 0x14015d424.
  **Step close:** RUN 2026-09-27 on a699884d + the flag commit. Mods route with `.pkm` exit 0; Quit / Options /
  Achievements exit 0; GoalDialog from a tutorial pause menu opens (1 `OpenGoal` line, killed at the timeout as
  designed); tool routes placement / road / removal exit 0; in-game save `SAVE OPERATION COMPLETE`, crash log 0;
  true release compile exit 0 (1,093 files) then `make clean`; clean rebuild of debug / perf / unit exit 0, 0 new
  warnings, unit 914 / 0 failures; full gate on the clean-built exe 0 problems (new game 5683/0, tutorial
  5773/12, Load Game 4758, resave 5/5, typetable == baseline 428 lines b983f144); `NOT IMPLEMENTED` stub set 0
  new / 0 gone; tutorial `[TownCensus] LOAD exit` citizen=16 herd=16; `acceptance.sh` at target on every line.
  **FOUND AND FIXED AT THE CLOSE:** `sa-stress` CRASHED (exit 2) -- the -O2 exe corrupted the heap in
  `~MeshInstance` at the menu shutdown, on the plain new-game route too; the -O0 exe never did, so no per-merge
  gate saw it. Cause: GCC's lifetime dead-store elimination deletes the field-nulling stores of a ported
  destructor (`ArrayDtorSited` -> `~Array`), and step 9's real classes free a member array in the destructor body
  and again in the compiler's member destructor. Fix: `-fno-lifetime-dse` in `SA_CXXFLAGS` / `UNIT_CXXFLAGS`
  (Makefile banner), MANDATORY like `-fno-strict-aliasing`. After it: `sa-stress` exit 0, 6,000 frames, clean quit
  (sim 0.19 ms/iteration, update 1.92 ms/frame; recorded, not chased), and the gate gained a `newgame_perf` route
  on the -O2 exe. **NOT RUN TO COMPLETION:** the forced device-lost route -- the harness killed it for low system
  memory with the game process at 6.6 GB (the repeated real device recoveries of `GATE_TABLE.md`); last clean
  round trip is the b39-TIUI merge. The user saw the terrain texture slide with the camera in that run; a plain
  new game does not show it (two screenshots at different pins). Both belong to the device-lost decision.
- **B.3.8 CLOSED (2026-09-26; step close completed 2026-09-27):** every exe-phase class in
  `code/Game` is a real C++ class with a COMPILER-GENERATED vtable, on the Runtime bases steps 3-7 made
  real: 310 of 310 reflected exe classes (components, descriptions, actions, works, tools, dialogs, panels,
  goals, scenario, tutorial, world externals) + the five loops, the controller members, the exe Geometry /
  Particle / Thread subclasses — each with a real ctor, `DeletingDtor(unsigned)` + protected dtor chaining
  its base, `DEFINE_EXE_TYPE` (literals exact), header `src/game/<Dir>/<Class>.h`, all agreeing with the
  retail dump (parent, sizeof, slot count). **Hand-built vtables 306 → 3, none serving an exe class.** Rule
  sheet `docs/history/endgame_b/B38_RULES.md` (D1-D10, B8-1..14, corrections §8/§11, run log §9, what stays §10), retail
  dump `B38_SLOTS.md`, file map `B38_FILEMAP.md`, kit `docs/history/endgame_b/b38_session/` (BLOCK_BRIEF,
  CALLER_BATCH_BRIEF, CLOSING_BRIEF, INVENTORY_A/B, SEAM_AND_FILEMAP, CALLER_SURFACE, GATE_TABLE,
  CLOSE_NUMBERS, gate.sh, stepclose.sh, toolroutes.sh, `ghidra/Step8Appendix.java`). Plan for the last B.3
  step: `docs/history/endgame_b/NEXT_SESSION.md`.
  **Found at block 0, retail-proved twice: there is NO multiple inheritance in the exe** (ENDGAME B.1
  decision 2 corrected): the "56 `ControllerComponent` types" are 29 classes embedding a controller MEMBER
  (9-slot interface 0x14015f070) at a per-class offset (+0x28 … +0x88); slot 17 returns its address.
  **Shape.** Block 0 (four read-only inventories + the Ghidra EXE dump) → one owner: rule sheet + a PILOT
  (Component, ControllerComponent, Description, Well leaves) → 14 class blocks on worktrees (RB Runtime
  bases' real ctors; K1 80 descriptions + ComponentEntity; ACT 18; WRK 23; TOOL 12; CIT 19; DLG 19; PNL 42;
  LOOP loops + 11 externals + Tornado/geometry/pathfinder; VIS 25; MI-A 13; MI-B 15; GOAL 27) → 7 caller +
  fold batches (CB1a/b, CB2-CB6: raw dispatches → virtuals, by-name type ids → `X::TypeId()`, delegating
  members' bodies moved in, loop fields named) → one closing owner. Every merge gated by the coordinator
  (`b38_session/GATE_TABLE.md`): 0 problems except four, each resolved — (1) CIT: the StateMachine lazy
  linker called controller slot 17 on every sibling, safe only while every component had a 48-slot padded
  table (fixed on main: ask `IsDerivedFrom(ControllerComponent)` first, as retail); (2) DLG: type table
  312→311 (AutoSaveDialog's duplicate registration removed, intended); (3) LOOP: every route crashed —
  the splash's newly-running base dtor evicted the menu scene the standalone had loaded under the splash's
  factory bit (fixed at the root: mark the menu loop's bit as `Loader::Run` would); merge undone, re-merged
  after the fix; (4) LOOP: resave DIFF — 128 main-menu UI objects no longer leaked (retail frees them), the
  golden re-cut `757e48a1` → `dd3893af` in its own commit (758597e2). New invariants: new-game `calls=5683
  openFileFail=0`, tutorial `5773/12`.
  **Numbers** (`b38_session/CLOSE_NUMBERS.md`): B.4 cast metric **36,450 → 32,057** (first step where it
  FELL: code −4,129) with 621 `// B.3.8 bridge`; `RegisterExeAddr` 1,390 → 1,007 (exe vtable rows 199 → 6);
  `REGISTER_TYPE` 289 → 3 / `DEFINE_EXE_TYPE` 296; `GetTypeIdByName("…")` 535 → 17; delegating members
  401 → 12; `engine/types` headers 147 → 28; unit tests 516 → 736, 0 failures; 142 PORT GAP + 49
  STANDALONE DEVIATION markers (the honest ceiling: retail bodies the tree still stubs, 38 of them leaking
  dtors). **Behaviour changes, each titled and gated with resave x5:** base dtors now run where hand-built
  slot 0s skipped them (panels, dialogs, loops, descriptions, PastureWork …); retail ctor defaults and
  dtor frees (CIT, ACT, MI-A, MI-B, DLG, TOOL); D9 port bugs (Action base-table stamp, ComponentEntity's
  own table, Tool/DragTool null restamps, EditTool/PathTool slot 13, the External triad, controller slot
  1/8, AbsorbResource / MarketLocation / MultiObject members, ClearArea/ConsumeProduce OnCancel, Nomad
  slot 7, TimeGoal meetable, Goal/Scenario null slots); `BANISHED_LIVE_TUTORIALINSTANCE_VT` retired.
  **Step close** (`b38_session/stepclose.sh` on `e24d1e7c`): Mods route with `.pkm` exit 0; front-end Quit,
  Options, Achievements exit 0, `[RtResolve] 0 misses`, 0 TRAP; **GoalDialog from a tutorial game's pause
  menu** (owed since B.3.6) opens with no crash; placement / road / removal tool routes exit 0; in-game save
  SAVE OPERATION COMPLETE, crash log 0; `sa-stress` exit 0 (update 1.85 ms/frame vs baseline 1.92, p95
  2.50 vs 2.40, draw 1.17 vs 1.38 — recorded, not chased); clean rebuild of debug / perf / unit from an
  empty obj dir exit 0, and on it 0 new warnings (debug, perf), 736 tests 0 failures, dispatch 0, manifest
  OK. **Step close COMPLETED 2026-09-27** (the first run was stopped by the harness for low system memory; the owed
  items re-run on `ab1e0846`, `b38_session/GATE_TABLE.md` tail): true release compile from a clean obj dir exit 0
  (1,124 files, 8.3 MB exe), `make clean`, clean rebuild of debug / perf / unit, then the full gate on the
  clean-built exe: 0 problems, 0 new warnings, 736 tests, dispatch 0, manifest OK, mint tables UP TO DATE
  (152/104/48), new game `calls=5683 openFileFail=0`, tutorial `5773/12`, Load Game 4758, `[RtResolve] 0 misses`,
  0 TRAP, resave 5/5; `NOT IMPLEMENTED` stub set identical to the step-8 baseline (23 hits); the tutorial
  `[TownCensus] LOAD exit` line prints (citizen=16 herd=16). Nothing owed.
- **B.3.7 CLOSED (2026-09-26):** `Engine`, `Save`, `Achievement`, `Audio`, `Input`, `Pathfinding`,
  `Picking` and `Application` are real C++ classes under retail's names — 15 polymorphic
  (`UpdateLoop`/`LoaderLoop`/`DialogLoopT`/`DialogLoaderLoopT` as their OWN root, `Loader` and
  `Audio::Stream` on the real `System::Thread`, `Achievement`/`Goal`/`SoundEffect`/`PointList`/
  `PickingMesh` on `Object::External`, `SoundBuffer` on `CompiledExternal`, `AchievementInstance`/
  `GoalInstance` on `Internal`, `IPathInfo`) + 17 non-polymorphic (`MainLoop` 0xa0, `Engine` 0x6f8
  PINNED, `Application` 0x30, `SaveLoad` 0x90, `AchievementManager`, `GoalList`, `Audio::Scene`/
  `Configuration`/`Device` 0x1298, `Input::KeyMap`/`Configuration`/`Device` 0x1200, `Picking::Scene`,
  `Pathfinding::Scene` 0x90 …) + 19 nested value types, in `src/engine/<ns>/` (list with parent,
  slots, sizeof in `docs/history/endgame_b/b37_session/CLOSE_NUMBERS.md` section 6: 14 of 14 checkable agree
  with the retail dump). Bodies moved onto members, callers converted, dead doors deleted. 14 merge
  gates + the close, each 0 problems (table in `B37_RULES.md` section 11); main `96b03474` ->
  `db990145` + the close docs. Rule sheet `docs/history/endgame_b/B37_RULES.md` (B7-1..13, corrections in 10 and
  14, run log 11, what stays 12), retail dump `B37_SLOTS.md` (DLL + EXE modes), file map
  `B37_FILEMAP.md`, kit `docs/history/endgame_b/b37_session/` (BLOCK_BRIEF, CALLER_BATCH_BRIEF, CLOSING_BRIEF,
  EXE_SUBCLASSES, CALLER_SURFACE, `ghidra/Step7Appendix.java`, CLOSE_NUMBERS, stepclose.sh). Plan
  for step 8: `docs/history/endgame_b/NEXT_SESSION.md`.
  **Shape.** One question pass from the retail DLL and exe (30 classes / 618 exports; every vtable
  terminated on the next symbol, sizeof from slot 0, parents from literals and base-ctor calls,
  31 retslot exports, by-value-argument prologues, the five exe loop tables and the Goal family read
  in the exe) plus a file map (six disjoint blocks, 116 files, ONE fragment body) and an exe-subclass
  + caller-surface report (12 exe classes on 14 tables, 25 qualified-call sites, 451 call sites by
  binding); wave 1 blocks B1 Engine+Application, B2 Save, B3 Achievement (+ the exe Goal family's
  qualified calls), B6 Pathfinding+Picking; wave 2 B4 Audio, B5 Input, fragment F; caller round 1
  K1 (src/game/Dialog), K2 (rest of code/Game) for the merged namespaces, round 2 K3 (Input),
  K4 (Audio), K5 (standalone/engine/runtime leftovers/dx11); one closing owner.
  **Numbers** (`CLOSE_NUMBERS.md`). `engine_funcs`: 92 globals bound to the eight namespaces BY
  INITIALISER (82 doors + 10 storage; the inventory's 63 was name-anchored) -> 17 (8 ctor/dtor
  doors still called + 9 storage globals; `g_pEngineSingleton` folded into `g_pMainLoopSingleton`,
  the same symbol; `g_pKeyMapObjectExref` renamed to what it is). Door CALLs 233 -> 30 (all
  ctor/dtor); `ResolveExport` sites 177 -> 49 (registry rows, storage, ctor/dtor doors);
  `RT_EXPORT_ONCE` 57 -> 13; exe import slots 47 -> 6 (storage exrefs); Runtime hex 21 -> 6; live
  hex resolve lines tree-wide 66 -> 12; hand-inlined vtable calls 2 -> 0. 433 `// B.3.7 bridge`
  (code 268, runtime 141). Census: 138 rows left `called` (214 -> 76) because member calls resolve
  nothing. Unit tests 462 -> 516, 0 failures. B.4 cast metric **36,449** lines (+224: code +261
  bridges, engine -93 deleted resolvers, runtime -33). Doors still registered: 285 names (238
  one-line qualified delegations, 24 dtors, 22 ctors, 1 `operator=`) + 16 mint, 8 `_typeInfo`, 12
  `RT_DELDTOR` rows and the 10 singleton storage slots (`SaveLoad::_singleton` has NO manifest row)
  — end of B.3. dx11's import table is down to the `appSingleton` data row.
  **Step-close routes:** merged-tree gate 0 problems (516 tests, 0 new warnings, newgame/tutorial/
  loadgame clean, typetable == baseline, resave 3/3; Save merged with resave 5/5); Mods route with
  `.pkm` mounted exit 0; front-end Quit, Options (id 4 -> Quit), Achievements (id 8 -> Quit) exit 0,
  all `[RtResolve] 0 misses`, 0 `[RtMint] TRAP`; an in-game save through `Save::SaveLoad` (resave
  gate file mode: SAVE OPERATION COMPLETE, 8.55 MB, crash log 0 — the whole-file diff is that
  mode's expected result); `make sa-stress` exit 0, clean quit (800 citizens x3, speed 10, 6000
  frames: update 2.01 ms/frame vs baseline 1.92, p95 2.87 vs 2.40 — recorded, not chased, per the
  B rule; the B.3.6 close read 1.97 / 2.79; draw 1.21 vs 1.38); the new-game log has the same
  `NOT IMPLEMENTED` stub SET as the baseline (39 -> 33 hits); `Audio::Device`/`Stream` construct on
  the plain new-game route (Stream ctor=1, Stream::Run calls=2, Scene::Update 799). True release compile from a clean obj dir exit 0 (1,148 files, 7.8 MB exe), `make clean`, clean rebuild of debug/perf/unit, then the full gate again on the clean-built exe: 0 problems, 0 new warnings, 516 tests, typetable == baseline, resave 3/3.
  **Found on the way, all retail-backed** (`B37_RULES.md` 10 and 14). (1) `Achievement::Achievement`
  is polymorphic (9 slots, 0x58, `??_7Achievement@0@6B@` — the back-reference the inventory's regex
  missed; 16 mint rows not 14) with the exe's `Scenario` on it; `GoalList` is a 0x10 value type.
  (2) The four loops are NOT `Object::Base` (slot 1 `Startup`); `Goal` slot 8 is `CheckGoalState`
  (0x18009f060 tail-calls `[RAX+0x40]`), not `Construct`; `GameUpdateLoop` has 12 slots, not 22;
  retail's Scenario table has 9 slots (tree 13), Goal tables 11 (tree arrays run into the next
  table). (3) `System::String` BY VALUE arrives by HIDDEN POINTER although it is 8 bytes;
  `ExternalPtr` and `System::Time` in a register (B7-5); every copy ctor read is DEEP. (4)
  `Application` has 27 exports (`@Application@1@`), not zero; `PointList` is LIVE
  (`InteractDescription::Serialize` loads one every game and read +0x30/+0x38 raw — now named
  fields); `Pathfinding::Scene`/`IPathInfo` dead in retail. (5) `Engine::Engine` 0x6f8 pinned by
  `??4Engine@0@`; `Audio::Device::PlatformChannel` is the "unnamed" voice-callback record;
  `SoundEffect` +0x20/+0x24/+0x28 and `SoundBuffer` +0x48/+0x50 named; `KeyMap` +0x00 is
  `_suspendCount`; `ModInfo` 0x20; `UpdateList` writes +0x2c (the old header said not). (6) Binding
  defects: `fn_KeyMap_Released` was bound ONLY to an unregistered exe slot (always null);
  `finishtutorialinstance.cpp` resolved `Audio::Configuration::Save`'s slot as `IsDerivedFrom`;
  `citizen_assignworkplace.cpp`'s five Audio emitter slots and `Vector3::operator==` (0x140152a70,
  still unregistered) were never registered; `g_pEngineSingleton` was bound to `MainLoop`'s
  singleton; five `MainLoop` members and `Input::Device::PlatformUpdate` were filed "not exported"
  while exported. (7) Tooling: `gate.sh`'s argument order is `<label> <n> [mode]`; the K1/K2 hex
  sweep dropped ordinary class-scoped names and K3's dropped back-references — anchor on the
  demangled OWNER; `warn_ab2.sh` misreports when a header loses a name (compile the base in its own
  worktree); three block agents went idle on background lock watchers (foreground only).
  **Behaviour changes, each gated and retail-faithful:** the drag tool's key-release paths run
  (K3); citizen emitter teardown/volume run (K4, own commit); the tutorial-instance type check runs
  (K2, per retail 0x1400d7bf0); MenuDLT `Shutdown` runs retail's full 12-step teardown (B1,
  carry-over 11 CLOSED); `PointList` and `Audio::Scene` allocate through the `rtcoll` door.
  **Deliberately NOT converted (reasons in `B37_RULES.md` 12):** no polymorphic class has a C++
  ctor; ctor/dtor doors, mint/`_typeInfo`/`RT_DELDTOR` rows and the singleton storage (end of B.3);
  the exe-phase subclasses (five loop tables, Goal family, Scenario, the unregistered
  `AchievementInstance` table) and the `RtEngine::` loop PODs they embed (95 `m_` uses in 7
  files) — step 8; 105 exported members declared and not ported (`Pathfinding::Scene` 15 dead,
  `MainLoop` 12, `Audio::Device` 11, `Input::Device` 10 …) + 8 undefined dtors; `KeyMap`'s split
  fields vs `Collection::Set`; the 19 `rtpickgrid::` free functions; four defined non-polymorphic
  dtor doors still called from `code/Game` (`Audio::Scene`, `Picking::Scene`, `SaveLoad`,
  `SavedState` — decide with their holders in step 8); the `GoalDialog` and Options-change routes
  still owed (the menu driver has no `OptionsDialog` token).
- **B.3.6 CLOSED (2026-09-26):** `UI` is real C++ classes under retail's names — 68 polymorphic
  classes (28 `*Description`, 28 `*Element`, the six-class Dialog family, `CharacterSet`, `Cursor`,
  `Font`, `SpriteCapture`, `SpriteSheet`, `StringTable`) plus the value types `UserInterface`
  (0x128), `SpriteContext` (0x88), `Sprite` and `String` (0x10 each) in `src/engine/ui/` (list
  with parent, slot count and sizeof in `docs/history/endgame_b/b36_session/CLOSE_NUMBERS.md` section 6: 68 of
  68 agree with the retail slot dump) — bodies moved onto members, callers converted, dead doors
  deleted. 110 commits (83 non-merge), main `dc647869` -> `0caf01c3` + the close docs; 312 source
  files, +31.1k / -18.3k lines. Rule sheet `docs/history/endgame_b/B36_RULES.md` (B6-1..11 and the dated
  corrections in sections 10 and 14, the run log in 11, what stays in 12), retail slot / literal /
  ctor-chain dump `B36_SLOTS.md`, file ownership `B36_FILEMAP.md`, session kit
  `docs/history/endgame_b/b36_session/` (BLOCK_BRIEF, CALLER_BATCH_BRIEF, EXE_SUBCLASSES, POD_TREE_AUDIT, the
  Ghidra dump script `ghidra/UiAppendix.java`, CLOSE_NUMBERS). Plan for step 7:
  `docs/history/endgame_b/NEXT_SESSION.md`, map `docs/history/endgame_b/STEP7_INVENTORY.md`.
  **Shape.** One question pass from the retail DLL (hierarchy from the 136 `_TypeName` /
  `_ParentTypeName` literals, all 68 vtables terminated on the next `??_7`/`??_R` symbol, 68
  `sizeof` from the slot-0 deleting dtors, the 76 by-value-returning exports, every ctor's stamp
  chain) plus a parallel file map, into a committed rule sheet; wave 1 blocks A (`Element` +
  `ElementDescription` bases), B (Dialog family), D (`Sprite`, `String`, the resources,
  `SpriteContext`); wave 2 the 27 leaf Description/Element pairs in five clusters C1-C5 (intermediate
  leaves first) and E (`UserInterface`); two fragment passes F2 (dialog side) and F1 (widget side);
  caller batches K1 (townhall/trade), K2 (the exe dialogs), K3 (the panel UIs), K4 (everything
  outside `src/game/Dialog`), then round 2 (K1r2, K3r2) after the fragment passes defined the last
  members; one closing owner. Every merge gated by the coordinator: 17 gates + the close, each
  0 problems (table in `B36_RULES.md` section 11).
  **Numbers** (`CLOSE_NUMBERS.md`, every unit stated there). `engine_funcs`: 155 globals bound a
  `@UI@@` symbol BY INITIALISER (132 door pointers + 23 type-info globals; the inventory's
  name-anchored 118 missed exe-slot and duplicate bindings); 126 deleted whole, 7 door pointers
  left (six ctor/dtor doors + one `Save` door whose name carries `@UI@@` in a parameter) and 22
  type-info globals. Door calls through `engine_funcs` in `code/Game` 1,472 -> 65 (61 code-only,
  all ctor/dtor); `ResolveExport("…@UI@@…")` sites 185 -> 41 (registry rows, `_typeInfo`/`??_7`
  reads, ctor/dtor doors); `RT_EXPORT_ONCE` 79 -> 11; `RT_DATA_SLOT` 147 -> 70; exe import slots
  onto UI 59 -> 29; exact hand-inlined vtable calls in `src/game/Dialog` 14 -> 9 (diagnostic slot
  reads, not calls). 2,083 `// B.3.6 bridge` casts (code 1,492, runtime 559). Census UI
  1625 total / 134 called / 78 ported (was 329 / 214: `called` counts what still resolves by name
  or address; member calls resolve nothing). Unit tests 358 -> 462, 0 failures. B.4 cast metric
  (lines): **36,225** (code 25,282 · runtime 6,885 · standalone 2,907 · engine 609 · dx11 168 ·
  tests 374), UP 1,400 from 34,825 while adding the 2,083 bridges; `engine` fell 147 and `runtime`
  124. Doors still registered: 809 `@UI@@` names (590 one-line qualified delegations, 67 ctors,
  69 dtors, 74 factory statics, 7 new/delete, 2 literals) + the 68 mint, 68 `_typeInfo` and 67+1
  `RT_DELDTOR` rows (end of B.3). Step-close routes: merged-tree gate 0 problems (462 tests, 0 new warnings,
  newgame/tutorial/loadgame clean, typetable == baseline, resave 3/3); Mods route with both `.pkm`
  mounted exit 0, front-end Quit, Options (id 4 -> Quit), Achievements (id 8 -> Quit) exit 0, all
  `[RtResolve] 0 misses`, 0 `[RtMint] TRAP`; the new-game run reaches TEN FEWER
  `NOT IMPLEMENTED` exe import-slot stubs than the baseline (UI doors now called as members) and
  no new one; `make sa-stress` exit 0, clean quit (800 citizens x3, speed 10, 6000 frames: update
  1.97 ms/frame vs baseline 1.92, p95 2.79 vs 2.40 — recorded, not chased, per the B rule; draw
  1.19 vs 1.38); true release compile from a clean obj dir exit 0 (1,148 files, 7.8 MB exe),
  `make clean`, clean rebuild of debug/perf/unit, then the full gate again on the clean-built exe: 0
  problems, 0 new warnings. NOT a finding: injecting front-end menu id 10 (Goals) faults in
  `GoalDialog` on a null game world — `buttonGoal` exists only in the pause-menu `.rsc`, so retail
  cannot reach that path from the front end (menu-driver skill corrected); the GoalDialog route is
  owed from a scenario game's pause menu.
  **Found on the way, all retail-backed.** (1) **An `InternalPtr<T>` ARGUMENT by value arrives
  by HIDDEN POINTER** (B6-11; `Element::Update`'s caller `LEA R9,[RBP+0x50]`), as does `UI::String`,
  `UI::Sprite` and every >=16-byte class; `ExternalPtr<T>` by value is a bare register pointer.
  `Element::AddChild` was the one declaration that missed it (0x180115df0 dereferences RDX) —
  fixed at the close. (2) An ABSTRACT class cannot be an alias target for an EMBEDDED POD: the
  `Element` POD moved to `RtUI::Element`, the engine dialog headers became aliases of the `RtUI`
  PODs, not the classes. (3) `SpriteContext` and `UserInterface` are NOT trivially copyable
  (`Array<T>` members with a deep copy ctor / destructor exported); `SpriteContext` is 0x88,
  `Element::UpdateState` 0x30, `EditElement` 0xf8, `LabelElement` fields +0x100..+0x12c; 42 layouts
  that block 0 had only sized were pinned from their ctors. (4) `ElementDescription` +0x40 is the
  ALIGNMENT seed, not an element type; `NumberElement` extends `Element` (the header said
  `TextElement`); `ImageDescription`'s applier, `ButtonDescription::CreateInstance`,
  `ComboElement::UpdateText` and the three `SpriteContext` members are EXPORTED (old banners said
  not); `??0CheckDescription`/`ComboDescription`/`ClipRegionDescription`/`NumberDescription` exist.
  (5) Retail slips transcribed faithfully: `GraphElement::SetYDivisions` writes the SAME field as
  `SetXDivisions` (+0xe4); the ported `ComboDialog::OnUpdate` passed `this` only where retail
  passes `(mx, my, _ui)` — a port bug, fixed. (6) `ToolTipDialog`'s parent literal is
  `"UI::DialogInstance"` (same namespace, still qualified: B6-7 — copy, never derive). (7) Six
  `g_TypeId_*` globals were bound to a DIFFERENT class than their name (`SliderElement` held
  SpinnerElement's id, `SpinnerElement` NumberElement's, `ObjectInternal` Element's,
  `UIElementBase` ButtonElement's, `ToolbarElement` CheckElement's, `g_pExternalTypeId` Dialog's);
  renamed to the bound class. (8) `element_common.h`'s `AtomicExport` froze pre-init resolves that
  `RtResolveCached` refuses to cache; fixed to the same shape. (9) mingw-w64 emits typeinfo as a
  COMDAT per TU, so a leaf's `TypeName` can be defined while the base's is a door; and
  `description_common.h` / `element_common.h` both define `enum class Module` (one per TU).
  (10) Loader-thread construction of UI forbids static-init / lazy caches in members (B6-5):
  `rtuielem::AtomicExport/AtomicAddr` slots instead.
  **Deliberately NOT converted (reasons in `B36_RULES.md` section 12):** no UI class has a C++
  constructor; dtors that would re-run retail teardown stay doors; the 68/68/68 mint, `_typeInfo`,
  `RT_DELDTOR` rows; the exe-phase subclasses (20 `DialogInstance`, 38 `DialogController`, 4
  `DialogControllerConfig`, 2 `Element`, 2 `ElementDescription`) keep their ctor doors and
  hand-built tables, with QUALIFIED calls inside their slot bodies (step 8); four `RtUI::` PODs
  still embed `RtUI::Element` (`Edit`, `Number`, `Progress`, `Spinner`); the four minted-table
  slot-copy sites; `TextDescription::CreateInstance` stays a door (reached by address from slot 8);
  103 exported UI members declared and defined nowhere (`DialogInstance` 22, `Dialog` 13,
  `UserInterface` 13, `Element` 10 …). **Largest carry-over, found only at the close:** 76 live
  by-address calls to NON-ctor UI members inside `src/engine/ui` through the file-local
  `RtAddr<Fn>(0x18…)` helper (`SpriteContext::AddSprite` x27, `Dialog::CreateElement` x8,
  `UserInterface::Play` x7) — a binding spelling the inventory never grepped (B36_RULES 14.1);
  plus the pre-existing carry-overs in `NEXT_SESSION.md` section 1.
- **B.3.5 CLOSED (2026-09-19):** `Video` and `Graphics` are real classes under retail's names —
  27 in `Video`, 32 in `Graphics` (`src/engine/video/`, `src/engine/graphics/`; list in
  `docs/history/endgame_b/b35_session/CLOSE_NUMBERS.md` section 7) — with their bodies moved onto members,
  their callers converted and the dead doors deleted. 103 commits (63 non-merge), main `bd483075`
  -> the close; 190 source files, +30.5k / -10.1k lines. Rule sheet `docs/history/endgame_b/B35_RULES.md`
  (rules B5-1..17, with dated corrections from the blocks), raw retail slot / literal / member
  lists `B35_SLOTS.md`, file ownership `B35_FILEMAP.md`, the session's briefs, Ghidra dump
  scripts and measurements `docs/history/endgame_b/b35_session/`. Plan for step 6 (UI):
  `docs/history/endgame_b/NEXT_SESSION.md`, map `docs/history/endgame_b/UI_INVENTORY.md`.
  **Shape.** One question pass from the retail DLL (hierarchy proved by the `_ParentTypeName`
  literals and shared slot targets; all 39 `sizeof` values from the slot-0 deleting dtors, 21 of
  21 existing header asserts agreed) plus a parallel file map; eleven class blocks on worktrees
  by DISJOINT FILE SET — A the twelve abstract `*Data` bases + `VertexLayout.h` authored, B
  program/shader family (three different parents), C `ImageBuffer` chain (crosses the namespace
  boundary) + `RenderState`, D1 `Context`/`Viewport`/`ConstantBuffer`, D2 `Device` + nested +
  `Configuration` + the buffers, E the Geometry tree (nine classes, one block), F
  `Camera`/`Renderer`/`Scene`, G `Material`/`MaterialInstance`/`GraphicsMesh`/`EdgeMesh`, H1
  animation, H2 particles, J dx11's import table + the seam dispatches; two fragment passes
  (bodies of a converted class that lived in another block's file); caller batches I1-I4 then
  K1-K2 after the fragment passes defined the last members; one closing owner. The coordinator
  gated every merge (19 gates).
  **Numbers.** Caller sites converted: about 190 in round 1 + 115 (K1) in `code/`, 29 + 32 in
  `runtime/src` outside the class blocks, 4 in `standalone/`, dx11's table 85 -> 38 live rows, 30
  seam dispatches -> virtual calls on the abstract `*Data` classes. `engine_funcs`: 76 globals
  bound a Video/Graphics symbol (about 20 spelled `fn_Video_*`/`fn_Graphics_*`, 3 bound only BY
  ADDRESS); 60 deleted with typedef, extern, initialiser and dump line; the 16 left are ctor doors
  kept by design. `ResolveExport` sites naming a Video/Graphics symbol 246 -> 108,
  `RT_EXPORT_ONCE` 147 -> 54 (what remains: ctor/dtor/new/delete names, static storage, vtable
  address reads), door call sites through `engine_funcs` 61 -> 0, hand-inlined slot calls in
  `src/engine/{video,graphics}` 49 -> 11. Every duplicate view of a Runtime object in dx11 is
  collapsed onto the runtime header (nine views). Unit tests 260 -> 358, 0 failures (slot order
  read back from GCC's tables, the retslot signatures, the qualified-call rule, sizes). Census:
  Graphics 599 total / 92 called / 70 ported, Video 573 / 79 / 54. B.4 cast metric (lines):
  **34,825** (code 23,760 · runtime 7,009 · standalone 2,889 · engine 756 · dx11 168 · tests
  243), UP 769 from 34,056 while adding **911** `// B.3.5 bridge` casts (transitional: each dies
  with its receiver's conversion); `engine` fell 67 with the door globals.
  **Found on the way, all retail-backed.** (1) **GCC's ms_abi passes a hidden return pointer
  BEFORE `this`; MSVC passes `this` first** — a C++ by-value VIRTUAL writes its result through
  `this` when a retail-shaped caller calls it (block E's first unit test segfaulted so). Slots 5
  and 6 of the Geometry family are declared `T* M(T* ret, …)`; non-virtual by-value members sit
  behind explicit `(this, ret)` doors. The rule sheet had it backwards; amended. (2)
  `ExternalPtr<T>` getters ARE hidden-retslot in retail (ten prologues read); an `ExternalPtr<T>`
  ARGUMENT is a bare pointer in a register with no AddRef, so `EdgeMesh::SetMaterials` converts.
  (3) `vertexlayout.cpp`'s `VertexElement` field names were permuted (`format` is +0x18, +0x0c is
  the byte offset; the wire order was accidentally preserved); dx11's view was the right one.
  (4) 23 retail addresses in `ImageBuffer.h` named no `ImageBuffer` export at all;
  `0x180054080` had two bodies and the wrong owner; `0x180006a10`, registered as a Video buffer
  `operator delete`, is a shared free with ~100 callers. (5) `EdgeMesh::Set` has 25 parameters,
  not 22; `{Vertex,Index}Buffer::Set`'s two ints were named backwards in the header; `Adapter`
  +0x2c/+0x30 are the DXGI adapter / output indices; `GraphicsMesh` +0x50 is an
  `Array<DetailLevel>` (0xb0), not of `TriangleList`; three of `EdgeMesh`'s four buffer pairs were
  misnamed; `Geometry` slot 1 is `Update`, not `Draw`; eight `Camera.h` field names. (6) A latent
  ODR defect: two different `RtGraphics::InterpolatedTransform` definitions of the same size,
  harmless only while no TU included both — one definition now. (7) `Volume.h`'s global
  `Rectangle` is hidden by `<wingdi.h>`'s `Rectangle()` FUNCTION once `<windows.h>` is in, and
  the failure prints as a bogus layout assert (`1 == 16`); fixed at the source with elaborated
  specifiers. (8) `ParticleSystem` is the one class with `Construct` at slot 9 (behind the
  inherited pure `CreateSystem`); pinned by a test. (9) Added retail members that had never been
  ported: eleven `Renderer` accessors, five `Camera` members, `Scene::AddDrawItems`,
  `ConstantBuffer::GetNextUploadIndex`, `Viewport` `==`/`!=`, `Graphics::InterpolatedState<T>`
  with `Lerp`. (10) Dead instrument deleted: the `RT_GFX23_CORRUPT` seam (its probe tool does
  not exist) and five replay captures.
  **Deliberately NOT converted (reasons in `B35_RULES.md` section 11 and the headers):** no
  Video/Graphics class has a C++ constructor, and destructors that would re-run retail's
  out-of-line teardown are empty or stay doors (every live object wears the MINTED table; slot 0
  is declared pure because its body is a `deleting_dtor.cpp` row, so nearly every class is
  abstract); none of the 39 minted rows, `RT_DELDTOR` rows or `_typeInfo` rows retired (end of
  B.3); the ten `CUTOVER_REQUIRED` Video/Graphics statics stay `RT_DATA_SLOT` reads
  (`MintStorageCell` redirect owed, section 9; `Particle::_rng` is the double-seed case); the
  concrete dx11 `*Data` classes and the 84-slot seam (RULE B5-8: the seam's completeness check is
  what device-lost recovery stands on); `Device`'s `CRITICAL_SECTION` stays an opaque
  hand-rolled recursive lock (B5-6); `Device::{Adapter,DisplayMode,RefreshRate,GraphicsInterface}`
  and `ShaderProgram::Parameter` take no real special members (their `Array<T>` helpers destroy
  elements through open-coded copies: a double release); `sizeof(Video::Configuration)` cannot
  be measured (no allocation site; the header asserts a range); `Camera`'s copy/assign are
  defaulted for trivial copyability while retail's partial copies stay the doors (three pad bytes
  nothing reads); the exe-phase subclasses in `code/Game` (tornado, weather, status icons,
  geometry_draw, the NoiseUI image buffers) — only their calls INTO base members converted, and
  those are QUALIFIED; `enumerate_video_modes.cpp`'s SEH; at most 425 exported members are
  declared and defined nowhere (unported retail members with no caller: a caller that appears is
  a link error). Small leftovers are listed in `NEXT_SESSION.md` section 1 item 13.
  **Open defect, characterised, NOT fixed — needs a decision:** a device-lost that lands while
  the loader thread builds the world kills the loader thread (`BANISHED_FORCE_DEVICE_LOST=400`:
  pre-step exe 5/11 runs, step-5 exe 1/11; clean at `=2000` and on the plain route on both).
  PRE-EXISTING, and RETAIL HAS THE SAME WINDOW: all seven `Device::Lock` sites are reproduced,
  retail unlocks right after `Present` and runs the whole recovery unlocked, `Create` and the load
  arm of `Serialize` take no lock, `LoaderLoop::OnVideoError` is a bare RET.
  `docs/history/endgame_b/DEVICE_LOST_LOADER_RACE.md` has the run tables, the lock map and the three possible
  fixes, each a deliberate divergence from retail. One run in this step died with the
  `Factory::IsDerivedFrom` near-null stack at frame 357, before its frame-400 injection: not
  explained; the coordinator's 57 route runs and 75 resaves never showed it.
  **Gate at the close** (coordinator, on a `make clean` rebuild of `8d72408b`, 1,148 compile
  lines per config): debug + perf + unit exit 0; distinct warnings 242 debug / 440 perf against
  the step's 284 / 482 clean-rebuild baseline, 0 new; `dispatch_slot_map --check` 0/109; cutover
  manifest OK (59 rows); mint tables UP TO DATE (152/104/48); new game + tutorial + Load Game
  routes exit 0, `calls=5686` / `5776 openFileFail=15`, `[RtResolve] 0 misses`, no TRAP, 0 GATE
  WARN; pointer-free type table == baseline; resave 5/5 PASS at 21,075,114 bytes; the forced
  device-lost round trip COMPLETE (blocks D2, VC, J; mandatory for anything touching the seam's
  callers). Mods route with both `.pkm` mounted (front-end Mods dialog, ModDetails, Apply): exit
  0, empty crash log, `[RtResolve] 0 misses`, files back in `_mods_off/`. One functional
  `sa-stress` (perf exe, label `b35_close`): exit 0, 6,000 sim frames, fps 139 -> 140, sim
  ms/iter -1.4% against the baseline (inside the 5% plan gate); the tool flagged PostSim ms +39%
  and private MB/min +52% and marked the machine BUSY (`make` ran during it) — recorded, not
  chased. A TRUE release compile (-O3, obj dir emptied first): exit 0, 1,148 files, 7.7 MB exe,
  followed by `make clean` and a debug rebuild (77 MB). Rendering, by command stream not pixels
  (`tools/trace_oracle`, `b35_session/RENDER_CHECK.md`): pre-step exe `bd483075` against the
  step's exe, two RenderDoc captures each (557 / 558 / 509 / 587 draws), selftest 19/19 caught;
  the verdict-bearing screens S3 (floored), S4 and S6 read **0** with the control pair taken from
  EITHER arm; the descriptive screens (S1 49 / 21, S2 3, S5 25 / 7) sit at or below the same-exe
  band measured the same day (47 / 3 / 28).
- **B.3.4 CLOSED (2026-09-19):** `IO` (Stream family, `Serializer`, `Encoding`/`ReaderWriter`,
  `Registry`, `Modification`, `PackageFile`, `FileSystem`, `Persistence`) and the `System` rest
  (`CriticalSection`, `Thread`, `Random`, `Time`, `Color`, `DateTime`, `Timer`, `CRC`, `Error`,
  `WideString`, `Compression`, `Configuration`) are real classes under retail's names, with
  their callers converted and the dead doors deleted. About 135 commits, main `523f8566` -> the close.
  Rule sheets: `docs/history/endgame_b/B34_RULES.md` (IO, with two coordinator amendments),
  `B34_BRIEFS.md`, `B34B_RULES.md` (System — incl. the verdict table for all thirteen System
  classes and rules B4b-1..5). Plan for step 5: `docs/history/endgame_b/NEXT_SESSION.md`.
  **Shape.** Phase 1 + blocks 0–3 by one core owner (Encoding 5 slots / ReaderWriter 26 slots /
  `IO::Serializer` 0x10 bytes, ctor consumes the 12-byte stream header via the Encoding factory,
  dtor patches 4 bytes — lifetime position is load-bearing); STREAMS 1–6 and PACKAGE+FS 1–6 in
  parallel on disjoint file sets (two owners each, handed over at commit boundaries); caller
  batches A (+residual), B, C, D, G, H, P; a four-commit closing pass that then SPECCED its own
  guard sweep (`docs/history/endgame_b/b34_session/GUARD_SWEEP_SPEC.md`; that directory keeps the session's
  briefs, sweep scripts and the coordinator's gate)
  for five directory agents plus a last `engine_funcs`-only pass (2d); System: one owner for the
  rule sheet + nine classes, a second for three + the int64 overload, four caller batches, then
  the same guard-and-cache sweep in small (three directory agents + a last `engine_funcs` pass).
  **Numbers.** Serializer callers: A 852 (+8 residual), B 343, C 252, D 146, closing 17;
  `InternalBasePtr::Serialize` 84 (G); hand-inlined ReaderWriter slot calls / direction reads /
  Harvest door 125 (H); Modification/Persistence/FileSystem 29 (P); System callers 381.
  Deleted: 18 + 20 Serializer-family and 6 System door globals in `engine_funcs.{h,cpp}`, ~370 file-local door caches (typedef +
  static + resolver) and on the order of a thousand readiness-guard lines across `code/Game`, `SerializerLite`,
  `NoteSlotDispatch` and its counter family, `standalone/async_loader.{h,cpp}`.
  `engine_funcs` keeps 20 `fn_*Serialize*` pointers and NONE is an `IO::Serializer` door (10
  ExternalPtr exe doors, 3 exe array helpers, 7 other classes' bodies). System-family globals that
  remain are alive on purpose: the `Time` / `Random` ctor doors, four by-value `String` return
  doors, the allocator doors, the `g_Timer_*` data pointers and the `fn_Random_RandFloatTol` shim
  FUNCTION. Unit tests: 260, 0 failures (the step added ~45, incl. the first executable proof of the
  qualified-call rule and a behavioural CRC-32 check vector). Census: IO 407 total / 210 called
  / 203 ported, 96.7% called-covered; System 262 / 78 / 61 (no registration
  added or retired: `called` and `ported` each fell by 8 because eight ported doors lost their last
  by-name caller to a direct member call — the number measures the registry, not the code). Added retail members that had never been ported:
  `IO::Serializer::GetDirection` 0x180006210, `IO::Stream::GetDirection` 0x180006200,
  `IO::Serializer::Serialize(int64_t&)` 0x180035000, `Object::Internal::Clone` 0x1800bfb10,
  `ExternalFactory::SerializeReferenceTable(IO::Stream&)` (forwards; `external_reftable.cpp`
  untouched). B.4 cast metric: **34,052** (code 23,463 · runtime 6,607 · standalone 2,890 ·
  engine 823 · dx11 149 · tests 120), down 757 from 34,809 while ADDING 2,258
  `// B.3.4 bridge` casts (transitional: each dies with its receiver's conversion); B.3.3's
  1,048 are unchanged (counted on the final tree `1b5eb639`).
  **Deliberately NOT converted (each with its reason in the header or commit):** no stream class
  has a C++ constructor and none of the 19 minted IO rows / 14 deleting-dtor registrations
  retired (B34_RULES §4.3/§4.2 — 13 of 15 IO tables are stamped from outside `src/engine/io`;
  retirement is the end of B.3); the compressed family's eleven non-virtual members and
  `TextFileStream`'s four non-vtable accessors (on the loader open/read path); `IO::
  CompressedCopyFileStream` (no live construction path anywhere — recorded, declined);
  `??0FileSystem` 0x1800ac7c0 and `??0Persistence` 0x1800b8020 (unported / builds the embedded
  Thread worker), every `PersistenceWorker_*` body incl. the by-address restamp of Thread vtable
  0x180155f20 (`persistence_worker_win.cpp:585-589`); `WriteSettingsBody`; `IO::Registry`'s
  destructor (three hand-inlined teardowns); `IO::TextCodec` — NOT A CLASS (four receiverless
  unexported free functions + `IO::SerializeStringArray`); ctor/dtor doors of `Random`, `Time`,
  `Color`, `DateTime`, `Thread`, `CriticalSection`, `Compression`, `Configuration` (B4b-2: a
  by-value holder seeded through the ctor door would double-seed — about 150 `Time` ctor call sites
  on slot 0x140152b00 stay doors by design); `Configuration::CurrentBuildId` 0x1800c4c60 (writes
  through an un-zeroed hidden return slot — needs its prologue read); eight by-value `String`
  returns into an already-constructed `String`; `System::MemoryAllocator` — **deferred past
  step 4 by decision** (~1,146 sites, assessment in `B34B_RULES.md` §6); `System::Debug` and
  `System::StackCriticalSection` found unported, nothing to convert; the eleven
  `CUTOVER_REQUIRED` System statics stay `RT_DATA_SLOT` reads, `MintStorageCell` redirect owed
  at the class-static conversion (tabled per row, §4.1; Timer's five together); the **125 sites
  through twelve `ExternalPtr` exe-side doors** — exe template instantiations ported as
  game-code bodies with their own guard set, NOT Runtime members (exe-phase, receiver-side;
  `fn_ExternalPtr_Serialize3` is the last reader of exe 0x140007140).
  **Found and fixed on the way, all retail-backed.** (1) **The two-loader defect** — our
  invented `standalone/async_loader` ran a second resource loader beside `Engine::Loader`, and a
  third main-thread loader (`ChunkedSceneLoader`) on the splash; every byte goes through
  `FileSystem::ReadFile`'s packed arm (`SetFilePointer`+`ReadFile` on ONE handle, no lock — and
  retail takes none, because retail has one loader: the exe does not import `Engine::Loader` at
  all). Torn reads gave NULL resources (`[GUL::Help] topics=-1`), bad magic, a
  `CompressedStream::Read` spin, an `ExternalFactory::FindNext` spin, and BOTH resave-gate flake
  signatures. Fix `96c17f47`: single loader, no lock, no guard, no timeout; new-game defects
  4/12 -> 0/24, `[GUL::Load] redundant dispatch` 12/12 -> 0/24, `calls` pinned at 5686.
  `docs/history/endgame_b/LOADER_READ_SPIN.md` §9 + `LOADER_READ_SPIN_GHIDRA.md` Q5. **The resave gate has
  not DIFFed once since** (about 90 coordinator runs plus the agents' own); a count-field or resident-row DIFF is now a finding, and
  so is a `redundant dispatch` line. (2) `HarvestArea`/`HarvestOrchard`/`PlantArea`/
  `PlantOrchard::Serialize` aliased the four-argument bool-array door onto the three-argument
  `Serialize(int&)` slot — a guaranteed AV that had never executed on the gate save; retail
  calls 0x140151458. (3) `AmbientEmitterDescription` / `ToolbarDescription::Serialize` read
  `ReaderWriter+8` (always 0) as the direction, so `loading` was stuck TRUE; retail reads
  `serializer+0 -> +8`. (4) ~60 null guards of OURS at hand-inlined ReaderWriter calls dropped
  with a per-site argument (retail's `StatGoal::Serialize` 0x1400da8e2 has no test).
  (5) `completeroad.cpp`'s uchar door cache was permanently NULL (misspelled mangled fragment),
  hidden by its guard.
  **The five defects left open at the close — worked 2026-09-19 (merged, main `2788c108`; what is
  still exposed is listed in `docs/history/endgame_b/NEXT_SESSION.md` §1.6):**
  (a) allocator `LockEnter` on a near-null lock: NOT reproduced, cause inferred. W2′ (unlocked
  free list) is not live — every door into the allocator is locked, as retail's inlined
  Enter/Allocate/Leave. W2 fits the stack; retail's only loader join ahead of `~Engine` is
  `LoaderLoop::Shutdown` (0x1800dc280, `WaitForSingleObject(hFinished, INFINITE)`), which our two
  LoaderLoop Shutdown stubs skipped — they now join (`a9873f47`). The recorded window was most
  likely already closed by `96c17f47`. Same exposure as retail remains for a quit during a splash
  loop; our `WinMain` tail after `~Engine` is longer than retail's. (b) `SaveLoadDialog_
  PopulateFileList` null = `tableSaveList` (+0x108) missing because one `.crs` loaded null under
  the two-loader race `96c17f47` removed; port matches retail, no change, 10 menu-Load runs (6
  under load) clean. Inferred, not reproduced on a pre-fix build. (c) FIXED by ordering, as
  retail: `MenuDLT::Shutdown` calls `Audio::Scene::StopAll(self+0xb30)` (exe 0x14009b41a) before
  the next loop remounts (`c4603875`); retail takes no lock on this path and keeps the same
  one-read residual window. Found with it: non-menu loops' `AllowsModChanges` returned true where
  retail's base (0x180031790) returns FALSE (`e353fb15`) — `_savingEnabled` now flips, `_savedMods`
  is snapshotted, game->menu re-runs `RefreshList`. (d) FIXED (`94258ffc`; retail `LEA RDX,
  [RAX+RCX*8]` at 0x14006ddee / 0x14006dfdb). (e) note corrected (`59ca40dd`); disposition
  unchanged (`?Get@Configuration@System@@` still unported), manifest check 59 rows identical.
  Merged-tree gate: new game reaches the world (`calls=5686` seen on the (c) branch, not re-read
  on the merge), resave PASS == golden (`calls=5691` is the load route's number), pause-menu
  Quit-to-Main-Menu returns to a live menu, crash log 0 bytes, unit 260/0, dispatch 0 mismatches.
  Tutorial route + pause-menu Quit: exit 0, `calls=5776 openFileFail=15` = the archived tutorial
  number. Mods (both `.pkm` moved in, back in `_mods_off/` after): front-end Mods route to Quit
  exit 0; new game -> Quit-to-Main-Menu live; in-game Mods toggle + Ok -> save-state world reload
  completes. (d) driven with a fabricated conflict (a duplicated `.pkm`) and a temporary trace:
  `conflictCount=1`, `Find` -> `cIdx=0`, valid `Mod*`. `sa-stress` (perf build): exit 0, clean
  quit, 140 fps, sim ms/iter +7.1% vs baseline under a BUSY machine — recorded, not chased.
  **Gate at the close** (coordinator; the final tree `1b5eb639` gated 0 problems with resave 5/5, and
  the numbers below are from the `make clean` run on `282cd55b` — 1,993 debug / 2,433 perf compile
  lines — before the System closing sweep, which was then gated per merge): debug + perf + unit exit 0; distinct warnings 284 debug / 482 perf
  against a 294 / 494 clean-rebuild baseline, 0 new; `dispatch_slot_map --check` 0/109; cutover
  manifest OK (59 rows); mint tables UP TO DATE (152/104/48); new game + tutorial + Load Game
  routes exit 0, `calls=5686`, `openCompFail=0`, `[RtResolve] 0 misses`, no TRAP, 0 GATE WARN;
  pointer-free type table == baseline (312 registered, 417/417); resave 5/5 PASS at 21,075,114
  bytes; a TRUE release compile (-O3, obj dir emptied first — `standalone` shares its obj dir
  with `standalone-debug`, so the usual release build compiles nothing) exit 0; Mods route with
  both `.pkm` mounted clean (run at the IO close); one functional `sa-stress`
  (perf exe, label `b34_close`): clean self-quit, no crash or hang log, fps 139 -> 140, sim ms/iter
  +3.6% against the baseline (inside the 5% plan gate; taken under load) — recorded, not chased.
- **B.3.2 CLOSED (2026-09-18):** `Collection::Array<T>`, `Set<T, Compare>` and
  `LinkedList<T>` are real templates under retail's names
  (`src/engine/collection/Collection.h`) wrapping the ported cores; three core
  agents, four caller batches by directory, two closing batches (template side
  f82ff0c1, game side 89b9cf9b) and this final pass. What the closing pass added to
  the templates: ONE allocator door for both the tree and the array
  (`rtcoll::DoorAlloc`/`DoorFree`, `src/engine/collection/alloc_door.cpp`, so
  template allocations are visible to `BANISHED_REDZONE`); the width-8 `AvlOps`
  specialisation the `Set<System::Filename>` path set needs; the deep
  `Array::operator=` (`rtcoll::ArrayAssignCore`); `Set::InsertInPlace`/`Emplace`;
  and one game-side bridge header — `src/game/Component/coll_bridge.h` — with
  `Dialog/dialog_array_bridge.h` deleted.
  **Deleted here:** 14 `fn_*Array*`/`fn_*Set*`/LinkedList pointers in
  `engine/funcs/engine_funcs.{h,cpp}` with zero callers tree-wide, taking 5 typedefs,
  5 hand-written C bodies (`fn_ExeArrayResize/2/3/Push` and
  `ExeArrayResizeFill_CppImpl`) and their init + `_DEBUG` printf lines with them;
  `fn_UI_GetFirstDialog` went with its last consumer, the inline dialog walk in
  engine_funcs.h, which now runs on `ui->_dialogs.GetFirst()` — the same
  CheckPtr/GetController order, and that walk IS retail's GetFirstDialog body
  (`*retval = this->_dialogs._head`). 36 collection pointers remain, every one with a
  live caller. `src/engine/io/modification.cpp`'s `RtColl_SetDestroySubtree_180010470`
  is folded onto `Collection::Set<Filename>::DestroySubtree` (same post-order walk,
  same element dtor, same free door) and its private `RtColl_SetNodeFree_180009c30`
  copy went with it; the clone twin `RtColl_SetCloneSubtree_1800104f0` has no template
  analogue and stays.
  **Rows:** of the 323 `RegisterRuntimeAddr` rows in `src/engine/collection/`, **82 are
  deleted with their shims** (array.cpp 67, array_destroy.cpp 10, array_graphics_anim.cpp 3,
  array_graphics_mesh.cpp 2 — all trivial per-instantiation `~Array`/`SetSize`/element-destroy
  bodies) and **241 stay**: 81 reached by NAME, 160 by ADDRESS. The address form is not just
  `ResolveAddr(Module::Runtime, 0x18…)` — 27 of those 160 go through per-file wrappers
  (`RtAddr<Fn>(0x…)`, `RtAddrFn`, `ArrayDtor(0x…, p)`, `ArraySetSize(…)`, `SetSizeWithFill`,
  `PlainArrayDtor`, `ByAddr(0x…)`, one address held in a named `constexpr`), which a sweep
  that greps only for `ResolveAddr` will miss. A comment or a `printf`/`static_assert` string
  citing an address is NOT a caller; 79 of the 82 had only that. The 82 were additionally
  cleared of bare-hex/table references (no `k…Addr[]` entry, no arithmetic) and ZERO
  collection address appears in `rt_export_addrs.inc` or `rt_data_mint_tables.inc` — so
  regenerating both with `tools/rt_mint_gen.py --check` produced no change, as expected.
  Nothing broke: link, unit and all three routes are clean, which is the honest test for a
  row (a dead row that was live shows up as EXEC-of-0 at runtime, never as a link error).
  `make unit`: **167 tests, 0 failures**. Debug and perf exes both build clean (the
  perf build is the one that catches a deleted init printf). Routes, all exit 0, no
  crash/hang log: autostart + pause-menu quit, the tutorial route, the front-end
  menu route. `tools/dispatch_slot_map.py --check`: 0 mismatches before and after —
  no registration changed, and slot 0x140151ca0 (GetFirstDialog) was never registered.
  An `Array::operator= = delete` probe over the whole merged tree found **four**
  copy-assignment sites, all of them the deliberate coverage in
  `tests/unit/array_test.cpp`: no production code copy-assigns an Array, so the
  operator becoming a DEEP copy in this pass changed nothing live.
  `reinterpret_cast` (ENDGAME §B.4 metric):

  | | code | engine | runtime | standalone | dx11 |
  |---|---|---|---|---|---|
  | B.3.1 close | 23,784 | 906 | 6,788 | 3,055 | 148 |
  | B.3.2 close | 23,661 | 899 | 6,669 | 3,052 | 148 |

  Same pattern as B.3.1 and for the same reason: a `fn_SomeArrayResize(hdr, n)` call
  becomes `reinterpret_cast<GameColl::I32*>(field)->SetSize(n)`, so one cast shape is
  traded for another. The drop is real but small (−242 overall) and comes from the
  pointers and shims that disappeared outright, not from the conversions. These casts
  are the B.3.2 bridge casts, one per use of a raw array header inside a struct that
  is not a class yet; each disappears with that struct's own §B.2 conversion.

  **Left, and why:**
  - ~~The ~20 sites that cite exe **0x1400021d0** but use the zeroing `GameColl::I32`.~~
    CLOSED 2026-09-18 (merge 36c36cf4): 21 sites in 13 files flipped to `I32NoInit` —
    9 are `SetSize(0)`/shrinks, 12 grow, and every grow is followed at once by a loop or
    a bulk stream read that writes the whole tail (per-site proof in each comment; no
    read-before-write found in retail). Two `SetSize(n, fill)` appends stay on `I32`
    (`createrawmaterial.cpp`, `toolbardialog.cpp`: neither calls 0x1400021d0/0x140003600).
    Gate: debug + perf build, unit 167/0, autostart + pause-menu quit, tutorial, Load Game
    dialog all exit 0, resave gate PASS, dispatch check 0, `make sa-stress` 6000 frames
    clean quit at sim 0.20 ms/iteration (+5.9% printed; the close run printed +3.1% at the
    same 0.20 — spread, `perf/results/20260918_131903_i32noinit_clean`).
  - Not converted, by design (documented in-file): `EntityList`'s update/post-update
    trees (an unbalanced BST standing in for retail's AVL, entangled with the tree
    guards), exe helpers with non-zero or partial tail init or per-element deep copy,
    the OOM-rollback appends in tradeui, the packfile introsort,
    `Configuration`/`Persistence` settings maps (no type header / POD String node).
  - The 241 collection rows kept because they are reached BY ADDRESS or BY NAME
    (counts above). A row reached by address is a live door; deleting it is an
    EXEC-of-0 at runtime, not a link error — so the wrapper call forms have to be in
    the sweep's vocabulary before any row is deleted.
  - The deep `Array::operator=` has **unit coverage only**: no production caller
    copy-assigns an Array. `Set<Filename>` insert (width-8 `AvlOps`) was in the same
    position at the close because `WinData/` holds no `.pkm`; it has since run live —
    2026-09-18, main f215e93f, both mods moved in from `WinData/_mods_off/`, the Mods
    route to Quit: exit 0, empty crash log, no hang log, the mods-Ok reload completed.
    The mods are back in `_mods_off/`. (`set_test.cpp`'s width-8 block drives
    `AvlGeneric` on the host, not the ported bodies — the real run verifies those.)
  - 36 collection `fn_*` pointers and `fn_Array_Resize_Stride02/08/14/16B/20`,
    `fn_BST_*`, `fn_DynArray_Clear_*` and friends stay: each still has at least one
    caller in `code/Game`, and each disappears with its caller's own conversion.

  **Coordinator gate on the merged main (2026-09-18):** build, unit 167/0, autostart +
  pause-menu quit and the Load Game dialog route exit 0 after every one of the three
  merges; resave gate PASS after each (the last merge read the README's known one-byte
  `count` flake at +0x18 once — 5689 vs 5688, sizes identical — then PASS twice on the same
  exe). `make sa-stress` (perf exe, 6000 frames, clean quit,
  `perf/results/20260918_124928_b32close`): sim **0.20 ms/iteration, +3.1%** against the
  2026-09-05 baseline (B.3.1 closed at +5.5%), so the allocator door's per-call slot
  re-read is not visible.

- **B.3.3 CLOSED (2026-09-18):** `Object::Base`, `Object::BaseTypeInfo`, `Object::Factory`,
  `Object::External` / `ExternalList` / `CompiledExternal` / `ExternalFactory`,
  `Object::Internal` / `InternalFactory` are real classes under retail's names, over a real
  `Base` with the three measured slots and no virtual destructor;
  `DECLARE_TYPE`/`DEFINE_TYPE` exist; `Factory` is `Collection::Set<TypeEntry,…>` wrapping
  the ported AVL core. One owner through step 5, four caller batches (6A External, 6B
  Internal, 6C Factory, 6D CheckPtr), one closing pass. History of steps 1–6D is in the
  per-step notes below, unchanged.
  **Next: B.3 step 4 (IO). The plan for the next session is `docs/history/endgame_b/NEXT_SESSION.md`;**
  rules so far: `docs/history/endgame_b/B33_RULES.md`, `B32_RULES.md`, `B31_RULES.md`.
  **What the closing pass deleted.** Twelve `fn_*` pointers in
  `engine/funcs/engine_funcs.{h,cpp}` with their typedefs, externs, definitions,
  `ResolveExport`/`ResolveAddr` initialisers and `_DEBUG` value prints:
  `fn_ExternalFactory_{FindOrLoad,Find,FindNext,IsReferenceModeEnabled,GetReferenceId,`
  `GetPathForReferenceId,Clean}`, `fn_External_{GetPath,SaveText}`,
  `fn_Factory_{GetTypeIdFromName,CreateObject}`, `fn_ExternalPtr_Serialize_Typed_7140`.
  Ten of them were freed by converting the last **10** call sites batches 6A–6C did not
  own, all inline bodies in `engine_funcs.{h,cpp}` (1 `Find`, 2 `FindNext`,
  2 `GetTypeIdFromName`, 5 `InternalBasePtr_Ctor`); the other two were already at zero.
  Then **117** guard conjuncts across 36 files plus 9 hand edits (4 `if (P) {…}` blocks
  unwrapped, 2 `P ? call : nullptr` ternaries collapsed, 3 printf/DLOG `%p` value arguments
  dropped). Removing a guard is safe here because each pointer resolved unconditionally
  through a REGISTERED Runtime export — `[RtResolve] 0 misses` on every boot since B.3.1 is
  the standing proof that each guard was always true — and the call behind it is now our own
  compiled member, not a resolved pointer. Also gone: `ThreadBaseVtable()`
  (`audio_stream_win.cpp`, zero callers by name, and 0x180155f20 STAYS — persistence_worker
  and both generated tables cite it); `rtavl::TypeSetInsert` with its declaration and its
  `RegisterRuntimeAddr(0x180030140, …)`, swept clean of `ResolveAddr`, of every per-file
  wrapper form (`RtAddr`, `RtAddrFn`, `ByAddr`, `RT_DATA_SLOT`, `RT_EXPORT_ONCE`,
  `RtResolveCached`, `RtPortLookupAddr`, named `constexpr`), of `RT_MINT_UNNAMED` slots in
  `rt_data_mint_tables.inc`, of `rt_export_addrs.inc` and of the exe dispatch slots.
  `src/engine/object/internal_class.h` is merged back into `Internal.h` (234 lines, no
  declaration edited) and `External.h`'s inert second `#include "TypeInfo.h"` — the reason
  the split ever existed — is deleted in the same commit.
  **Added:** `Object::Factory::Shutdown()` (retail `?Shutdown@Factory@Object@@QEAAXXZ`,
  0x1800c0600), ported from the disassembly and registered under both doors.
  **B33_RULES §9 step 7 was wrong about it** and the correction matters: `Shutdown` is eight
  instructions that FreeLibrary the compiler-plugin HMODULE at the unexported 0x1801fc6d0
  (the cell `UnloadCompilers` and `Engine::Shutdown` also release). The type-tree walk is
  `??1Factory@Object@@` (0x1800071a0 → 0x180008110), ported since Wave 11 as
  `RtObject_Factory_Dtor`. So `Shutdown` is **not** a prerequisite for moving
  `?_singleton@Factory@Object@@` off the mint, and that cell is untouched.
  **Left, and why.**
  · `fn_ExternalList_GetResourceCount` — two REAL calls remain, both inside fprintf
  argument lists (`ui_dialog_stub_factories.cpp`, `gameupdateloop_load.cpp`). The perf build
  strips printf ARGUMENTS, so 6A deliberately did not convert them; the pointer and its five
  guards stay with them.
  · `fn_InternalBasePtr_Ctor` / `fn_InternalBasePtr_DeletePtr` — NOT at zero. Ten files
  cache them in file-static function pointers behind an IAT-slot preference
  (`s_fn_IBP_Ctor = resolveIAT(0x140152a90)`, then fall back to the global) and call through
  the cache: `placementtool.cpp`, `edittool.cpp`, `removenaturalresource.cpp`,
  `performwork.cpp`, `dropoffrawmaterial.cpp`, `work_act_perform.cpp`, `work_act_path.cpp`,
  `work_act_clear.cpp`, `pickup_gate.cpp` and `work_base_reimpl.cpp`'s helpers. Converting
  them means deciding the IAT-vs-export preference per site — a caller batch, not a sweep.
  · `tools/rt_mint_gen.py` was NOT re-run and no `.inc` was regenerated: `--check` reports
  the committed `rt_data_mint_tables.inc` UP TO DATE (152 vtables, 104 BaseTypeInfo, 48
  storage). Dropping the `?_firstTypeInfo@BaseTypeInfo@Object@@` row would remove a LIVE
  door rather than inert bytes — `src/app/main.cpp` reads 0x1801fc6e0 by ADDRESS twice
  (the boot probe and the factory self-test, i.e. the source of the
  `[GameMain] Post-Startup _firstTypeInfo` line) and `cutover_manifest.inc:57` carries the
  name as `CUTOVER_REQUIRED`. Only the row's 8 bytes in `kStorageBytes` are inert;
  `MintStorageCell()`'s redirect is what makes the name door, the address door and the class
  static one cell, so it stays.
  · The five `?_typeInfo@` rows (`Base` 0x1801f9c50, `External` 0x1801f9b38, `ExternalList`
  0x1801f9be0, `CompiledExternal` 0x1801f9b00, `Internal` 0x1801f9c18) stay minted and the
  three External classes stay without `DEFINE_TYPE`, for the reason External.h's closing
  banner states, re-measured here: ~20 live by-name `RT_DATA_SLOT` readers, two
  `CUTOVER_REQUIRED` rows, two by-address readers, `reg_mint_promoted.cpp`'s promotion rows
  — and no way to show the post-Startup type ids are unchanged without a boot the closing
  agent cannot run. `Object::Internal` already carries `DEFINE_TYPE` beside its minted
  record, which is why the chain reads 417 and not 416; the closing pass adds and removes no
  chain record, so **417 is still the expected count on a live boot**.
  · Everything B33_RULES §9 step 7 lists as STAY is untouched: the `RT_DELDTOR` rows for
  `Base` / `External` / `CompiledExternal` / `Internal`, `RtObject_Base_Dtor`, every minted
  vtable, `fn_External_External` / `fn_External_Dtor` / `fn_ObjectInternal_Ctor` / `_Dtor`,
  `fn_ExternalList_Find` (signature disagreement recorded, not fixed: `External.h` spells
  the 2nd parameter `uint32_t`, retail's is `Object::Type` by value), the serializer
  pointers, `CallSlot0`, every `GetTypeIdByName` site, the exe-side `FactoryRegistration`
  staging list, the runtime port of the empty `CheckPtr` member, and the
  `ExternalFactory::Add` path. The three dispatch-slot rows 0x140151218 / 0x140151220 /
  0x140151228 stay registered although they now have no exe-side reader — they are the exe's
  own PE import slots and the Runtime ports behind them are unchanged.
  · §6.3's wrong reason is deleted where it was written down: `InternalBasePtr::DeletePtr`'s
  raw slot-0 call stays raw because the pointee is `RtObject::Internal*`, the POD, so the
  member spelling adds a bridge cast and removes no indirection — NOT because it "may be
  handed a minted-vtable object", which `object_vtable_abi_test.cpp` disproves.
  **Casts (B.4 metric), measured here, build dirs excluded:** 34,809 `reinterpret_cast`
  — code 23,971, runtime 6,685, standalone 3,054, engine 888, dx11 148, tests 63 (2026-09-13
  baseline ~35,700). B.3.3 added **1,045** `// B.3.3 bridge` markers, each of which dies
  with its receiver's own conversion.
  **Closing-pass checks:** `standalone-debug`, `standalone`, `standalone-perf` and `unit`
  all exit 0; unit **206 tests, 0 failures**; `tools/dispatch_slot_map.py --check` 109 rows,
  **0 mismatches**; `tools/runtime_census.py` clean (every remaining registration a LITERAL
  address); `tools/cutover_manifest.py --gate` **OK, 59 rows, 0 GATE WARN**;
  `tools/rt_mint_gen.py --check` UP TO DATE; zero new warnings on any line changed.
  The boot lines that carry each risky deletion: `[RtResolve] 0 misses` and the absence of
  `[RtMint] TRAP` (the guard removals), the type-table dump plus
  `[GameMain] Post-Startup _firstTypeInfo: 417 entries` (the `TypeSetInsert` retirement —
  Startup still builds the same tree through `Collection::Set::Insert`), and
  `[RtPort]   Object::factory dtors:` now reading **4** exports instead of 3
  (`Factory::Shutdown`).
  **Next (B.3 step 4, IO):** the 159 `fn_ExternalPtr_*Serialize*` /
  `fn_InternalBasePtr_Serialize` / `fn_ExternalRefArray_Serialize` sites over 9 pointers,
  still untouched; `fn_ExternalPtr_Serialize3` is the last reader of exe address
  0x140007140; `Internal::Clone` and `SerializeReferenceTable` as a member;
  `src/engine/io/file_stream.cpp:393`'s `NoteSlotDispatch` vptr counter dies with
  `IO::BinaryFileStream`; `RtObject::SerializerLite` and the retail `Encoding` vtable offsets
  are what every converted `Serialize` body still goes through.
  **Coordinator gate on the merged main (2026-09-18):** every B.3.3 merge (steps 3, 4, 5,
  6A, 6B/C, 6D, closing) was gated by the coordinator: debug + perf build (release too from
  6B on), `make unit` (206/0 at the close), autostart + pause-menu quit, the tutorial route,
  the Load Game dialog route — all exit 0, empty crash log, no hang log — a live-boot type
  table compared against the pre-step-3 boot (312 registered, the same ten id/name pairs;
  the chain reads 417 since step 5's `DEFINE_TYPE(Internal)`, was 416), HelpTopics 66 /
  DiseaseList 12 identical, `[RtResolve] 0 misses`, no `[RtMint] TRAP`,
  `dispatch_slot_map.py --check` 0 (109 rows since 6D), 0 GATE WARN. At the close also the
  Mods route with both `.pkm` mounted (6 clicks, exit 0; mods back in `_mods_off/`) and the
  resave gate PASS 3/3. Resave across the step: PASS except the documented loader-race
  flake (count 5689 + ONE extra row — `CropField.rsc:resource` twice after 6A,
  `ToolbarRemoval.rsc:resource` once after 6B/C; never after 6D or the close) — see
  `tools/resave_gate/README.md`. One coordinator fix: a 6B include inside
  `#if defined(_DEBUG)` that broke only the perf build.
  `make sa-stress` is a FUNCTIONAL check while B is open (B33_RULES §10): 6000 frames, clean
  quit at every step close. Printed sim ms/iteration, recorded and NOT chased: step 3
  0.19 (-0.2%), step 4 0.19 (+0.4/+0.8%), step 5 0.21 (+9–10%), 6A 0.21, 6B/C 0.21,
  6D 0.20 (+4.6%), close 0.21 (+10.7%, `perf/results/20260918_175719_b33close`). Revisit
  after B closes; the per-step numbers are the trail to bisect from.

- **B.3.3 step detail** (`Object::Base` / `BaseTypeInfo` / `Factory` + reflection macro,
  single owner). Rules and the order of work: `docs/history/endgame_b/B33_RULES.md`; measured input:
  `docs/history/endgame_b/OBJECT_INVENTORY.md` (spot-checked and corrected 2026-09-18).
  Steps 1–2 done (merge of `b33-owner`): the six design answers — Base has exactly three
  slots (`DeletingDtor`, `TypeName()`, `Type()`), the one record is `Object::BaseTypeInfo`
  and the exe-side staging list folds away, `Factory` (0x10 bytes, 0x50-byte nodes) moves
  onto `Collection::Set` inside step 3 — and `tests/unit/object_vtable_abi_test.cpp`
  (7 tests; unit now 174/0) proving a GCC class without a virtual dtor is slot-for-slot a
  retail vtable, with the second vptr at +0x28 for two bases.
  Step 3 done (merge of `13bcc143`): `Object::BaseTypeInfo` and `Object::Factory` are real
  classes (`Factory` = 0x10 bytes on `Collection::Set` over the 0x50-byte node, wrapping the
  ported rtavl core), `DECLARE_TYPE`/`DEFINE_TYPE` exist, `_firstTypeInfo` is a plain static
  that both mint doors publish (the generated row's bytes are inert until the closing pass
  regenerates). All seven `RtObject_*` shims and the exe-side staging list STAY: the 305
  `REGISTER_TYPE` sites name PODs, so the list folds only as those classes convert.
  Gate: debug + perf build, unit 185/0, a live boot with the type table byte-for-byte the
  same as the pre-merge boot (312 registered, 416/416 ids, same ten id/name pairs),
  autostart + pause-menu quit, tutorial, Load Game dialog exit 0, resave PASS, dispatch 0,
  `make sa-stress` -0.2% (`perf/results/*_b33step3`).
  Step 4 done (merge of `62926d44`): `Object::Base` is a real class — three virtuals in
  retail order (`DeletingDtor`, `TypeName()`, `Type()`), non-virtual `~Base()`,
  `Object::Destroy(p)`, `IsA`/`IsDerivedFrom` members; `DECLARE_TYPE` emits `Type()` /
  `TypeName()`. The three `RtObject_Base_*` shims delegate to it. Base's `RT_DELDTOR` row,
  `RtObject_Base_Dtor` and the minted `??_7Base@Object@@6B@` table all STAY: 64 by-name
  doors, 22 factory-replay `Vptr` rows and 59 `.cpp` readers still stamp that table, and
  its minted slot 0 is filled FROM the row (B33_RULES §6.1: check `rt_data_mint_tables.inc`
  for a `RT_MINT_UNNAMED` slot before retiring any row). They retire at the end of the
  fan-out. Gate: debug + perf build, unit 188/0, live type table identical to the
  pre-step-3 boot, `[RtResolve] 0 misses`, no `[RtMint] TRAP`, three routes exit 0, resave
  PASS, dispatch 0, `make sa-stress` +0.4% / +0.8% on two quiet runs (a first run straight
  after the resave gate printed +10.1% and did not reproduce).
  `-Wno-invalid-offsetof` is in `SA_CXXFLAGS` and `UNIT_CXXFLAGS` (every build variant
  expands `SA_CXXFLAGS`) ahead of step 5, where the first polymorphic classes WITH fields
  land.
  Step 5 done (merges of `3593efd3` External, `7302ecb3` Internal): `Object::External`
  (8 slots; slot 4 is `Compile()`, not nameless), `CompiledExternal`, `ExternalList`
  (9 slots), `ExternalFactory`, `Object::Internal` (4 slots: dtor, `TypeName`, `Type`,
  `Persist`) and `InternalFactory` are real classes over `Object::Base`; the `RtObject::*`
  PODs stay as layout-asserted trivially-destructible aliases, every `RtObject_*` door and
  registration is unchanged, no `RT_DELDTOR` row or minted vtable retired, nothing on the
  `ExternalFactory::Add` path reordered. `Internal` uses `DEFINE_TYPE` (a second chain
  record beside the minted one: same id in pass 1, no tree node, no child — the chain reads
  417 entries, was 416); the three External classes do NOT, until their minted
  `?_typeInfo@` rows are redirected the way `_firstTypeInfo` was (needs
  `standalone/rt_data_mint.cpp` + `tools/rt_mint_gen.py` — closing pass). Left for B.3
  step 4 (IO): `Internal::Clone`, `SerializeReferenceTable` as a member.
  Gate: debug + perf build, unit 206/0, 0 GATE WARN, three routes exit 0, same ten
  id/name pairs, `[RtResolve] 0 misses`, no `[RtMint] TRAP`, resave PASS with the persist
  pair live (`clearRuns=2 resetRuns=2`), dispatch 0. `make sa-stress`: clean quit, sim
  0.21 ms/iteration (+9–10% printed; External-only read +7%). **Recorded, not chased** —
  perf is revisited after B closes (B33_RULES §10).
  Step 6 RE-SCOPED (B33_RULES §9.6): none of the 545 literal `GetTypeIdByName` sites names
  a class that is real yet (287 game/engine names), so that sweep moves into each class's
  own conversion (B.3 steps 5–8); the 124 `CallSlot0` sites wait for their classes; 159
  serializer-pointer sites wait for `IO::Serializer`. What B.3.3 does convert: batch A
  External callers, B Internal callers, C Factory callers, D the 4,700 no-op `CheckPtr`
  calls.
  Batch 6A done (merge of `5d6a6eeb`): 158 of 380 real call sites of `fn_ExternalFactory_*`
  / `fn_External_GetPath` / `fn_ExternalList_GetResourceCount` / `fn_External_SaveText` are
  member calls on the real classes (53 files, 141 `// B.3.3 bridge` casts, every guard on
  a no-longer-called pointer kept for the closing pass). NOT converted, by design:
  `fn_External_External` (113) and `fn_External_Dtor` (107) — `Object::External` is
  abstract and a real ctor/dtor chain would stamp GCC's vtable where live objects wear the
  minted one; they convert with their receivers' classes. `fn_ExternalList_Find` (4):
  `External.h` spells its second parameter `uint32_t`, retail's is `Object::Type` by value.
  Two `GetResourceCount` calls inside fprintf argument lists stay (perf build strips them).
  Gate: debug + perf build, unit 206/0, three routes exit 0, HelpTopics 66 / DiseaseList 12
  identical to the pre-merge boot, dispatch 0, stress clean quit (0.21 ms/iter, recorded).
  Resave gate: 8 PASS / 2 DIFF in 10 runs (0/9 DIFF on the commit before) — the known
  loader-race flake one step further, one extra `CropField.rsc:resource` row at count 5689;
  documented in `tools/resave_gate/README.md`, no semantic cause in the batch.
  Batches 6B/6C done (merge of `ad54c8a4` + one coordinator fix): 876 real call sites in
  225 files — `fn_InternalBasePtr_Ctor` 815 (placement-new of `::Object::InternalBasePtr`
  into the SAME raw storage, which is literally what the registered door runs; the class is
  static_asserted trivially destructible and copyable, so no second destructor exists),
  `fn_InternalBasePtr_DeletePtr` 47, `fn_Factory_GetTypeIdFromName` 10,
  `fn_Factory_CreateObject` 4. NOT converted, by design: all 61 `fn_ObjectInternal_Ctor` /
  `_Dtor` sites (the door stamps the MINTED vtables on purpose; every receiver is a derived
  exe POD — they convert with `Object::Component`), `fn_BaseTypeInfo_Ctor` (staging path).
  One `runtime/` edit, a real bug fix: `src/engine/system/Value.h` no longer duplicates
  the global `struct Time` that `engine/types/common.h` owns — the redefinition is why no
  exe-side file could include `object/Internal.h` before (`mouse_inject.cpp`'s file-local
  `InputDevice()` renamed `InputDeviceSingleton()` as the one collateral).
  Coordinator fix: `buildrequirementui.cpp`'s new include had landed inside
  `#if defined(_DEBUG)` — debug built, the PERF build did not. All 245 step-6 includes
  scanned, that was the only one. (Agents must build `standalone-perf` too, not just
  `standalone`.) Gate: debug + release + perf build, unit 206/0, three routes exit 0, type
  table and list counts identical, `[RtResolve] 0 misses`, dispatch 0, resave 2 PASS + 1
  flake of the documented signature (count 5689, one extra row, this time
  `ToolbarRemoval.rsc:resource`), stress clean quit (0.21 ms/iter, recorded). The 6C
  `GetTypeIdFromName` sites in Options/NewGame dialogs have build coverage only on these
  routes.
  Batch 6D done (merge of `b3364b8e`): all 4,670 `InternalBasePtr::CheckPtr` calls deleted
  (3,373 through the macro, 1,297 through aliases/wrappers — 110 of those through wrappers
  whose names do not contain "CheckPtr"), 299 files, −5,463 lines. Retail's body is
  `C2 00 00` (`RET 0`) in the shipped AND the profile DLL and the exe has no import for it.
  Side-effect audit first: 1,302 distinct argument expressions, none with a side effect;
  1,402 `if (<pointer>) call;` statements deleted whole, none was the sole body of a
  loop/else; 11 comma-operator operands rewritten keeping the survivor. Gone with them: the
  macro (both forms), typedef, extern, `g_checkPtrIsNop` and its byte-sniff block, the
  `BANISHED_CHECKPTR_NOP` option, `-DBANISHED_CHECKPTR_STATIC_NOP`, the exe dispatch-slot
  row 0x140152ab0 (dispatch check now 109 rows, 0 mismatches). The runtime port of the
  empty member STAYS (it is the real class's ABI door; a unit test calls it).
  Gate: debug + release + perf (clean dir) build, unit 206/0, three routes exit 0, type
  table and list counts identical, no `[CheckPtr]` boot line, `[RtResolve] 0 misses`,
  resave PASS 3/3, stress clean quit (0.20 ms/iter, recorded).
  Step 7 done (the closing pass) — see the "B.3.3 CLOSED" block above for what it deleted,
  what it left and why.

- **B.3.1 CLOSED (2026-09-13 night):** `System::String`, `System::Filename` and the nine
  `Math` value types are real classes under retail's names (`src/engine/system/*.h`,
  `src/engine/math/Math.h`); three cores, five caller batches by directory, one closing
  pass. The C seam between callers and these types is gone: every `fn_String_*`,
  `fn_Filename_*`, `fn_Vector*`, `fn_Quaternion*`, `fn_Transform*`, `fn_Matrix*` pointer in
  `engine/funcs/engine_funcs.{h,cpp}` is deleted, `engine/types/SystemString.h` is deleted,
  and 36 zero-consumer `RtMath_*`/`RtSystem_*` shims went with their 44 registry rows.
  `standalone/filename_dtor_check.cpp` and `BANISHED_FNDTOR_CHECK` are gone with the
  `fn_Filename_Dtor` pointer they interposed (options.h is still the whole flag inventory).
  The 21 smallest Math members are now `inline` in Math.h (bodies moved verbatim).
  `make unit`: 154 tests, 0 failures. Routes, all exit 0, no crash/hang log: autostart +
  pause-menu quit, the Mods route, the tutorial route, the Load Game dialog.
  `tools/dispatch_slot_map.py --check`: 0 mismatches. `make sa-stress` reads sim ms/iter
  **+5.5% against the 2026-09-05 baseline**, of which +4.4% is already there with the
  inlining reverted — the inlining itself is inside the run-to-run spread.
  `reinterpret_cast` (ENDGAME §B.4 metric), start -> now:

  | | code | engine | runtime | standalone | dx11 |
  |---|---|---|---|---|---|
  | start (main 1d97579c) | 23,657 | 962 | 6,826 | 3,066 | 145 |
  | now | 23,784 | 906 | 6,788 | 3,055 | 148 |

  It moved barely at all, and `code/` went UP, because the conversions traded one cast
  shape for another: a `fn_String_Set(field, s)` call became
  `reinterpret_cast<System::String*>(field)->Set(s)`. Those are the B.3.1 bridge casts
  (`// B.3.1 bridge`), one per use of a POD member of a struct that is not a class yet;
  each disappears with that struct's own §B.2 conversion, not before.

  **Left, and why** — all of it belongs to a later §B.2 class conversion, not to B.3.1:
  - Bodies still in C shape over these types: `IO::FileSystem::ReadPackfile`,
    `WriteSettingsBody`, the RtSink-routed `src/engine/save/saveload_update.cpp`, and
    `UI::WideString` (never converted; it is its own type, not System::String).
  - `RtSystem::String` / `RtSystem::Filename` PODs and `src/engine/system/pod_bridge.h`
    stay while any struct still declares one as a MEMBER: `IO::FileSystem` (5 fields),
    `IO::PackageFile` (5), `IO::Persistence` (5), `Achievement::Achievement` (3),
    `Achievement::Goal` (1), `Engine::UpdateLoop`'s two loop subclasses (2),
    `IO::Registry` (1), `IO::Modification` (2, `Filename`), plus
    `src/engine/object/External.h`'s `using RtSystem::Filename`.
  - Shims still reached by name or address from an unconverted caller stay: String's
    Set/Dtor/Assign/SubString/Format/Serialize/InsertAt/RemoveAt/operator+/StrCompare,
    Filename's ctor/dtor/assign/compare family, and the Vector/Quaternion/Transform
    members the graphics bodies call.
  - The global aliases (`Scalar`, `Quaternion`, `Matrix`, `Transform`, `Transform2`,
    `Transform3` in Math.h; `Vector2/3/4` in engine/types/common.h) all still have
    hundreds of users and stay. `RtMath::` is already gone.

- **CLOSED (2026-09-13):** the stress-town teardown crash/hang was `FishingWork::OnCancel`
  ported with three parameters where retail forwards a fourth (the packed slot word) by
  register passthrough — Slot6 then stored a `2` and a null past two 4-entry arrays on every
  cancelled fishing action. One-line fix in `src/game/Work/fishingwork.cpp`; root cause,
  measurement and what the earlier analysis got wrong: `TEARDOWN_WILD_WRITE.md` §0. Verified:
  debug exe reproduced 1/1 before the fix with the writer named, 0/2 after (both teardown
  brackets clean, clean exit). The loader race in `ExternalFactory::Add` was not reached.

The plan for what follows the gate is `ENDGAME.md`: A remove the DLL, C remove the flags,
B fold the C bodies into real classes, in that order. **2026-09-13: A.1 and A.2 are done.**
Commit `aa7a094fe` (once the tag `dll-present-last`, deleted 2026-10-18) is the last commit where the exe still loaded
Runtime-x64.dll; the exe never loads it now. `make sa-run` is the DLL-absent route and
`make cutover` is kept as an alias for it.
- The flip exposed one bug the old gate could never see: `Video::Device::InitializeInterfaces`
  still `LoadLibraryW`'d the video plugins, and VideoDX9-x64.dll statically imports
  Runtime-x64.dll, so the DLL was mapped behind `ResolveInit`'s back and its first heap call
  faulted. The move-aside had made that LoadLibrary fail for the wrong reason. The loop now
  loads nothing (`src/platform/win/video_interfaces_win.cpp`); `ResolveInit` fails fast if the
  DLL is ever found mapped. Verified 2026-09-13 with the DLL on disk: sa-run + pause-menu
  quit exit 0, `BANISHED_AUTOQUIT`, and the resave gate PASS.

- **2026-09-13 later: A.3, A.4 and C.1 are done** (commit `5084c732` on `endgame-a3`; five
  parallel worktree batches by directory plus one serial pass over the shared definitions;
  493 files, +1.9k/-83k lines). Gone: the raw door (`RtOriginal`, `RT_ORIGINAL_ONCE`,
  `ResolveExportRaw/OrdinalRaw/RuntimeRaw`, the ordinal door), the diff oracle (`rt_diff`),
  the predictive oracle (`rt_predict`; `RtSink` survives as `src/engine/rt_sink.h`, the live
  sink the save/ui bodies are written against), `io/save_trace`, 46 `*_probe.cpp`, the alloc
  and mint shadows, the 11 detour/hook installers and `hde/`, the byte-pins and `*_sweep`
  make targets, `tools/attic`, and every `BANISHED_RT_*` read (`runtime/` getenv 133 -> 7).
  `allocator_lock.cpp` (a byte patch on the DLL's Allocate/Free) is reduced to
  `src/diag/alloc_stats.cpp`.
  - **Five live dependencies on the raw door were found**, each a call through NULL since
    A.1 that no gate had caught: `Graphics::Geometry` copy ctor in `graphics/mesh.cpp`
    (every MeshInstance / EdgeMeshInstance copy), `MemFree` in `ui/sprite.cpp`'s
    `SpriteSheetAssignString` (every refcount-to-zero string release), `MemAlloc/MemFree`
    in `system/string.cpp` and `object/internal.cpp` (raw first, registry fallback), and
    `_beginthreadex` in `system/thread_win.cpp`. All folded to the registry door.
  - A.5 routes, DLL-absent, all exit 0 with an empty crash log: `make sa-run` +
    pause-menu quit (town built, App::Quit), `BANISHED_AUTOQUIT`, the Load Game dialog
    (`BANISHED_AUTOMENU_BUTTON=2`), the Mods route (0 rows, no .pkm installed), and the
    resave gate PASS (byte-identical to the golden, same as the baseline on `c1fdd7f6`).
  - **The first `make sa-stress` died in boot with a VCALL-of-0** on `GraphicsMesh`'s minted
    vtable: `?PlatformSerialize@GraphicsMesh@` was unregistered in the PERF build only.
    Cause (pre-existing, 2026-09-12): seven registrations were written as
    `printf("... %d", RtXxxRegister())`, and the perf build compiles `printf` to `((void)0)`
    (`src/app/log_strip.h`), arguments included. Batch C graphics/achievement/audio/video
    leaves and three math rows never registered in any perf exe. Hoisted; sweep with
    `grep -rnPzo '(?s)printf\s*\([^;]*?Rt[A-Za-z0-9_]*Register\s*\(\)[^;]*?\);'` (0).
  - **Second perf crash, also perf-only and also pre-existing:** with the leaves registered,
    `Renderer::DrawItemList` faulted in `Context::SetTransform` reading the matrix the
    geometry's vtable slot 6 (`GetInstanceTransform`, returns `Math::Matrix` BY VALUE) had
    returned. Our `ParticleInstance::GetInstanceTransform` port (wave27_lane_gfxpart.cpp) was
    declared `void`: the hidden return slot arrives in RDX and its address must go back in
    RAX; at -O0 RAX happened to still hold it, at -O2 it was garbage. Now returns the slot.
    Audit over all 175 registered by-value-returning exports: no other port declared void
    (address-only registrations not covered). Same debug-exe town ran 1200 frames clean.
  - `make sa-stress` on the perf exe then completes 6000 frames with a clean quit. Sim cost
    0.202 ms/iteration vs the 2026-09-05 DLL-present baseline's 0.192 (+5.0%, `ph_postsim`
    0.069 -> 0.097 ms/frame is most of it); a second run with a compile going on beside it
    read +10.3%, so the run-to-run noise on this box is of the same size as the delta. The
    perf gate was deferred to after cutover by the user (Wave 4); recorded, not chased here.
    A clean A/B needs the machine idle and the baseline re-cut on this build.
  - The debug exe's stress town (`sa-stress-debug`, 1200 frames) simulates clean but CRASHES
    IN TEARDOWN after the quit click: READ fault inside `rtalloc::Free` from `MemFree` in
    `ComponentEntity_Dtor` <- `InternalBasePtr::DeletePtr` <- `EntityList_RemoveEntity` <-
    `EntityList_Shutdown` <- `GameUpdateLoop_Shutdown`. The normal-size town's pause-menu
    Quit exits 0; this is the teardown defect already listed above (seen once on 09-13
    morning), now reproducible with the 800-citizen town on the debug exe.
- **Serializer gate exists and passes**: `tools/resave_gate/resave_gate.sh Gate` (README
  there). Load a save DLL-absent, serialize the restored world again before the first tick,
  compare the raw bytes against a golden. Golden cut against `Gate.sav`
  md5 `68f99e06e6cdce6c17a8fb06e867b2c8`; two pinned runs byte-identical.
- Its first run found the last DLL-absent SAVE blocker: `CompressedStream::SetWindow`'s
  write-side grow path was still delegated to the DLL (the cutover gate never saved).
  Ported; the DLL-absent autosave now completes.
- Seen once each, pre-existing, not fixed: a loader race crash in `ExternalFactory::Add`
  (AVL rotate on a null node, `[GUL::Load] redundant dispatch` just before) and a hang in
  teardown after the pause-menu Quit returned.

- **2026-09-13: C.3 — the options struct exists and `standalone/` is at zero `getenv`.**
  `standalone/options.{h,cpp}`: one `Options` struct, filled once by `OptionsInit()` at the
  top of `WinMain` (before `HarnessGuardUnattendedBoot`, before any other reader), read
  everywhere else as `Opt().field`. Every field carries a comment naming its reader — a
  Makefile target, a `tools/` script, `perf/`, or a route documented in CLAUDE.md — and the
  ones nothing outside the tree sets are marked `[no tool reader]`, which is the C.4 delete
  list ready-made. The header also declares the tier-3 fields the `code/` sites will move
  onto (world setup, the stress/perf knobs, the `BUILD_DEMO_*` founding-town flags,
  `MENU_SCENE`, `SAVE_TEST`, `MODAL_ANSWER`), so it is the whole inventory, not just
  standalone's half.
  - **Both mid-run environment WRITES are gone.** `stress_harness.cpp`'s `SetIfUnset` bundle
    (autostart, map/seed/difficulty/climate, camera pin, the demo buildings, the in-game
    quit) is now `ApplyStressDefaults` inside `OptionsInit`, and
    `construction_force_harness.cpp`'s `_putenv_s("BANISHED_BUILD_SYNC_DELIVERED")` — which
    raced `GatherRawMaterials::Execute`'s own cached read — is the derived field
    `Options::buildSyncDelivered`. `OptionsInit` still `_putenv_s`es the stress bundle for
    the ~600 not-yet-migrated `code/` readers; that half goes with them, and `env_cache.h`
    stays until then.
  - **Deleted with their flag** (no reader in Makefile / tools / perf / .vscode / CLAUDE.md /
    STATUS.md): `BANISHED_W17_ATTEND` and all of `standalone/wave17_door_attend.cpp` (the
    Wave 17 door-attendance census, a per-`ResolveExport` strcmp loop),
    `BANISHED_MOUSE_DRAGSKEW` (existed only to skew what the A.4-deleted diff oracle could
    see), `BANISHED_AUTOSAVE_TRACE`, `BANISHED_LANE4_TRACE`, `BANISHED_W2LANE3_TRACE` (wave
    traces), and a dead `EnvOn` in `rt_data_mint.cpp`. Nothing else was deleted: the
    gameplay harnesses are wired into `src/game/Game/gameupdateloop.cpp`, `starttown.cpp`
    and a dozen components, so retiring them belongs with the `code/` pass, not this one.
  - Verified: `standalone-debug` and `standalone-perf` both build clean, and all three
    routes exit 0 with an empty `standalone_crash.log` — `sa-run` with
    `BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_QUIT=120` (town built, pause menu opened at
    frame 601, Quit clicked at 721), `BANISHED_AUTOQUIT`, and the Load Game dialog
    (`BANISHED_AUTOMENU_BUTTON=2 BANISHED_AUTOMENU_CLICKS=m6`).

- **2026-09-13: C.2 and C.4 are closed — the whole tree is at zero `getenv` and there are
  no live-feature gates left.** Four directory batches plus a closing pass.
  - **Gates:** the 38 remaining `kDefaultOnFlags` entries, `LiveEnabled`, `EnvOn` and the
    `BANISHED_LIVE_COMPONENTS` master read are deleted; the `EnvVar` parameter is gone from
    every `COMPONENT_LIVE_FACTORY*` / `CONTROLLER_LIVE_FACTORY*` macro and its 14
    invocations. The twelve flags the batches left undecided were proven in one forced-on
    run (all twelve set: `sa-run` + pause-menu quit exit 0, empty crash log, resave gate
    PASS twice, byte-identical to the golden) and graduated: `LIVE_AUTOSAVE`,
    `LIVE_BUILD_ONREMOVE`, `LIVE_CEMETERY`, `LIVE_CITIZEN_FAMILY`, `LIVE_CROPDISEASE`,
    `LIVE_FOUNDING_SPAWN`, `LIVE_HAPPINESS`, `LIVE_HEALER`, `LIVE_MARKETLOC`,
    `LIVE_TOMBSTONE`, plus `CLEAR_FOOTPRINT` and `LIVE_ANIMALS`, whose readers the batches
    had already removed. Graduating `LIVE_FOUNDING_SPAWN` deleted its off arm: the
    hand-rolled `SpawnOneCitizen` family loop in `starttown.cpp` and its four helpers
    (~230 lines) are gone, so the retail `sub_1040` port is now the only founding spawner.
    Three standalone opt-ins that were OFF on every default run went with their flags
    (`LIVE_CITIZENAI_TIDS` / `_HAPPYID` / `_AIID`: the `c42c0` and `c70e8` type-id fills).
  - **`getenv` is zero** in `code/`, `runtime/`, `src/render/dx11/`, `engine/` and `standalone/` — the
    only call sites left are in `src/app/options.cpp`. 69 reads moved onto `Opt()`
    (code 66, dx11 1, engine 2), with ~40 new typed/raw fields in `options.h`.
    `standalone/env_cache.{h,cpp}` and the `-include standalone/env_cache.h` in the
    Makefile are deleted, and `ApplyStressDefaults` no longer writes the environment: it
    sets the struct fields, so `OptionsInit` is now read-only with respect to the CRT
    environment block.
  - **Shims deleted with their call sites:** `MapQuery_FloorTraceOn` (+10 guarded traces),
    the empty `StartTown_DriveProduction` (and `production_harness.cpp`),
    `WorkPlaceComponent_ControllerLive`, `ObjAudit::Enabled` and the diagnostic half of
    `internal_factory_audit` (`Scan`, `ReportLeaks`, `NoteUnregisteredFree`;
    `IsRegistered`/`Count`/`Factory` stay — they are load-bearing on the free path), and
    the six empty probe stubs `WorkSchedProbe_Report`, `DumpSlot3Inventory`,
    `ModProbe_DumpModList`, `ModProbe_StartWatcher`, `TradeUI_PriceProbeReport`,
    `TradeUI_SortSelfTest`. `Options::buildSyncDelivered` lost its last reader and went too.
  - Verified on both exes: `standalone-debug` and `standalone-perf` build with exit 0 and no
    new warnings, and every route exits 0 with an empty `standalone_crash.log` — `sa-run` +
    pause-menu quit, `BANISHED_AUTOQUIT`, the Load Game dialog
    (`BANISHED_AUTOMENU_BUTTON=2 BANISHED_AUTOMENU_CLICKS=m6`), and `make sa-stress` on the
    perf exe (6000 frames, clean self-quit).

- **2026-09-13: frozen-exe detector.** The boot-state watchdog now carries the main loop's
  frame counter; a stalled counter publishes `STATE=HUNG` (exit 5 from `tools/boot_state.sh`),
  writes `standalone_hang.log` with all-thread stacks, and on unattended runs terminates the
  process with exit code 4 — so a hang costs ~30 s instead of the caller's whole timeout.

