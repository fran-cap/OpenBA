// ============================================================================
// src/ext/eye_camera/eye_math.h -- ext.eye_camera: the pure math
// ============================================================================
// NOT RETAIL. The eye-level citizen camera's arithmetic, standard C++ only, so
// tests/unit/ext_eye_camera_test.cpp links it without the world. The engine
// glue (which citizen, its model's interpolated transform, the camera write) is
// eye_camera.cpp.
//
// CONVENTIONS (the engine's, read from the bodies that build them):
//   * Z is up; a heading H (degrees) faces Math::Vector3::FromYawPitch(H, 0)
//     = (-sin H, cos H, 0), and Math::Vector3::Heading() inverts it.
//   * A citizen's model rotation is a pure Z rotation built by the move tick
//     (CharacterComponent_MoveTick_1094c0, citizenai_executor.cpp) as
//     q = (0, 0, -sin(Y/2), cos(Y/2)) with Y = heading + CharacterDescription::_f24
//     (the description's model yaw offset); ModelYawFromQuaternion inverts it.
//   * Units: world units. A tile is one unit, and the shipped citizen mesh
//     (data0.pkg Models\Citizen\CitizenMaleMesh_resource.crs, its bounding box
//     read from the compiled resource) stands z 0.002 .. 0.946: a person is
//     ~0.95 units tall, so one unit is ~1.85 m and a 1.6 m eye is ~0.88 units.
//   * The model scale: a citizen's age scale is the uniform factor
//     CitizenComponent::SetupYoung / SetupAdult / SetupElder (0x140115110 /
//     0x1401151a0 / 0x140115230, citizenaicomponent.cpp) store into the
//     model's MeshInstance::_color[0..2] (+0x10c..+0x114): 1.0, 0.83 and 0.66
//     (a child). MeshInstance::AddDrawItems hands it to the vertex shader as
//     the w column of the instance transform (Context::SetTransformScale), so
//     the drawn mesh is the 0.946-tall mesh times it, about the feet. The eye
//     height and the face offset are kEyeHeight / kFaceForward times it.
// ============================================================================
#pragma once

