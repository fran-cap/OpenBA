# Android P11c: the owner's camera rulings on ext.touch_mode (lane p11ccamera)

Base `android/p11b` (PR #61). The rulings are `docs/runtime_port/NEXT_SESSION.md` §2.11, "RULED 2026-10-26 (owner,
real-finger test of the P11b apk)". Everything is behind ext.touch_mode's camera seam (`Ext::TouchMode::CameraInput`
in `PlayerToolManager::UpdateCamera`): keys, the wheel and the mouse are unchanged, and the faithful profile runs
retail's lines alone. The reference description is `docs/android/TOUCH_MODE.md` §4.

## What changed (one commit each)

| Ruling | Code | Constants |
|---|---|---|
| Rotate: no ramp | `touch_mode.cpp`: the rotate buttons add `held * dt * 90` deg (retail's key rate) every held frame; the rotate `SpeedRamp` is gone | `kRotateDegPerSecond` 90 (retail's `kRotSpeed`) |
| Pan ramp ~100 ms | `touch_math.h` | `kRampInSeconds` 0.10, `kRampOutSeconds` 0.09 (smoothstep, as before) |
| Analogue D-pad | `touch_math.h` `PadVector` replaces the 8-way `PadDirection`; `CameraHold::panX / panY` are floats; the seam hands `UpdateCamera` the unit direction in `panX / panY` and the speed times the ramp in `panGain` | `kDeadZoneFraction` 0.10 of the side as a radius (was 0.15), `kPadSpeed = kPadFull` |
| Zoom smooth, zero delay | `CameraInput` returns `heldZoom` (retail's steps, + in) while a zoom button is held; `UpdateCamera` applies it after retail's own step with `ApplyHeldZoom` (target and smoothed distance moved together, clamped to the frame's range); with ext.eye_camera engaged, `EyeCamera::ZoomBy` moves the eye's distance instead. `ZoomRepeat` and its constants are gone | `kZoomStepsPerSecond` 8 (x retail's 5-unit step = 40 units/s; 20 .. 130 in 2.75 s) |

The rectangular pad graphic and the capture rule are unchanged (the press captures the pad; the direction follows the
finger until the release, also off the pad).

## Measured on the S25 (uinput touches, `tools/android_multitouch.py`, a temporary per-frame trace removed before the commits)

Layout 982 x 453 (UI 0.42), pad centre (109, 344) layout = (259, 819) screen px, half side 75 layout units. Every
hold below is 0.5 s of finger motion plus 1.5 s still (1.98 s held), camera at distance 25 (pan speed 14.6 /s).

- **Direction, full speed** (`kPadFull`): right -> target +28.7 x; 30 deg up-right -> (24.6, 14.7) = 30.9 deg;
  45 deg -> (20.1, 20.5), length 28.7 = the cardinal's (a diagonal is not faster); 120 deg up-left -> (-14.5, 24.7),
  length 28.6; a hold inside the dead zone (r = 22 px) -> no motion.
- **Finger moving while held** (right arrow to the top arrow): the pan angle tracked the finger's angle frame by
  frame (1.0, 9.1, 19.2, 31.4, 50.9, 65.8, 78.2, 87.0, 91.0 deg), length 1.000 throughout.
- **Ramp**: gain 0.074, 0.259, 0.500, 0.741, 0.926, 1.000 on the first six frames (100 ms); after the release
  0.910, 0.690, 0.417, 0.167, 0.016, 0 (5 frames, 0.78 units of coast).
- **Rotate**: -1.5 deg on the first held frame and every frame after (90 deg/s, no ramp); stops on the release.
- **Zoom**: 0.667 units per frame (40 /s at 60 fps) from the first held frame; zoom in stopped at retail's floor 20;
  zoom out 20 -> 100 in 1.98 s; the distance stayed at 99.997 after the release (no coast, no step).
- **Proportional speed tried** (`kPadProportional`, same build otherwise): over 1.98 s, r = 0.3 / 0.6 / 0.9 of the half
  side moved 3.3 / 13.8 / 24.8 units, r = 0.78 moved 20.4 (cardinal) and 20.8 (diagonal). Not taken: a finger on
  the drawn arrows panned at about half to three quarters of the keys' speed and a finger cannot feel where it rests
  on the flat pad; with full speed the press position only picks the direction, and the 0.10 s ramp gives the soft
  start. `PadVector` keeps both modes (unit-tested) so the owner's verdict is a one-constant change.

Traces and the in-game screenshot (the cluster graphic unchanged): session scratchpad `p11ccamera/`
(`run2_full_trace.log`, `run4_proportional_trace.log`, `cluster_ingame.png`, the uinput scripts `pad.json`,
`zoom.json`, `prop.json`).

## ext.better_camera and ext.eye_camera

`ApplyHeldZoom` takes the frame's `zMin / zMax` (retail's 20 .. 130, or better_camera's 6 .. 200 with that row on),
the same range retail's step clamps to, so the held zoom cannot leave either range; better_camera's
`ClampToRetailRange` (row off) runs before it as before. Unit test `ext_touch_mode_held_zoom` covers both ranges and
the clamp. With ext.eye_camera following a citizen the held zoom goes to `EyeCamera::ZoomBy` (`ZoomDistanceBy`,
fractional steps of `kDistanceStep`, same clamps; tested in `ext_eye_camera_test.cpp`) and retail's target is left
alone, as for the wheel. Neither combination was run on the phone (unit tests only).

## Tests

`tools/build.sh unit`: 1545 tests, 0 failures, 1 skipped (the kit cross-check, no ToolKit). New / changed:
`ext_touch_mode_pad_vector` (any angle, dead zone, both speed modes, off-pad), `ext_touch_mode_held_zoom`
(rate, frame-rate independence, ranges), `ext_touch_mode_speed_ramp` (the 0.10 s ramp), the eye camera's
`ZoomDistanceBy` lines. Windows debug exe built; Linux / macOS not built here (no platform seam touched).

## Rulings owed

1. **Pad speed**: full speed outside the dead zone (taken) vs proportional to the distance (measured above). A real
   finger decides.
2. **Dead zone** radius 0.10 of the side (20 % of the half side; 36 px on the S25): a first value for an analogue pad.
3. **Zoom rate** 40 units/s constant (20 .. 130 in 2.75 s; better_camera's 6 .. 200 in 4.9 s). Constant in distance,
   so the near end feels faster than the far end; a rate proportional to the distance is the alternative.
4. **Pan ramp out** 0.09 s (the ruling allowed 0.08 .. 0.10).

## Follow-ups

- The pinch (ext.touch_pointer) still reaches the zoom as whole steps through `zoomDir`; it could use `heldZoom`'s
  continuous path too (not in this ruling). DONE in P11d (`docs/android/P11D_ZOOM.md`).
