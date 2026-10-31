# Stress test + performance baseline

`BANISHED_STRESS=1` turns a fresh autostart new game into a stress town and measures it.
Two purposes: exercise large-scale interactions (hundreds of citizens, every building
type, every profession, full storage/market/trade loops running at once), and give
optimisation work a number to move.

## Run it

(The release exe is profile-guided: `perf/pgo/README.md`, `perf/pgo/train.sh`. The perf flavour measured
here stays plain so the baselines compare like with like.)

```bash
tools/build.sh stress                                    # perf build, 800 citizens @10x, ~2.5 min
STRESS_ARGS="--label wip --speed 5 --citizens 600" tools/build.sh stress
tools/build.sh stress debug                              # -O0 debug exe: functional check only
tools/stress_run.sh --help                               # all knobs
```

`tools/build.sh stress` builds `build/cmake-perf/OpenBA.exe` (`-O2 -g`, logs stripped, its
own build directory so it never mixes with the debug objects), deploys it, runs the town to
completion and leaves everything under `perf/results/<timestamp>_<label>/`:

| file | what |
|---|---|
| `standalone_stress.csv` | one row per 600 frames (see columns below) |
| `standalone_stress_compprof.txt` | per-component-type Update cost, every window |
| `standalone_error.log` | the run's stderr (`[Stress…]`, `[SIMPROF]`, `[Stress::staff]`) |
| `report.txt` | `tools/stress_report.py` output, incl. the diff against the baseline |

The run ends itself: the harness opens the in-game pause menu at `BANISHED_STRESS_FRAMES`
(default 6000 sim frames = 100 s at 60 Hz = 60 000 sim ticks at the default 10x) and clicks Quit Game, so the process exit is
a clean `Engine::Shutdown`. A crash leaves `standalone_crash.log` and the script exits 2.

## What the town is

Built by `StartTown_StressSpawnStructures` (`src/game/Game/starttown.cpp`) on top of the
normal Easy founding town, on a **medium (384) map**, seed 31337 (12345's town area is a
~11k-cell pocket between river and mountains; 31337 has ~27k connected cells):

- **3 sets** (`BANISHED_STRESS_SETS`) of every ordinary placed building: storage barn +
  yard, market, well, school, hospital, chapel, cemetery, cutter, forester, herbalist,
  toolmaker, tailor, tavern, iron mine, quarry, hunter lodge, gatherer hut, hostel; one
  town hall.
- **One each** of the buildings that need special siting, via their existing demo blocks:
  fishing dock and trading post (river), crop field, orchard, pasture (drag areas), plus a
  second density-sited hunter lodge and gatherer hut.
- **Houses** for the target population (`BANISHED_STRESS_CITIZENS`, default 800):
  alternating wood/stone, one per family, target/3 + 2 of them.
- **Citizens**: one family (2 adults + 0..3 children) per stress house, spawned at the
  house through the retail founding-spawn path, housed by the founding-housing pass.
- **Jobs**: from world frame 600 a staffing pass reads every workplace's authored
  `_defaultWorkers` and seats that many through `CitizenEmploy_SeatWorkers`, the same
  cascade a Town Hall spinner click runs. Retried every 300 frames (workplaces that open
  late get staffed late), then every 1800.
- **Food**: the seven start-stock counts are multiplied by citizens/10 for the seeding
  call, but the barns' volume caps what lands (~23k edible units in the four 6000-volume
  barns; a bigger `BANISHED_STRESS_FOOD_MULT` changes nothing). The town produces almost no
  food, so from world frame 300 a **food top-up pass** (`FoodPass` in
  `src/diag/stress_harness.cpp`) runs every 60 frames: when the town's storage locations hold
  fewer than 15 edible units per living citizen it adds the deficit into the food-accepting
  storage (StorageComponent::AddItem, CanAdd-checked, round-robin over the edible templates).
  Citizens still fetch it and carry it home. Each CSV row is followed by a `[Stress::food]`
  line: storage / house / other food, the units topped up, and each food store's
  food/used/capacity.

Every building is COMPLETED (instant-build bracket), sited 1 tile apart on workable
ground around the founding cluster. Placement is deterministic per seed. Siting rules
(2026-09-04, after the 10x population collapse was traced to walled-in house doors):

- **Door (access) cell**: learned per template from the first placed instance via retail's
  `Entity_impl_7fb0` (0x140087fb0). It is a walkable notch INSIDE the footprint rectangle,
  and `CitizenComponent::Update` re-validates every idle citizen's home by comparing the
  citizen's tile region against the door's region — a door facing water or another
  footprint evicts every resident, and a homeless citizen never eats.
- **Pocket test**: a whole-map flood from the town cell over free cells is kept up to date;
  a candidate is rejected if its footprint (door excepted) would cut any reachable cell
  off, or leave its own door unreachable (interior doors — yards, markets, fields — skip
  the door test). `BANISHED_STRESS_ALLOW_POCKETS=1` restores the old layout.
- The `[Stress::place] DONE` line reports `pocketRejects`, `doorWalled` and per-reason
  reject counts; `BANISHED_STRESS_KINDMAP=1` dumps the terrain kind map.

Env bundle applied by `StressHarness_Init` (only for variables you did NOT set):
`BANISHED_AUTOSTART=1 BANISHED_MAP_SIZE=medium BANISHED_SEED=12345 BANISHED_DIFFICULTY=easy BANISHED_CLIMATE=fair
BANISHED_FPS_CAP=off BANISHED_FORCE_SPEED=<speed> BANISHED_SIMPROF=<interval>
BANISHED_PAUSEMENU=<frames> BANISHED_PAUSEMENU_BUTTON=6 BANISHED_CAMERA_PIN=town,90` plus
the `BANISHED_BUILD_DEMO_*` flags above plus `BANISHED_CROPFIELD_FORCE_START=2
BANISHED_ORCHARD_FORCE_START=1`. Weather is a function of game time and the seeded RNG, so
the starting season repeats exactly for a given seed; if a seed starts in snow, pick another
seed rather than expecting variation.

