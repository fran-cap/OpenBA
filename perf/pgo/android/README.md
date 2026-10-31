# Profile-guided optimisation for the Android apk (P12)

The Android release library (`libmain.so`, the release flavour since P12) is built by the NDK's clang,
so the Windows profile set (`perf/pgo/profile/`, GCC's `.gcda` files) cannot be reused. This folder holds
the clang profile the release build reads: `openba.profdata`, one merged file, committed like the Windows
set. While it is absent the release apk is plain -O3 + ThinLTO (`CMakeLists.txt`, `BANISHED_PGO=auto`).

## Recipe (a phone over adb, from the repository root)
```bash
tools/android.sh build release --pgo generate       # the instrumented apk (no LTO: the profile build)
tools/android.sh sign && tools/android.sh install release
adb shell rm -rf /sdcard/Android/data/org.openba.game/files/pgo   # no counters of an older build
tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_FRAMES=3000 \
                     BANISHED_PANEL_HOLD=StorageComponent BANISHED_PANEL_HOLD_FRAME=1200   # the 10x town
# wait for the clean exit: boot-state --wait returns at RUNNING; poll until EXITED clean-shutdown
tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_SPEED=50 BANISHED_STRESS_FRAMES=3000 \
                     BANISHED_PANEL_HOLD=MarketComponent BANISHED_PANEL_HOLD_FRAME=1500    # the 50x town, the same way
tools/android.sh pgo-merge                           # pulls <data>/pgo/*.profraw, merges -> openba.profdata
rm -rf android/app/.cxx/RelWithDebInfo/<the release dir>   # see below: auto is decided at configure
tools/android.sh build release                       # reads openba.profdata (auto) + ThinLTO
```
The stress harness ends each town itself at its last frame (`EXITED clean-shutdown`). The instrumented
library writes `pgo/openba-<hash>.profraw` into the app's data folder at every clean
quit (`%m`: the runs of one build merge into that one file) (`src/platform/android/app_android.cpp`, Shutdown: Android ends the process before any atexit
handler, so the runtime is asked to write explicitly). A run killed from recents writes nothing. The
training mix is the Windows one (`perf/pgo/README.md`, "Training workload"): the 10x town with a panel
held, then the 50x town; both counters merge.

`BANISHED_PGO=auto` tests for the file when CMake configures, and Gradle does not reconfigure an existing
native build dir because a file appeared: a release dir configured before the first `pgo-merge` keeps
building plain until it is removed (the one whose `CMakeCache.txt` has `BANISHED_FLAVOUR=release` and
`BANISHED_PGO=auto`). A fresh checkout (CI, the release workflow) configures with the file present.

## Rules
- **One compiler.** The profile is read by the NDK pinned in `android/app/build.gradle`; a new NDK
  means a retrain (an old profile is a warning, `profile-instr-out-of-date`, never an error).
- **Retrain for every release**, on a cool phone with nothing else running; `pgo-merge` keeps the raw
  files under `perf/pgo/android/raw/` (not committed) so later runs can be added to the merge.
- The profile carries counters only; no game content is involved.
- Every clang build carries `-fno-delete-null-pointer-checks` (`CMakeLists.txt`). The first
  instrumented apk crashed at the main menu without it: clang hoisted `StringTable::GetText`'s
  `count` load above its `index < 0` test, through the null `this` of a never-assigned
  `UI::String {null, -1}` that retail relies on being harmless.
- The `perf` flavour (`build release --flavour perf`) never reads it, so the P7 numbers in
  `docs/android/PERF_P7.md` keep comparing like with like.
