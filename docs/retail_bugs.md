# Retail bugs

Behaviours that are faithful to the shipped game and therefore left alone by the port.
The port's job is to match retail; these get revisited only after the finalized build
(DLL-absent, all bodies ours) is the baseline. Until then a "fix" here would be a
deviation that the cutover gate and the A/B runs would have to explain.

Each entry: what a player sees, why retail does it (with addresses), how it was proven
to be retail and not the port, and the faithful lever if we ever change it.

## 1. Status icons slide in from the map origin for one sim tick

**Symptom.** A building status icon ("work stopped", "no workers", ...) appears at the
world origin (the map centre) and slides to its building. Reported 2026-09-13 as
"icons spawn at the town center then move to the field".

**Retail mechanism.**
- `StatusIconComponent` slot 11 (`0x14012bf40`) calls `Scene::AddObject` FIRST, then
  slot +0x40 (`StatusIconComponent_OnRemove`, `0x14012bbd0`) which positions the
  embedded `Graphics::Geometry` through `Geometry::SetTransform` (`0x1800155a0`).
- `SetTransform` rolls the old transform (identity, from the ctor `0x1800a4270`) into
  `_prevTransform` and takes the new one.
- `Geometry::Clean` (`0x1800a4420`) is two-phase: the tick's `Scene::Clean` only turns
  dirty bit 0 into "rolled-pending" bit 3; the NEXT tick's Clean copies current into
  previous. So for one full tick previous = origin, current = building.
- The icon's Draw (`0x1400a9c10`) renders
  `InterpolatedState<Transform>::Lerp(alpha)` with the frame's interpolation alpha, so
  every frame drawn inside that tick lands somewhere on the origin-to-building line.

**Proof it is retail, not the port.** Draw trace on 2026-09-13, DLL present vs absent:
identical pattern in both (prev=(0,0,0), alpha sweeping 0..1, flags=0x18, then rolled),
60 sim updates/s in both. Every body in the chain was read against the Ghidra decompile
(SetTransform, Clean, ctor, slot 11, OnRemove, Draw) and the ports match.

**Why it is visible in our build and barely in retail.** In-game rendering is uncapped in
the standalone (the registry has vsync=0; `src/app/frame_limiter.cpp` only runs in
the menu loop), so ~800-1000 draws/s spread ~15 frames across the one 1/60 s tick. With
vsync at 60 Hz it is a single frame at one intermediate position.

**Faithful lever, if it ever needs to go away.** The frame rate (vsync / an in-game
frame cap), not Clean or SetTransform. Alternatively position before AddObject in the
component, which is an exe-side deviation and must be documented as such.

Fix: `fix.icon_slide` (tier 1), default on. Positions the icon before `Scene::AddObject` in
slot 11 (`0x14012bf40`) and in `SetFlag` (`0x14012c3f0`, the same AddObject-then-Tick shape,
the path a running game's icons take); `src/game/Component/statusiconcomponent.cpp`.

## 2. The residence panel's "go to" button reads past a StorageComponent

**Symptom.** None reliably visible. Clicking a resident's "go to" button on a house panel
walks heap memory past the end of an object; on a lucky heap it centres on a resident,
otherwise it centres on nothing, on the wrong citizen, or faults.

**Retail mechanism.** `ResidenceUI::GoToCitizen` (`0x1400b56a0`, reached from slot 9
`0x1400b5843` on element event 0x12, id 300 + n) finds the house's component through
`0x140006270`, the StorageComponent finder: it compares `Type()` with the cell
`0x1401c7e78` and narrows with IsDerivedFrom. That cell's only writer is
`StorageComponent_RegisterType` (`0x14014fb10`, `LEA RCX,[0x1401c7e78]` at `0x14014fb35`,
name "StorageComponent", factory `0x140038b50` = `MemAlloc(0x40, 8)`). The body then reads
`[comp+0x58]` as a family count and `[comp+0x50]` as a 0x10-stride family array: both lie
past the 0x40-byte object. The rows it means to walk are ResidenceComponent's (cell
`0x1401c7cf0`, factory `MemAlloc(0x60, 8)`, families at +0x50 / +0x58), which
`OnUpdateLayout` (`0x1400b51f0`) walks correctly through the Residence finder.

**Proof it is retail.** Read in Ghidra 2026-10-09 (B6 Block 0, then block UI): the finder's
cell, its RegisterType and the factory size above.

**The port deviates (B6 ruling, known_port_issues row 452).** `src/game/Dialog/residenceui.cpp`
reads the ResidenceComponent's families by name, with a `STANDALONE DEVIATION` comment.
The faithful lever is the StorageComponent finder at that site.

Fix: `fix.residence_goto` (tier 1), default on. The deviation became the fix: the corrected arm
reads the ResidenceComponent, the faithful arm (`fix.residence_goto=0`) is retail's
StorageComponent finder and the raw +0x50 / +0x58 walk past it.

## 3. A tavern visit can count the wrong good as consumed

**Symptom.** None on screen directly. When a citizen's leisure visit spends the last unit
of a good at a building that needs goods (the tavern's ale), the town's "consumed this
year" counter goes up for the category of the NEXT good stored there, not for the good
that was spent. The stock counts themselves are right. Only the consumption statistics
(town stock row +0x0C) drift.

**Retail mechanism.** `HappinessComponent::OnMessage` (`0x14004a780`, message 0 with the
description's `_needsGoods` +0x25 set) walks the building's StorageComponent items
(`0x140006270`), and for the first item whose RawMaterialDescription flags (+0x5c) meet
the description's +0x28 mask with at least one unreserved unit:
- keeps `R14` = the address of that item's row in the storage array (`0x14004a824` /
  `0x14004a82a`, row stride 0x10, the material pointer at +0x00);
- calls `StorageComponent::SubtractItem(storage, material, 1)` (`0x14012d150`,
  `0x14004a8da`), which compacts the array when the row reaches zero;
- then re-reads the material through the same row address (`MOV RDX,[R14]` at
  `0x14004a8e6`) for the category lookup (`0x140088f90`) and bumps that category's
  counter (`INC [row + 0x0C]`, `0x14004a907`).

When the spent unit was the row's last, the row now holds the next item (or the stale
tail of the array), so the counter that moves is that item's category.

**Proof it is retail.** Read in Ghidra on 2026-10-11 by block CAI of the D.4 PORT GAP hunt
(decompile and disassembly), and re-read by block RESIDUE: the material is loaded from
`[R14]` after the call, not from a register saved before it.

**The port keeps it.** `HappinessComponent::OnMessage` (`src/game/Component/happinesscomponent.cpp`, D.4 CAI)
re-reads the row after `SubtractItem`, as retail does. The faithful lever is to load the
material before the call.

Fix: `fix.tavern_counter` (tier 1), default on. The corrected arm loads the material before
`SubtractItem` (`src/ext/fixes/tavern_counter.h`, unit test `tests/unit/fix_tavern_counter_test.cpp`);
the faithful arm re-reads the row after it.

## 4. The starter storage barn floats above the ground

**Symptom.** On a new game the storage barn the town starts with stands slightly above
the terrain: a visible gap under its walls, up to about 0.75 world units. Player-built
barns sit on the ground. Reported 2026-10-11 (block BARNZ, `known_port_issues.md` row 1323).

**Retail mechanism.**
- The start-town structure spawn `0x1400a1fc0` samples the terrain at the footprint's origin
  vertex and far vertex (`0x14009f3e0`, at `0x1400a21e4` / `0x1400a2243`), averages all three
  components including the height (`0x1400a2248`-`0x1400a2287`, `DAT_140172114` = 0.5) and
  passes that point as the model offset (`LEA R9,[RBP-0x8]` at `0x1400a24d4`) before
  `RegisterEntity`.
- `RegisterEntity` runs inside the instant-build bracket (world `+0xd0` bit 0). The state
  machine enters state 1 at once, and both the ClearArea state-1 enter (`0x140117820`) and
  the Build state-1 enter (`0x140108290`) call `0x140118270`. With
  `ClearAreaDescription::_flattenGround` (+0x2c, default true; `StorageBarn.rsc` declares
  `cleararea { }`) it calls `0x14009e0e0`, which writes height 0 into every heightmap
  vertex of the footprint, including the two that were sampled.
- Nothing moves the model afterwards: ModelComponent has no event handler (vtable
  `0x140163ba0` slot 12 is the Component default `0x140118ac0`), so `0x140118270`'s
  event 5 does not re-ground it.
- The player path does not have the defect: `PlacementTool::GetPlacement` (`0x1400fda00`)
  averages the same corners and then stores `param_6[2] = 0.0`, the flattened level.

**Proof it is retail.** Every step above was read in Ghidra and matches the port
(`src/game/Game/starttown.cpp` `placeStructure`, `ClearAreaComponent::FinishClear`,
`Map::ResetTerrainHeights`), and the order is the same: sample, transform, register,
flatten. On the Gate.sav image the barn's z is 0.021 over ground at 0.000, which is the
sampled average.

**Lever, if ever changed.** Zero the centre's z in `placeStructure` before
`FromYawPitchOffset`, as GetPlacement does. That changes the starting town's model z, so the
new-game world comparison would differ.

Fix: `fix.start_barn_height` (tier 1), default on.

## 5. Open dialogs flash to a corner when the UI scale is applied

**Symptom.** Click Apply or Ok in Options after moving the "User interface scale" slider:
for one frame every open dialog (the Options dialog itself included) draws displaced -- toward
the upper left when the UI gets smaller (75 -> 150), toward the lower right when it gets
bigger (150 -> 75) -- and then sits centred again. Reported by the user 2026-10-12 (screenshot,
main menu and in game alike, with every extension off); `known_port_issues.md` row 1325.

**Retail mechanism.**
- `UserInterface::Update` (`0x1801213c0`) reads `_uiScale` ONCE, before the events run: the
  unit-space screen `w, h = backBuffer * _uiScale` that sets `_clipRegion` (the block ending
  `0x180121525`) is the same pair the layout loop uses after `UpdateEvents` (`0x1801215de`) and
  `CleanRemovedDialogs` (`0x1801215e9`): `DialogInstance::ComputeLayout(0, 0, w, h)` for every dialog.
- The Apply / Ok click is a queued element event, dispatched inside that `UpdateEvents`:
  `OptionsDialog::OnApply` -> `ApplyGameSettings` (`0x140072420`) writes
  `_uiScale = uiScale / 100` and calls the game loop's `Update(0)`, which runs a NESTED
  `UserInterface::Update` -- that one lays the dialogs out at the new scale (its own events are
  skipped by the `_dragDropActive` re-entry guard in `UpdateEvents`).
- Back in the outer Update the layout loop runs with the OLD `w, h`: a centred root
  (`ComputeLayout`, alignment 4) lands at `(oldW - width) / 2`, the centre of the old
  unit-space screen. `UserInterface::Draw` (`0x180121890`) then maps units to pixels with the
  NEW `_uiScale`, so the dialog is drawn off centre for that frame. The next Update recomputes
  `w, h` and it snaps back.
