# MAPOVERLAYS -- `ext.map_overlays` (class C, default OFF)

Block MAPOVERLAYS, 2026-11-06. SimCity-style data views in the map window: where goods are produced, consumed
and stored, where services reach, where citizens walk, how homes fare, and where the natural resources are.
One tab per view beside retail's map, and a "Show overlay on terrain" check box that draws the same heat map
on the ground.

## 1. Retail

The map window is the `UtilityMap` tool: `ToolUI` (StandardDialog.rsc's controller) reads the entity's
`UIDescription` and injects each sub-controller at its `_insertAt` element; for the map that is `MapUI` with
`Dialog/Map.rsc:content` (`imageMap`, `imageOverlay`, `frustum`). `MapUI` shows two images of one pixel per
tile: the painted terrain (`Minimap::terrainPixels`) and the building footprints (`Minimap::overlayPixels`,
painted by `MapComponent`). Retail has no spatial view of any flow; the Town Hall shows town totals only.

## 2. The views

| tab | kind | what a tile's value is |
|---|---|---|
| Map | retail | -- (MapUI unchanged) |
| Production | history | items of the selected resources produced at the tile in the last N months |
| Consumption | history | items consumed at the tile (eaten, burned, used as input, used to build) |
| Stock | instant | items held at the tile now (storage, stockpiles, markets, homes) |
| Services | instant | coverage of a service kind (market, well, herbalist, chapel, school, tavern, town hall): 1 inside a building's radius, more where radii overlap |
| Traffic | history | footsteps (a citizen entering the tile) in the last N months |
| Citizens | instant | homes by a measure (happiness, health, food, warmth, age), averaged over the residents |
| Natural | instant | trees, stone, iron, wildlife, fish |

Resource pickers list the materials of the running game (the factory walk of RawMaterialDescription, as
`ext.restricted_foods` does), grouped by the `_flags` categories, with each material's own sprite. A mod's
material appears with no code; a mod's flag bits above the retail ones show as "Other".

## 3. The save (class C)

Test of DESIGN.md: a reload must restore the history for the views to read as before, so the row is class C.
The history travels in the extension's tail record (`save_state.cpp`, wire format v1), never in a retail
class. Payload, little-endian:

```
u32 mapSize  i32 now  i32 bucketStamp[12]  i32 trafficStamp[4]
u32 resourceCount  { u16 len  bytes path } * resourceCount
u32 siteCount  { u16 x  u16 z  u16 cellCount  { u16 resource  u8 flow  u32 counts[12] } * cellCount } * siteCount
u32 trafficCells  { u16 count[4] } * trafficCells^2        (trafficCells = ceil(mapSize / 2))
```

Block 1 is compressed, so the empty traffic cells cost almost nothing. A site is a tile, not an entity: no
binding after a load. Resources are paths, so a changed mod list keeps the save loadable. The decoder refuses
a cut payload, trailing bytes, a map size outside 1..4096, a count past the payload, or an index outside the
table; the history is then empty and the game plays on.

## 4. Seams

One test of `Ext::MapOverlays::g_on` each, above any loop. With the row off: one load and a not-taken branch.

| seam | event |
|---|---|
| (hooks agent fills this table: every place an item is created into an inventory by work, and every place one is destroyed by use) | |
| movement: a citizen enters a new tile | `OnStep` |
| `GameUpdateLoop::Update` | `BeginFrame` |
| the two tool-draw seams (beside `Ext::ShowGrid::Draw`) | `DrawOnTerrain` |
| the map window | the overlay tabs |

**Cost rule for `OnStep`.** It runs per citizen per tile crossed. The body is an index computation and a
saturating 16-bit add; no lookup, no allocation, no lock. Measured: stress run faithful vs row on.

## 5. The picture

`overlay_math.h`. The slider maps 0..1 to a blur radius of 0..24 tiles (0 = exact per building). The value at
full colour is a high percentile of the non-zero tiles. The ramp is transparent at 0, rising through blue,
green, yellow to red. The image is rebuilt at most once a second while a view shows, at once on a change of
selection; the minimap and the terrain upload only when its version changes.

## 6. Files and owners (the agent team)

| file | owner |
|---|---|
| `overlay_core.h`, `map_overlays.h`, `overlay_math.h`, this file §1-§3, §5 | coordinator (the contract; changes go through the coordinator) |
| `overlay_core.cpp`, `save_state.cpp`, `map_overlays_town.cpp`, registry row, `state_codecs.cpp`, unit tests of core + codec | history |
| the seams in `src/game` (§4 table) | hooks |
| `overlay_math.cpp`, `overlay_view.cpp`, `overlay_views.cpp`, unit tests of math | views |
| `src/ext/ui/Dialog/MapOverlays*.rsc`, strings, the map window tabs and controller (`map_window.cpp`) | UI |
| `overlay_render.cpp`, the draw seams | render |

## 7. Verification

- `tools/build.sh unit`: core, codec (round trip, every malformed case), math.
- Faithful gate (`tools/build.sh run`) and resave check: zero bytes with the row off.
- Row on: a town played a year, saved, reloaded: the views read the same.
- A save with a mod's resource loads without the mod: the path stays in the table and matches nothing.
- Stress run faithful vs row on (DESIGN.md hook rule 6).
- No game content: the window is our own `.rsc`; retail's map content is used by reference.
