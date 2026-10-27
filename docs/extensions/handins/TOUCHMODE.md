# TOUCHMODE hand-in

`ext.touch_mode` ("Touch controls"): a phone layout for fingers. One class A row, **off by default**,
switched on by the Android first-run `settings.cfg` (`src/platform/android/app_android.cpp`, with
`ext.touch_mode_toolbar_scale=200`), live, no serialized state. Design, rulings (2026-10-26) and the
session log: `docs/android/TOUCH_MODE.md`.

## What it does

* **Control cluster** (`Dialog/TouchControls.rsc`, our text `.rsc`): a D-pad (8-way camera pan), Rotate
  left / right above it, Zoom in / out right of it, at the bottom left, inside ext.phone_menu's safe area
  (or 2 % of the short edge without that row), in ONE frame around the group, the toolbar's own
  (`SharedElements.rsc:dialogBorder` + `sunkenBorderPad6`, as `Toolbar.rsc:toolBar`). Sizes are
  fractions of the short edge (cell 0.11, gap 0.014, frame 0.03; `touch_math.h LayoutCluster`), so the
  cluster keeps its physical size at any UI scale and render scale. Every look is the game's own, read
  from the install: the buttons inherit `Toolbar.rsc:buttonToolbar`, the `SpriteSheet.rsc` glyphs
  ArrowUp / Down / Left / Right, Plus, Minus, ToolCamera, the tooltips the Input tab's key names
  (`StringTable.rsc:gameKeys`). No art of ours but the row's icon.
* **Input**: one `// EXT SEAM` call in `PlayerToolManager::UpdateCamera` (`camera_input.cpp`), after the
  keys set their locals: a held control assigns `panX` / `panY` as the keys do (a key that set one keeps
  it), turns the yaw at retail's key rate (90 deg/s) and adds zoom steps (one at the press, one every
  0.15 s after 0.35 s). The capture rule (owner, 2026-10-26; `touch_math.h Capture`): the control a press
  starts on acts until the release, wherever the finger slides (the pad keeps its direction from its
  centre, even off the pad); nothing else fires meanwhile; a press that starts elsewhere captures nothing. The cluster dialog takes the pointer from
  the world over its box. `Hold(Source, CameraHold)` is the entry point the later `touch_pointer`
  extension calls (last device wins).
* **Toolbar at its own scale**: the engine seam `UI::UserInterface::_extDialogScale` (null by default and
  under the faithful profile; installed by ext.phone_menu's `Sync`, which asks `TouchMode::Scale` first and
  then its own tall-dialog fit, P11; `userinterface_frame.cpp` layout and pump loops, `userinterface_draw.cpp`
  per-dialog world transform and scissor scale, `userinterface.cpp` `GetFirstContainedDialog`) gives
  `Dialog/Toolbar.rsc` dialogs (the toolbar and every sub-toolbar) a scale k: laid out, hit-tested and
  drawn in units of layout / k. k = the requested scale (`ext.touch_mode_toolbar_scale` 100..200),
  shrunk to fit the bar region right of the cluster (ruled), never below 1.
* **Compensations** (`touch_mode.cpp Place`, called first from ext.phone_menu's place function): the
  sub-toolbar is laid out against `width * k` so retail's own centring (which uses the global width)
  centres it on its button, then clamped into the bar region; a tooltip of a toolbar button is moved
  from the toolbar's units to the global ones; the event notices (retail slides them in left of the
  toolbar, under the cluster) sit above the bar's left end. The right-hand strip above the bar stays
  free for touch_pointer's action bar.
* **Enhanced tab**: "Touch controls" Off / Toolbar 100 % .. 200 % (`enhanced_entry.cpp`,
  `SetTouchModeAndRecord`).

## Where

| file | change |
|---|---|
| `src/ext/touch_mode/touch_math.{h,cpp}` | the pure decisions (unit-tested) |
| `src/ext/touch_mode/touch_mode.{h,cpp}` | the cluster dialog, the scale, the place rules, the camera hold |
| `src/ext/touch_mode/enhanced_entry.cpp` | the Enhanced combo |
| `src/ext/ui/Dialog/TouchControls.rsc`, `EnhancedOptions.rsc`, `ExtensionsSprites.rsc` | the cluster, the combo row, the icon |
| `src/engine/ui/UserInterface.h`, `userinterface_frame.cpp`, `userinterface_draw.cpp`, `userinterface.cpp` | the seam |
| `src/game/Game/camera_input.cpp` | the camera seam |
| `src/ext/phone_menu/phone_menu.cpp` | one chained call (`TouchMode::Place` first) |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp`, `src/ext/resources/resources.cpp`, `CMakeLists.txt` | the row, the value key, `Sync`, the embedded `.rsc` |
| `src/platform/android/app_android.cpp` | the seed lines |
| `tools/ext_icons/touchmode.py`, `Entry_touch_mode.png`, `ext_sprites_data.h` | the row's icon |
| `tests/unit/ext_touch_mode_test.cpp` (+ registry / rsc tests) | tests |

## Known effects

* The tutorial's glow ring around a toolbar button is placed from the button's rectangle in the
  toolbar's own units, so with the row on and k > 1 it is drawn at the unscaled place (tutorial only).
* The text of a scaled dialog (the toolbar's speed label) is drawn with retail's filtered quads, not
  ext.sharp_ui's rasterised variants.
* The cluster's box takes the pointer from the world, including the small empty corner above the zoom
  buttons and the gaps between controls.
