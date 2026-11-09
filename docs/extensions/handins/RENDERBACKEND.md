# RENDERBACKEND -- one Windows exe with DirectX 11 and Vulkan, switched in game

Branch `feat/dual-backend`. Row `ext.render_backend` (class A) with the value key `ext.render_api`
(0 = DirectX 11, 1 = Vulkan). Build: `BANISHED_RENDER_BACKEND=dx11+vulkan tools/build.sh build`
(build dir `build/cmake-<flavour>-dx11+vulkan`). A one-backend build is unchanged and ignores the row.

## Maintainer rulings (2026-11-08)

1. **A shared function-pointer table in the render glue is approved** (`render_backend_table.h`).
2. **Vulkan is the default on future installs.** This is a deliberate exception to "extension rows
   default off":
   - A fresh install starts on Vulkan. `Ext::RenderBackend::SeedFreshInstall` (`src/app/main.cpp`,
     before `Engine::Startup`) writes `ext.render_backend=1` and `ext.render_api=1` when there is no
     `settings.cfg`.
   - **A retail registry import counts as a fresh install** (a retail player's first OpenBA run). The
     file is written as the imported set plus the two keys, and the engine's `ReadSettings` reads that
     file instead of importing again. An import that already names `ext.render_backend` is left alone.
   - An existing `settings.cfg` that lacks the keys keeps DirectX 11: the table default is off. Current
     OpenBA players are never switched silently.
   - The DirectX 11 fallback on a failed Vulkan start is on in every case.
3. **A fallback is saved.** When the wanted backend fails and another one starts,
   `Ext::RenderBackend::SaveFallback` writes the backend that started to `ext.render_api` (and turns the
   row on) and flushes `settings.cfg`, so the next launch starts on the working one. It runs after every
   successful `Device::Startup`: the boot (`game_main.cpp`, after `Ext::InitFromSettings`), the Options
   dialog's full restart and `SwitchNow`. Nothing is recorded in the faithful profile.
   - The registry row's comment (`src/ext/registry.cpp`) and `tests/unit/ext_registry_test.cpp`
     (class A default-on exception list) both name this ruling.
   - The faithful profile turns every row off, so `/profile:faithful` (and `tools/build.sh run`)
     always runs DirectX 11, retail's renderer.

## Retail findings (Runtime-x64.dll / exe)

| Address | Function | Role |
|---|---|---|
| 0x140071e10 | OptionsDialog::ApplyVideoSettings | the swap path when the renderer or adapter changed |
| 0x1800dc5e0 | MainLoop::OnVideoError | destructs External video resources (10 types), the context, the SaveLoad thumbnails |
| 0x1800f80e0 | Device::ReleaseDynamic | deletes companions |
| 0x1800f83f0 (thunk 0x1800f7280) | Device::Shutdown | |
| 0x1800f7270 -> 0x1800f8260 | Device::Startup -> PlatformStartup | fallback loop: the configured index first, then down with a wrap (1 -> 0 -> 2) |
| 0x1800f80b0 | Device::RecreateDynamic | rebuilds companions through the current DeviceData |
| 0x1800dc3b0 | MainLoop::OnVideoRestart | reloads from file |
| 0x1800f7930 | CreateVideoInterface | sets `activeInterface` (+0xF8) before createDeviceData |
| 0x1800f8610 | EnumerateVideoModes | a temporary DeviceData per slot for EnumerateInterfaces |
| 0x1800f87b0 | ReleaseVideoModes | frees the slots only |
| 0x18000cb00 | NeedsRestart | returns +0x9C |
| 0x1800f52b0 | ShaderProgram::PlatformSerialize | data arms per `activeInterface`: 0, 1, 2 |

- `Device::dllSlots[3]` sits at +0xB0 (DX9 = 0, DX11 = 1, GL = 2, stride 0x18).
- The interface array sits at +0x40, and its index equals the slot index.
- Vulkan reads the DX11 data arm, so both backends live in **slot 1**. Switching is a backend
  change behind slot 1, not a slot change. The DX9 placeholder in slot 0 keeps the index invariant.

## Design

- Each backend's sources compile with `BANISHED_RENDER_IMPL_NS=impl_<name>`.
  `render_impl.h` turns that into an inline namespace around every backend-defined interface
  function, so the two definitions of `Render::DeviceName` (and so on) do not collide.
  In a one-backend build the macros are empty.
- Each backend fills a `Render::BackendTable` in its own `render_backend_<name>.cpp`
  (`render_backend_table_fill.h`, internal linkage). CMake generates `render_backends.inc`.
- `src/render/common/multi/render_backend_dispatch.cpp` forwards every public name to the active
  table. `src/render/common/single/` holds the one-backend answers for the backend-set API.
- Extension requests made while no device exists are recorded and replayed on `SelectBackend`. They
  are not broadcast, because a backend's request body reads its own DeviceData type.
- No backend names another's namespace. Neutral code stays in `src/render/common/`.
- The switch is retail's own restart order: OnVideoError, ReleaseDynamic, Shutdown, select,
  list the adapters again, Startup, RecreateDynamic, OnVideoRestart.
- The Options Video tab's Renderer combo lists one item per backend in slot 1's place.
- Fallback: `Video::Device::_extInterfaceRetry` (a class static: the layout is untouched) is
  consulted in PlatformStartup's create-failed branch. It marks the backend failed, selects the next
  one, lists the adapters again and retries slot 1. If no other backend is left, retail's step-down
  loop continues.

## Vulkan fix found by the switch test

On the second Vulkan device of a run, `pipelines.cpp` WarmMain -> BuildPipeline crashed in
nvoglv64.dll.

- **Cause:** shader entries a holder still references survived `DeviceData::Shutdown` in
  `g_shaders`, but their modules were destroyed with the device ("N shader module(s) still held").
  The next device found the dead modules by hash.
- **Fix:** `PipelinesShutdown` orphans those entries (out of the registry, module nulled).
  `ReleaseShader` erases only its own entry.
- **Scope:** Vulkan's own device-lost recovery has the same window.

## Fixes from the maintainer's hand test (2026-11-08)

1. **"DirectX 9" in the Renderer combo.** The interface list keeps retail's index == slot invariant with
   a placeholder in slot 0 ("DirectX 9" in the DX11 build, "Vulkan (compatibility slot)" in the Vulkan
   build; its CreateDevice always fails, `dx9_placeholder.cpp`). Retail lists DirectX 9 because it ships
   VideoDX9-x64.dll. The dual exe's combo (`OpenVideoRow`) now lists slot 1's backends only: DirectX 11
   and Vulkan. The one-backend builds still list their placeholder, as main does; hiding it there would
   change the faithful Options dialog and is left for a ruling.
2. **Vulkan -> DirectX 11 froze the picture.** Reproduced with the user's settings: the frame loop runs,
   every Present returns S_OK, and the window keeps the last Vulkan frame. The trigger is `VideoVSync=true`
   alone (the same run with VSync off stays live; the window size, `ext.render_pipe` and the extension set
   do not matter, and destroying the VkInstance at the switch does not help). After a Vulkan swap chain on
   a window, retail's blit-model DXGI swap chain (`DXGI_SWAP_EFFECT_DISCARD`) presented with a sync
   interval no longer reaches the screen. Fix: `Render::WindowPresentedByOtherBackend()` (neutral; the
   dispatcher records which backends made a device on the window, a one-backend build answers false), and
   the DX11 backend makes a flip-model chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) only then; its resize keeps
   two buffers for such a chain (retail resizes its blit chain to one). Every other DX11 start keeps
   retail's chain. Checked with window captures (`PrintWindow`, a hash every 2 s): the user's settings,
   DX11 -> Vulkan -> DX11 -> Vulkan -> DX11, the picture live in every phase.

