// ============================================================================
// src/ext/fancy_terrain/fancy_terrain.cpp -- ext.fancy_terrain (see fancy_terrain.h)
// ============================================================================
// NOT RETAIL.
// ============================================================================
#include "ext/fancy_terrain/fancy_terrain.h"

#include "ext/fancy_terrain/horizon.h"
#include "ext/fancy_terrain/terrain_params.h"
#include "ext/better_clouds/cloud_params.h"   // ShadowTermsFor: the fog fade the clouds' shadows use
#include "ext/registry.h"
#include "ext/tweak/ext_tweaks.h"             // the debug overlay's live Look
#include "fancy_terrain/terrain_light_backend.h"   // src/render/common: the pass (DX11, Vulkan; Metal unsupported)
#include "Component/still_terrain.h"          // StillTerrain::g_epoch: every heightmap writer bumps it
#include "Game/GameUpdateLoop.h"
#include "Game/WorldGeometry.h"
#include "graphics/Camera.h"
#include "graphics/SunLight.h"
#include "math/Math.h"
#include "system/parallel_for.h"
#include "video/Context.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace Ext {
namespace FancyTerrain {

namespace {

constexpr float kHeightScale = 1.0f / 256.0f;   // WorldGeometry's int16 heightmap word -> world units

Look CurrentLook() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::TerrainValues();
#else
    return Look{};
#endif
}

// float -> IEEE half (round to nearest even is not needed: a height to ~1/1000 of a unit).
uint16_t ToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);                 // tiny: zero
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);      // huge: the largest half
    mant = (mant + 0x1000u) >> 13;
    if (mant == 0x400u) {
        mant = 0;
        ++exp;
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
}

// The map and what the draw needs, under g_mu (main thread writes, the drawing thread reads).
struct Data {
    Horizon::Map map;
    std::vector<uint16_t> ground;     // the heightmap as half floats (world units)
    std::vector<int16_t> heights;     // the heightmap the map was built from
    int stride = 0;                   // vertices per row (== rows)
    int radius = 0;
    uint32_t generation = 0;
    Horizon::Rect dirty;              // rebuilt since the last draw took it
    float sun[3] = { 0.0f, 0.0f, 1.0f };
    bool ready = false;
};
std::mutex g_mu;
Data g;

// Main thread: the last epoch the map was checked at, the geometry it was built from.
uint64_t g_epochSeen = 0;
const WorldGeometry* g_geometry = nullptr;

void Unite(Horizon::Rect* into, const Horizon::Rect& r) {
    if (r.Empty()) return;
    if (into->Empty()) {
        *into = r;
        return;
    }
    into->x0 = std::min(into->x0, r.x0);
    into->y0 = std::min(into->y0, r.y0);
    into->x1 = std::max(into->x1, r.x1);
    into->y1 = std::max(into->y1, r.y1);
}

// The horizon over `rect` (rows split over the worker pool: each vertex is computed by the same operations
// whatever the split, Compute reads only the heightmap) and the ground there. The caller holds g_mu.
void RebuildLocked(Horizon::Rect rect) {
    const std::vector<int> steps = Horizon::SearchSteps(g.radius);
    const int16_t* h = g.heights.data();
    const int stride = g.stride;
    Horizon::Map* map = &g.map;
    System::ParallelFor(rect.y0, rect.y1, 0, [&](int y0, int y1) {
        Horizon::Rect part = rect;
        part.y0 = y0;
        part.y1 = y1;
        Horizon::Compute(h, stride, kHeightScale, steps, part, map);
    });
    for (int y = rect.y0; y < rect.y1; ++y)
        for (int x = rect.x0; x < rect.x1; ++x) {
            const size_t i = static_cast<size_t>(y) * stride + x;
            g.ground[i] = ToHalf(static_cast<float>(g.heights[i]) * kHeightScale);
        }
    Unite(&g.dirty, rect);
}

// Main thread: the map current with the world's heightmap.
bool KeepMapCurrent(const WorldGeometry* geo, int radius) {
    const int stride = geo->_heightRowStride;
    const int16_t* words = geo->_heights.data;
    if (stride <= 1 || words == nullptr) return false;
    const size_t count = static_cast<size_t>(stride) * stride;
    const uint64_t epoch = StillTerrain::g_epoch;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g.ready || g.stride != stride || g.radius != radius || geo != g_geometry) {
        const auto t0 = std::chrono::steady_clock::now();
        g.stride = stride;
        g.radius = radius;
        g.heights.assign(words, words + count);
        g.ground.assign(count, 0);
        g.map.Resize(stride, stride);
        ++g.generation;
        RebuildLocked(Horizon::Rect{ 0, 0, stride, stride });
        g.ready = true;
        g_geometry = geo;
        g_epochSeen = epoch;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[FancyTerrain] horizon map %dx%d (radius %d) built in %.1f ms\n", stride, stride, radius,
                     ms);
        return true;
    }
    if (epoch == g_epochSeen) return true;
    g_epochSeen = epoch;
    // Something wrote the terrain: the changed vertices' rectangle.
    Horizon::Rect changed{ stride, stride, 0, 0 };
    for (int y = 0; y < stride; ++y) {
        const int16_t* now = words + static_cast<size_t>(y) * stride;
        int16_t* was = g.heights.data() + static_cast<size_t>(y) * stride;
        if (std::memcmp(now, was, sizeof(int16_t) * stride) == 0) continue;
        for (int x = 0; x < stride; ++x) {
            if (now[x] == was[x]) continue;
            changed.x0 = std::min(changed.x0, x);
            changed.x1 = std::max(changed.x1, x + 1);
        }
        changed.y0 = std::min(changed.y0, y);
        changed.y1 = std::max(changed.y1, y + 1);
        std::memcpy(was, now, sizeof(int16_t) * stride);
    }
    if (changed.Empty()) return true;   // a floor or a road's cells, not a height
    RebuildLocked(Horizon::Grow(changed, radius, stride, stride));
    return true;
}

