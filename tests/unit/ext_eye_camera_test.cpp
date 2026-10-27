// ============================================================================
// tests/unit/ext_eye_camera_test.cpp -- ext.eye_camera's pure math
// ============================================================================
// src/ext/eye_camera/eye_math.{h,cpp} without the world: the yaw read back
// from a model rotation built the engine's way (Transform::FromYawPitchOffset,
// and the move tick's (0, 0, -sin(Y/2), cos(Y/2))), the heading convention
// against Vector3::FromYawPitch / Heading, the eye pose from feet + heading +
// distance, the zoom clamp, and the smoothing step (frame-rate independence,
// the short way round a turn, the teleport snap).
// ============================================================================
#include "unit.h"
#include "ext/eye_camera/eye_math.h"
#include "math/Math.h"

#include <cmath>
#include <initializer_list>
#include <string>

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool NearDeg(float a, float b, float eps = 1e-2f) {
    return std::fabs(Ext::EyeCamera::WrapDegrees(a - b)) <= eps;
}

}  // namespace

UNIT_TEST(ext_eye_camera_wrap_degrees) {
    using Ext::EyeCamera::WrapDegrees;
    CHECK(Near(WrapDegrees(0.0f), 0.0f));
    CHECK(Near(WrapDegrees(180.0f), 180.0f));
    CHECK(Near(WrapDegrees(-180.0f), 180.0f));
    CHECK(Near(WrapDegrees(190.0f), -170.0f));
    CHECK(Near(WrapDegrees(-190.0f), 170.0f));
    CHECK(Near(WrapDegrees(725.0f), 5.0f));
}

UNIT_TEST(ext_eye_camera_yaw_from_model_rotation) {
    // The engine's builder, and the move tick's hand-built quaternion.
    for (float yaw : {0.0f, 30.0f, 90.0f, 135.0f, 179.0f, -45.0f, -120.0f, 250.0f}) {
        const Math::Transform t = Math::Transform::FromYawPitchOffset(
            Math::Scalar(yaw), Math::Scalar(0.0f), Math::Vector3(1.0f, 2.0f, 3.0f));
        CHECK(NearDeg(Ext::EyeCamera::ModelYawFromQuaternion(t.rotation.z, t.rotation.w), yaw));
        // q and -q are the same rotation.
        CHECK(NearDeg(Ext::EyeCamera::ModelYawFromQuaternion(-t.rotation.z, -t.rotation.w), yaw));
        const float half = yaw * 0.5f * 0.017453292f;
        CHECK(NearDeg(Ext::EyeCamera::ModelYawFromQuaternion(-std::sin(half), std::cos(half)), yaw));
    }
    // A lerped (renderer-interpolated) model transform reads back in between.
    const Math::Transform a = Math::Transform::FromYawPitchOffset(
        Math::Scalar(10.0f), Math::Scalar(0.0f), Math::Vector3(0.0f, 0.0f, 0.0f));
    const Math::Transform b = Math::Transform::FromYawPitchOffset(
        Math::Scalar(50.0f), Math::Scalar(0.0f), Math::Vector3(2.0f, 0.0f, 0.0f));
    const Math::Transform mid = a.Lerp(b, Math::Scalar(0.5f));
    CHECK(NearDeg(Ext::EyeCamera::ModelYawFromQuaternion(mid.rotation.z, mid.rotation.w), 30.0f, 0.5f));
    CHECK(Near(mid.translation.x, 1.0f));
}

UNIT_TEST(ext_eye_camera_forward_matches_engine_heading) {
    for (float h : {0.0f, 45.0f, 90.0f, -90.0f, 170.0f}) {
        const Ext::EyeCamera::Vec3 f = Ext::EyeCamera::Forward(h);
        const Math::Vector3 e = Math::Vector3::FromYawPitch(Math::Scalar(h), Math::Scalar(0.0f));
        CHECK(Near(f.x, e.x));
        CHECK(Near(f.y, e.y));
        CHECK(Near(f.z, 0.0f));
        // Heading() (the move tick's facing -> heading) inverts it.
        CHECK(NearDeg(Math::Vector3(f.x, f.y, 0.0f).Heading(), h));
    }
}

