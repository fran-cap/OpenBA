# Android P11b: the owner's rulings on ext.touch_pointer and ext.touch_mode

NOT RETAIL. The rulings of 2026-10-26 (`docs/runtime_port/NEXT_SESSION.md` §2.10 "RULED"), after the owner's real-finger
test of the P11 apk, plus two rulings sent during the session (§1 Flip, §5). Lane `p11bpointer`, branch based on
`android/p11` (c3093d522). Screenshots: session scratchpad `p11bpointer/` (named per step below).

## 1. Action-bar glyphs (ext.touch_pointer)

- **Rotate left / right** use the curved arrows of the Undo / Redo buttons of ext.terrain_edit's toolbar page
  (`ExtensionsSprites.rsc`: `ExtensionsGlyph_terrain_undo` / `_redo`, our own sprites drawn by
  `tools/ext_icons/terrain_tools.py`; referenced by name, no new art, the sheet unchanged): undo arrow + house,
  house + redo arrow (`Dialog/TouchPointer.rsc`).
- **Change model** first got the undo + redo pair; the owner looked at that build and RULED it confusing (a
  counter-clockwise AND a clockwise arrow in one button): it is BACK to P11's two small arrows (`ArrowLeftSmall` /
  `ArrowRightSmall`). Rotate keeps the new arrows (ruled good).
- **Spacing.** P11 sized every glyph alone to half the button high, so the arrow + house row was wider than the button
  and the house ran over its edge. `BarDialog::SizeGlyphs` (touch_pointer.cpp) now sizes the one or two glyphs of a
  button together: one height for both, the largest up to half the button whose row (widths plus the ribbon's 2 px
  pad) fits `kPairFraction` 0.72 of the button's width; the ribbon is centred by its MidCenter alignment. Every bar
  button checked on the S25: `bar1_crop.png` (Rotate with the new arrows), `bar2_crop.png` (the final look: check,
  undo + house, house + redo, `< >`, cross; each row centred inside its button, none touching an edge).

## 2. Press-and-hold tooltip (ext.phone_menu)

- Engine seam `UI::UserInterface::_extHoverWhileDown(const DialogInstance*)` (UserInterface.h; null by default and
  under the faithful profile, DESIGN.md hook rule 5). `DialogInstance::Update` counts hover time when no button is
  down (retail) OR the seam answers true.
- `Ext::PhoneMenu::HoverWhileFingerDown` (`src/ext/phone_menu/touch_hold.{h,cpp}`, its own file; installed by
  phone_menu.cpp `Sync`): true with the row on, the pointer last moved by a touch (`Platform::Input::PointerIsTouch`),
  the left button down and right / middle up. A real mouse keeps retail's rule (a mouse press still ends the hover).
  The lift hides the tooltip through P11's `FingerUpHidesToolTip`, unchanged.
- A finger trembles, and retail counts hover time only while the cursor stands still: ext.touch_pointer now keeps the
  cursor on a dialog press's point until the finger leaves a slop of `kHoldSlopFraction` 0.012 of the short edge
  (13 px on the S25; touch_pointer_math.h `LeftHoldSlop`), then follows it for the rest of the press (a slider drag
  starts after those 13 px). A release inside the slop is at the press point.
- S25: a press held 3 s on the Wooden House button of the Housing sub-toolbar shows its tooltip with the building costs
  (`h2_hold.png`: "Wooden House [1]", x16 / x8); the lift hides it and selects the tool (`h2_after.png`). The same on
  the pause menu's Extensions button (`h1_hold.png`) and on the toolbar's speed button (`g5_during.png`).

## 3. "Touch pointer" combo on the Enhanced tab

- One row (`EnhancedOptions.rsc`: `textTouchPointer`, `comboTouchPointer`, string table `touchPointerItems`; the table
  is 12 rows now) and one contribution (`src/ext/touch_pointer/enhanced_entry.cpp`, listed in enhanced_tab.{h,cpp}).
