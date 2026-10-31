# TWEAKUI hand-in: the debug tweak overlay (Dear ImGui, Alt+F10)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

Branch `worktree-agent-a0c76cd55353c663f`, based on main `57964ec3b`. Not merged. User doc:
`docs/reference/tweak_overlay.md` (opening it, the panels, adding a tweakable).

## 1. What shipped

- **`third_party/imgui`**: Dear ImGui **v1.92.9b** (the latest release, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab`,
  2026-07-31), MIT, `LICENSE.txt` kept. Cloned with `--depth 1 --branch v1.92.9b` into a fresh empty scratch directory,
  `git rev-parse HEAD` checked against the tag, then 15 files copied: the core (`imgui*.cpp/h`, `imconfig.h`,
  `imstb_*.h`) and `backends/imgui_impl_sdl3.*`, `backends/imgui_impl_dx11.*`. Unmodified; sha256 of each in
  `third_party/imgui/README.banished.md`. Commit `third_party:`.
- **CMake option `BANISHED_TWEAK_OVERLAY`** (commit `build:`): defaults ON for `BANISHED_FLAVOUR=debug` on Windows, OFF for
  og / perf / release and Linux. ON adds the ImGui sources (`-w` per file, as ASSAO's warnings), the overlay's two
  sources, `-DBANISHED_TWEAK_OVERLAY=1`, the include dir and `d3dcompiler` (imgui_impl_dx11 compiles its two shaders
  with `D3DCompile`; the debug exe now imports `D3DCOMPILER_47.dll`). OFF: `tweak_overlay.cpp` and `ext_tweaks.cpp` are
  removed from the globs, nothing of ImGui is compiled, every site below is `#if`'d out.
- **`src/diag/tweak_overlay.h` + `src/diag/win/tweak_overlay.cpp`** and **`src/ext/tweak/ext_tweaks.{h,cpp}`** (commit `diag:`): the overlay and the
  two tweak structs (`Ext::Tweak::Ao`, `Ext::Tweak::Faa`; defaults = today's constants, a copy under a mutex each way).
- Panels: stats line (fps, ms, sun map size, FAA gen / mode / transformed blobs / binds); Rows (5 checkboxes via
  `Ext::SetAndRecord`); AO (quality, strength via `Ext::SetValueAndRecord`; radius street / overview, shadow
  multiplier, power, horizon angle threshold, blur passes, sharpness, detail strength, fog fade on / from x / to x /
  ceiling via the tweak struct); Foliage (mode via `Ext::FoliageAA::Set`; k and floor via the tweak struct +
  `Ext::FoliageAA::Sync`, on release; no fwidth clamp: the rewriter's `1e-4` immediate has no hook); Shadow (the row,
  the map size); Write to settings.cfg; Reset defaults; the key-less values printed for copying. Under the faithful
  profile: "faithful profile: nothing to tweak", every control disabled.

## 2. Where it hooks (all inside `#if BANISHED_TWEAK_OVERLAY`)

| Site | What |
|---|---|
| `src/platform/sdl/pump_sdl.cpp` `Platform::Pump` | `MainThreadTick()` at the top (applies queued changes; one atomic load when idle); `OnSdlEvent(ev)` per event: the toggle, and while open the mouse / key / text events are queued for ImGui and not translated for the game |
| `src/platform/sdl/input_sdl.cpp` | `ReadKeyboard` (all released), `ReadMouse` (zero motion / wheel / buttons, rotate drag off), `AsyncMouseButtonState`, `AsyncKeyState` (0) while open |
| `src/render/dx11/contextdata.cpp` `ContextData::Finish` | `BeforePresent(...)` right before `Present` (direct path, main thread) |
| `src/render/dx11/render_pipe_finish.cpp` `RenderPipe::FinishPresent` | the same before the render thread's `Present` |
| `src/render/dx11/devicedata.cpp` `DeviceData::Shutdown` | `OnDeviceShutdown()`: `ImGui_ImplDX11_Shutdown` before the device goes |
| `src/ext/ambient_occlusion/ao_math.cpp` `FrameFor`, `FadeFromFog` | read `Ext::Tweak::AoValues()`; the `#else` keeps today's code |
| `src/ext/foliage_aa/foliage_aa.cpp` `Sync` | k / floor from `Ext::Tweak::FaaValues()` |
| `src/render/dx11/foliage_aa/*`, `src/engine/graphics/SunLight.h` / `sunlight.cpp` | read-only stats getters (`FaaBackend::Stats`, `SunLight::ExtTweakSunMapSize`) |

Outside the `#if`s one refactor with no codegen change: `ao_math.h` names the four ASSAO literals and the fade ceiling
(`kShadowPower`, `kHorizonAngleThreshold`, `kSharpness`, `kDetailShadowStrength`, `kFogFadeCeiling`) so the tweak
defaults are the same constants (proved by the perf comparison in §4).

## 3. The thread story under render_pipe

- The ImGui context and the SDL3 backend are created on the main thread at the first open (`ImGui_ImplSDL3_InitForD3D`;
  the two SDL hints it sets, `MOUSE_FOCUS_CLICKTHROUGH` / `MOUSE_AUTO_CAPTURE`, are restored right after it; IME,
  clipboard and shell callbacks nulled; no imgui.ini; `NoMouseCursorChange`).
- After that only the **presenting thread** touches ImGui: `BeforePresent` drains the event queue the pump filled
  (mutex), runs `ImGui_ImplSDL3_ProcessEvent` on each (pure io updates), sets `DisplaySize` from the back buffer and
  `DeltaTime` from `SDL_GetTicksNS`, then `ImGui_ImplDX11_NewFrame`, `ImGui::NewFrame`, the panels, `Render`, and
  draws into an RTV on the swap chain's buffer 0 (OM targets saved / restored around it; the backend saves the rest).
  `ImGui_ImplSDL3_NewFrame` is deliberately not called: it calls SDL mouse / window functions (capture, warp, cursor,
  IME) that belong on the main thread.
- The panels never touch the registry or render state: they queue actions (mutex), and the pump applies them on the
  main thread at its next call (`Ext::SetAndRecord` etc., the same thread and moment class as the dialogs), and
  snapshots the registry for the UI. A render_pipe or max_shadow_res toggle therefore runs exactly where the dialog's
  would.
- Device loss: on the render thread path `FinishRecover` runs `DeviceData::Shutdown` on the main thread with the render
  thread idle; `OnDeviceShutdown` (behind a draw mutex) releases the backend; the next open frame re-inits on the new
  device and ImGui re-creates its font texture (1.92's "destroyed by the backend" path).

## 4. Evidence (logs and screen captures in `C:/GOG Games/Banished/tweakui/`)

| Item | Result |
|---|---|
| Debug build | `tools/build.sh build debug --jobs 2`: clean, no warning from any touched file |
| Boot clean, toggle (run A, ext AO + FAA on, render_pipe off) | crash log 0 bytes; `[TweakOverlay] open (Alt+F10)` / `closed (Alt+F10)`; `a0_closed.png` / `a1_open.png` / `a5_closed.png` |
| Overlay on the direct path | `drawing on thread 85096 (the main thread; ext.render_pipe=0) at 2560x1377` |
| AO strength live | slider clicked to 0 then 30: `[Ext] ext.ao_opacity=0`, `[Ext] ext.ao_opacity=30` (read by `AO::Current()` the next frame); `a2_strength0.png`, `a3_strength30.png` (the season turned to snow between them, so the pair is not a clean A/B of the AO alone) |
| Foliage k re-creates the shaders | k clicked to 0.282: `[FoliageAA] request mode 2 k 0.282 floor 0.150 (gen 2)`, `stage reset (mode / k request): previous gen 1 ... 24 transformed`, `stage gen 2 ... fill k 0.282`, 24 `transformed ... gen 2` lines |
| Device lost, overlay open, direct path (run B, `BANISHED_FORCE_DEVICE_LOST=40000`) | `DX11 backend released (device shutdown)` -> `round trip COMPLETE` -> `DX11 backend on device <new>`; crash log 0; overlay redrawn with its text (`b1_after_devlost.png`); a slider click after it applied (`ext.ao_opacity=0`) |
| render_pipe (run C, `ext.render_pipe=1`, loss at 40000) | `drawing on thread 167852 (NOT the main thread; ext.render_pipe=1)` (`c1_renderpipe_open.png`); `INJECTING ... (render thread)` -> `DX11 backend released` -> `COMPLETE` -> re-init on thread 167852; crash log 0; render_pipe unticked in the overlay: `[Ext] ext.render_pipe requested=0 active=0`, then `drawing on thread 172608 (the main thread ...)` |
| Shadow row under render_pipe (run E) | ticked: `[Ext] ext.max_shadow_res requested=1`, `[ShadowRes] sun map 4096x4096`, stats line 4096x4096, no hang (`e2_shadow_on.png`) |
| Write to settings.cfg (run E) | the five rows + `ao_quality` / `ao_opacity` / `foliage_aa_mode` recorded, `settings.cfg written`, the constants line logged; the file showed `ext.max_shadow_res=1`. settings.cfg backed up before the runs and restored byte-identical after (md5 `6392ed7f01b5483475cf55ebd7519fdb` before and after) |
| Faithful (`tools/build.sh run`, run D) | `[Ext] profile=faithful (command line /profile)`; the overlay opens, "faithful profile: nothing to tweak", controls disabled (a click on a row produced no `[Ext]` line); crash log 0 (`d1_faithful.png`). It showed ImGui's duplicate-id warning for the max_shadow_res checkbox drawn in two panels: fixed (per-panel id), re-verified in run E |
| Perf flavour, option OFF | builds; full md5 differs (`3c464f90...` main vs `576506c3...` branch) because the unstripped perf exe carries DWARF and a link timestamp. `objcopy --strip-debug` of both: **4 differing bytes**, the PE checksum (offsets 217-218) and the debug-directory time stamp (offsets 5405189-5405190: `6ad2863f` vs `6ad28bb9`, the link times); every non-debug section has the same size. Code and data are byte-identical; the two stamps are 1402 s apart, the gap between the two links, so any two links differ in those 4 bytes |
| Unit tests | `tools/build.sh unit debug`: **1226 tests, 0 failures** (the unit target has no overlay define; `ao_math.cpp` takes the `#else`) |
| Other invariants | `tools/castmetric.py src`: 2; `tools/acceptance/acceptance.sh`: 0 lines off target |

Not run: `tools/build.sh stress` (as asked). The game's own `BANISHED_SCREENSHOT_AT` capture was not used: it reads the
frame inside the game's draw, before `Present`, where the overlay is not drawn yet; the window's client area was
captured from the screen instead (what was presented, overlay included).

## 5. The SendInput recipe (nothing of it is in the tree)

`C:/GOG Games/Banished/tweakui/drive.ps1` (PowerShell, user32 by P/Invoke, by PID):
`drive.ps1 -ProcId <pid> toggle` = `SetForegroundWindow` + `keybd_event` Alt down (0x12), F10 down (0x79), F10 up,
Alt up; `click <x> <y>` = `ClientToScreen` + `SetCursorPos` + `mouse_event` left down / up (client coordinates);
`shot <png>` = `Graphics.CopyFromScreen` of the client rect; `rect`. The foreground took from a background shell
(`foreground=True` every time). Run: launch with `BANISHED_AUTOSTART=1 BANISHED_VITALS=1` (no `/profile`, so settings.cfg's rows apply; the census is off by default),
wait `tools/boot_state.sh --wait 300` and the first `[EntityCensus] frame=5` line, then drive. Client coordinates at
2560x1377 with the default layout: strength slider left end 38,410 (= 0), right end 350,410 (= 30); fill k 131,919
(about 0.28); Rows render_pipe checkbox 49,247; Shadow max_shadow_res checkbox 49,1073; Write to settings.cfg 150,1140.
Stop a run with `Stop-Process -Id <pid>`.

## 6. Notes

- `git diff --stat 57964ec3b..HEAD` holds **no game content**: our C++ sources and headers, CMakeLists, markdown, and the
  15 upstream Dear ImGui files + its license (only upstream ImGui files were added under `third_party/imgui`, unmodified,
  plus our README). ImGui's built-in font is ImGui's own (ProggyClean, in `imgui_draw.cpp`). The screen captures stay in the
  game directory, not the tree.
- The worktree needed `git submodule update --init third_party/SDL` (fetched from GitHub).
- The frame the toggle is pressed on, the game's keyboard poll (before the pump) still sees Alt+F10 held, and after closing
  it sees the keys still held; nothing in Banished is bound to Alt or F10 that we found harmful in the runs.
- While open the game's cursor hover (absolute position) still reaches its UI; clicks, keys, wheel and motion do not.
- Tweak-struct values are not persisted (no keys by design); copy them from the bottom lines or the
  `[TweakOverlay] constants:` log line into `ao_math.h` / `foliage_aa.h`.
