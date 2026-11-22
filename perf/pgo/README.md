# Profile-guided optimisation (PGO) for the release exe

The Windows release exe is compiled against a profile set recorded from the stress town:
GCC gets real branch, call and loop counts instead of guesses, and uses them for layout,
inlining and unrolling. Measured 2026-10-26 on the 9800X3D (sim ms / iteration, lower is
better; `perf/results/20261026_10*`):

| perf exe, 800 citizens | 10x town | 50x town | draw ms |
|---|---|---|---|
| GCC 13.1 (the previous toolchain), plain | 0.138 - 0.141 | 0.126 - 0.129 | 1.09 - 1.15 |
| GCC 16.2, plain | 0.134 | 0.123 - 0.124 | 1.09 - 1.14 |
| GCC 16.2 + PGO trained on the 50x run only | not run | 0.112 - 0.114 | 1.08 |
| GCC 16.2 + PGO, the mixed profile below | 0.125 | 0.112 - 0.114 | 1.04 - 1.09 |

So GCC 16 alone is about 3%, PGO on top about 10% at 10x and 12% at 50x on the sim and 5% on
draw. The world hash (`[WorldHash]`) is identical with and without the profile. Hand-placed
`[[likely]]` / `[[unlikely]]` hints on the same hot spots (141 of them, five branches) measured
neutral the same day: the profile does that job, and the sim's cost is dependent loads, not
mispredicts.

## LTO on top (2026-10-26, same day, `perf/results/20261026_13*_lto_ab_*`)
The release exe is also link-time optimised (`BANISHED_LTO`, on by default on Windows). Interleaved
50x runs against the PGO release exe:

| release exe | sim ms / iteration | draw ms | size |
|---|---|---|---|
| -O3 + PGO (two controls) | 0.1084, 0.1092 | 1.07 | 14.0 MB |
| -O3 + LTO, no profile | 0.1190, 0.1228 | 1.10 | 13.7 MB |
| -O3 + PGO + LTO | 0.1010, 0.1020 | 1.02 | 12.1 MB |

