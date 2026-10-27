# Android P11: the touch pointer (`ext.touch_pointer`)

NOT RETAIL. The owner's rulings of 2026-10-26 (`docs/android/TOUCH_PLACEMENT_IDEAS.md` §5, all nine accepted),
built on P10's `ext.touch_mode` (`docs/android/TOUCH_MODE.md`). Lane `p11pointer`.

## 1. Implementation plan

### 1.1 What retail does with the mouse (measured in `src/game/`, not guessed)

| Fact | Where |
|---|---|
| The input poll runs once per fixed step: `Input::Device::Update` (poll, `UpdateState` per device, `PlatformUpdate`), then the message pump, then the loop's `Update`. | `src/engine/engine/mainloop_frame.cpp` (the step loop), `src/engine/input/input_device_update.cpp` |
| `UpdateState` derives `pressed` / `released` from the previous `value` and the polled one. A state written after it holds for the whole step: every reader (`KeyMap::Pressed / Released / GetValue`, the UI's direct reads of `_mouse[0, 1, 4, 5, 6, 12]`) sees it. | `input_device_update.cpp` `UpdateState`; `userinterface_frame.cpp` |
| Mouse inputs: 0 / 1 cursor x / y (client engine pixels), 2 / 3 deltas, 4 / 5 / 6 left / right / middle, 12 wheel. Actions 0..7 are bound to them (`MouseX` .. `MouseMiddle`); 0x13 / 0x14 / 0x15 (`StructureLeft / Right / Type`) and 0x16 (`PathMethod`) are keyboard keys, rebindable. | `src/game/Game/inputbindings.cpp` |
| `GameUpdateLoop::Update`: `UserInterface::Update`, then `PlayerToolManager::Update(time, _mouseCaptured == 0)`. A pointer over any dialog (one that hit-tests it) makes `inputFlag` 0 for the tools. | `src/game/Game/gameupdateloop.cpp` |
| `PlayerToolManager::Update`: `UpdateHotkeys` (the tool's `OnKey(inputFlag)`, then a right click = action 6 with `inputFlag` and no forced tutorial tool (`field_0x80`) calls `OnCancel`, and a 0 answer drops to the EditTool), `UpdateCamera`, the tool's `Update`. | `src/game/Dialog/entityui.cpp`, `src/game/Game/camera_input.cpp` |
| `PlacementTool::OnKey`: `OnKey(0)` closes the CreateBar; actions 0x13 / 0x14 rotate the `RotationSetting`, 0x15 flips; `Pressed(0, 5)` with `inputFlag` places at the cursor through `GetPlacement`. The ghost follows the cursor in `Update` with no button needed. | `src/game/Player/placementtool.cpp` |
| **Retail's single-click mode** (`DragTool::OnKey`, 0x1400fc6b0, shared by Road, Bridge, Fenced, Removal, DragPlacement): idle + `Pressed(0, 5)`: start = end = the cell under the cursor (`PlayerRay_ScreenToCell_101270`), `dragState = 1`, CreateBar opened. Dragging: the end cell follows the cursor every step, `CalcCost` previews, the CreateBar shows the cost. On the release (`Released(0, 5)` or `GetValue(0, 5) == 0`) with `startCell == endCell`: `singleClick = 1`, and the end keeps following the hover. In single-click mode the NEXT `Pressed(0, 5)` commits through `CalcPath` (or the error sound off the map) and resets `dragState / singleClick`. `OnKey(0)` (the pointer over a dialog) with a `Released(0, 5)` resets both. `OnCancel` mid-drag clears the drag and answers 1 (the tool stays); idle it answers 0 (the tool ends). | `src/game/Player/dragtool.cpp` |
| `EditTool::OnKey` / `PathTool::OnKey`: on `Pressed(0, 5)` one ray from the cursor pixel (`PlayerRay_GetMouseRay_1005a0`: the camera's frustum direction at x / width, y / height), `WorldGeometry::TerrainMarch` (the cell's building through `Map::EntityAtCell`), then `Picking::Scene::Intersect` capped at the terrain distance; its user data is the entity. | `src/game/Player/edittool.cpp`, `tool_base_ray.cpp` |
| `UpdateCamera`: the pan locals are a direction (normalised, times retail's zoom-scaled speed); the yaw is a local the keys turn; touch_mode's `CameraInput` assigns them from a source-tagged hold. | `src/game/Game/camera_input.cpp`, `src/ext/touch_mode/touch_mode.cpp` |

So the tools need only three things from a finger: a cursor that moves with the button UP (retail's hover), a press
/ release pair at a parked cursor, and the keyboard actions. All three are input-device states.

### 1.2 Hooks

| Hook | Kind | What |
|---|---|---|
| `Platform::Touch::SetSink` (`src/platform/common/platform_touch.h`, body `src/platform/sdl/touch_sdl.cpp`, shared by every SDL platform) | platform seam | With a sink set: `SDL_HINT_TOUCH_MOUSE_EVENTS` = 0 (no emulated first-finger mouse), and the pump hands every `SDL_EVENT_FINGER_*` to the sink (client engine pixels) and drops it; a real mouse's motion / button / wheel (`which != SDL_TOUCH_MOUSEID`) is reported to the sink too and still reaches the game. Null sink: emulation back on, nothing observed. One call in `pump_sdl.cpp`'s loop. |
| `Input::Device::_extAfterUpdate` (`src/engine/input/Device.h`) | engine seam, null by default (DESIGN.md hook rule 5), null under the faithful profile | Called at the end of `Device::Update`, after `UpdateState`: the ext writes the mouse states and one-step action edges there, before the pump and the loop's update. |
| `Ext::TouchMode::Hold(Source::Fingers, ...)` | P10's camera hold, EXTENDED as TOUCH_MODE.md §4 / §8 foresee | `CameraHold` gains `dragX / dragY` (a grab-the-map drag, fractions of the short edge) and `yawDegrees` (a direct turn). `CameraInput` applies them for the Fingers source only. The Pad source is unchanged. |
| `Ext::TouchMode::Place` -> `Ext::TouchPointer::Place` | one chained line at the top of touch_mode's place function (itself chained from phone_menu's) | Places the action bar and the aim marker. No edit to `src/ext/phone_menu/`. |

No tool, no `EditTool`, no UI body is edited. Nothing is `#if __ANDROID__`: the row is seeded on Android only
(`app_android.cpp` `kPhoneSettings`), off on a desktop.

### 1.3 How edges are injected

The ext keeps a short queue of pointer states (position, left / right button). Each poll pops one and writes it into
`Input::Device::_mouse` with `pressed` / `released` computed against the previous INJECTED value, so a press and a
release that came in one pump become two steps (the latch: a tap is never shorter than one step). Moves coalesce into
the last queued state; a button change always queues. Keyboard actions (rotate, flip) and the cancel (action 6) are
one-step `pressed` edges on the input the KeyMap binds the action to (`GetDeviceForId / GetInputForId`), so a rebinding
keeps them working. While a finger is the last pointer device the ext writes every step; a real mouse event hands the
pointer back at once (last device wins) and the overlay hides after 3 s of mouse use.

### 1.4 What a finger does

| Where the finger lands | One finger | Lift |
|---|---|---|
| The action bar | the button under it (pressed look) | its action, if lifted on it |
| A dialog (hit test `GetFirstContainedDialog`, the tool's CreateBar and our marker excepted) | a plain mouse: cursor at the finger, left down | left up, then the cursor goes back to its last world position (no parked tooltip) |
| The world, PlacementTool | aim: cursor at the finger minus the lift offset, button up (the ghost and cost follow) | parks; Place = a click at the parked cursor; tap-twice (setting) places on a second tap near the first |
| The world, a DragTool | aim, as above (in single-click mode the route / rectangle previews to it); edge auto-pan while the finger is near an edge | idle tool: a press + release at the parked cursor = retail's single-click start; Confirm (the Place button) = the second press = `CalcPath`; Cancel = action 6 = `OnCancel` keeps the tool |
| The world, EditTool / PathTool | nothing | a tap: the snap search, the cursor on the chosen candidate's pixel, a press + release; a second tap near the first within 1.5 s cycles to the next candidate |
| The world, any other tool (a mod's, ext.terrain_edit's brush) | a plain mouse | a plain mouse |

Two fingers on the world: pan (the centroid's motion, grab-the-map), pinch (the spread's log ratio as zoom steps),
spin (the pair's angle change as yaw), through `Hold(Source::Fingers)`. Each component engages past its own threshold
and then follows the fingers. The gesture ends when either finger lifts; the other is ignored until it lifts too.

### 1.5 The snap search

On a tap in a pick tool: rays (the `PlayerRay_GetMouseRay_1005a0` ray, at the sample pixel) at the tap, then on rings
at r/3, 2r/3 and r (8, 12, 16 samples), r = `ext.touch_pointer_pick_radius` device px (default 50). Each ray marches the
terrain and intersects the picking scene capped at the terrain distance, as `EditTool::OnKey` does. The distinct
entities hit, nearest sample first (the centre's own hit first), are the candidates; the cursor goes to the chosen
one's sample pixel, so `EditTool::OnKey`'s own ray picks it with retail's code. No candidate: the tap's own pixel
(retail's cell fallback still selects a building there). The pure parts (`touch_pointer_math.h`: the ring samples,
the candidate order, the cycle, the gesture decomposition, the tap test, the latch queue, the bar layout) are unit
tested.

### 1.6 Settings

- `ext.touch_pointer` class A, live, default OFF; Android seed `ext.touch_pointer=1`.
- `ext.touch_pointer_lift` 0 / 80 / 120 / 160 / 200 / 240 device px (0 = off), default 160.
- `ext.touch_pointer_pick_radius` 0 / 25 / 50 / 75 / 100 device px, default 50.
- `ext.touch_pointer_tap_twice` 0 / 1, default 0.

### 1.7 Files

| File | Change |
|---|---|
| `src/ext/touch_pointer/touch_pointer.{h,cpp}` | the glue: sink, post-poll writer, modes, action bar and marker dialogs, snap search |
| `src/ext/touch_pointer/touch_pointer_math.{h,cpp}` | the pure parts |
| `src/ext/ui/Dialog/TouchPointer.rsc` | the bar (retail button art and sprites: `GoalSuccess`, `ArrowLeft / Right`, `ToolClose`; tooltips from `StringTable.rsc:gameKeys`) and the marker |
| `src/platform/common/platform_touch.h`, `src/platform/sdl/touch_sdl.cpp`, `pump_sdl.cpp` | the finger sink |
| `src/engine/input/Device.h`, `input_device_update.cpp` | `_extAfterUpdate` |
| `src/ext/touch_mode/touch_mode.{h,cpp}` | the hold's drag / yaw, the chained place call |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp`, `resources.cpp`, `CMakeLists.txt`, `app_android.cpp` | row, keys, Sync, the rsc, the seed |
| `tests/unit/ext_touch_pointer_test.cpp`, `sources.cmake`, the counts in the registry / rsc tests | tests |
| `tools/ext_icons/touchpointer.py`, the sheet | the row's icon |

## 2. As built (what differs from §1)

- **Coordinates.** The sink gets client engine pixels (the window's pixel size times the render scale), the cursor's
  own unit; the settings' device pixels go through `Platform::Touch::EnginePixelsPerDevicePixel` (the render scale).
- **The release over a dialog is held three polls** before the cursor goes back to the world position. With one poll
  the info panel jumped across the screen after a tap on its title: `ToolUI::OnUpdate` arms the title drag from the
  press's element event and tests the 5 px move before the button, so a jump on the poll after the release dragged
  it (measured on the S25, `e17.png`). Holding the release lets the panel see the button up first.
- **The bar's Place glyph is `GoalSuccess`** (the game's green check), not `SimpleCheck` (a 16 px white tick that read
  as nothing at the bar's size).
- **The marker** is the game's `ArrowDown`, about half a button high, its tip on the cursor; its dialog passes the
  mouse through (`DialogInstance` bit 2), so the cursor under it stays on the world and the tool keeps `inputFlag`.
- **Which buttons:** a placement tool shows all five; a drag tool Place (= Confirm) and Cancel, plus Rotate for a
  `FencedTool`; no tool, no bar. The bar sits a gap above the higher of touch_mode's `BarRect` / `SubToolbarRect`
  (an open sub-toolbar pushes it up), right-aligned to the safe margin, with touch_mode's cell size.
- **Not built** (in the ideas sheet, not in this task's six parts): the long press for a tooltip, two-finger list
  scrolling over a dialog, the road route modifier (0x16) toggle, the Android back gesture as Cancel. One finger in
  idle (no tool) does nothing but tap (no one-finger pan: not ruled).

## 3. Verification

Builds: `tools/build.sh build --jobs 2` (Windows DX11 debug) and `tools/android.sh build debug --jobs 2 --ccache`
clean; `tools/build.sh unit`: 1531 tests, 0 failures, 1 skipped (the kit cross-check: no `build/ext_resources` in this worktree). New tests: `ext_touch_pointer_*`
(latch queue, tap / aim, edge pan, two-finger classifier, hold drag mapping, snap samples and cycle, bar layout,
registry row and keys), `ext_rsc_text_parse_touch_pointer`; counts updated in `ext_registry_table_shape`,
`ext_values_table_shape`, `ext_rsc_text_embedded_files`; the row's icon (`ext_sheet_every_entry_has_an_icon`).

S25 (debug apk, the owner's settings: UI 42, render scale Full, phone_menu and touch_mode on; the seed appended
`ext.touch_pointer=1`). Taps are `adb shell input tap` (a one-pump down + up: the latch shows it on two polls); held
drags and every two-finger gesture are a uinput touchscreen (`tools/android_multitouch.py`,
`docs/reference/android_mouse.md`). Screenshots: session scratchpad `p11pointer/`.

| Step | What was done | What happened | Shots |
|---|---|---|---|
| (b) aim | Houses > Wooden House, a held drag on the world | the ghost and its cost bar follow the cursor 160 px above the finger, button up; the bar (5 buttons) right, above the open sub-toolbar | `b2_aim`, `b4_aim_ok`, `b6_aim_ok` |
| (b) Place | the check | `[TouchPointer] bar place`, the house is placed where the ghost was | `b5_placed`, `b8_placed` |
| (b) Rotate | Rotate right, then Place | the ghost turns a quarter, then the turned house is placed | `b7_rotated`, `b8_placed` |
| (b) Cancel | the cross | the tool ends (retail's right-click path: `OnCancel` 0, `SelectTool(null)`), the bar goes | `b9_cancel` |
| (c) pinch | 150 -> 300 px spread, later 350 -> 120 | `pinch on 5.39 steps`, distance 25 -> 20 (the range end); out: 60 -> 23.8 for an in-pinch after it; the view zooms | `c0_before`, `c1_after`, `e2`, `e6` |
| (c) spin | 200 px radius, 60 deg | `spin on 53.0 deg` (8 deg to engage), yaw 0 -> 53 | `c2_spin_before`, `c3_spin_after` |
| (c) pan | both fingers 150 px down / 250 px right | `pan on 0.000,0.121`, target 0.5,0.5 -> -1.6,2.1; the ground follows the fingers | `c4_pan_after`, `e1` |
| (c) last device wins | a uinput mouse moved, then a finger tap | `mouse: the pointer goes back to the mouse`, then the finger took the pointer again at its next touch | log |
| (d) road | Roads > Road; one drag + lift (the start), a second drag (the end), the check | `drag tool: start ... (single-click mode)`; the route preview and the cost bar follow the second finger; the check commits it | `d2_preview`, `d4_preview`, `d5_confirmed` |
| (d) rectangle | Removal > Harvest Trees, the same three steps | the 7x6 rectangle with its size in the cost bar follows; the check marks it | `d10_rect_preview`, `d11_rect_confirmed` |
| (d) edge pan | road tool, a finger dragged to x 2320 and held 2.5 s | the camera pans right while the finger is held at the edge | `d6_edge_before`, `d7_edge_after` |
| (d) Cancel mid-drag | the cross in single-click mode | the drag ends, the road tool stays (retail's `DragTool::OnCancel` 1) | `d8_cancel_drag` |
| (d) marker | a drag held 3 s | the down arrow sits on the cursor above the finger | `d12_marker`, `f1_lift240_marker` |
| (e) pick snap | a tap 34 px beside a child (the exact ray hit nothing) | `1 candidate(s), chose 0 at 1000,601`: the Citizen panel opens on the child | `e12_direct` |
| (e) snap to a building | a tap 33 px under the storage barn's edge | `chose 0 at 1210,379`: the Storage Barn panel | `e24` |
| (e) tap-to-cycle | three taps on one spot between two candidates, 0.7 s apart | `chose 0`, `chose 1 (cycle)`, `chose 0 (cycle)`: the panels alternate | `e32_cycle` |
| (f) lift setting | `ext.touch_pointer_lift=240` | the aim sits 240 px above the finger | `f1_lift240_marker` |
| (f) tap twice | `ext.touch_pointer_tap_twice=1`, two taps 0.4-0.5 s apart near each other | `tap twice: place`: the click went to the aimed spot (there an invalid place: the game's error and red ghost, retail's rule) | `f3_tap_twice`, `f5_tap_twice_placed` |

Taps 1.2 s apart did NOT cycle in an earlier try (`kCycleSeconds` 1.5 s counts from the last tap's lift; the
`adb input` taps with a screenshot between them took longer): the cycle window is a feel to tune by hand. The test
settings were put back on the phone afterwards (the owner's `settings.cfg` copied back into the app-owned file).

Not verified: a real finger (every input here is synthetic: `adb input` and uinput); the KVM mouse (a uinput mouse
only); the tutorial's forced placement (`field_0x80`); render scale Half; the desktop with the row on (it is off
there by default; under the faithful profile no seam is installed: `Input::Device::_extAfterUpdate` null, no sink).

## 4. Rulings owed

- **The lift offset's default** (160 device px, about 10 mm on the S25; 240 tried): the owner's thumb.
- **The cycle window** (1.5 s) and the **pick radius** (50 px): to feel by hand.
- **A settings row for the three keys.** They are `settings.cfg` keys only; the Enhanced tab is already 11 rows tall
  on the phone (P9). A combo like Touch controls would fit the pattern.
- **Two-finger spin sense**: the pair turning clockwise on the screen turns the yaw positive; the owner may want the
  opposite.
- **The bar's look**: the toolbar's buttons in the toolbar's frame; Rotate shows an arrow beside the Housing icon,
  Change model two small arrows (no retail rotate / flip sprite exists).

## 5. Follow-ups

- The long press for a tooltip, two-finger list scrolling, the road route modifier toggle, Android back = Cancel
  (TOUCH_PLACEMENT_IDEAS.md §4.5).
- Merge note: `src/ext/touch_mode/touch_mode.cpp` gained the chained `TouchPointer::Place` call at the top of
  `TouchMode::Place` and the Fingers hold's drag / yaw in `CameraInput`; `camera_input.cpp`'s touch_mode seam line
  also runs for `ext.touch_pointer`. ext.touch_pointer has its own `Sync` (registry_boot.cpp), independent of
  `TouchMode::Sync` (which the p11bugs branch removes).
