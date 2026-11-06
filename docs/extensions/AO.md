# AO -- `ext.ambient_occlusion` (class A): screen-space ambient occlusion, the plan

Decided with the user 2026-10-14 (afternoon) from three surveys: the frame pipeline, the options
menu / registry, and the algorithm research. Rulings are in §1; everything else is derived. Blocks
hand in under `handins/AO*.md`; this sheet is updated when a block closes a question in §6.

## 1. Rulings (the user, 2026-10-14)

1. **Intel ASSAO first** (github.com/GameTechDev/ASSAO, MIT). XeGTAO only if ASSAO's look
   disappoints; the AO stage is written against a small internal interface so XeGTAO can replace the
   middle without touching the depth resolve, the seam, the apply pass, the settings or the menu.
2. **Post-multiply the finished opaque colour** (sun + fog included) between the Opaque and Blended
   lists. Modulating only the ambient term would mean recompiling every retail `.srsl`: out.
3. **Menu**: an Off / Low / Medium / High combo and a 0-100 opacity slider in the **graphics
   menu**. Preferred shape: a new tab in the Options dialog ("Enhanced graphics") added by the
   extension, or a dynamically spawned dialog. Overriding retail's packaged `Options.rsc` objects
   is acceptable only if the spike proves a loose `.crs` wins over the `.pkg` object.
4. **A spike block first** (AOSPIKE), no port work before its go / no-go facts are in.

## 2. Facts the design rests on (the surveys; file:line are the tree's on 2026-10-14)

**Frame.** `Renderer::Draw` pass 2 walks the scene entries backward (`src/engine/graphics/
wave27_lane_gfxbig.cpp:1035`); per entry `Clear(6)` (depth + stencil, colour NOT cleared), then
`for k in 0..2: for each light: DrawLight(k)` (`:1091`). k = `MaterialType`: 0 Opaque (Opaque,
OpaqueSkin, Snow, InWater, AlphaTest, Billboard, Foliage, Terrain, Sky, SelectionMask), 1 Blended
(Lake, River, Particle, Weather, TerrainDecal, SelectionEdge, StatusIcon, Debug), 2 Effect (no kit
material uses it). The render-thread extension runs a copy of the loop at
`src/ext/render_pipe/passes.cpp:136`. **The AO hook is the k=0 -> k=1 boundary of the MAIN scene
entry, in both loops**, and nowhere else: the water-reflection entry (shader 2, its own 640x360
target), the main-menu scene (`menudialogloopt.cpp:696`) and the minimap paint
(`minimap_paint.cpp:211`) also go through `Renderer::Draw` and must not get it.

**Targets.** Main colour: back-buffer size, B8G8R8A8, MSAA, RENDER_TARGET only
(`src/render/dx11/devicedata.cpp:513`). Main depth: D24_UNORM_S8_UINT typed, MSAA, DEPTH_STENCIL
only -- **not shader-readable** (`devicedata.cpp:517`, `:488-511`; DSV at
`imagebufferdata.cpp:440-467`). MSAA sample count = `Configuration::multisample + 1`
(`devicedata.cpp:387-401`; default index 1 = 2x; the swap chain is 1x, `EndCommands` resolves
colour every frame, `contextdata.cpp:458-471`). Engine ImageBuffers of kind 1 / format 7 ARE
readable: R24G8_TYPELESS, DEPTH_STENCIL | SHADER_RESOURCE, SRV R24_UNORM_X8, always 1x
(`imagebufferdata.cpp:505-595`) -- the shadow map and the reflection depth use it. Mid-frame
colour resolve precedent: `FrameCopyImageBuffer::Generate` (`wave23_lane_gfxbody.cpp:606-618`,
`smallclasses.cpp:180-206`). Screen-size targets rebuild on device reset through the tracked-image
registry (`src/engine/video/dynamic_resources.cpp:242-262`).

