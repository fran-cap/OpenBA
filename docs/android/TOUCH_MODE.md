# Android P10: touch mode (`ext.touch_mode`) -- design

NOT RETAIL. The owner's ruling of 2026-10-26: a phone layout for playing with fingers. In a landscape
phone (Galaxy S25, 2340x1080) the game view sits on top. The bottom-left holds a virtual D-pad (camera pan)
with ROTATE LEFT / ROTATE RIGHT above it and ZOOM IN / ZOOM OUT to its right. The in-game toolbar runs as
one bar along the bottom, to the right of that cluster, at its own scale: about 2x at render scale Full and
UI 40 on the S25. Panels, dialogs and tooltips keep the existing UI scale. Sketch: session scratchpad
`android-touch/mobile_layout_sketch.png`. Phase 1 is this design only; nothing here is built yet.

**Rulings (owner, 2026-10-26).** (1) The per-dialog scale seam in the engine (§2: the null-by-default
`UI::UserInterface::_extDialogScale`, null in the faithful profile) is RULED; the enlarge-in-place
alternative is dropped. (2) The toolbar scale is capped at the requested value, then SHRUNK TO FIT beside
the control cluster (about 1.48x on the S25, §3): RULED; the stacked full-width 2x layout is dropped.
(3) The right-hand strip above the bottom bar is reserved for the later `touch_pointer` action bar, and
the camera hooks stay callable from it (§4, §8).

## 1. Retail mechanisms this hooks

| What | Where | Fact the design relies on |
|---|---|---|
| UI unit space | `UI::UserInterface::_uiScale` (`UserInterface.h`, `SetUIScale`) | `_uiScale` = percent / 100 = layout units per back-buffer pixel. One value for every dialog. The layout screen is `backBuffer * _uiScale` (`Update`, `userinterface_frame.cpp`). |
| Layout | `UserInterface::Update` -> `DialogInstance::ComputeLayout(x, y, right, bottom)` (0x180113d90) for each dialog | The rectangle goes through `_extDialogPlace` (ext.phone_menu's seam: `kExtPlaceLayout`, `kExtPlaceAfterLayout`). |
| Hit testing | `UserInterface::Update`: `st.mouseX/Y = Input::Device mouse * scale`, then `DialogInstance::Update(time, ui, st)` tail to head | Every element hit-tests `st` in the dialog's layout units. A tooltip starts from the same `st` (`element_vtable.cpp`, `BeginToolTip(.., mx, my)`, `SetUpperLeft(x, y - h)`). `_mouseCaptured` comes back in `st` and gates world input (`gameupdateloop.cpp`: `m_toolManager->Update(.., _mouseCaptured == 0)`). |
| Drawing | `UserInterface::Draw` (`userinterface_draw.cpp`): `SpriteContext::Begin(_uiScale, ..)` | Records are stored in layout units, and `Flush` maps them to pixels by `SpriteContext::_scale`. The sharp_ui seam already flushes between dialogs (`_extSharp` origin). |
| The toolbar | `ToolbarDialog` (`src/game/Dialog/toolbardialog.cpp`, `Dialog/Toolbar.rsc`), opened in `GameUpdateLoop::Startup` (`gameupdateloop.cpp`) | Bottom-right, one row of buttons (templates 0..3; the extension button is template 3). A category button opens a CHILD `ToolbarDialog` with the same `Dialog` description. `OnElementEvent` places the child with `SetPadding(0, xPos, 0, radio height + root bottom pad)`, where `xPos = backBufferWidth * _ui->_uiScale - buttonCentre - subWidth / 2`. That formula reads the global unit space. |
| Event notices | `EventNotifyDialog::OnUpdateLayout` (`eventnotifydialog.cpp`, 0x1400680c0) | Parented to the toolbar. Its padding is `toolbarRoot->_layoutWidth - ownWidth + 44 (+40 per extra entry)`, smoothed, so the ribbon sits LEFT of the toolbar on the bottom row. In touch mode that is where the cluster is. |
| Tutorial glow | `TutorialHighlightDialog` (`tutorialhighlightdialog.cpp`) | Places its ring from a toolbar element's layout rectangle in global units. |
| Camera input | `PlayerToolManager::UpdateCamera` (FUN_1400feec0, `src/game/Game/camera_input.cpp`), called every frame from `PlayerToolManager::Update` (`entityui.cpp`) | Locals: `yaw -= dt * kRotSpeed` (90 deg/s) for RotateLeft (key id 9) and `+=` for RotateRight (10). `panX / panY = -1 / +1` for Left / Right / Forward / Back (0xd, 0xe, 0xb, 0xc), assigned after edge scroll (`g_gameSettings->edgeScroll`, `GameEdgeScroll`). `zoomDir = +1 / -1` on a PRESS of ZoomIn / ZoomOut (0xf / 0x10), else the wheel, and each step is `kZoomStep` 5 on the zoom target. Keys run whatever `inputFlag` is (only the wheel is gated). The ext.eye_camera and ext.better_camera seams already sit in this function. |
| Safe area | ext.phone_menu `phone_math.h` `SafeArea` (margin 3.375 % of the screen height since P11c, 4.5 % before) | The cluster and the bar stay inside it. |

## 2. Independent toolbar scale: a per-dialog unit space

**Meaning.** The toolbar and its sub-toolbars get their own unit space: `toolbar units = global units / k`.
Everything inside them is laid out, hit-tested and drawn as if the UI scale were `_uiScale / k`: sprites,
nine-sliced button frames, border thickness, padding, the speed text and hit rectangles all grow by k
together. Panels, dialogs and tooltips stay at `_uiScale`. `k` is relative to the UI scale, so when the
other agent's render-scale coupling re-derives the UI scale at Half (the interface keeps its on-screen
size), the bar keeps its on-screen size as well. Nothing in this row reads the render scale.