UNIT_TEST(ext_eye_camera_pose) {
    using namespace Ext::EyeCamera;
    const Vec3 feet{10.0f, -4.0f, 2.0f};
    // First person, heading 0 (facing +Y): the eye just ahead of the face, at
    // eye height, looking level along +Y.
    Pose p = ComputePose(feet, 0.0f, 1.0f, 0.0f);
    CHECK(Near(p.eye.x, 10.0f));
    CHECK(Near(p.eye.y, -4.0f + kFaceForward));
    CHECK(Near(p.eye.z, 2.0f + kEyeHeight));
    CHECK(Near(p.look.x, 10.0f));
    CHECK(Near(p.look.y, -4.0f + kLookAhead));
    CHECK(Near(p.look.z, p.eye.z));
    // Facing +X is heading -90: backed off 2 units, the eye is behind (-X) and raised.
    p = ComputePose(feet, -90.0f, 1.0f, 2.0f);
    CHECK(Near(p.eye.x, 10.0f + (kFaceForward - 2.0f)));
    CHECK(Near(p.eye.y, -4.0f));
    CHECK(Near(p.eye.z, 2.0f + kEyeHeight + 2.0f * kRisePerDistance));
    CHECK(Near(p.look.x, 10.0f + kLookAhead));
    CHECK(Near(p.look.z, 2.0f + kEyeHeight));
    // The look point is always ahead of the eye (FromLookAt never degenerates).
    CHECK(p.look.x - p.eye.x > 1.0f);
}

UNIT_TEST(ext_eye_camera_pose_child_scale) {
    using namespace Ext::EyeCamera;
    // A child is the same mesh drawn at its age scale (SetupElder's 0.66):
    // the eye and the face offset shrink with it, about the feet.
    const Vec3 feet{0.0f, 0.0f, 1.0f};
    const float child = 0.66f;
    Pose p = ComputePose(feet, 0.0f, child, 0.0f);
    CHECK(Near(p.eye.z, 1.0f + kEyeHeight * child));
    CHECK(Near(p.eye.y, kFaceForward * child));
    CHECK(Near(p.look.z, p.eye.z));
    // Backed off, the rise per unit is the same as an adult's; only the head moved.
    const Pose adult = ComputePose(feet, 0.0f, 1.0f, 1.5f);
    p = ComputePose(feet, 0.0f, child, 1.5f);
    CHECK(Near(adult.eye.z - p.eye.z, kEyeHeight * (1.0f - child)));
    CHECK(Near(p.eye.y, kFaceForward * child - 1.5f));
}

UNIT_TEST(ext_eye_camera_shadow_fit_pose) {
    using namespace Ext::EyeCamera;
    // Facing +X (heading -90): the fit view looks at the ground kShadowFitAhead
    // ahead of the feet, from behind (-X) and above, kShadowFitPitch below level.
    const Vec3 feet{5.0f, 7.0f, 3.0f};
    const Pose p = ShadowFitPose(feet, -90.0f);
    CHECK(Near(p.look.x, 5.0f + kShadowFitAhead));
    CHECK(Near(p.look.y, 7.0f));
    CHECK(Near(p.look.z, 3.0f));
    const float dx = p.look.x - p.eye.x, dy = p.look.y - p.eye.y, dz = p.look.z - p.eye.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    CHECK(Near(len, kShadowFitDistance));
    CHECK(Near(std::asin(-dz / len) * 57.29578f, kShadowFitPitch, 1e-2f));
    CHECK(dx > 0.0f);                       // the eye is behind the target
    CHECK(p.eye.x < feet.x);                // and behind the feet: they are in its view
}

UNIT_TEST(ext_eye_camera_zoom) {
    using namespace Ext::EyeCamera;
    CHECK(Near(ZoomDistance(1.0f, 1), 1.0f - kDistanceStep));    // zoom in: closer
    CHECK(Near(ZoomDistance(1.0f, -1), 1.0f + kDistanceStep));   // zoom out: farther
    CHECK(Near(ZoomDistance(1.0f, -120), 1.0f + kDistanceStep)); // a raw wheel value counts by sign
    CHECK(Near(ZoomDistance(1.0f, 0), 1.0f));
    CHECK(Near(ZoomDistance(0.1f, 1), 0.0f));                    // first person is the floor
    CHECK(Near(ZoomDistance(kMaxDistance, -1), kMaxDistance));   // the over-the-shoulder cap
    // ext.touch_mode's held zoom (P11c): fractional steps, the same clamps.
    CHECK(Near(ZoomDistanceBy(1.0f, 0.5f), 1.0f - 0.5f * kDistanceStep));
    CHECK(Near(ZoomDistanceBy(1.0f, -0.25f), 1.0f + 0.25f * kDistanceStep));
    CHECK(Near(ZoomDistanceBy(1.0f, 0.0f), 1.0f));
    CHECK(Near(ZoomDistanceBy(0.05f, 1.0f), 0.0f));
    CHECK(Near(ZoomDistanceBy(kMaxDistance, -2.0f), kMaxDistance));
}

