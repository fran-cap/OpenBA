# Android P11d: the camera zooms in to about two houses (lane p11dzoom)

Base `android/p11c` (PR #62). The ruling is `docs/runtime_port/NEXT_SESSION.md` §2.12, "RULED 2026-10-26 (owner,
real-finger test of the P11c apk)": "the camera should zoom IN closer, to about the size of two houses". The camera seam
is the one P11c built (`docs/android/P11C_CAMERA.md`): `PlayerToolManager::UpdateCamera`
(`src/game/Game/camera_input.cpp`), ext.touch_mode's `CameraInput` and `ApplyHeldZoom`.

## 1. Measured on the S25 (debug apk, UI 42, the phone's own settings)

`BANISHED_CAMERA_PIN=town,<dist>` (the start town's spawn tile, world (-11.5, -11.5), yaw 0) with
`ext.touch_mode_zoom_min=6` appended for the measurement, so the zoom target was not pulled back to 20 and the tilt
was the one the new floor gives (below). The screen is 2340 px wide. The house widths at 20 and 14 are read on the
screenshots (the house right of the focus); the others are scaled from them (width proportional to 1 / distance; the
readings at 20 and 14 agree with it within 5 %).

| Distance | A wooden house at the focus depth | Houses across the screen | Eye above the focus (default tilt) | Screenshot |
|---|---|---|---|---|
| 20 (retail's floor) | ~370 px | ~6.5 | 11.1 | `town20.png` |
| 14 | ~610 px | ~3.8 | 7.4 | `town14.png` |
| 10 | ~740 px | ~3.2 | 5.1 | `town10.png` |
| 8 | ~920 px | ~2.5 | 4.0 | `town8.png`, `v2_min_rotated.png` |
| 6 | ~1230 px | ~1.9 | 2.9 | `town6.png` |

At 8 two houses fill the width with a little ground around them (`v2_min_rotated.png`): the owner's "about two
houses". 6 is about one and a half houses plus the gap and starts to look like a citizen's-eye view.

The default tilt is retail's pitch target with the manual pitch at its default 0.75 (PlayerToolManager +0x44): -33.75 deg
at 20. Below 20 the normalised zoom ((target - 20) / 110) goes negative and retail's own blend carries on in a straight
line: -30.5 deg at 10, -29.8 at 8, -29.1 at 6 (a little flatter), and the pan speed 12 + (z - 20) * 58 / 110 likewise:
5.7 units/s at 8 (slower, so a pad press moves about the same fraction of the smaller view).

**What retail's 20 protected.** Not the terrain (the eye stays 2.9 units above the focus even at 6 with this tilt;
ext.better_camera's README records terrain clipping only at its 1 with the shallow 25-degree tilt) and not the near
plane against the ground. It protects the BUILDINGS: at 20 the eye is 11 units up, above every roof; at 8 it is 4 units
up and about 7 units behind the focus, so with a house behind the focus point the eye is inside its roof and the near
plane cuts the roof open (the roof's edges drawn as lines across the view: `town8.png`, `v1_min_buttons.png`, the
roof sliced in the foreground of `v3_min_panned.png` / `v5_pinch_in_min.png`). The same happens with
ext.better_camera at 6 on the desktop. Citizens, animals and the status-icon layer were drawn at every distance (no
status icon was due in these short runs, so none appeared); no terrain clipping or near-plane artefacts on the ground.

## 2. The route (decided before coding)

**(a) ext.touch_mode owns a nearer floor, `ext.touch_mode_zoom_min`, default 8.** Taken.
- With the row on, every zoom input of the frame (the zoom buttons, the pinch, a key, the wheel) clamps at the
  nearer of the key and the frame's own floor (retail's 20, or ext.better_camera's 6 when that row is on), so the
  inputs agree: the wheel zooming in from 12 does not jump back out to 20. `touch_math.h NearZoomLimit`.
- Only the clamp moves. The normalised zoom and the pitch blend and pan speed that read it are retail's expressions
  with retail's 20 / 110, carried on below 20 (section 1). better_camera's per-frame `ClampToRetailRange` takes the
  near limit as a parameter (default retail's 20), so the faithful call is unchanged.
- Row off (the desktop default, and the faithful profile, which turns the row off): `zNear == zMin`, the camera is
  retail's, the wheel unchanged. Unit-tested (`ext_touch_mode_zoom_min`).
- A range value key (6 .. 20), settings.cfg only, no Enhanced-tab entry: nothing for a player to tune unless the
  owner asks for it.

**(b) ext.better_camera seeded on Android.** Not taken. It is class B: locked for the game and recorded in the save
(the pseudo-mod `ext:better_camera`), so a phone save would need the row to load elsewhere. It also changes far more
than the near end: the far limit goes to 200, the steady tilt removes retail's flattening toward top-down on zoom out,
and the zoom span is renormalised to 194, which changes the pitch and pan speed at EVERY distance, not just near.

**(c) both.** Not needed: (a) alone gives the two-house view.

**Saves.** The zoom target (PlayerToolManager +0x3C) is serialized; a phone game saved at 8 stores 8. On a desktop
with the row off the first frame's `ClampToRetailRange` puts it back to 20. Retail itself would keep an 8 until the
first wheel step (which clamps to 20) and draws it with the extrapolated tilt (-29.8 deg, no flip: the flip
better_camera's header describes is at the far end, 200). So the row stays class A (ruling owed, below).

## 3. The pinch: continuous

The P11c follow-up: the pinch (ext.touch_pointer's two fingers, `Hold(Source::Fingers)`, fractional `zoomSteps`)
reached the zoom as whole `zoomDir` steps (5 units each, through retail's 0.9 / 0.1 smoothing). Now
`CameraInput` adds the fingers' fractional steps to `heldZoom` and they go through `ApplyHeldZoom` like the zoom
buttons: the target and the distance move together (no lag, no step), inside the same range (the same near limit).
The pinch's scale is unchanged (`kPinchLogPerStep` 0.12: a finger spread by e^0.12 = one retail step of 5 units).

## 4. Verified on the S25 (the default 8, no key in settings.cfg; uinput touches, `tools/android_multitouch.py`)

From `standalone_error.log` (`run_v2/`):
- Zoom in button held 6.5 s: distance 25.0 -> **8.0**, stopped there.
- Rotate left held 1.48 s at 8: yaw 0 -> -135 (90 deg/s), distance stayed 8.0 (`v2_min_rotated.png`).
- Pad held 1.5 s at 8: target (-17.5, 0.5) -> (-11.4, -5.2) = 8.6 units, about 5.7 units/s (the extrapolated pan speed).
- Pinch together (-9.80 steps) then three spreads (+10.56, +10.50, +10.55 steps): the zoom-out press after them
  started at distance **8.0**, so the pinch stops at the same floor (`v4_pinch_out.png`, `v5_pinch_in_min.png`).
- Zoom out button held 1.97 s: 8.0 -> 87.3 (40 units/s, P11c's rate).

Screenshots (session scratchpad `p11dzoom/`): `town20.png`, `town14.png`, `town10.png`, `town8.png`, `town6.png`
(the pinned measurement), `v0_start.png` .. `v6_zoom_out.png` (the touch run). Windows: not run (the functional game
dir stayed held by another lane for the whole window); the desktop path is the unit test plus the unchanged retail
lines with the row off.

## 5. Tests

`tools/build.sh unit`: 1552 tests, 0 failures, 1 skipped (the kit cross-check, no ToolKit). New:
`ext_touch_mode_zoom_min` (the near limit with the row off / on / with better_camera's 6, the held zoom and the
per-frame clamp at the new floor, a fractional pinch step, the key's range / default / clamp);
`ext_values_table_shape` counts 14 keys. Windows debug exe and the Android debug apk built; Linux / macOS not built
here (no platform seam touched; pure arithmetic in `src/ext/touch_mode/` and one call site in `camera_input.cpp`).

## Rulings owed

1. **The floor: 8** (about two and a half houses across the S25's width; 6 is about two houses edge to edge but the
   eye is under 3 units up). One settings.cfg line moves it (6 .. 20).
2. **Roofs at the near end**: below about 10 the eye can sit inside a roof that is behind the focus point and the
   near plane opens it up (section 1). Retail never shows it (eye 11 up at 20). Options: accept it (most close-zoom
   builders do; better_camera at 6 does the same); a steeper tilt below 20 (e.g. keep retail's -33.75 deg instead of
   the extrapolated -29.8, eye 4.4 instead of 4.0 at 8: barely helps), or a tilt that keeps the eye above a roof
   height (a new camera behaviour, a look the owner should choose); or raise the floor to 10.
3. **The tilt and pan speed below 20** follow retail's expressions in a straight line (slightly flatter, slower pan).
   Holding retail's near tilt and near pan speed instead is a one-line clamp of the normalised zoom.
4. **Class**: ext.touch_mode stays class A although a phone save can now hold a zoom target below 20 (section 2,
   "Saves": harmless to retail and pulled back to 20 by the row-off frame).
5. **Enhanced-tab entry** for the key: none added (settings.cfg only).

## Follow-ups

- With ext.touch_mode off and ext.touch_pointer on, the pinch stops at retail's 20 (the key belongs to the touch_mode
  row). Both are on in the Android seed.
- The phone's `settings.cfg` was briefly lost during this session (an `adb shell sed -i` on the app's file left it
  unreadable to the app; the game rewrote defaults): restored byte for byte from the copy pulled before
  (`p11dzoom/phone_settings_backup.cfg`). Edit the phone's settings by `cat > file` into the existing file, never
  `sed -i`.
