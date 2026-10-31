# FAA-SHADOW2 -- "Max shadow resolution" Off / 4096 / 8192 (`ext.max_shadow_size`)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/enhanced_tab/shadow_res_entry.cpp` moved to `src/ext/max_shadow_res/enhanced_entry.cpp`; `EnhancedTab::FindIn` is now `Ext::FindElement` (`src/ext/ui/find_element.h`).)

The user, 2026-10-16: "Max shadow resolution" should offer 4096 AND 8192, "a similar slider / combo". Block of
`docs/extensions/FOLIAGE_AA.md` §7, on FAA-SHADOW (`handins/FAA-SHADOW.md`) and FAA-MENU (`handins/FAA-MENU.md`).
Branch `worktree-agent-a4dd4ab9a1445f330` (from main `627330510`, main `3fb108562` merged before the final build);
commits prefixed `ext:`; **not merged**.

## 1. What shipped

| Item | Where |
|---|---|
| The key `ext.max_shadow_size` = 4096 \| 8192, default 4096, owner row `max_shadow_res` | `src/ext/registry.{h,cpp}`: `Values::max_shadow_size`, one `kValueKeys` row. `ValueKey` gained `choices` / `choiceCount` (null / 0 for the three range keys): a **discrete** key; `ClampValue` returns `v` when it is a choice, else the default. So `6000`, `8191`, `2048`, `16384` all read 4096 (counted as "clamped" in the `[Ext] ... valued key(s)` line), garbage reads 4096 (counted malformed). The faithful / all profiles leave it at 4096, as every valued key. |
| The engine seam | `SunLight::_extMaxShadowSize` (`const int32_t*`, beside `_extMaxShadowRes`, `src/engine/graphics/SunLight.h`), installed by `Ext::InitFromSettings` at `&Val().max_shadow_size` unless faithful. `SunMapRawSize(bool mayProbe)` (`sunlight.cpp`): row off or scalar != 0 -> retail `0x800`; row on at High -> `0x2000` when the key is 8192 and the device can hold it, else `0x1000`. **Medium / Low stay 1024 / 512 whatever the key says** (FAA-SHADOW's rule). |
| The 8192 fallback | 8192 x 8192 D24S8 = 256 MB. The dimension always fits: the device is created at FL 11_0 / 10_1 / 10_0 (`devicedata.cpp`), and the 2D limit is 8192 at FL 10_x, 16384 (`D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION`) at 11_0. The memory may not: the first time 8192 is asked for (`InitFields`, or a main-thread request), a throwaway depth `ImageBuffer` with the map's exact descriptor (`Create(0, 1, 7, 0x40, 8192, 8192, ...)`) is created and deleted through the engine's own `NewImageBuffer` / `DeleteImageBuffer`; its `Create` result decides for the process. On failure: ONE stderr line `[ShadowRes] an 8192x8192 sun map could not be created on this device: using 4096 (ext.max_shadow_size=8192 left as set)`, the map is built at 4096, settings are not rewritten. The per-frame call (render thread under `ext.render_pipe`) never probes. |
| A size change rebuilds the map on the main thread | `SetValueAndRecord` (`registry_boot.cpp`) calls `SunLight::ExtRequestMaxShadowResSync()` when `max_shadow_size` changed, exactly as `SetAndRecord` does for the row. No new raise site; nothing raises `Device::settingsChanged` off the main thread. |
| One choice for both UIs | `Ext::SetMaxShadowResAndRecord(size)` / `Ext::MaxShadowResChoice()` (`registry.h`, `registry_boot.cpp`): 0 = the row off (the key kept); 4096 / 8192 = the key first, then the row on (so a change is exactly one rebuild). |
| Enhanced-tab combo "Max shadow resolution" Off / 4096 / 8192 | `src/ext/enhanced_tab/shadow_res_entry.cpp`; the string table `shadowResItems` replaces `offOnItems` in `src/ext/ui/Dialog/EnhancedOptions.rsc`. Apply / OK record, Cancel writes nothing (AOMENU's persistence, unchanged). The Extensions dialog's checkbox is the bool row alone (`SetAndRecord`), so it mirrors the combo's Off and keeps the key (code path unchanged, not re-driven by mouse in this block). |
| Tweak overlay | `src/diag/win/tweak_overlay.cpp`: the Shadow panel's checkbox is now an ImGui combo Off / 4096 / 8192 (`Act::ShadowRes` -> `SetMaxShadowResAndRecord`, main thread), the line under it and the stats line show the built map size. `max_shadow_size` is in `kKeys` (Write records it, Reset puts it back to 4096). |
| `[ShadowRes]` line | now also prints `ext.max_shadow_size=<n>`. |
| Docs | FOLIAGE_AA.md §7 (the two sizes), `docs/reference/tweak_overlay.md` (the combo). |

No getenv, no new `BANISHED_*` flag, no new raise site.

## 2. Evidence (debug exe; game-dir lock `faashadow2` around every deploy + run, `assert-held` with the exe before reading logs; 0 `*.pkm`; settings.cfg backed up (md5 `bbaf48675f03ad16d07b06c797ac17f8`) and restored byte-identical (`cmp`) after every batch -- note the gate run itself writes `VideoSettings=true`, restored too)

**Gate** `tools/build.sh run` (`/profile:faithful`), before and after `git merge main`: in game (`[Vitals] frame=5`,
`[AutoStart] sequence fired`), crash log 0 bytes, no `[ShadowRes]` line (seams null), `[Ext] ... 4 valued key(s) (0 malformed, 0 clamped)`.
Unit: `tools/build.sh unit` **1229 tests, 0 failures** (three new: `ext_values_max_shadow_size_{table,parse,resolve_and_set}`, plus the
discrete-key shape checks in `ext_values_table_shape`).

**Boot, the user's settings with the three keys set per run** (every run in game, crash log 0):
```
row 0            scalar 0: sun map 2048x2048 (raw 2048, VideoShadowScalar=0, ext.max_shadow_res=0, ext.max_shadow_size=4096, texelSize 0.00048828)
row 1 size 4096  scalar 0: sun map 4096x4096 (raw 4096, ... ext.max_shadow_size=4096, texelSize 0.00024414)
row 1 size 4096  scalar 1: sun map 1024x1024 (raw 2048, ... texelSize 0.00097656)
row 1 size 4096  scalar 2: sun map 512x512   (raw 2048, ... texelSize 0.00195312)
row 1 size 8192  scalar 0: sun map 8192x8192 (raw 8192, ... ext.max_shadow_size=8192, texelSize 0.00012207)   (= 1/8192)
row 1 size 8192  scalar 1: sun map 1024x1024 (raw 2048, ... ext.max_shadow_size=8192, texelSize 0.00097656)
row 1 size 8192  scalar 2: sun map 512x512   (raw 2048, ... ext.max_shadow_size=8192, texelSize 0.00195312)
row 1 size 6000  scalar 0: sun map 4096x4096 (... ext.max_shadow_size=4096)   [Ext] 4 valued key(s) (0 malformed, 1 clamped)
```

**RenderDoc, in-game capture at 8192** (`LANE=faashadow2 tools/foliage_aa/run_arm.sh shadow2_8192 300 5 BANISHED_CAMERA_PIN=town,120`,
the user's settings + row 1 / 8192; the in-game file, the menu one kept as `_menu`; read with FAA-SHADOW's replay script, the MCP's
cbuffer reader still returns zeros): the only texture >= 4096 is `ResourceId::2634 8192 8192 D24S8_TYPELESS`; terrain draw 1131
binds it as `_e_shadowMap_` and `_PixelConstants_._pc_.texelSize = [0.0001220703125, ...]` (= 1/8192). The texel snap divides by the
same `Width()`.

**Live, Options > Enhanced combo, `ext.render_pipe=1`** (the user's settings, start 4096; recipe §4). Each Apply:
```
4096 -> 8192: [Ext] ext.max_shadow_size=8192 / [ShadowResMenu] Apply: item=2 choice=8192 / [ShadowRes] sun map 8192x8192
8192 -> Off : [Ext] ext.max_shadow_res requested=0 / Apply: item=0 choice=0 / sun map 2048x2048 (... ext.max_shadow_size=8192: kept)
Off  -> 8192: [Ext] ext.max_shadow_res requested=1 / Apply: item=2 choice=8192 / sun map 8192x8192
8192 -> 4096: [Ext] ext.max_shadow_size=4096 / sun map 4096x4096
4096 -> 8192, OK: sun map 8192x8192; settings.cfg ext.max_shadow_res=1, ext.max_shadow_size=8192
```
No hang (process responding after each, no `standalone_hang.log`), crash log 0, every `ResizeTrace` entry on one thread id (the main thread).
**Round trip**: the next run from that file built 8192 at boot and the tab opened on item 2 (`[ShadowResMenu] ... item=2 (choice 8192)`).
**Cancel**: 8192 -> Off, Cancel: no Apply line, settings.cfg md5 unchanged.

**Live, tweak overlay** (Alt+F10, debug exe, in game, `ext.render_pipe=1`, start 4096; recipe §4): 4096 -> 8192 -> Off -> 8192:
`[Ext] ext.max_shadow_size=8192` + `sun map 8192x8192`; `ext.max_shadow_res requested=0` + `sun map 2048x2048`;
`requested=1` + `sun map 8192x8192`. Stats line read `sun map 8192x8192`. No hang, crash log 0.

**Device lost at 8192** (`BANISHED_FORCE_DEVICE_LOST=12000`, row 1 / 8192): `[DeviceLost] device-lost round trip COMPLETE`, the game
kept running, crash log 0. The map comes back from the stored `0x2000` descriptor (no new `[ShadowRes]` line, as in FAA-SHADOW).

**Fallback** (Create fails at 8192): NOT exercised -- no unattended way to make the device refuse a 256 MB texture on this box (24 GB card).
The path is the engine's own `ImageBuffer::Create` return (`ImageBufferData::Create` returns `hr == 0`).

## 3. Memory (GPU dedicated, `\GPU Process Memory(pid_*)\Dedicated Usage`, ~25 s in game, scalar 0, the user's settings)

| arm | run 1 | run 2 |
|---|---|---|
| row off (2048) | 430.8 MB | 416.2 MB |
| 4096 | 485.5 MB | 490.6 MB (the `6000` run) |
| 8192 | 726.6 MB | 728.3 MB |

**8192 vs off: +304 MB** (format: 256 - 16 = +240 MB; the rest is driver placement, as FAA-SHADOW measured +58 for +48).
8192 vs 4096: +240 MB. Live, 8192 -> Off in one process: 716.8 -> 415.6 MB (-301 MB): the old map is released, the probe leaves
nothing behind (its companion is untracked and released by `~ImageBufferData`).

Device lost, NOT this block's: after a forced round trip the counter reads about double -- row off 820 MB (vs 423), 8192 1433 MB
(vs 727) -- i.e. the old device's whole working set still counted after the reset, with or without the row. Pre-existing; handed to
whoever owns device-lost memory (the DEVLOSTMEM block).

## 4. Recipes (nothing of them is in the tree; scratchpad `faashadow2/`)

- Boot arms: `runcfg.ps1 -Row 1 -Size 8192 -Scalar 0 -Tag x [-ExtraEnv "BANISHED_FORCE_DEVICE_LOST=12000"]` = settings.cfg rewritten from
  the backup with the three keys, `BANISHED_AUTOSTART=1` (and `BANISHED_VITALS=1`, off by default since the log cleanup), wait `[Vitals] frame=5`, hold, read the GPU counter and the `[ShadowRes]` /
  `[Ext]` / `[DeviceLost]` lines, `Stop-Process -Id`.
- Options combo: `launchopt.ps1` (`BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=900 BANISHED_PAUSEMENU_BUTTON=4`, `GameLastOption=4` opens
  Enhanced) + FAA-MENU's `ui.ps1` (SetCursorPos / mouse_event by PID). Client 2560x1377: shadow combo 1376,632; its list Off / 4096 /
  8192 at 1300,660 / 685 / 706; Apply 1340,906; OK 1460,906; Cancel 1221,906.
- Overlay: `launchopt.ps1 -Overlay` (in game) + TWEAKUI's `C:/GOG Games/Banished/tweakui/drive.ps1`: `toggle` (Alt+F10), the Shadow
  combo at client 190,1073, its list Off / 4096 / 8192 at 70,1109 / 1136 / 1161.

## 5. Content

`git diff --stat main HEAD` holds C++ sources and headers (`src/engine/graphics/{SunLight.h,sunlight.cpp}`, `src/ext/registry{.h,.cpp,_boot.cpp}`,
`src/ext/enhanced_tab/shadow_res_entry.cpp`, `src/diag/win/tweak_overlay.cpp`), one unit test file, our own `.rsc` (three strings:
"Off", "4096", "8192") and markdown. **No game content**: no retail shader, DXBC, texture, sprite, `.rsc` copy or capture. The capture
stays at `C:/GOG Games/Banished/rdoc_foliage/shadow2_8192.rdc`. Not run: `tools/build.sh stress` (as instructed). The worktree needed
`third_party/SDL` copied from the main checkout (untracked; its `.git` file removed so git works).