UNIT_TEST(ext_eye_camera_smoothing) {
    using namespace Ext::EyeCamera;
    CHECK(Near(BlendFactor(0.0f, 0.1f), 0.0f));
    CHECK(Near(BlendFactor(0.1f, 0.0f), 1.0f));
    CHECK(Near(BlendFactor(0.1f, 0.1f), 1.0f - std::exp(-1.0f)));

    // Not valid yet: the first step snaps.
    Smooth s;
    Step(s, Vec3{1.0f, 2.0f, 3.0f}, 40.0f, 1.0f, 0.016f);
    CHECK(s.valid);
    CHECK(Near(s.feet.x, 1.0f) && Near(s.heading, 40.0f) && Near(s.distance, 1.0f));

    // Frame-rate independent: two half steps == one whole step.
    Smooth a, b;
    Snap(a, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
    b = a;
    Step(a, Vec3{1.0f, 0.0f, 0.0f}, 60.0f, 2.0f, 0.1f);
    Step(b, Vec3{1.0f, 0.0f, 0.0f}, 60.0f, 2.0f, 0.05f);
    Step(b, Vec3{1.0f, 0.0f, 0.0f}, 60.0f, 2.0f, 0.05f);
    CHECK(Near(a.feet.x, b.feet.x, 1e-4f));
    CHECK(Near(a.heading, b.heading, 1e-2f));
    CHECK(Near(a.distance, b.distance, 1e-4f));
    // It moved part of the way, not all of it.
    CHECK(a.feet.x > 0.5f && a.feet.x < 1.0f);
    CHECK(a.heading > 0.0f && a.heading < 60.0f);

    // A turn across +-180 takes the short way round (170 -> -170 is +20).
    Smooth t;
    Snap(t, Vec3{0.0f, 0.0f, 0.0f}, 170.0f, 0.0f);
    Step(t, Vec3{0.0f, 0.0f, 0.0f}, -170.0f, 0.0f, 0.05f);
    CHECK(t.heading > 170.0f || t.heading < -170.0f);
    for (int i = 0; i < 200; ++i) Step(t, Vec3{0.0f, 0.0f, 0.0f}, -170.0f, 0.0f, 0.016f);
    CHECK(NearDeg(t.heading, -170.0f, 0.05f));

    // A jump past kSnapDistance (a teleport, a reload) snaps.
    Smooth j;
    Snap(j, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
    Step(j, Vec3{kSnapDistance + 1.0f, 0.0f, 0.0f}, 90.0f, 1.0f, 0.016f);
    CHECK(Near(j.feet.x, kSnapDistance + 1.0f));
    CHECK(Near(j.heading, 90.0f));
}

UNIT_TEST(ext_eye_camera_idle_citizen_stays_followed) {
    using namespace Ext::EyeCamera;
    // EYECAM2 defect 3. The weak reference's table is InternalFactory::_objects,
    // which RemoveObject (0x1800c0110) swap-removes: the LAST object moves into
    // the freed slot. Replay that: the followed entity is last when a transient
    // (a finished walk's objects) dies, so it moves. It must still resolve.
    int objs[6] = {0, 0, 0, 0, 0, 0};
    const int* table[6] = {&objs[0], &objs[1], &objs[2], &objs[3], &objs[4], &objs[5]};
    int count = 6;
    const int* followed = &objs[5];
    int hint = 5;
    CHECK(FollowSlot(table, count, followed, hint) == 5);
    auto removeAt = [&](int idx) {   // RemoveObject's two stores and the shrink
        if (idx != count - 1) table[idx] = table[count - 1];
        --count;
    };
    removeAt(1);                          // something else died; `followed` moved to 1
    hint = FollowSlot(table, count, followed, hint);
    CHECK(hint == 1);
    removeAt(4);                          // unrelated, `followed` stays put
    CHECK(FollowSlot(table, count, followed, hint) == 1);
    removeAt(1);                          // the followed object itself is destroyed
    CHECK(FollowSlot(table, count, followed, hint) == -1);

    // The idle citizen's model transform does not change: the smoothed heading
    // and feet hold exactly (no drift, no snap) however long it stands.
    Smooth s;
    Snap(s, Vec3{3.0f, 4.0f, 1.0f}, 37.0f, kDefaultDistance);
    for (int i = 0; i < 1000; ++i) Step(s, Vec3{3.0f, 4.0f, 1.0f}, 37.0f, kDefaultDistance, 0.016f);
    CHECK(s.valid);
    CHECK(Near(s.heading, 37.0f, 1e-4f));
    CHECK(Near(s.feet.x, 3.0f, 1e-5f) && Near(s.feet.y, 4.0f, 1e-5f));
}

UNIT_TEST(ext_eye_camera_follow_identity_is_the_entity) {
    using namespace Ext::EyeCamera;
    // EYECAM3 (EYECAM.md §10): the engagement decision. The identity is the
    // followed entity read from the frontmost open FollowDialog; a latched
    // entity is never asked of InternalFactory's table again (a live object can
    // drop out of it), so only the dialog closing, a missing citizen or the
    // removal mark hand back.
    int a = 0, b = 0;
    const void* A = &a;
    const void* B = &b;
    int tableAsks = 0, markAsks = 0;
    bool inTable = true, marked = false;
    auto table = [&](const void*) { ++tableAsks; return inTable; };
    auto mark = [&](const void*) { ++markAsks; return marked; };
    auto decide = [&](bool open, const void* front, const void* latched, const void* dead) {
        return DecideFollow(open, front, latched, dead, table, mark);
    };

    // A new follow latches (asked of the table once, before any dereference).
    FollowDecision d = decide(true, A, nullptr, nullptr);
    CHECK(d.verdict == FollowVerdict::Latch && tableAsks == 1);

    // Latched and idle for a long time while A drops out of the factory table
    // (EYECAM2's hand-back): kept, and the table is never asked.
    inTable = false;
    tableAsks = 0;
    for (int i = 0; i < 1000; ++i) {
        d = decide(true, A, A, nullptr);
        CHECK(d.verdict == FollowVerdict::Keep);
    }
    CHECK(tableAsks == 0);
    inTable = true;

    // The dialog re-created / another dialog on the same citizen: the front
    // entity is still A, so Keep (the glue only re-attaches).
    CHECK(decide(true, A, A, nullptr).verdict == FollowVerdict::Keep);

    // The frontmost follow now targets B: a new latch.
    CHECK(decide(true, B, A, nullptr).verdict == FollowVerdict::Latch);

    // The three hand-backs while engaged on A.
    d = decide(false, nullptr, A, nullptr);
    CHECK(d.verdict == FollowVerdict::Off && d.reason == FollowReason::NoFollowDialog);
    d = decide(true, nullptr, A, nullptr);
    CHECK(d.verdict == FollowVerdict::Off && d.reason == FollowReason::NoCitizen);
    marked = true;
    d = decide(true, A, A, nullptr);
    CHECK(d.verdict == FollowVerdict::Off && d.reason == FollowReason::EntityMarked);
    marked = false;

    // Once dead, never dereferenced again (neither the mark nor the table asked).
    markAsks = tableAsks = 0;
    d = decide(true, A, nullptr, A);
    CHECK(d.verdict == FollowVerdict::Off && d.reason == FollowReason::EntityDead);
    CHECK(markAsks == 0 && tableAsks == 0);

    // A new entity missing from the table is destroyed: no mark read.
    inTable = false;
    d = decide(true, B, nullptr, nullptr);
    CHECK(d.verdict == FollowVerdict::Off && d.reason == FollowReason::EntityGone && markAsks == 0);

    CHECK(std::string(FollowReasonName(FollowReason::EntityMarked)) == "entity-marked");
    CHECK(std::string(FollowReasonName(FollowReason::NoFollowDialog)) == "no-follow-dialog");
}