3. **A switch while the window is minimised.** Neither backend makes a device on an iconic window (DX11's
   colour buffer is 0 x 0, Vulkan's surface has no pixels), so the restart ended with no device:
   `Device::deviceData` null after PlatformStartup's loop (the port's slot 0 is the never-starting
   placeholder where retail had a real DX9 plugin), and the restart tail crashed in
   `ImageBuffer::RecreateDynamic` (`dynamic_resources.cpp:265`, the null companion of a null DeviceData).
   `SwitchNow` now defers: while the window is minimised nothing is torn down and the switch is kept;
   `RunPendingSwitch` runs it once the window is restored. A restart that still starts no backend starts
   the one that was running again; if that fails too, retail's end for a device it cannot make again (the
   DX11 device-lost recovery): `System::Error::SetText` (stop) + `Display`, with no restart tail on no
   device. The Options dialog's Apply cannot be pressed on a minimised window, and its double failure is
   the fallback's (the other backend is the one that was running); both failing there runs retail's
   ApplyVideoSettings tail, as retail does. Harness: `failswitch=<backend>[+<backend>]` (in-game
   CreateDevice failures, the adapter listing skipped). Tested: maximise, minimise across the switch,
   restore (deferred, then run; the picture live); `failswitch=vulkan+dx11` (the running DX11 again, then
   a normal switch to Vulkan).

