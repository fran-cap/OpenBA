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
#include "ext/winter_ice/winter_ice.h"      // ext.winter_ice's look (WINTERICE)
#include "ext/better_clouds/cloud_params.h" // ext.better_clouds' look (BETTERCLOUDS)
#include "ext/winter_ice/open_mask.h"       // ...and the boat channel's default width
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
    BetterWater::StripParams strip;
    BetterWater::ContactParams contact;   // Fancy's contact foam look, read by DrawShore every water range   // Fancy's shore strip look, read by DrawStrip once per frame
};
// ext.winter_ice (WINTERICE): the ice program's look, read by DrawIce every lake range, and a preview
// that replaces the fronts the weather made (and the snow cover the ice program reads), so the ice can
// be judged in any season. The preview is drawing only: nothing is saved, the sim never sees it.
struct Ice {
    WinterIce::IceParams p;     // freeze / thaw / offsetX / offsetY are the pass's own; the rest is the look
    WinterIce::IceEdge edge;    // the open-water edge's noise
    bool  preview = false;      // the fronts below instead of the weather's
    float freeze  = 1.75f;      // the preview's freeze front (>= ice_model.h kFrozen, 1.65: the whole lake)
    float thaw    = 0.0f;       // the preview's thaw front
    float snow    = -1.0f;      // the preview's snow cover 0..1; < 0 = the weather's
    WinterIce::ChannelShape channel;   // the boat channel's width, centre pull and smoothing (a new mask)
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
Ice   IceValues();
void  SetIce(const Ice& v);
// ext.better_clouds (BETTERCLOUDS): the sky program's look and the wind's speed, read by BeginFrame.
BetterClouds::Look CloudsValues();
void  SetClouds(const BetterClouds::Look& v);

}  // namespace Tweak
}  // namespace Ext

#endif  // BANISHED_TWEAK_OVERLAY
