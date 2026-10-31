# Showroom lists

`BANISHED_SHOWROOM=<list file>` (`src/diag/showroom.h`) places the listed buildings and trees
in a new game, pauses, and screenshots each one with a pinned camera. Same seed + same list =
same layout and framing, so two builds give image pairs to compare by eye (pixel diffs between
runs measure nothing).

| File | What |
|---|---|
| `trees.txt` | one tree of each of the eleven species, at seven months (spring blossom and catkins, summer, autumn) |

Line forms (`src/diag/showroom_list.h`): `Template\X.rsc` (a building), `tree <species>`,
`month <m>`, `months <m1> <m2> ...`; `#` starts a comment. A month is a float: 0 is the start
of Early Spring, one month is 300 s of year time.

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
