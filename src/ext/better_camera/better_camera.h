#pragma once
// ============================================================================
// src/ext/better_camera/better_camera.h -- ext.better_camera (class B, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCAM.md. Two changes to retail's camera, one toggle:
//
//   * A WIDER ZOOM RANGE. Retail clamps the zoom target (the distance the camera settles at) to 20 .. 130
//     (.rdata 0x1401721c4 / 0x140172218, span 110 at 0x14017220c). With the row on the range is 6 .. 200. The top
//     is the range Banished Game Tweaks' exe fix has used since 2026-09 (live-confirmed in the game); the floor is 6, not that
//     fix's 1: at the shallow tilt (25 degrees) the eye sits only distance * sin(25 deg) above the focus point, 0.4
//     units at 1, and the camera clips the terrain (PR review); at 6 it is about 2.5 units. The span that
//     normalises the zoom for the pitch blend and the pan speed is recomputed from the new limits (max - min),
//     as the exe fix does, so those two keep their near-to-far shape across the new range.
//
//   * A STEADY TILT. Retail's pitch target is a blend between a NEAR pair of slopes and a FAR pair,
//     weighted by how far out the camera is (normZ, 0 near .. 1 far). The far pair is weaker, so the
//     camera flattens toward top-down as you zoom out -- by design, but it is what players ask to turn off.
//     With the row on the far pair is replaced by the near pair, so the tilt is the same at every zoom.
//     (Banished Game Tweaks' tilt fix redirects the same two constants.)
//
// The pitch's own ceiling is NOT touched: Vector3::FromYawPitch is a fitted approximation that is only valid
// up to about 1.0 rad (the exe-fix research found raising the ceiling flips the camera).
//
// WHERE: PlayerToolManager::UpdateCamera (game/Game/camera_input.cpp), once per frame, the toggle read once at
// the top; with the row off every value is retail's own constant and the pitch expression is retail's, in
// retail's operation order, so the faithful camera is bit-identical.
//
// SAVES: class B. No new state, but with the row on two fields retail serializes, the zoom target (PlayerToolManager
// +0x3C) and the camera distance (PlayerController::distance, which follows it), can hold values outside retail's
// 20 .. 130, so the save records the extension (the pseudo-mod ext:better_camera, DESIGN.md section 2) and the row is
// locked for the game. The faithful route clamps the zoom target into retail's range EVERY frame the row is off
// (ClampToRetailRange): a save written with the row on, and a camera preset stored with it on (ApplyCameraView writes
// the stored distance straight back), hand the camera a target outside retail's range with no toggle edge in between.
// At 200 retail's normZ is (200 - 20) / 110 = 1.64 and the pitch blend extrapolates to about -101 degrees (retail's
// ceiling is -85), which flips the view. The clamp is a no-op for every value retail itself can hold.
// ============================================================================

namespace Ext {
namespace BetterCamera {

// Retail's limits (the faithful values; camera_input.cpp keeps its own copy of the .rdata constants).
constexpr float kRetailZoomMin = 20.0f;
constexpr float kRetailZoomMax = 130.0f;
constexpr float kRetailZoomSpan = 110.0f;

// ext.better_camera on.
constexpr float kZoomMin = 6.0f;
constexpr float kZoomMax = 200.0f;
constexpr float kZoomSpan = kZoomMax - kZoomMin;

// The pitch target (degrees). `manual` is the player's manual pitch (0..1), `normZ` the normalised zoom.
// steady = false is retail's expression exactly (same terms, same order); steady = true uses the near pair
// for the far term as well, which makes the result independent of normZ.
inline float PitchTarget(float manual, float normZ, float farMan, float farFlat, float nearMan, float nearFlat, bool steady) {
    if (steady) {
        farMan = nearMan;
        farFlat = nearFlat;
    }
    return (manual * farMan  - (1.0f - manual) * farFlat) * normZ +
           (manual * nearMan - (1.0f - manual) * nearFlat) * (1.0f - normZ);
}

// The zoom target brought back into retail's range, applied every frame the row is off. The identity for 20 .. 130.
// (Retail's own "a zero target is seeded from the distance" step runs before the seam, so a zero does not reach it.)
// `nearLimit` is retail's 20 except while ext.touch_mode brings the near limit closer (touch_math.h NearZoomLimit,
// docs/android/P11D_ZOOM.md): then a target between that limit and 20 is kept.
inline float ClampToRetailRange(float zoomTarget, float nearLimit = kRetailZoomMin) {
    if (zoomTarget < nearLimit) return nearLimit;
    if (zoomTarget > kRetailZoomMax) return kRetailZoomMax;
    return zoomTarget;
}

}  // namespace BetterCamera
}  // namespace Ext
