// ============================================================================
// src/ext/eye_camera/eye_camera.h -- ext.eye_camera (class A, default OFF)
// ============================================================================
// NOT RETAIL. "Eye-level camera": while the player follows a citizen through
// retail's own follow (the citizen panel's Follow button -> FollowDialog), the
// camera sits at the citizen's eyes, or a short way behind and above the head,
// looking along the citizen's heading. Position, heading and distance are
// smoothed; the mouse wheel / zoom keys move the eye between first person (0)
// and kMaxDistance. Ending the follow (the dialog's close button, Escape, the
// citizen panel closing) gives retail's camera back exactly where
// PlayerController's target / yaw / pitch / distance put it: this code never
// writes them (they are serialized, PlayerController::Serialize 0x1400988c0 and
// PlayerToolManager::Serialize 0x140102920).
//
// The three seams, each selected at the caller with the toggle read once:
//   GameUpdateLoop::Update step 6   if (Ext::Ext().eye_camera && Ext::EyeCamera::Active())
//                                       Ext::EyeCamera::Apply(*playerCtrl);
//                                   else retail's CalcTransform / AsTransform3 /
//                                       SetCameraToWorld
//   GameUpdateLoop::Draw step 4     ... Ext::EyeCamera::ApplyFrame(pc, alpha, fovY, far)
//                                   else retail's UpdateCamera(40, 2, 512)
//   PlayerToolManager::UpdateCamera ... zoomDir = Ext::EyeCamera::Zoom(zoomDir)
//
// Active() is the whole engagement test (EYECAM.md §10): the frontmost open
// FollowDialog in the UI's dialog list, and its citizen's entity -- the
// identity, compared by pointer with the latched one -- not marked for
// removal. A new entity is first checked to be in Object::InternalFactory's
// table (a freed one never is); a latched one is not (a live object can drop
// out of that table). A removed citizen gives retail's follow back, and
// nothing else does while a FollowDialog targets it: an idle citizen keeps its
// last heading, a hidden one (model out of the scene: inside a building) keeps
// the last eye transform (or, engaged while inside, the door it went in by).
// Every engage and hand-back prints one "[EyeCamera] ..." line on stderr.
// ============================================================================
#pragma once

class PlayerController;   // Game/PlayerController.h
class ComponentEntity;    // Entity/ComponentEntity.h
namespace Graphics { class Renderer; class Scene; }   // graphics/Camera.h, graphics/Scene.h
namespace Video { class Context; }                    // video/Context.h

namespace Ext {
namespace EyeCamera {

// True while the extension should own the camera (see the banner). Cheap: one
// flag, one walk of the open dialogs, one weak-reference test. Call it only
// when Ext::Ext().eye_camera is on.
bool Active();

// The entity the eye is engaged on (null when not engaged). For the one seam
// that must not treat it as off-display: EditTool::Update closes an unpinned
// info panel when its entity projects to no screen rect (retail 0x1400fcebc),
// and at eye level the followed citizen is at or behind the camera, so the
// close fired whenever they stopped (EYECAM.md §11). Cheap: no walk, no test.
const ComponentEntity* FollowedEntity();

// GameUpdateLoop::Update step 6 (once per update tick). Runs retail's
// PlayerController::CalcTransform for its +0x00 store (a serialized field,
// kept exactly retail's) and then puts the last eye transform on the camera,
// so the tick's later readers (picking, the audio listener) see the eye.
void Apply(PlayerController& pc);

// GameUpdateLoop::Draw step 4 (once per rendered frame): advance the
// smoothing by the real frame time, build the eye from the citizen model's
// transform interpolated at `alpha` (the renderer's own), set it on the
// camera, and rebuild the projection with the near plane kNearPlane.
void ApplyFrame(PlayerController& pc, float alpha, float fovY, float farZ);

// PlayerToolManager::UpdateCamera's zoom input: moves the eye's distance and
// returns 0, so retail's zoom target (PlayerToolManager +0x3C) is untouched.
int Zoom(int zoomDir);

// The same for a continuous zoom (ext.touch_mode's held zoom button): `steps` of retail's zoom (+ in, fractional)
// move the eye's distance by steps * kDistanceStep; returns 0.
float ZoomBy(float steps);

// GameUpdateLoop::Draw step 7 (once per rendered frame): retail's
// Renderer::Draw with the player camera (the eye, set at step 4), with the
// sun's shadow camera fitted to ShadowFitPose's view instead of the eye's
// (Graphics::SunLight::_extShadowFitView, set for this one call). Retail's
// GetSunCamera fit assumes a view looking down at the ground; at eye level its
// box misses the citizen (EYECAM.md §9).
void DrawScene(Graphics::Renderer& renderer, Video::Context& context, float alpha,
               const Graphics::Scene& scene, PlayerController& pc);

}  // namespace EyeCamera
}  // namespace Ext
