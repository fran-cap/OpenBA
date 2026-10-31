# The debug tweak overlay (Alt+F10)

A Dear ImGui panel over the game for live tuning of the graphics extensions, without the Options dialog
round trip. **Debug flavour only** (`tools/build.sh build debug`; CMake option `BANISHED_TWEAK_OVERLAY`, ON for
debug on Windows, OFF for og / perf / release and Linux, where none of it is compiled). Code:
`src/diag/tweak_overlay.h` + `src/diag/win/tweak_overlay.cpp`, `src/ext/tweak/ext_tweaks.{h,cpp}`; library: `third_party/imgui` (v1.92.9b).
Hand-in: `docs/extensions/handins/TWEAKUI.md`.

## Opening it

- **Alt+F10** (either Alt) toggles it, in the menu or in game. The combo is the `#define`s
  `TWEAK_OVERLAY_KEY` / `TWEAK_OVERLAY_KEY_MOD` in `tweak_overlay.h` (no settings key, no environment flag).
- While it is open it owns the mouse and the keyboard: the game sees every key and button released and no
  motion or wheel (the camera does not move, clicks do not reach the world or the game UI). Close it to play.
- It never writes `imgui.ini`; the window starts at the top left each run.
- Under `/profile:faithful` (the gate) it shows **"faithful profile: nothing to tweak"** and every control is
  disabled.
- Log lines: `[TweakOverlay] ...` in `standalone_error.log` (open / closed, the DX11 backend's device, the thread
  it draws on, the settings write, the constants).

## The panels

| Panel | Control | Goes through |
|---|---|---|
| stats line | fps, frame ms (ImGui's rolling average), the sun map size of the last `[ShadowRes]` line, FAA generation / mode / blobs transformed / binds | read only |
| Rows | `ambient_occlusion`, `better_trees`, `better_water`, `max_shadow_res`, `render_pipe`, `eye_camera` | `Ext::SetAndRecord` (the Extensions dialog's path: the Sync hooks, the shadow rebuild request) |
| Better water | open by default (the other sections below start collapsed): overall opacity; the wet bed's strength, shade, rise, band width (holds to depth) and end; the line's plain and bend opacity, fade-in, fade-out from / to; the bend ring's radius and land share from / to; the waterline's wander and its scale; **water defaults**; a line of the set to copy into `src/ext/better_water/shore_params.h` | `Ext::Tweak::Water` (`Ext::BetterWater::ShoreParams`), read by `DrawShore` every water range (BETTERWATER.md §4) |
| Ambient occlusion | quality 1..3 (`ext.ao_quality`), strength 0..30 (`ext.ao_opacity`) | `Ext::SetValueAndRecord` (read by `Ext::AO::Current()` every frame) |
| | radius street / overview, shadow multiplier, shadow power, horizon angle threshold, blur passes (-1 = the tier's), sharpness, detail shadow strength | `Ext::Tweak::Ao`, read by `AoMath::FrameFor` the next frame |
| | fade with the fog, fog fade from / to (x the fog's near / far), fade ceiling | `Ext::Tweak::Ao`, read by `AoMath::FadeFromFog` in the AO pass |
| Foliage | mode 0 / 1 / 2 | `Ext::BetterTrees::Set` (0 turns the row off, as the Options combo does) |
| | fill k, fill floor (applied on release) | `Ext::Tweak::Faa`, then `Ext::BetterTrees::Sync` -> `FaaBackend::Request`: a change re-creates the 24 transformed shaders (`[FoliageAA] request ... stage gen N`) |
| | (no fwidth clamp: the rewriter's `1e-4` has no hook) | |
| Billboard shadow | the `better_trees` row, plus five sliders, each applied on release (the first four snapped to 0.05): **basis** 0..1 (0 = P's depth, retail; 1 = the card centre's), **margin k** -1..1.5 (the mean offset toward the sun, in half extents), **jitter** 0..1.5 (the per-card spread of the cut), **noise** 0..1.5 (the per-texel edge dapple, in half extents), **noise scale** 8..256 snapped to 8 (cells per atlas uv unit); **default** (`BbsRewrite::kDefaultParams`) and **retail (0/0/0/0)** buttons; a stage line with the set the current copies were made with (TS-TWEAK, TS-DAPPLE) | `Ext::Tweak::Bbs`, then `Ext::BetterTrees::Sync` -> `BbsBackend::RequestParams`: the next vertex bind releases the rewritten copies (`[BillboardShadow] params ... -> ...: copies re-created`) and re-makes them with the new immediates, once per program |
| Shadow | max shadow resolution combo Off / 4096 / 8192 (the Enhanced tab's choice: Off = the `max_shadow_res` row off, 4096 / 8192 = `ext.max_shadow_size` + the row on), the sun map size (High only; Medium / Low stay 1024 / 512) | `Ext::SetMaxShadowResAndRecord` -> `SetValueAndRecord` / `SetAndRecord` -> `SunLight::ExtRequestMaxShadowResSync` (main thread) |
| buttons | **Write to settings.cfg**: records the five rows and the four valued keys and flushes `settings.cfg`; logs the tweak-struct constants | `Ext::PersistSettings` |
| | **Reset defaults**: the tweak structs and `ao_quality` / `ao_opacity` / `foliage_aa_mode` / `max_shadow_size` back to their defaults (rows untouched; with the row on, the size going back to 4096 rebuilds the map) | |
| bottom lines | the tweak-struct values that have no settings key, to copy into `ao_math.h` / `better_trees.h` / `receiver_rewrite.h` (`BbsRewrite::kMargin`) | |

A row or value change is recorded into the settings map at once (as the dialogs do) but only reaches
`settings.cfg` on the button, on the game's own settings write, or never (a killed run). Tweak-struct values
are not persisted at all: copy them into the constants.

## Threads

The panels are built and drawn on the thread that presents: the main thread on the direct path
(`ContextData::Finish`), the render thread under `ext.render_pipe` (`RenderPipe::FinishPresent`), right before
`Present`, the device locked. The pump (main thread) only queues SDL events for it; the panels only queue
changes, which the pump applies on the main thread at its next call (`MainThreadTick`). The tweak structs are
copied under a mutex each way. The DX11 backend lets go of the device in `DeviceData::Shutdown` and re-inits on
the next open frame (device lost, a settings rebuild).

## Adding a tweakable

1. A value with a settings key (`registry.cpp` `kValueKeys`): add a control in `DrawPanels`
   (`tweak_overlay.cpp`) that calls `Queue(Act::Value, "<id>", v)`; add the id to `kKeys` if the Write button
   should record it. Nothing else: the extension already reads `Ext::Val()`.
2. A constant with no key: name it as a `constexpr` in the extension's header if it is a literal; add a field
   to the extension's struct in `src/ext/tweak/ext_tweaks.h` defaulting to that constant; at the read site put
   `#if BANISHED_TWEAK_OVERLAY` (read `Ext::Tweak::...Values()`) `#else` (the constant, unchanged) `#endif`; add
   the control (edit `g_uiAo` / `g_uiFaa` or a new copy, then `Ext::Tweak::Set...`). If the consumer only
   picks the value up through a request (as foliage k does), queue an action that calls the extension's own
   Sync on the main thread.
3. A new row: add its id to `kRows`.
4. Every site outside `src/diag/tweak_overlay.*` and `src/ext/tweak/` stays inside `#if BANISHED_TWEAK_OVERLAY`,
   so the perf / release exes keep their code and data byte-identical (check: `objcopy --strip-debug` both perf
   exes, the only differing bytes are the PE checksum and the build timestamp).
