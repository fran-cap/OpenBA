# Android P13: the EntityList tree walk on ARM (Galaxy S10)

NOT RETAIL. The question from `docs/runtime_port/NEXT_SESSION.md` §2.14 ("Next session" item 2): the
desktop closed the walk's ideas list on 2026-10-12/13 (`docs/extensions/STATUS.md`, handins PREFETCH /
SMMEMO / STILLPATH / STILLTERRAIN / DIRECTTICK / PARTREPEAT / OFFSCREEN / WALKBATCH), on a 96 MB X3D L3
where "the sim is not memory-bound". What does not transfer to a phone? Branch `android/p13-walk`
(from `android/p12` at `3fdf76d50`). Raw logs and profiles are in the session scratchpad, not the tree.

## 1. Device and protocol

- **Galaxy S10** SM-G973U, Snapdragon 855: cpu7 prime Cortex-A76 2.84 GHz with 512 KB L2, cpu4-6 gold
  A76 2.42 GHz with 256 KB L2 each, cpu0-3 silver A55 1.79 GHz; 2 MB L3 (DSU), 3 MB system cache;
  Android 12, 7.6 GB. `cpu_capacity`: cpu7 alone at 1024 (`[Cpu] ... big=0xf0`).
- **Build:** release apk (`tools/android.sh build release --jobs 4`: -O3, ThinLTO, the committed Android
  PGO profile), signed with the debug key, plus `<profileable android:shell="true"/>` in the manifest
  for the build only (never committed).
- **Run:** `tools/android.sh run BANISHED_STRESS=1 BANISHED_FPS_CAP=60` (800 citizens, 3 sets, speed 10,
  6000 frames, seed 31337, `BANISHED_STRESS_COMPPROF=1` default), battery <= 32.0 C and thermal status 0
  before each run; every run below ended at thermal 0 and <= 33 C. The phone defaults of
  `app_android.cpp`: fixes and the default-on class A rows on, so the walk is ext.walk_batch's
  (`EntityList::UpdateWalkBatch`), not the faithful `EntityList::Update`.
- **Metric:** the mean of the `[Stress]` / `[COMPPROF]` windows t = 20..100 s. Run-to-run spread on the
  same apk: 4 runs, update 10.02-10.06 ms/frame, treeWalk 6.46-6.49 ms/frame, fps 35.1-35.5 (< 0.5%).
- **Counters:** NDK r30 simpleperf pushed to `/data/local/tmp`, `--app org.openba.game -t <main tid>`
  (the shell needs `--app` on a profileable app; `-o FILE` because stdout is lost under `--app`).
  `simpleperf stat` (15 s, 6 events a group, multiplexed 50%) and `simpleperf record -f 3000 --call-graph fp`
  on five events at once, symbolised from the unstripped `libmain.so` (`binary_cache_builder.py`), lines
  from `llvm-addr2line --inlines`.

## 2. Where the sim goes (baseline)

Per frame, speed 10 (10 sim iterations a frame), ~803 citizens, mean of windows 2-10:

| | ms / frame |
|---|---|
| fps (cap 60, main thread bound) | 35.3 |
| update (sim) | 10.03 (1.00 / iteration) |
| ... EntityList | ~8.0 (82% of update, `[SIMPROF]` window 1: 7.66 of 9.31) |
| ... ... treeWalk (13,950 nodes / frame = 1,395 / iteration) | **6.47** |
| ... ... buckets (300 Updates / frame) | 1.54 |
| ... PostSim (UI, camera, audio) | ~1.0 |
| draw (main-thread draw work + wait on the render thread) | 9.6 |

- **treeWalk = 464 ns per node** including the slot-8 ticks it calls. The desktop's figure for the
  same walk is ~75 ns (PREFETCH.md §3, X3D). A 2.4 GHz A76 against a 5 GHz Zen is a factor ~2-3 on
  core speed; the rest is the memory system (section 3).
- The `[COMPPROF]` "component Update total" (1.3-1.6 ms) is the BUCKET Updates only; the walk's ticks
  are inside treeWalk. COMPPROF=2 would time them, at a profiler cost on every tick: not used.

## 3. Is the walk memory-bound on the S10? Yes, largely

### 3.1 Thread placement

The main thread (sim + draw gather, single-threaded) sampled once a second for 60 s: **cpu7 (prime)
0 of 60**, gold cpu4-6 57, silver 3. `simpleperf stat` puts it at 1.86-1.96 GHz while running (gold max
2.42). The P12 pin (`perf_hint_android.cpp`) needs >= 2 CPUs of the top capacity and logs
`[Pin] not pinned: 1 CPU(s) of the largest capacity 1024`. So on this phone the sim never sees the
2.84 GHz core or its 512 KB L2.

