#pragma once
// ============================================================================
// src/ext/fancy_terrain/terrain_params.h -- ext.fancy_terrain's look (NOT RETAIL)
// ============================================================================
// The defaults the pass draws with (src/render/common/fancy_terrain/terrain_light.hlsl has what each does).
// The debug tweak overlay moves a copy of them (Ext::Tweak::TerrainValues, debug flavour only); every other
// build reads these. `radius` is the horizon search in heightmap vertices: a change rebuilds the map.
// ============================================================================

namespace Ext {
namespace FancyTerrain {

struct Look {
    float ambientShare = 0.40f;   // the ambient part of the ground's light (the rest is the sun's, times N.L)
    float skyStrength  = 0.90f;   // how much a valley floor / the foot of a cliff loses of the open sky's light
    float sunStrength  = 0.90f;   // the sun's share taken away where the terrain hides it, far from the camera
    float sunNear      = 0.26f;   // ...and the share of that near the camera (the retail shadow map shadows there)
    float nearDistance = 120.0f;  // the distance (world units) where the sun shadow reaches its full strength
    float softness     = 0.06f;   // the sun's edge around the horizon angle (radians)
    float heightFade   = 4.9f;    // the height above the ground (world units) over which the effect fades
    float bounce       = 0.60f;   // the share of the darkening given back in the ground's colour
    float tint[3]      = { 0.62f, 0.52f, 0.38f };   // the bounce colour
    float fogScale     = 4.4f;    // the fog's fade range (near, far) times this: where the effect fades out
    int   radius       = 64;      // the horizon search (vertices)
};

}  // namespace FancyTerrain
}  // namespace Ext