- At the main menu the nested Update does not run (no game loop) and the result is the same:
  the outer layout at the old size.

**Proof it is retail.** The port's `userinterface_frame.cpp` is the address-by-address port of
`0x1801213c0` (the clip-region block precedes the events, the layout loop follows them); the
Apply path is the port of `0x140072420`. Measured 2026-10-12 with a frame-timing trace in
`ext.sharp_ui`: the change frame and the three after it are all 2-12 ms, the font-variant load
is 1-20 ms, and the flash shows with the extension off. A single displaced frame is visible to
the eye; retail at an uncapped frame rate shows it for the same one frame.

**Lever.** After `UpdateEvents` in `UserInterface::Update`, if `_uiScale` differs from the value
read at the top, lay out with `backBuffer * _uiScale` instead. Nothing else in the frame depends
on the stale pair: `_clipRegion` is reset from `_uiScale` by Draw, and the mouse position mapped
with the old scale is one frame of input in the old unit space.

Fix: `fix.ui_scale_layout` (tier 1), default on, engine seam `UI::UserInterface::_extFixScaleLayout`
(a null-by-default pointer the game side points at the fix's bool at boot: DESIGN.md hook rule 5).

**The modals ignore the UI scale (same row, 2026-10-25, seen on the S25).** `UserInterface::ShowDialog`
(`0x18011fd10`) builds a NESTED UserInterface for the modal and copies only `+0x90..+0xc8` (the audio scene
and the seven UI sounds) from the one it is shown from; the nested ctor leaves `_uiScale = 1.0f` (+0xdc).
So every modal shown that way (the mods' "changes require a restart" notice `Dialog/Reload.rsc`, the
Extensions restart notice, the save / load warnings, the Options confirmations) is laid out and drawn at
100 % whatever the Options UI scale: invisible at retail's default 100, too small at a phone's large UI.
Lever: copy `_uiScale` into the nested UI in the same step. Under the same row (both are "a dialog drawn
at the wrong UI scale"): `userinterface_showdialog.cpp`, the same seam pointer.

## 6. The employment allocator rates a never-measured commute as free

**Symptom.** None a player can point at. Once every 300 simulated seconds the employment
allocator (AssignCitizens, `0x1400d43b0`) can seat a citizen at a workplace that is far from
their home, because the commute it compared was reported as 0.

**Retail mechanism.**
- AssignCitizens asks `0x1400d6060` (the port's `PathGraph::RegionPairCost`) for every idle
  citizen x workplace commute. The answer comes from the PathGraph's region-pair tree
  (`PathGraph+0x18`), which ProcessPaths (`0x1400d6f00`) filled over the preceding ticks.
- When the region pair is not in the tree, `0x1400d6060` computes it (`0x140008830`) and stores
  it (`0x140025a10`), but the cost it returns is R15D. R15D is zeroed at `0x1400d60d2` and
  written only on the found arm (`0x1400d6213`), so the first query of that pair reports 0.
  Every later query of the pair in the same pass reads the stored cost.
- A cost of 0 makes that citizen the nearest candidate for the workplace, and the pair raises
  the workplace's priority score by the same zero.

**When it happens.** BuildPathGraph (`0x1400d70c0`) lists every residence x workplace pair, so
a miss needs a pair the rebuild did not list: a home or a workplace built, or a region
re-cut, while the pairs drained. The `[Stress::pathgraph]` line of the stress harness counts
the misses per drain (docs/extensions/handins/HITCH_PATHGRAPH.md).

**Proof it is retail.** The disassembly of `0x1400d6060` (objdump of Application-x64.exe,
2026-10-18): the not-found arm runs from `0x1400d6143` to the insert call at `0x1400d61be` and
falls into the common tail at `0x1400d61c3`, which stores R15D into the record's cost field.
No instruction on that arm writes R15.

**Lever.** Return the computed cost on the not-found arm, that is, the value just stored.

**Fix: `fix.commute_cost` (WIRED 2026-11-01).** The not-found arm of `PathGraph::RegionPairCost` returns the
cost it just stored, read the way the found arm reads a stored cost (a negative cost is unreachable,
`0x1000000`), so a pair's first query answers what every later query of it answers. Pure rule
`src/ext/commute_cost/commute_cost_rule.h`, unit test `tests/unit/ext_commute_cost_test.cpp`. Default on,
off under `/profile:faithful`. Sim-visible only after a miss (see "When it happens"); nothing new is saved.

## 7. Rocks on a slope float on their downhill side

**Symptom.** A boulder or an iron deposit on sloped ground shows a lit band of ground between
its bottom edge and its own shadow; from a low camera the downhill edge stands visibly off the
ground. Reported 2026-10-20 (`rocknotonground.PNG`), confirmed in retail (`rockmshadow.PNG`).

**Retail mechanism.**
- `NaturalResourceComponent::PlaceAtCell` (`0x140126540`) takes the terrain height at the CELL
  CENTRE (`0x14009f3e0`), then adds the x / y jitter (`RandFloatTol() * desc+0x60`, the
  template's `_positionTolerance`, 0.4 for rocks and 0.3 for ore) without sampling again, and
  builds the transform with pitch 0 (`FromYawPitchOffset`). The worldgen copy in the start town
  pass does the same.
- The rock meshes are open shells whose bottom ring sits at the model origin, so on a slope the
  downhill half of the ring stands above the ground by about the slope times the half width.
  The sun pass draws the casters with front faces culled and no bias, so the ground under the
  raised rim is lit (docs/extensions/handins/SHADOWGAP.md section 3, SHADOWCONTACT.md section 3).

**Proof it is retail.** The user's retail capture shows the same band; the placement was read in
Ghidra and matches the port; a shadow bias cannot close it (SHADOWCONTACT.md section 2).

**Lever.** The mesh's draw transform, not the placement: `ModelComponent::transform` is
serialized and read by nothing that draws, so the seated transform goes only into the model's
`Graphics::MeshInstance`.

Fix: `fix.rock_seat` (tier 1), default on, render only; `ext.rock_seat_mode` picks sink (1) or
tilt (2). `src/ext/rock_seat/`, docs/extensions/handins/ROCKSEAT.md.

## 8. "Red squares": cells that turn unbuildable and stay that way

**Symptom.** Reported by a player 2026-10-20 (with a screenshot: one isolated red cell, nothing
on it, inside an otherwise green placement rectangle). A cell that was buildable turns red in
the placement overlay for no visible reason. Three shapes:
- a house demolished and re-placed one cell over: one cell is red;
- a mountain road built in one drag succeeds, the same road built tile by tile hits red cells;
- a bridge deleted and then rebuilt over the same span is refused.

**Status: the flat-ground case is reproduced from player saves and fixed (`fix.terrain_resort`,
below); the bridge case is read in Ghidra; the house's count candidates are unconfirmed.**
`tools/cell_dump/` (README alongside) dumps a save's cells, with what the classifier says of each
today, and diffs two dumps; `BANISHED_CELL_TRACE=1` logs the count wraps and re-sorts named below
while a game runs.

**Reproduced (2026-10-22): flat ground stays "too steep".** A player sent two before / after save
pairs on two maps, each with the quarry overlay over a narrow strip between a stream and a
mountain: the green band of the early save has shrunk in the late one. The cell dumps show two
causes, one retail by design and one a desync:
1. *Trees hid steep ground.* A cell holding a tree is type 4, which the quarry's mask and the
   road mask (`0xa17`) accept. When the tree goes, `0x14009dee0` re-sorts the cell from its
   heights. On one map 474 of the 533 cleared-tree cells that turned red were already steep in
   the early save at unchanged heights. World generation (`0x1400a12b0`) scatters trees and ore
   in their templates' windows, height 0.00..1.00 and normal z >= 0.75, while the classifier
   calls buildable only height <= 0.75 and normal z >= 0.9 (rocks: 0.00..0.25 and >= 0.99, always
   buildable). A fresh map (seed 12345) has 950 of 20,297 trees and 89 of 2,274 ore on ground the
   classifier calls steep. So a road placeable over such a tree is not placeable once it is cut.
2. *The type grid drifts from the heights.* `ResetTerrainHeights` (`0x14009e0e0`: every finished
   road cell, a building's clear area) zeroes height words and re-sorts no cell. The cells
   around it keep the type they were sorted with: a cell sorted 10 while steep stays red after
   the ground under it is flat (cell (163,169) on the first map: height 0.000, normal z 1.000,
   type 10). The reverse also happens: a removed road is set to type 2 without a classify, and
   a cell beside a flattened road can be steeper than its stored 2.

A fresh map holds no such cell (classifier and stored type agree everywhere). The four player
saves hold 227 / 294 / 222 / 445 cells stored 10 that the classifier calls 2, free of any
entity, count or road.

Fix: `fix.terrain_resort` (tier 1), default on, two levers. (a) After every
`ResetTerrainHeights`, over its rectangle grown by one cell, and once over the whole map on a
load, a free cell stored 10 that the classifier now calls 2 is set to 2; a stored 2 is never
demoted. (b) When a lone occupant (a tree, ore, a goods pile: type 4, no other entity's zone
on the cell) leaves and the re-sort says 10, the cell becomes 2 instead (ruled 2026-10-22: what
was placeable over the tree stays placeable; lever in `StartTown_UnmarkResourceCellGrid`, the live
body of the removal re-sort). Goods piles are in because a hunter's dropped leather or venison,
picked up from a cell stored 2 that the classifier calls steep, turns that cell red the same way
(seen in a fix-on run of a player's save). Lever (b) cannot reach cells already cleared in an
existing save: nothing records that a stored 10 held a tree. `src/ext/terrain_resort/`,
docs/extensions/handins/TERRAINRESORT.md. With the fix on, the four saves load with 0 such
cells left.

**What red means.** `Map::TestPlacement` (`0x14009f5d0`, `Player/tool_base_path.cpp`) fails a
cell on any of: (a) its terrain type (MapCell `+0x29`) is outside the tool's mask; (b) a
blocker entity (`+0x00`); (c) `footprintCount` (`+0x28`) non-zero while the tested cell is not
a '.' cell of the new footprint; (d) height. `footprintCount` is one `uint8_t` refcount shared
by roads (+1 in `Map::PlaceRoadCell` `0x14009c960`, -1 in `Map::FinishRoadCell` `0x14009c640`
type 2 and `Map::RemovePendingRoad` `0x14009c700`) and the '.' cells of building footprints
(±1 in the stamp `0x14009d360`, from `ZoneComponent::OnAdded` / `OnRemove`). The loader's
fix-up pass in `Map::Serialize` (`0x14009f100`) turns a count of 0xFF back into 0, so retail's
author met the count wrapping below 0.

**The house: its own lifecycle is symmetric.** Placement, clearing, construction, demolition,
fire / tornado and save / load write and undo the same cells with the same rectangle and
rotation: blocker and '.' count through `0x14009d360` on add and remove, occupant and terrain
through `0x14009dc20` / `0x14009dee0` gated on component flag `0x100000` (saved), and a load
does not re-run `OnAdded`. The single red cell most likely comes from a road sharing the count
on the house's door cell. Candidates, both unconfirmed:
1. Removal of one road cell completes twice (the removal tool dragged over a road twice while a
   worker is already on its first marker): `FinishRoadCell` type 2 subtracts 1 unconditionally,
   so a door cell holding a road (count 2) reaches 0, and the house's removal wraps it to 0xFF.
   Red for the rest of the session; a reload clears it (the 0xFF fix-up).
2. A pending road cancelled while a citizen already carries its `CompleteRoad`, then a save /
   load: the fix-up pass sets the count to 1 on a road cell, and `RemovePendingRoad`'s
   `+0x2A == 0xFF` arm never subtracts. Survives a reload.

**Candidates 1 and 2 checked in Ghidra (2026-11-01): neither completes twice.** Neither was run.
- *Candidate 1.* The second drag does place a second marker on the still-finished road
  (`RemovePendingRoad` returns 1). In the same pass it marks the first marker for removal
  (`RemovalTool::CalcPath` `0x1401035e0`: the cell's entity has the marker template `+0x38`).
  `MarkForRemoval` (`0x1400c2ea0`) runs `WorkComponent`'s remove hook (`0x14012e6c0`), which cancels the
  marker's ClearArea work. `WorkScheduler::TaskCleanup` (`0x1400f8e30`) then calls the work's slot 8,
  `ClearArea::Cleanup` (`0x1400e6650`). That clears the action stack of every citizen in its cell set
  (`+0x58`), which holds the citizen phase 2 dispatched `CompleteRoad` to. The first `CompleteRoad` is
  dropped before it runs, and the second marker's own phase 2 dispatches a single one. The marker's phase 2
  only offers road cells: `Map::GetTilesInArea` (`0x14009fc00`, empty-tile arm at `0x14009fdce`) emits an empty tile only
  when it holds a road, is road terrain or is pending.
- *Candidate 2.* `RemovePendingRoad` (`0x14009c700`) hands the pending road's work to `TaskCleanup` the
  same way. `BuildRoad::Cleanup` (`0x1400e4910`) clears the stack of the citizen it dispatched (`+0x48`).
- The saves checked hold no stuck count. These are the dumps of the four player saves and of `RetailA`
  (4300 road cells). None has a cell with a count, no road and no building within 2 cells (`cell_diff.py
  --suspects`), and none has a road-terrain cell (type 0 / 1) whose `+0x2A` is 0xFF.

**A third sequence, read only, not reproduced.** A road upgrade drawn over a cell that a removal marker
still holds:
1. The upgrade queues a `BuildRoad` and a pending node. `PlaceRoadCell` (`0x14009c960`) sets `+0x2A` to
   the new type and leaves the count alone, because the cell already has a road.
2. The marker's citizen finishes first. `BuildRoad::Execute` waits while a non-pile, non-resource entity
   occupies the cell. `FinishRoadCell` type 2 sets the count to 0 and `+0x2A` to 0xFF, sets type 2, and
   erases the pending node. It does not touch the queued `BuildRoad`.
3. That builder then finishes. `FinishRoadCell` with a road type writes the terrain type and `+0x2B` but
   not `+0x2A`.

The result is road terrain with `+0x2A` 0xFF and count 0. From there:
- Drawing a road over it adds no count (`PlaceRoadCell` adds only on non-road terrain), so its later
  removal wraps the count to 0xFF.
- A save and load before any change sets the count to 1 (the fix-up pass counts road terrain). The Remove
  tool then takes `RemovePendingRoad`'s 0xFF arm, which sets type 2 and leaves the count at 1. The result
  is a red cell that the Remove tool does not select, and that re-laying and removing a road leaves at 1.

This needs the road tool to accept a cell holding a removal marker. `RoadTool::CalcPath` refuses a
blocker only at the endpoints and in its A*, and whether the marker template stamps a blocker is in the
game data, not checked. No fix row until a run shows it. The lever would be in `FinishRoadCell` type 2:
erase the pending node and cancel its `BuildRoad` as `RemovePendingRoad` does, or skip the subtraction
when the cell holds no road.

**The tile-by-tile road: the removal re-sort.** `FinishRoadCell` resets the height grid at the
finished cell's four corners (`Map::ResetTerrainHeights` `0x14009e0e0`). When an entity later
leaves a neighbouring cell (a tree, rock or pile cleared, a building demolished), `0x14009dee0`
re-sorts that cell with `ClassifyTerrain` (`0x14009e2c0`) from the changed heights; on a slope
it can come out as type 10 (too steep), outside the road mask `0xa17`, and nothing sorts it
again. In one long drag the neighbouring cells are already queued, and finishing them
overwrites the re-sorted type. Open: what the height grid `ResetTerrainHeights` writes really
is. The port's slope sample did not match retail's (a fixed forward-difference triangle); since
2026-10-30 `Map::GetTerrainPos` uses retail's ray cast (`WorldGeometry::HeightUnderPoint`,
`Game/worldgeometry_ray.cpp`), whose hit-triangle normal is the value tested at 0.9.

**The bridge: the far bank must not be road (Ghidra-confirmed).** Deleting a bridge undoes
everything it wrote (body cells re-sorted back to water, the zone stamp cleared, no road and no
heights written). `BridgeTool::ValidatePath` (`0x1400fb2d0`, `Player/bridgetool_validate.cpp`)
runs its clearance ring from the first body cell to `bodyCol + bodyCount` inclusive, so it also
tests the first bank cell past the water on the higher-index end (east, or south), and that
cell must hold no road (terrain 0 / 1, `+0x2A` set, or pending). The near bank is never
tested. A bridge's connecting road on the far bank therefore blocks the rebuild, and the
preview paints the whole span red without naming the cell. Removing that road tile makes the
span valid. Looks like an off-by-one, not a rule.

**Levers, if these are ever changed** (each a `fix.*` row, `docs/extensions/DESIGN.md` §3):
end the bridge ring at the last body cell; clamp the count at 0 in `FinishRoadCell`; exclude
road-adjacent cells from the removal re-sort. (Re-sorting the cells around a flattened tile is
`fix.terrain_resort`, above.)
## 9. A mod package larger than 2 GiB crashes the content load

**Symptom.** A single-file mod package past 2 GiB (the reporter's MegaMod "All In One" build,
`MegaMod.pkm`, 10,757,625,300 bytes; GitHub fran-cap/OpenBA#13) mounts, and about 2 s into the
content load the game dies with an access violation in `MaterialInstance::BuildParameters`
(retail Runtime `0x180103933`). The same content split into seven packages under 2 GiB loads.

**Retail mechanism.**
- The package index stores each entry's offset as a uint32 (`ReadPackfile` `0x1800b5de3`), and
  `OpenFile` copies it into the FileInfo's int32 `baseOffset` (`0x1800aca0c`).
- `FileSystem::ReadFile` (`0x1800ace00`) reads a packed entry at the int32
  `baseOffset + cursor` (`0x1800ace43`) with `SetFilePointer(h, abs, NULL, FILE_BEGIN)`
  (`0x1800ace61`): no high dword. An entry in [2, 4) GiB seeks negative and the read fails; an
  entry past 4 GiB has a stored offset already wrapped mod 2^32 and reads bytes from the wrong
  place. `GetFileSize` is called with no high dword too.
- The packer lays the data out contiguously in index order, so the stored offsets decrease
  exactly where the true offset crosses 4 and 8 GiB (twice in this file).

**Proof it is retail.** Retail and OpenBA crash identically on the file (issue #13 scoping);
the read path above is byte-faithful in the port.

**Lever.** The packed read's absolute offset. `FileInfo` and `PackageFile` layouts are pinned,
so the 64-bit offsets live in a side table built when the packages are mounted.

Fix: `fix.large_packages` (tier 1), default on: a mounted package larger than 2 GiB gets 64-bit
entry offsets, unwrapped from its index and validated, and its packed reads seek 64-bit; packages
up to 2 GiB are read exactly as retail. `src/ext/large_packages/`,
docs/extensions/handins/LARGEPKM.md.

## 10. A graphics device lost before the main menu crashes the device-lost recovery

**Symptom.** A GPU driver reset (TDR) or any device removal during the boot's loader and splash
frames, before the main menu has finished starting, ends the game with an access violation
(READ of 0x58) inside `Engine::MainLoop::OnVideoError` instead of the device-lost round trip
that later losses get. Reproduced by injection (`BANISHED_FORCE_DEVICE_LOST=10`,
`docs/known_port_issues.md` row 1332) and by real driver resets on a first-run boot with a second
fullscreen game taking the foreground (row 1334).

**Retail mechanism.**
- `MainLoop::OnVideoError` (Runtime `0x1800dc5e0`) drops every save thumbnail by walking
  `Save::SaveLoad::_singleton->_saves` with no null test (`0x1800dcc30`: the count at `+0x58`
  is read straight off the singleton); `MainLoop::OnVideoRestart` (`0x1800dc3b0`) calls
  `SaveLoad::OnVideoRestart` (`0x1800c0f10`) on the same singleton.
- The first `Save::SaveLoad` is constructed at the very end of `MenuDialogLoopT::Startup`
  (exe `0x14009b930`: `MemAlloc(0x90)` then `SaveLoad::SaveLoad`, after `SetCursor` and the
  menu fades). Until then the singleton is null, and the boot already presents frames (the
  loader loops and the splash), so a loss there reaches the walk with a null singleton.

**Proof it is retail.** Read in Ghidra (both bodies above); the port's walk is a transcription
and crashed at the same READ of 0x58 with the fix off.

**Lever.** The two walks: skip them while no SaveLoad exists (there are no save thumbnails to
drop or rebuild yet).

Fix: `fix.boot_device_lost` (tier 1), default on, two seams. `OnVideoError` step 5 and
`OnVideoRestart` step 3 are skipped while `Save::SaveLoad::_singleton` is null (seam
`Engine::MainLoop::_extFixBootDeviceLost`); with a SaveLoad both run as retail. And the DX11
backend's `ContextData::BeginCommands` runs the device-lost recovery when a swap-chain reset
fails on a removed device (retail recovers only from Present, which such a reset never reaches;
seam `BootDeviceLostBackend::SetFixFlag`, `src/render/common/boot_device_lost/`). Off under
`/profile:faithful`: retail's spin in the reset, or the crash above.

## 11. A crop field stays in the planting state when its last slots cannot be planted

**Symptom.** A crop field that is planting (state 9) never moves on to tending, harvest or the re-clear,
so it is never harvested and never planted again. Two triggers reach it:
- **A slot no farmer can reach** (a steep or fenced-off row, a blocked edge): the farmers plant the rest
  and the field stays in "planting"; the crops that did grow are never harvested and rot. Reported by
  players since 2019 (about five Discord reports, 2019-2026: "Since they can't finish planting, they never
  see that field as ready to tend for the summer, or ready to harvest"; removing the field and building a
  smaller one is the known workaround).
- **Every slot ends done while planting** (state 2 on every slot: every planted crop lost before planting
  finished). Not observed in play.

**Retail mechanism.**
- `CropFieldComponent` state 9 per-frame (`0x14011a1e0`) requests 10 (tend) when every slot is growing or
  done, and 0x15 (21, the re-clear) when every slot is done. A slot leaves "empty" (state 0) only when a
  crop is bound to it (OnMessage 0, `0x14011ab50`) or when `MarkPlantSlots` (`0x14011b390`) turns every
  empty slot done.
- `PlantArea::Execute` (`0x1400f4010`) walks the slots still to plant. For each it clears its "nothing
  left to plant" flag *before* the reach test (`Map::FindCell` `0x14009ccf0`: an access cell among the
  slot's eight neighbours; `Map::ValidateCell` `0x1400a00f0`: the citizen's zone), and skips a slot that
  fails it. When the walk assigns nothing it returns 1 on that flag (`0x1400f4281` `JZ` past the `MarkPlantSlots` call at `0x1400f4298`), so its
  end-of-planting branch (`MarkPlantSlots` when no plant is in flight) runs only once every slot has been
  handed out. An unreachable slot is never handed out: it stays empty, growing + done never reaches the
  slot count, and state 9 never requests 10. Frost does not end it either: dead crops turn their slots
  done (`ValidateEntityRefs`), the unreachable slot stays empty.
- For the all-done trigger, the crop-field populator (`0x14011b1a6`) registers state 9 with the
  transitions {3, 10} only. States 10 and 11 list 0x15; state 9 does not. The node's transition lookup
  misses, so the request is dropped.

**Proof it is retail.** The populator read in Ghidra (PR #33 review), and the per-frame body in the
Steam build's disassembly (`MOV dword [RSP+0x30],0x15` on the all-done branch). `PlantArea::Execute` read
in the Ghidra decompile of `0x1400f4010` (2026-11-01): the flag store precedes the set lookup and the reach
test, and the `return 1` on it precedes the `MarkPlantSlots` call. The port
(`CropFieldComponent::State9PerFrame`, `src/game/Component/cropfieldcomponent_states.cpp`;
`PlantArea::Execute`, `src/game/Work/plantarea.cpp`) is the same logic. Retail itself was not run on
either trigger; the reports above are retail players'.

**Lever.** For the all-done trigger, add 0x15 to state 9's transitions, so the all-done branch reaches the
re-clear (ClearArea state 0x15) as states 10 and 11 do. That alone does not reach the unreachable-slot
trigger (its slot is never done). For that one, let `PlantArea::Execute` run its end-of-planting branch
when every slot left to plant fails the reach test.

**Fix: `fix.crop_planting_exit` (WIRED 2026-11-01).** State 9's table carries 21 (appended after 3 and 10,
so their transition indices are retail's; the table is built before settings are read), and
`State9PerFrame` makes the 21 request only with the row on. In `PlantArea::Execute`, when the walk assigns
nothing because every slot left failed the reach test, no plant is in flight and at least one slot of the
field is growing or done (a farmer did reach the field, so the slots are not merely out of this citizen's
reach), the end-of-planting branch runs: the unreached slots are marked done and stay bare for the season,
and the field goes on to tending (`Ext::CropPlantingExit::EndPlantingOnUnreachable`,
`src/ext/crop_planting_exit/crop_planting_exit_rule.h`). The next planting rebuilds every slot and tries
them all again. Off under `/profile:faithful`.

## 12. A dragged slider snaps back when the pointer strays off it

**Symptom.** Drag the thumb of any slider (the Options dialog's "User interface scale", the
volume sliders, a terrain-edit brush slider) and let the pointer drift more than 32 pixels above,
below or past either end of the slider: the thumb jumps back to where the drag began and stays
there while the pointer is outside. Releasing the button out there discards the drag. Dragging
past an end to pin the value at its minimum or maximum therefore does not work unless the pointer
stays within 32 pixels of the track.

**Retail mechanism.**
- `SliderElement::Update` (`0x1800d67e0`) arms the drag on a press over the thumb:
  `_dragAnchor = _value` and flag bit 21 (`kFlagDragging`).
- It then tests a band 32 pixels (`0x20`) around the slider's layout rectangle. While dragging
  and outside the band it stores `_value = _dragAnchor`, calls `UpdateNumber` (`0x1800d6c20`) and
  returns -- before the release test and before the drag arm.
- Inside the band the drag arm sets `_value = _dragAnchor - dragDx / span * (max - min)`
  (`dragDx` = press x minus current x, `UserInterface::Update` `0x1801213c0`), and the tail clamps
  `_value` to `[_minimum, _maximum]`.
- Because the early return skips the release test, a release outside the band leaves bit 21 set;
  the next release inside the band clears it.

**Proof it is retail.** The port's `src/engine/ui/sliderelement.cpp` `SliderElement::Update` is
the address-by-address port of `0x1800d67e0`; the early return is retail's block. Reported by
the user 2026-10-25 on the UI scale slider.

**Lever.** In the band test, while dragging, treat the pointer as inside the band: the drag arm
follows `dragDx`, the existing tail clamp holds the value at its ends, and a release clears bit 21
and keeps the value (the release frame's `dragDx` is 0, but the drag arm re-reads the flag after
the release cleared it, so the value is not recomputed). The page-step arm (a press left or right
of the thumb) is unchanged.

`ScrollElement::Update` (`src/engine/ui/scrollelement.cpp`) has the same 32-pixel band: dragging a
scroll bar's thumb off it takes the else arm `_currentValue = _dragAnchor`, and a release off it clears
the drag flag and returns without the in-band release sound.

Fix: `fix.slider_drag_clamp` (tier 1), default on, engine seams `UI::SliderElement::_extFixDragClamp`
and `UI::ScrollElement::_extFixDragClamp` (null-by-default pointers the game side points at the fix's
bool at boot: DESIGN.md hook rule 5).
Hand-in `docs/extensions/handins/SLIDERCLAMP.md`.

## 13. A panel at the screen's edge jumps for one frame when it is moved

**Symptom.** A building's or citizen's info panel is placed every frame above its entity (its bottom on
the entity's screen top, `EditTool::Update`). When the panel's layout crosses the screen's bottom edge
(its first layout before its controller has filled it, or a panel that grows while it is held against
the edge), it is drawn a few layout units too high for one frame, then snaps back. Measured on the
Galaxy S25 at UI 42: 3 to 11 units (`docs/android/P11B_DIALOG_SNAP.md` cause 3). Rare at a desktop UI
scale, where a panel's first size is usually its final one.

**Retail mechanism.**
- `DialogInstance::ComputeLayout` (`0x180113d90`) lays the root out at the stored `_upperLeft` (+0x88 /
  +0x8c) and pulls a dialog that crosses the far edge back onto the screen. It does not write the
  position it used back into `_upperLeft`.
- `DialogInstance::SetUpperLeft` (`0x180114a60`) clamps the requested upper-left into the screen, then
  moves the laid-out tree by `new - _upperLeft` (`Element::AdjustUpperLeft` `0x180115840`) and stores
  the new value. When the last layout put the root elsewhere than `_upperLeft`, the move lands it off by
  that difference until the next layout.

**Proof it is retail.** The port's `src/engine/ui/dialog.cpp` `DialogInstance::SetUpperLeft` is the
address-by-address port of `0x180114a60` (the clamp order is load-bearing, kept); `ComputeLayout` is
faithful. Seen with ext.phone_menu on the S25 (P11b), the mechanism is the faithful pair's.

**Lever.** Before SetUpperLeft takes its delta, set `_upperLeft` to where the root is laid out (the
root's laid-out left / top less its pads), on the axes the root's alignment anchors top / left. The
move then lands the dialog where the next layout keeps it. A root not laid out yet is left alone.

Fix: `fix.dialog_position_drift` (tier 1), default on, off under `/profile:faithful`. No seam of its own:
it rides `UI::UserInterface::_extDialogPlace` at `kExtPlaceUpperLeft` (installed under every profile but
faithful by ext.phone_menu's `Place`, the row on or off). Code `src/ext/dialog_position_drift/`.
Hand-in `docs/extensions/handins/DIALOGPOSITIONDRIFT.md`.

## 14. Ties in the job and house allocators follow memory addresses

**Symptom.** None a player can point at in one game: the town is valid either way. Load the same save
twice, or play the same seed twice, and which of two equally near idle citizens takes an open seat,
which idle citizen Phase 5B strips first, and which of two equally good empty houses a family moves to
can differ between the runs. From that tick on the two towns diverge. It is a determinism defect, not a
rule defect.

**Retail mechanism.**
- `PathGraph::AssignCitizens` (`0x1400d43b0`) builds its scratch trees with raw pointers in the key: the
  idle set (local_148) by `CitizenComponent*`, the profession-count map (local_158) by `Profession*`
  (citizen +0x1f0), and each need's cost tree by (cost, `CitizenComponent*`). The best candidate is a
  tree's leftmost node, and Phase 5B takes both maps' leftmost.
- `PathGraph::ReassignWorkers` (`0x1400d62a0`) keys the empty-residence tree (local_b8) by
  `ResidenceComponent*` and the occupied tree (local_98) by (cost descending, `ResidenceComponent*`); the
  destination search takes the first empty house whose cost is strictly lower, so on a tie the lowest
  address wins.
- Every one of those objects is a heap allocation, so a tie goes to whichever the allocator placed lower.

**Proof it is retail.** The orders are read off the disassembly: the cost tree's (cost, then the
citizen pointer with an unsigned compare) at `0x1400d5b55..0x1400d5b97`, Phase 5B's two leftmost walks at
`0x1400d5dc0..0x1400d5dfc` (the profession map down +0x18, the idle set down +0x10). The port's std::
containers use the same order (`src/game/Component/citizen_assign_citizens.cpp`, `citizen_reassign_workers.cpp`). The need
priority queue is not affected: it keys on the need record's place in one array (`need_queue.h`).

**Lever.** Put a stable id in front of each pointer: a citizen's or residence's entity id
(`ComponentEntity::_entityId`, unique at any moment and saved), a profession's index in
`ProfessionList::professions`. The pointer then only orders two keys with one id.

Fix: `fix.alloc_tiebreak` (tier 1), default on, off under `/profile:faithful`. Two seams, one read of the
row per call: `PathGraph::AssignCitizens` and `PathGraph::ReassignWorkers`. Code `src/ext/alloc_tiebreak/`.
Hand-in `docs/extensions/handins/ALLOCTIEBREAK.md`.

## 15. The sprite capture utility crashes on its first save

**Symptom.** None in a normal game: the utility is a hidden toolbar entry (`Toolbar capture` in
`Game/Toolbar.rsc`, `_visible = false`, Action Utility, hot key Camera0) that opens
`Template/UtilityCapture.rsc` (SpriteCaptureUI over `Dialog/Capture.rsc`). Opened and given a name,
its Capture button starts the capture; one frame later the save faults reading address 0, and no
file is written.

**Retail mechanism.** The save goes through `External::SaveText`, whose `TextFileStream` (ctor
`0x1800ba340`) installs the encoding vtable `0x1801559d0` (`LEA` at `0x1800ba431`). Its +0x18 and +0x20
entries, `0x1800b9a80` (CreateReader) and `0x1800b9a70` (CreateWriter), are both `33 c0 c3`
(`XOR EAX,EAX; RET`): the shipped runtime has no text writer. `SpriteCapture::Serialize` (`0x1800d9530`)
builds its serializer through `[RAX+0x20]` (`0x1800d957f`), stores the null (`0x1800d9582`), and reads
its vtable with no test (`MOV RCX,[RBP-0x30]` / `MOV RAX,[RCX]`, `0x1800d958d` / `0x1800d9591`).

**Proof it is retail.** The bytes above, read with objdump from the GOG `Runtime-x64.dll`; the shipped DLL
exports no `IO::TextWriter`, while the toolkit's `ToolKit/bin/x64/Runtime-x64-profile.dll` exports
`??0TextWriter@IO@@QEAA@AEAVSerializer@1@H@Z`, so the tool only works in the toolkit build. The port
faults at the same read (`UI::SpriteCapture::Serialize`, `src/engine/ui/spritecapture_class.cpp`); the
reproduction is `BANISHED_TOOL_HARNESS=spritecapture` (`src/diag/tool_harness.cpp`). Retail itself was
not launched. No fix or extension: the utility is a content-author tool.

## 16. A selected entity's outline shows one frame before its panel

**Symptom.** Click a building, a field or a citizen: for one frame its yellow outline is drawn and its
info panel is not; the panel appears the next frame. Where the panel lands over the outlined area the
outline seems to flash over the panel. Invisible at a desktop's frame rate; plain at 60 fps on a phone
(measured on the Galaxy S25 by screen recording a tap on a stockpile: the outline alone for 16.7 ms).

**Retail mechanism.**
- `GameUpdateLoop::Update` runs `UserInterface::Update` (step 9, `0x1801213c0`), which takes the dialogs
  added since its last run (`_addDialogs`, appended by `AddDialog` `0x180120ee0`) into its list and lays
  them out, BEFORE the tool manager's Update (step 10, `0x1400965a0`-`0x1400965ed`).
- A click is handled in step 10: the select tool adds the entity's outline (`HighlightComponent`'s
  embedded `EdgeMeshInstance`) to the scene and opens its panel, which only reaches `_addDialogs`.
- The frame's draw gathers the scene with the outline (`EdgeMeshInstance::AddDrawItems` `0x1800a2fd0`)
  and draws the UI without the panel, which the next frame's step 9 takes in.

**Proof it is retail.** The port's step order in `src/game/Game/gameupdateloop.cpp` follows retail's
(steps 9 and 10 at the addresses above), and `UserInterface::Update` / `AddDialog` are address-by-address
ports. Seen with every phone extension's draw path unchanged; the order alone produces it.

**Lever.** On a frame whose tool update left a dialog waiting (`GetAddDialogCount() > 0` after step 10),
skip at the gather the outlines the previous frame did not draw. They are drawn from the next frame, with
the panel. Outlines already on screen and frames that opened no dialog are untouched.

Fix: `fix.outline_with_panel` (tier 1), default on, off under `/profile:faithful`. Engine seam
`Graphics::EdgeMeshInstance::_extHoldDraw` (null under the faithful profile). Code
`src/ext/outline_with_panel/`. Hand-in `docs/extensions/handins/OUTLINEWITHPANEL.md`.

## 17. A building with no absorb component, placed over resources, never finishes while the town's storage is full

**Symptom.** A building with no `AbsorbResourceComponent` (a town hall is the case measured) placed over
trees and rocks, typically over an area the Clear Tree / Clear Stone / Clear Iron tools are already
working, is never built. (The first report, 2026-10-31, was a stockpile; that half was a port bug, see
below.) The trees and rocks are felled, the logs and stone stay on the ground,
the site keeps showing "x of y to remove", and the laborers stand idle. Reported 2026-10-31. It is
reached quickly on an Easy start: the starting stockpile holds 5550 of its 6000 units (the faithful
seeding, `MapGenerator::SeedStartingResources` `0x1400a1f00`), so a town hall's footprint and one
10x10 clear area fill it.

**Retail mechanism.** A cycle with no exit:
- A site leaves clear state 0 only when its footprint holds no occupant at all
  (`ClearAreaComponent::IsDone` `0x140118440` -> `Map::IsAreaEmpty` `0x14009dac0`, the StorageYard and
  TownHall descriptions take that branch). A felled tree's pile is an occupant.
- A pile is hauled only by its `Pickup` work, whose Execute (`0x1400f2f00`) asks
  `CitizenBehavior::HasStorageFor` (`0x14008d3c0`, call at `0x1400f30f2`, `JZ 0x1400f2f3d` = no work)
  first: some registered storage location must take it. The site's own ClearArea offers the pile
  through the same work (`ClearArea::Execute` phase 3, `WorkScheduler::TryAssignWorkplace`
  `0x1400f8620`).
- A new stockpile joins the storage list only from `StorageLocationComponent` state 2's enter
  (`0x14012d230`, registered by `StorageLocationDescription_SetupCrossRefs` `0x14014fc74`), that is
  after it is built, which needs the empty footprint.
So with every existing store full, the piles cannot move and the footprint never empties. A building that
cannot take the piles itself, such as a town hall, stays stuck until some store has room. A stockpile placed
beside it is such a store once built, and its own site clears without help (below).

**A stockpile site is not caught in this cycle in retail.** Its `AbsorbResourceComponent` (`State0Tick`,
`0x140105950`) sweeps the footprint every frame while the site is clearing, and a pile its own
`StorageComponent` takes (`CanAdd` `0x14012cd80`: the flags admit it and the whole stack fits) is moved into
that storage and removed from the footprint; the town's storage list and its room are not involved. The
sweep runs because `ClearAreaComponent`'s per-frame handler (`0x1401178f0`) returns 0 and
`StateMachine::Tick` (`0x1400a9660`) then goes on to the next node of the chain. The port's handler returned
1, so the sweep never ran and a stockpile stalled like the hall (known-issue row 1343). A stockpile site
still waits for a pile its storage refuses (the flags do not admit it, or the stack does not fit).

**Proof it is retail.** Every link above is the port of the address named, read against the decompile
(Pickup::Execute's gate and drop-target walk, HasStorageFor, IsDone / IsAreaEmpty, the state-2
registration). The reproduction is `tools/harness_route.sh clearoverlap`
(`src/diag/clear_overlap_harness.cpp`): with a town hall over 46 trees and the three clear tools over a
10x10 window, then a StorageYard over that window, the census shows the start stockpile at 5990/6000,
`HasStorageFor` false for every pile, and 24 piles in the hall's footprint unchanged from frame 3100 to
the end of the run at 9400. (The run also counted 17 piles unchanged in the yard's footprint; that was
the port's ClearArea return value, row 1343, not retail. With that fixed, the same route empties the yard's
footprint: its piles are absorbed and the yard is built by frame 2500, and the hall's piles are gone from its
footprint by frame 3100.) The control (`CLEAR_OVERLAP=400,150,300,6000,6,0`: a 6x6 window, no hall, the
stockpile placed over four claimed trees while their clear jobs run) clears and builds in 450 frames,
so placing a stockpile over live clear jobs is not the cause. Retail itself was not launched.
The stall is timing-dependent: a run in which food, firewood or tool use frees store room while the
footprint piles are still being offered clears on its own. Retail also recovers after a game year, when the
piles rot (`RawMaterialComponent::Update` `0x140129130`, age past 3600 s).

**Fix: `fix.foundation_piles` (WIRED 2026-10-31, `docs/extensions/handins/FOUNDATIONPILES.md`).** At the
lever, `ClearArea::Execute` phase 3: when a building site's footprint holds only piles and
`HasStorageFor` refuses one, a citizen carries it to the nearest free cell outside the footprint with
retail's own haul chain and `DropOffRawMaterial`'s drop-on-ground arm (`0x1400c78c0`). Loose piles are not
in the town's totals or storage numbers (`CountResources` `0x140089ed0` sums the storage lists only), so
the move changes no count, and the new pile posts its own Pickup work. Off under `/profile:faithful`.
A stockpile site meets it only for piles its own storage refuses (see above); the buildings it is for are
those without an absorb component.
`tools/harness_route.sh clearoverlap`: `HARNESS_FIXES=none` FAILs (2 of 2 runs, 2026-10-31);
`HARNESS_FIXES=foundation_piles` PASSes (3 of 3; one run moved 18 piles off the town hall's footprint,
two cleared without a move because store room freed first). These runs predate the ClearArea return fix
(row 1343); the yard's part of each result is from before it.

## 18. A synchronous path query waits for every citizen search queued before it

**Symptom.** In a large town (about 1600 citizens, a 640-cell map, game speed 10) the game freezes for
15 to 20 seconds, sometimes longer, when a merchant boat sets off for a trading post. The same wait can hit
the road tool's cost query. Unattended runs end it as a hang (`standalone_hang.log`: the main thread in
`RunPathQuery` under `BoatComponent::SetDestination`, the pathfinder thread in `GridPathSearch::Run`).

**Retail mechanism.** There is one pathfinder worker (`PathfinderThread`) with one FIFO of commands
(tail `+0x60`, head `+0x68`, count `+0x70`). Citizens' walks queue their searches there (`Map::RequestPath`
-> `0x1400a6c10`, command 5) and poll for the answer later. The synchronous query `0x1400a6e30` (port:
`RunPathQuery`, `src/game/Action/keystone_a_zone.cpp`) queues its search the same way, at the tail, and then
spins on the main thread (`YieldToThreads`) until its result slot is ready. Its callers are the boat route
builder (`BoatComponent::SetDestination` `0x1400456e0` via `0x14009ffc0`) and the road tool (`0x14009ce00`).
So the main thread waits until every command queued before the query has run.

**Proof it is retail.** The order is the port's faithful transcription of `0x1400a6c10` (claim a slot,
then queue command 5 at the tail), `0x1400a6e30` (the spin) and the worker loop (pop the head, apply).
Measured on a copy of a player's save (six runs, 20 min each budget): when the merchant's query was
queued behind 1899 / 2509 commands (1063 / 1199 searches, citizen walks of up to ~50 ms each), it took
19.4 s / 17.6 s; behind 1 to 19 commands it took 0 to 15 ms. Retail itself was not run on that town.

**Lever.** Queue the synchronous query at the HEAD, with the search settings the caller queued just before
it (the main thread's staged passable mask, step costs and search flags), and put the worker's own settings
back after it, so every command behind it runs as before. The query then waits for at most the search the
worker is in.

Fix: `fix.path_query_first` (tier 1), default on, off under `/profile:faithful`. One seam reads the row,
`RunPathQuery`; the worker runs the query as command 6 (`PathfinderThread::ApplyCommand`). Code
`src/ext/path_query_first/`. Hand-in `docs/extensions/handins/PATHQUERYFIRST.md`.

## 19. A citizen who reaches another land zone over water or mountain cannot walk back, and starves

**Symptom.** Player reports since 2015 (Steam "Wandering Idles", "Citizens Getting Stuck"; Discord
2019-2025, collected in the 2026-11-01 community sweep): a citizen stands still until he starves or
freezes, often showing "going somewhere to get warm"; idle citizens end up on a spot across a stream
or at the map edge and never come back; builders walk out to a dock, bridge or planking piece over
water ("under water"), and are stuck on it once it is built if it is not joined to land. A bridge or a
land fill back to the spot frees them; so, sometimes, does changing their profession. "Citizens can
walk on water precisely once" (a player, 2020).

**Status: the mechanism is read in Ghidra and matches the port function by function. Fixed by
`fix.stranded_wade` (the ruled rescue, below).**

**Retail mechanism.** Reachability is a zone test. `Map::FillZone` (`0x14009c170`) floods a zone id
(MapCell `+0x26`) over connected cells whose terrain type is in the land set `0xe1f` (types 0-4, 9,
10, 11). Types 5-8 (`0x1e0`: quarry-dug ground, mountain 6, shore 7, deep water 8; the classifier
`0x14009e2c0`) get no zone. Every reachability test has the same shape, mask = the caller's pass
flags | both cells' type bits, then:
- `Map::ValidateCell` (`0x1400a00f0`; `CitizenComponent::CanReach` `0x1401158d0`, the work and target
  pickers, `Map::PathDistance` `0x14009f810`), the build-node test inlined in `BuildStructure::Execute`
  (`0x1400e4fc0`) and `Component::FindBestAdjacentCell` (`0x14012d5d0`): **if either cell's type is in
  `0x1e0` the pair is reachable**, with no zone test; else both zones must be non-zero and equal.
- `Map::RequestPath` (`0x14009cfb0`, every citizen walk, from `MoveTo::PathSetup` `0x1400c8c10`):
  the same test; when an endpoint is in `0x1e0` the A* runs with that endpoint's type added to the
  passable mask *for the whole map* (and both water types when either is in `0x180`). Otherwise a
  zone mismatch returns the ticket {-1, 0}, and `MoveTo::Execute` (`0x1400c8570`) answers 2 (no path).

So a walk with a water or mountain endpoint may cross water or mountains anywhere on the map, and
nothing checks which land zone it ends in. A citizen who ends a walk on land in another zone (or is
standing there when the zones are refilled) is stranded: from a zoned land cell every target on the
town's side fails the zone test, in the pickers and in `RequestPath`. Routes into that state, all in
retail's code:
1. *Builders over water.* A build node on a water cell (a dock / bridge / planking piece placed out
   over the water) passes the build-node test (an endpoint in `0x1e0`), and the builder walks out over
   the water. If the finished piece's cells are stamped with a land type, the next `FillZones` gives
   them a zone of their own unless they touch land or a finished piece, and the builder standing on
   it is stranded (players: "they build the far ones first"). Inferred from the code shape and the
   reports; the stamped type of a finished dock / bridge cell was not read for this entry.
2. *Standing on a zone-less cell.* While a citizen's own cell is a mountain, shore or water cell, every
   picker passes for every target on the map (the source is in `0x1e0`), and the walk's A* may cross
   that type of ground; the walk can end on land across a stream or on an island at the map edge.
3. *The world changes under him:* a building, road removal or quarry splits a zone while he is on
   the far side.

**Route: nomad arrival.** `ImmigrationComponent::FindSpawnCell` (`0x14004e270`) applies the same test
to the spawn cell and the town hall's cell (`NodePosition(0,0)`). The candidate must be type
0/1/2/9/10/11, so the bypass fires only if the hall's cell is type 5..8. A town hall's cell is stamped
type 4 (measured, zone 3), so the zone test runs, and in the measured town it rejected every candidate on
cut-off edge land. What strands nomads is the walk: `NomadComponent::TickWalkIn` (`0x14004f9c0`) takes
"action stack idle" as arrival, so a nomad whose walk to the hall fails (`MoveTo::Execute` returns 2)
counts as arrived where it stands. Allowing the group then converts every member in place
(`ConvertToCitizen` `0x1400501a0`, the nomad's model position), and they are stranded as citizens. Fixed
for nomads by `fix.nomad_arrival` (section 22).

**Why he stands still.** The warmth state `0x14010d2c0` (state 1 / 15) first asks
`CitizenBehavior::FindElderResidence` (`0x14008ec70`, which validates from the citizen's cell), then
falls back to his own home with **no reachability test** and pushes the walk (`PerformWork` chain
`0x1400bd790`, `MoveTo` mask `0xe1f`). The walk fails at once, `ActionStack::Execute` (`0x1400c4c50`)
clears the stack on the 2, and the AI picks the same state on its next update: the citizen stands with
"going somewhere to get warm" until he dies. The food states test reachability and find nothing, so he
starves. A profession change can free him only when the new job's picker accepts a target through the
`0x1e0` bypass (a build node over water, for instance), which lets the next walk cross back.

**Ruled out.** (a) The pickers do test zones (the same test as `FindSpawnCell`); the gap is the
`0x1e0` bypass, not a missing test. (b) No path request leak freezes a citizen: a refused request
returns {-1, 0} without claiming a result slot, a search that fails publishes an empty path and
`TakePath` (`0x1400a6d50`) makes `MoveTo::Execute` answer 2. (A walk destroyed while its search is
queued never frees its slot: `MoveTo`'s dtor `0x140022900` does not release the ticket, so the result
table grows by one slot per abandoned walk. A slow leak, not a freeze.) (c) §18 (`fix.path_query_first`)
only moves the synchronous query (boats, road tool) to the head of the queue; a citizen's walk is
answered in the same order as before, so it neither causes nor cures this.

**Levers, if this is ever changed** (a `fix.*` row, `docs/extensions/DESIGN.md` §3; not ruled):
- *Rescue, narrowest:* in the warmth fallback, when `CanReach(home)` fails, push the home walk with
  the zone-less types in its pass flags (`0xe1f | 0x1e0`): `RequestPath` then takes its bypass and the
  A* wades or climbs home, the same movement retail already allows any walk with a water endpoint.
  Reaches only cold citizens (every winter); a stranded citizen in summer still starves.
- *Rescue, general:* treat a citizen as stranded when his cell is land whose zone differs from his
  home's (both non-zero), and let his walks and pickers use the bypass until he is back. Wider, and it
  needs a rule for the homeless.
- *Prevention* (end a bypass walk only in the zone it started from) would break building a bridge from
  one bank and does not cover route 1, so it is not recommended on its own.

**Fix: `fix.stranded_wade` (WIRED 2026-11-01; trigger ruled by the user, second ruling).** A citizen is
stranded when both hold: (1) he stands on zoned land (not types 5-8) whose zone differs from his *anchor*
zone, his home's zone, or the nearest town hall's when he is homeless; (2) since he entered his current zone,
at least one of his walks to a target in the anchor zone was refused on the land-to-land zone mismatch
(`RequestPath` {-1, 0}, `MoveTo::Execute` answers 2). It clears when he stands in the anchor zone again; a
walk that succeeds inside his zone does not reset it, nor does a refusal toward another zone. A citizen
whose home is in his own zone is never rescued: one living across water by the player's choice, and the
town citizen whose walks to a work target across the river are refused (measured: 45 refusals in 200 s).
Three seams in `src/game/Action/moveto.cpp` report the walks: `AbstractImpl` (`0x1400c8ab0`, the refused
ticket, classified from the two endpoint cells and recorded with the zone he stands in), `Execute`
(`TakePath` gave an empty path) and `Exit` (`0x1400c8520`, an arrival on the destination cell; a record
made in another zone than the one he arrives in is dropped). The state is `CharacterComponent`'s padding
(`+0x44`: flags in byte 0, the zone of the recorded refusal in bytes 2..3), never saved: a load starts
every citizen unstranded. The rescue runs where the AI picks its next state, at the head of
`CitizenAIComponent::TransitionState` (`0x14010db90`), for any state: one walk with the pass flags
`0xe1f | 0x1e0` (retail's own wade / climb bypass in `RequestPath`) to his home's interaction node, or to
the nearest town hall when he is homeless or his last wading walk home failed (if that one fails too, the
record is dropped and the next refusal tries home again). The walk carries the AI's usual "decide again"
lead message, and while it is out no state pick replaces it (`Decide`'s cold / hunger / housing
transitions call `TransitionState(1)` while an action runs, and every state's builder clears the stack
first; without this the rescue was cleared on the next update). A builder left on a finished dock or
bridge piece is covered by the same rule. Composes with `fix.nomad_arrival` (§22): that row keeps cut-off
nomads from becoming citizens; this one rescues a citizen stranded by any other route (or by a nomad
arrival with §22 off). Code `src/ext/stranded_wade/`, test `tests/unit/ext_stranded_wade_test.cpp`, one
`[StrandedWade]` stderr line per rescue for the first 16. Off under `/profile:faithful`.

*Runs (2026-11-01, functional dir, seed 12345, speed 10, debug exe, a throwaway probe not committed):* one
housed citizen teleported to grass in the zone across a river and kept cold.
- Ordinary start town, faithful: he never left that zone in 240 s and ended standing in the warmth state.
- Stress town (60 citizens, a town hall), row on, nothing set by hand: his first walk home was refused
  (recorded), retail's `CitizenComponent::Update` then dropped his home (an idle citizen whose home fails
  `CanReach` gets `SetHome(nullptr, -1)`), so his anchor became the town hall's zone; the next state pick
  sent him wading 44 cells over the river to the hall, where the record cleared and he went on with his
  needs. One rescue in the town, no other citizen rescued, no crash.
- **Open (needs a ruling):** in the ordinary start town the same citizen is *not* rescued. He loses his home
  at once (above) and the start town has no town hall, so he has no anchor. Every stranded citizen becomes
  homeless this way, so the rule rescues only in a town with a town hall. Candidates: keep the home's zone
  as the anchor for a while after the home is dropped (the padding holds one zone; the "since he entered"
  zone would then have to be re-derived from his walks), or fall back to another town building (a
  storage barn, the start town's centre) when there is no hall.

## 20. The Firefighter achievement asks for 20 wells and is awarded at 40

**Symptom.** "Firefighter: Build 20 wells in a single town." is awarded only once the town has 40
finished wells. Players have reported it for years (Steam, February 2025: "You will need 40 well. It's a
very well known bug/typo"; the poster's achievement came at 38 + 2 wells).

**Retail mechanism.** The threshold is game data, not code. The achievement list in the user's install
holds, for Firefighter, one `StatGoal` with stat type 0x19 (finished, standing buildings of a template:
`0x1400d81b0`, one per entity, no double count), template `Template\Well.rsc`, comparison 10 (`>=`) and
the constant 40. The description string `FirefighterDesc` reads "Build 20 wells in a single town."
(`ToolKit/resource/Dialog/StringTable.rsc`). `StatGoal::CheckGoalState` (`0x1400da720`) compares the
count with the constant as given.

**Proof it is retail.** The goal's fields read from the loaded data in the port (a one-run dump of every
achievement's goals, 2026-11-01: `stat A=25 cmp=10 fixed=40 a=Template\Well.rsc`); the counting body
`0x1400d81b0` and the comparison are the port's transcriptions. The player report above is retail.

**Lever.** Compare that one goal with 20 (what the description says), or change the description to 40.
The description is what the player is shown, so the fix takes the number from it.

**Fix: `fix.firefighter_wells` (WIRED 2026-11-01).** In `StatGoal::ComputeOperands`
(`src/game/Goal/statgoal.cpp`), the constant operand goes through
`Ext::FirefighterWells::Threshold` (`src/ext/firefighter_wells/firefighter_wells_rule.h`): with the row
on, a goal with stat 0x19, the constant 40 and the template `Template\Well.rsc` compares with 20; every
other goal, and a mod that sets its own constant, is unchanged. The data in the install is not touched.
An award is written to the settings (`Ach/<index>`) and announced in the event log; there is no Steam in
OpenBA. Off under `/profile:faithful`.

## 21. Selecting a crop field, orchard or pasture crashes when the town names a crop it cannot find

**Symptom.** The game faults ("fatal access violation") the moment the player clicks a crop field,
orchard or pasture, finished or still a construction site; placing one is fine. It depends on the map
seed and the starting condition, or on a save made with a mod that is now off. About twelve player
reports 2022-2026; a mod author traced one to a starting condition that names an orchard seed "Fig"
where the mod's resource is "Fig Tree".

**Retail mechanism.** The three panels share `SelectResourceUI::PopulateCombo` (`0x140078830`), which
fills the seed / tree / animal menu from the town's available-agriculture list (`CitizenBehavior +0x120`,
count `+0x128`) and reads each entry's sub-description count (`entry+0x30`) with no test. Two writers can
put null in that list:
- the start town (`0x1400a3300`): for each of the start condition's three pools (crops `+0xb0`, trees
  `+0xc0`, livestock `+0xd0`) it picks an entry at random, resolves its name with the find-only resolver
  `0x14001b1d0`, and passes the result untested to the dedup-append `0x140091a20` (`0x1400a34b9`,
  `0x1400a3549`, `0x1400a35e7`; the livestock loop also hands it to the herd spawn `0x1400a0b10`). A name
  that does not resolve appends null.
- a load: the list is read with the `ExternalPtr<ComponentDescription>` array body `0x1400049c0`, which
  stores null when `FindOrLoad` finds nothing for the saved path. A save that has a mod's crop in the list
  (granted at start, or bought from a trader, `0x140081420`) loads with a null entry once the mod is off;
  so does the world an in-game Mods change restores after disabling that mod.
Either way, the first panel that builds the menu faults reading address `0x30`.

**Proof it is retail.** The decompile of `0x140078830` (the `*(int *)(lVar4 + 0x30)` loop bound with
`lVar4` the list entry) and the disassembly of `0x1400a3300` (`CALL 0x14001b1d0` / `MOV RBX,[RBP-8]` /
`MOV RDX,RBX` / `CALL 0x140091a20`, no test between) and of `0x140091a20` (compare-and-append, no null
test). `0x1400049c0` writes `local_58 = 0` and stores it through the element when `FindOrLoad` returns
null. The port transcribes all four (`selectresourceui.cpp`, `starttown.cpp` `SeedAgriPool`,
`game.cpp` `CitizenBehavior::RemoveEntity`, `External.h` `SerializeExternalArray`). Unmodded data alone
does not produce the null: every vanilla start condition names resources that exist.

**Lever.** Keep the null out of the list where it is written, not at the panels: the start town skips an
entry that did not resolve (after its random pick, so the world RNG draws as in retail, and with no herd
spawned from it), and the load drops the null entries from the list it read (order kept: the panels'
menus index the list by position).

Fix: `fix.unresolved_seeds` (tier 1), default on, off under `/profile:faithful`. Two seams read the row:
`SeedAgriPool` (`src/game/Game/starttown.cpp`) and `CitizenBehavior::Serialize`'s `+0x120` array
(`src/game/Game/citizenlist_serialize.cpp`). Code `src/ext/unresolved_seeds/`, test
`tests/unit/ext_unresolved_seeds_test.cpp`. Each seam prints one `[UnresolvedSeeds]` line to stderr when
it drops something.

## 22. Nomads whose walk to the town hall fails count as arrived and become citizens where they are stuck

**Symptom.** A nomad group spawns at the map edge, cannot walk to the town hall (behind a river, on
cut-off land), and the town hall reports it as arrived anyway. If the player allows it, the new citizens
appear where the nomads stood and starve there (player reports of nomads "spawning behind a river and
starving", collected in the 2026-11-01 community sweep).

**Retail mechanism.** `NomadComponent::TickWalkIn` (`0x14004f9c0`, state 0 of the nomad's state table)
waits only for the action stack to be idle (`CharacterComponent+0x60 == -1`) and has no position test.
When the walk to the meeting point fails, `MoveTo::Execute` (`0x1400c8570`) answers 2 and
`ActionStack::Execute` clears the stack; the nomad is idle where it stands, posts event 6 (ARRIVED) to
the town hall's `ImmigrationComponent` and starts to loiter. Once every member has posted ARRIVED the hall
moves to "waiting for an answer" (`OnArrived` `0x14004cff0`). On Allow, `Accept` (`0x14004d450`) calls
`ConvertToCitizen` (`0x1400501a0`) for each member, which places the citizen at the nomad's current model
position. The citizens are then stranded as in section 19. Loitering (`0x14004f600`, cells of type < 3
within 8 of the meeting point) and walking out (`0x14004f5e0`, back to the spawn point) do not rescue
them.

**Proof it is retail.** The Ghidra decompile of `0x14004f9c0`: the idle test, the `hall == null` destroy
arm, `PostEvent(..., 6)` through the hall's StateMachineComponent node and the transition to state 1, with
no read of the nomad's position or of `+0x3c` (the meeting point). `0x1400501a0` reads the nomad's
ModelComponent transform for the citizen's position. The port transcribes both
(`src/game/Component/nomadcomponent_states.cpp`, `immigrationcomponent_states.cpp`).

**Lever.** Arrival means being near the meeting point. A nomad that is idle away from it walks there once
more; if it is still away when idle again, it leaves the hall's group and walks back to the map edge, as
a turned-away nomad does. A walk that succeeds ends on the meeting point, so nothing changes in a game
where every walk succeeds. (A stricter spawn test, choosing only spawn cells from which the hall is
reachable, was proposed and not ruled; it would not catch zones joined only through building footprints,
which the arrival test does.)

**Fix: `fix.nomad_arrival` (WIRED 2026-11-01).** In `NomadComponent::TickWalkIn`, after retail's
`hall == null` arm, `Ext::NomadArrival::Decide` (`src/ext/nomad_arrival/nomad_arrival_rule.h`) lets an
idle nomad count as arrived only within 4 cells of its meeting point. Otherwise the first time it re-issues
the walk, and the second time `NomadLeaveHall` takes it out of the hall's group, forgets the hall and walks
it back to its spawn point, where retail's `hall == null` arm destroys it. The hall then moves to the answer
state (0x11) if the members left have all arrived, or back to idle (state 2) if nobody is left; the 0x10 -> 2
transition is appended to the hall's state table (always present, requested only by the row; the indices of
retail's transitions are unchanged). The retry mark is a byte of the component's padding (`+0x54`), not
saved. Nothing new is written to a save. Off under `/profile:faithful`.

**Runs (2026-11-01, `tools/harness_route.sh immigration`, stress town, seed 12345).** No map in reach has
nomads whose walk to the hall fails, so the failure was forced in a throwaway build (the nomad's walk to the
meeting point pushed no action: the stack clears at once, as when `MoveTo::Execute` answers 2). Faithful
profile: the first group (15) "arrived" at the map edge the frame after it spawned, about 140 cells from the
hall, and Allow made 15 citizens there (the route's PASS). Row on: each nomad retried once, gave up, left the
group; the empty group sent the hall back to waiting, and no group reached the answer (no crash, no hang).
With the real walk and the row on, all 70 arrivals ended within 0.07 cells of their meeting point and the
route PASSed (one group allowed, one denied).

## 23. A Mods change is lost after a crash, and a change made in a game is undone at the next menu

**Symptom.** Players report that "a crash resets the mod list", that a mod turned on or off in the Mods
window is back to its old state at the next start, and that changing mods without a full restart crashes
later (the community's "restart twice" ritual; 2026-11-01 sweep, leads 4 and 5).

**Retail mechanism** (Runtime-x64.dll and Application-x64.exe):
1. *The list reaches disk only at a clean shutdown.* `ModDialog::Apply` (`0x14006ea70`) calls
   `Modification::SaveList` (`0x1800b05f0`), which writes `ModEnabled_<name>` / `ModIndex_<name>` into the
   in-memory `Persistence` map only. `Persistence::WriteSettings` (`0x1800b88e0`) is called from
   `Engine::Shutdown` (`0x1800db671`, `0x1800b7ed2`), the launcher's `PlatformConfigure`,
   `SetAchievementMet` and the Options dialog's OK, not from the Mods dialog. The reload that follows a
   Mods change is where a bad mod set crashes, so the change is lost exactly when it matters.
2. *An in-game change is never kept.* `Engine::Loader::Load` (`0x1800dc050`) clears `_savingSettings`
   when the next loop is a game loop, so the in-game `SaveList` only copies `_mods` to `_savedMods`
   (`0x1800b0619`). The world reloads under the new set (Apply -> SaveRestore -> LoadFromSave ->
   `Loader::Load`). At the next menu `Loader::Load` sets `_savingSettings` again and calls `RefreshList`,
   which reads the list back from `Persistence`, i.e. the pre-change list, and remounts the old set: every
   resource whose package changed is reloaded or force-unloaded a second time, and the player's change
   silently disappears. A world restored after an in-game disable also loads missing descriptions as null
   (section 21).

**Proof it is retail.** The disassembly of the functions above (handin of 2026-11-01, contentload). The
port transcribes them (`src/game/Dialog/moddialog.cpp`, `src/engine/io/modification.cpp`,
`src/engine/engine/engine_lifecycle.cpp`); mechanism 2 was reproduced in the port with the pause-menu
harness (`t<row>` / `q<id>` tokens): the quit to menu ran a second remount with the list read back from
the settings.

**Lever.** Write the settings when the Mods dialog is confirmed; refuse changes in a game, so the list is
only changed from the main menu (ruled 2026-11-01).

**Fix: `fix.mods_persist` (WIRED 2026-11-01).** Two seams read the row:
- `ModDialog::Apply` (`src/game/Dialog/moddialog.cpp`): after `SaveList`, before the reload,
  `Persistence::WriteSettings`. At the main menu `SaveList` has just put the list in the settings map, so
  the file holds the new list before the reload starts.
- `MainMenuDialog::OnOpened` (`src/game/Dialog/mainmenudialog.cpp`), the pause menu only: the Mods button
  is disabled with retail's own `Enable(false)`, the call retail makes for a game with flag bit 10. It shows
  greyed out like any disabled button; no new text, no new dialog.
Not sim-visible; nothing new is saved. Off under `/profile:faithful` (retail's behaviour, both parts).

## 29. A save that cannot be written fails silently, and a failed overwrite destroys the previous save

**Symptom.** Saving shows the usual spinner and then nothing: the save is missing from the list, or
an older save of that name has turned into a blank row that will not load. Players report it with a
read-only or OneDrive-synced Documents folder, with Windows Controlled Folder Access ("ransomware
protection") on, and with antivirus software that blocks the game's writes. The autosave fails the
same way, so a player can play for hours with nothing saved.

**Retail mechanism.** `Save::SaveLoad::UpdateSave` (Runtime `0x1800c2a80`) state 1 builds the two
save blocks and queues the write on the persistence worker (`0x1800b75c0`, which clears the worker's
status `Persistence+0x48`); state 2 waits only for the minimum spinner time (`[this+0x8] > [this+0x10]`)
and then frees the saved state and resets. It never reads the status. The worker's save job
(`0x1800b6e00`) opens the save itself with `IO::FileStream::Create` (`CreateFileW`, `CREATE_ALWAYS`),
writes the header and the blocks in place, and stores status 1 on success or 2 when the create fails
or any `WriteFile` comes up short. So a blocked create says nothing, and a write that fails part way
(disk full, a scanner or sync client taking the file) has already truncated the previous save of that
name to a file that fails the save list's CRC check.

**Proof it is retail.** The decompile of `0x1800c2a80`: state 2 is the time test, the two frees and
the field resets, with no read of `Persistence+0x48` anywhere in the function; nothing else in the
save path reads it either. The decompile of `0x1800b6e00`: `FileStream::Create` on the pending name,
the four `WriteFile` calls into that handle, `*(param_1 + 0x38) = 1` or `2`. The port transcribes both
(`src/engine/save/saveload_update.cpp`, `src/engine/io/persistence_worker.cpp`).

**Lever.** Report the status the job already computes, and write so that a failure cannot take the old
save with it: the job writes `<name>.tmp` and moves it over the save only after every byte is written
(`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`, `Platform::FileReplace`), deleting it on failure; state
2 waits for the job's status and, when the save did not reach the disk, the game shows a message box
naming the save folder and the usual causes.

Fix: `fix.save_failure_notice` (tier 1), default on, off under `/profile:faithful`. Engine seams
`IO::PersistenceWorker::_extFixSaveTemp` (the temporary file) and `Save::SaveLoad::_extFixSaveResult` /
`_extSaveResult` (the status read), installed by `src/ext/registry_boot.cpp`; the notice is
`src/ext/save_failure_notice/` over `Dialog/Extensions.rsc:saveFailed`, added to the game's UI without a
nested modal loop. The first failure after a success opens it; further failures in a row (a failing
autosave) do not, until a save succeeds. Every save path takes it: a manual save, the autosave, and
`ext.background_save` / `ext.async_save_compress`, which change how the blocks are built but end in the
same enqueue and save job. Test `tests/unit/ext_save_failure_notice_test.cpp`.

## 30. A trading post whose dock boats cannot reach never trades, and nothing says why

**Symptom.** A trading post is built, staffed and stocked, and no merchant ever arrives. Players
report it for posts on ponds or lakes, behind shallows, or on a river bend; moving the post a few
tiles sometimes cures it. The game gives no hint that the dock is the problem.

**Retail mechanism.** The post's idle state (`TradeComponent` state 2 per-frame, `0x14005aa60`) counts
the merchant timer down, clones the trader boat and calls `BoatComponent::SetDestination`
(`0x1400456e0`) with the post. That builds the route from the map's river polyline and two water A*
legs (`0x14009ffc0`, mode `0x380`: terrain classes 7 to 9): river entry to the dock's "boat" point,
and the dock to the river exit. When either leg comes back empty, the test at `0x140045e2e` /
`0x140045e38` (`TEST EBX,EBX / JLE`, `TEST EAX,EAX / JLE` to `0x140046104`) skips both and the route
is the bare polyline. The boat then docks only if the polyline happens to pass close to the dock;
otherwise it sails the river, is deleted at the route's end (state 0, `0x140045250`), and the post
re-arms its timer, for ever. Placement (`0x14009f5d0`) never tests that the dock's water joins the
river; shallow cells below class 7 cut the water graph while still looking like water.

**Proof it is retail.** The disassembly of `0x1400456e0`: the two `CALL 0x14009ffc0` at `0x140045d77`
and `0x140045e23`, the length of each kept in `RBX` / `[RBP+0x8060]`, then the two `JLE` to the
departure-lane code with nothing between them that records or reports the failure. The port
transcribes it (`src/game/Component/boatcomponent_route.cpp`). A working dock's legs are not empty:
the `fix.path_query_first` measurements (`docs/extensions/handins/PATHQUERYFIRST.md` §3) logged
route lengths of 746 / 207 and 767 / 184 for the stress town's posts.

**Lever.** Say so where the player looks for news: an event-log line (and the event ribbon) at the
post, when a merchant's boat finds no water route to its dock. Whether boats come and where they sail
stay retail's.

**Ruling (2026-11-01).** Trading-post placement stays as retail's: a dock that boats cannot reach is
not refused. The event-log warning below is the whole fix.

Fix: `fix.boat_reach_notice` (tier 1), default on, off under `/profile:faithful`. The failed-leg arm
of `BoatComponent::SetDestination` calls `src/ext/boat_reach_notice/`, which appends one ordinary
event-log entry (the trader's event type 0xd and icon, warning colour, the post's tile; the game's
message table has no text for it, so the text is ours). A save keeps it like any other entry, as text
and five ints, and retail reads such a save unchanged. No unit test: the row has no pure logic.

## 27. A cancelled building site loses most of its delivered materials

**Symptom.** A building site that is cancelled, or removed with the Remove tool, after its materials were
delivered gives back only a few piles. They lie on the site's ground, and the rest is gone ("all
disappeared into the ether", Steam). A large footprint, such as a town hall, refunds the least.

**Retail mechanism.**
- `DestroyComponent` state 4's enter (`0x14011cfd0`) skips the `ClearStorage` haul-away for a site that is
  not finished.
- State 5 (enter `0x14011cc70`, or its tick `0x14011c120` once any labour was spent) calls
  `ScatterContents` (`0x14011c530`). For an unfinished site it lists the storage stacks,
  `(count - reserve) / (laborProgress > 0 ? 2 : 1)`.
- It then walks the footprint with one cursor. A pile goes only on a cell with no blocker (`cell+0x00`),
  terrain type 2 and terrain height in [0, 0.2). Each cell takes one pile of at most `100 / weight`
  (`0x14011ca59`).
- The site's zone stamp (`Map::StampFootprint` `0x14009d360`) is still on the cells: it is removed only when
  the entity drains after `Destroy_MarkAndPushRemoval`. It stores the site as the blocker on every non-'.'
  cell, so only the footprint's '.' cells qualify.
- When the cursor leaves the footprint (`row == depth`), the material still listed is dropped with no
  message.

**Proof it is retail.** The decompile of `0x14011c530`:
- the blocker / type / height test on `*(cells + idx*0x30)`, `+0x29` and `DAT_1401720fc`;
- the `while ((iVar30 != param_5) && (0 < iVar24))` loop exit;
- the list freed with nothing done for the rest.

The port transcribes it (`destroycomponent_states.cpp`). Measured on the port with retail's logic
(`fix.cancel_refund` off), on the `clearoverlap` town hall (10x9, 90 cells) cancelled with 118 units in
storage and no labour:
- 10 of the 90 footprint cells qualified (no blocker); all 90 passed the type and height tests.
- The first material's 62 units took 7 cells (9 a pile).
- The second material's 56 units got the last 3 cells (18 units). The other 38, a third of the delivery,
  were dropped.

The run used a temporary hook in `src/diag/clear_overlap_harness.cpp` (not committed) that called
`DestroyComponent::RunStateMachine` once the hall held 1500 weight units. That is the Remove tool's path
for a site. Retail itself was not launched.

**The halving is retail's design, not part of this fix.** A site with any labour (`laborProgress > 0`)
first goes through a demolition work (state 5's `BuildStructure`), and its stock is then halved. A finished
building refunds half its recipe the same way (a quarter after a fire). So the halving is read as the
price of a started building. It is left as retail has it.

**Lever.** Place what the footprint walk could not, around the site, through the search a citizen's dropped
load uses (`Map::FindPlacementCell` `0x14009bd40`, as `DropOffRawMaterial::DropOnGround` `0x1400c78c0`
calls it). Piles keep retail's size.

Fix: `fix.cancel_refund` (tier 1), default on, off under `/profile:faithful`.
- What the row does: after each material's footprint walk, the units left go out as piles of
  `100 / weight`. Each pile's cell is the first one `FindPlacementCell` returns from the ring of cells just
  outside the footprint. The site's own cells never qualify: the blocker is on the non-'.' cells and the
  footprint count on the '.' cells.
- What stays as retail: what no cell within that search's six rings can take is still dropped. The new
  piles are ordinary piles: they post their own Pickup work and go to storage, and nothing new is saved.
- The fix-on run of the same scene: `[CancelRefund] site 183,224 10x9: 38 of 38 units laid in 7 piles
  outside the footprint`.
- Code: the seam is `DestroyComponent::ScatterContents`, the code is `src/ext/cancel_refund/`, and the test
  is `tests/unit/ext_cancel_refund_test.cpp`. The row prints one `[CancelRefund]` line per scatter that
  needs it, for the first 16 only.
- Not touched: `fix.foundation_piles` (§17) moves piles off a site that is waiting to be built; this row
  acts once, when a site is removed.
