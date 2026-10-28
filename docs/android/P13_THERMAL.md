# Android P13 pass 3: the throttled state of the stress soak (Galaxy S10)

NOT RETAIL. The problem statement is `P13_PROFILE.md` section 7: in the 19-minute capped stress soak the
Galaxy S10 falls from 47 to 21-23 fps after ~155 s, far more than the thermal caps alone explain, with
~14 ms of the throttled frame in no timer. Branch `android/p13-thermal` (from `android/p12` at
`2a5c2c481`). Raw logs, traces and scripts are in the session scratchpad, not the tree.

## 1. Device and protocol

- **Galaxy S10** SM-G973U, Snapdragon 855, Android 12 (API 31), kernel 4.14 (Qualcomm WALT, schedtune,
  core_ctl). `cpu_capacity` 378 (cpu0-3 silver), 871 (cpu4-6 gold), 1024 (cpu7 prime alone).
- **Build:** `tools/android.sh build release --jobs 4` (-O3, ThinLTO, the committed Android PGO profile),
  signed with the debug key. For the traces only, the same build plus `<profileable android:shell="true"/>`
  (never committed).
- **Run:** `tools/android.sh run BANISHED_STRESS=1 BANISHED_FPS_CAP=60 BANISHED_STRESS_FRAMES=66000`, as
  pass 2, from battery <= 32.0 C and thermal status 0, the app stopped while cooling.
- **Traces:** `atrace -b 65536 -t 8 sched freq idle gfx` (ftrace text: sched_switch / wakeup, cpu_frequency,
  sched_blocked_reason) at t ~ 40 s (not throttled) and t ~ 333 s (throttled) of one run, then NDK r30
  `simpleperf record --app org.openba.game -e cpu-clock -f 2000 --call-graph fp --trace-offcpu --duration 20`
  (t ~ 341-361 s), read in `on-off-cpu` mode with each sample charged inclusively to every `libmain.so`
  function on its chain.

## 2. Facts

