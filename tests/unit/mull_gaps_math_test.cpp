// ============================================================================
// tests/unit/mull_gaps_math_test.cpp -- src/engine/math cases the Mull run found unchecked
// ============================================================================
// The first tools/mull/run.sh run (tools/mull/README.md) left most arithmetic mutants
// of the matrix, transform and quaternion bodies alive: the existing tests feed them
// identities, axis-aligned values and zeros, where `a + b` and `a - b`, or `x * 2` and
// `x / 2`, give the same answer. Each case here checks a body against an independent
// property on general values (an inverse undoes, a composition equals the two steps,
// a matrix moves a point where its transform does), within a float tolerance, so the
// expected values do not restate the body under test.
// ============================================================================
#include "unit.h"

#include "math/Math.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using Math::Matrix;
using Math::Quaternion;
using Math::Scalar;
using Math::Transform;
using Math::Transform2;
using Math::Transform3;
using Math::Vector2;
using Math::Vector3;
using Math::Vector4;

namespace {

constexpr float kTol = 1e-4f;

bool Near(float a, float b, float tol = kTol) {
    return std::fabs(a - b) <= tol * (1.0f + std::fabs(b));
}

bool Near3(const Vector3& a, const Vector3& b, float tol = kTol) {
    return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol);
}

Scalar S(float v) {
    Scalar s;
    s.v = v;
    return s;
}

Vector3 V3(float x, float y, float z) {
    Vector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

Matrix General() {
    // Well conditioned, no symmetry, no zero cell and no +-1 (x * 1 == x / 1).
    const float cells[16] = {4.0f, 1.5f, -2.0f, 0.5f,
                             0.25f, 3.0f, 1.75f, -1.25f,
                             -1.5f, 0.75f, 5.0f, 2.0f,
                             1.5f, -0.5f, 0.25f, 2.5f};
    Matrix m;
    for (int i = 0; i < 16; ++i)
        m.m[i] = cells[i];
    return m;
}

Matrix Other() {
    const float cells[16] = {1.0f, -2.0f, 0.5f, 3.0f,
                             2.5f, 0.25f, -1.0f, 1.0f,
                             0.75f, 1.5f, 2.0f, -0.5f,
                             -3.0f, 1.25f, 0.5f, 1.0f};
    Matrix m;
    for (int i = 0; i < 16; ++i)
        m.m[i] = cells[i];
    return m;
}

Transform Rigid(float yaw, float pitch, const Vector3& t) {
    return Transform::FromYawPitchOffset(S(yaw), S(pitch), t);
}

float QuatLength(const Quaternion& q) {
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

// A rotation about no coordinate axis (every basis vector has three non-zero components).
Transform Oblique(const Vector3& t) {
    const float x = 0.3f;
    const float y = -0.5f;
    const float z = 0.4f;
    const float w = 0.7f;
    const float len = std::sqrt(x * x + y * y + z * z + w * w);
    Transform r;
    r.rotation = Quaternion(x / len, y / len, z / len, w / len);
    r.translation = t;
    return r;
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

Vector3 Unit(const Vector3& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return V3(v.x / len, v.y / len, v.z / len);
}

float BitsToFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The pole threshold FromDirection and Transform3::FromLookAt compare against
// (0x180161698, transform.cpp kNearParallelPosBits / from_lookat.cpp kPosPole).
const float kPole = BitsToFloat(0x3f7fbe77u);

// The reference "up" both bodies pick: world +Z, unless the direction is within the pole
// threshold of it (strictly beyond), where it is -Y (towards +Z) or +Y (towards -Z).
Vector3 RefUp(float dz) {
    if (dz > kPole)
        return V3(0.0f, -1.0f, 0.0f);
    if (dz < -kPole)
        return V3(0.0f, 1.0f, 0.0f);
    return V3(0.0f, 0.0f, 1.0f);
}

}  // namespace

UNIT_TEST(mull_math_matrix_product_matches_row_major_definition) {
    const Matrix a = General();
    const Matrix b = Other();
    const Matrix p = a * b;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float want = 0.0f;
            for (int k = 0; k < 4; ++k)
                want += a.m[r * 4 + k] * b.m[k * 4 + c];
            CHECK(Near(p.m[r * 4 + c], want));
        }
    }
}