**Camera.** Perspective, fovY 40, near 2, far 512 (`gameupdateloop_load.cpp:532-553`; eye_camera
near 0.1). `Graphics::Camera` holds `_cameraToWorld` (+0x64), `_projection` (+0x94), `_zUp`
(+0xd4), a frustum with `isOrthographic`; near / far / FOV are NOT stored. `Camera::SetFrustum`
(`renderer.cpp:447-464`) builds the projection with `RtVideoDeviceUsesZeroToOneDepth()`
(`renderer.cpp:198`) and multiplies in the Z-up -> Y-up swap when `_zUp` is set. Shaders get
`SetCamera(cameraToWorld, projection)` in `DrawItemList` (`batchc_gfx_leaves.cpp:692-710`).

**Shaders.** No runtime compiler in the exe (no D3DCompile anywhere in `src/`); precompiled DXBC
goes straight to `CreateVertexShader` / `CreatePixelShader` (`shaderprograms.cpp:126-217`).
Retail's are SRSL compiled by the kit's `Tools-x64.exe` (ps_4_0). `fxc.exe` is on this box
(`C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe`), `D3DCompiler_47.dll` in
System32. The Linux renderer is the Vulkan backend since Vulkan P5, which converts the same DXBC
(`src/render/vulkan/ao/assao_pass.cpp`).

**Lighting.** Per-pixel constant ambient in every lit shader (`SetLight`,
`batchc_context_setlight.cpp:135-200`; kit `lighting.srsl` `ComputeLighting`:
`(sun * shadow * sat(-N.L) + ambient * ((N.L+3)/2) * ambientTint) * diffuse`, then fog). Models
already carry **baked AO** (`e_aoMap`, slot 3, as `ambientTint`). The colour alpha is NOT free
(terrain writes the water height, River reads it back).

**Settings / menu.** The registry (`src/ext/registry.h:96-105`, `registry.cpp:21-127`) is
**bool-only**: `ParseToggle` accepts "0" / "1" (`registry.cpp:185-191`), slots are `bool&`, keys
`ext.<id>`; no extension carries a value today. The Options dialog is retail's class
(`src/game/Dialog/OptionsDialog.h`, sizeof 0x1C0 pinned at `:157`); its graphics tab is a 2x9 table
in the kit's `Dialog/Options.rsc:225-259`; the shadow / reflection combos are found by NAME in
`OnOpened` (`optionsdialog.cpp:616-624`) and read as a plain index in `ApplyVideoSettings`
(`:848`, `:908-910`), which writes `Video::Configuration` and calls `ApplyChangedSettings` (no
device reset unless renderer / adapter changes). Sliders: `SetSliderRange` / `SetSliderValue`
(`:217-224`), UI scale 75..150 (`:663-668`). The tab control is `TabDescription tabOptions`
(`Options.rsc:70-99`, four `Tab _tabs` entries, button + content), `OptionsDialog::tabOptions` at
+0x1B8, `SetActiveTab(lastTab)` at `optionsdialog.cpp:706-711`. Our dialog resources ship as
loose compiled `.crs` under `<game>/WinData/{Dialog,Font,Build}/` via `tools/ext_resources.sh`
(TARGETS at `:125`, deploy at `:174`); only NEW object names have been shipped this way.