## Why 800 citizens at 10x is the default

Measured 2026-09-03 (perf build, 6000-frame runs, steady state):

| town | update ms / frame | ms / sim iteration | notes |
|---|---|---|---|
| 400 citizens, 1x | 0.45 | 0.45 | first baseline |
| 400 citizens, 5x | 2.54 | 0.51 | |
| 400 citizens, 10x | 3.18 | 0.32 | per-iteration cost FALLS: animation and other per-frame work amortise over 10 ticks |
| 800 citizens, 1x | 0.66 | 0.66 | |
| 1200 citizens, 1x | 0.82 | 0.82 | ~+0.18 ms per extra 400 citizens |
| **800 citizens, 10x** (2026-09-03) | **5.10** | 0.51 | seed 12345, pre-fix town; population 802 -> 702 over 6000 frames |
| 800 citizens, 10x (2026-09-04 baseline) | 3.94 | 0.39 | seed 31337, door/pocket siting: population 802 -> 794 |
| 800 citizens, 10x (after the perf wave, 20 iterations) | 2.42 | 0.24 | see "Optimisation log" below; `perf/baselines/2026-09-04_post-perf-wave.csv` (the 0.39 run is `2026-09-04_pre-perf-wave.csv`); icons OFF |
| **800 citizens, 10x, faithful, the FED town** (2026-10-18, main `8339497d6`, machine CLEAN, mods parked; the harness stocks food since HITCH) | **1.65** | **0.16** | `perf/baselines/current.csv` = `2026-10-18_fed_faithful.csv`; hunger stays 2.8-10 where the unfed town starved; 24822 -> 25231 entities; hitch verdict PASS |
| 800 citizens, 10x, TRUE faithful (2026-10-12: every fix and extension off; PerfX is `ext.render_culling` / `ext.anim_batch` since PERFXEXT) | 1.83 | 0.18 | `2026-10-12_faithful_true.csv`, the baseline until 2026-10-18; the 2026-09-05 row below had PerfX on |
| 800 citizens, 10x (PerfX knobs, 2026-09-05) | 1.92 | 0.19 | see "Non-retail experimental knobs" below; now `perf/baselines/current.csv` (= `2026-09-05_perfx.csv`); icons ON |

Speed multiplies the same work; citizens change its shape (AI, pathing, hauling). The
default takes both. `BANISHED_STRESS_SPEED=1 BANISHED_STRESS_CITIZENS=400` reproduces the
gentler town when a 1x number is wanted. Two things the heavy default exposed, both CLOSED
2026-09-04: **the population collapse at 10x** (802 -> 702 by frame 6000, 96 by 18000) was
the stress siting walling in house access cells — see the siting rules above; it now holds
802 -> ~794 over 6000 frames with the remaining deaths being ordinary starvation of the
~1% whose door region still differs from their tile region. **The heap corruption during
teardown** was two out-of-bounds writers in the sim: `CF_MakeAreaWork` zeroing 8 bytes past
the 0x60-byte TendArea, and `ConsumeProduce` bounding its produce-position loops by the
citizen-slot count instead of the status-array count. Both were caught live with
`BANISHED_REDZONE=watch` (allocator watchdog thread, no shim) and a hardware write
watchpoint (`src/diag/win/hw_watch.cpp`). The run script still reports a post-quit crash as
"crashed during teardown" (exit 6) with the numbers intact.

## CSV columns

| column | meaning |
|---|---|
| `wall_s` | seconds since the world's first tick |
| `world`, `frame` | world number in this process, world-relative sim frame |
| `speed`, `sim_iters` | game speed, sim iterations in the window (= frames × speed) |
| `fps`, `frame_ms_mean/p95` | rendered frames per wall second; Draw-entry-to-Draw-entry period |
| `update_ms_mean/p95` | `GameUpdateLoop::Update` wall time per sim frame |
| `update_ms_per_iter` | update ms ÷ sim iterations: cost of ONE 60 Hz tick — the cross-speed, cross-build number |
| `draw_ms_mean` | `GameUpdateLoop::Draw` CPU time (submission; GPU time is not in it) |
| `ph_*_ms` | SIMPROF split of update ms: PairCache, Animation, EntityList, Weather, CitizenList, PostSim (UI/camera/audio) |
| `entities` | EntityList count (gameWorld+0xE8 → +0x10) |
| `citizens`, `home238`, `work248`, `busy` | town registry count; citizens with +0x238 / +0x248 set (home / workplace); AI with a non-empty action stack |
| `age_mean`, `hunger_mean` | population means (hunger climbing forever = nobody eats) |
| `ws_mb`, `private_mb` | working set / private bytes; steady growth is a leak |

### Pathfinding columns (PFBENCH, appended after `private_mb`)

Measured on the pathfinder worker per command-5 request (one RDTSC pair around the search
body in `ApplyCommand` case 5, one lock per request, nothing inside the search) and on
the main thread; percentiles are over the requests completed in the window
(`src/diag/pf_stats.h`). Design and gating: `docs/extensions/handins/PFBENCH.md`.