UNIT_TEST(mull_math_matrix_row_vector_product) {
    const Matrix a = General();
    Vector4 v;
    v.x = 1.5f;
    v.y = -2.0f;
    v.z = 0.5f;
    v.w = 3.0f;
    const Vector4 r = a * v;
    const float in[4] = {v.x, v.y, v.z, v.w};
    const float out[4] = {r.x, r.y, r.z, r.w};
    for (int c = 0; c < 4; ++c) {
        float want = 0.0f;
        for (int k = 0; k < 4; ++k)
            want += in[k] * a.m[k * 4 + c];
        CHECK(Near(out[c], want));
    }
}

UNIT_TEST(mull_math_matrix_inverse_undoes_a_general_matrix) {
    const Matrix a = General();
    const Matrix inv = a.Inverse();
    const Matrix left = inv * a;
    const Matrix right = a * inv;
    for (int i = 0; i < 16; ++i) {
        const float id = (i % 5 == 0) ? 1.0f : 0.0f;
        CHECK(std::fabs(left.m[i] - id) < 1e-4f);
        CHECK(std::fabs(right.m[i] - id) < 1e-4f);
    }
}

UNIT_TEST(mull_math_transform2_compose_applies_this_then_other) {
    Vector2 offA;
    offA.x = 1.5f;
    offA.y = -0.5f;
    Vector2 offB;
    offB.x = -2.0f;
    offB.y = 3.0f;
    const Transform2 a(S(35.0f), offA);
    const Transform2 b(S(-70.0f), offB);
    Vector2 v;
    v.x = 0.75f;
    v.y = 2.25f;
    const Vector2 composed = (a * b) * v;
    const Vector2 stepwise = b * (a * v);
    CHECK(Near(composed.x, stepwise.x));
    CHECK(Near(composed.y, stepwise.y));
    // And a point moves by rotate-then-offset: cos/sin of 35 degrees.
    const float c = std::cos(35.0f * 3.14159265f / 180.0f);
    const float s = std::sin(35.0f * 3.14159265f / 180.0f);
    const Vector2 moved = a * v;
    CHECK(Near(moved.x, v.x * c - v.y * s + offA.x));
    CHECK(Near(moved.y, v.x * s + v.y * c + offA.y));
}

UNIT_TEST(mull_math_transform3_point_and_rotate_general_basis) {
    Transform3 t;
    t.axisX = V3(1.0f, 2.0f, 3.0f);
    t.axisY = V3(-0.5f, 4.0f, 1.5f);
    t.axisZ = V3(2.5f, -1.0f, 0.25f);
    t.translation = V3(10.0f, -20.0f, 30.0f);
    const Vector3 v = V3(0.5f, -1.5f, 2.0f);
    const Vector3 want = V3(v.x * 1.0f + v.y * -0.5f + v.z * 2.5f,
                            v.x * 2.0f + v.y * 4.0f + v.z * -1.0f,
                            v.x * 3.0f + v.y * 1.5f + v.z * 0.25f);
    CHECK(Near3(t.Rotate(v), want));
    CHECK(Near3(t * v, V3(want.x + 10.0f, want.y - 20.0f, want.z + 30.0f)));
}

UNIT_TEST(mull_math_transform_as_matrix_moves_points_like_the_transform) {
    const Transform t = Rigid(40.0f, 30.0f, V3(1.0f, -2.0f, 3.5f));
    const Matrix m = t.AsMatrix();
    const Vector3 points[3] = {V3(1.0f, 0.0f, 0.0f), V3(0.3f, -1.2f, 2.0f), V3(-4.0f, 0.5f, 1.0f)};
    for (const Vector3& p : points) {
        Vector4 h;
        h.x = p.x;
        h.y = p.y;
        h.z = p.z;
        h.w = 1.0f;
        const Vector4 r = m * h;
        CHECK(Near3(V3(r.x, r.y, r.z), t * p));
        CHECK(Near(r.w, 1.0f));
    }
}

UNIT_TEST(mull_math_transform_inverse_undoes) {
    const Transform t = Rigid(-25.0f, 60.0f, V3(4.0f, 1.5f, -7.0f));
    const Transform inv = t.Inverse();
    const Vector3 p = V3(0.3f, -1.2f, 2.0f);
    CHECK(Near3(inv * (t * p), p));
    CHECK(Near3(t * (inv * p), p));
}

