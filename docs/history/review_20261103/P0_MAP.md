# hotspots

## 1. Hotspot ranking

**Method.** A module is `src/<area>/<module>`. Files placed directly under `src/<area>/` are grouped as `src/<area>/(root)`. The table covers 124 modules and 597,504 LOC (counting .cpp/.h/.mm/.c/.glsl/.hlsl/.metal/.cmake).
- **Commits:** `git log --since=2026-08-05 --no-merges` gives 6,254 non-merge commits.
- **Bug-fix commits:** a commit counts if its subject starts with `fix` or `behaviour:`, or contains crash, race, deadlock, null, stale, leak, overflow, torn, uninit, dangling or wrong. 1,131 of the 6,254 match.
- **KPI and RB:** `docs/known_port_issues.md` and `docs/retail_bugs.md` were split into entries, one per heading or table row; the KPI file has 392 numbered table rows and the RB file 28 sections. Each entry is credited to every module it names, either by a `src/...` path or by a bare file name that maps to only one module.

**Score = 2·bugfix + 0.5·commits + 3·KPI + 3·RB + LOC/2000**

| # | Module | LOC | Commits (90 d) | Bug-fix | KPI | RB | Score |
|---|---|---|---|---|---|---|---|
| 1 | src/game/Component | 71,222 | 148 | 91 | 132 | 8 | 712 |
| 2 | src/game/Game | 38,188 | 165 | 52 | 96 | 4 | 506 |
| 3 | src/game/Dialog | 36,584 | 90 | 34 | 62 | 4 | 329 |
| 4 | src/ext/(root) (registry*, boot) | 2,675 | 142 | 41 | 1 | 1 | 160 |
| 5 | src/game/Action | 15,768 | 36 | 13 | 32 | 3 | 157 |
| 6 | src/game/Player | 13,881 | 27 | 13 | 29 | 2 | 139 |
| 7 | src/diag/(root) | 19,926 | 94 | 11 | 13 | 4 | 130 |
| 8 | src/game/Work | 25,531 | 22 | 13 | 23 | 2 | 125 |
| 9 | src/game/Entity | 4,970 | 44 | 9 | 20 | 0 | 102 |
| 10 | src/ext/resources | 29,970 | 81 | 22 | 1 | 0 | 102 |
| 11 | src/engine/io | 23,597 | 43 | 6 | 14 | 2 | 93 |
| 12 | src/ext/ui (.rsc mostly, 64 code LOC) | 64 | 100 | 21 | 0 | 0 | 92 |
| 13 | src/engine/ui | 45,638 | 32 | 9 | 6 | 4 | 87 |
| 14 | src/platform/common | 4,606 | 72 | 20 | 2 | 0 | 84 |
| 15 | src/platform/win | 1,940 | 72 | 20 | 2 | 0 | 83 |
| 16 | src/render/dx11 | 15,066 | 83 | 12 | 3 | 0 | 82 |
| 17 | src/app/(root) | 3,438 | 62 | 16 | 3 | 1 | 77 |
| 18 | src/engine/graphics | 31,323 | 46 | 4 | 6 | 0 | 65 |
| 19 | src/engine/engine | 7,171 | 35 | 8 | 6 | 1 | 58 |
| 20 | src/platform/sdl | 1,980 | 21 | 12 | 6 | 0 | 53 |
| 21 | src/render/common | 35,482 | 43 | 2 | 2 | 1 | 52 |
| 22 | src/render/vulkan | 15,446 | 50 | 1 | 2 | 0 | 41 |
| 23 | src/platform/posix | 2,044 | 36 | 7 | 2 | 0 | 39 |
| 24 | src/game/Tutorial | 6,313 | 5 | 1 | 10 | 0 | 38 |
| 25 | src/engine/object | 11,792 | 28 | 1 | 4 | 1 | 37 |
| 26 | src/engine/audio | 7,367 | 24 | 6 | 3 | 0 | 37 |
| 27 | src/ext/phone_menu | 4,145 | 48 | 3 | 1 | 0 | 35 |
| 28 | src/engine/collection | 12,642 | 11 | 1 | 7 | 0 | 35 |
| 29 | src/engine/system | 9,261 | 32 | 2 | 2 | 0 | 31 |
| 30 | src/engine/save | 5,862 | 17 | 5 | 2 | 1 | 30 |

Next below the cut: engine/video 28, ext/derived_cache 25 (8 of its 16 commits are bug fixes, the highest share of any module), platform/android 19, ext/render_pipe 19, game/Goal 19, ext/stranded_wade 13.

