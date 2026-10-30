# Stress-town optimisation log (2026-09-03 → 2026-09-04)

Everything tried against the `make sa-stress` town (medium map, seed 31337, 800 citizens in
their own houses, speed 10, every workplace staffed). Headline metric is **sim ms per 60 Hz
iteration** (`update_ms_per_iter` in the CSV): **0.39 → 0.24 (−38%)** over 20 measured
iterations. Run-to-run noise on this machine is about ±0.01, so anything inside that band is
listed as "no measurable gain". Each row names the kill-switch or knob where one exists.

Prerequisites that had to be closed first (not perf work, but the town was unmeasurable
without them):

- **Population collapse at 10x** — the stress siting walled in house access cells; the
  faithful home re-validation evicted residents, and the homeless never eat. Fixed in the
  siting (door offsets learned from the first placed instance, whole-map pocket test,
  `BANISHED_STRESS_ALLOW_POCKETS=1` reproduces the old layout). 802 → ~794 over 6000 frames.
- **Teardown heap corruption** — two out-of-bounds writers in the sim: `CF_MakeAreaWork`
  zeroing 8 bytes past the 0x60-byte TendArea, and `ConsumeProduce` bounding its
  produce-position loops by the citizen-slot count instead of the status-array count. Found
  with `BANISHED_REDZONE=watch` + `src/diag/hw_watch.cpp`.

## Worked (kept, retail-faithful)

| change | ms/iter after | notes |
|---|---|---|
| Paired double-polynomial sin/cos for the character heading (`src/game/Component/character_trig.cpp`) | 0.39 → 0.36 | MinGW's `sinf`/`cosf` go through x87 long double (`__sinl_internal`); retail's CRT is a double polynomial rounded to float. **Superseded 2026-10-30:** the Cephes polynomial was not bit-identical to retail; the move tick now calls the ported CRT bodies `MsvcLibm::Sinf/Cosf` (also double-precision SSE2, no x87) and `character_trig.cpp` and `BANISHED_FAST_TRIG` are gone. |
| No `std::nearbyint` / `floorf` libcalls in per-tick code (trig reduction, `MapQuery_SampleHeight`) | 0.36 → 0.31 | `nearbyint` alone was 12% of the EntityList phase. Check `objdump -d \| grep call` on anything that runs per tick. |
| Particle un-stamp seen-set: generation-stamped hash table instead of a 64-entry linear array, plus a once-per-render-frame early-out per component | 0.31 → 0.30 | The old array was also a silent **cap**: houses 65+ never got their smoke un-stamped. Behaviour fix as much as perf. |
| **Register dispatch slot 0x140151f30 → Runtime `MeshInstance::_rootOnlyUpdates`** | 0.30 → 0.25 | Retail `GameUpdateLoop::Update` writes `*slot = (iteration < N-1)` every speed iteration; the move tick (0x1401094c0) reads it and skips terrain + picking, and `MeshInstance::Update` goes root-only. Unregistered, the flag stayed false and speed 10 did ten full updates. This was a faithfulness gap, not a tuning. |
| RDTSC instead of `QueryPerformanceCounter` in the per-component profiler; `[HomeLoss]` watch made opt-in (`BANISHED_STRESS_HOMEWATCH=1`) | 0.25 → 0.24 | Harness overhead only; `BANISHED_STRESS_COMPPROF=0` now measures the same number. |
| Process-wide getenv cache (`standalone/env_cache.h`, force-included via `-include`) | within noise | ~370 `getenv` sites in the port, many per-tick diagnostics; sampling showed getenv+strcmp at 20% of the CitizenList phase and ~1% of EntityList. Env changes must go through `_putenv_s` (wrapped, invalidates). |
| Compile-time no-op `InternalBasePtr::CheckPtr` in the perf build (`-DBANISHED_CHECKPTR_STATIC_NOP`) | within noise | The shipped Runtime body is `C2 00 00` (RET 0); the resolver verifies it and warns if not. The runtime form (`g_checkPtrIsNop` flag, `BANISHED_CHECKPTR_NOP=0`) stays for the other builds. Exe shrank 1.3 MB. |
| Memoised the character-manager cell-level divides (`CharMgr_sub_8660`) | within noise | Pure function of (radius, worldSize, levels); cached values are the identical floats. |
| Bit-identical inline copies of trivial Runtime math exports (Vector3 ctor/splat/Dot, Vector2::Dot, Box::Intersect, Scalar::Max) at the hot sites | within noise | Transcribed from the Runtime decompiles including Dot's operand order. Non-trivial helpers (Normalize, Heading, RootAnimationOffset) still call the DLL. |
| Cached two per-tick `ResolveAddr` lookups of .rdata floats in the town-manager tick | within noise | |

