# P3 Semgrep + variant analysis: baseline (2026-11-03)

Tree: `review/sanitizer-builds` at bfe14e5fe (main 033dbca08 + the sanitizer plumbing). Semgrep 1.183.0
(OSS, `--metrics=off`, no login). Rules: `static/semgrep/*.yml`, one file per class (C4/C5/C6 and
C9/C13/C15/C17/C18 share two survey files). Most rules are `pattern-regex`: Semgrep's C++ parser
partially fails on 25 of 2241 files (PartialParsing in starttown.cpp, saveload.cpp, townhallui.cpp,
devicedata.cpp, vulkan/device.cpp and others), and structural `...` patterns miss across those.
`.mm` (Metal) files are not scanned by `languages: [cpp]`; Metal was checked by ripgrep for C11/C12.

Reproduce:

```bash
semgrep scan --metrics=off --config docs/history/review_20261103/static/semgrep/ --json -o out.json src
```

## Totals

| Rule | Hits | Triaged | Real (finding) |
|---|---:|---:|---:|
| c01-static-container-of-entity-pointers | 10 | 10 | 0 |
| c01-container-keyed-by-entity-pointer | 4 | 4 | 0 |
| c02-global-mode-hold | 2 | 2 | 2 (P3-03) |
| c02-global-mode-name | 3 | 3 | (same sites) |
| c02-global-mode-raii | 1 | 1 | 1 (P3-03, harness-only caller) |
| c03-null-state-registration | 2 | 2 | 0 (both match retail) |
| c03-perframe-return-literal | 19 | 19 | 3 sites of P3-04 (C6 shape found through it) |
| c04 / c05 / c15 / c18 survey rules | 0 | - | - |
| c06-null-return-at-entry (survey) | 308 | 0 (sampled only) | - |
| c07-serialize-through-cast | 1 | 1 | 0 (ruled, `CAST-OK: byte_io`) |
| c07-reinterpret-ref-scalar | 2 | 2 | 0 (retail's uint slot, commented) |
| c08-32bit-stream-offset | 0 | - | - |
| c08-snprintf-return-as-length | 11 | 11 | 2 sites of P3-01 |
| c08-snprintf-return-used | 13 | 13 | 0 |
| c09-clearstate | 8 | 8 | 0 |
| c09-om-get-backup | 17 | 17 | 0 |
| c10-resize-result-ignored | 3 | 3 | 0 (retail OnActivate) |
| c11-cv-wait-survey | 43 | 25 | 0 |
| c12-detached-thread | 4 | 4 | 2 sites of P3-02 |
| c12-join-in-destructor | 2 | 2 | 0 |
| c12-thread-captures-this | 2 | 2 | 0 |
| c13-deepcopy-running-cursor | 9 | 9 | 0 (inside the entry's own extent; batches use MarkBatch offsets) |
| c16-bare-libm-in-sim | 44 | 44 | 0 |
| c16-std-sort-in-sim | 8 | 8 | 0 |
| c17-null-this-test | 1 | 1 | 0 (comment) |
| c17-win32-ifdef-in-core | 7 | 0 | (P0 lists them; not this phase) |
| **custom total** | **524** | **198** | **4 findings** |
| public r/c (73 rules; 16 C-only rules re-targeted to cpp) | 15 | 15 | 0 |
| public p/c (2 rules, C files only) | 0 | - | - |

`p/default` (1073 rules) was downloaded but not run: almost all are other languages. `--config auto`
needs a login and was skipped. `r/cpp` is empty in the registry.

## Findings (proof in the ledger form)

The two replays are in `static/semgrep/replays/` (built and run in WSL under `~/tidy/p3/`).

- **P3-01** `src/diag/scene_watch.cpp:717-721` (and `:733-737`, the `vb2[320]` line): `off += snprintf(buf + off,
  sizeof(buf) - off, ...)` with no clamp. Once one level truncates, `off > 512`, `sizeof(buf) - off` wraps to
  ~2^64 and the next call writes past `buf`. The census itself tests for distances above 1e6 (line 725), and
  `%.1f` of such a distance is ~31 characters, so 8 levels exceed 512 bytes. Proof: ASan replay in WSL
  (`~/tidy/p3/lodcensus_overflow.cpp`, scratchpad copy), `stack-buffer-overflow ... WRITE of size 87` in
  `vsnprintf` from the 6th level. Label harness (`BANISHED_LOD_CENSUS`). Fix: stop the loop when
  `off >= (int)sizeof(buf)` (as `redzone_alloc.cpp:144` and `stress_harness.cpp:1217` already do).
- **P3-02** `src/render/vulkan/async_gpu_create/async_create.cpp:41-66` and the DX11 twin
  `src/render/dx11/async_gpu_create/async_create.cpp:70-73,120-124`: namespace-scope `std::mutex` /
  `std::condition_variable` / `std::deque` with detached workers parked in `g_workCv.wait`. When `main`
  returns, `exit()` destroys the condition variable with blocked waiters (UB, [thread.condition.condvar]:
  "no thread is blocked on *this"). Proof: replay (`async_create_exit.cpp`) built with clang-19 on glibc 2.39
  hangs at exit (`timeout 10` -> 124); the same replay exits 0 on Windows (silent UB). The same shape was
  fixed in `src/platform/common/event_std.cpp:30-42` (immortal heap objects: "the Linux exe hung in
  QUITTING on exactly that"). Label ext (`ext.async_gpu_create`, default off). Fix: heap-allocate the shared
  state and never destroy it, as `event_std.cpp` and `async_save_compress.cpp:93-95` do.
- **P3-03** C2 residual holds of `g_wpcRosterOnly` after 033dbca08: `PathGraph::ReassignWorkers`
  (`citizen_reassign_workers.cpp:241-308`, retail 0x1400d62a0), `StartTown_AssignFoundingHousing`
  (`starttown.cpp:4020-4106`), `CitizenBehavior::AddWorker` (`citizen_employ_cascade.cpp:465-470`, retail
  0x140091130, only caller `src/diag/builder_seat_harness.cpp:99`), and a dead `extern` at
  `buildcomponent_state8.cpp:75`. Trace: the two gameplay/build holds only reach `CitizenComponent::SetHome`
  (0x1401163d0) -> `ResidenceComponent::RemoveResident` / `AddResident`; neither reaches
  `WorkPlaceComponent::Attach` / `Detach`, the only readers (`workplacecomponent.cpp:116,214`). So today the
  holds change nothing, but they are port-only state across retail bodies (the 033dbca08 shape): any future
  path from SetHome to a workplace re-home would silently skip the "no workers" icon again. Label port,
  static_only. Fix: delete the holds and the extern, as 033dbca08 did for the other three.
- **P3-04** C6 port-only null guards in the boat states (found by the C3 return-literal rule):
  `BoatComponent::Steer` (`boatcomponent_states.cpp:222-223`, `if (!mp) return 0;`), `State0Tick` (`:443`),
  `State1Tick` (`:478`), `State3Tick` (`:562`), through `ModelPos` (`:130-133`, "see the guard note in
  Steer()": there is no such note). Ghidra: retail 0x1400463d0 (Steer) and 0x140045250 (State0Tick) read
  `[boat+0x30]+0x158` with no test. In the port a null model makes Steer return 0, so State0Tick deletes the
  boat and posts event 0xb (merchant lost): behaviour retail does not have. Label port, static_only.
  Per the hard rule, the null model should be traced upstream rather than guarded, or the guard justified
  as a standalone difference in a comment.

## Leads closed

- C3 open leads (P0 map): `buildcomponent.cpp:306` state 2 and `destroycomponent_states.cpp:999` state 7
  are registered with null handlers in retail too. Ghidra: 0x140109090 calls `0x14002f500(2, 0, 0, -, 0)`
  (EDX/R8D zeroed, `[RSP+0x20]=0`, R9 never set), then `0x14002f620(3, 0)`; 0x14011dd00 calls
  `0x140033e50(7, 0, 0, -, 0)` then transitions 0, 8, 5 and events 0 (d2a0), 1 (d1a0). Both ports match,
  including transition and event order. Not findings.
- C2 `buildcomponent_state8.cpp:75`: only an unused `extern` (folded into P3-03).
- C1: `family_tree` tables (`s_idOf`, `s_citizen`, `s_cause`) are cleared by `OnRemovedSlow` -> `Unlink`, by
  `ForgetAll` on row-off and on tree replacement; `citizen_button.cpp` `g_button` by panel close and
  `CitizenButtonsForget`; `render_culling` `s_culledModels` by `ForgetModel`; `drain_split` `s_seen` is
  harness-only and cleared each window; `starttown.cpp` founding vectors cleared at each spawn
  (`starttown.cpp:5337-5340`); `entitylist.cpp` `s_walkSnap` rebuilt on the dirty mark; `tool_names`
  `g_live` keys are never dereferenced and are pruned. Minor leftover: `s_cause` keeps an entry when
  `IdOf(citizen) == 0` (`family_tree_town.cpp:320-327` returns before the erase); at worst a later citizen
  at the same address inherits a death cause; not proven reachable, not a finding.
- C7: `consumeproduce.cpp:180` (`Serialize(bool&)` of a `uint8_t`) carries the S4 ruling; `layoutdescription.cpp:149-150`
  is retail's uint slot for an int enum.
- C8: the other nine accumulating `snprintf` sites are bounded (`componententity.cpp:441` n < 400 with at most
  ~29 bytes per write into 512; `redzone_alloc.cpp` and `stress_harness.cpp` test the room first;
  `terrain_edit/toolbar_page.cpp:107` checks `length < sizeof text`). The 13 "return used" sites clamp or
  test it; `input_trace_darwin.cpp:62` cannot exceed 640 (body <= 511, short literal tags).
- C9: `better_water/scene_depth.cpp` `ResolveStateBackup` saves and releases every binding
  `DepthResolve::Run` touches (`ao_depth_resolve.cpp`); `ao_pass.cpp` is the 228bfc24b fix.
- C10: `devicedata.cpp:928/935` is retail's `OnActivate` (commented transcription); `:866` tests the result.
- C11: read in full: `lazy_table.cpp` (8 waits; `DrainQueue` would hang while paused but has no caller outside
  tests, callers use `DrainQueueUnlessPaused`), `render_pipe/handoff.h` (4), `async_save_compress.cpp` (3),
  both `async_create.cpp` (8), `derived_cache.cpp:778/796` (the 97233ff0e fix). No request flag that the
  woken side clears was found. Not read: `parallel_for`, `gather_threads`, `path_workers`, `bc4_chain`,
  `block_cache`, `load_decode_batch`, `metal/pipeline_cache.h`, `vulkan/pipelines.cpp:814`,
  `derived_cache.cpp:920/922`.
- C12: `block_cache.cpp` and `lazy_table.cpp` destructors raise `stop` under the lock before joining; the
  `bc4_chain` pool and `async_save_compress` state are heap objects never destroyed.
- C16: all 44 libm hits are `sqrtf` (correctly rounded), `MsvcLibm::` calls, or comments, except
  `gameupdateloop_load.cpp:547/550` (`tanf` / `atanf` for the camera FOV: render-only, not sim state).
  The game-side `std::sort` (`citizen_assign_citizens.cpp:539`) sorts a strict total order, so it equals the
  tree's in-order walk; the engine hits are draw order, settings and audio batches.
- Public `r/c`: 10 `memset` hits (zeroing PODs, not secrets) and 5 `atoi` hits, 4 of them on harness flag
  strings (`market_overstock_harness.cpp:72-85`, `gameupdateloop.cpp:1444`); `atoi` overflow on an absurd
  flag value is UB in a harness path only. Not filed.

## Not done in this phase

- c06 (308 entry null-returns) needs a per-hit retail comparison; P8 sweeps own it. P3-04 is the one family
  checked against Ghidra here.
- c17 `_WIN32` sites are already inventoried in P0 section C17.
