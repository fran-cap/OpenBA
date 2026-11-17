#pragma once
// ============================================================================
// src/ext/better_camera/better_camera.h -- ext.better_camera (class B, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCAM.md. One change to retail's camera:
//
//   * A WIDER ZOOM RANGE. Retail clamps the zoom target (the distance the camera settles at) to 20 .. 130
//     (.rdata 0x1401721c4 / 0x140172218, span 110 at 0x14017220c). With the row on the range is 6 .. 200. The top
//     is the range Banished Game Tweaks' exe fix has used since 2026-09 (live-confirmed in the game); the floor is 6, not that
//     fix's 1: at the shallow tilt (25 degrees) the eye sits only distance * sin(25 deg) above the focus point, 0.4
//     units at 1, and the camera clips the terrain (PR review); at 6 it is about 2.5 units. The span that
//     normalises the zoom for the pitch blend and the pan speed is recomputed from the new limits (max - min),
//     as the exe fix does, so those two keep their near-to-far shape across the new range.
//
//   * THE PAN CLAMP KEEPS RETAIL'S FLOOR. Retail's focus clamp tightens with the distance; past 130 it reads 130
//     (PanClampDistance below), so the wider zoom does not pull the focus away from the map's rim.
//
//   * THE FAR PLANE COVERS THE TERRAIN. Retail's 512 clips low ground behind a nearer mountain at the wide zoom
//     (FarPlane below); with the row on the plane lies past the terrain's diagonal.
//
//   * The tilt is not this row's: ext.camera_angle (src/ext/camera_angle/camera_angle.h, FarPair) sets the far
//     pair of the pitch blend, and UpdateCamera calls PitchTarget below with steady = false.
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

#include <cmath>

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

// The distance the pan clamp reads with the row on. Retail shrinks the square the focus may move in as the camera
// backs off: factor = clamp(1 - (distance - 100) / 150, 0, 1), which is 0.8 at retail's far limit (130) and would
// be 0.33 at this row's (200): fully zoomed out the focus could not leave the middle third of the map (a player's
// report). Past retail's far limit the clamp reads that limit, so the square never gets smaller than retail's own
// smallest. The identity for every distance up to 130.
inline float PanClampDistance(float distance) {
    if (distance > kRetailZoomMax) return kRetailZoomMax;
    return distance;
}

// The camera's far plane with the row on. Retail's is fixed (512, .rdata 0x140172240): from its far zoom the
// fog has hidden the ground long before that distance. From this row's zoom, with the tilt flat and the fog
// pushed back (ext.no_fog), low ground past 512 is clipped while a nearer mountain still draws, and the
// clear colour shows between the mountains. With the row on the far plane is one value for the whole game,
// from the terrain's size alone: the terrain's diagonal and the row's highest eye (kZoomMax), plus
// kFarMargin, which is past the farthest ground an eye over the terrain can see at any zoom, tilt or
// heading. It does not follow the eye, so every frame of a game builds the same projection.
// `terrainHalf`: half the terrain's side in world units (the ground runs -half .. +half on x and y, centred
// on the origin); 0 when there is no terrain. Row off, no terrain, or a terrain small enough for retail's
// plane: retail's value as given (the same bits come back). The sun's shadow box does not read the far
// plane (SunLight::GetSunCamera extrudes the near corners); the frustum cull does, and draws what is now in
// view.
constexpr float kFarMargin = 16.0f;
inline float FarPlane(bool on, float retailFar, float terrainHalf) {
    if (!on || !(terrainHalf > 0.0f)) return retailFar;
    const float side = terrainHalf * 2.0f;
    const float reach = std::sqrt(side * side * 2.0f + kZoomMax * kZoomMax) + kFarMargin;
    return reach > retailFar ? reach : retailFar;
}

// The same for the game being drawn: the terrain's side from the world's heightmap (better_camera.cpp). The
// three callers of PlayerController::UpdateCamera's game arm (GameUpdateLoop::Draw step 4, ext.render_pipe's
// copy of it and its idempotence check) pass retail's constant through it, so they agree.
float FrameFarPlane(float retailFar);

}  // namespace BetterCamera
}  // namespace Ext
