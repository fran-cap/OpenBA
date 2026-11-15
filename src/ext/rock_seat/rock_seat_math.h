// ============================================================================
// src/ext/rock_seat/rock_seat_math.h -- fix.rock_seat: the pure seating math
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ROCKSEAT.md. Standard C++ over the
// engine's Math value classes only, so tests/unit/ext_rock_seat_test.cpp links
// it without the world. The glue (which models, the terrain query, the mesh
// transform write) is rock_seat.cpp.
//
// THE PROBLEM. Retail places a rock (NaturalResourceComponent::PlaceAtCell,
// 0x140126540) at the terrain height of its CELL CENTRE, jitters x / y after
// the height was taken, and never tilts it (pitch 0). The rock meshes are open
// shells whose bottom ring sits at the model origin, so on a slope the
// downhill side of the ring floats and the sun shows the ground under it
// (SHADOWCONTACT.md §3).
//
// THE TWO VARIANTS (the valued key ext.rock_seat_mode):
//   kSink  keep retail's rotation; put the origin at the LOWEST terrain height
//          under the bottom ring (an ellipse inscribed in the mesh's local
//          AABB, turned by the rock's yaw), so no part of the ring floats.
//   kTilt  turn the rock's up axis onto the terrain normal (a least-squares
//          plane through the heights under the ring), keeping its yaw, capped
//          at kMaxTiltDeg, then seat it so the lowest ring point meets the
//          ground, and sink it a further TiltSink(f): kTiltSink, but never more
//          than kTiltSinkFraction of the rock's height (the user's "minor minor
//          sink": it hides the last sliver of rim where the ground curves
//          between ring samples, and keeps the small ore pieces visible).
//
// CONVENTIONS (the engine's, src/engine/math/transform.cpp): Z is up; the
// engine's `a * b` composes so that (a * b).Rotate(v) == a.Rotate(b.Rotate(v));
// Rotate(v) is the rotation the mesh is drawn with (Transform * Vector3).
// ============================================================================
#pragma once

#include <cstdint>

#include "math/Math.h"

namespace Ext {
namespace RockSeat {

// ext.rock_seat_mode values.
constexpr int32_t kSink = 1;
constexpr int32_t kTilt = 2;

constexpr int   kRingSamples = 16;     // points on the bottom ring
constexpr float kMaxTiltDeg  = 30.0f;  // the tilt cap (NaturalResourceOre spawns up to ~41 degrees)
constexpr float kTiltSink    = 0.06f;  // world units the tilted rock goes below its lowest ring point
constexpr float kTiltSinkFraction = 0.12f;  // ... at most this fraction of its height (ore pieces are 0.26 .. 0.4 tall)

// The terrain height at world (x, y).
using HeightFn = float (*)(void* ctx, float x, float y);

// The bottom ring in model space: an ellipse in the z = 0 plane centred at
// (cx, cy) with semi-axes (ax, ay), from the mesh's local AABB times the
// instance scale.
struct Footprint {
    float cx;
    float cy;
    float ax;
    float ay;
    float height;   // the mesh's local AABB height times the scale (TiltSink)
};

Footprint FootprintFromBounds(const Math::Vector3& boundsMin, const Math::Vector3& boundsMax, float scale);

// The tilt variant's extra sink for this footprint: min(kTiltSink, kTiltSinkFraction * height).
float TiltSink(const Footprint& f);

// Ring point i of kRingSamples, in model space (z = 0).
Math::Vector3 RingPoint(const Footprint& f, int i);

// The lowest terrain height under the ring of a rock drawn with rotation `rot`
// at world (x, y), and under (x, y) itself.
float MinHeightUnder(const Math::Quaternion& rot, float x, float y, const Footprint& f, HeightFn height, void* ctx);

// Least-squares plane z = a + sx * dx + sy * dy through n points (dx, dy, z).
// False (slopes 0) when the points do not span a plane.
bool FitPlane(const float* dx, const float* dy, const float* z, int n, float* sx, float* sy);

// The unit normal of the plane with slopes (sx, sy): normalize(-sx, -sy, 1).
Math::Vector3 NormalFromSlopes(float sx, float sy);

// The rotation taking world up (0, 0, 1) onto `n` about the axis up x n, in the
// engine's convention (TiltTo(n).Rotate(up) == n), the angle capped at
// `maxTiltDeg`. Identity for a normal at or below the horizontal (n.z <= 0), or within
// 1e-6 of up.
Math::Quaternion TiltTo(const Math::Vector3& n, float maxTiltDeg);

// The origin height that puts the lowest point of the ring, drawn with `rot` at
// world (x, y), on the terrain: min over the ring of (height under the point
// - the point's height above the origin), and height(x, y) itself.
float SeatHeight(const Math::Quaternion& rot, float x, float y, const Footprint& f, HeightFn height, void* ctx);

// The seated render transform for `placed` (the transform retail computed and
// serializes): its x / y are kept, its z and (kTilt) rotation replaced.
Math::Transform Seat(const Math::Transform& placed, const Footprint& f, int32_t mode, HeightFn height, void* ctx);

}  // namespace RockSeat
}  // namespace Ext
