// ============================================================================
// src/ext/winter_ice/winter_ice.h -- ext.winter_ice (class A, default OFF): water freezes in winter
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md.
//
// When the town's temperature stays below 32 F the lakes and rivers ice over: first in the shallows
// along the bank, then over the deep middle, which closes in drifting patches; in spring the shallow
// sides open first and the middle goes last (ice_model.h). Open water stays round any building standing
// in the water (a fishing dock, a trading post) and, while the town has a trading post, along the trade
// boats' channel (open_mask.h). Snow banks against the shore while the ground is snowy.
//
// Drawing only (class A): nothing is saved and the sim is untouched (the boats sail and the citizens
// walk where they always did). A town the row first sees (a load, a new game, the row turned on) starts
// from a guess made from the temperature (ice_model.h Guess). Phase 2 (walkable ice, a class C row of
// its own) will save the fronts.
//
// THE SEAM is WorldGeometry's contour-range draw (geometry_draw.cpp, modes 0x6000 rivers / 0x8000
// lakes): after retail's DrawIndexed and ext.better_water's shore pass, `if (Drawing()) DrawIce(...)`.
// The pass redraws the range with the water material's vertex layout, vertex program and render state
// and our pixel program (src/render/common/winter_ice/ice_overlay.hlsl), our textures bound at t4 / t6 /
// t7 for its draw alone. The faithful path pays one relaxed load per water range.
//
// THREADS. BeginFrame (main thread, GameUpdateLoop's per-frame ext block) advances the ice, builds
// the textures and the open-water mask and publishes them; DrawIce runs on the drawing thread (the
// main thread, or the render thread under ext.render_pipe) and only reads what was published.
// ============================================================================
#pragma once

#include <cstdint>

namespace Video { class Context; }
class WorldGeometry;

namespace Ext {
namespace WinterIce {

// The look: the ice program's MaterialConstants (b1, four float4s in this order: ice_overlay.hlsl
// mc0..mc3). The fronts and the position transform are filled per draw; the rest are the defaults
// (the user's set from the debug overlay, 2026-11-06).
struct IceParams {
    // mc0
    float freeze   = 0.0f;    // the freeze front, depth units (ice_model.h)
    float thaw     = 0.0f;    // the thaw front
    float edgeAmp  = 0.043f;  // how far the fronts wander, depth units
    float bankSnow = 1.0f;    // snow banked against the shore, times the ground's snow cover
    // mc1: filled per draw. The world position is the view direction (world - camera, the water vertex
    // programs' TEXCOORD2) plus the camera; cells = world + the map's half edge.
    float offsetX  = 0.0f;    // camera x + the map's half edge
    float offsetY  = 0.0f;    // camera y + the map's half edge
    float edgeSoft = 0.33f;   // the open-water edge's soft band (0 = a hard cut); the shader reads the
                              // mask's size itself (GetDimensions)
    float tile     = 10.3f;   // world units per repeat of the ice texture
    // mc2
    float thinR    = 0.161f;  // new, thin ice over dark water
    float thinG    = 0.239f;
    float thinB    = 0.290f;
    float reflect  = 0.42f;   // the sheen on smooth bare ice
    // mc3
    float frost    = 0.38f;   // pale haze over thick ice
    float normal   = 0.73f;   // the ice normal map's strength
    float inner    = 0.48f;   // the inner layer's depth (world units of parallax)
    float dusting  = 0.76f;   // patches of snow over the ice, times the ground's snow cover
};

// The open-water edge's noise: b0 userPosition.yzw for the pass's draw (only .x, the snow cover, is the
// scene's; put back after). The softness is IceParams::edgeSoft.
struct IceEdge {
    float wobble     = 0.52f;   // how far a broad wobble moves the edge in and out (mask units)
    float wobbleSize = 2.24f;   // the wobble's size, cells
    float grain      = 0.33f;   // a fine roughness on top
};

// The boat channel's shape (a new mask when it changes).
struct ChannelShape {
    float halfWidth  = 1.46f;   // cells either side of the line (open_mask.h kChannelFull)
    float centrePull = 1.0f;    // how hard the route keeps to the middle of the water (ext/boat_lane/boat_lane.h)
    float smoothing  = 4.0f;    // cells between the spline's keys (below 2: the cell path itself)
    bool operator==(const ChannelShape& o) const {
        return halfWidth == o.halfWidth && centrePull == o.centrePull && smoothing == o.smoothing;
    }
    bool operator!=(const ChannelShape& o) const { return !(*this == o); }
};

// The row (ext.winter_ice), as the Extensions dialog leaves it.
bool Enabled();

// Main thread, once per frame (GameUpdateLoop's ext block): reads the row, advances the ice from the
// calendar and the temperature, keeps the textures and the open-water mask.
void BeginFrame();

// Drawing thread: true when `geo`'s water ranges get the ice pass this frame: the row is on, there is
// ice, the textures exist, and `geo` is the town's own world, whose per-frame call (BeginFrame) ran
// within the last second. The main menu's backdrop worlds draw through the same seam but have no town
// loop: after a game is left, the last town's state must not reach them.
bool Drawing(const WorldGeometry& geo);

// Drawing thread, right after a water range's DrawIndexed (WorldGeometry's 0x6000 / 0x8000 draw): the range
// again with the ice program. The arguments are that DrawIndexed's.
void DrawIce(Video::Context& context, int32_t a, int32_t b, int32_t c, int32_t d);

// Engine::MainLoop::_extVideoLost / _extVideoRestored (registry_boot.cpp): our textures released on
// the lost device and made again on the new one; the program twins are made again on first use.
void OnVideoLost();
void OnVideoRestored();

}  // namespace WinterIce
}  // namespace Ext