**Engine seam** (DESIGN.md hook rule 5; `src/engine` never includes `src/ext`). One new static pointer
`UI::UserInterface::_extDialogScale`, `float (*)(const DialogInstance*)`, null by default and always null
under the faithful profile. A null pointer, or an answer of 1.0, is retail's path. It has three call points:
1. `Update`, layout loop: `ComputeLayout` gets the placed rectangle divided by k.
2. `Update`, pump loop: for a dialog with k != 1, the dialog gets a copy of `st` with `mouseX / mouseY /
   dragDx / dragDy` divided by k. The fields the callee writes back (`leftDown`, `mouseCaptured`, the drag
   state) are copied into the shared `st`, and the mouse fields are restored.
3. `Draw`, per dialog: when k != 1, pending quads are flushed, `SpriteContext::_scale = _uiScale / k` is set
   for the dialog, and the old value comes back after the dialog's own flush. For that dialog only,
   `_extSharp` is null: its text is drawn with retail's filtered quads, as sharp_ui already does below
   75 %. No edits to src/ext/sharp_ui, which is another agent's area.

`GetFirstContainedDialog` (only the unwired `EntityUI::UpdateInput` calls it) gets the same per-dialog
point division so it stays consistent.

**Which dialogs.** The `Dialog` description equals the toolbar's, `Dialog/Toolbar.rsc`, recorded at
`GameUpdateLoop::Startup` through one `// EXT SEAM` line, `Ext::TouchMode::ToolbarOpened(toolbar)`. The
sub-toolbars share that description, so they follow. The cluster dialog is ours (§3) and uses the same k.

**Fit.** `k = min(requested, (barRegionWidth in global units) / (toolbar root width in toolbar units))`,
then floored at 1.0. The toolbar's width in its own units does not depend on k, so the fit cannot
oscillate. It changes only when buttons change (mods, the extension button). One log line is written per
change: `[TouchMode] toolbar k=<k> (requested <r>, fit <f>) bar <w>x<h> px`.

**Compensations** (retail code that mixes the two unit spaces). Each one lives in touch_mode's place hook
and touches no game code:
- *Sub-toolbar.* Retail's `xPos` uses the global screen width. Laying the child out with its rectangle's
  right edge at `W_global` in toolbar units makes retail's own formula centre the child on its button
  exactly. The derivation: left edge = `R - subW - xPos = R - W_g + centre - subW/2`, which is the centre
  only when `R = W_g`. `kExtPlaceAfterLayout` then clamps the child horizontally into the bar region, so a
  wide group does not run over the cluster or off the right edge.
