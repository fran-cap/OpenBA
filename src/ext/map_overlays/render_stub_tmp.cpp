// ============================================================================
// src/ext/map_overlays/render_stub_tmp.cpp -- TEMPORARY (the render agent's test stub; drop before merge)
// ============================================================================
// Stands in for what the history, views and UI agents own, so the renderers can be built and seen:
//   * g_on true from the first frame;
//   * a Production selection with onTerrain set;
//   * Image(): a test pattern (a radial gradient and a few blobs, moving every kRebuildFrames frames);
//   * TmpTick (GameUpdateLoop::Update, TMP line): RenderFrame, and MapWindowUpdate on the open MapUI
//     (mapui.cpp, TMP lines, records it).
// ============================================================================
#include "ext/map_overlays/map_overlays.h"
#include "Dialog/MapUI.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace MapOverlays {

bool g_on = true;

MapUI* g_tmpMap = nullptr;

namespace {

Selection MakeSelection() {
    Selection s;
    s.view = View::Production;
    s.onTerrain = true;
    return s;
}

std::vector<uint32_t> g_image;
int g_size = 0;
uint32_t g_version = 0;
int g_frames = 0;
int g_builtFrame = -1;

uint32_t TmpRamp(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    if (t <= 0.0f) return 0;
    // blue -> green -> yellow -> red, alpha rising with t
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (t < 0.33f) {
        b = 1.0f - t / 0.33f;
        g = t / 0.33f;
    } else if (t < 0.66f) {
        g = 1.0f;
        r = (t - 0.33f) / 0.33f;
    } else {
        r = 1.0f;
        g = 1.0f - (t - 0.66f) / 0.34f;
    }
    const uint32_t a = static_cast<uint32_t>(64.0f + 191.0f * t);
    const auto byte = [](float c) {
        return static_cast<uint32_t>(c * 255.0f) & 0xFFu;
    };
    return (a << 24) | (byte(b) << 16) | (byte(g) << 8) | byte(r);
}

void Rebuild(int size, int phase) {
    g_image.assign(static_cast<size_t>(size) * size, 0u);
    const float c = static_cast<float>(size) * 0.5f;
    const float radius = static_cast<float>(size) * 0.35f;
    const float shift = static_cast<float>(phase % 16) * 2.0f;
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) - c - shift;
            const float dz = static_cast<float>(z) - c;
            float v = 1.0f - std::sqrt(dx * dx + dz * dz) / radius;
            // a ring of zero, so the transparent parts show
            if (std::fabs(v - 0.5f) < 0.04f) v = 0.0f;
            // single tiles at the exact scale, every 16th tile in a checker band
            if (z > size * 3 / 4 && z < size * 3 / 4 + 4 && x % 8 == 0) v = 1.0f;
            g_image[static_cast<size_t>(z) * size + x] = TmpRamp(v);
        }
    }
    ++g_version;
}

}  // namespace

const Selection& CurrentSelection() {
    static const Selection s = MakeSelection();
    return s;
}

const std::vector<uint32_t>& Image(int& size, uint32_t& version) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    const int mapSize = (game != nullptr && game->m_game != nullptr) ? game->m_game->size : 0;
    if (mapSize <= 0) {
        size = 0;
        version = g_version;
        return g_image;
    }
    if (mapSize != g_size || (g_frames % kRebuildFrames == 0 && g_builtFrame != g_frames)) {
        g_size = mapSize;
        g_builtFrame = g_frames;
        Rebuild(mapSize, g_frames / kRebuildFrames);
    }
    size = g_size;
    version = g_version;
    return g_image;
}

void TmpTick() {
    ++g_frames;
    RenderFrame();
    if (g_tmpMap != nullptr) MapWindowUpdate(*g_tmpMap);
}

}  // namespace MapOverlays
}  // namespace Ext
