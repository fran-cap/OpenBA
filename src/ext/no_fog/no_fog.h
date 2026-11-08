#pragma once
// ============================================================================
// src/ext/no_fog/no_fog.h -- ext.no_fog (class A) + ext.fog_distance
// ============================================================================
// NOT RETAIL. docs/extensions/handins/NOFOG.md. Pushes the fog back, up to no fog at all.
//
// RETAIL. Each frame the game blends the lighting record of the month (and, while it is shown, of every
// live weather effect) into one EnvironmentSetting: ambient colour, light colour, fog colour, fog near,
// fog far, fog maximum and the sun's heading and pitch (WeatherSimulation::BlendLighting, retail
// 0x140093ac0). The fog colour's fourth channel is the fog maximum, and the lighting shader mixes
// min((fogNear' + viewDepth) * fogScale, fogMax) of the fog colour into every lit pixel, with
// fogNear' = -fogNear and fogScale = 1 / (fogFar - fogNear) (Context::SetFog). Zoomed far out, the view
// depth is large, the mix reaches the maximum (0.95 by default) and the far map fades into haze.
//
// THE EXTENSION. With the row on, the valued key ext.fog_distance (0..100, the Enhanced tab's "Fog
// distance" slider) says how far back the fog goes:
//   * 0       retail's fog, untouched (the key's default: the row ships ON on a desktop, ruled 2026-11-08, with
//             nothing changed until the player moves the slider)
//   * 1..99   fog near and fog far are multiplied by 100 / (100 - d): 1.01x at 1, 2x at 50, 4x at 75,
//             100x at 99. The fog keeps its colour and its maximum; it starts and thickens farther out.
//   * 100     the fog maximum is 0, so the mix is 0 whatever the depth: no fog (the row's behaviour before
//             the key existed; a settings.cfg with ext.no_fog=1 and no ext.fog_distance is migrated to 100 at
//             boot, registry_boot.cpp, so it keeps no fog).
// One seam, at the end of BlendLighting, which both the shipped
// draw path (WeatherSimulation::DrawUpdate) and ext.render_pipe's recorded path call; every later reader of
// the fog (AO's fade, the clouds' fade) derives it from the same record, so they follow. With the row off,
// or on at 0, the record is retail's own, bit for bit.
//
// CLASS A. It changes the picture only: no simulation state, nothing serialized, no lock. The row and the key
// are read on each call, which is once per drawn frame, so a change takes effect at the next frame.
//
// Standard C++ only: tests/unit/ext_no_fog_test.cpp links nothing but this header.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace NoFog {

constexpr float kFogMax = 0.0f;           // the fog maximum at ext.fog_distance 100: no fog is mixed in
constexpr int32_t kDistanceRetail = 0;    // ext.fog_distance's range (registry.cpp kValueKeys) and default: retail's fog
constexpr int32_t kDistanceOff = 100;     // the top of the range: no fog

// The fog maximum at ext.fog_distance 100 (kept for the row's original contract): `retail` when the row is
// off, kFogMax when it is on.
inline float FogMax(float retail, bool on) {
    return on ? kFogMax : retail;
}

// How far fog near and far are pushed at `distance` (1..99): 100 / (100 - d). 1 at 0 and below, and the
// value at 99 above it (100 itself is "no fog", handled by Apply, never a scale).
inline float DistanceScale(int32_t distance) {
    if (distance <= kDistanceRetail) return 1.0f;
    if (distance > kDistanceOff - 1) distance = kDistanceOff - 1;
    return 100.0f / static_cast<float>(kDistanceOff - distance);
}

// The seam's whole job: the blended record's three fog values, rewritten in place. Row off, or the key at 0
// (retail's fog): untouched.
inline void Apply(float& fogNear, float& fogFar, float& fogMax, bool on, int32_t distance) {
    if (!on || distance <= kDistanceRetail) return;
    if (distance >= kDistanceOff) {
        fogMax = kFogMax;
        return;
    }
    const float s = DistanceScale(distance);
    fogNear *= s;
    fogFar *= s;
}

}  // namespace NoFog
}  // namespace Ext