UNIT_TEST(mull_math_transform3_inverse_undoes) {
    const Transform t = Oblique(V3(-3.0f, 2.0f, 5.0f));
    const Transform3 t3 = t.AsTransform3();
    const Transform3 inv = t3.Inverse();
    const Vector3 p = V3(1.25f, 0.5f, -2.0f);
    CHECK(Near3(inv * (t3 * p), p));
    CHECK(Near3(t3 * p, t * p));
}

UNIT_TEST(mull_math_transform_compose_applies_this_then_other) {
    const Transform a = Rigid(40.0f, 30.0f, V3(1.0f, -2.0f, 3.5f));
    const Transform b = Rigid(-110.0f, 15.0f, V3(-6.0f, 0.5f, 2.0f));
    const Vector3 p = V3(0.3f, -1.2f, 2.0f);
    CHECK(Near3((a * b) * p, b * (a * p)));
}

// FromYawPitch and FromYawPitchOffset are the same rotation written twice in retail.
UNIT_TEST(mull_math_from_yaw_pitch_agrees_with_offset_form) {
    const float cases[3][2] = {{40.0f, 30.0f}, {-75.0f, 50.0f}, {120.0f, -20.0f}};
    for (const auto& c : cases) {
        const Quaternion q = Quaternion::FromYawPitch(S(c[0]), S(c[1]));
        const Transform t = Transform::FromYawPitchOffset(S(c[0]), S(c[1]), V3(0.0f, 0.0f, 0.0f));
        CHECK(Near(q.x, t.rotation.x));
        CHECK(Near(q.y, t.rotation.y));
        CHECK(Near(q.z, t.rotation.z));
        CHECK(Near(q.w, t.rotation.w));
        CHECK(Near(QuatLength(q), 1.0f));
        // Half angles: the pure-pitch quaternion's w is cos(pitch / 2).
        const Quaternion p = Quaternion::FromYawPitch(S(0.0f), S(c[1]));
        CHECK(Near(p.w, std::cos(c[1] * 0.5f * 3.14159265f / 180.0f)));
        CHECK(Near(p.x, -std::sin(c[1] * 0.5f * 3.14159265f / 180.0f)));
    }
    const Quaternion yp = Quaternion::FromYawPitch(S(40.0f), S(30.0f));
    const float hy = 20.0f * 3.14159265f / 180.0f;
    const float hp = 15.0f * 3.14159265f / 180.0f;
    CHECK(Near(yp.x, -(std::cos(hy) * std::sin(hp))));
    CHECK(Near(yp.y, -(std::sin(hp) * std::sin(hy))));
    const Transform yt = Transform::FromYawPitchOffset(S(40.0f), S(30.0f), V3(0.0f, 0.0f, 0.0f));
    CHECK(Near(yt.rotation.x, -(std::cos(hy) * std::sin(hp))));
    CHECK(Near(yt.rotation.y, -(std::sin(hp) * std::sin(hy))));
}

// FromDirection / FromLookAt: the rotation takes the local forward axis (+Y, the basis
// FromDirection hands the Quaternion constructor as `forward`) onto the direction.
UNIT_TEST(mull_math_from_direction_and_look_at_aim_forward) {
    // General directions, both poles, and a z of exactly the threshold on either side
    // (not beyond it, so the reference stays +Z there).
    const float side = std::sqrt(1.0f - kPole * kPole);
    const Vector3 dirs[7] = {V3(0.48f, -0.6f, 0.64f), V3(-0.8f, 0.36f, -0.48f), V3(0.6f, 0.8f, 0.0f),
                             Unit(V3(0.03f, 0.02f, 1.0f)), Unit(V3(-0.02f, 0.03f, -1.0f)),
                             V3(side, 0.0f, kPole), V3(side, 0.0f, -kPole)};
    for (const Vector3& d : dirs) {
        const Transform t = Transform::FromDirection(d, V3(1.0f, 2.0f, 3.0f));
        // Local +X is right = d x ref, +Y the direction, +Z up = right x d.
        const Vector3 right = Unit(Cross(d, RefUp(d.z)));
        const Vector3 up = Unit(Cross(right, d));
        CHECK(Near3(t.rotation.Rotate(V3(0.0f, 1.0f, 0.0f)), d, 1e-3f));
        CHECK(Near3(t.rotation.Rotate(V3(1.0f, 0.0f, 0.0f)), right, 1e-3f));
        CHECK(Near3(t.rotation.Rotate(V3(0.0f, 0.0f, 1.0f)), up, 1e-3f));
        CHECK(Near(QuatLength(t.rotation), 1.0f));
        CHECK(Near3(t.translation, V3(1.0f, 2.0f, 3.0f)));
    }
    const Vector3 from = V3(1.0f, -2.0f, 0.5f);
    const Vector3 to = V3(4.0f, 2.0f, -1.5f);
    const Transform look = Transform::FromLookAt(from, to);
    const float dx = 3.0f;
    const float dy = 4.0f;
    const float dz = -2.0f;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    CHECK(Near3(look.rotation.Rotate(V3(0.0f, 1.0f, 0.0f)), V3(dx / len, dy / len, dz / len)));
    CHECK(Near3(look.translation, from));
}

