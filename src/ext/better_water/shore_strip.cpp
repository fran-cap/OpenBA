// ============================================================================
// src/ext/better_water/shore_strip.cpp -- ext.better_water Fancy: the shore strip's mesh (pure)
// ============================================================================
// NOT RETAIL. shore_strip.h has the contract.
// ============================================================================
#include "ext/better_water/shore_strip.h"

#include <algorithm>
#include <cmath>

namespace Ext {
namespace BetterWater {
namespace Strip {

namespace {

constexpr float kHeightStep = 1.0f / 256.0f;   // the int16 heights' unit
constexpr float kPosScale = 48.0f;             // the water's position and uv packing

int16_t Pack(float v, float scale) {
    const float s = std::round(v * scale);
    return static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, s)));
}

}  // namespace

uint8_t EncodeHeight(float aboveWaterline) {
    const float t = std::max(0.0f, std::min(1.0f, (aboveWaterline + 0.5f) * 0.5f));
    return static_cast<uint8_t>(std::lround(t * 255.0f));
}

float DecodeHeight(uint8_t byte) { return static_cast<float>(byte) / 255.0f * 2.0f - 0.5f; }

void Build(const int16_t* heights, int32_t n, int32_t halfGrid, const Vertex* water, int32_t waterCount,
           Mesh& out) {
    out.vertices.clear();
    out.indices.clear();
    out.chunks.clear();
    out.cells = 0;
    if (heights == nullptr || n < 2) return;
    const int32_t cells = n - 1;

    // The cells the game's water covers, then grown by kNearWater.
    std::vector<uint8_t> wet(static_cast<size_t>(cells) * static_cast<size_t>(cells), 0);
    for (int32_t i = 0; i < waterCount; ++i) {
        const int32_t col = static_cast<int32_t>(std::floor(water[i].position[0] / kPosScale)) + halfGrid;
        const int32_t row = static_cast<int32_t>(std::floor(water[i].position[1] / kPosScale)) + halfGrid;
        for (int32_t r = row - 1; r <= row; ++r) {   // a vertex on a cell corner touches four cells
            for (int32_t c = col - 1; c <= col; ++c) {
                if (r >= 0 && r < cells && c >= 0 && c < cells) wet[static_cast<size_t>(r) * cells + c] = 1;
            }
        }
    }
    std::vector<uint8_t> near(wet.size(), 0);
    for (int32_t r = 0; r < cells; ++r) {
        for (int32_t c = 0; c < cells; ++c) {
            if (wet[static_cast<size_t>(r) * cells + c] == 0) continue;
            for (int32_t dr = -kNearWater; dr <= kNearWater; ++dr) {
                for (int32_t dc = -kNearWater; dc <= kNearWater; ++dc) {
                    const int32_t rr = r + dr;
                    const int32_t cc = c + dc;
                    if (rr >= 0 && rr < cells && cc >= 0 && cc < cells) near[static_cast<size_t>(rr) * cells + cc] = 1;
                }
            }
        }
    }

    auto above = [&](int32_t col, int32_t row) {
        return static_cast<float>(heights[static_cast<size_t>(row) * n + col]) * kHeightStep - kWaterline;
    };

    // Grid vertex -> its index in the current chunk (-1 = not in it yet).
    std::vector<int32_t> local(static_cast<size_t>(n) * static_cast<size_t>(n), -1);
    std::vector<int32_t> touched;
    Chunk chunk = {0, 0, 0, 0};

    auto flush = [&]() {
        if (chunk.vertexCount == 0) return;
        out.chunks.push_back(chunk);
        for (int32_t g : touched) local[static_cast<size_t>(g)] = -1;
        touched.clear();
        chunk.vertexStart = static_cast<int32_t>(out.vertices.size());
        chunk.vertexCount = 0;
        chunk.indexStart = static_cast<int32_t>(out.indices.size());
        chunk.indexCount = 0;
    };

    auto vertex = [&](int32_t col, int32_t row) -> uint16_t {
        const int32_t g = row * n + col;
        int32_t& slot = local[static_cast<size_t>(g)];
        if (slot >= 0) return static_cast<uint16_t>(slot);
        const float h = above(col, row);
        const float x = static_cast<float>(col - halfGrid);
        const float y = static_cast<float>(row - halfGrid);
        const float z = std::max(h + kWaterline + kLift, kWaterTop);
        Vertex v;
        v.position[0] = Pack(x, kPosScale);
        v.position[1] = Pack(y, kPosScale);
        v.position[2] = Pack(z, 256.0f);
        v.position[3] = 0;
        v.uv[0] = v.position[0];
        v.uv[1] = v.position[1];
        v.normal[0] = 0x7f;
        v.normal[1] = 0x7f;
        v.normal[2] = EncodeHeight(h);
        v.normal[3] = 0;
        out.vertices.push_back(v);
        slot = chunk.vertexCount++;
        touched.push_back(g);
        return static_cast<uint16_t>(slot);
    };

    for (int32_t row = 0; row < cells; ++row) {
        for (int32_t col = 0; col < cells; ++col) {
            if (near[static_cast<size_t>(row) * cells + col] == 0) continue;
            const float h0 = above(col, row);
            const float h1 = above(col + 1, row);
            const float h2 = above(col + 1, row + 1);
            const float h3 = above(col, row + 1);
            const float lo = std::min(std::min(h0, h1), std::min(h2, h3));
            const float hi = std::max(std::max(h0, h1), std::max(h2, h3));
            if (lo > kBandAbove || hi < -kBandBelow) continue;

            if (chunk.vertexCount + 4 > kMaxChunkVertices) flush();
            const uint16_t tl = vertex(col, row);
            const uint16_t tr = vertex(col + 1, row);
            const uint16_t br = vertex(col + 1, row + 1);
            const uint16_t bl = vertex(col, row + 1);
            // The terrain's diagonal, in the water quads' winding (tl, tr, br order).
            const uint16_t even[6] = {tl, tr, bl, tr, br, bl};   // bottom-left to top-right
            const uint16_t odd[6] = {tl, tr, br, tl, br, bl};    // top-left to bottom-right
            const uint16_t* tri = ((col + row) & 1) == 0 ? even : odd;
            out.indices.insert(out.indices.end(), tri, tri + 6);
            chunk.indexCount += 6;
            ++out.cells;
        }
    }
    flush();
}

}  // namespace Strip
}  // namespace BetterWater
}  // namespace Ext
