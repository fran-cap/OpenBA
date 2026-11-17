// ============================================================================
// src/ext/better_camera/better_camera.cpp -- ext.better_camera (see better_camera.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCAM.md. The one part of the row that reads the game: the far
// plane of the game being drawn.
// ============================================================================
#include "ext/better_camera/better_camera.h"

#include "ext/registry.h"
#include "Game/GameUpdateLoop.h"
#include "Game/WorldGeometry.h"

namespace Ext {
namespace BetterCamera {

float FrameFarPlane(float retailFar) {
    if (!Ext::Ext().better_camera) return retailFar;
    const GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    if (loop == nullptr || loop->m_worldGeometry == nullptr) return retailFar;
    // `stride` samples span stride - 1 world units, centred on the origin (ext/map_edge_fade/edge_math.h).
    const int stride = loop->m_worldGeometry->_heightRowStride;
    const float half = stride > 1 ? static_cast<float>(stride - 1) * 0.5f : 0.0f;
    return FarPlane(true, retailFar, half);
}

}  // namespace BetterCamera
}  // namespace Ext
