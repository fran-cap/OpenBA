#pragma once
// ============================================================================
// src/ext/camera_angle/camera_angle.h -- ext.camera_angle (class A) + ext.camera_tilt
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CAMERAANGLE.md. How far the camera tilts toward top-down as it zooms out.
//
// RETAIL. PlayerToolManager::UpdateCamera (game/Game/camera_input.cpp) sets the pitch target to a blend of a
// NEAR pair of slopes and a FAR pair, weighted by the normalised zoom (normZ, 0 near .. 1 far):
//   pitch = (m * farMan - (1 - m) * farFlat) * normZ + (m * nearMan - (1 - m) * nearFlat) * (1 - normZ)
// with m the player's manual pitch (0..1). Retail's pairs: far (-65, 85), near (-25, 60) degrees, so the
// camera goes from -25 .. -60 up close to -65 .. -85 (almost top-down) zoomed out.
//
// THE EXTENSION. With the row on, ext.camera_tilt (0..100, the Enhanced tab's "Camera angle" slider) replaces
// the FAR pair; the near pair is never touched:
//   *   0 .. 50   from top-down (-85, 85: every manual pitch gives -85, retail's own ceiling) to retail's far pair
//   *  50        retail's far pair exactly
//   *  50 .. 90   from retail's far pair to the near pair: at 90 the tilt is the same at every zoom, which is
//                 what ext.better_camera's "steady tilt" did (the slider took that half of it over)
//   *  90 .. 100  on to the near pair made kShallowAtMax (10 %) shallower: zoomed out, a little flatter to the
//                 horizon than up close ("a touch more than Better camera", maintainer ruling 2026-11-08)
// Every pitch the slider can give lies in -85 .. -22.5. Retail's ceiling (-85; Vector3::FromYawPitch is a fitted
// approximation, a steeper pitch flips the view, BETTERCAM.md) is never passed. The shallow end, -22.5, only
// applies zoomed far out (the near pair is retail's), where the eye is high above the ground.
//
// The slider's 50 is the row OFF (retail); FarPair with the row off returns retail's constants as given, so the
// faithful camera is bit-identical.
//
// CLASS A. The pitch target is recomputed from scratch every frame (retail assigns it, it never accumulates), and the
// smoothed pitch follows it within a few dozen frames; a save made with the row on loads in retail and settles back
// to retail's angle. No simulation state, live.
//
// Standard C++ only: tests/unit/ext_camera_angle_test.cpp links nothing but this header.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace CameraAngle {

constexpr int32_t kTiltMin = 0;
constexpr int32_t kTiltRetail = 50;     // the slider's middle: the row off
constexpr int32_t kTiltSteady = 90;     // the far pair == the near pair (ext.better_camera's old steady tilt)
constexpr int32_t kTiltMax = 100;
constexpr float kTopDown = 85.0f;       // retail's ceiling, |pitch| degrees (kPitchFarFlat)
constexpr float kShallowAtMax = 0.9f;   // the near pair's slopes x 0.9 at the slider's top

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// The far pair to use. Row off: retail's own values, untouched (the same bits come back).
inline void FarPair(bool on, int32_t tilt, float retailFarMan, float retailFarFlat, float nearMan, float nearFlat,
                    float& farMan, float& farFlat) {
    farMan = retailFarMan;
    farFlat = retailFarFlat;
    if (!on) return;
    if (tilt < kTiltMin) tilt = kTiltMin;
    if (tilt > kTiltMax) tilt = kTiltMax;
    if (tilt < kTiltRetail) {
        const float t = static_cast<float>(kTiltRetail - tilt) / static_cast<float>(kTiltRetail - kTiltMin);
        farMan = Lerp(retailFarMan, -kTopDown, t);
        farFlat = Lerp(retailFarFlat, kTopDown, t);
    } else if (tilt <= kTiltSteady) {
        const float t = static_cast<float>(tilt - kTiltRetail) / static_cast<float>(kTiltSteady - kTiltRetail);
        farMan = Lerp(retailFarMan, nearMan, t);
        farFlat = Lerp(retailFarFlat, nearFlat, t);
    } else {
        const float t = static_cast<float>(tilt - kTiltSteady) / static_cast<float>(kTiltMax - kTiltSteady);
        farMan = Lerp(nearMan, nearMan * kShallowAtMax, t);
        farFlat = Lerp(nearFlat, nearFlat * kShallowAtMax, t);
    }
}

}  // namespace CameraAngle
}  // namespace Ext
