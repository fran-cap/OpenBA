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
