# Issue #253: per-feature render cost on the S25, and what was changed

Device: Galaxy S25 (SM-S931U, Snapdragon 8 Elite, Adreno 830), Vulkan, perf flavour apk signed with the
debug key. Town: the stress town (`BANISHED_STRESS=1`, 800 citizens), `BANISHED_STRESS_SPEED=1`,
`BANISHED_FPS_CAP=off`, render scale 50, every graphics extension off unless named.

## 1. Cost of each Enhanced feature (main 3a6314072)

| feature | fps | vs base | main-thread draw ms |
|---|---|---|---|
| base | 256.6 / 263.4 | - | 3.65 / 3.42 |
| Reflections terrain | 255.4 | ~0% | 3.58 |
| Reflections everything | 204.4 | -21% | 4.49 |
| Shadows High (`VideoShadowScalar=0`) | 240.2 | -7% | 3.92 |
| Better Trees | 246.1 | -5% | 3.84 |
| Better Trees + Natural Growth | 219.1 | -15% | 3.81 |
| AO Medium | 212.8 | -17% | 3.96 |
| zoom 200, Better Camera off (`BANISHED_CAMERA_PIN=town,200`) | 103.7 | -60% | 9.02 |
| zoom 200, Better Camera on | 65.9 | -75% | 12.86 |

Per-group split (the `[Stress::pipe] split` counters): at the default zoom pass 1 costs main 0.80 ms and
shadow 1.47 ms; at zoom 200 main 4.55 ms, shadow 5.07 ms, snapshot copy 2.90 ms. The shadow group gathers as
many items as the main view (16.1 k against 16.7 k at zoom 200). The level-0 fallback past the last LOD
switch distance is 0% at both zooms. Far-zoom batches average 42.6 instances and mostly end at the 52 cap.

## 2. Measuring on this phone

- **Throttling.** The S25 throttles from SKIN 38 C (`dumpsys thermalservice` thresholds 38/40/42/45). A
  100 s uncapped run ends at SKIN 43-44 C with thermal status 2 and fps decaying inside the run. The
  battery temperature lags the SKIN sensor; gate a cooldown on SKIN (the "Current temperatures from HAL"
  block, not "Cached temperatures"). While charging over USB the phone idles at about SKIN 34-36 C.
- **A flat frame-rate cap** at about 153 fps (and 88 / 65 fps plateaus at zoom 200) appears mid-run
  regardless of the workload, most likely the vendor's game optimiser. fps comparisons are valid only in
  windows before it engages.
- **Speed 10 is sim-bound.** At the stress harness's default speed the frame is mostly simulation; render
  costs are only visible at `BANISHED_STRESS_SPEED=1`.
- **Instructions per frame** (`simpleperf stat --app org.openba.game --per-thread -e instructions,cpu-cycles`,
  divided by the frames in the window) are clock-independent; they need `<profileable android:shell="true"/>`
  in the manifest of a local measurement build (not committed). At the default zoom they repeat within
  about 3%; at zoom 200 the render thread flips between about 22 M and 27 M per frame on the same build, so
  differences under about 10% are not resolvable there.
- **Play Protect** asks to scan every adb install of a debug-signed apk and the dialog pauses the game
  (`settings put global verifier_verify_adb_installs 0` stops it for adb installs).
- **The app folder.** If adb creates `/sdcard/Android/data/org.openba.game` (for example by restoring a
  backup into a fresh install), the app cannot read the harness token or its WinData. Remove the folder,
  start the app once, then `tools/android.sh push-data`.

## 3. Results of perf/issue253-android-render

| change | output | measured |
|---|---|---|
| `ext.staggered_passes` (shadow and reflection every 2nd frame, alternating, held map sampled with its own sun matrix; refresh on view change) | changes the image (one frame of lag on moving casters); row default OFF | reflections everything 210 -> 254 fps, CPU work per frame -30%; Shadows High 233 -> 256 fps, -26%. Ruled imperceptible on the phone while panning |
| AO: 1x depth read directly, post-AO pass discards depth, apply folded into the Blended main pass | identical | AO Medium 212.8 -> 223.6 fps |
| AO Lowest tier (`ext.ao_quality=4`, ASSAO quality -1) | new tier | 239.2 fps (Medium 223.6, off 257.8) |
| fused shadow + main walk in ext.draw_gather; per-Draw instance-transform memo; lean render_pipe snapshot builder; falling-leaves scan window | identical (COMPPROF=4: DrawGatherFused, XformMemo, DrawGatherSide 0 mismatches) | not resolvable on the S25 (below the run-to-run noise); kept by ruling |
| natural growth's pixel copy without the blossom outside the bloom window | identical | not testable in the stress town: it starts in spring, inside the window |
| stress counters (LOD fallback, per-shader gather and batch rows, per-group CPU split) | harness only | - |
