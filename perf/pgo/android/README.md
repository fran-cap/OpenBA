# Profile-guided optimisation for the Android apk (P12)

The Android release library (`libmain.so`, the release flavour since P12) is built by the NDK's clang,
so the Windows profile set (`perf/pgo/profile/`, GCC's `.gcda` files) cannot be reused. This folder holds
the clang profile the release build reads: `openba.profdata`, one merged file, committed like the Windows
set. While it is absent the release apk is plain -O3 + ThinLTO (`CMakeLists.txt`, `BANISHED_PGO=auto`).

## Recipe (a phone over adb, from the repository root)
```bash
tools/android.sh build release --pgo generate       # the instrumented apk (no LTO: the profile build)
tools/android.sh sign && tools/android.sh install release
tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_FRAMES=3000 BANISHED_PAUSEMENU=3000 \
                     BANISHED_PAUSEMENU_QUIT=120 BANISHED_PAUSEMENU_BUTTON=6   # the 10x town, quit through the pause menu
tools/android.sh boot-state --wait 600
tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_SCALE=50 ...             # the 50x town, the same way
tools/android.sh boot-state --wait 900
tools/android.sh pgo-merge                           # pulls <data>/pgo/*.profraw, merges -> openba.profdata
tools/android.sh build release                       # reads openba.profdata (auto) + ThinLTO
```
The instrumented library writes `pgo/openba-<hash>.profraw` into the app's data folder at every clean
quit (`src/platform/android/app_android.cpp`, Shutdown: Android ends the process before any atexit
handler, so the runtime is asked to write explicitly). A run killed from recents writes nothing. The
training mix is the Windows one (`perf/pgo/README.md`, "Training workload"): the 10x town with a panel
held, then the 50x town; both counters merge.

## Rules
- **One compiler.** The profile is read by the NDK pinned in `android/app/build.gradle`; a new NDK
  means a retrain (an old profile is a warning, `profile-instr-out-of-date`, never an error).
- **Retrain for every release**, on a cool phone with nothing else running; `pgo-merge` keeps the raw
  files under `perf/pgo/android/raw/` (not committed) so later runs can be added to the merge.
- The profile carries counters only; no game content is involved.
- The `perf` flavour (`build release --flavour perf`) never reads it, so the P7 numbers in
  `docs/android/PERF_P7.md` keep comparing like with like.