### 2.1 ADPF does not run on the S10
- `APerformanceHint_*` resolves (API 31) and `APerformanceHint_getManager` returns a manager, but
  `APerformanceHint_createSession` returns null for both threads: `[ADPF] main session refused (tid ...,
  target 16666666 ns)`, the same for the render thread (pass 2's soak log has the same two lines).
- The reason is the device's: `adb shell dumpsys performance_hint` prints `HintSessionPreferredRate: -1`,
  `HAL Support: false`. The power HAL has no hint sessions; no app can have one on this phone.
- `[Thermal] status 0 (at start)` and no change for the whole run: the thermal listener works, the status
  never moves (the vendor thermal engine caps the clocks below anything `PowerManager` reports).
- `[Pin] not pinned: 1 CPU(s) of the largest capacity 1024 in mask 0xff`: P12's fastest-cores pin needs two
  CPUs of the largest capacity; the S10 has one, so nothing is pinned.
- No other per-thread clock lever is open to an app here: the kernel has no uclamp (no `sched_util_clamp_*`
  sysctl; the config has `SCHED_WALT`, `SCHED_TUNE`, `SCHED_CORE_CTL`), `/dev/stune/top-app/schedtune.boost`
  is `root:system 0664`, and core_ctl's `min_cpus` is root-only (P13_WALK 5.1). What an app can set is its
  own threads' affinity, within the cpuset.

### 2.2 The "untimed 14 ms" is more sim steps per drawn frame, not a wait
The `[Stress]` `update=` column is the mean of ONE `GameUpdateLoop::Update` call (one fixed 1/60 s step,
`speed` sim iterations inside it), not of a drawn frame. `MainLoop::Frame` runs as many steps as the
accumulator holds, so when a frame takes longer than 16.7 ms the next frame runs more steps: the sim is
held at 60 steps a second, and the drawn frame gets what is left of the main thread.

| t (s) | window (s) | sim steps / s | fps | steps per drawn frame | update ms per step | main ms / s in steps | in draw main | in pipe waits | citizens |
|---|---|---|---|---|---|---|---|---|---|
| 10 | 10.0 | 59.9 | 57.8 | 1.04 | 8.17 | 490 | 249 | 210 | 803 |
| 100 | 10.0 | 60.0 | 46.5 | 1.29 | 9.91 | 595 | 198 | 200 | 954 |
| 310 | 10.3 | 58.3 | 39.5 | 1.47 | 10.83 | 631 | 182 | 177 | 1010 |
| 341 | 10.1 | 59.4 | 29.8 | 1.99 | 11.51 | 684 | 145 | 160 | 1012 |
| 372 | 10.6 | 56.6 | 21.5 | 2.63 | 12.86 | 728 | 116 | 152 | 1016 |

(this pass's diagnostic run; one CSV row per 600 steps, its window from the `wall_s` column; steps per drawn
frame = `sim_iters / speed / (fps x window)`. The three main-thread columns and the rest, ~5-10 ms a
second, add up to the second.) The throttled frame of pass 2 adds up the same way: 2.7 steps x 12.4 ms +
draw main 5.0 + wait 7.0 = 45.5 ms against the measured 45-46 ms. The profile agrees: in the throttled
window the main thread is on a CPU 82% of the time (atrace), and 681 of its 685 on-CPU ms a second are
under `MainLoop::Update`; its off-CPU time (161 ms a second, 7.6 ms a drawn frame) is all
`Ext::RenderPipe::Hook`, the pipe's waits. Nothing else blocks it.

So fps ~ (1000 - 60 x update) / (draw main + wait + ~0.6) ms while the sim keeps real time: the sim's
demand is fixed, and the drawn frames share the remainder. Near saturation this amplifies: at update
12.4 ms a step, 1 ms more per step costs ~25% of the fps. Once the steps alone pass ~75% of the main thread
(pass 2 after ~600 s, this pass's soak A after ~780 s), the game no longer keeps real time: the CSV rows
stretch to 11-12.4 s for their 600 steps (48-54 steps a second, the accumulator's clamp drops the rest)
while the drawn rate settles at ~22 fps. From there the cost of a slower step shows as a slower game, not
as fewer frames, so a soak comparison reads both columns.

### 2.3 What the throttling itself costs
- Pass 2's windows at t ~ 450-530 s, where the caps lifted for a minute (gold 2419 MHz, main thread back
  on gold), ran update 11.2-11.4 ms a step at 1035-1056 citizens; the throttled windows around them ran
  12.4-12.5 ms at 1016-1070 citizens. **The caps add ~10% to a step; the rest of the growth from 8.3 ms
  is the town growing (803 -> 1219 citizens).** Through the amplification above, that 10% is worth
  36 -> 22 fps.
- Where the throttled main thread runs (atrace, t ~ 333 s): 82% on CPU at a mean 1838 MHz (pre-throttle:
  2203 MHz): 62% of its run time on gold at 1920, the rest on cpu7 at 1497-1920 MHz. Under simpleperf
  (t ~ 341-361 s) it ran 80% of its samples on cpu7. core_ctl un-isolates cpu7 for it (`task_thres=1`),
  and the prime's clock ramps from its 825 MHz floor each time.
- The render thread: 17% on CPU, on gold, and **29% of its run time at gold's 710 MHz floor** (the gold
  cluster's load fell when the main thread left it), mean 1471 MHz (pre-throttle 2209 MHz). Its emit is
  CPU work (146 ms a second on CPU against 30 ms a second blocked in `VideoVulkan::PresentFrame`), so the
  emit, and the main thread's fence wait behind it, stretch with that clock: wait 3.6 -> 7.1 ms a frame.
- The fork-join helper (`SDLActivity` tid, 18% on CPU, woken by the main thread ~9 times a frame) ran 43%
  of its time at 710 MHz in the same window.
- GPU: cap 585 -> 427 MHz, running at its 257 MHz floor, 54-58% busy; not on the main thread's path
  (no main-thread time in the driver, the render thread blocks 1.4 ms a frame in Present).

### 2.4 Pass 2's thread sampler watched the wrong thread
Pass 2's sampler took "the second busiest thread" from `ps -T` at t = 20 s; `ps` reports CPU since the
thread started, which made it the asset loader (`SDLActivity`, 37% from loading, ~0.5% in play), not the
render thread (an `SDLThread`, 11%). Its "render thread alone on gold at ~25%" column was that thread.
The conclusion survives through the traces above (the render thread does fall to 710 MHz), but the
render thread is now found by its `[ADPF]` / `[Pin]` line.

## 3. Ranked plan

1. **Keep the main and render threads together on the fastest cluster with at least two CPUs** (S10:
   gold, cpu4-6). Today's rule (P12) pins both to the CPUs of the largest capacity when there are at
   least two (S25: the 2 prime cores); the S10's single prime fails that rule and nothing is pinned. The
   generalisation takes the highest capacity tier that has at least two CPUs. Expected on the S10: the
   main thread stays off the lone prime and its 825 MHz ramps, the render thread shares a cluster whose
   load the main thread keeps up (no more 710 MHz), cpu7 stays isolated (less prime power). Estimate in
   the throttled state: the emit at the gold cap (8.4 -> ~6 ms, wait 7 -> ~5 ms) and a few % off the step
   (no prime ramps): (1000 - 60 x 12.2) / (5 + 5.2 + 0.6) = **~25 fps against 21-22** (unverified). Before
   throttling nothing changes (prime isolated, both threads on gold already). **S25: the rule returns the
   same 2 prime CPUs (the largest tier already has two), so its path is unchanged by construction.**
   Risks to measure: gold's core_ctl keeps 2 of 3 CPUs online (`min_cpus=2`), so the two threads plus the
   fork-join helpers share 2-3 CPUs; a cpuset change may reset the mask (P13_WALK 5.1), checked by
   sampling each thread's CPU.
2. **Read the pin back periodically and re-apply it** if a cpuset move reset it: only if item 1's soak
   shows the threads leaving the mask; same code on the S25 (P13_WALK 5.1's open note).
3. **Not levers here:** ADPF (no HAL), uclamp / schedtune (not available to an app), the thermal status
   (never moves), the GPU (not on the path), the "untimed gap" (it is the fixed-step sim, faithful). A
   frame-rate cut below the cap is excluded by the owner's policy.
4. **For later measurements:** in the stress town at speed 10 the fps is the main thread's remainder after
   the sim, so main-thread savings (p13cpu's items) are worth ~3x their share in fps once the sim takes
   ~75% of a second; the soak's comparable metric is update ms per step at a given citizen count, and the
   soak town grows, so A and B must be compared at equal t.

## 4. Results

### 4.1 Item 1, the tier pin: KEPT (commit `perf: pin the frame's two threads to the fastest tier of two or
more CPUs`)
`FindFastCores` walks the capacity tiers from the largest down and pins to the first with two CPUs or
more; boot lines on the S10: `[Pin] the largest capacity 1024 has one CPU: the tier of capacity 871 (3
CPUs)`, then `[Pin] render thread tid N -> CPUs 0x70 (of 0xff): pinned` and the same for the main thread.

**Soak A/B** (one pair, A = `android/p12` as built here, B = the tier pin, same flavour: release + PGO,
debug key, not profileable; 66000 steps, both from battery 31.7 C, thermal 0; ~1100 s of play each).
Per minute (CSV rows by `wall_s`; clocks and placement from a sampler every ~9 s):

| min | A fps | B fps | A update / step | B update / step | A wait / frame | B wait / frame | A gold MHz (cap) | B gold MHz (cap) | A GPU MHz busy | B GPU MHz busy |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 56.6 | 56.3 | 8.64 | 8.68 | 4.00 | 3.95 | 2355 (2419) | 2272 (2419) | 487 78% | 499 77% |
| 2 | 44.8 | 44.8 | 10.15 | 10.19 | 4.30 | 4.26 | 2405 (2419) | 2392 (2405) | 427 74% | 427 73% |
| 3 | 39.5 | 37.1 | 10.62 | 10.90 | 4.50 | 4.48 | 2106 (2137) | 1961 (1961) | 356 72% | 368 70% |
| 4 | 25.7 | 30.2 | 11.81 | 11.62 | 6.19 | 4.67 | 1280 (1722) | 1708 (1708) | 270 60% | 270 76% |
| 5 | 25.2 | 28.1 | 11.96 | 12.02 | 6.13 | 4.74 | 1182 (1796) | 1752 (1752) | 270 60% | 270 69% |
| 6 | 30.4 | 32.3 | 11.67 | 11.64 | 5.01 | 4.41 | 1772 (2016) | 2016 (2016) | 307 67% | 307 71% |
| 8 | 25.2 | 30.5 | 12.18 | 11.97 | 6.11 | 4.45 | 1399 (2016) | 2016 (2016) | 270 61% | 282 70% |
| 10 | 26.4 | 30.1 | 12.16 | 11.95 | 5.45 | 4.40 | 1643 (2016) | 2016 (2016) | 294 68% | 257 74% |
| 11 | 22.4 | 28.4 | 12.47 | 12.20 | 6.55 | 4.37 | 1168 (2016) | 2016 (2016) | 257 57% | 257 75% |
| 12 | 22.2 | 25.3 | 12.95 | 12.62 | 6.64 | 4.54 | 1025 (2016) | 2016 (2016) | 257 55% | 257 68% |
| 14 | 22.6 | 21.6 | 14.39 | 13.42 | 7.06 | 4.77 | 1089 (2016) | 2016 (2016) | 257 59% | 282 53% |
| 16 | 22.6 | 22.2 | 14.41 | 13.14 | 6.91 | 4.69 | 710 (2016) | 2016 (2016) | 270 59% | 257 58% |
| 18 | 23.1 | 22.4 | 14.54 | 13.96 | 6.71 | 4.81 | 1212 (2016) | 1968 (2016) | 257 62% | 279 54% |

| phase | A fps | B fps | A sim steps / s | B sim steps / s | A update / step | B update / step | A wait | B wait | A current | B current | battery A / B |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0-240 s (not throttled) | 47.0 | 46.5 | 60.0 | 60.0 | 9.80 | 9.89 | 4.26 | 4.22 | -429 mA | -410 mA | 31.7 -> 34.4 / 31.7 -> 34.4 C |
| 240-600 s (throttled) | 27.0 | **30.0** | 59.9 | 59.9 | 11.95 | 11.90 | 5.55 | **4.52** | -273 mA | -281 mA | -> 35.0 / -> 35.1 C |
| 600-1100 s (saturated) | 23.0 | **24.2** | 54.0 | **59.1** | 13.60 | **12.97** | 6.65 | **4.62** | -238 mA | -271 mA | -> 35.4 / -> 35.8 C |
| whole run | 29.4 | 30.8 | 57.2 | 59.5 | 12.19 | 11.96 | 5.51 | 4.46 | -290 mA | -304 mA | end 35.4 / 35.8 C |

(current: `battery/current_now` with USB attached and the battery full, negative = drawn from the
battery; a relative figure only.) Thermal status 0 throughout both; both runs ended `EXITED
clean-shutdown`.
- **Before throttling nothing changes** (as predicted: the prime is isolated and both threads are on gold
  either way).
- **Throttled (240-600 s): fps +11%**, the pipe wait -19% (the render thread at the gold cap instead of
  its 710 MHz floor: A's gold averaged 1519 MHz, B's 1921, against caps of 1708-2016).
- **Saturated (600 s on): the sim keeps real time** (59.1 against 54.0 steps a second: A's game ran 10%
  slow) **and draws 5% more frames**, with update per step -4.6% although B's town, further along, is
  as large or larger. The GPU is busier in B (70-75% against 55-68%) at the same clocks.
- Placement: B's two threads stayed inside 4-6 in every sample (130 of 130 while running: no mask reset,
  so item 2 is not needed); A's main thread was on cpu7 in 141 of 207 samples after t ~ 285 s.
- **Cost:** +0.4 C at the end (35.8 against 35.4) and ~5% more current over the run. One pair: the heat
  difference is not resolved against run-to-run spread (the throughput difference is consistent minute
  by minute from minute 4 to 12). Below the cap either way, so the owner's policy (hold the cap, never step
  below it) is not in play: B does more sim and draws more frames per degree.
- Item 1's estimate (~25 fps against 21-22 in the throttled state) was close for 240-600 s (30.0 against
  27.0); in the saturated phase most of the gain went to the sim rate instead of frames.

**Normal play** (`BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town`, 1x, 180 s window after 40 s, both
from 31.8 C): A 59.5-59.9 fps windows, SurfaceFlinger 10790 frames 0 dropped; B 59.6-59.8, 10812 frames 0
dropped. CPU (% of one core over the window): main 46 / 35, render 33 / 30, process 85 / 73; current -158 /
-113 mA; battery +0.8 / +0.4 C; gold sampled at 710 MHz most of the time in both; both threads on 4-6 in
both (the prime idles isolated at 825 MHz). Not worse; single runs, so the lower CPU in B is not claimed as
a gain.

**S25 (not available today):** its largest tier has two CPUs (the prime pair), so the loop stops at the
first tier and returns the same mask as before; no new log line (the "one CPU" line is printed only when a
larger tier was skipped). Provably the same path; nothing to re-measure there. A uniform CPU or a failed
capacity read still pins nothing.

### 4.2 Dropped
- Item 2 (periodic read-back of the pin): no reset seen in 130 samples over 19 minutes; not built.
- ADPF changes: the S10 has no HAL; the S25 path is untouched. Reporting wall time instead of CPU time,
  or one session over both threads, cannot be tested on this phone and was not changed.
- uclamp / schedtune / core_ctl: not available to an app on this kernel (2.1).
- The "untimed gap": it is sim steps (2.2); nothing to fix.

## 5. Verified versus assumed
- Verified: ADPF refused and why (`dumpsys performance_hint`); thermal status 0 throughout; the steps per
  frame arithmetic (CSV + the main thread's on / off-CPU profile); the threads' clocks and placement
  (atrace); no uclamp / schedtune for apps (sysctl, kernel config, file mode).
- Verified: the tier pin's soak A/B (one pair) and normal-play check (one pair); that the pin held all run;
  the S25's unchanged mask follows from the rule (not run on an S25).
- Assumed: that the caps' cost is ~10% of a step (two pass-2 windows at slightly different citizen counts,
  not an A/B); the heat cost of the pin (+0.4 C, one pair, within plausible spread).