namespace Ext {
namespace EyeCamera {

struct Vec3 {
    float x, y, z;
};

// ---- the constants (the hand-in, docs/extensions/handins/EYECAM.md §4) ----
constexpr float kEyeHeight       = 0.88f;  // feet -> eyes at model scale 1 (0.93 of the 0.946 mesh height)
constexpr float kFaceForward     = 0.10f;  // the eye ahead of the body axis at model scale 1 (face front ~0.09)
constexpr float kMaxDistance     = 3.0f;   // the over-the-shoulder limit (~5.5 m)
constexpr float kDefaultDistance = 0.9f;   // the first follow's distance (close third person)
constexpr float kDistanceStep    = 0.3f;   // one wheel notch / zoom key press
constexpr float kRisePerDistance = 0.30f;  // the camera rises 0.3 per unit it backs off
constexpr float kLookAhead       = 6.0f;   // the look-at point ahead of the eyes
constexpr float kNearPlane       = 0.1f;   // retail's 2.0 clips everything at eye height
constexpr float kPositionTau     = 0.06f;  // seconds: feet position smoothing
constexpr float kHeadingTau      = 0.25f;  // seconds: heading smoothing (a turn does not snap)
constexpr float kDistanceTau     = 0.10f;  // seconds: wheel distance smoothing
constexpr float kSnapDistance    = 4.0f;   // a jump farther than this (teleport) snaps
constexpr float kMaxFrameGap     = 0.5f;   // seconds without a frame: re-engage with a snap
constexpr float kMinClearance    = 0.15f;  // camera height kept above the terrain
// The shadow fit's view (EYECAM.md §9): a retail-like camera over the citizen,
// looking down at the ground ahead along the heading. Fitted by retail's
// GetSunCamera, it gives a box ~50 units across from just behind the feet to
// ~50 ahead, the size retail's own fit gives at a middle zoom.
constexpr float kShadowFitAhead    = 16.0f;  // the fit view looks at the ground this far ahead
constexpr float kShadowFitDistance = 48.0f;  // its distance from that point
constexpr float kShadowFitPitch    = 55.0f;  // degrees below level

// (-180, 180]
float WrapDegrees(float degrees);

// The model yaw Y (degrees) of a pure Z rotation q = (0, 0, -sin(Y/2), cos(Y/2)).
// Sign-safe (q and -q give the same yaw) and scale-safe (a lerped, unnormalised q).
float ModelYawFromQuaternion(float qz, float qw);

// FromYawPitch(heading, 0): (-sin H, cos H, 0).
Vec3 Forward(float headingDegrees);

// One zoom step: zoomDir > 0 is zoom IN (retail's sign, camera_input.cpp), which
// brings the eye closer; the result is clamped to [0, kMaxDistance].
float ZoomDistance(float distance, int zoomDir);

// A continuous zoom of `steps` (+ in, fractional): distance - steps * kDistanceStep, clamped the same way.
float ZoomDistanceBy(float distance, float steps);

// The eye and its look-at point for a citizen standing at `feet` facing
// `headingDegrees`, drawn at `modelScale` (the age scale, see CONVENTIONS: the
// eye sits at kEyeHeight * modelScale, kFaceForward * modelScale ahead).
// distance 0 = first person (the eye just ahead of the face); > 0 backs the
// eye off along the heading and lifts it kRisePerDistance per unit.
struct Pose {
    Vec3 eye;
    Vec3 look;
};
Pose ComputePose(const Vec3& feet, float headingDegrees, float modelScale, float distance);

// The view the shadow fit is computed from while the eye is engaged (never
// drawn): look at the ground kShadowFitAhead ahead of the feet from
// kShadowFitDistance back along the heading, kShadowFitPitch below level.
Pose ShadowFitPose(const Vec3& feet, float headingDegrees);

// Exponential smoothing, frame-rate independent: 1 - exp(-dt / tau), 0..1.
float BlendFactor(float dt, float tau);

// The smoothed inputs of ComputePose.
struct Smooth {
    bool  valid    = false;
    Vec3  feet     = {0.0f, 0.0f, 0.0f};
    float heading  = 0.0f;
    float distance = 0.0f;
};
void Snap(Smooth& s, const Vec3& feet, float heading, float distance);
// Advance toward the targets by dt seconds. Not valid yet, or a jump past
// kSnapDistance: snap. The heading takes the short way round.
void Step(Smooth& s, const Vec3& feet, float heading, float distance, float dt);

// The slot of `object` in a swap-remove table (Object::InternalFactory::_objects:
// RemoveObject 0x1800c0110 moves the LAST entry into the freed slot, so a live
// object's index changes whenever it was last and something else died). `hint`
// is the index it had; when the table no longer holds it there, the table is
// searched by pointer value (nothing is dereferenced). -1 = not in the table:
// the object was destroyed (~Internal removes it). The caller checks the
// generation once the slot names the object (an address reused by a new one).
template <class Slot>
int FollowSlot(const Slot* slots, int count, const void* object, int hint) {
    if (hint >= 0 && hint < count && slots[hint] == object) return hint;
    for (int i = count - 1; i >= 0; --i)
        if (slots[i] == object) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// EYECAM3: one step's engagement decision, without the world (EYECAM.md §10).
// The identity is the FOLLOWED ENTITY, read each step from the frontmost open
// FollowDialog (its citizen's entity: the same read retail's FollowDialog::
// OnUpdate 0x140069250 makes every frame), not the dialog object and not the
// InternalFactory slot. The factory table is consulted only before a NEW
// entity is first dereferenced (a freed one is never in it); once latched, a
// table miss does not hand back (a live object can drop out of the table:
// EYECAM.md §10.2). An entity seen marked for removal or missing from the
// table is `dead` and never dereferenced again while a dialog targets it.
// ---------------------------------------------------------------------------
enum class FollowVerdict {
    Keep,    // the latched entity, still the frontmost follow's: stay engaged
    Latch,   // a different entity: (re)engage on it
    Off,     // hand back to retail's camera, for `reason`
};
enum class FollowReason {
    None,
    NoFollowDialog,   // no FollowDialog open (closed: button, Escape, panel closed)
    NoCitizen,        // the frontmost FollowDialog has no citizen (+0xB8 null)
    EntityDead,       // its entity was seen marked / gone on an earlier step
    EntityGone,       // a new entity not in InternalFactory's table: destroyed
    EntityMarked,     // its entity is marked for removal (+0x40 bit 10)
};
struct FollowDecision {
    FollowVerdict verdict;
    FollowReason  reason;
};
const char* FollowReasonName(FollowReason reason);

// haveDialog: an open FollowDialog exists. front: its citizen's entity
// (nullptr: no citizen). latched: the entity the eye follows (nullptr: none).
// dead: the entity last seen marked or gone (nullptr: none). inTable(front)
// is asked only for an entity not latched yet, marked(front) only after the
// dead and table tests passed (it dereferences).
template <class T, class InTable, class Marked>
FollowDecision DecideFollow(bool haveDialog, const T* front, const T* latched,
                            const T* dead, InTable inTable, Marked marked) {
    if (!haveDialog) return {FollowVerdict::Off, FollowReason::NoFollowDialog};
    if (front == nullptr) return {FollowVerdict::Off, FollowReason::NoCitizen};
    if (front == dead) return {FollowVerdict::Off, FollowReason::EntityDead};
    if (front != latched && !inTable(front)) return {FollowVerdict::Off, FollowReason::EntityGone};
    if (marked(front)) return {FollowVerdict::Off, FollowReason::EntityMarked};
    return {front == latched ? FollowVerdict::Keep : FollowVerdict::Latch, FollowReason::None};
}

}  // namespace EyeCamera
}  // namespace Ext
