# CAMERAANGLE hand-in

`ext.camera_angle` + `ext.camera_tilt`: how far the camera tilts toward top-down as it zooms out. Class A, **ON by default on a desktop** at tilt 50 = retail's pitch (the Android first run seeds it off), live, nothing serialized. Maintainer ruling 2026-11-08: Better camera's steady tilt becomes a slider, "middle is retail and max is a touch more than better camera"; the zoom range stays a class B toggle, renamed "Wider zoom".

## 1. What it does

Retail's pitch target (`PlayerToolManager::UpdateCamera`, `src/game/Game/camera_input.cpp`) blends a near pair of slopes (-25, 60) and a far pair (-65, 85) by the normalised zoom, so the camera flattens toward top-down as it zooms out. With the row on, `ext.camera_tilt` (0..100) replaces the far pair (`src/ext/camera_angle/camera_angle.h`, `FarPair`); the near pair is never touched:

| slider | far pair | zoomed out |
|---|---|---|
| 0 | (-85, 85) | top-down (retail's -85 ceiling) at every manual pitch |
| 50 | retail's (-65, 85) | retail (the default) |
| 90 | the near pair (-25, 60) | the same tilt at every zoom: Better camera's old steady tilt |
| 100 | the near pair x 0.9 (-22.5, 54) | a touch flatter than up close |

Linear between the anchors. Every pitch the slider can give is in -85 .. -22.5 (unit-tested over every position, manual pitch and zoom); retail's ceiling is never passed (a steeper pitch flips the view, BETTERCAM.md). The flattest value only applies zoomed far out, where the eye is high above the ground.

## 2. The seam

One, in `UpdateCamera`, where the pitch target is computed: `Ext::CameraAngle::FarPair(row, tilt, retail far pair, near pair) -> far pair`, then `BetterCamera::PitchTarget(..., steady = false)`, which is retail's expression in retail's order. Row off: the far pair is retail's two constants, bit for bit.

## 3. Class

Class A. The pitch target is reassigned from scratch every frame and the smoothed pitch follows it within a few dozen frames, so a save made with the row on loads in retail and settles back to retail's angle; no simulation reads the pitch.

## 4. Menus and migration

* Options > Enhanced "Camera angle" slider (`src/ext/camera_angle/enhanced_entry.cpp`, `EnhancedOptions.rsc` row 17): sets `ext.camera_tilt`; it never turns the row off (50 with the row on is retail), and turns it on for any other value when the player switched it off.
* The Extensions dialog lists the row "Camera angle" (icon `tools/ext_icons/camera.py`).
* `ext.better_camera` is now the zoom range only ("Wider zoom"). `InitFromSettings` (`registry_boot.cpp`) migrates a settings.cfg that has `ext.better_camera=1` and no `ext.camera_angle` key to `ext.camera_angle=1`, `ext.camera_tilt=90`, so a player who had Better camera keeps the tilt they had.

## 5. Verified / not verified

* VERIFIED: unit 2307 / 0 / 3 skipped (`tests/unit/ext_camera_angle_test.cpp`: the row and key, retail's bits with the row off, the four anchors, the -85 .. -22.5 envelope, 90 independent of zoom); the exe builds.
* NOT YET LOOKED AT in game: the slider and the camera at its positions; the migration on a real settings.cfg.
* For a ruling: 100's "touch more" is 10 % (kShallowAtMax); 0 is fully top-down zoomed out.

## 6. The sun box's depth (SunLight::_extDeepSunBox)

Retail's sun shadow box (`GetSunCamera`, `src/engine/graphics/sun_camera.cpp`) is wide enough for the view but a fixed 256 deep along the sun: the eye 128 back from the centre of the view's bounding sphere, the far plane at 256. A shallow tilt or a wide zoom puts ground more than 128 past the centre on the side away from the sun; that ground lies past the far plane, its shadow depth clamps to 1.0 and the LESS compare against the cleared map reads it as fully shadowed: a dark, straight-edged wedge over the distant terrain (seen with the slider past 50).

With the row on (the seam is null under the faithful profile, `registry_boot.cpp`), the eye is pulled back `radius + 64` (casters above the sphere toward the sun) and the far plane set at `pull + radius + 1`, only when that is more than retail's 128; a view whose sphere fits retail's box keeps retail's numbers bit for bit. The box's width, the texel snap and ext.max_shadow_zoom's scale are unchanged. D24 over a deeper range costs depth steps that the unbiased front-face-cull compare does not notice.

* VERIFIED by hand (the maintainer, 2026-11-08): the wedge is gone at a high tilt. Unit 2342 / 0 / 3 skipped; gate and resave check PASS.