### 3.2 Whole main thread (`simpleperf stat`, 15 s at t ~ 30 s)

| | value |
|---|---|
| IPC | 0.90 (9.69 G instructions / 10.79 G cycles) |
| backend stall cycles | 60% (frontend 7.5%) |
| L1D refills | 3.3% of L1D accesses, 12.7 per 1000 instructions |
| L2D refills | 13.9 per 1000 instructions (135 M in 15 s) |
| L1D TLB refills / L2 TLB refills (= page walks) | 14.7 / 2.7 per 1000 instructions |
| branch mispredicts | 3.0 per 1000 instructions |

The L3 events (`raw-l3d-cache*`, `raw-ll-cache-miss-rd`) count more L3 accesses than L2 refills
(1.2 G against 0.135 G): the DSU counts them for the cluster, not the thread. They are not usable per
thread, so L3-hit versus DRAM is **not** separated here.

### 3.3 The walk itself (`simpleperf record`, main thread, 15 s)

`EntityList::UpdateWalkBatch` (the walk loop with everything it inlines: TickList, the staged prefetch,
ext.walk_batch's eligibility and step, render_culling's visibility cache) is the top symbol:
29% of the main thread's cycles, 36% of its instructions, 32% of its L2 refills. Out of line under it:
MoveTo::Enter 3.5%, CharacterComponent::Tick 3.2%, ModelComponent::Tick 1.2%, CharacterGrid::Place 1.2%,
MeshInstance::Update 4.4% (part of it the draw's).

| per walk node, UpdateWalkBatch's own code | value |
|---|---|
| cycles | ~545 (3.99 G over ~7.3 M nodes) |
| IPC | 1.05 |
| backend stall | 61% of cycles |
| L2 refills | ~7.8 (13.6 per 1000 instructions) |

At ~40 cycles an L3 hit and ~250-300 a DRAM miss at 2 GHz, 7.8 L2 refills a node cost 310 cycles if
all hit L3 and are serial, more if some reach DRAM; the 61% backend stall says most of the 545 cycles
wait on data. This is the opposite of the X3D verdict: per iteration the walk touches ~1.1-1.6 KB per
character (PREFETCH.md §5), ~1-2 MB for this town, against a 256 KB L2 and a 2 MB L3 shared with the
render thread and the gold cluster; the desktop's "the walk is an L3 walk" becomes an L3 + DRAM walk.

Inside UpdateWalkBatch, by inlined source line (cycles; the five events give nearly the same
distribution, so the A76's sample skid limits what a single line says; the groups are reliable):

| group | share of the walk's own cycles |
|---|---|
| the staged prefetch (`WalkPrefetchStaged`, `ForEachWalkCandidate`: entitylist.cpp 505-620) | ~25% |
| ext.walk_batch `Eligible` (the top action's vptr, MoveTo fields: walk_batch.cpp 104-113) | ~17% |
| ext.render_culling `CachedVisible` (a 128 KB direct-mapped table, random slot per object) | ~9% |
| an atomic load (libc++ `__cxx_atomic_load`: the ClassVptr / `Object` pointer reads) | ~4% |
| StillPath `IsFixedPoint`, TerrainSidecar, the walk_batch step | ~5% |

The prefetch stages are not free on this core: each stage re-derives its addresses from live objects
(the component IBP array, each component's vptr, the character's action slot), and those reads are
demand loads that miss in turn. On the X3D they were L3 hits behind a 75 ns visit; here they are
part of a 464 ns visit.

### 3.4 What the prefetch distance does here

One entity apart per stage was chosen on x86 because a visit (~75 ns) is about one miss. On the S10 a
visit is ~460 ns, already longer than a DRAM miss (~120-150 ns): stage k is k x 460 ns ahead, so the
look-ahead is not too short. A longer stride cannot help (the desktop sweep was monotone the other way
for the same reason: lines must survive more visits in a 64 KB L1D / 256 KB L2). The open question
is the opposite one: whether the five-stage pipeline's own dependent loads cost more than they hide on
an A76 (whose hardware prefetchers also run), which is variant 0 / 1 / 2 of `kWalkPrefetch`.

## 4. Ranked plan

1. **The main thread on the prime core when the SoC has exactly one** (`src/platform/android/`, the
   P12 pin's single-prime case). Expected from the clock (+17%) and the 2x L2 (512 KB) on a walk that
   is mostly waiting on data: sim -10..-20%. Risk, from P12 on the S25: pinning the main thread alone
   let the render thread's cluster clock fall. On the S10 the prime is a cluster of its own, so the
   main thread leaving the gold cluster removes its load there, which can lower the gold clock the
   render thread runs at; measured, not assumed. The S25 path (>= 2 top CPUs: both threads pinned) must
   stay byte-identical in behaviour: the new branch runs only when the top capacity has one CPU.
2. **`kWalkPrefetch` on ARM**: variants 2 (shipped) / 1 / 0 A/B on the phone. If one is better here, a
   per-ABI constant (`__aarch64__`, the AGENTS.md §4 spelling), no flag. Faithful-code hints only:
   results identical by construction (a prefetch changes no value). Desktop keeps 2.
3. **ext.walk_batch_grid (class B) measured on the phone**, `ext.walk_batch_grid=1` in the device's
   settings.cfg (the stress town is a new game, so the row's lock takes the value). Report only: the
   default stays off (owner ruling).
4. **render_culling's visibility cache** (~9% of the walk's own cycles): a 128 KB table indexed by a
   hash of the object address is a random L2 access per test on a 256 KB L2. A smaller slot (the
   key could be a 32-bit tag, 8 B slots) halves it; class A (identical answers, a cache). Only if 1-3
   leave the walk where it is, and with the hit rate measured.
5. **Not pursued:** a longer prefetch stride (section 3.4); entity / component layout or node packing
   (the objects are allocated by the faithful code in retail's order; repacking them is not
   bit-identical in allocation and the desktop closed it); huge pages for the TLB (2.7 page walks per
   1000 instructions is real, but Android gives an app no control over THP on its heap).

## 5. Results (2026-10-28)

Every A/B below is ABBA, one pair per hold of the phone lock, the protocol of section 1; numbers per
run, mean of windows t = 20..100 s, all runs started at <= 32.0 C / thermal 0 and ended at thermal 0.
The B builds of 5.2 and 5.3 change the walk's code, so clang drops the committed PGO profile for
those functions (`-Wno-profile-instr-out-of-date` hides it): every B there ran its walk WITHOUT PGO
against a PGO'd A. That biases against B, so the gains are a floor (and 5.2's losses partly PGO);
the retrain on the final code (NEXT_SESSION §2.14 item 1) settles it.

### 5.1 The main thread on the prime: blocked by the system, dropped

| run | update ms | treeWalk ms | fps | main thread on (60 samples) |
|---|---|---|---|---|
| A base | 9.98 / 10.04 / 9.98 | 6.42 / 6.50 / 6.41 | 35.9 / 35.5 / 35.6 | gold 59-60, prime 0-1 |
| B pin main to cpu7 at attach | 10.01 / 10.06 | 6.49 / 6.44 | 35.3 / 35.6 | gold 59, prime 1 |
| B + re-pin every 60 frames | 9.86 | 6.36 | 36.9 | gold 60, prime 0 |

`sched_setaffinity(cpu7)` returns 0 at attach, but the thread is back on 0-7 seconds later, and the
re-pin is refused (EINVAL) whenever cpu7 is not in the allowed set: the thread's mask read back as
0x5f and 0x3f. The cause is Qualcomm's `core_ctl` (`/sys/devices/system/cpu/cpu7/core_ctl`:
`enable=1 min_cpus=0 max_cpus=1`, `global_state` CPU7 `Isolated: 1 Active CPUs: 0`, cpu6 isolated
too, cpu7 idling at 825 MHz during the stress town): the prime is isolated until the cluster's own load
asks for it, which a thread that never runs there cannot make. `min_cpus` is root-only. So an app cannot
put its main thread on the S10's prime; the 2-prime S25 path is unaffected (nothing committed). Note
for the S25: the same reset may undo its P12 pin after a cgroup move (P12 verified the pin by sampling
at the time); a periodic read-back is the cheap check if a future S25 run shows the main thread off
6-7.

### 5.2 `kWalkPrefetch` on ARM: the shipped variant 2 is right, and the pipeline matters 3-4x more than on x86

| run | update ms | treeWalk ms | fps |
|---|---|---|---|
| A variant 2 (shipped) | 10.06 / 10.02 | 6.50 / 6.47 | 35.1 / 36.0 |
| B variant 0 (no prefetch) | 13.61 / 13.07 | 10.21 / 9.80 | 22.7 / 23.4 |
| A variant 2 | 10.02 / 10.01 | 6.45 / 6.44 | 35.7 / 35.7 |
| B variant 1 (three stages) | 11.03 / 11.04 | 7.74 / 7.73 | 31.9 / 31.4 |

Without the prefetch the walk is **+54%** (desktop: +15..+19%), with the old three stages +20%
(desktop: +3..+6%). Same ranking as x86, much larger gaps: the clearest evidence that the walk is
memory-bound here, and that more coverage, not a different distance, is the lever. No per-ABI constant
for the variant: 2 stays everywhere.

### 5.3 More coverage: the MoveTo lines (kept, ARM only) and the visibility slot (dropped)

`kPrefetchCovMoveTo` (commit `perf: the walk prefetch names a walking character's MoveTo lines on
ARM`): stage i+1 already prefetched the character's current Action's first 0x20 bytes; ext.walk_batch's
`Eligible` (the hottest line of the walk in section 3.3 is its `field_0x474` read) and MoveTo::Execute
read +0x28..+0x43, +0x60 and +0x474 next. The arm names those three lines instead of +0x1F. The vptr is
not resident yet at that stage, so the lines are named for any current action (wasted on a short one).

| run | update ms | treeWalk ms | draw ms | fps |
|---|---|---|---|---|
| A base | 10.06 / 10.02 | 6.50 / 6.47 | 9.65 / 9.55 | 35.1 / 35.7 |
| B MoveTo lines | 9.77 / 9.80 | 6.14 / 6.16 | 9.92 / 9.83 | 35.8 / 35.8 |

**treeWalk -5.2%, update -2.5%**, both B runs under both A runs (and B ran without PGO on the walk).
The frame barely moves (fps +1%): draw rises by about what update saves, i.e. the main thread now
waits longer on the render thread, which becomes the limit at this town (the render-thread
investigation's territory). `#if defined(__aarch64__)`: the desktop x86 object is the code it was;
macOS arm64 takes it too, unmeasured there. A prefetch changes no value, so results are identical by
construction; Windows gate (new game, 2 min, crash log 0), resave gate PASS (the golden md5
`971aaec2cf95f5f730bb1cd1a4fa8428` unchanged), unit 1558 / 0 / 3 skipped.

`kPrefetchCovVis` (not committed): stage i+2 prefetched ext.render_culling's visibility-cache slot of
each model's position (the table made visible through a header). On top of MoveTo: treeWalk 6.13 /
6.14 -> 6.07 / 6.13, update 9.75 / 9.75 -> 9.68 / 9.81, fps 36.5 / 37.2 -> 36.4 / 35.7: noise. Dropped;
the section 3.3 cycles on that line were the skid of a neighbouring miss, or the slot is already
resident (three asks per citizen per frame).

### 5.4 ext.walk_batch_grid (class B) on the phone: no gain at speed 10

| run | update ms | treeWalk ms | fps |
|---|---|---|---|
| B grid on | 10.05 | 6.50 | 35.3 |
| A off | 10.10 / 10.04 | 6.44 / 6.51 | 35.0 / 35.2 |
| B grid on | 10.01 | 6.50 | 36.2 |

`[Ext] ext.walk_batch_grid=1 class B` in both B logs (a new game, so the row locked on). treeWalk
6.50 against 6.48, update -0.4%: nothing. Expected from section 3.3: CharacterGrid::Place, the call the
row skips, is 1.2% of the main thread here (6.5% on the desktop at speed 50 everything-on, where the
row was aimed). The default stays off; no case for an owner ruling from the phone.

## 6. Next steps, ranked

1. **Retrain the Android PGO profile on the final code** (already NEXT_SESSION §2.14 item 1): the walk
   of this branch runs unprofiled until then, so the shipped gain should grow, not shrink.
2. **More prefetch coverage in the walk, ARM first.** Variant 0 -> 1 -> 2 -> MoveTo is monotone and
   large on this phone; what is left to name: `MoveTo::path[pathIndex]` (needs the index, one stage
   after the MoveTo lines: a new stage, or the index read at the visit for entity i+1), the terrain
   cell under a walker (TerrainSidecar, ~1% of the walk), the CharacterGrid slot Place writes, the
   MoveTo::Enter path (3.5% of the main thread, 6% of its L2 refills; pathfinding, not the walk). Each is
   one A/B pair; the same no-PGO caveat applies until a retrain.
3. **The frame at this town is now render-thread limited** after the sim gain: the render-thread
   investigation's packet / emit work decides whether sim savings show as fps.
4. **Not pursued, and why:** the prime pin (5.1, system-blocked); a different prefetch distance (3.4);
   walk_batch_grid by default (5.4); entity or node repacking (not bit-identical in allocation, desktop
   closed it); huge pages (no app control).