// The drawing thread: the frame's constants and the map, under the map's lock.
void DrawTerrain(Video::Context& context, const Graphics::Camera& camera) {
    const Look look = CurrentLook();
    TerrainLightBackend::Frame f = {};
    const Math::Matrix cameraToWorld = camera._cameraToWorld.AsMatrix();
    const Math::Matrix viewProjection = cameraToWorld.Inverse() * camera._projection;
    const Math::Matrix inverse = viewProjection.Inverse();
    std::memcpy(f.invViewProj, inverse.m, sizeof(f.invViewProj));
    f.look[0] = look.skyStrength;
    f.look[1] = look.bounce;
    f.look[2] = look.softness;
    f.look[3] = look.heightFade;
    // The fog fade as the clouds' shadows take it (cloud_params.cpp ShadowTermsFor).
    const BetterClouds::ShadowTerms fog =
        BetterClouds::ShadowTermsFor(BetterClouds::Look{}, 0.0f, 0.0f, 1.0f, context._fogParams.x, context._fogParams.y);
    // The retail fog does not cover the far terrain as fully as its linear range says: the fade is stretched.
    f.screen[2] = fog.fadeFrom * look.fogScale;
    f.screen[3] = fog.fadeTo * look.fogScale;
    f.camera[0] = cameraToWorld.m[12];
    f.camera[1] = cameraToWorld.m[13];
    f.camera[2] = cameraToWorld.m[14];
    f.camera[3] = look.sunNear;
    f.tint[0] = look.tint[0];
    f.tint[1] = look.tint[1];
    f.tint[2] = look.tint[2];
    f.tint[3] = look.nearDistance;

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g.ready) return;
    f.sun[0] = g.sun[0];
    f.sun[1] = g.sun[1];
    f.sun[2] = g.sun[2];
    f.sun[3] = look.sunStrength;
    f.map[0] = static_cast<float>(g.stride >> 1);
    f.map[1] = 1.0f / static_cast<float>(g.stride);
    f.map[2] = 1.0f / static_cast<float>(g.stride);
    f.map[3] = look.ambientShare;
    TerrainLightBackend::Textures t;
    t.width = g.stride;
    t.height = g.stride;
    t.horizon0 = g.map.plane[0].data();
    t.horizon1 = g.map.plane[1].data();
    t.ground = g.ground.data();
    t.generation = g.generation;
    t.dirtyX0 = g.dirty.x0;
    t.dirtyY0 = g.dirty.y0;
    t.dirtyX1 = g.dirty.x1;
    t.dirtyY1 = g.dirty.y1;
    TerrainLightBackend::Draw(context, t, f);
    g.dirty = Horizon::Rect{};
}

}  // namespace

bool Enabled() { return Ext::Ext().fancy_terrain; }

GroundFn TerrainDraw() {
    const GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    const WorldGeometry* geo = loop != nullptr ? loop->m_worldGeometry : nullptr;
    const bool wanted = Enabled() && geo != nullptr;
    TerrainLightBackend::Request(wanted);
    if (!wanted || !TerrainLightBackend::Supported()) return nullptr;
    const Look look = CurrentLook();
    const int radius = look.radius < 4 ? 4 : (look.radius > 256 ? 256 : look.radius);
    if (!KeepMapCurrent(geo, radius)) return nullptr;
    float sx = 0.0f;
    float sy = 0.0f;
    float sz = 1.0f;
    if (loop->m_sunLight != nullptr) {
        // The direction the light travels; the pass wants the way to the sun.
        sx = -loop->m_sunLight->_direction.x;
        sy = -loop->m_sunLight->_direction.y;
        sz = -loop->m_sunLight->_direction.z;
        const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (len > 1e-6f) {
            sx /= len;
            sy /= len;
            sz /= len;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g.sun[0] = sx;
        g.sun[1] = sy;
        g.sun[2] = sz;
    }
    return &DrawTerrain;
}

}  // namespace FancyTerrain
}  // namespace Ext
