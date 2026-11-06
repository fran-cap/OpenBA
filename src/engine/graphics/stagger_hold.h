#pragma once
// ============================================================================
// src/engine/graphics/stagger_hold.h -- ext.staggered_passes: when a pass may
// reuse last frame's target instead of rendering it again
// ============================================================================
//
// NOT RETAIL (issue #253; the row is src/ext/staggered_passes/). Retail renders
// the sun's shadow map and the water's reflection image every frame. With the row
// on, each of the two passes may skip a frame and leave its target as it is: the
// backends keep a target's contents until it is drawn again (Vulkan's images are
// single, not one per frame in flight; DX11's are plain textures), so the water
// samples the held reflection and DrawLight samples the held shadow map. The
// shadow map is sampled with the sun camera it was rendered with
// (wave28_lane_partsys.cpp), so the matrix always matches the map.
//
// Ruled by the maintainer: a held target is acceptable while its staleness stays
// within the frame window the pipeline already buffers. So a pass renders at least
// every `every` frames (2 by default: one frame held at most), and at once when:
//   * the view camera turned or moved more than a small tolerance since the
//     target was rendered, or its projection changed (zoom, aspect, field of view);
//   * the key changed: the scene, the target, the detail settings or the map size;
//   * the epoch moved: a dynamic-resource rebuild (Device::RecreateDynamic, i.e. a
//     settings change, a resize, a device restart), a draw with another camera
//     (the minimap capture), or a change of the row or its values;
//   * nothing was rendered yet, or the frame index did not move forward.
// The shadow pass refreshes on even frames and the reflection on odd ones
// (`phase`), so with both at 2 the two costs land on alternate frames.
//
// Header-only and free of game types: tests/unit/stagger_hold_test.cpp drives it.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace Graphics {
namespace StaggerHold {

// The view camera at the target's last real render: Camera::_cameraToWorld (axisX,
// axisY, axisZ, translation: 12 floats) and Camera::_projection (16 floats).
struct View {
    float xf[12]   = {};
    float proj[16] = {};
};

// What the target depends on besides the view (compared by identity and value).
struct Key {
    const void* a     = nullptr;
    const void* b     = nullptr;
    int32_t     c     = 0;
    int32_t     d     = 0;
    uint32_t    epoch = 0;
};

inline bool SameKey(const Key& x, const Key& y) {
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.epoch == y.epoch;
}

// One pass's last real render. Owned by the main thread (the pass-1 side).
struct State {
    bool    valid = false;
    int32_t frame = 0;
    Key     key;
    View    view;
};

constexpr int32_t kShadowPhase     = 0;   // the shadow map renders on frames where frame % every == 0
constexpr int32_t kReflectionPhase = 1;   // the reflection one frame later
// An axis of the view may turn by about 0.25 degrees from the rendered one.
constexpr float kTurnCos = 0.99999f;

// The epoch every Key carries. Bumped from any thread; read on the main thread.
inline std::atomic<uint32_t> g_epoch{ 0 };

inline uint32_t Epoch() { return g_epoch.load(std::memory_order_relaxed); }

// Every held target renders at its next use.
inline void Invalidate() { g_epoch.fetch_add(1u, std::memory_order_relaxed); }

inline void FillView(View& v, const void* cameraToWorld12, const void* projection16) {
    std::memcpy(v.xf, cameraToWorld12, sizeof(v.xf));
    std::memcpy(v.proj, projection16, sizeof(v.proj));
}

// True when the view turned or moved past the tolerance. `moveLimit` is in world units.
inline bool ViewMoved(const View& was, const View& now, float moveLimit) {
    if (std::memcmp(was.proj, now.proj, sizeof(was.proj)) != 0) return true;
    for (int axis = 0; axis < 3; ++axis) {
        const float* const p = was.xf + axis * 3;
        const float* const q = now.xf + axis * 3;
        const float dot = p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
        const float pp  = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
        const float qq  = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        // cos(angle) >= kTurnCos, without a square root: dot > 0 and dot^2 >= cos^2 |p|^2 |q|^2.
        if (!(dot > 0.0f)) return true;
        if (dot * dot < kTurnCos * kTurnCos * pp * qq) return true;
    }
    const float dx = now.xf[9] - was.xf[9];
    const float dy = now.xf[10] - was.xf[10];
    const float dz = now.xf[11] - was.xf[11];
    const float limit = moveLimit > 0.0f ? moveLimit : 0.0f;
    // Not `>`: a NaN delta must refresh too.
    return !(dx * dx + dy * dy + dz * dz <= limit * limit);
}

// The decision for `frame`: true = keep last frame's target (skip the pass), false = render it.
// `every` <= 1 is retail (render every frame).
inline bool ShouldHold(const State& s, int32_t every, int32_t phase, int32_t frame, const Key& key,
                       const View& now, float moveLimit) {
    if (every <= 1) return false;
    if (!s.valid) return false;
    if (!SameKey(s.key, key)) return false;
    // Frames since the last render, wrap-safe: 1 .. every - 1 may hold.
    const int64_t since = static_cast<int64_t>(frame) - static_cast<int64_t>(s.frame);
    if (since <= 0 || since >= every) return false;
    // This pass's scheduled frame renders (the two passes are a frame apart).
    const uint32_t slot = static_cast<uint32_t>(frame) + static_cast<uint32_t>(phase);
    if (slot % static_cast<uint32_t>(every) == 0u) return false;
    return !ViewMoved(s.view, now, moveLimit);
}

// The stress harness's window counts (src/diag/stress_harness.cpp, [Stress::stagger]): how many
// decisions each pass made while the row was on. Main thread only; never printed per frame.
struct Counters {
    uint64_t shadowHeld       = 0;
    uint64_t shadowRendered   = 0;
    uint64_t reflectionHeld   = 0;
    uint64_t reflectionRendered = 0;
};

inline Counters g_counters;

inline Counters TakeWindow() {
    const Counters c = g_counters;
    g_counters = Counters();
    return c;
}

// After a real render.
inline void Latch(State& s, int32_t frame, const Key& key, const View& now) {
    s.valid = true;
    s.frame = frame;
    s.key   = key;
    s.view  = now;
}

}  // namespace StaggerHold
}  // namespace Graphics
