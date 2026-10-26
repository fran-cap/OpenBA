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

## Files
- `train.sh` -- the recipe: instrumented release build, two training runs, optional release build.
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
  error); nothing breaks, the gain just erodes until the next retrain. The owner's ruling
  2026-10-26: releases are infrequent, drift between them is accepted.
- **Only the release flavour reads the set by default.** `perf` stays plain so `perf/baselines/`
  keeps comparing like with like; measure a PGO exe with an explicit `-DBANISHED_PGO=use`.
- The training runs are the harness towns, not a saved game: no game content is involved and the
  set carries only counters.

## Training workload
`train.sh` runs the instrumented exe (about 9x slower than plain) on:
1. the default 10x stress town, 3000 frames, a storage barn's panel held open from frame 1200
   (`BANISHED_PANEL_HOLD=StorageComponent`), ending through the pause menu;
2. the 50x town, 3000 frames, a market's panel held open from frame 1500.

The counters of both runs are summed (GCC merges on exit). Trained on the 50x run alone the
profile gave only -4% at 10x; the mix gives -10% at 10x with nothing lost at 50x.

## Other platforms (later)
The recipe ports, the files do not. Linux (g++) needs its own set, trained in WSL2 on lavapipe
(the sim is CPU-bound, lavapipe only slows the draw). macOS and Android are clang:
`-fprofile-instr-generate`, merge the `.profraw` with `llvm-profdata`, build with
`-fprofile-instr-use`; Android trains on the device and pulls the raw profile with adb. None of
them can train in CI (no game data), each set is a committed artifact like this one.

## How the numbers were taken
`tools/stress_run.sh --no-build --speed 50 --frames 3000` and the 10x default, two runs per exe,
interleaved with plain-main controls, `machine load: CLEAN` on every run; the exe was copied into
the game directory by hand between runs (`exe_identity.txt` records whose it was).