- *Tooltip of a toolbar button.* Its upper-left was set in toolbar units. The `kExtPlaceLayout` arm for the
  tooltip dialog (`ui->_toolTipOwner` has k != 1) maps it back to global units:
  `x = ulX * k`, `y = (ulY + h) * k - h`.
- *Event notices.* Moved by `kExtPlaceAfterLayout` (`Element::AdjustUpperLeft`) to sit above the bar's right
  end, so the ribbon grows leftwards along the bar's top edge with retail's slide kept as an offset. The
  exact offset is settled in phase 2 from the laid-out rectangles (a one-time `[TouchMode]` print).
- *Tutorial glow ring.* It would be drawn at the unscaled place. Phase 2 maps it in the same hook if the
  ring's dialog can be identified cheaply; otherwise it becomes a known_port_issues row (tutorial plus
  touch mode only).

**Dropped (ruling 1).** Reshaping the toolbar instance in one unit space (min sizes, padding and
`ImageElement::SetWidthHeight` times k, as ext.phone_menu reshapes the menu buttons) needs no engine seam,
but text and frame borders would not scale and every element type would need its own rule.

## 3. The control cluster: layout in the safe area

**What it is.** Our own dialog `src/ext/ui/Dialog/TouchControls.rsc` (root `pageTouchControls`), served by
the resource providers like the Extensions dialog. Its buttons inherit from the toolbar's button
descriptions (`"Toolbar.rsc:<template>"`, the brown retail button art from the user's install) and their
nine-sliced frames draw at any size.
- D-pad arrows: an existing arrow sprite from the installed sheets, turned 0 / 90 / 180 / 270 with
  `ImageElement::_rotation`. Phase 2 picks one, in order: the tutorial arrow (`TutorialHighlight`'s arrow
  image), then a scroll-bar arrow. If none fits, the fallback is the game font's `^ v < >`.
- Rotate and zoom labels: the game's own Input-tab key-binding strings ("Rotate Left", "Rotate Right",
  "Zoom In", "Zoom Out"; localised, retail data). If they do not fit, the fallback is game-font text
  `<` `>` `+` `-`.

No new art enters the tree. The only new PNG is the Extensions-row icon that every row has (§5).

**Sizes.** `S` is the short edge in device pixels, `m` the safe margin (ext.phone_menu's 3.375 % of the
height when that row is on; 2 % of `S` without it, the desktop test case).

