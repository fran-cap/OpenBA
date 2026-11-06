# Showroom lists

`BANISHED_SHOWROOM=<list file>` (`src/diag/showroom.h`) places the listed buildings and trees
in a new game, pauses, and screenshots each one with a pinned camera. Same seed + same list =
same layout and framing, so two builds give image pairs to compare by eye (pixel diffs between
runs measure nothing).

| File | What |
|---|---|
| `trees.txt` | one tree of each of the eleven species, at seven months (spring blossom and catkins, summer, autumn) |
| `forest_2.txt` | the tree-rendering bench (issue #253): 60 x 60 mixed forest 2 tiles apart, camera at 130, unpaused |
| `forest_1.txt` | the same forest 1 tile apart (denser, smaller) |

Line forms (`src/diag/showroom_list.h`): `Template\X.rsc` (a building), `tree <species>`,
`month <m>`, `months <m1> <m2> ...`, `forest <species|mixed> <columns> <rows> <spacing>`,
`bench <dist> [yaw] [sweep]`; `#` starts a comment. A month is a float: 0 is the start
of Early Spring, one month is 300 s of year time. With a `bench` line, the first month line sets the calendar
once when the bench starts (for example `month 4.5`, mid-summer: a run's start in winter draws the oaks
and birches bare); at speed 1 a bench run stays inside that month.

## Forest bench

A repeatable "screen full of trees" view for GPU benches of tree rendering, without hunting
for a seed:

- `forest <species|mixed> <columns> <rows> <spacing>` plants a grid of fully grown trees
  (`mixed` = oak, birch and pine by a fixed hash of the grid position; one species name plants
  only that one), `spacing` tiles apart (1..6), at most 200 a side. They are real game trees:
  `Template\NaturalResourceTree.rsc` from the install, placed through world-gen's per-cell
  placement (jitter and yaw from a harness-local seeded rng) and registered, so Better Trees,
  natural growth, shadows, culling and the draw paths treat them as any forest. The grid is sited
  a quarter map east of the town on the origin with the fewest unusable cells (water, unworkable
  ground, a building or the 2 tiles around it), at most a quarter of them; those cells stay bare.
  When no origin passes, the grid shrinks by a tenth a side and the search runs again. The world-gen
  scatter keeps the forest's rectangle (plus 8 tiles) bare. One `[Showroom] forest` line in
  `standalone_error.log` gives the species counts, the grid planted and asked for, the centre and
  the bounds in world coordinates.
- `bench <dist> [yaw] [sweep]` replaces the screenshot sequence: the game is not paused, no shot
  is taken and the showroom never ends the run. The camera stays on the forest's centre at
  `dist` (heading `yaw` degrees, default 45) for the whole run; `sweep` moves it as
  `BANISHED_CAMERA_PIN`'s `,sweep` does (yaw turning, zoom swinging 25 around `dist`, pitch
  sweeping). `[Showroom] bench focus=...` is logged once. Without a bench line a forest adds one
  shot per month pass to the screenshot sequence (`forest_y045.bmp`).

Run it under the stress town: it builds its town as usual, the forest is planted away from it,
and the stress harness's `[Stress]` fps windows and its quit after `BANISHED_STRESS_FRAMES` are
the bench's clock (`BANISHED_STRESS_SPEED` applies). The forest's siting and the world after it
are fixed by seed + list, so the `[Vulkan] frame=` draws / triangles / objects repeat run to run
(two runs of `forest_2.txt`: the same 2960 trees, triangles 2.7321 M and 2.7314 M).
The forest is not excluded from work, but the town does not reach it in a bench of a few thousand
frames: over 3000 frames at speed 1 the triangle count stays within 0.1% (the citizens walking).
The world's weather follows the seed and the list (a different forest size shifts the world rng
after it), so compare runs of the same list.

Windows (the list path is absolute, or relative to the game dir, the process's working directory):

```bash
eval "$(tools/game_dirs.sh env forest functional)"
cp build/cmake-perf-vulkan/OpenBA.exe "$BANISHED_DIR/OpenBA.exe"
BANISHED_STRESS=1 BANISHED_STRESS_SPEED=1 BANISHED_FPS_CAP=off BANISHED_STRESS_FRAMES=3000 \
    BANISHED_SHOWROOM="$(cygpath -w tools/showroom/forest_2.txt)" \
    tools/stress_run.sh --no-build --baseline none --label forest2
tools/game_dirs.sh release forest
```

Android: the app runs in its data folder (`/sdcard/Android/data/org.openba.game/files`,
`EnterRunDirectory` in `src/platform/android/app_android.cpp`), so a relative list path is read
from there. Push the list next to the logs (the app must have started once, so that it made the
folder) and pass the flag through `tools/android.sh run`:

```bash
adb push tools/showroom/forest_2.txt /sdcard/Android/data/org.openba.game/files/forest_2.txt
tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_SPEED=1 BANISHED_FPS_CAP=off \
    BANISHED_STRESS_FRAMES=3000 BANISHED_SHOWROOM=forest_2.txt
tools/android.sh boot-state --wait 600
tools/android.sh pull <dir>          # standalone_error.log: [Showroom] forest, [Stress], [Vulkan] frame=
```

The absolute form `BANISHED_SHOWROOM=/sdcard/Android/data/org.openba.game/files/forest_2.txt`
reads the same file.

## Run

Deploy a build, take a game dir, and launch it by hand (`tools/build.sh run` forces
`/profile:faithful`, which turns the extensions off):

```bash
tools/build.sh deploy
eval "$(tools/game_dirs.sh env showroom functional 900)"
cd "$BANISHED_DIR" && BANISHED_AUTOSTART=1 BANISHED_SEED=12345 \
    BANISHED_SHOWROOM="$(cygpath -w <repo>/tools/showroom/trees.txt)" start OpenBA.exe
tools/boot_state.sh --wait 300        # from the repo root
# then wait for "[Showroom] DONE" in $BANISHED_DIR/standalone_error.log; the exe quits itself
tools/game_dirs.sh release showroom
```

Shots land in `<game dir>/showroom/`:

- `m<month>_NN_tree_<species>_y045.bmp`: one per tree per month (NN is the list index);
- `m<month>_wide_y045.bmp`: the whole tree grid, once per month;
- `NN_<name>_y045.bmp` / `_y225.bmp`: buildings, two headings (with the `m<month>_` prefix
  when the list has month lines).

`standalone_error.log` has one `[Showroom] tree` line per tree naming the template and mesh it
resolved to, and `[Showroom] month` before each month's first shot.

While paused only what reads the calendar directly follows the month (the shader season stamp,
the weather's season step); snow build-up and fruit ripening are sim state and stay where the
new game started.
