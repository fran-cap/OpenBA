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

| t (s) | fps | steps per drawn frame | update ms per step | 60 x update (% of a second) | draw main | pipe wait | citizens |
|---|---|---|---|---|---|---|---|
| 10 | 57.8 | 1.04 | 8.17 | 49% | 4.31 | 3.64 | 803 |
| 100 | 46.5 | 1.29 | 9.91 | 59% | 4.26 | 4.31 | 954 |
| 310 | 39.5 | 1.52 | 10.83 | 65% | 4.62 | 4.48 | 1010 |
| 341 | 29.8 | 2.01 | 11.51 | 69% | 4.88 | 5.36 | 1012 |
| 372 | 21.5 | 2.79 | 12.86 | 77% | 5.40 | 7.07 | 1016 |

(this pass's diagnostic run; steps per drawn frame = `sim_iters / speed / drawn frames` of the CSV window.)
The throttled frame of pass 2 adds up exactly: 2.7 steps x 12.4 ms + draw main 5.0 + wait 7.0 = 45.5 ms
against the measured 45-46 ms. The profile agrees: in the throttled window the main thread is on a CPU
82% of the time (atrace), and 681 of its 685 on-CPU ms a second are under `MainLoop::Update`; its off-CPU
time (161 ms a second, 7.6 ms a drawn frame) is all `Ext::RenderPipe::Hook`, the pipe's waits. Nothing
else blocks it.

So fps ~ (1000 - 60 x update) / (draw main + wait + ~0.6) ms: the sim's real-time demand is fixed, and the
drawn frames share the remainder. Near saturation this amplifies: at update 12.4 ms a step, 1 ms more
per step costs ~25% of the fps. Fitted over pass 2's whole soak, the formula tracks the measured fps
within ~2 fps up to ~600 s and underestimates after (it is a bound, the late windows' update means carry
hitches).

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

## 4. Verified versus assumed
- Verified: ADPF refused and why (`dumpsys performance_hint`); thermal status 0 throughout; the steps per
  frame arithmetic (CSV + the main thread's on / off-CPU profile); the threads' clocks and placement
  (atrace); no uclamp / schedtune for apps (sysctl, kernel config, file mode).
- Assumed: item 1's estimate; that the caps' cost is ~10% of a step (two pass-2 windows at slightly
  different citizen counts, not an A/B).