## Didn't work (measured, reverted or left off by default)

| change | result | why |
|---|---|---|
| Successor prefetch in the update-tree walk, one level | 0.26 | Extra `TreeSuccessor` walk costs more than the miss it hides. Left in behind `BANISHED_TREE_PREFETCH=1`, default off. |
| Two-level prefetch (next node's component objects + node-after-next's entity) | 0.27 | Same, worse. |
| `-O3`, `-O2 -flto`, `-O3 -flto -march=x86-64-v2` (`make standalone-perf PERF_EXTRA=...`) | all 0.24 vs 0.24 at -O2 | Within noise; perf build stays at `-O2 -g`. Note: the first attempt measured four identical -O2 builds because the Makefile patch had failed silently — grep the flag before spending runs on it, and `rm -rf standalone/build-perf` between flag sets (make does not track flags). |
| Tree-walk corruption guards off (`BANISHED_TREE_GUARDS=0`) | 0.24 | No measurable cost; guards stay on. |
| Skipping the `CheckPtr` indirect call at runtime (`g_checkPtrIsNop`) | 0.39 → 0.39 | 60k calls per frame of an indirect RET cost nothing measurable; only the static form removes the flag load, and that too is within noise. |

## Would work but is not retail-faithful (not done)

These are the levers that remain if the 50% target ever has to be met. Each one changes what
the game does per tick, so none was applied.

- **Skip per-tick animation / picking / terrain for off-screen characters.** The move tick's
  Runtime calls (`MeshInstance::Update`, `Picking::Scene::SetTransform`, terrain height,
  CharMgr cell update) are ~25% of the EntityList phase and retail does them for every
  character every tick regardless of visibility.
- **Throttle the character move tick or the ModelComponent animation tick** (e.g. every
  other iteration at high speed). Retail's only throttle is the root-only flag, which is now
  wired.
- **Cache the update-tree walk as a flat array** instead of walking the BST each iteration.
  Retail walks the tree; a flat cache changes insert/remove ordering semantics within a
  frame (`StartEmitter`'s re-bucket registers entities mid-walk).
- **Reduce the storage path-graph rebuild** (7 A* pair costs per tick for 300 s every
  1800 frames at 10x, the CitizenList phase's spikes) by caching pair costs across rebuilds.
  Retail recomputes them.
- **Reorder float math** in `CharMgr_sub_8660` / the heading smoothing to use reciprocals or
  fewer divides. Not bit-identical; cell assignment could differ at boundaries.

## Where the remaining 0.24 ms goes

From the sampling profiler (`BANISHED_SAMPLE=1000`, `tools/sample_report.py --tag 4`), all
checked against the retail decompiles before being left alone:

- update-tree walk pointer chase (node → entity → component array → component → vtable) ~12%
- character move tick ~25% (CharMgr cell registration, action executor, Runtime transform /
  animation / picking calls)
- MoveTo enter's spatial proximity query ~7% (9 KB walker init + quadtree DFS, as retail)
- ModelComponent tick → `MeshInstance::Update` / `AnimationState::Update` ~10%
- residence heat ticks, ambient-environment cell scans, particle node-follow ~8%
- bucket-rate component updates (CitizenAI, Citizen, StateMachine) ~15%
- 300 s storage path graph (CitizenList phase) ~5%

## Tooling added

- `BANISHED_SAMPLE=<hz>` sampling profiler (`src/diag/sampler.cpp`) + `tools/sample_report.py`
  (self / inclusive / `--callers` / `--callees`, per SIMPROF phase). Runtime frames are
  attributed to the nearest DLL export — verify a hot offset in Ghidra before trusting the
  name. Frames beneath a ucrtbase leaf are garbage (unwinder fallback).
- `BANISHED_REDZONE=watch` allocator watchdog (no shim) and `src/diag/hw_watch.cpp` hardware
  write watchpoints.
- `make standalone-perf PERF_EXTRA="..."` for flag experiments.
- Baselines: `perf/baselines/current.csv` = final 0.24 run; `2026-09-04_pre-perf-wave.csv` =
  the 0.39 run; `2026-09-03_800x10.csv` = the original 0.51 run with the collapsing town.
