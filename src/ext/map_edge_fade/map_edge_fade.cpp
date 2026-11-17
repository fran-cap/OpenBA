// ============================================================================
// src/ext/map_edge_fade/map_edge_fade.cpp -- ext.map_edge_fade (see map_edge_fade.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPEDGE.md.
// ============================================================================
#include "ext/map_edge_fade/map_edge_fade.h"

#include "ext/map_edge_fade/edge_math.h"
#include "ext/registry.h"
#include "ext/town_screenshot/town_screenshot.h"
#include "map_edge/map_edge_backend.h"   // src/render/common: the pass (DX11, Vulkan; Metal unsupported)
#include "Game/GameUpdateLoop.h"
#include "Game/WorldGeometry.h"
#include "graphics/Camera.h"
#include "math/Math.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace MapEdgeFade {

namespace {

// The terrain heightmap's row stride armed for this frame (main thread, SceneDraw; read by the drawing
// thread's Draw of the frame; under ext.render_pipe the render thread may still draw the previous frame:
// either value is a whole frame's, and the stride only changes between games).
std::atomic<int> g_stride{0};

void Draw(Video::Context& context, const Graphics::Camera& camera) {
    const int stride = g_stride.load(std::memory_order_acquire);
    if (stride <= 1) return;
    const Look look;
    const Band band = BandFor(TerrainHalf(stride), look);
    MapEdgeBackend::Frame f = {};
    const Math::Matrix cameraToWorld = camera._cameraToWorld.AsMatrix();
    const Math::Matrix viewProjection = cameraToWorld.Inverse() * camera._projection;
    const Math::Matrix inverse = viewProjection.Inverse();
    std::memcpy(f.invViewProj, inverse.m, sizeof(f.invViewProj));
    f.map[0] = band.start;
    f.map[1] = band.invWidth;
    f.map[2] = look.horizon;
    f.camera[0] = cameraToWorld.m[12];
    f.camera[1] = cameraToWorld.m[13];
    f.camera[2] = cameraToWorld.m[14];
    MapEdgeBackend::Draw(context, f);
}

}  // namespace

SceneFn SceneDraw() {
    const GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    const WorldGeometry* geo = loop != nullptr ? loop->m_worldGeometry : nullptr;
    const bool screenshot = Ext::Ext().town_screenshot && TownScreenshot::Active();
    const bool wanted = Ext::Ext().map_edge_fade && geo != nullptr && loop->m_playerController != nullptr &&
                        geo->_heightRowStride > 1 && !screenshot;
    MapEdgeBackend::Request(wanted);
    if (!wanted || !MapEdgeBackend::Supported()) return nullptr;
    const int stride = geo->_heightRowStride;
    if (g_stride.exchange(stride, std::memory_order_acq_rel) != stride) {
        // Once per terrain size (a new game, a load).
        std::fprintf(stderr, "[MapEdgeFade] terrain stride %d: the ground ends at +-%.1f\n", stride,
                     static_cast<double>(TerrainHalf(stride)));
    }
    return &Draw;
}

}  // namespace MapEdgeFade
}  // namespace Ext