4. **Vulkan -> DirectX 11 -> Vulkan froze the picture with VSync off** (fix 2's reverse; fix 2 was
   checked with VSync on). Every `vkQueuePresentKHR` returns VK_SUCCESS and the frame counter runs, but
   the window keeps the last DX11 frame. The trigger is the flip-model DXGI chain fix 2 makes: on a
   window that has carried one, a Vulkan swap chain reaches the screen only in FIFO; MAILBOX (VSync off)
   and IMMEDIATE do not. The DXGI chain is gone by then (a private-data sentinel on it is released inside
   the switch), so no outstanding reference holds it, and a `ClearState` + `Flush` before the release
   changes nothing. A window that carried only retail's blit chain (DX11 boot -> Vulkan) shows MAILBOX.
   Fix: `Render::WindowTakenOverByOtherBackend()` (the dispatcher records the order of the devices on the
   window), and the Vulkan backend presents in FIFO with VSync off once it is true, one `[Vulkan] present
   mode: FIFO with VSync off` line per process. The cost is VSync-capped Vulkan until the next launch,
   only after such a round trip. Checked with window captures as above, VSync off: Vulkan -> DX11 ->
   Vulkan -> DX11 -> Vulkan and DX11 -> Vulkan (MAILBOX) -> DX11 -> Vulkan (FIFO), the picture live in
   every phase; on main 5ef295300 (perf build) the same route freezes.

## Tests (Windows, RTX 5080, 2026-11-08, rebased on main 250bbfee2)

| Test | Result |
|---|---|
| dual debug, boot DX11, town loaded, DX11 -> Vulkan -> DX11 -> Vulkan -> DX11 (`tools/dual_backend_check.sh dx11 "900:vulkan,1800:dx11,2700:vulkan,3600:dx11"`) | PASS, clean shutdown |
| dual debug, boot Vulkan with AO, Fancy clouds and water on (the shared depth copy), Vulkan -> DX11 -> Vulkan -> DX11 | PASS, clean shutdown; the depth copy is made, released and made again on each backend |
| forced Vulkan CreateDevice failure at boot (`failcreate=vulkan`) | DX11 fallback PASS; `fallback saved: ext.render_api=0`; the next plain boot wants and starts DX11 |
| fresh install over a retail registry import (`settings.cfg` moved aside, the retail registry present) | PASS: the file is written as the import (155 entries) plus the two keys, and the boot starts Vulkan |
| user's settings (VSync, MSAA 4x, AO High, borderless row, `render_pipe=0`), DX11 -> Vulkan -> DX11 -> Vulkan -> DX11, window captures | PASS after the flip-model fix (frozen before it) |
| Options Video tab, dual exe | 2 renderer items (DirectX 11, Vulkan) |
| fresh install with nothing to import | not run: it needs the retail registry key gone |
| probe-level failure (`Available()` false) | not run: the Vulkan loader ignores `VK_LOADER_DRIVERS_*` in an elevated shell |
| `tools/build.sh unit` (DX11) | 2297 ok, 0 FAIL |
| `tools/build.sh unit` (dx11+vulkan) | 2328 ok, 0 FAIL |
| DX11 gate `tools/build.sh run` | PASS (in game, no crash) |
| Vulkan `run-menu` (one-backend build) | PASS |
| `tools/acceptance/acceptance.sh` | 0 lines off target |
| Metal | not compiled (no Mac); its macros are empty and `single/` is compiled |

The shared depth copy (#278) adds no backend call to neutral code (`depth_copy_backend.h` is inline), so
the table needed no new entry. DX11's copy is remade when the device pointer changes (`EnsureDevice`);
Vulkan's `DepthCopy::Shutdown` runs with the device.

Stripped perf exe: dual 16.26 MB, DX11-only 13.44 MB, Vulkan-only 14.90 MB. The dual perf build
took 350 s at -j3.

## Accepted as they are (maintainer, 2026-11-08)

- `failcreate=` (the harness `BANISHED_RENDER_SWITCH`) injects at boot only.
- The tweak overlay is off in the dual build: its default needs a DX11-only build.
- After a switch, the Options combo re-reads the saved fields and lists the adapters again. A second
  switch in the same dialog session has not been driven by hand.
- The 7-8 shader entries still held at a Vulkan shutdown are not identified (harmless since the
  orphaning fix).

## Open

- CI has no dual configure yet. A Windows `dx11+vulkan` configure + unit would cover the table.