- Items: **Off** (the row ext.touch_pointer off, its keys kept), then the presets of touch_pointer_math.h
  `kTouchPresets`, which set `ext.touch_pointer_lift` / `_pick_radius` / `_tap_twice` together and turn the row on:

  | Item | lift (device px) | pick radius | tap twice |
  |---|---|---|---|
  | Default | 160 | 50 | off |
  | Close aim | 120 | 25 | off |
  | Far aim | 240 | 75 | off |
  | Tap twice | 160 | 50 | on |

  and **Custom**: shown when settings.cfg holds values no preset matches; choosing it turns the row on and keeps the
  values. Every preset value is a step the registry key accepts (unit test `ext_touch_pointer_presets`), so a preset
  never reads back as Custom. The pick radius steps are 0 / 25 / 50 / 75 / 100, so the task's example 40 / 70 became
  25 / 75.
- The label is **"Touch pointer"**, not "Touch controls": that caption is already ext.touch_mode's row (the toolbar
  scale combo) on the same tab. Ruling owed below.
- Row height: a combo row like its neighbours (`Options.rsc:comboAutosave`, `_minWidth` 180, `_bottomPad` 3); the
  touch-size pass sizes it as every other combo of the tab. The Enhanced tab's list scrolls (P11's fit).
- S25: the row reads "Default" (`m3_a.png`), the drop-down lists the six items (`m4_a.png`); Far aim + Ok logged
  `Apply: item=3 -> row 1 lift 240 radius 75 tap twice 0`, a new start reopened it on Far aim (`combo ... item=3`),
  and Default + Ok put the owner's values back (`lift 160 radius 50 tap twice 0`).
- Tests: `ext_touch_pointer_presets`; `ext_rsc_text_parse_enhanced_and_strings` (23 table items, 6 strings).

## 4. Smooth D-pad pan (ext.touch_mode)

