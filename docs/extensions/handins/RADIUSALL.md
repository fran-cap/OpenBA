# RADIUSALL hand-in

Branch `ext/radius-peers`, on `main` af1ef837. The task (the user's): Shift+click a gatherer or production
building's circle and the circle is shown for every building of that type, so the areas already covered are
visible while a new one is placed; on a phone, a long press. **Result: `ext.radius_all`, class A, on by default.**

## 1. What retail does
A building with a work area carries a `RadiusDescription` (a radius and a decal material). Its circle is a
terrain decal made by `RadiusDescription::AddHighlight` (0x140055430, through `WorldGeometry::AddDecal`
0x1400b0d80) and exists for one building at a time in two places:

* the building's panel: `RadiusUI::OnOpened` (0x140077650) adds it round the footprint's centre cell,
  `RadiusUI::OnClosed` (0x140077600) removes it;
* the building being placed: `PlacementTool::Update` adds / moves it under the ghost.

So while a second gatherer is being placed, the first one's circle is not on screen.

## 2. What the extension does
A Shift+click on a building whose template has a `RadiusDescription` with a material switches "show the
circles of this kind": every building the town has of that template (the town's per-template tree,
`CitizenBehavior::entitiesByTemplate`, the one the toolbar counts buildings in) gets the same decal the panel
would add: the same `AddHighlight` call, round the same centre cell (`CentreCell`, `radius_all_math.h`). The
click selects nothing. A Shift+click on any building of a kind already shown hides that kind's circles.
Several kinds can be shown at once. A Shift+click on anything without a circle is retail's click.

* The circles stay while the player selects, places and builds: a building of a shown kind built later gets
  its circle, a removed one loses it (`Frame`, once per game update; it compares a digest of the kinds'
  entity lists and does nothing while it is unchanged).
* Shift is retail's Shift: `GameKey::PathMethod` read with `KeyMap::GetValue`, the key the road tool reads.
  A player who rebinds it rebinds this too.
* A touch screen (`ext.touch_pointer` on): a finger held still on the world for half a second with no tool
  (`IsLongPress`) sends the pick a tap sends, armed as a Shift+click. It fires while the finger is down; the
  lift does nothing. A long press on a building with no circle selects it, as a tap does.
* The click plays the UI's button click.
* Row switched off in a game: the circles are removed at the next update and the kinds forgotten.

## 3. Why class A
* Nothing is simulated differently: a decal is drawn, no cell or entity is touched.
* Nothing is saved. `WorldGeometry` serialises its decal list, so the row's decals are removed at the head
  of every save (`BeforeSave`, the save arm of `GameUpdateLoop::SerializeAllHead`, on the main thread for
  the plain save, the roundtrip check and `ext.background_save` alike) and put back by the next `Frame`. A
  plain save runs inside step 14 of the Update and `Frame` follows it, so the circles are back before the
  frame is drawn (by the order of the calls; not measured on a real save); with
  `ext.background_save` they are off the screen while the worker writes (the worker reads the decal list,
  so nothing is added until `BackgroundSave::Busy()` is false).
* The kinds shown are not kept: they are tagged with `Ext::LockEpoch()` and dropped when another game holds
  the lock (a new game, a load, back to the menu). The old game's decals went with its `WorldGeometry`.

One thing a save can show: the decal list never shrinks (retail's `RemoveDecal` 0x1400b0d00 frees a slot in
place), so a town that showed N circles saves up to N more free slots than one that did not, as a retail town
does after its own circles. A free slot draws nothing and is reused by the next decal; retail reads the save.

## 4. Seams
| Where | What |
|---|---|
| `EditTool::OnKey` (0x1400fd030), after the tutorial's target test, before the selection | `Ext::RadiusAll::Click(foundEntity, keyMap)`: true = the kind was switched, return without selecting |
| `GameUpdateLoop::Update`, after step 14 (`SaveLoad::Update`) | `Ext::RadiusAll::Frame()` |
| `GameUpdateLoop::SerializeAllHead`, save arm, first | `Ext::RadiusAll::BeforeSave()` |
| `Ext::TouchPointer` `AfterUpdate` (the input poll) | the long press: `ArmLongPress()` + the tap's pick |

The first three are one call each and do nothing until a kind is asked for; the row's bool is read at the
click and in `Frame`. Under the faithful profile the row resolves off.

## 5. Known limits
* A building whose panel is open has two circles on the same spot (the panel's and the row's): the ring is
  drawn twice there and reads a little stronger.
* Circles are per template: a modded variant of a gatherer with its own template is another kind.

## 6. Files
| File | Role |
|---|---|
| `src/ext/radius_all/radius_all_math.h` | the kinds' toggle, the centre cell, the digest, the diff, the long press (pure, unit tested) |
| `src/ext/radius_all/radius_all.{h,cpp}` | the click, the per-frame upkeep, the save arm |
| `tests/unit/ext_radius_all_test.cpp` | the row and the rules |
| `tools/ext_icons/radiusall.py` | the row's icon (our own drawing) |

## 7. Verified
* `tools/build.sh unit`: 2380 tests, 0 failures (the three kit cross-checks skipped, no kit output here).
* The gate (`tools/build.sh run`, faithful profile, fixes off) in the functional game dir: new game, running.
* `tools/resave_gate/resave_gate.sh Gate`: PASS against the main checkout's golden.
* The stress town (debug exe, `STRESS_EXT=radius_all`) with a temporary trigger, since removed: every
  template with a circle switched on at once gave 18 circles for 6 kinds, each entity's `_description` was
  its bucket's key, the save arm removed all 18 and the next `Frame` added 18 again; a screenshot showed them.
* Not run by a person yet: the Shift+click itself and the long press on a phone.
