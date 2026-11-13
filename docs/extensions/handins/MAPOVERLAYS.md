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
| Traffic | history | footsteps (a citizen entering the tile) in the last N months: all of them, only walks to the walker's workplace, or only walks to its home (three buttons, All / To work / To home) |
| Citizens | instant | homes by a measure (happiness, health, food, warmth, age), averaged over the residents |
| Natural | instant | trees, stone, iron, wildlife, fish |
| Land | instant | a class, not an amount: buildable (green), buildable once the trees, rocks or ore on it are cleared (amber), not buildable: water, steep ground, mountain (red). Built-on tiles (buildings and their yards, roads, fields, bridges) are left clear |

**Land** is the placement tool's own test of one footprint cell, `Map::TestPlacement` (0x14009f5d0), for an
ordinary building: the cell's `PathType` is in the building's place bits, `MapCell::blocker` is null and
`MapCell::footprintCount` is 0 (`land_class.h`). The place bits are per building; the view uses
`Normal | Obstacle`, which nearly every building has. The ones that differ need the other ground (docks and
bridges onto water, mines and quarries on the hillside), so red reads "no ordinary building". The view is
exact per tile (no blur, no scale slider) and its page is a three-row key in place of the ramp. On the
terrain it is drawn from a point-sampled texture of the image (white triangles with texture coordinates, on
an instance of the Selection material's Material), so a tile keeps its own colour at any zoom; the heat views
keep their vertex colours, which blend between the cells of a far view.

**Traffic's filter** (2026-11-12). Each footstep is recorded under the Trip of its walk
(`map_overlays_town.cpp` `TripOf`): the walk's destination tile (`MoveTo::destination`) lies in, or one tile
from, the footprint of the citizen's workplace (to work) or of its home (to home); every other walk is
"other" (hauling, a walk to a tree, a pile or a storage, nomads, traders). A long bright trail under To work
is a long commute. A forester's or gatherer's walk to a tree is not a walk to the workplace. Footsteps
recorded before the filter existed (a wire v1 save) are "other": they show under All only.

Resource pickers list the materials of the running game (the factory walk of RawMaterialDescription, as
`ext.restricted_foods` does), grouped by the `_flags` categories, with each material's own sprite. A mod's
material appears with no code; a mod's flag bits above the retail ones show as "Other".

**The map's size.** Retail's map is a fixed 192-unit square (`Map.rsc`). The row is as tall as its tallest
cell, so when the button column is taller (nine finger-sized buttons under `ext.phone_menu`) the map takes
the column's height (`MapWindowController::FitMap`): the two images and the camera outline are resized
together, and retail's click and outline code read the elements' own sizes. Never smaller than retail's.

## 3. The save (class C)

Test of DESIGN.md: a reload must restore the history for the views to read as before, so the row is class C.
The history travels in the extension's tail record (`save_state.cpp`, wire format v2), never in a retail
class. Payload, little-endian:

```
u32 mapSize  i32 now  i32 bucketStamp[12]  i32 trafficStamp[4]
u32 resourceCount  { u16 len  bytes path } * resourceCount
u32 siteCount  { u16 x  u16 z  u16 cellCount  { u16 resource  u8 flow  u32 counts[12] } * cellCount } * siteCount
u32 trafficCells  { u16 count[3][4] } * trafficCells^2     (trafficCells = ceil(mapSize / 2); [Trip][season])
```

v1 (before the Traffic filter) has `{ u16 count[4] }` per traffic cell and is otherwise the same; it is read
with its counts as Trip "other". A v2 save does not load in a build that only knows v1 (the tail's newer
`formatVersion` rule).

Block 1 is compressed, so the empty traffic cells cost almost nothing. A site is a tile, not an entity: no
binding after a load. Resources are paths, so a changed mod list keeps the save loadable. The decoder refuses
a cut payload, trailing bytes, a map size outside 1..4096, a count past the payload, or an index outside the
table; the history is then empty and the game plays on.

## 4. Seams

One test of `Ext::MapOverlays::g_on` each, above any loop. With the row off: one load and a not-taken branch.

The flow seams are where retail itself counts production (`CitizenBehavior::CountProducedMaterial`
0x140088ef0 and its inlined copy in the pasture, the `+0x170[cat]._field10` tally) and consumption (the
`+0x170[cat].+0x0C` tally), plus the trade purchases retail registers but does not tally. An item moved between
storages (pickup, drop-off, a storage absorbing a pile, the market, the move on a reassignment) is not a flow
and has no seam.

| seam | event | flow | what is counted, and the site |
|---|---|---|---|
| `Action/createrawmaterial.cpp` `CreateRawMaterial::AbstractImpl` | each pile spawned by work, beside `CountProducedMaterial` | produced | the pile's items. The site is the workplace (`+0x30`); a pile with no workplace counts at its own tile. Covers gatherers and orchards (0x1400bb7b0), fishing (0x1400beae0), every `ConsumeProduce` output, mines and quarries included (0x1400bef70), and hunted or slaughtered animals: meat, leather (0x1400bbea0, from the hunter and the pasture) |
| `Action/action_base_cnr.cpp` `NaturalResourceComponent::RemoveResource` | a natural resource removed, beside `CountProducedMaterial` | produced | its yield. The site is the citizen's workplace, or the pile's tile when there is none. Covers crop fields, foresters, herbalists, clearing by labourers (logs, stone, iron, food), and a resource whose fall animation ends with no worker |
| `Component/pasturecomponent_states.cpp` (pasture produce) | each produce pile, beside the inlined tally | produced | wool, milk, eggs: `_lowCreateCount` per pile, at the pasture |
| `Dialog/tradeui_checkout.cpp` (checkout) | buy row, raw material, `AddItem` | produced | the units bought, at the trading post. Retail does not tally a purchase as production; the overlay does, so imports show where they enter |
| `Dialog/tradeui_checkout.cpp` (checkout) | sell row, `SubtractItem` | consumed | the units sold, at the trading post (retail tallies them as consumed) |
| `Component/tradecomponent_dock.cpp` `TradeComponent::AutoTrade` | the delivery loop, `AddItem` | produced | auto-trade purchases, at the trading post (as the checkout) |
| `Component/tradecomponent_dock.cpp` `TradeComponent::AutoTrade` | the payment loop, beside the `+0x0C` tally | consumed | one unit per sale, at the trading post |
| `Action/consumerawmaterial.cpp` `ConsumeRawMaterial::AbstractImpl` | each material, beside the `+0x0C` tally | consumed | workshop inputs, tools, clothing, herbs, road stone. The site is the holder's MapComponent (a workshop, a home or a storage); an item a citizen carries counts where the citizen stands (`OnConsumed`) |
| `Component/citizencomponent.cpp` `CitizenComponent::EatFrom` | each unit eaten | consumed | one food, at the home, or at the storage when the citizen eats straight from one |
| `Component/happinesscomponent.cpp` `HappinessComponent::OnMessage` | a good spent by a happiness building | consumed | one unit (ale and the like), at the building. The material is read before the subtract, so the overlay counts the spent good under both arms of `fix.tavern_counter` |
| `Component/residence_heattick.cpp` (the burn branch) | one fuel burned | consumed | firewood or coal, at the home |
| `Component/buildcomponent_state8.cpp` (build complete, refund loop (a)) | each storage slot of a finished site | consumed | the construction materials, at the building. This is when retail tallies them |
| movement: `Action/action_base_move.cpp` `MoveTo::UpdateFacing` | a waypoint reached (`pathIndex > 1`; `path[0]` is the start cell) | `OnStep` | one footstep at `path[pathIndex - 1]`, under the walk's Trip (section 2). Faithful `MoveTo::Execute` case 2, `ext.walk_batch`'s and `ext.walk_batch_grid`'s batched `Step` (both call the real `UpdateFacing`) and `ext.fast_paths` (the same per-cell path format) all pass here. Every `MoveTo` walker is counted: citizens, nomads and traders on foot |
| `GameUpdateLoop::Update` | `BeginFrame` | | |
| `Renderer::Draw` pass 2 and ext.render_pipe's copy, after the opaque list of the main scene (`Renderer::_extGroundDraw`, armed by `GameUpdateLoop::Draw` step 7 through `Ext::GroundDraw`, `src/ext/ground_draw.h`) | `DrawOnTerrain` | | |
| the map window | the overlay tabs | | |

Not hooked: demolition refunds (`destroycomponent_states.cpp`, piles of a demolished building's materials).
Retail does not tally them, and they return materials already counted as consumed. Game-start stock
(`starttown.cpp`, `town_storage_seed.cpp`, `game.cpp`) and the harness seeding are not work.

**Placement in the hot paths.** The pile loop (`CreateRawMaterial`), the pasture loop, `ConsumeRawMaterial`,
`EatFrom`, the build refund loop and the two trade functions read `g_on` once into a local above their
loop. `HappinessComponent::OnMessage` and the burn branch test it inside a branch that runs once and then
returns. `OnStep` sits in `UpdateFacing`, which runs once per tile a walker crosses, never per tick: there is
no test in the per-character tick (`CharacterComponent_MoveTick_1094c0`) or in the copy-pinned bodies of the
move tick or `MoveTo::Execute`.
| `GameUpdateLoop::Update` | `BeginFrame` |
| `Renderer::Draw` pass 2 and ext.render_pipe's copy (`Renderer::_extGroundDraw`, `src/ext/ground_draw.h`) | `DrawOnTerrain` |
| the map window | the overlay tabs |

**Cost rule for `OnStep`.** It runs per citizen per tile crossed. The body is the Trip (three component
lookups: the walker's CitizenComponent, the MapComponent of its workplace and of its home), an index
computation and a saturating 16-bit add; no allocation, no lock. Measured before the Trip was added: stress
run faithful vs row on (section 7); not re-measured with it.

## 5. The picture

`overlay_math.h`. The slider maps 0..1 to a blur radius of 0..24 tiles (0 = exact per building). The value at
full colour is a high percentile of the non-zero tiles. The ramp is transparent at 0, rising through blue,
green, yellow to red. The image is rebuilt at most once a second while a view shows, at once on a change of
selection; the minimap and the terrain upload only when its version changes.

Near, steep ground (2026-11-11): a coarse quad floats over a hillside by the cell's relief, which a near
camera with a long view showed as a sheet standing off the slope and past the ridge (a player's "red aura on
top of mountains"). `RefineCells` (`overlay_render_math.h`) picks the cells whose float is over 1% of their
distance from the eye, worst first, up to 8192 tiles a frame; they are drawn tile by tile on the ground, their
edge vertices on a coarse neighbour's edge so the two meet with no gap or overlap (a first version without that
stitch showed dark lines where two sheets blended twice). Checked with the coarse step forced to 4 at a close
view of a player's save, Vulkan: before, the picture floats over ridges, the river and tree bases; after, it is
the per-tile picture.

A new game (2026-11-11): the renderers keep nothing that points at the last game's Externals. The game's
Materials are freed with the game and an `ExternalPtr` holds no reference, so `OnGameStartRender`
(`overlay_render.cpp`, from `OnGameStart`) drops the map window's sheet and material, each terrain texture's
MaterialInstance and the snapshot's material; they are made again at the next use. Before, the Land view on
the terrain in a second game of one process drew with the first game's freed Material (a player's report:
the game aborts on "Show overlay on terrain"). Repro: `BANISHED_AUTOSTART_GAMES=2` with
`BANISHED_PAUSEMENU_BUTTON=5` and the view forced on in each game.

## 6. Files

| file | what it holds |
|---|---|
| `overlay_core.h` / `.cpp` | the History (sites, resource table, month buckets, traffic grid) and its wire format; standard C++ |
| `save_state.cpp` | the class C codec and `Town()` |
| `map_overlays.h`, `map_overlays_town.cpp` | the game-side seams: `g_on`, `BeginFrame`, the flow and footstep entry points, site resolution |
| `overlay_math.h` / `.cpp` | blur, range, colour ramp, the Land view's class colours; standard C++ |
| `land_class.h` | the Land view's rule for one tile; standard C++ and `Game/PathType.h` |
| `overlay_view.cpp`, `overlay_views.cpp`, `overlay_views_internal.h` | the selection, the image build, the instant views |
| `map_window.h` / `.cpp`, `src/ext/ui/Dialog/MapOverlays.rsc` | the map window's view buttons and controls (injected beside retail's map content) |
| `overlay_render.h` / `.cpp`, `overlay_render_math.h` | the minimap image (rows flipped to retail's order) and the terrain picture |
| `src/ext/ground_draw.h` / `.cpp` | arms `Graphics::Renderer::_extGroundDraw` for the heat map and `ext.show_grid` |
| `src/ext/minimap_outline_clip/clip_rule.h` | `fix.minimap_outline_clip` (retail bug 31) |

## 7. Verification

The plan, and what each item showed on the merged tree (2026-11-06):

| check | result |
|---|---|
| `tools/build.sh unit` (core, codec round trip and every malformed case, math, render math, the minimap row flip, the outline clip) | 2229 tests, 0 failures |
| Faithful gate (`tools/build.sh run`) | town reached, no crash, no map_overlays line in either log |
| `tools/resave_gate/resave_gate.sh Gate` (row off: zero bytes) | PASS, resave == golden |
| Row on, stress town: save at frame 5500, reload | encode and decode summaries identical (289 sites, 9 resources, 352,992 steps) |
| Row on save loaded with the row off | the extension notice, the row locked on from the save's manifest, plays on |
| Stress run, every extension off vs `ext.map_overlays` alone (no view open) | sim 0.263 vs 0.263 ms per iteration (row means); draw CPU 1.09 vs 1.10 ms. fps not comparable: an fps limiter was running |
| By hand (the user), DX11, a real save (Elysium) with better_camera / eye_camera on | every view, the pickers, the sliders, terrain on / off, rotate and zoom: no defect left |

**The stress town cannot show production**: its gatherers, field, orchard and dock produce next to
nothing and its food is added to the barns by the harness (`src/diag/stress_harness.cpp`), which is no
work and correctly counts as nothing produced. Use a real town for Production.

**Found while testing, outside the extension:**
- `RawMaterialFlags` bits 7+ were toolkit-order guesses; the installed templates give Wood 0x400, Stone
  0x800, Iron 0x1000, Tool 0x200, Health 0x2000, Clothing 0x4000, Textile 0x10000, Alcohol 0x20000
  (`RawMaterialDescription.h`).
- Retail bug 31 (the minimap's camera outline loses a side when a corner is off the map): `fix.minimap_outline_clip`,
  which clips each side to the sprite cull's own rect (the scissor goes through the UI scale and is truncated).
- Retail bug 32 (a ground decal's underwater tint is applied after the fog): written up, fixed in a separate change.
- `ext.show_grid` drew over the status icons for the same reason as this extension did; both now draw in the
  ground pass (`Graphics::Renderer::_extGroundDraw`).

No game content: the window is our own `.rsc`; retail's map content is used by reference (moved into the
side-by-side row at run time, never copied).
