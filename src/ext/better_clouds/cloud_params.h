// ============================================================================
// src/ext/better_clouds/cloud_params.h -- the clouds' motion and the sky program's constants (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCLOUDS.md section 2. Standard C++ only (the unit tests link it).
//
// THE MOTION. Every octave of the sky program (src/render/common/better_clouds/cloud_sky.hlsl) reads the
// noise at R_o * q * F_o + off_o: q the point on the cloud layer, R_o and F_o the octave's rotation and
// frequency (kOctaves below: the program's R0..R2 / F0..F2), off_o this offset. Moving the clouds by W
// down the wind is off_o = -R_o * W * F_o, so every octave travels the one world direction although each
// is rotated in the texture. The offsets are accumulated here a frame at a time, in double, and wrapped
// to 0..1 (the texture wraps), so the program never sees a large time value and a long session keeps
// its precision. Each octave moves a little faster than the one under it (wisps run ahead of the bulk);
// the warp field drifts slower, so the shapes change as they travel.
//
// THE CLOCK. The frame time times the game's speed, capped at kMaxSpeed (the user's ruling, 2026-11-06:
// at 10x and 50x uncapped clouds race); nothing while the game is paused.
// ============================================================================
#pragma once

namespace Ext {
namespace BetterClouds {

constexpr int kMaxSpeed = 3;

// The wind on the cloud layer: direction (dome space, unit) and speed (layer units a second at 1x).
constexpr double kWindX = 0.8;
constexpr double kWindY = 0.6;
constexpr double kWindSpeed = 0.009;

// An octave of the program: rotation (cos, sin), frequency, speed relative to the wind.
struct Octave {
    double cosA;
    double sinA;
    double freq;
    double speed;
};
// Base, mid, detail; then the warp field (no rotation).
constexpr Octave kOctaves[4] = {
    { 1.000, 0.000, 1.00, 1.00 },
    { 0.809, 0.588, 2.13, 1.35 },
    { 0.276, 0.961, 4.71, 1.80 },
    { 1.000, 0.000, 0.37, 0.30 },
};

// The look the program takes from the CPU (the debug overlay moves it live: src/ext/tweak/ext_tweaks.h).
// The defaults are the shipped look (the user's pick in the overlay, 2026-11-06).
struct Look {
    float white  = 1.00f;   // the clouds' lit side (grey level; the sun's colour tints it a little)
    float shade  = 0.83f;   // the shaded side, times the lit side's white
    float skyLift = 0.02f;  // the blurred painted sky under the clouds, lifted toward white by this much
    float coverage = 0.01f; // added to the weather's coverage (0.50 clear .. 0.95 storm)
    float scale  = 0.331f;   // noise tiles per unit of the cloud layer: smaller = bigger clouds
    float speed  = 1.00f;   // times the wind (kWindSpeed)
    float morph  = 0.35f;   // how fast the shapes change as they travel: the octaves' speed differences
                            // (kOctaves .speed) times this; 0 = rigid clouds, 1 = the full spread
    // Fancy: the shadows on the ground (cloud_shadow.hlsl).
    float shadowStrength = 0.30f;    // how much of the light a full cloud takes, in clear weather
    float shadowScale    = 0.006f;   // noise tiles per world unit: one tile = about 170 units
    float shadowHeight   = 60.0f;    // the layer's height over the ground (world units): the sun's slant
    float shadowSoftness = 0.60f;    // the shadow edge's width (density units)
};

// The weather and fog terms of the shadow pass, from what the frame's draws already hold: the installed
// sky's blend (the pixel constants' userPosition x and w), the sun's height (toward the sun, z of a unit
// vector) and the fog (Video::Context::_fogParams x, y: the fog starts at -x, is full 1 / y later).
constexpr float kShadowCoverageMax = 0.70f;
struct ShadowTerms {
    float coverage;   // 0..1, the sky's coverage, capped at kShadowCoverageMax so gaps of sun remain
    float strength;   // 0..1: the look's, less in bad weather (no direct sun to block) and under a low sun;
                      // winter alone does not weaken it (the snow keeps the trees' sun shadows too)
    float fadeFrom;   // world distance from the camera where the shadows start to fade
    float fadeTo;     // and are gone
};
ShadowTerms ShadowTermsFor(const Look& look, float userX, float userW, float sunZ, float fogX, float fogY);

struct Motion {
    double off[4][2] = {};   // per octave, texture space, 0..1

    // `frameSeconds` of the frame at game speed `speed` (the simulation steps it runs; 0 = paused), the
    // wind times `windScale`, each octave's speed 1 + (its speed - 1) * `morph`.
    void Advance(double frameSeconds, int speed, double windScale = 1.0, double morph = 1.0);
};

// The sky program's MaterialConstants (b1), four float4s in its order:
//   mc0 base offset xy, mid offset xy;  mc1 detail offset xy, warp offset xy;
//   mc2 toward the sun xyz (dome space, z up, unit; straight up when there is none), the layer scale;
//   mc3 white, shade, sky lift, coverage bias (Look).
// The shadow pass reads mc0 and mc1 (the offsets) and mc2.xyz (the sun) from the same block.
void Constants(const Motion& m, const Look& look, float towardSunX, float towardSunY, float towardSunZ,
               float out[16]);

}  // namespace BetterClouds
}  // namespace Ext