| column | meaning |
|---|---|
| `pf_requests`, `pf_req_per_sim_s` | requests completed in the window; per simulated second (frame `m_gameTime` step × speed) |
| `pf_search_us_p50/p95/max` | worker wall time of the search body per request |
| `pf_expanded_mean/p95` | nodes expanded per request, from the search's own state after the run (retail: inserts − entries still open = expansions + decrease-keys) |
| `pf_wait_us_p50/p95` | enqueue (`RequestPath`) → start of the search |
| `pf_queue_max` | deepest worker queue seen at a request's enqueue (all command kinds) |
| `pf_fail` | requests that returned 0 cells |
| `pf_worker_busy_pct` | summed search time ÷ window wall time |
| `pf_len_mean` | cells per path, over the requests that found one |
| `pf_e2e_us_p50/p95` | enqueue → `TakePath` (includes the walker's once-per-update poll) |
| `pf_ready_us_p50/p95` | enqueue → the worker's publish (`ready = 1`): queue wait + search + copy |
| `pf_poll_us_p50/p95` | publish → `TakePath`: the walker's poll. The citizen polls once per sim iteration, and `MainLoop::Frame` runs the iterations of one Update back to back on a fixed 1/60 s step, so a request made late in an Update waits for the next one (p95 ≈ 14 ms, independent of fps) |
| `pf_frames0/1/2/3p_pct` | share of requests taken within the same Update / the next / two later / three or more later |
| `pf_shadow_n`, `pf_shadow_equal_cost`, `pf_shadow_same_cells`, `pf_shadow_worse`, `pf_shadow_speedup` | RESERVED for `ext.fast_paths`'s shadow mode (`StressHarness_PathShadow`): 0 until an extension calls it; % equal cost, % identical cells, count of extension paths costing MORE (must be 0), retail ÷ extension search time |
| `pf_sync_wait_ms` | main-thread ms per frame inside the road-tool / boat `RunPathQuery` (request + spin) |
| `ph_pathdistance_ms`, `ph_fillzones_ms` | main-thread ms per frame inside `Map::PathDistance` / `Map::FillZones` |

`stress_report.py` prints them in a **PATHFINDING** section, with its own baseline table
(search p95, wait p95, worker busy %, …); a baseline written before these columns existed
(the committed `current.csv`) reads `n/a` there. The path-heavy town:
`STRESS_ARGS="--spread"` (or `BANISHED_STRESS_SETS=-3`) puts the sets' workplaces and
storage a quarter map west of the centre and the houses a quarter map east.

## Recording and using a baseline

```bash
STRESS_ARGS="--label baseline" tools/build.sh stress
cp perf/results/<stamp>_baseline/standalone_stress.csv perf/baselines/current.csv
git add perf/baselines/current.csv
```

Every later `sa-stress` diffs against `perf/baselines/current.csv` automatically
(`--baseline none` to skip, `--baseline path` for another). `stress_report.py` flags a
`REGRESSION` when a metric moves past its threshold (10 % for timing, 15 % for memory).
Only compare runs of the **same** town (citizens, sets, seed, map) and prefer
`sim ms / iteration` as the headline: it is independent of game speed and of the GPU.

Baseline hygiene: same machine, nothing else heavy running, perf build only, and let it
finish (the first two rows are warm-up and are excluded from the steady-state numbers).
A perfectly flat `fps` (e.g. 139.9 every row on a 144 Hz display) is the swap chain's vsync,
not the CPU: the frame limiter is off but Present still waits. `update_ms`, `draw_ms` and
the per-iteration cost are unaffected; only treat `fps`/`frame_ms` as meaningful when they
move. Committed baseline `perf/baselines/current.csv` (= `2026-09-03_initial.csv`): -O2, Fair
climate, 402 citizens, 200 stress buildings incl. 4 fields + 4 orchards, 14.3k entities, 215
employed: 0.45 ms per sim iteration, draw 1.4 ms CPU, EntityList ~75 % of the sim,
CitizenAI/Citizen/Character Updates dominate it; memory flat.
Keep old baselines as `perf/baselines/<date>_<commit>.csv` when you replace `current.csv`
so a regression can be bisected by re-diffing.

## Retail as the target

Retail cannot be driven by these env vars, but the same world can be put in front of it:
save the stress town from our exe (`BANISHED_PAUSEMENU_BUTTON=3` = Save, or the autosave
harness) and load that save in `Application-x64.exe`. Retail prints no timings, so its
number comes from an external frame-time capture (PresentMon / RenderDoc / the profile
build `Application-x64-profile.exe`). Until that is wired up, treat retail as "60 fps at
1x with this town on this machine" and the goal as: our `sim ms / iteration` low enough
that speed 10 still holds 60 Hz — i.e. under ~1.6 ms per iteration with the render frame
on top.

## Known gaps in the town

- **No roads, no bridges.** Roads are drag-tool tiles and bridges are multi-segment
  drag objects; neither has a placement path outside the player tools yet. Every stress
  building is sited only where its footprint centre path-reaches the town cell, so nothing
  is cut off by a river, but travel is over raw terrain (slower, and no bridge/tunnel
  traffic is exercised). Porting a scripted road/bridge lay-down is the next realism step.
- **One pasture, one dock, one trading post** (their siting is bespoke); fields and
  orchards are per set and switched on by the force-start harnesses.
- **No construction traffic.** Everything is instant-built; builders/haulers to sites are
  not exercised. `BANISHED_BUILD_DEMO_INDEX` can leave one founding building unbuilt.

## Sampling profiler

`BANISHED_SAMPLE=<hz>` (default 1000; `src/diag/win/sampler.cpp`) runs a thread that suspends
the sim thread on a timer, unwinds its stack and tags the sample with the SIMPROF phase.
Samples go to `standalone_samples.txt`; `tools/sample_report.py <file> --tag 4` (4 =
EntityList) prints self/inclusive time per function, `--callers X` / `--callees Y` the
edges. Exe frames are symbolised with addr2line, Runtime frames with the nearest DLL export
(verify a hot Runtime offset in Ghidra before trusting the name). Use it whenever the
`[COMPPROF]` table does not add up: COMPPROF only sees vtable dispatch, the sampler sees
libm, the Runtime and the walk itself. Sampling costs ~5% of update time; do not compare a
sampled run's numbers against the baseline.

## Hitches (HITCH, 2026-10-18)

The CSV's per-row means hide a stutter: a 24 ms frame once every 1800 frames moves
`frame_ms_mean` by 0.01. The harness therefore closes **every Draw-to-Draw period** (the
on-screen gap, which is what a hitch is) and holds it against the rolling median of the last
128 periods **of its class** -- MainLoop::Frame draws every loop iteration and Updates only when
the fixed-step accumulator has a step, so a draw-only period (~1 ms) and an Update period
(~8 ms at speed 50) are different populations; a period with k Updates (the cascade after a
long frame) is held against the one-Update median plus (k-1) more median Updates. A period more
than `BANISHED_STRESS_HITCH_MS` (default one 140 fps frame, 7.14 ms; 0 = off) over its baseline
is a row of `standalone_stress_hitch.csv`: the period, its baseline, the excess, the split
(update / draw / outside = Present + pump + limiter; `harness_ms` = the stress harness's own
CSV-row and staffing work, which is inside outside), the six SIMPROF phases, the main-thread
pathfinding brackets, the class medians of each of those (so a reader sees WHICH part grew),
the Update count and the markers (staff pass, CSV row, frame % 1800, multi-Update, new world).
`t0_ms`/`t1_ms` are the `[LoadTime]` clock the sampler stamps its lines with. The stress CSV
gains `frame_ms_max`, `hitch_n`, `hitch_excess_max_ms`, `hitch_excess_sum_ms` per row.

`tools/hitch_report.py <results dir>` (run by `stress_run.sh` after `stress_report.py`,
appended to `report.txt`) prints the per-window maxima, the excess distribution, the
**attribution** (for every hitch the part that grew most over its median: `cascade`, `ph_*`,
`update-other`, `draw`, `outside`, `harness`), the worst list and, for a sampled run
(`BANISHED_SAMPLE=4000 tools/build.sh stress`; `standalone_samples.txt` is copied), the functions
whose share inside the hitch periods is far above their share in the whole run. Verdict PASS =
no period over the criterion except the harness's own rows.

The sampler's timer is the high-resolution waitable timer since the same day: before it, a
"1000 Hz" sampler took 64 samples/s whenever nothing had raised the clock resolution (the
limiter is off in a stress run), so every pre-2026-10-18 sample report was 15x sparser than it
said.

A save (`BANISHED_SAVE_TEST=<frame>`) prints `[Stress::save]` lines in a stress run: the reference-id
memo's counts and the save's `SaveRestore` time, plus the worker's counters with
`ext.async_save_compress` (`docs/extensions/handins/HITCH_SAVE.md`). With `ext.background_save`
the line is `[Stress::save] background main=… worker=… frozenUpdates=…` (the main thread's share, the
worker's, the Updates the sim stood frozen); at `BANISHED_STRESS_COMPPROF=4` a `[BackgroundSave] verify`
line compares the image with a synchronous save (`docs/extensions/handins/HITCH_SAVESNAP.md`).

Acceptance for the hitch hunt (the user, 2026-10-18): **nothing more than one 140 fps frame over
its baseline**, at every speed; measured at 10x, 20x and 50x, since a wall-clock-periodic cause
and a sim-time-periodic cause show at different frame cadences (the 300 sim-second storage
path-graph rebuild is every 1800 frames at 10x, every 360 at 50x; a frame-counted harness fires
at the same frame at every speed). The first 12 periods of each class are blind (the median
needs them). Baseline rows: `perf/results/20261018_01*_hitch_s{50,20,10}*` (kept only on the machine that recorded them; results are not committed).

## Optimisation log (2026-09-04, 800x10, sim ms / iteration)

| step | ms/iter | note |
|---|---|---|
| baseline after the population + heap fixes | 0.39 | `perf/baselines/2026-09-04_pre-perf-wave.csv` |
| skip `InternalBasePtr::CheckPtr` (Runtime body is `RET 0`; `g_checkPtrIsNop`, kill-switch `BANISHED_CHECKPTR_NOP=0`) | 0.39 | no measurable change |
| paired double-polynomial sin/cos in the character move tick (`character_trig.cpp`; replaced 2026-10-30 by the ported CRT `MsvcLibm::Sinf/Cosf`, which retail matches bit for bit) | 0.36 | MinGW sinf/cosf go through x87 long double: 16.7% of the phase |
| no `nearbyint`/`floorf` libcalls in per-tick code | 0.31 | nearbyint alone was 12% of the phase |
| particle un-stamp seen-set: hash table + once-per-frame per component (the 64-entry array also silently CAPPED at 64 emits) | 0.30 | |
| **register dispatch slot 0x140151f30 -> Runtime `MeshInstance::_rootOnlyUpdates`** | **0.25** | retail sets it on every intermediate speed iteration; the move tick then skips terrain + picking and MeshInstance::Update goes root-only. Unregistered, so 10x did 10 full updates |
| RDTSC instead of QPC in COMPPROF, `[HomeLoss]` watch opt-in (`BANISHED_STRESS_HOMEWATCH=1`) | 0.24-0.25 | profiling overhead is now within noise (`BANISHED_STRESS_COMPPROF=0` measures the same); run-to-run noise is about ±0.01 |
| tried, no gain: `-O3`, `-flto`, `-march=x86-64-v2` (`make standalone-perf PERF_EXTRA=...`, all 0.24 vs 0.25 at -O2, within run noise); successor prefetch in the tree walk (`BANISHED_TREE_PREFETCH=1`, 0.26; two levels 0.27); tree-walk corruption guards off (`BANISHED_TREE_GUARDS=0`, 0.24) | 0.25 | |
| ELPERF (2026-10-12): `Map::PathDistance`'s region A* on a bucket open set reproducing the AVL's (f, LIFO) pop order (PATHDIST, byte-identical, shadow 126k / 0); the slot-8 walk over per-entity cached tick lists (WALKCACHE, identical ticks, shadow 155.7M / 0) | 0.17 -> 0.16 compact, 0.32 -> 0.29 heavy | `docs/extensions/handins/PATHDIST.md`, `WALKCACHE.md`; COMPPROF=4 runs the shadows |
| iterations 16-20, each within noise (0.24): compile-time no-op CheckPtr in the perf build (`-DBANISHED_CHECKPTR_STATIC_NOP`, exe 1.3 MB smaller); memoised CharMgr cell-level divides; bit-identical inline copies of the trivial Runtime math exports (Vector3 ctor/Dot, Box::Intersect, Scalar::Max) in the move tick, proximity query and MoveTo; process-wide getenv cache (`standalone/env_cache.h`, force-included — ~370 getenv sites, 20% of the CitizenList phase was getenv); cached per-tick ResolveAddr of two .rdata floats | 0.24 | **final: 0.24 ms/iter = 38% below the 0.39 baseline after 20 iterations; the 50% target was not reached** |

What is left is faithful work: the update-tree pointer chase (~12%), the character move
tick (~25%: CharMgr cell registration, action executor, Runtime transform/animation calls),
MoveTo enter's spatial proximity query (~7%), residence heat ticks, ambient environment
scans, and the 300 s storage path-graph rebuild (7 A* pair costs per tick, the CitizenList
phase's 1800-frame spikes). Each was checked against its retail decompile before being
left alone. Reaching 50% would mean doing less than retail does per tick.

2026-10-18 (HITCH_PATHGRAPH): the CitizenList spikes were NOT retail's work. They were the
drain at the end of that rebuild, where the port's AssignCitizens priced every idle citizen x
workplace commute with an exact PathDistance instead of reading the region-pair costs the
rebuild had just filled (retail 0x1400d6060), and the rebuild itself collected 5 residences of
278 (an append that tested the capacity). Both are fixed; the drain went from 13-25 ms to ~5 ms
and left the hitch list (`docs/extensions/handins/HITCH_PATHGRAPH.md`).

2026-10-18 (HITCH_POSTDRAIN): the EntityList rows at frames 15/21/33 (spawn), 1081-1083 (the
900 s birth wave) and ~4351 (every herd grows on one tick, as in retail) were mostly our own
`fprintf(stderr, ...)` traces: stderr is unbuffered and MinGW's fprintf writes it one character
per WriteFile (245 us for a 90-character line). Those sites now use `ErrLine`
(`src/diag/err_line.h`, one write per line, 3.6 us), and the three classes left the hitch list.
A trace in a sim path is a hitch candidate in every flavour: only printf is stripped
(`docs/extensions/handins/HITCH_POSTDRAIN.md`).

2026-10-18 (HITCH_STDERR): the same fix for the whole tree. `src/app/one_write_stdio.h` is
force-included into every TU, and routes `fprintf` / `std::fprintf` / `vfprintf` to stdout or stderr,
plus `printf` where not stripped, through one `fwrite` per call. The bytes are the same. `vfprintf`
and `printf` were per character too. The harness's own 600-frame row was mostly the `[COMPPROF]` dump
to stderr (~25 ms, now ~13 ms), and the frame-180 / 300 force-start rows were their trace: 20 harness
rows per 12000-frame leg dropped to 1-2 (`docs/extensions/handins/HITCH_STDERR.md`).

## 50x game speed (2026-09-05)

The speed table in `ToolbarDialog` (`src/game/Dialog/toolbardialog.cpp`, step 9 of the
ctor) is `{1, 2, 5, 10, 20, 50}` -- retail stops at 10. 20x and 50x are NON-RETAIL; the
speed-up/down buttons and `BANISHED_STRESS_SPEED` / `BANISHED_FORCE_SPEED` are table-driven
so nothing else changed. Results, 800x50, PerfX on, same build as the 10x numbers:

| | 10x | 50x |
|---|---|---|
| sim ms / iteration (healthy window, frames 600-1800) | 0.17-0.19 | 0.14-0.18 |
| update ms / frame | 1.7-1.9 | 7.1-9.2 |
| fps (cap off) | 139 | 110-131 |

50x holds 60 Hz with margin. The sampled profile (`--tag 4,8,6,1,3`) has the SAME shape at
both speeds -- the top 20 self-time entries are the same functions within a point: the
executor's action-node walk, `EntityList_TreeNodeHandler`, `MoveTo_Execute_Impl`,
`CharacterComponent_MoveTick`, then region A*/AVL, the spatial proximity query and the PerfX
helpers (`CachedVisible` + `BatchEntry` + `NeedsPerTickRootUpdate` ~6%). Inclusive: the
per-citizen move-tick chain 33%, `CitizenAIComponent_TransitionState` 12-15% (of which
`Pf_IsDstReachable` -> region A* 11%), the per-model `ModelComponent` slot-8 tick
(mis-named `ModelComponent_OnRemove` in the port) 6-7%. So there is no NEW bottleneck at
50x; the per-iteration cost is flat because every intermediate iteration still walks every
citizen and every model. The hottest single addresses are all the instruction after a
dependent load -- action node `+0x18`, component flags `+0x20`, the terrain cell byte
`cells[cur*0x30+0x29]`, the visibility-cache probe -- i.e. cache misses from pointer chasing
across ~800 citizens x 15k entities 50 times a frame. The next step-change is data layout
(hot fields of the per-citizen chain contiguous), not more batching.

**The stress town starves at ~100k ticks regardless of speed.** At 50x that is frame 2400
(citizens 802 -> 333 -> 4 by frame 6000); the 10x control over 30000 frames collapses at
the same tick count (frame ~10000). Hunger mean climbs 3 -> 20 and every burial line is
`cause=0(starvation)`: only 215 of 800 are employed and the starting stock runs out. This is
the town, not a speed defect, and it means a 50x run's "steady state" past frame ~2000 is a
dead town -- use `--frames 1800` at 50x, or the first three CSV rows.

**FIXED 2026-10-18 (FOOD): the food top-up pass** (see "What the town is"). The cause was
capacity, not access: the seeded food is capped by barn volume, citizens carry it home within
~30k ticks, the houses run dry by ~100k ticks and production is negligible. With the pass the
50x town lives through 12000 frames (600k sim ticks): citizens 803 -> 957 -> 1189, hunger
mean flat at ~6, 10 starvation deaths (was 866 by frame 3000). The population keeps growing,
so `update_ms_per_iter` climbs with it (0.13 at frame 600 -> 0.16 at 6000 -> 0.19 at 12000);
the first rows match the pre-fix town.

## PerfX: the extensions `ext.render_culling` and `ext.anim_batch` (since 2026-10-12, PERFXEXT)

PerfX was a perf-build-only, env-gated set of non-retail knobs (`-DBANISHED_PERF_EXPERIMENTAL`,
`BANISHED_PERF_*`, 2026-09-05). Since PERFXEXT (`docs/extensions/handins/PERFXEXT.md`) it is two
extensions in `src/ext/render_culling/` (with `src/ext/anim_batch/`; `src/ext/perfx/` until 2026-10-20), compiled into every flavour and default OFF like every extension:
a `settings.cfg` key (`ext.render_culling=1`, `ext.anim_batch=1`) or the Extensions dialog turns
them on. **Every `BANISHED_PERF_*` flag and the build master are gone**; the perf build no longer
differs from the others in behaviour, only in optimisation.

| extension | class | what is skipped | why it is safe / what it costs |
|---|---|---|---|
| `render_culling` | A (live toggle) | off-screen `MeshInstance::Update` stays root-only on the last iteration; the move tick's `Geometry::SetTransform` on intermediate iterations and, on the last, for off-screen citizens; status-icon repositioning on intermediate iterations and for off-screen owners; `Picking::Scene::SetTransform` for off-screen citizens; ticking off-screen particle systems that are not fading | nothing the sim or the save reads: the clock and root-motion offset still advance (retail's own intermediate-iteration path), the gameplay position `model+0x148` is written every tick, the pick scene is queried only by the player's tools. A particle system is frozen only while none of its live emitters is fading, because the fade is what calls `StopEmitter` (sceneId / `active[]`, read by the tornado and the save). Everything deferred is replayed on pause and when the toggle goes off. Margin 6 world units (16 for particles), `PerfX::kCullMargin` |
| `anim_batch` | B (locked per game, `ext:anim_batch` pseudo-mod) | at speed N, the N−1 intermediate root-only `MeshInstance::Update` calls of a model whose root offset comes from the instanced pool or is zero; the last iteration advances it by the ticks it skipped | walk speed is exact for frames without a clip change, but the sim reads the animation clock (`IsAnimating`: a falling tree, the felling citizen's action, the tornado's knock-down) and the clock lags by up to N−1 ticks inside the frame; after a mid-frame `PlayAnimation` the skipped ticks land on the new clip. So the game diverges: class B |

Dropped with the move: `ANIM_BATCH_CULLED` (the off-screen-only batch: no gain alone, and it
would make the sim depend on the camera), `AMBIENT_THROTTLE` (the ambient emitter's slot sounds
it chose late are serialized; its cache was never pruned), `ANIM_CULL_MARGIN` (a constant now)
and `TRACE` (the census).

Not extensions, **faithful perf work, on in every flavour**: the TREE_SNAPSHOT walk (and
WALKCACHE / PERFXTBL on it; kill switch `kTreeSnapshot` in `entitylist.cpp`) and the terrain-type
sidecar (`src/game/Game/terrain_sidecar.{h,cpp}`: two writers that never told the mirror were
found and fixed in PERFXEXT). `BANISHED_PERF_TERRAIN_VERIFY` is now the COMPPROF=4 shadow
`TerrainSidecar`.

**Measuring.** The stress run is faithful by default (`/profile:faithful`: every extension off,
the snapshot and the sidecar on). The A/B pair against it is
`STRESS_EXT=render_culling,anim_batch tools/build.sh stress` (one extension: `STRESS_EXT=<id>`),
which reproduces the old "PerfX on" perf-build numbers minus the two dropped knobs.
`BANISHED_STRESS_COMPPROF=4` adds the shadows: `PerfXVerdict` (PERFXTBL's side table vs the old
hash, expect 0), `PerfXSim` (retail's StopEmitter decision on every frozen particle tick, expect 0),
`TerrainSidecar` (expect 0) and `AnimBatchSim` (the IsAnimating answers anim_batch changes and the
clip-change frames: the class B evidence, nonzero expected in a town that fells trees).

**Fixed 2026-10-12 (PERFXTBL, `docs/extensions/handins/PERFXTBL.md`):** `AnimCullSkip` and the
`_ANIM_BATCH_CULLED` arm of `AnimBatchDecide` tested a STACK copy of the model position, and the
per-frame visibility cache is keyed on the pointer. Every model ticked from the walk reaches it at
the same stack address (perf exe: `lea 0x20(%rsp),%rcx` at 0x1401e6e88), so within a frame they
all reused the first model's verdict until a hash collision evicted it. A visible citizen could be
left in a frozen pose while an off-screen one was fully posed. Both now key on
`&model->transform.translation`, as `ModelCulled` / `PickCullSkip` always did.

The history below names the old flags; read `BANISHED_PERF_EXPERIMENTAL=0` as "both extensions
off" and the knob names as the parts of `render_culling` / `anim_batch` above.

### The 2026-09-05 knob table (historical: these flags are retired; the evidence still holds)

| env | what is skipped | why it is invisible |
|---|---|---|
| `BANISHED_PERF_ANIM_CULL` | off-screen `MeshInstance::Update` stays root-only on the last iteration | the animation clock and root-motion offset still advance (retail's intermediate-iteration path); the pose is recomputed from the clock on the first visible tick, or by a zero-time update on pause |
| `BANISHED_PERF_GEOMXF_LASTONLY` | move-tick `Geometry::SetTransform` on intermediate iterations | only the last iteration is drawn; the render-side interpolation snaps from that same update; `model+0x148` (gameplay position) is still written every tick |
| `BANISHED_PERF_GEOM_CULL` | the last-iteration `Geometry::SetTransform` for off-screen citizens | nothing draws it; the margin updates it before it can enter the view; pushed on pause |
| `BANISHED_PERF_ICON_LASTONLY` / `_ICON_CULL` | status-icon repositioning on intermediate iterations / for off-screen owners | pure sprite placement; replayed on pause |
| `BANISHED_PERF_PICK_CULL` | `Picking::Scene::SetTransform` for off-screen citizens | cannot be clicked; replayed on pause |
| `BANISHED_PERF_PARTICLE_CULL` | ticking off-screen particle systems (frozen, not simulated) | the plume simply continues when the camera reaches it (margin +10 for rising smoke) |
| `BANISHED_PERF_AMBIENT_THROTTLE=n` | the biome histogram rescan of ambient-audio emitters, every n-th tick (8) | the region only changes when the map does; the temperature crossfade is still per tick |
| `BANISHED_PERF_TREE_SNAPSHOT` | the AVL pointer chase of the update-tree walk (flat in-order snapshot, rebuilt when the tree changes) | identical visit order — a data-structure change, not a behaviour change |
| `BANISHED_PERF_ANIM_BATCH_ALL` | at speed N, the N−1 intermediate root-only `MeshInstance::Update` calls per model whose per-tick root-motion offset cannot differ; the last iteration advances the animation by the ticks it skipped in one call | the move tick reads walk speed from `MeshInstance::RootAnimationOffset()`. For an instanced animation (`AnimationState` count 1, layer pool id ≥ 0, instanced byte set) that reads the `AnimationManager` pool pose, which `AnimationManager::Update` still refreshes with the real tick on every iteration; for an animation with no root motion (no non-zero node-0 keyframe position, cached per `Animation`) the offset is 0 for any dt. Those models are batched. A model mid-blend (count > 1), on a non-instanced root-motion animation, or with its instanced byte not yet set keeps the plain per-tick update (`PerfX::NeedsPerTickRootUpdate`, ~1.3% of the intermediate updates in the 800x10 town), because there the offset is the instance's own `+0x100`, `Animation::GetRootPoseAtTime` scales it by the dt it is given, and the blend weight changes per tick. Verified with `BANISHED_MOVETICK_TRACE=1` (4 citizens, 1500 frames, 10x): the per-tick root offset — `delta` divided by the citizen's own speed factor `vel38` — has mean 0.01482 / p50 0.014999 / max 0.01501 with batching on, 0.01484 / 0.014999 / 0.01501 off, and the blend-ramp samples (0.002–0.007) reappear (229 vs 228; the first ANIM_BATCH version had 0 and held values instead). The earlier "−2% mean step" (0.01418 vs 0.01447) was the run-to-run mix of `vel38` (0.69–1.06 per citizen and path), which two batching-OFF runs also show (0.01447 vs 0.01393). What still moves to the last iteration: animation events and the "animation finished" notification of a batched non-looping animation (up to N−1 ticks late). Bit-identical drawn pose. `_ANIM_BATCH_CULLED` is the off-screen-only variant (no measurable gain on its own) |
| `BANISHED_PERF_TERRAIN_SIDECAR` | the move tick's per-citizen read of the terrain-type byte (`cells[cur*0x30+0x29]`, the two sites in `moveto.cpp` and `citizenai_executor.cpp`) goes through a 1-byte-per-cell mirror (`standalone/perf_terrain.{h,cpp}`) instead of the 7 MB 0x30-stride cell array | same byte, same value. The mirror is keyed on the cell-array base pointer + count (new game / load resize rebuild it), the 7 `+0x29` stores call `TerrainNoteWrite`, worldgen classify and the stream load call `TerrainInvalidate`. `BANISHED_PERF_TERRAIN_VERIFY=1` compares every hot read against the real byte: 800x10, 3000 frames = 17.0M reads, 55k writes, 0 mismatches. **Measured gain ~1%** at 800x50 (update 8.16/8.20 vs 8.27/8.29 ms/frame, two runs each): on an X3D part the whole cell array already sits in L3, so the miss this removes is an L3 hit, not a DRAM miss. Kept on because it is proven exact and free; do not expect it to move the number |

### Optimisation log (2026-09-05, 800x10, sim ms / iteration, icons ON — see below)

| step | ms/iter | note |
|---|---|---|
| HEAD `aa71278` measured today (3 runs) | 0.266 | the recorded 0.24 was an icons-OFF run; see the caveat below |
| retail-faithful fixes only (`BANISHED_PERF_EXPERIMENTAL=0`) | 0.245 | `GetTypeIdByName` pointer cache (StatusIcon's per-tick name lookup was 9% of the phase), the per-burn `[HeatTick]` stderr line gated (1.7%), inline `sqrtf` in the move tick |
| + anim cull, geomXf last-only, icon last-only, ambient throttle | 0.231 | |
| + tree snapshot walk | 0.211 | |
| + particle freeze, pick cull, geom cull, icon cull, per-frame visibility cache, CharMgr cell memo | 0.209–0.214 | each within run noise (±0.005); kept because they remove work that grows with town size and zoom-in |
| iteration 11: Pickup resolver re-ran its whole ResolveAddr table every tick (one dependency stays missing so its "resolved" flag never set), `CitizenStatusIcon_SetFlag` called getenv per call, A* heuristic sqrt inlined (bit-identical) | 0.2095 | bucket-phase profile via the new sampler sub-tag 8 (`EntityList/buckets`) |
| **iteration 12: ANIM_BATCH_ALL** — one root-only animation update per model per frame instead of N | **0.192 / 0.196** (two runs) | treeWalk 1.36 → 1.11 ms/frame; culled-only variant 0.211 (no gain) |
| iteration 13: ANIM_BATCH_ALL made exact for walk speed — per-model decision (instanced / no root motion → batch; blend or non-instanced root motion → per tick), last iteration advances by the ticks actually skipped | **0.197 / 0.194** (two runs) | per-tick root offset now matches batching-off (see the knob table); ~1.3% of intermediate updates stay per-tick |
| iteration 14 (2026-09-05): TERRAIN_SIDECAR — 1-byte mirror of the terrain type for the move tick | **0.164 vs 0.165** (800x50, two runs each; ~1%) | the cell array is L3-resident on an X3D CPU, so the removed miss was cheap. Verified exact (17M reads, 0 mismatches). The remaining per-citizen misses (action node, component flags) are on heap objects, which a mirror cannot fix — that needs pooled allocation |
| iteration 15 (2026-09-05): per-size slot POOLS for every component + action node (frees reclaimed in the `MemoryAllocator::Free` detour), with double-free / poison / leak catches | **no change** (8.08/7.86 vs 7.84/7.86 ms/frame at 800x50) | DID NOT WORK, REMOVED. Same lesson as the terrain side-car: on a 96 MB-L3 X3D part the whole sim working set is L3-resident, so packing same-size objects buys nothing. It ran clean (22 pools, 64,631 live at teardown, 0 at exit, 0 catches, quarantine sweep clean), so the recipe is sound if a leak/double-free hunt ever needs it: pool at `MakeInertTyped` / `Action_Alloc` / `MemA()`, claim frees in `FreeLocked` with a one-range ownership test, poison 0xDD, report at `PerfX::Reset` |
| tried, reverted: heading sincos memo table, `always_inline` node handler, 3-deep prefetch | — | neutral or worse |

**Result: 0.194 ms/iter (mean of two runs) = −27% same-day vs HEAD (0.266), −50% vs the recorded
0.39 pre-wave baseline, reached at iteration 12 with ANIM_BATCH_ALL and kept at iteration 13 (0.194–0.197) once it was made exact for walk speed; without it the plateau is 0.21.** What remains is
retail gameplay work per tick: the action executor + MoveTo (~13% of the phase), the
root-only animation update that root motion needs (~8%), CharMgr cell registration (3%),
MoveTo enter's proximity query (3%), the region A* in the CitizenList phase (7% of the
update), and the entity/component pointer chase of the walk itself. In the bucket phase specifically
(`--tag 8`): the CharMgr proximity query MoveTo::Enter runs (16% of the phase, builds a 9 KB
quadtree walker per query exactly like retail), the region A* behind FindBestTarget (15%), and
the standalone allocator critical section around every Allocate/Free (~13%, needed for the
async pathfinder threads). Candidates that would need a decision because they change
behaviour or safety: a heap open set for the region A* (order of expansion), dropping the
allocator lock, or skipping child mesh-instance updates for culled models (their animation
clocks would drift).

**Measurement caveat — the status-icon setting.** `GameStatusIconEnable` (registry,
`HKCU\Software\Shining Rock Software LLC\Banished`) changes the HEAD number by 10%
(0.240 off vs 0.266 on: every icon runs a per-tick name lookup + `Geometry::SetTransform`).
Only compare runs whose `standalone_error.log` shows the same `[GameSettings] Load:
StatusIconEnable=` value. Two more traps hit this session: a failed perf build leaves the
previous exe in the game dir (check the `build=` stamp in `report.txt` against
`build/cmake-perf/OpenBA.exe`), and `BANISHED_SAMPLE` + `BANISHED_STRESS_COMPPROF=2`
together make the run ~15x slower — use one or the other.

## Runtime port perf gate (2026-09-06, Wave 3 lane P)

PLAN.md's definition of done requires `sim ms / iteration` within 5% of
`perf/baselines/current.csv` (0.19238) at each runtime-port wave boundary. The Wave 2
boundary measured **+7.8%**. Full writeup, with every run and its command:
**[docs/history/runtime_port/WAVE3_LANE_P.md](../docs/history/runtime_port/WAVE3_LANE_P.md)**.

The port's kill switches are RUNTIME switches, so one binary measures every configuration
-- no compiler or layout differences in the comparison. Take the ports-off A/B at every
wave boundary; it costs one extra run and separates "our port is slow" from "something
drifted":

| configuration | env | n | ms/iter | vs baseline |
|---|---|---|---|---|
| all ports off | `BANISHED_RT_PORT=0` | 4 | 0.1949 | +1.3% (drift) |
| Wave 1 only (Math+Volume) | `BANISHED_RT_SYSTEM=0` | 3 | 0.1969 | +2.3% |
| Wave 2 only (System) | `BANISHED_RT_MATH=0 BANISHED_RT_VOLUME=0` | 3 | 0.2043 | +6.2% |
| as landed | (default) | 4 | 0.2074 | +7.8% |
| + Timer resolve cached | (default) | 5 | 0.1967 | **+2.2%** |

The whole breach was one defect: `RtOriginal` (`src/engine/rt_port.h`) is
`GetProcAddress`, and `System::Time`'s ctor and `AsSeconds` called it **per call** -- an
export-table search by string, under the loader lock, on the value type every `Update()`
passes. It is invisible in `[COMPPROF]` and nearly invisible in the sampler's self time;
what it moves is **ntdll self time, 5.1% -> 8.6% -> 4.9%** across off/on/fixed. Caching the
two pointers in statics is worth 5.5 points. **Never call `RtOriginal` from a function body
on the live path.**

Also recorded here: compiling the diff oracle out (`RT_DIFF_BEGIN` forces a stack frame on
functions that would be frameless leaves) is worth only ~0.9% -- a negative result, not
worth a build switch today. And `tools/stress_report.py` now prints an explicit
`PLAN 5% gate (sim ms/iter): +X%  PASS|FAIL` line, because its own REGRESSION threshold is
10% and it called the failing Wave 2 boundary clean.

## Reading a regression

1. `report.txt` → which phase moved (EntityList vs CitizenList vs Animation vs PostSim).
2. If EntityList: `standalone_stress_compprof.txt` → which component type's Update grew.
3. Bisect with `git bisect run tools/stress_run.sh --frames 6000 --label bisect` and a
   small shell wrapper that greps `REGRESSION` out of `report.txt`.

### Pathfinding shadow mode (`BANISHED_STRESS_COMPPROF=3`)

Level 3 = level 1 plus `ext.fast_paths`'s shadow mode: retail's search drives the game and the
fast search runs on every request's inputs right after it, on the worker; the pair fills the
`pf_shadow_*` columns (`pf_shadow_equal_cost`, `same_cells`, `worse`, `pf_shadow_speedup`). The
`pf_search_us` columns then include BOTH searches, so a level-3 run is for the shadow columns only;
compare timing between separate faithful and `ext.fast_paths` runs. Set
`BANISHED_STRESS_COMPPROF=3` in the environment of the run.

## Native macOS Metal measurement

There is no macOS stress adapter. See [measurement facts](../docs/reference/macos_perf.md) and the
[historical method](../docs/history/macos/PERF_METHOD.md).
