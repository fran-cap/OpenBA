// src/ext/better_clouds/cloud_params.cpp -- cloud_params.h (BETTERCLOUDS)
//
// NOT RETAIL. Pure.

#include "ext/better_clouds/cloud_params.h"

#include <algorithm>
#include <cmath>

namespace Ext {
namespace BetterClouds {

namespace {

double Wrap01(double v) {
    v -= std::floor(v);
    return v >= 1.0 ? 0.0 : v;
}

}  // namespace

void Motion::Advance(double frameSeconds, int speed, double windScale, double morph) {
    if (speed <= 0 || !(frameSeconds > 0.0)) return;
    const double s = frameSeconds * static_cast<double>(std::min(speed, kMaxSpeed)) * windScale;
    for (int o = 0; o < 4; ++o) {
        const Octave& k = kOctaves[o];
        // This frame's travel down the wind, on the layer.
        const double octaveSpeed = 1.0 + (k.speed - 1.0) * morph;
        const double wx = kWindX * kWindSpeed * octaveSpeed * s;
        const double wy = kWindY * kWindSpeed * octaveSpeed * s;
        // off -= R * W * F, R = [[c, -s], [s, c]] (the program's float2x2 rows).
        const double rx = k.cosA * wx - k.sinA * wy;
        const double ry = k.sinA * wx + k.cosA * wy;
        off[o][0] = Wrap01(off[o][0] - rx * k.freq);
        off[o][1] = Wrap01(off[o][1] - ry * k.freq);
    }
}

void Constants(const Motion& m, const Look& look, float towardSunX, float towardSunY, float towardSunZ,
               float out[16]) {
    for (int o = 0; o < 4; ++o) {
        out[o * 2 + 0] = static_cast<float>(m.off[o][0]);
        out[o * 2 + 1] = static_cast<float>(m.off[o][1]);
    }
    const double len = std::sqrt(static_cast<double>(towardSunX) * towardSunX +
                                 static_cast<double>(towardSunY) * towardSunY +
                                 static_cast<double>(towardSunZ) * towardSunZ);
    double sx = 0.0;
    double sy = 0.0;
    double sz = 1.0;
    if (len > 1e-6) {
        sx = towardSunX / len;
        sy = towardSunY / len;
        sz = towardSunZ / len;
    }
    out[8] = static_cast<float>(sx);
    out[9] = static_cast<float>(sy);
    out[10] = static_cast<float>(sz);
    out[11] = look.scale;
    out[12] = look.white;
    out[13] = look.shade;
    out[14] = look.skyLift;
    out[15] = look.coverage;
}

ShadowTerms ShadowTermsFor(const Look& look, float userX, float userW, float sunZ, float fogX, float fogY) {
    // cloud_sky.hlsl's blend and coverage, on the CPU.
    const float t = userX * 0.75f + (1.0f - userX * 0.75f) * userW;
    const float s = t * t * (3.0f - 2.0f * t);
    ShadowTerms out;
    out.coverage = std::min(kShadowCoverageMax, std::max(0.0f, 0.50f + (0.95f - 0.50f) * s + look.coverage));
    const float sunUp = std::min(1.0f, std::max(0.0f, sunZ * 6.0f));
    const float weather = std::min(1.0f, std::max(0.0f, userW));
    out.strength = std::min(1.0f, std::max(0.0f, look.shadowStrength * (1.0f - 0.8f * weather) * sunUp));
    out.fadeFrom = 1.0e6f;
    out.fadeTo = 2.0e6f;
    if (fogY > 0.0f) {
        const float fogNear = -fogX;
        const float fogFar = fogNear + 1.0f / fogY;
        if (fogFar > fogNear) {
            out.fadeFrom = std::max(fogNear, 0.0f);
            out.fadeTo = fogFar;
            if (!(out.fadeFrom < out.fadeTo)) out.fadeFrom = out.fadeTo * 0.5f;
        }
    }
    return out;
}

}  // namespace BetterClouds
}  // namespace Ext