LTO alone is no faster than plain -O3; with the profile it is another 7% on the sim and 5% on draw.
The profile is recorded by the NON-LTO instrumented build (an instrumented LTO link fails inside
GCC's lto1 with "missing resolution data" for a vtable) and read by the LTO build: 0
coverage-mismatch warnings, the per-function records match. The static third-party libraries
must be archived by `gcc-ar` / `gcc-ranlib` (tools/build.sh passes them at configure; plain `ar`
leaves ~220 undefined SDL / wmadec references). The LTO link's partitions run in parallel only
when a `make` is on PATH (install_winlibs.sh copies mingw32-make.exe to that name; serial = ~1.5 min,
parallel = ~10 s). The nine `-Wodr` vtable warnings LTO first reported were the compiler-vtable
aliases (`DEFINE_EXE_VTABLE`, a vtable named by its assembler symbol); they were retired on
2026-10-26 (`Object::ClassVptr`, `src/engine/object/class_vptr.h`), and every TU is LTO.

## Files
- `train.sh` -- the recipe: instrumented release build, four training runs, optional release build.
- `profile/` -- the profile set (`*.gcda`, one per object, about 3 MB), committed with each release.
  Keyed on the object's path relative to the build directory (`-fprofile-prefix-path`), so the set
  written here is read by CI's `build/cmake-release`.
- `CMakeLists.txt` `BANISHED_PGO` -- `auto` (default: the release flavour on Windows uses the set
  when it exists), `generate`, `use`, `off`. `BANISHED_PGO_DIR` moves the set.

## Rules
- **One compiler.** GCC refuses a profile written by another version, and ccache keys on the
  profile. The compiler is pinned in `tools/toolchain/install_winlibs.sh`; moving the pin means
  retraining.
- **Retrain for every release** (`perf/pgo/train.sh --build`), on a quiet machine with the mods
  parked. Between releases the set drifts as sources change: a function whose shape no longer
  matches its record is compiled without a profile and GCC warns (`coverage-mismatch`, not an
  error), and the gain erodes until the next retrain. Usually that is all, but a stale set can also
  break the release build: GCC 16.2 has failed with an internal compiler error ("during IPA pass:
  profile") and a multi-GB allocation on files changed since the last retrain. The fix is a retrain;
  until then build the release flavour without the set:
  `CMAKE_EXTRA="-DBANISHED_PGO=off" tools/build.sh build release` (a cache variable: it sticks in
  `build/cmake-release` until set back). The owner's ruling 2026-10-26: releases are infrequent,
  drift between them is accepted.
- **No `gnu::flatten` in the tree.** Only the release flavour links with LTO, and there the attribute
  inlines every reachable callee of the whole program into the one function. On 2026-11-22 a flattened
  `WalkUpdateTreeSnapshotById` (`src/game/Entity/entitylist.cpp`) held 134,786 inline copies and its link
  partition ran past 12 GB without finishing; no other flavour and no PR check shows it. To find such a
  function: `lto1 -fltrans -fdump-ipa-cgraph` on the stuck `*.ltrans<N>.o`, then count
  `is inline copy in` per host.
- **Only the release flavour reads the set by default.** `perf` stays plain so `perf/baselines/`
  keeps comparing like with like; measure a PGO exe with an explicit `-DBANISHED_PGO=use`.
- The training runs are the harness towns, not a saved game: no game content is involved and the
  set carries only counters.

## Training workload
`train.sh` runs the instrumented exe (about 9x slower than plain) on:
1. the default 10x stress town, 3000 frames, a storage barn's panel held open from frame 1200
   (`BANISHED_PANEL_HOLD=StorageComponent`), ending through the pause menu;
2. the 50x town, 3000 frames, a market's panel held open from frame 1500;

3. the 10x town with the registry's default rows on the Vulkan renderer (what a player runs), 3000
   frames, the storage barn's panel;
4. the 50x town with the same rows plus the Experimental tab's replayed frames
   (`ext.render_pipe_replay`), 3000 frames, the market's panel.

Runs 1 and 2 use the faithful profile (every fix and extension off, DirectX 11). Runs 3 and 4 use a
user dir of their own (`build/pgo_userdir`, `BANISHED_USER_DIR`): this machine's `settings.cfg` less
every `ext.` / `fix.` line, so the rows are the registry's defaults and not the trainer's own.

All four are the stress harness's current town: the housing pass and full staffing are on (its defaults
since 2026-11-19). `train.sh` names `--mode faithful` on runs 1 and 2: `tools/stress_run.sh` defaults
to `player` (the registry's default rows), and without the switch the set has no faithful run.

The counters of all runs are summed (GCC merges on exit). Trained on the 50x run alone the
profile gave only -4% at 10x; the mix gives -10% at 10x with nothing lost at 50x.

### Why the extension runs (2026-11-15)
Until this date the set was trained on runs 1 and 2 only. A retrain on that recipe measured SLOWER
than the stale 1.0.3 set where extensions were on (release exe, Vulkan stress town at 50x, the
trainer's own settings with the replayed frames on, order old / new / new / old): sim 0.115 and 0.099
ms / iteration on the old set against 0.128 and 0.129 on the retrained one, draw 2.75 and 2.51 ms
against 3.06 and 3.10. The likely reason, not proven: a fresh faithful-only set records the extension
and Vulkan code as never run, and GCC optimises it as cold; the stale set no longer matched that code,
which was then compiled without a profile.

The four-run set against the stale 1.0.3 set (release exes, 50x, 6000 frames, order old / wide / wide /
old, `perf/results/20261115_*_pgow_*`, nothing else running):

| scene | set | sim ms / iteration | draw ms | fps |
|---|---|---|---|---|
| faithful, DirectX 11 | 1.0.3 | 0.144, 0.139 | 0.90, 0.93 | 620, 614 |
| faithful, DirectX 11 | four runs | 0.132, 0.156 | 0.78, 0.80 | 736, 651 |
| default rows, Vulkan | 1.0.3 | 0.107, 0.101 | 0.99, 1.01 | 622, 643 |
| default rows, Vulkan | four runs | 0.105, 0.108 | 1.02, 1.02 | 628, 627 |

Read: no loss in either scene; faithful draw about 14% faster; the sim figures are inside the
run-to-run spread (two runs of one exe differ by up to 18%). Not measured: a release exe without any
profile in the default-rows scene, so what PGO is worth there is open (the 1.0.3 set logged about
38,000 profile-mismatch warnings in that build and performed the same as the fresh set).

## Other platforms
The recipe ports, the files do not. **Android** has its own (`perf/pgo/android/README.md`, P12): the
NDK's clang, `-fprofile-generate` on the device, the `.profraw` files pulled with adb and merged with
`llvm-profdata` into `perf/pgo/android/openba.profdata`, which the release apk reads. Linux (g++) would
need its own set, trained in WSL2 on lavapipe (the sim is CPU-bound, lavapipe only slows the draw);
macOS is clang like Android. None of them can train in CI (no game data), each set is a committed
artifact like this one.

## How the numbers were taken
`tools/stress_run.sh --no-build --speed 50 --frames 3000` and the 10x default, two runs per exe,
interleaved with plain-main controls, `machine load: CLEAN` on every run; the exe was copied into
the game directory by hand between runs (`exe_identity.txt` records whose it was).
