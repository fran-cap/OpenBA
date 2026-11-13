# RIVERCHANNEL -- `ext.river_channel`: thin rivers get a channel two tiles wide

Class B, off by default (the Optional tab); settings key `ext.river_channel`, display "Wider thin rivers".
Acts at world generation only: a new game. A loaded save keeps the heights it was saved with.

## 1. What was wrong

Along a thin river the water shows as a staircase of single tiles on every diagonal run, and in places as a
row of separate puddles. Retail draws the same; `ext.better_water`'s shore layers only make it easier to see.

The water mesh is not the thin part. `tools/water_view` (a top-down picture of the heights under the water
mesh, README alongside) shows the river mesh five to eight height cells wide along a tributary, and under it
a strip of ground below the waterline (-0.75) one or two height vertices wide. A game tile is two height
cells (medium map: 768 x 768 heights, 384 x 384 tiles). The visible water is the cut of the water plane
through the terrain's own triangles, and on a diagonal run that cut is a staircase. Seed 12345, medium: near
(-328, -17) the bed lies within 0.1 of the waterline and the river breaks into separate dips; the viewer's
`--thin` lists 52 such stretches.

The height pipeline (`Game/worldgen.cpp`, `wg_HeightmapPipeline_f390`) carves each river along its feature
polyline (`wg_f390_stamp`, `wg_f390_carve`), with droplet erosion between the two and two smoothing passes
after. Which of those leaves the channel shallow was not measured; the extension acts after all of them.

## 2. What the extension does

One seam, after the pipeline's last smoothing pass and before the heights are quantized:
`Ext::RiverChannel::Apply` (`src/ext/river_channel/river_channel.cpp`) walks every feature polyline of the
heightfield and calls the pure carve (`river_channel_math.h`, `CarvePath`).

The carve steps along the polyline every 0.25 height cells. At a step where any height vertex within 2.5
cells of the centreline is dry, as the heights were before the carve, every vertex within 2.5 cells is
lowered to a parabolic bed: 0.35 below the waterline on the centreline, the waterline at 2.5. That is five
vertices across, four height cells, two tiles. A height is only ever lowered.

**Only the thin stretches.** A step whose vertices within 2.5 cells are all under the waterline already is a
river at least that wide: nothing there is touched, so wide rivers and lakes come out bit for bit as retail's.

**Dry stretches fill too.** Where retail's channel does not reach the waterline at all (the river mesh lies
under dry ground and no water shows), the carve gives it water. On seed 12345 the western tributaries, which
retail leaves broken into pieces, become continuous rivers. This follows from the rule above and is the
larger part of the change on that map.

Everything built from the heights after the seam follows them: the quantized heightmap, the splat texture,
the terrain mesh, the water mesh and the map cells' terrain types.

| file | role |
|---|---|
| `src/ext/river_channel/river_channel_math.h` | the carve (pure, unit tested) |
| `src/ext/river_channel/river_channel.{h,cpp}` | reads the heightfield's polylines, one stderr line |
| `src/game/Game/worldgen.cpp` | the `EXT SEAM` |
| `tests/unit/ext_river_channel_test.cpp` | six tests |
| `tools/water_view/`, `src/diag/water_dump.{h,cpp}` | the viewer and its dump (`BANISHED_WATER_DUMP`) |

## 3. Class

B. Nothing new is saved (the heights are part of every save), but with the row on a new map differs from
retail's for the same seed: more water cells along the tributaries, so the starting site, crossings and
fishing differ. Off under `/profile:faithful`.

## 4. Verified, and not

- Unit: the six `ext_river_channel_*` tests.
- Seed 12345, medium, debug, the row on (`WATER_VIEW_EXT=river_channel tools/water_view/water_view.sh`):
  `[RiverChannel] 6453 heights lowered along 4 river path(s)`. In the viewer the tributaries are continuous,
  four to five height cells wide, and the stretch near (-328, -17) is one channel; `--thin` lists 25
  stretches against 52, each a single vertex.
- Bare bed (ground under the waterline that the water mesh does not cover) grows from about 200 to about 270
  square height cells over the whole map: in a few places the polyline runs off the middle of the water mesh
  and the channel reaches the mesh's side. Not looked at in game.
- Gate (faithful, the row off): the world built and ran with an empty crash log. Resave check PASS
  (resave == golden, 23,453,453 bytes). Unit 2373 / 0 failures. Acceptance 0 lines off target.
- In game (debug, DX11, seed 12345 medium, the row and `ext.better_water` on): the user's look, accepted.
- **Not done:** other seeds, sizes and the Mountains terrain; whether a start
  town can end up cut off by a river that retail left dry; perf (one pass over the polylines at world
  generation, not measured).
