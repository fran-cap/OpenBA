// ============================================================================
// src/ext/ambient_occlusion/ao_math.cpp -- ao_math.h
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/handins/AOCORE.md).
// ============================================================================
#include "ext/ambient_occlusion/ao_math.h"

#include "ext/ambient_occlusion/params.h"
#include "graphics/renderer_ext_ao.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug tweak overlay's values (debug flavour only)
#endif

namespace Ext {
namespace AmbientOcclusion {
namespace AoMath {

bool TierFor(int quality, bool lowIsLowest, Tier& out) {
    switch (quality) {
    case 1: out = {lowIsLowest ? kAssaoLowest : kAssaoLow, 1}; return true;
    case 2: out = {kAssaoMedium, 2}; return true;
    case 3: out = {kAssaoHigh, 2}; return true;
    case 4: out = {kAssaoLowest, 1}; return true;
    default: return false;
    }
}

namespace {
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
}  // namespace

Zoom ZoomFor(float d) {
    float t = (d - kZoomMin) / (kZoomMax - kZoomMin);
    if (!(t > 0.0f)) t = 0.0f;   // also NaN -> street
    if (t > 1.0f) t = 1.0f;
    Zoom z;
    z.t           = t;
    z.radius      = Lerp(kRadiusStreet, kRadiusOverview, t);
    z.fadeOutFrom = Lerp(kFadeFromStreet, kFadeFromOverview, t);
    z.fadeOutTo   = Lerp(kFadeToStreet, kFadeToOverview, t);
    return z;
}

void AssaoProjection(const float in[16], float out[16]) {
    for (int c = 0; c < 4; ++c) {
        out[0 * 4 + c] = in[0 * 4 + c];
        out[1 * 4 + c] = in[2 * 4 + c];
        out[2 * 4 + c] = in[1 * 4 + c];
        out[3 * 4 + c] = in[3 * 4 + c];
    }
}

bool LinearizeConsts(const float m[16], float& mul, float& add) {
    int row = -1;
    for (int r = 0; r < 3; ++r)
        if (m[r * 4 + 3] != 0.0f) row = r;
    if (row < 0) return false;
    add = m[row * 4 + 2] / m[row * 4 + 3];
    mul = -m[3 * 4 + 2];
    return true;
}

void AssaoUnpack(const float lh[16], float& mul, float& add, float& tanX, float& tanY) {
    mul = -lh[3 * 4 + 2];
    add = lh[2 * 4 + 2];
    if (mul * add < 0.0f) add = -add;
    tanX = 1.0f / lh[0 * 4 + 0];
    tanY = 1.0f / lh[1 * 4 + 1];
}

bool FrameFor(const Ext::AmbientOcclusion::Params& p, float cameraDistance, Graphics::ExtAoFrame& out) {
    Tier tier;
    if (!TierFor(p.quality, kLowIsLowest, tier)) return false;
    int opacity = p.opacity;
    if (opacity <= 0) return false;
    if (opacity > 30) opacity = 30;   // ext.ao_opacity's ceiling (percent; the multiplier below stays /100)
    const Zoom z = ZoomFor(cameraDistance);
#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay's values (ext_tweaks.h; their defaults are the constants of the #else).
    const Ext::Tweak::Ao tw = Ext::Tweak::AoValues();
    out.qualityLevel          = tier.qualityLevel;
    out.blurPassCount         = tw.blurPasses >= 0 ? tw.blurPasses : tier.blurPassCount;
    out.radius                = Lerp(tw.radiusStreet, tw.radiusOverview, z.t);
    out.shadowMultiplier      = tw.shadowMultiplier * static_cast<float>(opacity) / 100.0f;
    out.shadowPower           = tw.shadowPower;
    out.horizonAngleThreshold = tw.horizonAngleThreshold;
    out.sharpness             = tw.sharpness;
    out.detailShadowStrength  = tw.detailShadowStrength;
#else
    out.qualityLevel          = tier.qualityLevel;
    out.blurPassCount         = tier.blurPassCount;
    out.radius                = z.radius;
    out.shadowMultiplier      = kShadowMultiplier * static_cast<float>(opacity) / 100.0f;
    out.shadowPower           = kShadowPower;    // ASSAO_Settings defaults (ao_math.h); AOTUNE owns them
    out.horizonAngleThreshold = kHorizonAngleThreshold;
    out.sharpness             = kSharpness;
    out.detailShadowStrength  = kDetailShadowStrength;
#endif
    out.fadeOutFrom           = z.fadeOutFrom;
    out.fadeOutTo             = z.fadeOutTo;
    return true;
}

bool FadeFromFog(float fogParamX, float fogParamY, Graphics::ExtAoFrame& frame) {
#if BANISHED_TWEAK_OVERLAY
    const Ext::Tweak::Ao tw = Ext::Tweak::AoValues();   // the debug tweak overlay (ext_tweaks.h)
    if (!tw.fogFade) return false;                      // its "fog fade" off: keep the zoom fade
#endif
    if (!(fogParamY > 0.0f)) return false;            // no fog set (or NaN): keep the zoom fade
    const float fogNear = -fogParamX;
    const float fogFar  = fogNear + 1.0f / fogParamY;
    if (!(fogFar > fogNear)) return false;
#if BANISHED_TWEAK_OVERLAY
    const float kFadeCeiling = tw.fadeCeiling;
    const float fadeNear = fogNear * tw.fogFromScale;
    const float fadeFar  = fogFar * tw.fogToScale;
#else
    constexpr float kFadeCeiling = kFogFadeCeiling;   // below the far plane: the sky stays unoccluded
    const float fadeNear = fogNear;
    const float fadeFar  = fogFar;
#endif
    float to   = fadeFar  < kFadeCeiling ? fadeFar : kFadeCeiling;
    float from = fadeNear > 0.0f ? fadeNear : 0.0f;
    if (!(from < to)) from = to * 0.5f;               // a fog that starts past the ceiling
    frame.fadeOutFrom = from;
    frame.fadeOutTo   = to;
    return true;
}

}  // namespace AoMath
}  // namespace AmbientOcclusion
}  // namespace Ext
