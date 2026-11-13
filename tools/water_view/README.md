# water_view — a top-down picture of the water mesh over the terrain

For questions about where water shows and why: thin rivers, holes, a mesh that stops short of a bank.
The game writes one binary dump; a script draws it from above, per pixel, on the terrain's own triangles.

## Dump a map

```bash
tools/build.sh build debug
tools/water_view/water_view.sh build/water_view/s12345 12345 medium      # a new game: seed, size
WATER_VIEW_SAVE=MyTown tools/water_view/water_view.sh build/water_view/mytown   # a save by name
WATER_VIEW_EXT=river_channel tools/water_view/water_view.sh build/water_view/on  # rows on for this run
```

The script takes the functional game dir, runs with `BANISHED_WATER_DUMP=<outdir>` (`src/app/options.h`,
`src/diag/water_dump.h`), quits through the pause menu and leaves `<outdir>/water_dump.bin` plus the run's
logs. The dump is written where `ext.better_water` makes its copy of the water mesh, so the run uses the
settings.cfg profile and needs that row on (its default); under `/profile:faithful` nothing is written.

The file holds the int16 heightmap, retail's water vertices, the copy the extension draws (river rows
relaid, lakes faded), the indices and both range tables (rivers, lakes).

## Draw it

```bash
tools/water_view/water_view.py DUMP --out map.png                       # the whole map, 3 px per height cell
tools/water_view/water_view.py DUMP --out a.png --at=-215,-25 --radius 26 --scale 22
tools/water_view/water_view.py DUMP --thin                              # list the thin stretches
tools/water_view/water_view.py DUMP --thin --crops 6 --out thin/        # and draw the first six
tools/water_view/water_view.py DUMP --out b.png --at=-215,-25 --carve 1.5   # preview a least channel
```

Write `--at=X,Y` with the `=`: a leading minus is read as an option otherwise. Coordinates are the
heightmap's (index minus half the grid), the same as the water mesh's position / 48. A game tile is two
height cells (medium map: 768 x 768 heights, 384 x 384 tiles).

| colour | meaning |
|---|---|
| tan to green | land, by height above the waterline (-0.75) |
| blue, light to dark | water: the terrain is under the waterline and the water mesh covers it; darker = deeper |
| magenta | bare bed: under the waterline with no water mesh over it |
| yellow / cyan lines | the water mesh's triangles, river / lake (`--scale` 6 and up) |
| red lines | retail's river mesh under the copy's (`--mesh both`) |
| white / brown dots | a height vertex under / above the waterline (`--scale` 10 and up; its height above the waterline is printed at 36 and up) |

`--mesh retail` covers and outlines with retail's mesh instead of the extension's copy.

`--thin` lists the stretches near a river where a wet height vertex belongs to no cell with four wet
corners (the channel is one vertex wide there), most diagonal first. `diagonal` counts the wet vertices
whose only wet neighbours are cornerwise: the staircase the water shows along a thin river.

`--carve HALF_WIDTH` (`--carve-depth`, default 0.35) is a preview, not something the game does: it lowers
the heights along every river's centreline (the middle vertex of each mesh row) to a parabolic channel
that many height cells to each side, and draws or lists the result. Heights are only ever lowered.