**Notes**
- **Bug density per KLOC:** game/Entity 1.8, game/Component 1.3, game/Game 1.4, platform/common 4.3, platform/win 10.3, platform/sdl 6.1, ext/derived_cache 4.8.
- **Least churned:** the large engine modules engine/ui, engine/graphics and render/common each sit under 0.3 bug fixes per KLOC.
- **Path drift:** some older KPI rows name the pre-move `code/Game/...` paths. Those resolve only when a bare file name maps to a single module, so KPI counts for game/* are lower bounds.
- **Regex catches faithfulness commits:** in the Oct 9–11 B6/B8 waves, "behaviour: … drop the null test" commits match the keyword regex. game/Component and game/Game are inflated by about 40–60 such guard-removal commits, which are classed separately below as class 6.

## 2. Bug-class taxonomy

About 85 commits were read: subjects for all of them, plus `--stat` and bodies or diffs for about 40. They form 17 classes.

### C1. Side table outlives the entity it indexes (stale id or pointer after remove)
- ddd862312: ext.render_culling deferred pick/model/icon entries outlived `OnRemove`, and a flush without a sim tick wrote through a freed pick entry. The fix adds `ForgetPick/ForgetModel/ForgetIcon` in pickingcomponent.cpp, modelcomponent.cpp and statusiconcomponent.cpp.
- e319c926c: ext.family_tree citizen-button pointers were forgotten only when a panel closed, not when the row went away.
- f9178b6a0: ext.terrain_edit's refused-undo flag outlived its world; `ForgetWorld` now clears it.
- a4ba1f7ed / KPI §13: TradeWork::Validate walked a stale entry of the post's citizen tree.
- **Grep:** any `src/ext/*` cache keyed by entity id, pointer or scene id (`std::unordered_map<.*Entity|uint32_t id`), cross-checked against the 98 `OnRemove(` definitions in 84 files. Only 5 files carry `Forget*`. Also `ForgetWorld` / world-changed handlers.
- **Modules:** ext/render_culling, ext/family_tree, ext/terrain_edit, game/Component, game/Work.

### C2. Port-only mode flag or state that retail does not have
- 033dbca08: the global `g_wpcRosterOnly`, held across SetWorkplace, made Attach stop early, leaving a stale "no workers" icon.
- 8342d8678: the in-memory `BuildComponent_DemoGateSkip` table was not saved, so a site loaded mid-construction skipped State8Finalize.
- 958dc143a: a non-retail `_loadBit = -1` store in ~MenuDialogLoopT caused a use-after-free on remount (rows 1295/1333).
- 2683ea16b / 3b4f6c9fc: port-only null branches.
- **Grep:** `g_[A-Za-z]*(Only|Skip|Hold|Gate)\b` (42 hits in 18 files: g_wpcRosterOnly 29, g_altSkip 11, g_uiHold 5); RAII guards that toggle a global; `static std::(set|map)` in src/game that are not serialized.
- **Open leads:** `g_wpcRosterOnly` is still held at citizen_employ_cascade.cpp:465-469 (RosterGuard), citizen_reassign_workers.cpp:241-308 and buildcomponent_state8.cpp:75.
- **Modules:** game/Component, game/Game, game/Dialog.

### C3. State-machine table mismatches (handler, return value, gate)
- 90482e4c8: ClearArea state 0x15 was registered with null handlers, so crop fields planted only once.
- 9a2539420: `State0PerFrame` returned 1 where retail returns 0, so parent controllers (Absorb) never ticked.
- 1df0c62a3: ClearArea phase 2 must return 2.
- fe4f33984: State0Enter carried an extra `CountRemaining()==0` test, so Remove Roads never worked.
- 4c5e94edc: Boat State3 gated on a constant length, so boats parked forever.
- **Grep:** `TableRegisterState\(t, *\w+, *nullptr, *nullptr, *nullptr, *nullptr\)`. Two remain: buildcomponent.cpp:306 (state 2) and destroycomponent_states.cpp:999 (state 7); check both against retail's populator. Also the `return` values in `*PerFrame` bodies (42 hits in 9 files), and `Enter` bodies with compound finish tests.
- **Modules:** game/Component (clearareacomponent_states.cpp, boatcomponent_states.cpp, buildcomponent*).

### C4. Retail body never wired: missing slot override or missing call
- 9d22dc510: EntityUI slot 8 ran OnUpdateLayout a second time instead of the select body.
- fb3a9a2d7: PickingComponent slot 11 was the base no-op, so nothing was pickable after a load.
- 0c7842662: NaturalResourceComponent slot 8 was missing.
- f92bb18af: GrowthComponent::OnRemove was missing.
- cb9e9fe25: RoadTool::CalcPath did not call `RebuildRoadMesh`.
- 08f8e6ba8: StorageLocation OnRemove was missing.
- **Grep:** class headers whose slot count is below retail's; slot overrides that forward to another slot of the same object; post-load passes (`0x1400c4730`) reaching only some components. Diff each `Tool::CalcPath` / `On*` body's call list against the Ghidra callee list.
- **Modules:** game/Dialog, game/Component, game/Player.

### C5. Wrong element or wrong type picked (index, component, parent)
- 1142711b6: the Event Log used child `i` instead of `2*i` / `2*i+1` in a 2-column table, and had a wrong early-exit test.
- 015266447: the Town Hall Inventory tested `StateMachineComponent` instead of `ResidenceComponent`, so every building counted as a home.
- caab45cf2: CitizenUI skipped the `textNameIs` parameter.
- d331e9858: the DialogInstance was looked up on a child; it lives only on the root.
- **Grep:** `FindComponent<StateMachineComponent>` used as a type test (57 hits in 25 files); child-index loops over `.rsc` tables (`GetChild*(` inside `for`); `SetParameter(` call counts compared with retail; `GetDialogInstance` on non-root elements.
- **Modules:** game/Dialog, ext/enhanced_tab.

### C6. Port-only guard or cap that skips retail behaviour (the inverse class: faithfulness drift)
- 55e77520f, 8aca9ff11, 6e56d0740: guards retail lacks were dropped from StorageUI/EntityUI, Immigration and TradeComponent.
- ad242fb09: the worldgen 100000-try and 1e6 caps were removed.
- 639605797, 059334076, 642418302: unguarded divisions restored, as retail.
- dda5a5e8f: CitizenComponent::OnRemove ring-walk breaks dropped.
- **Grep:** `IsDerivedFromNullSafe` (652 hits: 231 in game/Dialog, 108 Component, 56 Work, 40 Action); `if \(!\w+\) return` at body entry; `if \(\w+ (==|<=) 0\.?0?f?\)` before a division; `kMaxTries|100000` (52 hits in src/game).
- **Modules:** all of src/game. This is the largest commit population (about 120 commits in Oct 9–11 and Oct 30).

### C7. Wrong width or overload in serialize and constant reads
- 5a8dd770b: the EventLog colour went through `Serialize(int&)` by cast where retail uses `Serialize(unsigned int&)`.
- f4fda68f8: the same for `_entityFlags`.
- ccd12b63f: the state bytes take `Serialize(signed char&)`.
- 086c29946 (KPI §1): the double fabs mask cell was read as `uint32`, so `abs` was a no-op.
- KPI §5: door typedef width and sign mismatches.
- **Grep:** `reinterpret_cast<(int|unsigned|uint32_t|short|char)&>` (2 remaining in 1 file); `static_cast<uint32_t>\(kRdata_`; `Serialize\(reinterpret_cast`; `kRdata_\w+` readers whose width differs from the registered constant.
- **Modules:** game/Game, game/Entity, game/Component, engine/save.

### C8. Integer offset overflow (32-bit)
- 74faba8e2: package offsets are int32/uint32, so packages over 2 GiB crash (retail_bugs §9, fix.large_packages).
- e319c926c: `Out::Add` truncated lines at 1024 bytes.
- 9ba8c7f72: decode_ahead accepted a block larger than its window.
- **Grep:** `SetFilePointer\(|uint32_t .*[Oo]ffset|int32_t baseOffset`; fixed `char buf\[1024\]` paired with `snprintf` whose return value is ignored.
- **Modules:** engine/io, ext/large_packages, ext/decode_ahead, ext/family_tree.

### C9. Cached render state not invalidated or not restored
- 787df69c4: after a resize, `ClearState()` reset the topology to UNDEFINED behind ContextData's topology cache (garbled screen).
- e565c4b2a: the `SetScissor(saved)` round trip scaled and truncated the rect, leaving a 2 px-short scissor.
- 228bfc24b: ASSAO's state backup released the RTVs but never the DSV, leaking about 69 MB of MSAA depth per toggle.
- 3af714e12: the clear-state generation made atomic.
- **Grep:** `ClearState\(\)` (11 hits in 4 files) cross-checked against every `if \(\w+ != cached` / skip-if-same cache in render/dx11 and render/vulkan; `SetScissor\(` used as a restore (24 hits in 13 files); `OMGetRenderTargets|Get.*\(&` backups without a matching `Release()` on every out-param.
- **Modules:** render/dx11, render/dx11/ao, engine/ui (userinterface_draw.cpp).

### C10. Device-lost and reset paths
- 5e7fef9ac: a failed reset on a removed device never reached the recovery; it now runs `RecoverLostDevice`.
- 81463507a: the menu scene loaded during a pending reset and hit a null `colorBuffer`.
- f1192d836: fix.boot_device_lost; a device loss during boot walked a SaveLoad singleton that did not exist yet (retail_bugs §10).
- 6d5bb9f43: ext.create_on_demand published objects created on a dead device.
- e35a4587c: background_save waits at device lost.
- **Grep:** `GetDeviceRemovedReason` (12 hits in 4 files); `DeviceData_Resize\(` return values ignored; singleton walks (`SaveLoad::|_singleton->`) reachable from `OnVideoError` / `OnVideoRestart`; async creates without a removed check after `Create*`.
- **Modules:** render/dx11, engine/engine (mainloop_video.cpp), game/Game (splash_loop_stubs.cpp), ext/create_on_demand.

### C11. Lost wake-up, self-wait and cross-thread deadlock
- 97233ff0e, f48bcddc0, 54cec0083: derived_cache `Flush` waited for `!_usedDirty` while the writer had dropped the request (macOS CI hang).
- 744e4345d, 32250bb88: the sun-map descriptor was rewritten from the render thread or a window move, deadlocking render_pipe.
- 1c3f6efc7: the decode_ahead loader waited on its own queued block.
- fbc49674d: a gather_threads helper spun without bound after a slept-through Park.
- 90c013c5d (retail): the synchronous path query waited at the tail of a FIFO of 1000+ jobs (15–20 s freeze).
- **Grep:** `\.wait\(lk|_cv\.wait\(` (40 hits in 14 files), checking whether the predicate's request flag can be cleared by the other side; a thread that enqueues and then blocks on the same queue (`Submit.*;.*Wait`); main-thread mutation of render-thread-owned objects (SunLight, descriptors) under `ext.render_pipe`.
- **Modules:** ext/derived_cache, ext/render_pipe, ext/decode_ahead, ext/gather_threads, engine/graphics (sunlight.cpp), game/Action (pathfinder).

### C12. Ownership and lifetime across threads (join, free, splice)
- 9cef01ce1: the background_save destructor's join touched the freed SaveLoad.
- 7ea0ee752: share_gpu_data raced equal creates and kept the loser's copy.
- c76561942: Present held the device lock and stalled loader threads.
- 4551ceef9: a type dump spliced with loader-thread TRACE lines.
- 087901674: an EyeCamera hook ran from the menu loop with a null `m_toolManager` (every render_pipe run crashed).
- **Grep:** `std::thread` members joined in destructors; `Insert` on a shared table whose refusal path keeps the local object; hooks called from both the menu loop and the game loop without an `in.gameLoop` gate.
- **Modules:** ext/background_save, render/dx11/share_gpu_data, render/dx11 (buffers.cpp), ext/render_pipe, diag.

### C13. Arena or snapshot layout assumption
- ddd862312 (second half): render_pipe copied decal/road batches "right after the WorldGeometry arrays" instead of at the offset `MarkBatch` reserved, overwriting later entries (row 1347).
- 7e23be243: depth discard re-armed by frame signature.
- **Grep:** in `src/ext/render_pipe/snapshot.cpp`, any `DeepCopy\(.*, at\)` where `at` is a running cursor rather than a recorded offset; reserve-then-copy pairs whose two passes walk in a different order.
- **Modules:** ext/render_pipe, render/vulkan.

### C14. Order of frame steps (one-frame artefacts)
- 560222fbc: UI Update (step 9) ran before tool Update (step 10), so the outline drew one frame before the panel (fix.outline_with_panel; retail_bugs §16).
- cbd69fd83: a panel moved after layout drifted (§13).
- 731c762a5: a dialog's first frame was not its final size.
- RB §1: status icons slide in from the origin.
- **Grep:** the step order in `GameUpdateLoop::Update`; position reads before `OnUpdateLayout`; `scene id -1` / origin defaults in component constructors.
- **Modules:** game/Game, engine/ui, ext/phone_menu, game/Component (statusicon).

### C15. Transform applied twice (UI scale)
- f5cf2c2ac: CreateBar's rect was already scaled, then `SetUpperLeft(.., true)` scaled it again.
- d19dda8a8: a modal shown through `ShowDialog` missed the UI scale.
- 9d07ed354: the render scale resized the interface.
- e565c4b2a: also C9.
- **Grep:** `SetUpperLeft\([^)]*, *true\)` and `SetPosition.*true` fed by values already multiplied by `_uiScale` / `GetScale()`; any `\* *_uiScale` appearing twice on one data path.
- **Modules:** game/Player (tool_base_createbar.cpp), engine/ui, ext/touch_mode, ext/phone_menu.

### C16. Numeric and ordering divergence from MSVC (determinism)
- 307bc288d / e40ff4f8d: libm `sinf/cosf` replaced by MsvcLibm.
- 795f3d9a0, 290dcbc89, 14220ebaf: the CRT qsort skeleton replaces std::sort.
- 07c4799e9: equal scores pop the lowest index.
- 424338be9, 42b8f672a, 7ebd7417e: frustum NaN dN must still take the dP test (scalar and NEON).
- 2c4f1dcc9: allocator ties broke by address (fix.alloc_tiebreak; RB §14).
- **Grep:** `std::sort\(|std::stable_sort\(` (38 hits in 24 files); bare `\b(sinf|cosf|powf|atan2f)\(` (31 hits in 18 files). The game-side hits are worldgen.cpp, worldgen_f390_erosion.cpp, worldgen_f390_rivers.cpp and tutorialhighlightdialog.cpp; check each against MsvcLibm. Also float compares that NaN silently fails (`if \(d[NP] [<>]`), and pointer-valued sort keys.
- **Modules:** game/Game (worldgen), game/Component (allocators), engine/graphics (frustum), platform NEON.

### C17. Platform or toolchain seam left empty
- 0abc58b9d: an `#if defined(_WIN32)` around the allocator install left the POSIX seam empty (EXEC of 0); PE `.data$exetype_*` grouped sections do not exist on ELF; an event-lifetime defect also surfaced on the first Linux boots.
- 875b0a9cb: ThinLTO `--gc-sections` dropped the type records (needs `retain`); clang hoisted a field load above a null-`this` test (now `-fno-delete-null-pointer-checks`).
- d798d29b0: no `_countof` on glibc.
- **Grep:** `#if(def)? *(defined\()?_WIN32` in app/game/engine. Eight files remain: app/main.cpp, app/runtime_init.cpp, app/video_interface.cpp, game/Object/exe_type.h, game/Object/factory_registration.cpp, engine/system/allocator.cpp, engine/system/error.cpp, engine/system/MemoryAllocator.h. Also `__attribute__\(\(used\)\)` without `retain`, and `this == nullptr` / null-`this` callers (`UI::String{null,-1}`).
- **Modules:** engine/system, game/Object, app, platform/posix, platform/android.

### C18. Retail logic bugs fixed as `fix.*` rows (wrong sentinel, missing check)
These are retail behaviour, not port drift, and they are fixed as `fix.*` rows under src/ext.
- f9425650e: RegionPairCost's not-found arm returned 0, so an unmeasured commute counted as free (RB §6).
- 4a3ee9210: an unresolved seed resolve appended null to the agriculture list (RB §21).
- 9cfdf0580: an input fetch ignored the room left (RB §25).
- 62c200161: auto-trade paid with goods priced 0 (RB §24).
- 9e560ec1e: a cancelled site lost most of its delivered materials (RB §26).
- e50668b1f: a mod's Options.rsc dropped the scroll bar.
- **Grep:** cache-miss arms `return 0;` in cost or lookup functions; find-only resolves whose results are appended untested (`Append\(.*Find`); `FindElement\("…"\)` on mod-overridable `.rsc` without a fallback.
- **Modules:** game/Action, game/Component, game/Game, game/Work, ext/* fix rows.

**Weight by commit count:** C6 is about 120 commits, C4 about 15, C3 about 12, C10 to C12 together about 25, C16 about 15. The rest are 3–8 each. The most recurrent crash shapes are C1, C3, C10 and C11; C2 and C3 have the concrete open leads listed above.

---

# input_surface

# OpenBA: inventory of parsers for untrusted input and fuzz targets

No fuzz infrastructure exists in the tree: no libFuzzer target, no `fuzz` CMake option, no corpus. The unit build (`tests/unit/sources.cmake`) already links the engine's IO classes (`CompressedMemoryStream`, `BinaryReader`, `String`, the allocator). Most targets below can therefore be added as `-fsanitize=fuzzer,address` executables that reuse the unit link set.

**Seed rule (AGENTS.md §1).** Seeds come from our own writers: `CompressedStream` write, `PackageFile::Serialize`, `SaveTail::Encode`, `SettingsFile::Serialize`, the ext codecs' `Encode`, and the FFmpeg wmav2 encoder for xWMA. A corpus minimised from retail `.pkg`/`.sav`/DXBC stays in the user's own dir (`BANISHED_FUZZ_CORPUS`-style, out of tree, like `xwma_decode_test.cpp:40`, which reads `/WinData/data0.pkg` at run time and SKIPs without it).

## Ranked by fuzz yield

### 1. CompressedStream: block directory, window fill and LZF decode (every package entry and every save block)
- **Entries**
  - `IO::CompressedStream::Open` (`src/engine/io/compressed_stream_open.cpp:99`)
  - `DecompressFromSource` (`compressed_stream_read.cpp:249`)
  - `SetWindow` (`:294`)
  - `Read` (`:421`)
  - `System::Compression::Decompress` (`src/engine/system/compression.cpp:296`)
- **Bytes:** `u32 blockCount`, then `blockCount` × 8-byte `CompressedStreamHeader {u32 sourceOffset; u16 compressedSize; u16 uncompressedSize}` (`Stream.h:270`), then LZF (FastLZ-style) blocks of 32 KB windows.
- **Data → memory**
  - `rtcoll::RtColl_ArraySetSize_180024d70(&_headerData, blockCount);` (open:123). The check is `static_cast<int32_t>(blockCount * 8) > len`, which is signed and wraps (the comment at :88 says so).
  - `src->Read(h->compressedSize, scratch);` (read:276). `scratch` is `uint8_t scratch[RtIo::kWin]` (0x8000), but `compressedSize` is u16 (up to 0xFFFF), so the stack overflows by up to 32 KB.
  - `src->Read(h->uncompressedSize, w->buffer);` (read:285). The stored arm reads up to 0xFFFF into a 0x8000 window, a heap overflow.
  - `src->SetCurrentPos(static_cast<int32_t>(h->sourceOffset))` (:272). The offset is attacker-chosen. `MemoryStream::Read` (`memory_stream.cpp:64-75`) checks only `end > _size` in 32-bit signed arithmetic, so a negative cursor gives `memcpy(dst, _data + cursor, n)`, an out-of-bounds read before the buffer.
  - In `Decompress`, the output is bounded (`op + n > dstEnd`, `dist > op - dstBase`). The source is not bounded: a literal run reads `n ≤ 32` bytes and the match reads 1–2 bytes past `srcEnd` (the do-while at :305 always reads one control byte).
- **Existing checks:** only `total == len` in `Open`, which is a sum of u16s, not a per-block cap. Retail's `Read` ignores the `SetWindow` failure.
- **Tests:** `decode_exact_test.cpp` runs `Decompress` against an oracle on garbage input, with padded sources. `ext_stream_cache_test` and `b39_io_classes_test` cover only well-formed streams.
- **Fuzz target: YES.** Build a `CompressedMemoryStream` over a `MemoryStream` on the input (exactly what `decode_exact_test` does), call `Open(…, 1)`, then random `SetCurrentPos`/`Read`. A `Decompress(src, len, dst, 0x8000)` target is trivial.
- **Note:** the overflows are retail-faithful, so a hardening change is a `fix.*` row.

### 2. Package index: `PackageFile::ReadPackfile` (.pkg and every third-party .pkm mod)
- **Entry:** `src/engine/io/package_file_read.cpp:413`. Callers: `FileSystem::LoadPackages` (`filesystem_packages.cpp`, `*.pkg`) and `Modification::RefreshList` (`modification_refresh.cpp:563`, every `*.pkm` in the mods dir, read at the mod-list scan).
- **Bytes:** `u32 hdr` (bit 31 = has metadata; the low bits seek). Then `u32 blobLen` + blob, the blob being a CompressedStream (§1) holding: fileCount, formatVersion, version, userVersion, priority, platform, u8 overridable, crc, icon w/h + pixels, an optional preview, three strings (UTF-16 for v≤2, raw for v>2), and `fileCount` × {name, offset, size}.
- **Data → memory**
  - `MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(blobLen)), 4)` (:465). A negative `blobLen` gives a ~16 EB request.
  - `RtColl_ArraySetSize_180029510(&_iconData, _iconHeight * _iconWidth)` (:514): int32 multiply, overflows.
  - `RtColl_ArraySetSize_180029630(&_previewData, *n90); Read(*n90, _previewData._data)` (:523-525): unbounded.
  - **Stack overflow.** `cs->Read(nameLen, buf60)` (:596) and `cs->Read(len, buf860)` (:347, :600) copy into `uint8_t frameBuf[0x1840]`. `buf860` has 0x1040 bytes and `buf60` has 0x1840; `len` is only checked `> 0`.
  - The UTF-16 transcode then walks `end = buf860 + 2*len` (:353). This is the documented §76 over-read: twice the bytes read are decoded.
  - Path B writes no terminator, so `BuildStringFrom` (:302) runs `strlen` over stale stack data.
  - `fileCount` drives an unbounded loop with `SetSizeFill(_entries._size + 1)` per entry (:645): O(n²) growth, a DoS.
- **Existing checks:** only `len > 0`. `PackedMapSpan` (`filesystem_openfile.cpp:421-433`) later bounds packed reads against the mapping size, but not against the entry's own size. `large_packages_unwrap.h` validates offsets only for files over 2 GiB with the fix on.
- **Tests:** `ext_create_on_demand_test.cpp:607` builds a synthetic directory (offsets and sizes only). The metadata, string and icon paths are untested.
- **Fuzz target: NEEDS-SHIM.**
  - The body calls `IO::FileSystem::_singleton->OpenFile/ReadFile`. A harness can construct the singleton and write the input to a temp loose file. Cleaner: split the post-open body (`blob` → `CompressedMemoryStream`) into a function over `(const void*, int)`. That is a move, not a behaviour change.
  - Highest real-world exposure: `.pkm` files are downloaded mods.

### 3. Save files: container, metadata block, class B/C tail
- **Container:** `PersistenceWorker::GetSaveInfoJob` (`persistence_saveinfo_job.cpp:157`) reads `u32 header`, `{magic 0x80808080, crc}`, and `blockCount` × 12-byte descriptors (`ReadFile(n * 0xC, descs->_data)`, :232). It then computes a chunked CRC (:263-287). `blockCount` must equal the caller's count (:200), which bounds it.
  - `LoadJob` (`io/closure_batch2b.cpp:143`) reads `desc[i].size` into buffers that `SavedState::SetupLoad` (`savedstate_setupload.cpp:152,167`) allocated as `MemAlloc(size, 4)` from the same descriptors.
- **Metadata block:** `Save::SaveLoad::LoadMetaData` (`src/engine/save/saveload_loadmeta.cpp:245`).
  - `const int32_t byteCount = (*width) * (*height) * 4; *pixels = MemAlloc(...sign-extended...); rw->SerializeBytes(byteCount, *pixels, nullptr);` (:313-315). The int32 overflow is transcribed on purpose.
  - `rtcoll::AsArray<&kSpec_MetaEntry20>(mods)->SetSize(count);` (:339): `count` is unbounded, then `count` × two `String::Serialize`.
  - `CompressedStream::Open`'s result is discarded (:282), so §1 applies in full.
- **`String::Serialize`** (`src/engine/system/string.cpp:889`), used by every string in saves and resources:
  - `SetLength(n - 1)` with `n` up to 0x7FFFFFFF.
  - The legacy UTF-16 arm runs `RtMemAlloc(n * 2)`, then decodes twice.
- **SaveTail:** `Ext::SaveTail::Decode` (`src/ext/savetail.cpp:82`) is a length-prefixed manifest with a trailer CRC, id length capped by `kMaxIdLength` (:123), checked from the end (:96-98). Class C payloads go to 4 `StateCodec::decode(bytes, size, ver)` functions:
  - `family_tree.cpp:479`
  - `item_priority/save_state.cpp:145`
  - `restricted_foods/save_state.cpp:118`
  - `tool_names.cpp:185`
- **Game-state block 1:** about 406 `ReaderWriter` calls in 65 files (`grep _readerWriter->|rw->Serialize`), with 82 count-driven `SerializeInt(n|count|…)` → `SetSize` sites, e.g. `animationstate.cpp:246-247` `rw->SerializeInt(n); if (IsReading) SetSize(a, n);` followed by an `n`-iteration loop.
- **Exposure:** saves are shared between players. Android imports `.sav` from user-picked zips (`GameFilesJob.java:575-592`).
- **Tests:** `save_abi_test` covers layout only. `ext_savetail_test` and the four `ext_*_test` codec tests are round-trips. `known_port_issues.md:951` (row 806) notes that a duplicate key "needs a corrupt save", which nothing exercises.
- **Fuzz targets**
  - `SaveTail::Decode` + codecs: YES (pure).
  - `LoadMetaData`: NEEDS-SHIM (a `SaveLoad` object with `_embeddedState` set to the input; the allocator only).
  - `GetSaveInfoJob`: NEEDS-SHIM (FileSystem singleton, temp file).
  - Block 1: NO (it needs a live world and entity factories).

### 4. Audio: SoundBuffer resource → xWMA decoder (FFmpeg wmav2 extract, LGPL, `third_party/wmadec`)
- **Entries**
  - `SoundBuffer::PlatformSerialize` (`src/engine/audio/vtbatch7_soundbuffer_soundeffect.cpp:205`)
  - `DecodeXwmaJob` (:324)
  - `XwmaDecoder::Open/DecodePackets/DecodeChunk` (`xwma_decode.cpp:160/269/279`)
  - The music worker (`audio_stream_run.cpp:272-332`)
- **Data → memory**
  - `_data = MemAlloc(_length, 4); _wmaData = MemAlloc(_wmaLength, 4)` (:212-213), with int32s from data.
  - The fixed `SerializeBytes(0x28, _format)` read.
  - `packetCount = _wmaLength >> 2`.
  - `TotalSamples() = _seekTable[_packetCount-1] / (2*channels)` sizes `job.pcm = MemAlloc(total * ch * 2)` (:338-341). An attacker-chosen u32 gives up to about 8 GB per sound.
- **Existing checks:** `Open` rejects tag ≠ 0x161, channels ∉ {1,2}, and `bytes / blockAlign < packetCount` (:173-176). Decode output is capped by `maxOut`. The wmav2 bitstream parser itself is the deep surface: an extracted FFmpeg, possibly without later upstream fixes.
- **Tests:** `xwma_decode_test`, `xwma_tos16_test`, `audio_pcm_test`, `load_decode_batch_test`. The xwma ones SKIP without the game install (`xwma_decode_test.cpp:13,173`).
- **Fuzz target: YES.** `XwmaDecoder` is documented "no OS header, no engine type, no allocator but std". Split the input into a 0x28-byte format + seek table + packets. Synthesize seeds with an ffmpeg `wmav2` encode out of tree.

### 5. Shaders: DXBC → SPIR-V (Vulkan) and DXBC → AIR (Metal)
- **Entries**
  - `ConvertDxbc` (`src/render/vulkan/shader_convert.cpp:421`)
  - `StageOfDxbc` (:386)
  - `DecodeEntry` (:615, from the derived cache)
  - SPIR-V passes: `spirv_legalize.cpp` (`LowerBitFieldInsert` :41, `DowngradeSpirv` :162, `WidenInputs` :323), `spirv_combine.cpp:78`, `CheckSpirvStructure` (:532)
  - `third_party/dxbc-spirv` does the parse.
- **Bytes:** SM4 DXBC program blobs from the user's packages, including mod `.pkm`.
- **Tests:** `vulkan_shader_convert_test.cpp`, `metal_shader_cache_test.cpp`, `metal_fast_pipeline_cache_test.cpp`.
- **Fuzz target: YES** for `ConvertDxbc` + legalize ("No Vulkan header and no device", `shader_convert.h`). Seeds: compile with fxc from our own HLSL; no retail DXBC goes in the tree. Metal's `libairconv.dylib` (DXMT) is NEEDS-SHIM (macOS, dlopen).
- **Pipeline cache:** `vkCreatePipelineCache(initialDataSize = blob.size())` (`pipelines.cpp:893`) hands a cache file to the driver, which validates the header. Not a target.

### 6. Text codec and string transcode
- **Entries**
  - `RtIo_TextCodec_DetectFormat` (`src/engine/io/text_codec.cpp:309`)
  - `DecodeOne` (:199), `EncodeOne` (:103), `ConvertText` (:354)
  - `RtIo_SerializeStringArray` (:483): `len = tagged & 0x7FFFFFFF; SetLength(len-1); tmp = MemAlloc(len*2)` (:501-518)
- **Existing checks:** non-uniform, transcribed as retail has them (UTF-8 needs `src+n < end`; a surrogate pair needs `src+4 < end`, banner :49-58).
- **Tests:** `system_abi_test` covers ABI only. Nothing exercises `ConvertText` or `SerializeStringArray`.
- **Fuzz target: YES.** The functions are pure and `extern "C"`. A differential target: `ConvertText(fmt a→b→a)`.

### 7. Binary `.rsc` resources (`Object::External` via `BinaryReader`)
- **Bytes:** compiled resources in packages: about 88 `Serialize(...Serializer&)` methods over 26 files. Representative count → alloc sites:
  - `achievement_serialize.cpp:141,170`
  - `audio_effectslot_serialize.cpp:111`
  - `vtbatch7_soundbuffer_soundeffect.cpp:627,691`
  - `animationstate.cpp:246,323-357`
  - `package_file_serialize.cpp:211-235` (three counted arrays)
- **Textures:** `ImageBuffer` → `imagebuffer_platform_serialize.cpp:269`, where a level count → `Array<Array<u8>>::SetSize` gives blobs of attacker-chosen size.
  - DX11: `desc.Format = kFormatToDxgi[ib->_format]` (`render/dx11/imagebufferdata.cpp:610`). The table is `const int kFormatToDxgi[8]` (:212) and `_format` is a serialized unsigned (`imagebuffer_vtable.cpp:384`), so a `_format` ≥ 8 is an out-of-bounds table read. `kFormatBytes[ib->_format]` (:634) has the same problem.
  - DX11 hands `pSysMem` + a pitch with no check of the blob size (an out-of-bounds read inside D3D).
  - Vulkan checks `levels[i].size >= need` (`render/vulkan/seeded_create.cpp:125-126`); ETC2 uses `LevelBytes` (`etc2_chain.cpp:39`).
- **Tests:** `b39_obj_compiledexternal_test`, `object_external_test` and the ABI tests cover layout and well-formed paths only.
- **Fuzz target: NEEDS-SHIM.** Per class: construct the object and drive `Serialize` with a `BinaryReader` over a `MemoryStream`. `SoundBuffer`, `AnimationState`, `PackageFile::Serialize` and `ImageBuffer` (with the backend Create stubbed) are feasible without a GPU. A whole-factory target is NO for now (type registry, ExternalPtr resolve).

### 8. Ext caches (Documents\Banished\Cache, written locally, hash-checked)
- **`DerivedCache::Store::LoadIndex`** (`src/ext/derived_cache/derived_cache.cpp:381`): the 32-byte header plus 80-byte records are validated (`r.magic`, `RecordCheck`, contiguous `r.offset`, `r.offset + r.size > datSize`, :427-428). `ReadWhole` caps at 2³¹ (:164). XXH64 is not cryptographic, so a planted cache can forge hashes. Tests: `ext_derived_cache_test`. YES, over a temp dir.
- **`StreamCache::Bundle::AddChunk`** (`src/ext/stream_cache/stream_record.cpp:234`): `parsed.reserve(count)` runs at :239 on the raw u32 `count` before any per-record validation, so a bad_alloc or abort on a forged chunk is likely. The rest is checked carefully (:244-279). Tests: `ext_stream_cache_test`. YES (pure).
- **`PackageCache::Decode`** (`src/ext/package_cache/index_codec.cpp:151`): range-checks pack/file (:52) and strict `_stricmp` order (:70). Tests: `ext_package_cache_test`. NEEDS-SHIM (`rtavl` node allocator, engine `Filename`).
- **Shader cache entries:** `DecodeEntry` (§5). Pure.

### 9. fix.large_packages `Unwrap`
- `src/ext/large_packages/large_packages_unwrap.h` is pure and standard C++, with a dense/monotonic/in-file rule. Tests: `ext_large_packages_test`. YES, low yield.

### 10. settings.cfg and the settings map
- `IO::SettingsFile::Parse` (`src/engine/io/settings_file.cpp:96`): `key=value` lines, escapes, BOM, "No length cap". Tests: `settings_file_test`, `persistence_settings_test`, and about 20 `ext_*` tests that read keys.
- **Fuzz target: YES**, as a round-trip property `Parse(Serialize(Parse(x)))` == `Parse(x)`. Low crash yield (`std::string`).
- `persistence_settings.cpp` and `ext/registry_values` (numeric conversion of values): low.

### 11. Command line and environment
- **`System::Configuration::ParseValue/ParseCommand`** (`src/engine/system/configuration.cpp:273/307`): `s._data[pos]` is read at `pos == GetLength()` without a guard, by design (:322, :342). It is safe on a NUL-terminated buffer, but worth checking with an empty or null-`_data` `String`. YES (`String` links in unit tests), low.
- **`src/app/options.cpp`:** the only `getenv` (:26), with `std::atoi` on about 15 flags (:38,107-207). No fuzz value.
- **Android IPC (not a fuzz target; worth a review):** `OpenBAActivity.applyLaunchEnvironment` copies every intent extra prefixed `BANISHED_` into the process environment (`Os.setenv`, `OpenBAActivity.java:262-272`). The launcher activity is `exported="true"` (`AndroidManifest.xml:52`), so any app on the device can set harness flags, e.g. `BANISHED_USER_DIR` or `BANISHED_CAMERA_PIN`.

### 12. Android data import (Java, out of the C++ fuzz scope)
- Zip import takes only the last path segment and requires a `.pkg`/`.pkm`/`.sav` suffix and no leading dot (`GameFilesJob.java:456-476`, `:584-591`). Archive folders map to numbered dirs, so a zip slip is not possible there.
- The SAF folder copy (`:341-355`) does `new File(staged, d.name)`, where `d.name` is a provider-supplied `COLUMN_DISPLAY_NAME` checked only by `isPackageName` (suffix). A name containing `/` or `..` from a hostile documents provider would escape `staged`. **Not verified.**
- Fonts are copied from the APK's own assets (`OpenBAActivity.java:225`): trusted.

### 13. Fonts
- FreeType (`third_party/freetype`, `src/platform/common/font_raster_freetype.h`) rasterises a system TTF on posix and a TTF copied from the APK on Android. Tests: `ext_font_raster_test`. FreeType is already fuzzed upstream (OSS-Fuzz). YES, but low yield.
- The `.rsc` text dialect `Ext::RscText` (`src/ext/resources/text_parser.h`) only reads files embedded in the exe (`tools/ext_rsc_embed.py`). Tests: `ext_rsc_text_test`. YES, low exposure.

### 14. BC1/BC3/BC4/ETC2 block codecs
- `render/common/bc_decode/bc_decode.h:30-35`, `etc2/etc2.h:46-59`, `bc_to_etc2.h:21-27`. These are fixed-size per-block functions, and their callers check level sizes. Tests: `bc_decode_test`, `bc4_test`, `etc2_test`. YES, trivially, but low yield (total functions over fixed arrays).

### Not reachable from input (recorded)
- `TextFileStream::Read` (`text_file_stream.cpp:716`): `memmove(buf, _buffer + _bufCursor, len)` has no bound. Only the text save writer (`external_savetext.cpp`) builds one.
- Screenshot BMP writing (`save_image_to_file.cpp`) and the cursor from texels are write paths or fixed 32×32.

## Summary table

| # | Surface | Pure fuzz fn? | Unit-tested on malformed input? | Untrusted source |
|---|---|---|---|---|
| 1 | CompressedStream + LZF | YES | partly (`Decompress` only) | every pkg/pkm entry, saves |
| 2 | `ReadPackfile` | needs-shim (FileSystem/temp file) | no | downloaded `.pkm` mods |
| 3 | Save meta / container / tail | tail YES; meta/container needs-shim; block 1 no | no | shared `.sav` |
| 4 | xWMA / wmav2 | YES | no (data-gated, SKIP) | packages, mods |
| 5 | DXBC→SPIR-V | YES | well-formed only | packages, mods |
| 6 | TextCodec / string arrays | YES | no | packages, saves |
| 7 | `.rsc` Serialize bodies, ImageBuffer format | needs-shim per class | no | packages, mods |
| 8 | Derived / stream / package caches | YES (package_cache needs-shim) | round-trip only | local cache dir |
| 9–14 | Unwrap, settings, cmdline, fonts, rsc text, BC/ETC2 | YES | yes | local / embedded |

**Retail-faithful overflows to flag for a `fix.*` ruling** (these belong in `docs/retail_bugs.md`, not in silent guards):
- §1: the u16 block sizes against the 0x8000 scratch and window buffers, and `MemoryStream::Read` with a negative cursor.
- §2: the `ReadPackfile` stack buffers.
- §3: the `LoadMetaData` thumbnail `w*h*4` overflow.
- §7: `kFormatToDxgi[_format]`.

Of these, `docs/retail_bugs.md` names only the uint32 offset limit (§9, line 419). None of the buffer overflows is listed there.

---

# concurrency

## OpenBA concurrency inventory (read-only; the tree is main at 033dbca08)

### 1. Threads OpenBA creates

All `System::Thread` subclasses start in `System::Thread::InitFields` at `src/engine/system/thread.cpp:147`, which calls `Platform::ThreadCreateSuspended`. That is `_beginthreadex` with CREATE_SUSPENDED on Windows (`src/platform/win/thread_win.cpp:20`) and a latched pthread on POSIX (`src/platform/posix/thread_posix.cpp:89`). Each thread then parks on {hStart, hStop}. `Start()` resets hFinished and sets hStart. `ThreadFunc` sets `_runFlags|=1`, calls `Run()` then `OnCompleted()`, clears the bit and sets hFinished (thread.cpp:193-216). `_runFlags` is a plain `uint32_t` (Thread.h:259) that other threads read.

#### Retail threads (the engine and game had them in retail)

| Thread | Created | Runs | Shared data / protection |
|---|---|---|---|
| **Engine::Loader** (stack 256 KB) | Static `LoaderLoop::_loader`, ctor `src/engine/engine/UpdateLoop.h:308` (CRT init) | `Loader::Run` → `UpdateLoop::LoadResources` while the main thread draws the loading loop | `_loop` (+0x30): the main thread writes it in `Load`, and the Start/hFinished events order the hand-off. The resource factory AVL tree and String refcounts use **ResLoadGuard** (`src/app/res_load_lock.h`, a recursive Platform mutex initialised with `OnceRun`, taken at `src/engine/io/external_path.cpp:521` and 6 sites in `ext/find_fastpath`). The load progress uses three `std::atomic<int32_t>` (UpdateLoop.h:280-282). The res-load lock **is port-only**: retail had the race (33% menu crash before the lock). |
| **Audio::Stream** (64 KB) | `::new (dev+0xa00)` in singleton_ctors; ctor `src/engine/audio/audio_stream.cpp:286` | The music stream decode worker | `Platform::LockStorage _lock` (+0x50, Stream.h:122), which `Play`/`Stop`/`Lock`/`Unlock` take on the main thread. The header says (Stream.h:36-42) that the audio callback thread writes the Device channel flags **without a lock, by retail's design**. |
| **IO::PersistenceWorker** (64 KB) | Embedded at Persistence+0x10, ctor `src/engine/io/Persistence.h:387` | Save, load, delete, save-info and save-list jobs (switch on `_command`) | `_command`/`_pendingFilename` are written by the main thread before `Start()`. The events order the hand-off. `_status` is a plain `int32_t` that the main thread **polls through a `volatile` cast** (`src/engine/save/saveload_update.cpp:999`, `src/ext/background_save/background_save.cpp:322`). Bit 0 of `_runFlags` means "unavailable". |
| **PathfinderThread** (64 KB) | Placement-new in `src/game/Game/pathfinder_seed.cpp` (~line 123) | Drains the command FIFO and runs A* (`GridPathSearch::Run`) | `System::CriticalSection lock` (+0x30) guards the queue and the result slots. **The `_runFlags & 1` check followed by `Start()` happens under the lock, but the worker clears the bit outside it** (action_pf_astar.cpp:789, keystone_a_zone.cpp:64/105, keystone_a_gamedtor.cpp:243, path_query_first.cpp:45). Retail compensates by re-checking in the spin loop. The case-5 path copy and the `ready` store run **unlocked, as in retail** (known_port_issues row 961 "RETAIL RACE KEPT"; `std::atomic_signal_fence` stops GCC hoisting the loads). `queueCount` is read with `__atomic_load_n(RELAXED)` only in the profiler. |

The audio device thread belongs to the platform layer (XAudio2 in retail, SDL3 now), not to OpenBA.

#### Port-only engine infrastructure (not an ext row)

| Thread | Created | Runs | Sync |
|---|---|---|---|
| ParallelFor pool (block WGPAR) | `src/engine/system/parallel_for.cpp:158` | Fork-join chunks for worldgen (`worldgen_cells.cpp` ×5, `worldgen_f390_erosion.cpp:809`), `audio/load_decode_batch.cpp:193` and `render/common/etc2/etc2_chain.cpp:57` | Call lock plus pool `std::mutex` + condition variable + epoch, an atomic `next` claim counter, MXCSR copied from the caller, `thread_local tl_inParallel` |
| Lazy-create workers | `src/render/common/create_on_demand/lazy_table.cpp:163` | GPU object creation on demand | `_mutex`, `_workCv`/`_doneCv`, an atomic `_count` fast path (lazy_table.cpp:168/176) and `_epoch`; `_sweepMutex` |
| Async GPU create (DX11 / Vulkan) | `render/dx11/async_gpu_create/async_create.cpp:154`, `render/vulkan/async_gpu_create/async_create.cpp:130` (detached) | Off-thread `Create*` calls | `g_mutex`, `g_workCv`/`g_doneCv`, atomics `g_pending`/`g_requested` |
| BC4 chain (compress_ao) | `src/render/common/compress_ao/bc4_chain.cpp:31` (detached) | AO texture compression into the derived cache | `_m`/`_cv`; `std::atomic<DerivedCache::Store*> g_cache` (release/acquire, :141/:160) |
| Vulkan pipeline warm | `src/render/vulkan/pipelines.cpp:917` | Pre-builds pipelines | Five mutexes (`g_pushLock`, `g_shaderLock`, `g_layoutLock`, `g_linkLock`, `g_pipelineLock`), plus `g_warmLock`/`g_warmCv` and atomic counters |
| Android logcat mirror | `src/platform/android/app_android.cpp:322` | Tails `standalone_error.log` to logcat | `std::atomic<bool> g_mirrorStop` |
| Android start-mask wrapper | `src/platform/android/thread_start_android.cpp:36` (`--wrap=pthread_create`) | Pins every thread to a core mask | n/a |

#### Threads that exist only in `ext.*` rows

| Row (default) | Created | Runs | Sync |
|---|---|---|---|
| `render_pipe` (off, class A) | `RenderThread` `src/ext/render_pipe/render_pipe.cpp:654` (1 MB), `Start` at :742 | Emits frame N (pass 2, Present) while main builds N+1 | `handoff.h` (std::mutex + cv + atomics, 18 sites). The `Video::Device` +0x60 recursive lock moves across threads (BeginCommands→FinishPresent). The comment at :55 says "the context is never read while the render thread runs", **which is a convention**. Fixes 4/5 skip SunLight/camera writes while a packet is pending. |
| `path_workers` (off, class B) | `PoolThread` `src/ext/path_workers/path_workers.cpp:129`, Start :164 | N concurrent A* searches, each with its own grid | `pool.h` mutex + `work_`/`idle_` cv. The publish runs under the pathfinder `lock`. **`cellFlags` is shared read-only by convention**: "commands 2 / 3 … wait for the pool to be idle first". |
| `gather_threads` (off, class A) | `src/ext/gather_threads/gather_threads.cpp:31` | Parallel cull helpers | Spin on `alignas(64) std::atomic<uint64_t> _ticket`, `_done`, `_awake`, `_sleepers`; parks on `_m`/`_cv` |
| `background_save` (off) | `src/ext/background_save/background_save.cpp:234` | **Serialises the live world on a worker while the main thread Draws** | `std::atomic<bool> done` only. **No lock on the world**: correctness rests on measured claims (banner lines 5-15: "Draw changes no serialised byte", "0 ExternalFactory calls in 3000 Draw windows", the tool manager's +0x3c..+0x44 shadowed and copied back after the join). |
| `async_save_compress` (off) | `src/ext/async_save_compress/async_save_compress.cpp:260` (detached) | FastLZ block compression | `mu`, `work`/`done` cv |
| `decode_ahead` (off) | `src/ext/decode_ahead/block_cache.cpp:18` | Decodes CompressedStream blocks ahead | `mu_`, `work_`/`done_` |
| `derived_cache` writer (with compress_ao) | `src/ext/derived_cache/derived_cache.cpp:312` | Appends payloads, compacts, writes the keep list | `_mutex` ("everything below", derived_cache.h:224), `_fileMutex` (taken **before** `_mutex`), `_usedFileMutex`, `_cv`, atomic `_disabled`/`_closing` |
| `path_query_first` (**on**) | no thread | Pushes onto the pathfinder queue head | The pathfinder lock |

#### Diagnostics threads (port-only)

- Boot-state watchdog: Windows `src/diag/win/boot_state_watchdog.cpp:425`, POSIX :320, Darwin :360, Android :337. It uses `volatile LONG` flags, always read and written through `Interlocked*` (:299-304, :439).
- Sampler: `src/diag/win/sampler.cpp:286`. `volatile int g_samplerTag` is written by the main thread and read by the sampler after `SuspendThread`, a plain-volatile hand-off.
- Redzone watchdog and self-test: `src/diag/win/redzone_alloc.cpp:289/296`.
- Hardware watch arm thread: `src/diag/win/hw_watch.cpp:75`.
- `DiagThreads` registry: `std::atomic<const volatile uint32_t*> busy` (diag_threads.cpp:16) points at `_runFlags`.

**Totals:** 4 retail thread classes. About 7 port-infrastructure creators. 9 ext creators (8 with threads; path_query_first adds no thread). 4 or 5 diagnostics creators per platform.

### 2. Lock types and wrappers

| Primitive | Count | Notes |
|---|---|---|
| `Platform::LockStorage` + `MutexInit/Lock/Unlock/Destroy` (`src/platform/common/platform_sync.h`) | 4 embedded locks | 0x28 opaque bytes holding a CRITICAL_SECTION (Windows) or a heap mutex pointer (Darwin); **always recursive**. Retail sites: `MemoryAllocator+0x640` (MemoryAllocator.h:367; every allocation from every thread goes through it), `Video::Device+0x60` (Device.h:588, its recursion is load-bearing), `Audio::Stream+0x50`. Port-only: `res_load_lock`. |
| `System::CriticalSection` (Thread.h:178, Lock/Unlock thread.cpp:124-131) | 10 refs | Retail class over LockStorage. Its only member instance is `PathfinderThread::lock`. |
| `ResLoadGuard` (`src/app/res_load_lock.h`) | 7 call sites | RAII over the recursive res-load lock |
| `Platform::OnceFlag`/`OnceRun` | — | INIT_ONCE |
| `Platform::Event*` (auto/manual reset, `EventWaitAny`) | — | Win32 events. POSIX: one **immortal** process-wide `std::mutex` + cv (`src/platform/common/event_std.cpp:37`, fix for row 1285) |
| `std::mutex` | 80 decls/refs in 70 files | `lock_guard` 298, `unique_lock` 49; no `scoped_lock` |
| `std::recursive_mutex` | 15 refs | `Dx11PresentMutex` (contextdata.cpp:385), Metal `stateMutex` / `pipelineMetadataMutex` |
| `std::condition_variable` | 25 | |
| `std::atomic<>` | 251 refs | |
| `std::atomic_flag` spinlock | 1 | `src/engine/graphics/scene_ext.h:70`: a hand-rolled spin with no backoff |
| `Interlocked*` | 13 | diag only |
| `thread_local` | 24 | |

There is no `shared_mutex`, no SRWLOCK and no project-wide annotated mutex wrapper.

### 3. Flags

**Hand-offs with no visible synchronisation:**
1. `PersistenceWorker::_status`: the main thread polls it through a `volatile` cast, and the worker writes it with plain stores (saveload_update.cpp:999, background_save.cpp:322). This is **volatile used as sync**. It is retail-shaped, but the C++ model does not order it.
2. `System::Thread::_runFlags`: a plain `uint32_t` that the worker RMWs (`|=1`, `&=~1`) while the main thread reads it under a different lock (the pathfinder CS). There is a lost-wake window: main sees bit 1 just before the worker clears it, so it skips `Start()`. Retail relies on a later enqueue or the YieldToThreads re-check loop (keystone_a_zone.cpp:97-109). `DiagThreads` reads the same word as `const volatile`.
3. Pathfinder case 5: the path copy and the `ready` byte run unlocked against `AllocResult`'s grow (row 961, a kept retail race). `path_workers` closes it for its own workers.
4. `background_save`: the worker reads live world state concurrently with Draw, and only measurement supports that.
5. `render_pipe`: Video::Context state is "never read while the render thread runs", and SunLight/camera skips depend on `pending` (the RenderPipeIdem shadow proves the bytes match).
6. `path_workers`: `cellFlags` is shared read-only "because commands 2/3 wait for idle".
7. The audio callback thread writes the `Audio::Device` channel flags lock-free (Stream.h:41, retail design).
8. The sampler's `volatile int g_samplerTag`. This is acceptable for a diagnostic.

**Double-checked or first-use patterns:**
- The lazy_table atomic `_count` fast path before the lock (lazy_table.cpp:168/176).
- `class_vptr.h:33` relaxed load-compare-store cache (benign: every writer stores the same value).
- The UI `rtuielem::AtomicExport` `std::atomic<void*>` caches (Element.h:210, UserInterface.h:282).
- `bc4_chain g_cache` release/acquire publish.
- The `use_tracker.h` `_closed`/`_users` seq_cst spin-yield gate (:77-88).
- `fold_index.h:244` static atomic counters.

The res-load lock correctly uses `OnceRun`, not a hand-rolled DCLP.

**Null-by-default static seams:** 67 `static … _ext*` declarations in 27 headers under `engine/` and `game/`. `src/ext/registry_boot.cpp` assigns 13 of them directly; the rest are set by their own `ext/` installers. They are plain pointers, set at boot or install. The cross-thread ones:
- `IO::PersistenceWorker::_extFixSaveTemp` (Persistence.h:407): it points at a row's **live plain `bool`** (`src/ext/registry.h` rows are plain `bool`), which the worker reads in SaveJob while the options UI can flip it on main.
- `IO::CompressedStream::_extAsyncCompress` and `_extDecodeAhead`: consumed by the Loader and Persistence threads.
- `UpdateLoop::_extLoadProgress` (UpdateLoop.h:284): the Loader thread writes it.
- `LoaderLoop::_extFrame` (UpdateLoop.h:363) and `MainLoop::_extFrame` (MainLoop.h:272): installed or uninstalled while the render_pipe thread exists (it is joined first at `kExtBeforeTransition`).
- The `Object::External` seams `_extVisitReplay` and `_extFindFast` (External.h:938-939): reached from the Loader thread.
- `FileSystem::_extHandleClosing` and the other FileSystem seams: reached from Loader and decode_ahead.
- The Mesh, Material and Particle `_ext*` callbacks: reached from the render_pipe thread.

None is atomic. They are safe only if install and uninstall happen while the reading threads are parked.

### 4. Where GUARDED_BY / REQUIRES would pay most

1. **`Ext::DerivedCache::Store`** (derived_cache.h:200-248). About 20 fields are already commented "under _mutex", with a documented order (`_fileMutex` before `_mutex`). Mark the fields `GUARDED_BY(_mutex)`, mark `Lookup`, `Stamp` and `RefusedBytes` `REQUIRES(_mutex)`, and add `ACQUIRED_BEFORE`.
2. **`PathfinderThread`** (PathfinderThread.h): `queueHead`, `queueTail`, `queueCount`, `results` and `nextToken` `GUARDED_BY(lock)`; `ApplyCommand`, `AllocResult` and the enqueuers `REQUIRES(lock)`. The kept-race sites (case 5 copy, `_runFlags`) need `NO_THREAD_SAFETY_ANALYSIS` plus a row reference, which also documents rows 47 and 961.
3. **`ext/render_pipe/handoff.h` `Handoff`**: the packet state under `_m`. It is the riskiest new protocol and has 18 lock sites.
4. **`ext/path_workers/pool.h` `Pool`**: the queue and `inFlight_` under `mutex_`.
5. **`render/common/create_on_demand/lazy_table.h`**: `_entries` and the record state under `_mutex`; sweep under `_sweepMutex`; annotate the atomic `_count` fast path as the deliberate exception.
6. **`app/res_load_lock`**: annotate it as a capability and put `REQUIRES(ResLoad)` on `ExternalFactory::FindOrLoad` and `External::Load`, so a future unguarded caller on the Loader thread fails to compile.
7. **`Video::Device::lock` (+0x60)**: its `Lock`/`Unlock` as `ACQUIRE`/`RELEASE` on the device. render_pipe moves it across threads, so the analysis checks lock/unlock pairing but cannot follow the hand-off itself.
8. **`async_gpu_create` / `load_decode_batch` registries**: the file-static `g_mutex` with guarded queues. This is cheap to add.

### 5. Clang / -Wthread-safety feasibility

- **Windows and Linux** build with the pinned **GCC 16.2** (WinLibs / system). GCC ignores the Clang thread-safety attributes, so the macros must expand to nothing there.
- **macOS** (CI `macos-15`, AppleClang) and **Android** (NDK clang, ThinLTO) **already compile the tree with Clang**. CMakeLists.txt has Clang branches at :117 (`-fno-delete-null-pointer-checks`), :185 (PGO) and :622 (`-O0` exceptions for D2 destructors). Both platforms build `src/engine`, `src/game`, `src/ext`, `render/common` and the Vulkan or Metal backend, so every structure in §4 except DX11 is covered.
- **Feasible today with three additions:**
  1. A `thread_annotations.h` macro header that is a no-op on GCC.
  2. Mark `Platform::LockStorage` and `System::CriticalSection` `CAPABILITY("mutex")`, and give `MutexLock`/`MutexUnlock` and `Lock`/`Unlock` `ACQUIRE(*lock)`/`RELEASE(*lock)`. The attributes do not change layout, so every `static_assert` offset holds and `abi_audit.py` is unaffected.
  3. For `std::mutex`: both Clang targets use **libc++**, whose annotations turn on with `-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS`. Use them, or wrap `std::mutex` in an annotated `Mutex`/`MutexLock` pair.
- Add `-Wthread-safety` to the macOS CI job first (warnings only, or `-Werror=thread-safety` scoped to annotated files). The engine's `Platform::Event` hand-offs and the volatile `_status` cannot be expressed in the annotation model and stay as documented exceptions.

---

# gpu_platform

## GPU backends and platform layer: bug-class inventory

**Scope (tracked files / lines):** `src/render/dx11` 53 / 15.0k, `src/render/vulkan` 53 / 15.3k, `src/render/metal` 30 / 5.7k, `src/render/common` 80 / 35.5k (about 25k of that is generated shader-blob headers), `src/platform` 120 / 14.2k. The largest files are `vulkan/draw.cpp` (2107), `metal/backend.mm` (2210), `dx11/devicedata.cpp` (1985), `vulkan/device.cpp` (1654), `dx11/contextdata.cpp` (1239), `vulkan/resources.cpp` (1099), `vulkan/companions.cpp` (943).

---

### (a) GPU resource lifetime, frame-in-flight indexing, deferred deletion

**Key code**
- **Vulkan**
  - `kFramesInFlight = 2` and `kSwapchainImages = 3` (`vulkan/device_objects.h:24,27`). `Frame frames[kFramesInFlight]` (`device.cpp:113`).
  - Per-slot constant ring, descriptor pools and set caches: `g_slots[kFramesInFlight]` (`draw.cpp:127-138`, `RingAlloc` `draw.cpp:736`, chunk size 1 MiB).
  - One timeline semaphore. The 1.1 path uses fence-per-serial instead (`resources.cpp:490-560`, `SubmitCommands` `:559`).
  - Deferred deletion: `Retire(std::function)` stamps `FramesSubmitted()` (`resources.cpp:677`). `CollectRetired` (`:660`) destroys entries whose frame is below `FramesCompleted()`. It runs only from `BeginCommands`, after the slot fence wait (`device.cpp:1462`). `RetireImage` / `RetireBuffer` are used by AO (`ao/assao_pass.cpp:331`), companions (`:62,346`) and draw (`:353`).
  - Dynamic VB/IB: versioned host buffers (`companions.cpp:162-195`). A DISCARD takes a free version where `busyUntilFrame < FramesCompleted()`, otherwise it allocates a new one.
  - `renderFinished` is per swap image (`device.cpp:523`), which is correct. `imageAvailable` is per frame slot.
- **Metal**: `kFramesInFlight = 3` with a dispatch semaphore (`backend.mm:101-104,1180`), per-slot upload arenas, completion tickets, and `retire_pool.h`.
- **DX11**: COM refcounts. Thread safety with ext.render_pipe comes from `Dx11PresentMutex` (8 call sites) around every immediate-context user.

**Existing handling:** the Vulkan retire queue and versioned dynamic buffers are well-formed. Device-lost recovery drains `AsyncCreate`, pauses `CreateOnDemand` (`device.cpp:1215`) and waits for idle before teardown.

**Suspicious sites**
1. `vulkan/companions.cpp:148-151`: `SetBufferData` on a dynamic buffer memcpys into `versions[current]` in place. Frames in flight may still read that version. The comment says "nothing in the game does", which is not enforced: no log, no assert.
2. `vulkan/companions.cpp:169-191`: `LockBuffer` never checks `sizeBytes > size`. After a discard, `writeCursor = 0` and the returned pointer is good for only `size` bytes. A larger lock overruns the host buffer. DX11 has the same shape, but there it overruns a driver-owned map; here it is our own allocation.
3. `vulkan/resources.cpp:660-683`: `CollectRetired` runs only inside `BeginCommands`. Every early return before it lets the retire list grow and keeps device-lost / AO / companion images alive. The early returns are `backgrounded`, the minimised swap-chain failure, and the settings-change frame (`device.cpp:1392-1425`).
4. `vulkan/device.cpp:1413-1415` and `CreateSwapchain` (`:500-523`): `DestroySwapchainObjects` destroys the `renderFinished` semaphores after only `vkDeviceWaitIdle`. Present-engine waits are not covered by device idle (VUID-vkDestroySemaphore-05149 territory without `VK_EXT_swapchain_maintenance1`).
5. `metal/backend.mm:1145-1150`: `ReleaseDynamic` / `RecreateDynamic` run with no in-flight semaphore drain. This is safe only while every command buffer retains its resources; any `unretainedReferences` path would turn it into a use-after-free.

---

### (b) Swapchain and window edge cases

**DX11** (`dx11/devicedata.cpp`, `contextdata.cpp`)
- Swap chain: `BufferCount 2`, `DXGI_SWAP_EFFECT_DISCARD`, `ALLOW_MODE_SWITCH` (`devicedata.cpp:463-467`). `MakeWindowAssociation(NO_WINDOW_CHANGES)` (`:1260`) turns off DXGI's Alt+Enter.
- Resize (`:852-875`): `ResizeTarget`, then `ResizeBuffers(1, …)`, then `SetFullscreenState`, then the colour and depth buffers, chained with short-circuit `&&`. `DegenerateSize` / `RememberGoodSize` guard a 0×0 size (`:820-830`).
- Alt-tab: the WM_ACTIVATE handler (`:900-930`) uses `wasFullscreen` to leave fullscreen and restore it.
- Device lost: `Present` result goes through `GetDeviceRemovedReason` into `RecoverLostDevice` (`contextdata.cpp:647-670`). The fix.boot_device_lost seam covers a reset that fails on a removed device (`:445-466`). A forced-loss drive exists (`BANISHED_FORCE_DEVICE_LOST`).

**Vulkan** (`vulkan/device.cpp`)
- Acquire: `OUT_OF_DATE` / `SURFACE_LOST` set `swapchainDirty` and skip the frame. `SUBOPTIMAL` presents, then rebuilds. `DEVICE_LOST` goes to `BeginLostFrame` (`:1430-1446`). Present handles the same cases (`:1543-1550`).
- Zero extent: `CreateSwapchain` refuses "no pixels (minimised)" (`:432-435`) and clamps `0xFFFFFFFF` currentExtent to min/max (`:425-431`).
- Android pre-rotation and render scale go through `present.cpp`. The mobile background path destroys the surface and swap chain (`LifecycleWatch`, `:169-197`).

**Metal:** watches `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` and raises `settingsChanged` (`backend.mm:887-895`). It skips a frame when the size is 0 (`:1168-1171`) and handles a nil `nextDrawable` (`:730`).

**DPI:** Windows runs DPI-unaware (`app/main.cpp:1068`, `SDL_WINDOWS_DPI_AWARENESS=unaware`), so the OS scales the window. macOS uses `HIGH_PIXEL_DENSITY` (`platform/darwin/window_darwin.cpp:56`). Vulkan has no `DISPLAY_SCALE_CHANGED` handling and relies on `PIXEL_SIZE_CHANGED` → WM_SIZE → `Device::SizeWindow` (`engine/video/device_window.cpp:63-80`).

**Suspicious sites**
1. `vulkan/device.cpp:1392-1397` together with `:555`: `ApplyEngineSettingsChange` returns early while the window is minimised, but `BeginCommands` then clears `settingsChanged` anyway, so a resize or Options Apply made while minimised is lost. `SizeWindow` will also have written a 0×0 back buffer at minimise. It only recovers if the restore's WM_SIZE differs.
2. `vulkan/device.cpp:1413-1425` and `:1472-1476`: the `swapchainDirty` rebuild (from OUT_OF_DATE or SUBOPTIMAL) calls only `CreateSwapchain`. It does not update `dev->backBufferWidth/Height` or `RecreateDynamic`. Until the WM_SIZE-driven `settingsChanged` arrives, the frame renders a back-buffer-sized viewport into a swap image of a different extent: a render-area or viewport mismatch on the non-present path.
3. `vulkan/device.cpp:169-191`: `LifecycleWatch` is an `SDL_AddEventWatch` callback. On Android, SDL delivers `WILL_ENTER_BACKGROUND` from the Java thread, not inside the pump, despite the "Main thread" comment. It waits up to 2 s for `framesOpen == 0`, then tears down "anyway" (`:176`) while the render thread may be mid-frame. `swapchain`, `surface` and `swapchainDirty` are plain, non-atomic globals.
4. While minimised on Vulkan, `BeginCommands` runs `vkDeviceWaitIdle`, a surface-caps query and a failed `CreateSwapchain` on every loop iteration (`:1413-1425`), with no throttle beyond the frame cap.
5. DX11 (faithful, flagged only): `ResizeBuffers(1, …)` against a swap chain created with 2 buffers (`devicedata.cpp:864`); `ResizeTarget` with format 28 (RGBA) against BGRA buffers (`:840`); `Present` returning `DXGI_STATUS_OCCLUDED` falls through `GetDeviceRemovedReason == S_OK` with no throttle (`contextdata.cpp:660-668`). None of these is listed in `docs/retail_bugs.md`.

---

### (c) Unchecked HRESULT / VkResult

**Method:** a Python scan of statements whose value is discarded. False positives (`Array::GetData`, calls inside an `&&` chain) were removed by hand.

**DX11 / Windows / ext / app:** about 177 HRESULT-returning COM call sites, **21 discarded**.
- 10 `Create*`:
  - `buffers.cpp:500`
  - `imagebufferdata.cpp:396, 456, 497, 939`
  - `renderstatedata.cpp:419-421`
  - `vertexlayoutdata.cpp:209`
  - `async_gpu_create/async_create.cpp:112` (ext)
- 3 `Map`: `buffers.cpp:507`, `imagebufferdata.cpp:895, 948`.
- 8 others:
  - `contextdata.cpp:523` `GetBuffer`
  - `devicedata.cpp:1078` `SetFullscreenState`
  - `devicedata.cpp:1260` `MakeWindowAssociation`
  - `devicedata.cpp:1244, 1252` `CheckFormatSupport` (the `support` output is tested, so benign)
  - `devicedata.cpp:1753` `CheckMultisampleQualityLevels`
  - `better_water/scene_depth.cpp:121, 254` `QueryInterface` (ext)

Most retail-path sites carry an explicit "the original ignores this HRESULT … reproduced" note (`buffers.cpp:494-499`, `imagebufferdata.cpp:907-913`, `contextdata.cpp:513-518`).

**Vulkan:** about 101 result-returning `vk*` calls (void and pure-query functions excluded), **45 discarded**. Only 3 go through `NoteResult(vk…)`. 33 of the 45 are in `device.cpp`.

| Kind | Count | Sites |
|---|---|---|
| Enumerate / count pairs | 15 | |
| Command-buffer begin/end | 9 | `device.cpp:726, 747, 1457, 1520`; `draw.cpp:1823, 1832`; `resources.cpp:224, 238, 414` |
| Pool / fence resets | 7 | |
| Create (no check) | 6 | `device.cpp:308` messenger, `:521` swap image view, `:523` `renderFinished` semaphore, `:663` frame fence, `:665` `imageAvailable`; `resources.cpp:215` command pool |
| Command-buffer allocate | 2 | `device.cpp:741`, `resources.cpp:220` |
| `vkDeviceWaitIdle` | 4 | `device.cpp:180, 579, 1220, 1414` |
| Surface-support query | 2 | `device.cpp:1066, 1409` |

**Most suspicious**
- `vulkan/device.cpp:521-523` and `:663-665`: a failed view, semaphore or fence leaves `VK_NULL_HANDLE` in the frame's sync objects, and the next wait or submit then fails in confusing ways.
- `vulkan/device.cpp:1414` and `:180`: `vkDeviceWaitIdle` returning `DEVICE_LOST` is ignored, so the loss is only noticed later.
- `dx11/imagebufferdata.cpp:895`: a dynamic-texture `Map` is unchecked and is followed by a memcpy to `mapped.pData`. On a removed device that is a null write on the loader thread. Faithful.
- `dx11/imagebufferdata.cpp:456/497`: unchecked RTV/DSV creation on resize, which ends in a null bind later.

---

### (d) Constant-buffer / UBO layout

- **Retail shaders** are DXBC, converted to SPIR-V at load by dxbc-spirv (`vulkan/shader_convert.cpp`; cb register limit at `:87`, `maxCbvSize` 4096 vec4 at `:174`). The cbuffer stays the D3D vec4 array, so no std140 re-packing happens. Constants are copied from a byte shadow (`companions.cpp:649-667`, clamped to `kConstantBufferRange`). Metal uses the same DXBC route (`airconv_abi.h`). No C++ mirror of retail cbuffers exists, so there is nothing to drift.
- **OpenBA-authored constants**
  - ASSAO, 240 B. `dx11/ao/assao/ASSAODX11.cpp:123` has **no static_assert at all**. The Vulkan (`vulkan/ao/assao_pass.cpp:100-133`) and Metal (`metal/passes/assao_pass.mm:34-48`) copies are separate hand-written structs with **sizeof-only** asserts and **no offsetof**, three copies kept in step by comment ("Byte offsets match ASSAOConstants in ASSAO.hlsl").
  - Better water: `ext/better_water/shore_params.h:42,66,89` assert 64 B each (four float4s), sizeof only. `better_water.cpp:347,379,409` check that the staging size matches.
  - Vulkan present push constant: `present.cpp:41` `Transform{float a[4]; float b[2];}` = 24 B against GLSL `vec4 a; vec2 b;` (`present/present.vert:8`). The layout matches, with no assert.
  - `vulkan/pipelines.h:164` asserts `StateBlock` is 28 B with no padding, because it is hashed as bytes.

**Suspicious:** the three ASSAO struct copies (a field reorder in one compiles silently), and the lack of offsetof asserts on any GPU-visible struct.

---

### (e) SDL init/shutdown, controllers, event threads

- **Init:** `SDL_Init(SDL_INIT_VIDEO)` only (`app/main.cpp:1079`, second entry `:1191`). Audio is a subsystem (`platform/sdl/audio_sdl.cpp:230/239`). Drop events are disabled before the window is created (`main.cpp:1091-1092`).
- **Shutdown order:** `Platform::App::Shutdown` → `SDL_Quit` → `SDL_UnregisterApp` (`main.cpp:1120-1123`).
- **Vulkan instance and messenger are never destroyed** ("kept for the process", `device.cpp:1262-1264`). `SDL_Quit` therefore runs with a live `VkInstance`, and `SDL_Vulkan_UnloadLibrary` is never called. Shutdown destroys the device, then the surface (`:1251-1257`).
- **Controllers:** there is no gamepad or joystick init, open or hotplug code anywhere in `src` (0 hits). This class does not apply.
- **Event watches:**
  - Vulkan `LifecycleWatch` (`device.cpp:1069`) and Android `app_android.cpp:327` are never removed: no `SDL_RemoveEventWatch` in the tree.
  - Metal `OnWindowEvent` (`backend.mm:965`) only writes a flag.
  - `diag/darwin/input_trace_darwin.cpp:316` is diagnostic only.
- **Pumping:** `Platform::Pump` polls with `SDL_PollEvent` (`pump_sdl.cpp:234`). `Input::Poll` also calls `SDL_PumpEvents` (`input_sdl.cpp:178`, plus the posix / darwin / android twins), which is legal only on the video thread. ext.render_pipe moves frame work to a render thread, so the SDL calls in the backends need auditing: `SDL_Vulkan_CreateSurface` inside `BeginCommands` (`device.cpp:1404`), `SDL_GetWindowSizeInPixels` in Metal `BeginCommands` (`backend.mm:1166`), and `SDL_SyncWindow` / `AdjustDisplay` in Vulkan `ApplyEngineSettingsChange` (`:563-564`). On the render thread, all of these are off-main-thread SDL video calls.

---

### (f) Size arithmetic

- **Good:**
  - `vulkan/seeded_create.cpp:162-165`: `size_t` level sizes, `max(dim>>mip, 1)`, BC rounding `(w+3)/4`, `have < need` guard.
  - `vulkan/draw.cpp:1776`: read-back size in `VkDeviceSize`.
  - `vulkan/companions.cpp:526-531`: Update clamps to the image and uses `size_t`.
  - `common/compress_ao/bc4_chain.cpp:130`: `size_t`.
- **32-bit int:**
  - `dx11/imagebufferdata.cpp:898`: `static_cast<size_t>(width * height) * 4`. The product is int before the cast, and `RowPitch` is ignored (documented as retail).
  - `imagebufferdata.cpp:954-962`: `int dstPitch = width*4` and an `int srcOffset += RowPitch` accumulator.
  - `imagebufferdata.cpp:630-641`: `SysMemPitch = kFormatBytes * rowUnits`, BC row `levelWidth/4` (truncating, so a 2-px-wide BC mip has pitch 0 before the clamp to 4). The mip walk restarts per cube face using `GetLevelCount()`, while `desc.MipLevels = 0` means "full chain", so the subresource count can disagree with the D3D chain length.
  - `ASSAODX11.cpp:1635`: `int approxSize`, statistics only.
  - `common/bc4/bc4.cpp:177`: `int BlockRowPitch`.
- **Offsets:**
  - `companions.cpp:654-655`: `int32_t bytes = registerCount<<4` or `bankStride*(sizeBytes/bankStride)`, clamped and floored at 0.
  - `companions.cpp:172`: `writeCursor + sizeBytes` is int32 with no overflow or negative-size check.

None is realistic at game texture sizes, which top out around 4k. The exposure is negative or garbage sizes reaching `Lock` and `Update`.

---

### Validation and debug layers

- **Vulkan** (`device.cpp:264-309`): `_DEBUG` only. `VK_LAYER_KHRONOS_validation` is enabled when the loader lists it. The messenger takes WARNING|ERROR severity and VALIDATION|PERFORMANCE types; GENERAL is off. The callback prints every message unthrottled and returns `VK_FALSE`.
  - **No `VkValidationFeaturesEXT` and no layer settings:** sync validation, GPU-AV and best practices are **off** in code.
  - Sync validation is reachable only through `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1`, which `vulkan/README.md:133-143` mentions.
  - The README covers a MinGW-built layer via `VK_ADD_LAYER_PATH`. Messages go to `standalone_error.log` as `[Vulkan] validation …`.
- **DX11:** **never debug.** Both `D3D11CreateDevice` calls pass flags `0` (`devicedata.cpp:1230` main, `:1721` the MSAA probe). There are 0 hits for `D3D11_CREATE_DEVICE_DEBUG` or `ID3D11InfoQueue` in `src`. `dx11/README.md:472` only says the flag "becomes available". Comments that rely on "the D3D11 debug layer will complain" (`buffers.cpp`, the IndexBuffer subresource-as-offset quirk) are untested assertions.

### trace_oracle and backend parity

- **What it compares** (`tools/trace_oracle/README.md`): two RenderDoc captures of **our** exe, dumped by `tools/dx11_state_diff/dump_state.py`.
  - The dump is D3D11-only: `GetD3D11PipelineState()` at `:424`; shaders keyed by DXBC sha1, textures by desc + mip-0 hash, buffers by length.
  - Six screens. The verdict-bearing ones are S3 (per-field diff, floored by a same-build control pair), S4 (sampler `maxLOD == mips-1`, SRV mips and slices against the bound texture) and S6 (VB/IB stride set).
  - Constant-buffer contents are never compared. S1, S2 and S5 are within the noise floor.
- **DX11 vs Vulkan parity is not supported today.** It is feasible with moderate work:
  - A Vulkan dumper via `GetVulkanPipelineState()`.
  - Mapping SPIR-V modules back to their source DXBC hash; the shader cache is keyed by DXBC (`KBSV` payload, `shader_convert.cpp:28`).
  - Descriptor set/binding → D3D slot translation (`shader_convert.h` binding layout).
  - S4 and S6 port nearly directly. S3 needs per-field equivalence tables (blend, raster and depth enums).
  - Expect deliberate divergences: per-pass MSAA resolve, mid-frame uploads hoisted, D32F on Metal. These are listed in `known_port_issues.md` "Vulkan backend".
- **The only cross-backend check today** is simulation-level, not rendering: `world.txt` for `BANISHED_SEED=12345` is byte-identical between the DX11 exe and Vulkan/lavapipe (`vulkan/README.md:78,207,253`).