| Item | Rule | S25 (2340x1080, S = 1080, m = 49) |
|---|---|---|
| cell `c` | 0.11 S (about 7.3 mm at the S25's ~16.4 px/mm) | 119 px |
| gap `g` | 0.02 S | 22 px |
| D-pad | square 3c, left = m, bottom = H - m | x 49..406, y 674..1031 (357 px, 0.33 S) |
| Rotate L / R | each (3c - g) / 2 wide, 0.8 c tall, a gap g above the D-pad | x 49..216 and 238..406, y 557..652 |
| Zoom in / out | 1.4 c wide, (3c - g) / 2 tall each, a gap g right of the D-pad, aligned with it | x 428..595; y 674..841 and 863..1031 |
| Cluster box | | x 49..595, y 557..1031: 546 x 474 px (0.51 S x 0.44 S) |
| Bar region | left = cluster right + 2g, right = W - m, bottom = H - m | x 639..2291: 1652 px |

The device pixels go to cluster units with `units = px * (backBuffer / device) * _uiScale / k`. The
cluster is placed through `kExtPlaceLayout` like any dialog. Its sizes are re-applied as `_minWidth /
_minHeight` whenever the screen, `_uiScale` or k changes, so it keeps its physical size at any UI scale or
render scale.

**The bar at the ruled settings.** On the P8 screenshot (UI 42, render scale Full) the toolbar measures
about 447 x 51 units: 1065 x 120 device px, 10 buttons. At UI 40 that is 1118 x 128 px, so 2x would be
2236 x 256 px. That is the whole safe width (2242 px), and it does not fit beside the cluster. The fit
gives `k = 1652 / 1118 = 1.48`: a 1652 x 189 px bar with buttons about 140 px (8.5 mm) square. The owner's
sketch draws the bar at about 63 % of the width, roughly 1.3x. **RULED (2026-10-26):** k is the
requested scale, shrunk to fit beside the cluster; the stacked full-width 2x layout is dropped. Phase 2 confirms the 447 units from the log.

**Sub-toolbars** open above the bar at the same k, clamped into the bar region (§2). The cluster's top is at
y = 557, and a sub-toolbar sits at about y 650..840. They overlap only if a group is wider than the bar
region, which is a modded case: it then aligns to the region's left edge.

## 4. Input flow

Touches arrive as left-button mouse events (SDL). The cluster is a UI dialog, so a touch on it sets
`_mouseCaptured` and the world never sees that click (no building placed under the D-pad). The camera
seam is one `// EXT SEAM` call in `UpdateCamera`, after `zoomDir` is set and before the eye_camera zoom
seam:
```cpp
if (Ext::Ext().touch_mode || Ext::TouchMode::Shown())
    Ext::TouchMode::CameraInput(dt, yaw, panX, panY, zoomDir);   // the locals the keys write
```
- **Which control is held.** The control is held while the left button is down, the press began inside
  the cluster (`UserInterface::_mouseDownX/Y`), and the pointer is over a control. Our own pure math
  (`touch_math.h`) does the hit test against the laid-out rectangles. It does not rely on the elements'
  click events, which fire on release.
- **D-pad**, analogue (P11c, ruled 2026-10-26; it was 8-way until P11b). touch_math.h `PadVector`: the pan
  direction is the finger's offset from the pad's centre at any angle (a unit vector), zero inside a dead zone of
  radius `kDeadZoneFraction` 0.10 of the side, full speed outside it (`kPadFull`; `kPadProportional`, the speed
  growing to the inscribed circle, is the tested alternative). The rectangular graphic is unchanged. The press
  captures the pad and the direction follows the finger until the release, also off the pad. The seam hands
  `UpdateCamera` the unit direction in `panX` / `panY` and the speed in `panGain`; retail normalises the vector,
  so a diagonal pans no faster than a cardinal. Releasing stops the pan. P11b (ruled 2026-10-26,
  `docs/android/P11B_POINTER.md` §4): the pan
  speed RAMPS (touch_math.h `SpeedRamp`: up over `kRampInSeconds`, down over `kRampOutSeconds` after the
  release, smoothstep-eased; the camera coasts out of the last direction). P11b tuned 0.30 / 0.20 s; P11c (owner,
  2026-10-26: too laggy, about 100 ms) 0.10 s in, 0.09 s out. The gain
  reaches retail's pan through `CameraInput`'s `panGain` (camera_input.cpp multiplies retail's speed by
  it; 1 without the seam). A key panning keeps retail's speed.
- **Rotate**: `yaw -= / += dt * 90` every frame while held, retail's key rate. P11b ramped it like the pan;
  P11c (owner, 2026-10-26: too laggy) took the ramp out: full speed on the press, stop on the release.
- **Zoom**: continuous (P11c, ruled 2026-10-26: smooth, zero delay, no steps; until P11b one step at the press,
  then one every 0.15 s after 0.35 s). While a zoom button is held, `CameraInput` returns `heldZoom` =
  `kZoomStepsPerSecond` 8 of retail's 5-unit steps per second (40 units/s) times `dt`, from the first frame to the
  release, no ramp. `UpdateCamera` applies it after retail's own step with touch_math.h `ApplyHeldZoom`: the zoom
  target (+0x3C) and the smoothed distance move together by what the target moved inside the frame's range
  (retail's 20 .. 130, or ext.better_camera's 6 .. 200 with that row on), so retail's 0.9 / 0.1 smoothing adds
  no lag at the press or the release. With ext.eye_camera following a citizen the same steps move the eye's
  distance (`EyeCamera::ZoomBy`). The wheel and the zoom keys keep their steps.
- **Keyboard and mouse still work.** The seam only assigns while a control is held. Keys, edge scroll, the
  wheel and the middle-button rotate drag (the KVM mouse) are unchanged, and a click anywhere outside the
  cluster is retail's.
- **Shown.** The cluster opens with the toolbar (`ToolbarOpened`) while the row is on. It closes when the
  row goes off (the next `CameraInput` call sees `Shown()` and closes it: live, class A), and it hides while
  the toolbar root is hidden (ToggleAll). It is not opened in the front end.
- One pointer only: SDL's mouse emulation carries one touch, so pan and zoom cannot be held together. The
  follow-ups (§8) record this.
- **Callable from `touch_pointer`** (§8). The camera hold is a source-tagged input, not private state:
  `struct CameraHold { int8_t panX, panY; int8_t rotate; int8_t zoom; float zoomSteps; }` and
  `Ext::TouchMode::Hold(Source, const CameraHold&)` with `Source::Pad` (this row's cluster) and
  `Source::Fingers` (the later extension: two-finger pan, pinch as `zoomSteps`, two-finger spin as
  `rotate`). `CameraInput` applies the hold of the source that changed last (last device wins); a real
  mouse or key input is never overridden, it simply keeps working through retail's lines. The hold API
  works with this row's cluster hidden, so `touch_pointer` does not depend on the D-pad being shown.
- **Reserved strip.** Nothing of this row is placed in the right-hand strip above the bar, where
  `touch_pointer`'s action bar (Place / Rotate / Flip / Cancel) will sit. `Ext::TouchMode::BarRect()` and
  `SubToolbarRect()` (global units) are exported so that bar can sit above whichever is higher.

## 5. Settings rows

- `ext.touch_mode`: class A, live, default OFF. Registry row in `src/ext/registry.cpp` after `fps_cap`,
  `Extensions::touch_mode`. Display name "Touch controls". Description: "A camera pad and zoom / rotate
  buttons at the bottom left, and the toolbar enlarged for fingers along the bottom. For phones and
  touch screens." Icon `src/ext/ui/Dialog/Sprite/Extensions/Entry_touch_mode.png` drawn by a new
  `tools/ext_icons/touchmode.py`, the sheet rebaked (`tools/ext_sprites_bake.py`), and
  `tools/ext_resources.sh` re-run so the kit cross-check unit test passes (the stale-kit trap).
- `ext.touch_mode_toolbar_scale`: a value key in `kValueKeys`, owner `touch_mode`, discrete
  100 / 125 / 150 / 175 / 200, default 200 (the ruled "about 2x", fitted per §3).
- `ext.touch_mode_zoom_min` (P11d, `docs/android/P11D_ZOOM.md`): a range value key, owner `touch_mode`, 6 .. 20,
  default 8: the camera's near zoom limit while the row is on (retail's is 20), for every zoom input (the zoom
  buttons, the pinch, keys, the wheel); `touch_math.h` `NearZoomLimit`. settings.cfg only, no Enhanced-tab entry.
- **Enhanced tab** (`src/ext/touch_mode/enhanced_entry.cpp`, `Ext::EnhancedTab::TouchModeEntry()`), one
  combo built like Frame rate cap: "Touch controls": Off / Toolbar 100 % / 125 % / 150 % / 175 % / 200 %.
  Picking a size sets the value key and turns the row on. Labels are new keys in
  `ExtensionsStringTable.rsc`.
- **Android seed** (`app_android.cpp` first-run lines, appended to an existing file through
  `phone_seed.h`): `ext.touch_mode=1`, `ext.touch_mode_toolbar_scale=200`.
- No `BANISHED_*` flag, no `getenv`.

## 6. Files (phase 2)

| File | Change |
|---|---|
| `src/ext/touch_mode/touch_math.{h,cpp}` | Pure math: `ClusterLayout`, `ToolbarFit`, `DpadDirection`, `ZoomRepeat`, `ToGlobal / ToDialog`, `SubToolbarRight`, `TooltipUpperLeft` |
| `src/ext/touch_mode/touch_mode.{h,cpp}` | `ToolbarOpened`, `Shown`, `CameraInput`, `Place` (called from phone_menu's place function, one chained line), `Scale` (the engine seam's function), the cluster dialog |
| `src/ext/touch_mode/enhanced_entry.cpp` | The Enhanced combo |
| `src/ext/ui/Dialog/TouchControls.rsc`, `ExtensionsStringTable.rsc` | The cluster dialog, the labels |
| `src/engine/ui/UserInterface.h`, `userinterface_frame.cpp`, `userinterface_draw.cpp`, `userinterface.cpp` | `_extDialogScale` and its call points (§2) |
| `src/game/Game/camera_input.cpp`, `gameupdateloop.cpp` | Two `// EXT SEAM` lines |
| `src/ext/phone_menu/phone_menu.cpp` | One chained call to `TouchMode::Place`. This is another agent's file: the line is announced and merged after theirs |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp`, `src/platform/android/app_android.cpp` | Row, value key, `Sync` installs `_extDialogScale`, seed |
| `tests/unit/ext_touch_mode_test.cpp`, `ext_registry_test.cpp`, `sources.cmake` | Tests |
| `docs/extensions/handins/TOUCHMODE.md`, `LIST.MD`, `docs/android/README.md` | Hand-in, list row, phone defaults |

## 7. Tests and verification

**Unit** (`ext_touch_mode_test.cpp`, cases `ext_touch_mode_<what>`):
- `cluster_layout_s25`: the table in §3 to the pixel at 2340x1080. The same fractions hold at 1920x1080
  and 3080x1421, and the cluster stays inside the safe area.
- `cluster_layout_render_scale_invariant`: device sizes are equal at render scale 100 / 50 / 25 with the
  UI scale re-derived.
- `toolbar_fit`: k = min(requested, fit) and >= 1. At UI 40 / 447 units / S25 the answer is 1.48. The
  result is stable when fed its own output.
- `dpad_direction`: 8 sectors, the dead zone, outside the pad means none, and the sector boundaries
  (22.5 deg).
- `zoom_repeat`: a step at the press, none before 0.35 s, then every 0.15 s, none after release, at
  irregular frame times.
- `unit_mapping`: `ToDialog(ToGlobal(x)) == x`; the sub-toolbar right edge makes retail's xPos formula
  centre the child (the formula re-run in the test); the tooltip upper-left mapping.
- Registry: the row is class A, default off, and has its key and description; the value key's discrete
  set, default and clamping.

**Windows** (functional dir, `tools/game_dirs.sh env p10touch functional`, debug exe copied there, never
`deploy` / `run`). A windowed run at the S25 aspect (the 3080x1421 window P8 used) with `ext.phone_menu=1`,
`ext.touch_mode=1` and UI 40, started with `BANISHED_AUTOSTART=1`:
- Screenshot: the cluster and the bar match §3 within a few px (scaled by 1421 / 1080), and the
  `[TouchMode]` k line matches.
- A held press on the D-pad and on rotate / zoom. The existing mouse-inject harness is used only if its
  flags cover a held press; otherwise the check is by hand on the phone. The camera's target, yaw and
  distance before and after come from the log, through one `[TouchMode] held <control> <seconds>` line
  per release.
- A sub-toolbar opens centred on its button inside the bar region; a toolbar tooltip appears at the
  finger; the row turned off live removes the cluster and gives retail's toolbar back.
- Row off: the toolbar is exactly as on main (layout rectangles logged equal).

**S25** (phone lock; `tools/android.sh build debug --jobs 2 / install / run`):
- Screencaps at render scale Full / UI 40 and at Half.
- `adb shell input swipe x y x y 1500` on each D-pad arm, rotate and zoom (a 1.5 s held touch), with
  screencaps before and after.
- A 200 ms `input swipe` on one spot over a category button (an `input tap` is shorter than a frame and
  the polled mouse misses it, `docs/reference/android_mouse.md`): the sub-toolbar opens.
- A tap in the game view still selects and places.
- A real mouse middle-drag still rotates.
- Build / unit counts recorded; the DX11 gate and resave stay the coordinator's.

## 8. Follow-ups (the owner's thoughts, NOT ruled; not built)

- **`touch_pointer` (a later class A extension, not this row's work;
  `docs/android/TOUCH_PLACEMENT_IDEAS.md`).** It reads SDL finger events. One finger aims while a tool is
  active, two fingers pan, a pinch zooms, a two-finger spin rotates. Between the finger, the KVM mouse and
  this row's D-pad, the last device wins. Its action bar sits on the RIGHT side above the bottom toolbar.
  This row keeps that strip free (§4 "Reserved strip"), exports `BarRect` / `SubToolbarRect`, and its
  camera hold (`Hold(Source::Fingers, ...)`, §4) is the entry point that extension calls for pan, zoom
  and rotate. The ideas below that it covers (place-then-confirm, pick radius, multi-touch) are tracked
  there.

- **Place-then-confirm** for buildings and roads: the ghost follows the finger, and an on-screen tick /
  cross confirms or cancels. Hooks: the build and road tools' `OnKey` / `Update` (`src/game/Player/`),
  retail's tool bubbles (`tool_base_createbar.cpp`).
- **Larger pick area, or pick-nearest,** for citizens and buildings: a pick radius in device mm around the
  touch, over EditTool's pick (`edittool.cpp`).
- Multi-touch (pan and zoom together, pinch zoom, two-finger rotate): SDL finger events beside the mouse
  emulation, behind the platform twins.
- A cluster on the right for left-handed players, a mirrored layout (a value key).
- The cluster's own size as a value key if 0.11 S proves wrong in the hand.

## 9. Session log (phase 2, 2026-10-26, lane p10touch)

Branch `worktree-agent-a60153362df7a69d4` on main `14415217e`. Built with `--jobs 2`; Windows DX11 debug and the
Android debug apk (Vulkan).

**As built: what differs from §1-§7.**
- **Two later owner rulings (2026-10-26).** (1) The capture rule (`touch_math.h Capture`). A press that starts on a
  control owns the pointer until it is released. While the finger is down that control, and only that control, acts
  wherever the finger moves, so sliding over Rotate, Zoom, the toolbar or the world triggers nothing else. A press
  that starts anywhere else captures nothing. A captured D-pad keeps panning from the finger's direction relative to
  the pad's centre, even off the pad (the dead zone still applies near the centre). The input no longer reads
  `_mouseDownX/Y`. (2) One frame around the group, the toolbar's own: `SharedElements.rsc:dialogBorder` and
  `sunkenBorderPad6`, as in `Toolbar.rsc:toolBar`. The cluster's math has a frame inset of 0.03 S and a gap of
  0.014 S, which match the toolbar's look at k of about 1.5. On the S25 (UI 100 layout = px): pad 81..438 x
  642..999, rotate 81..252 / 267..438 x 532..627, zoom 453..620 x 642..813 / 828..999, frame (the dialog's root)
  49..652 x 500..1031, bar region 682..2291.
- **The toolbar dialog is identified per game.** `Dialog/Toolbar.rsc` is resolved again for each
  `GameUpdateLoop` instance, because a load gives the resource a new address. The first phone build cached the front
  end's copy, and the row never engaged (`phone_row_not_applied_first_build.png`). Touch mode acts only on the game's
  own `UserInterface`: the loading screens have a second UI at another scale.
- **Buttons inherit `Toolbar.rsc:buttonToolbar`.** `SharedElements.rsc:raisedButton` is not a compiled object of its
  own in the packages (`[ExtRes] ... the base Dialog/SharedElements.rsc:raisedButton did not load`). Each button is
  `TopLeft` inside a holder group whose padding places it: a group gives every child its whole rectangle. The pad's
  tooltip is switched off (`kElementFlagToolTipValid`). Rotate and zoom keep the Input tab's names ("Rotate View
  Left", "Zoom View In").
- **Glyphs.** Rotate buttons show `ArrowLeft` / `ArrowRight` beside the toolbar's `ToolCamera` icon (no rotate sprite
  exists). Zoom shows `Plus` / `Minus`. The pad shows the four `Arrow*` sprites at 0.75 of a cell.
- **Event notices.** Retail keeps the ribbon hidden behind the toolbar while it has nothing to show, and slides it
  out left by the root's right padding. The same padding now lifts it from behind the bar's left end to just above
  it (fully out at retail's 44-unit base), so the right-hand strip stays free.
- **The cluster's root sits at the frame's corner.** It takes the pointer from the world over the frame only (a
  dialog hit-tests its root's rectangle), not over the whole left of the screen.
- **Not built.** The tutorial glow-ring mapping (a known effect, `docs/extensions/handins/TOUCHMODE.md`).

**Verification.**
- **Unit** (`tools/build.sh unit --jobs 2`): 1507 tests, 0 failures, 1 skipped (the kit cross-check: this worktree has
  no `build/ext_resources`). New cases:
  - `ext_touch_mode_*` (cluster on the S25, other screens, render-scale invariance, toolbar fit, hit test, pad
    sectors, capture, zoom repeat, unit mappings, registry row and scale key);
  - `ext_rsc_text_parse_touch_controls`;
  - counts updated in `ext_values_table_shape` and `ext_rsc_text_*`.
- **S25** (debug apk; the owner's settings: UI 42, render scale Full, phone_menu on; the seed appended
  `ext.touch_mode=1`, `ext.touch_mode_toolbar_scale=200`):
  - `[TouchMode] toolbar k=1.489 (requested 2.00, fit 1.489): 454x58 own units` once the frame was in (1.531
    before it).
  - Held touches (`adb shell input swipe x y x y <ms>`):
    - **Pad right** (1.5 s): the target moved 7.4,-25.2 -> 23.0,-11.6.
    - **Rotate left** (1.38 s): yaw 27 -> -99, about 91 deg/s.
    - **Zoom** (the owner's own presses, logged): distance stepped and repeated.
    - **Pad dragged across the whole toolbar** (2.5 s): kept panning, and no toolbar button fired.
    - **A drag from the game view onto the pad**: nothing.
    - **Rotate slid onto Zoom**: kept rotating.
  - A tap on Houses opened its sub-toolbar centred on the button, inside the bar region. The tooltip of a toolbar
    button appeared at the finger.
  - **Render scale Half** (`VideoRenderScale=50`): k 1.269, so the bar keeps its on-screen width. The cluster has
    the same physical size.
- **Windows** (DX11, functional dir, a 2340x1080 window at UI 40, phone_menu + touch_mode on, autostart): k 1.416,
  the cluster in its frame beside the bar, the same layout as the phone.
- **Screenshots** (session scratchpad `android-touch/`):
  - `phone_cluster.png`;
  - `phone_subtoolbar.png`;
  - `phone_tooltip.png`;
  - `phone_final.png`;
  - `phone_capture_slide.png`;
  - `phone_half.png`;
  - `phone_frame.png` (the final look);
  - `win_touch_frame.png`.
- **Not run.** The DX11 gate and the resave check (the coordinator's). The faithful profile (the seam is null there
  by `Sync`). A live row toggle in game. A real finger on the phone, beyond the owner's own taps.

**Trap (phone).** `adb shell sed -i` on the app's `settings.cfg` replaces the file with one the shell owns. The app
cannot read it and writes a fresh default file. The owner's file was restored from a copy, written back into the
app-owned file with `cat copy > settings.cfg`. To edit a key, write an edited copy into the existing file the same
way, never `sed -i` and never `mv`.

**Opens.**
- The tutorial glow ring under touch mode.
- The empty corner above the zoom buttons inside the frame.
- Whether the pad should have a press-state look of its own (retail's button only lights while the finger is on it).
- The Enhanced tab gains an 11th row on a dialog that is already too tall on the phone (P9's tall-dialog work).
- `tools/ext_resources.sh` was not re-run in this worktree: re-run it before the merge for the sheet cross-check.

**Round 2 (owner rulings, 2026-10-26).**
- **The empty corner above the zoom buttons stays** (ruled fine).
- **A held control keeps its pressed look until the lift, even off the button** (`Capture::ShowsPressed`).
  Retail's `ButtonElement::Update` shows the pressed state (state 3, the toolbar's held art) only while the pointer
  is over the button. Each frame, after the UI's update and before the draw, `ClusterDialog::ShowPressed` puts the
  captured button back in state 3, the same steps retail takes on a state change (the state child laid out over the
  button, the label's colour). On the lift the next UI update returns it to retail's state.
- **Tests and checks.** Unit 1507 / 0 / 1 skipped (the capture test now checks the pressed look, held off the pad
  and gone on the lift). On the S25 a 4 s swipe from the pad to x = 900 kept the pad pressed with the finger well
  off it (`phone_pad_held.png`), and the normal art came back after the lift (`phone_pad_released.png`).