// Transform3::FromLookAt: axisY is the normalised forward vector, axisX = forward x up
// and axisZ = axisX x forward, normalised, with up chosen as in RefUp. Off the poles, on
// both poles, and close to (not on) each pole so every term of both cross products counts.
UNIT_TEST(mull_math_transform3_from_look_at_basis) {
    const Vector3 pos = V3(1.0f, 2.0f, 3.0f);
    const Vector3 targets[5] = {V3(4.0f, -2.0f, 4.0f), V3(1.0f, 2.0f, 9.0f), V3(1.0f, 2.0f, -9.0f),
                                V3(1.15f, 2.1f, 8.0f), V3(0.9f, 2.12f, -2.0f)};
    for (const Vector3& tg : targets) {
        const Transform3 t = Transform3::FromLookAt(pos, tg);
        const Vector3 f = Unit(V3(tg.x - pos.x, tg.y - pos.y, tg.z - pos.z));
        const Vector3 right = Unit(Cross(f, RefUp(f.z)));
        const Vector3 up = Unit(Cross(right, f));
        CHECK(Near3(t.axisY, f));
        CHECK(Near3(t.axisX, right));
        CHECK(Near3(t.axisZ, up));
        CHECK(Near3(t.translation, pos));
    }
}

// Quaternion::Lerp: normalise(a * (1 - t) + b * +-t), the sign of b's weight from the sign
// of the 4-term dot (negated unless the dot is > 0). The pairs are chosen so that dropping
// or flipping any one term of the dot flips its sign.
UNIT_TEST(mull_math_quaternion_lerp_general_and_dot_sign) {
    const Quaternion pairs[6][2] = {
        {Quaternion::FromYawPitch(S(30.0f), S(10.0f)), Quaternion::FromYawPitch(S(-50.0f), S(40.0f))},
        {Quaternion(0.6f, 0.8f, 0.0f, 0.0f), Quaternion(0.0f, 1.0f, 0.0f, 0.0f)},
        {Quaternion(0.0f, 0.6f, 0.8f, 0.0f), Quaternion(0.0f, 0.0f, 1.0f, 0.0f)},
        {Quaternion(0.6f, 0.0f, 0.0f, -0.8f), Quaternion(0.9f, 0.0f, 0.0f, 0.4f)},
        {Quaternion(1.0f, 0.0f, 0.0f, 0.0f), Quaternion(0.0f, 0.0f, 0.0f, 1.0f)},   // dot exactly 0
        {Quaternion(0.6f, 0.0f, 0.8f, 0.0f), Quaternion(-0.6f, 0.0f, 0.8f, 0.0f)}};
    for (const auto& pr : pairs) {
        const Quaternion& a = pr[0];
        const Quaternion& b = pr[1];
        const Quaternion r = a.Lerp(b, S(0.3f));
        const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        const float sb = dot > 0.0f ? 0.3f : -0.3f;
        const float wx = a.x * 0.7f + b.x * sb;
        const float wy = a.y * 0.7f + b.y * sb;
        const float wz = a.z * 0.7f + b.z * sb;
        const float ww = a.w * 0.7f + b.w * sb;
        const float wl = std::sqrt(wx * wx + wy * wy + wz * wz + ww * ww);
        CHECK(Near(r.x, wx / wl));
        CHECK(Near(r.y, wy / wl));
        CHECK(Near(r.z, wz / wl));
        CHECK(Near(r.w, ww / wl));
    }
}