- touch_math.h `SpeedRamp`: a linear level 0..1 up over `kRampInSeconds` 0.30 s while held, down over
  `kRampOutSeconds` 0.20 s after the release, eased by a smoothstep; frame-rate independent (unit test
  `ext_touch_mode_speed_ramp`). The pad's pan and the rotate buttons each have one; the last direction is kept so the
  camera coasts out of it. Zoom keeps its steps (retail's zoom target is smoothed already).
- The pan's gain reaches retail's pan through a new `panGain` out-parameter of `TouchMode::CameraInput`; camera_input.cpp
  multiplies retail's pan speed by it (1.0 when the seam does not run, and for keys). Keys and the mouse are unchanged.
- S25 (uinput holds on the pad's right arrow): a 2.5 s hold moved the target 18.5 -> 53.0, then coasted 1.4 more after
  the release (53.0 -> 54.4); a 0.58 s press moved it 6.7 (about 2 units less than the unramped speed would).
  The constants are a first tune on synthetic input: the feel is the owner's (ruling owed).

## 5. A finger on the toolbar keeps the placement ghost (ext.touch_pointer)

Ruling (sent during the session): with a placement tool open, a tap on a toolbar button made the ghost flicker for
about a second; RULED: the ghost stays solid at its last aimed spot while the finger is on the toolbar / a dialog,
and goes only with Place / Cancel or another tool.

- **Cause.** A finger on a dialog moves the game's cursor to the finger (the dialog needs it for the click). Retail
  then hides the ghost: `GameUpdateLoop::Update` passes `_mouseCaptured == 0` (false) to `PlayerToolManager::Update`,
  `PlacementTool::Update(param3 0)` removes the ghost mesh, `OnKey(0)` closes the cost bar, and `GameUpdateLoop::Draw`
  passes the "UI holds the mouse" flag to `Tool::Draw`, which skips the footprint tiles. On the lift the cursor went
  back to the world after three held polls and everything came back: the blink.
- **Fix.** From the press on a dialog (with a placement or drag tool aiming) until the poll that shows the cursor back
  on the world, ext.touch_pointer holds the tool's aim (`g_uiHold`):
  `PlayerToolManager::Update` skips the tool's hotkeys (no `OnKey(0)`, no click of the dialog press reaches the tool)
  and updates it as if no dialog held the mouse (`HoldToolAim`); `GameUpdateLoop::Draw` and render_pipe's main pass
  give `Tool::Draw` retail's "free" flag (`ToolAimHeld`); and the tools' cursor ray (`PlayerRay_GetMouseRay_1005a0`)
  reads the last world aim instead of the cursor (`ToolCursor`, atomics: the draw may run on the render thread). A
  different current tool (the tap chose another building) ends the hold at once; retail's `SelectTool` clears the
  old ghost. Every seam is behind `Ext().touch_pointer` (off on a desktop, off under the faithful profile).
- First try (skipping the tool's update only) was not enough: the draw flag hid the tiles and the ghost mesh still
  went (`g3_during.png`); with the draw flag alone the tiles followed the cursor onto the toolbar (`g4_during.png`).
  Hence the ray seam.
- S25, the exact sequence (Housing, Wooden House, aim, then a press on the leftmost speed button): the ghost and its
  footprint stay where they were before (`g5_before`), during the press (`g5_during`), at the lift (`g5_lift`) and
  after the speed sub-toolbar opened (`g5_after`). Short taps: the hold lasts 7-9 frames (`tool aim held 7 frame(s)`).
  Then Housing > Stone House: the tool switches and the stone ghost takes the aimed spot (`g6_t3.png`).
- Windows: debug build, `tools/build.sh run` (faithful profile, autostart) to a running town, empty crash log; the
  row is off on a desktop, so the mouse path is retail's by construction (every seam tests `Ext().touch_pointer`).

## 6. Verification

- Windows DX11 debug build clean; `tools/build.sh unit`: 1540 tests, 0 failures, 1 skipped (the kit cross-check, no
  `build/ext_resources` in this worktree; no sprite was added, the sheet is unchanged).
- Android debug apk built (`tools/android.sh build debug --jobs 2 --ccache`), every item checked on the S25 under the
  phone lock with uinput input (`tools/android_multitouch.py`); no real finger.
- Linux / macOS not built here: the seams are in shared code (engine, game, ext); `touch_hold.cpp` uses only
  `Platform::Input::PointerIsTouch`, already on every platform.

## 7. Rulings owed

- **The combo's caption**: "Touch pointer" (the task said "Touch controls", which is ext.touch_mode's row on the same
  tab). Alternatives: rename ext.touch_mode's row to "Touch toolbar" and give this one "Touch controls".
- **The presets** (Close aim 120 / 25, Far aim 240 / 75) and whether Off belongs in this combo.
- **The ramp constants** (in 0.30 s, out 0.20 s, smoothstep) and whether the coast after the release is wanted;
  rotate ramps too, zoom does not.
- **The hold slop** (0.012 of the short edge): a slider drag now starts after 13 px.
- **The kept ghost while a dialog is pressed** also skips the tool's hotkeys for that press (a keyboard rotate pressed
  in the same 0.1 s is lost); judged harmless.

## 8. Files

`src/ext/ui/Dialog/TouchPointer.rsc`, `src/ext/touch_pointer/touch_pointer.{h,cpp}`, `touch_pointer_math.{h,cpp}`,
`enhanced_entry.cpp` (new); `src/engine/ui/UserInterface.h`, `userinterface_frame.cpp`, `dialog.cpp`;
`src/ext/phone_menu/touch_hold.{h,cpp}` (new), `phone_menu.cpp`; `src/ext/ui/Dialog/EnhancedOptions.rsc`,
`src/ext/enhanced_tab/enhanced_tab.{h,cpp}`; `src/ext/touch_mode/touch_math.{h,cpp}`, `touch_mode.{h,cpp}`,
`src/game/Game/camera_input.cpp`; `src/game/Dialog/entityui.cpp`, `src/game/Game/gameupdateloop.cpp`,
`src/game/Player/tool_base_ray.cpp`, `src/ext/render_pipe/render_pipe.cpp`; tests `ext_touch_pointer_test.cpp`,
`ext_touch_mode_test.cpp`, `ext_rsc_text_test.cpp`; docs `DESIGN.md` (finger input row), `TOUCH_MODE.md` §4.