**Algorithms** (the research, 2026-10-14; costs are Intel's, 1080p):

| | ASSAO | XeGTAO | SAO (G3D / Filament) |
|---|---|---|---|
| licence | MIT | MIT | BSD / Apache-2 |
| stage | pixel shaders, DX11 host shipped (`ASSAODX11.cpp`) | compute only, SM6-tested, no DX11 host | GLSL |
| inputs | depth only (D24 via R24_UNORM_X8 or R32F); normals optional | depth + normals advised (R32) | depth only |
| temporal | none; the "temporal" fields only rotate the kernel (leave 0) | wants TAA; else noise 0 + 2-3 denoise | none |
| tiers | Lowest 3 / Low 6 / Medium 10 / High 24 samples / Adaptive | Low 4 / Med 8 / High 18 / Ultra 54 | sample count |
| RX 480 ms | 0.35 / 0.64 / 1.03 / 1.39 | -- (GTX 1050: 1.5 Med, 2.2 High) | ~2 (old) |
| known artefacts | screen-edge kernel (margin), aliased-normal flicker (smoothing), low-poly self-occlusion (`HorizonAngleThreshold`), halo (`Sharpness`), ortho breaks | thin-object halo, static noise without TAA | halo, banding |
| port effort | 1-2 | 3-4 | 2-3 |

Excluded: HBAO+ (NVIDIA 1-way licence), Unity (Companion licence), Crytek sphere SSAO (flat terrain
reads grey), BAR / OpenMW code (GPL; ideas only). ASSAO wins on fit: pixel shaders, own math types
(no DirectXMath), accepts our depth, no history, tiers map 1:1, `DrawOpaque=false` multiplies into
the bound RTV. GCC fixes known: explicit `-ld3d11`, drop `#pragma comment(lib)`, drop the
`MessageBoxA` on shader errors, replace the runtime `D3DCompile` with precompiled blobs.

## 3. The shape

```
src/render/dx11/ao/            the D3D side (precedent: render_pipe_finish.cpp lives in the backend)
  ao_stage.h                   the internal interface: Resize(w,h,samples), Draw(depthMS, proj, view, params)
  ao_depth_resolve.cpp/.hlsl   Texture2DMS<float> D24 -> R32_FLOAT linear view depth, 1 fullscreen PS
  assao/                       Intel's ASSAO.h / ASSAODX11.cpp / ASSAO.hlsl, MIT header kept, GCC fixes marked
  shaders/*.h                  the fxc-compiled DXBC byte arrays (ps_5_0 / vs_5_0), regenerated by tools/ao_shaders.sh
src/ext/ambient_occlusion/     the extension: Sync(), params (quality tier, opacity, radius curve), the seam set/clear
src/game/Dialog/               the menu work (shape decided by the spike, §6.3)
```

- **Seam** (DESIGN §3 rule 5): one null-by-default static pointer in `Graphics::Renderer` (or
  `Scene`), tested once at the k=0 -> k=1 boundary of both loops, only for the main scene entry
  (`target == context._defaultRenderBuffer`, shader 0). The faithful path pays one null test. The
  ext sets it in `Sync()` and clears it when toggled off (live, class A).
- **Apply**: `colour *= lerp(1, ao, opacity)` by blend state (ZERO / SRC_COLOR) into the bound MSAA
  colour target -- the colour is never read, so RENDER_TARGET-only is fine. Depth test off, depth
  write off, so the Blended list draws over it as today.
- **Depth**: when the ext is on, the main depth is created readable (kind 1 / format 7's recipe:
  R24G8_TYPELESS + SRV) -- OR, if the typed MSAA DSV cannot be made readable without changing the
  faithful creation, a readable copy. Then the resolve PS reads sample 0 (or the min) of the MSAA
  depth into R32F linear depth. At 1x the resolve is a plain copy / linearise.
- **Zoom**: radius in world units, `lerp(r_street, r_overview, t(camera distance))`, AO faded to 0
  past a far distance (`FadeOutFrom` / `FadeOutTo`). Buildings are a few units tall; start at
  ~0.5-1x a building's height at street level.
- **Tiers**: Off / Low = ASSAO Low (6) / Medium = Medium (10) / High = High (24). Lowest (half-res, 3)
  is the fallback if Low measures too dear on the stress town. Since issue #253 Lowest is a menu tier
  for phones (Off / Lowest / Low / Medium / High), stored as `ext.ao_quality=4` so 1-3 keep their meaning.
- **Settings**: `ext.ambient_occlusion` (the bool row, checkbox in the Extensions dialog as today)
  plus two VALUED keys, `ext.ao_quality` (1-3) and `ext.ao_opacity` (0-100), through a small generic
  "valued key" addition to the registry (the first one; do it once, properly: parse / clamp /
  default / `SetAndRecord`). The combo is the master: Off = the bool off; the checkbox mirrors it.
- **Device reset**: the AO targets register with the tracked-image registry and rebuild on resize /
  `settingsChanged`.
- **Faithful profile untouched**: with the row off, no target is created, no pass runs, one null
  test per frame. The stress run faithful vs on is in every hand-in (DESIGN §3 rule 6).

## 4. Blocks

| Block | Owner | What | Done when |
|---|---|---|---|
| **AOSPIKE** | one opus agent | the go / no-go facts of §5, in a worktree, NOTHING merged but the hand-in | `handins/AOSPIKE.md` answers every row of §5 with evidence (a log line, a screenshot, a RenderDoc fact), §6 updated |
| **AOCORE** | one opus agent | readable depth + resolve, the ASSAO port (GCC, precompiled blobs), the seam, the apply pass, the bool row, the stage interface, unit tests of the pure parts (radius curve, tier table, params) | `tools/build.sh run` clean; RenderDoc capture shows the AO texture and the multiply; `trace_oracle` frame differs ONLY by the AO pass; faithful stress unchanged, everything-on stress recorded; gate at 0 |
| **AOMENU** | one opus agent | the valued keys in the registry, the Options tab / dialog (per §6.3), the Extensions-dialog mirror, Apply / Cancel semantics | the combo and slider change the live frame; settings.cfg round-trips; unit tests of the key parsing |
| **AOTUNE** | one opus agent, then the user by hand | radius / zoom curve, tier mapping, baked-AO double-darkening, foliage noise, screen-edge margin, sky at depth 1.0 | the user's look ruling per tier; numbers in the hand-in |

AOCORE and AOMENU can run in parallel once the spike is in (AOMENU's keys are independent of the
D3D work); AOTUNE after both. Agents: opus, `--jobs 2`, one game run at a time (the game dir is one
shared install), merge main before the final build, the coordinator runs the gate.

## 5. The spike's questions (AOSPIKE answers each with evidence)

| # | Question | Why it matters | Expected evidence |
|---|---|---|---|
| S1 | Does `ASSAO.hlsl` compile with `fxc` to vs_5_0 / ps_5_0 for every entry point and every `#define` combination `ASSAODX11.cpp` requests? List the entry points and defines. | no runtime compiler: the blobs must be made offline | a `tools/ao_shaders.sh` (or `.py`) that produces the `.h` byte arrays, its log, the byte counts |
| S2 | Does `ASSAODX11.cpp` + `ASSAO.h` compile under GCC (UCRT64, the project's flags incl. `-fno-lifetime-dse`) with the runtime compile replaced by the blobs? What else had to change (list every edit)? | GCC port effort claim | a compiling object in the worktree, the diff against upstream |
| S3 | Can the main depth be created shader-readable (R24G8_TYPELESS + SRV) under MSAA 2x without changing the faithful creation when the ext is off? Does `CreateDepthStencilView` on a typed-less MSAA texture work as retail's typed one did (D3D11 debug layer clean)? | the input | a run with the ext path forced on, `[AO]` stderr line with the formats, debug-layer output |
| S4 | Does a `Texture2DMS<float>` resolve PS produce a correct R32F linear depth at 2x and at 1x (`multisample` index 0)? Is `RtVideoDeviceUsesZeroToOneDepth()` true on DX11, and which handedness / Z-up swap does `_projection` carry? Write down the exact linearisation formula that works. | ASSAO wants "LH projection or equivalent"; the Z-up swap is in the projection | a RenderDoc capture (`captures/`) with the R32F target saved as an image showing the scene depth |
| S5 | Does a loose `WinData/Dialog/Options_*.crs` override the packaged object? (compile a copy of `Options.rsc` with one visible change, deploy loose, open Options) | ruling 3 | a screenshot or the dialog's element tree on stderr |
| S6 | Can the extension add a FIFTH tab to `tabOptions` (a new `TabDescription` object with five entries, loaded instead of retail's; or the tab element extended at runtime through `UI::TabElement`'s API)? Which of the two is cheaper and does `lastOption` / `SetActiveTab` survive it? | ruling 3's preferred shape | the tab visible in a run, or the API reason it cannot be |
| S7 | Where does Sky sort in list 0, and what depth does it leave (1.0 = far plane)? Does anything fill list 2 (Effect) at runtime (count the items per list for one in-game frame)? | ASSAO must skip the far plane; the hook position | one `[AO] lists: 0=N 1=M 2=K` stderr line from a run, the sky material's depth state |
| S8 | What does the stress town cost for an EMPTY hook (the seam + a no-op pass that binds and unbinds the targets)? | the faithful-path regression rule | the faithful stress row vs the row with the empty hook on, machine load CLEAN |

## 6. Decided by the spike (filled in when AOSPIKE hands in)

Evidence for every line: `handins/AOSPIKE.md` (2026-10-14).

- 6.1 depth input: **the readable main depth, no copy.** Under the ext the main depth is created
  `R24G8_TYPELESS`, `DEPTH_STENCIL | SHADER_RESOURCE`; its DSV gets an explicit `D24_UNORM_S8_UINT`
  desc (`TEXTURE2DMS` / `TEXTURE2D`), its SRV `R24_UNORM_X8_TYPELESS`. Verified at 1x / 2x / 4x, D3D11
  debug layer silent for the views and the pass. The faithful arm stays the literal retail call. The
  format is fixed at device creation / rebuild, so the live toggle goes through the rebuild path.
- 6.2 linearisation: DX11 `zeroToOne = true`; `_projection` = retail's RH 0..1 projection x
  `kZUpToYUp`, i.e. LH in the Z-up camera (y forward, `w = +y`), n = 2, f = 512.
  `viewDepth = (n f/(f-n)) / (f/(f-n) - z)` = `-M[3][2] / (M[1][2]/M[1][3] - z)` = 2.00784 / (1.00392 - z);
  z = 1.0 is the far plane (sky). ASSAO is handed `_projection` with **rows 1 and 2 swapped**
  (a standard LH perspective), `MatricesRowMajorOrder = true`.
- 6.3 menu shape: **a fifth tab added at run time** by `UI::TabElement::AddTab` from
  `OptionsDialog::OnOpened`, its button + page in a new `.rsc` under new object names (loose files
  never override packaged names: `IO::FileSystem::OpenFile` checks the packfile index first). The
  `lastOption > 3 -> 0` clamp (`optionsdialog.cpp:710`) must learn the fifth tab (retail's or the
  port's: check first). The page is a `ContainerDescription` deriving from `Options.rsc:gameOptions`
  (the Game tab's size, clip region and scroll bar) around the rows' table, so the fifth tab takes
  the retail tabs' room: the tab strip is not widened and the scroll bar sits at the pane edge as on
  the retail tabs. A row that needs more room wraps or moves its extra text into the control column
  (the derived-data cache's size sits there, its button on the row under it).
- 6.4 blob tool: `tools/ao_shaders.py` (fxc, `python -I`): `VSMain` vs_5_0 + 21 ps_5_0 entries, one
  define set (`SSAO_MAX_TAPS=32`, `SSAO_MAX_REF_TAPS=512`, `SSAO_ADAPTIVE_TAP_BASE_COUNT=5`,
  `SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT=(32-5)`, `SSAO_DEPTH_MIP_LEVELS=4`,
  `SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION=0`), `/Ges /O3 /WX`; 5 entries exist only for the
  Adaptive level and can go. ps_5_0 needs feature level 11_0: gate the row on it.
- 6.5 (new) **the main-menu scene passes the main-entry test** (`target == _defaultRenderBuffer`,
  shader 0): the seam must be armed from the game's draw only, or the user accepts AO on the menu
  backdrop. List 2 (Effect) is always empty; Sky is item 0 of list 0, depth test and write off.
  **2026-10-17: the user chose the backdrop** -- AO-MENU arms the seam from the menu loop's draw too
  (`handins/AO-MENU.md`; the menu's own controller distance, -13% menu fps at High).
- 6.6 (new) the empty hook (seam + readable depth + bind / unbind, no draw) costs nothing measurable on
  the stress town (draw +0.9%, sim +1.3% = the pair's noise; machine load CLEAN; fps capped at 140).

## 7. Open after AOCORE + AOMENU (2026-10-14 evening; AOTUNE's list, the user's look first)

The feature ships OFF; the player turns it on from Options > Enhanced. Before AOTUNE is kitted the
user looks at it by hand (debug exe deployed): each tier at street level and at the overview, the
slider, Off; a live toggle mid-game (class A: no reload); the Extensions-dialog checkbox mirror.

| # | Item | Owner | Done when |
|---|---|---|---|
| 7.0 | **The user's first look (2026-10-14 evening, release exe): "overall looks good."** Rulings: strength 20% is the usable maximum, past 30 "iffy" -> `ext.ao_opacity` is **0..30, default 20** (slider range the same; the multiplier stays percent / 100). Finding: **AO drawn over the fog** (the materials fog in their pixel shaders, the multiply came after) -> the ASSAO fade now follows the frame's fog range (`AoMath::FadeFromFog` from `Context::_fogParams`, the zoom fade only when no fog is set). Both DONE the same evening, unit 1217 / 0; the fog fix awaits the user's second look. **Boot defect found by the look ("no window"), FIXED:** with the row ON in settings.cfg the boot-time readable-depth rebuild set `settingsChanged` before the first frame; `DeviceData_Resize` early-outed on the never-shown window and left the flag set, `BeginCommands` refused every frame, no `Draw` ran, and the deferred reveal (which lives in the splash `Draw`) never fired -- every flavour, not just release (bisected with `BANISHED_DEFER_WINDOW_SHOW=0`, then stderr traces). Fix: the early-out is skipped while `BootWindow::IsPending()` (the reset runs on the not-yet-revealed window; its client rect is valid). AOCORE's "row-on run clean" never looked at the window | done | the user's second look |
| 7.1 | **The look.** | RULED by the user 2026-10-16 (live, through the debug tweak overlay) | every tier "fine" as shipped except the overview radius: `AoMath::kRadiusOverview` 4.0 -> **5.0** (`ao_math.h`; street radius, fades, ASSAO params unchanged). No retune block needed |
| 7.2 | GPU cost per tier | DONE 2026-10-16 (`handins/AOTUNE.md` §7.2) | perf exe, compact town, 4x MSAA, fps uncapped, machine CLEAN: faithful 904 fps; Low 866 (-4.2%), Medium 838 (-7.3%), High 785 (-13.2%); sim / draw CPU unchanged; +70..80 MB private. Lowest is not a menu tier, not measured |
| 7.3 | The live toggle in game and device-lost with the row on | DONE 2026-10-16 (`handins/AOTUNE.md` §1-2) | PASS: six toggle states captured (AO passes present / absent, the depth format follows, tier and opacity change without a rebuild), memory flat over 10 off/on cycles (the DSV fix holds); the forced loss: one round trip, the AO stage rebuilt, passes back after the trip, crash log 0. Found beside it: a post-trip memory climb that happens with the row OFF too -> known_port_issues row 1329 |
| 7.4 | Eye camera + AO | DONE 2026-10-16 (`handins/AOTUNE.md` §3) | PASS: a follow at High / 20, the nearest surface reads ~1.1 units (the 0.1 near plane in use), AO not blown out at the near plane |
| 7.5 | Linux: the AO pass on the Vulkan backend (lavapipe bed; the Phase D.4 D3D11 route this row was written for went in Vulkan P5) | AOTUNE or the Linux list | `tools/linux_run.sh newgame` with the row on, crash log 0 |
| 7.6 | `tools/trace_oracle/capture_arm.sh` | DONE `e930322f2` | the in-game capture is picked by its rank after the `capture armed for 0 frames` line; the game is killed by the PID the watchdog publishes, never by image name; refuses to start over a running game |
| 7.7 | The new-game 5694 / 0 ExtLoad count and worldcmp were not run at this close (the ext is OFF in faithful; the resave gate covered the load / save route) | next gate | the next full gate's table |
| 7.8 | AO on the main-menu backdrop (ruled game-only for now, one arm site to change) | DONE 2026-10-17 (`handins/AO-MENU.md`) | the menu loop's draw arms the seam like step 7; live Apply at the menu, the game <-> menu hand-off and row-off = no resource all checked; -13% menu fps at High (uncapped) |
