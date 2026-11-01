// ============================================================================
// src/ext/tweak/ext_tweaks.h -- the live-tunable constants of the graphics extensions
// ============================================================================
// NOT RETAIL. DEBUG FLAVOUR ONLY (CMake option BANISHED_TWEAK_OVERLAY; block TWEAKUI,
// docs/reference/tweak_overlay.md). Without the option this header is empty and
// ext_tweaks.cpp is not compiled: the extensions read their constexpr constants as before.
//
// The values the debug tweak overlay (src/diag/tweak_overlay.cpp) moves that have no
// settings key: ext.ambient_occlusion's ASSAO parameters and fog fade, ext.better_trees'
// distance-fill k and floor and its shadow basis / margin / jitter / noise / noiseScale. Defaults = the constants the faithful-to-today code uses
// (ao_math.h, better_trees.h), so an untouched overlay changes nothing.
//
// Readers: Ext::AmbientOcclusion::AoMath::FrameFor (main thread, once per frame), Ext::AmbientOcclusion::AoMath::FadeFromFog (the AO
// pass: the render thread under ext.render_pipe), Ext::BetterTrees::Sync (main thread).
// Writer: the overlay, on the presenting thread. A copy under a mutex each way, so no
// reader sees a half-written struct. The row-off paths never read it.
// ============================================================================
#pragma once

#if BANISHED_TWEAK_OVERLAY

#include "ext/ambient_occlusion/ao_math.h"
#include "ext/better_trees/better_trees.h"
#include "better_trees/receiver_rewrite.h"
#include "ext/better_water/shore_params.h"   // ext.better_water's look (BETTERWATER)   // src/render/common: BbsRewrite::kDefaultParams (pure, no D3D)

namespace Ext {
namespace Tweak {

struct Ao {
    float radiusStreet          = Ext::AmbientOcclusion::AoMath::kRadiusStreet;     // world units at the closest zoom
    float radiusOverview        = Ext::AmbientOcclusion::AoMath::kRadiusOverview;   // world units at the overview
    float shadowMultiplier      = Ext::AmbientOcclusion::AoMath::kShadowMultiplier; // x ext.ao_opacity / 100
    float shadowPower           = Ext::AmbientOcclusion::AoMath::kShadowPower;
    float horizonAngleThreshold = Ext::AmbientOcclusion::AoMath::kHorizonAngleThreshold;
    float sharpness             = Ext::AmbientOcclusion::AoMath::kSharpness;
    float detailShadowStrength  = Ext::AmbientOcclusion::AoMath::kDetailShadowStrength;
    int   blurPasses            = -1;     // -1 = the tier's (Ext::AmbientOcclusion::AoMath::TierFor), else 0..6
    bool  fogFade               = true;   // false: the zoom fade only (FadeFromFog not applied)
    float fogFromScale          = 1.0f;   // FadeOutFrom = fogNear x this
    float fogToScale            = 1.0f;   // FadeOutTo   = fogFar  x this
    float fadeCeiling           = Ext::AmbientOcclusion::AoMath::kFogFadeCeiling;   // FadeOutTo never past this (the sky at 512)
};

struct Faa {
    float fillK     = BetterTrees::kDistanceFillK;
    float fillFloor = BetterTrees::kDistanceFillFloor;
};

struct Water {
    BetterWater::ShoreParams p;   // ext.better_water's shore look, read by DrawShore every water range
    BetterWater::StripParams strip;   // Fancy's shore strip look, read by DrawStrip once per frame
};
struct Bbs {
    BbsRewrite::Params p = BbsRewrite::kDefaultParams;   // ext.better_trees' basis / margin / jitter / noise / noiseScale (TS-TWEAK)
};

Ao   AoValues();
void SetAo(const Ao& v);
Faa  FaaValues();
void SetFaa(const Faa& v);
Bbs  BbsValues();
void SetBbs(const Bbs& v);
Water WaterValues();
void  SetWater(const Water& v);

}  // namespace Tweak
}  // namespace Ext

#endif  // BANISHED_TWEAK_OVERLAY
