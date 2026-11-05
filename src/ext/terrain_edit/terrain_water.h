#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_water.h -- ext.terrain_edit, the water an edit makes (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only, so tests/unit/ext_terrain_edit_test.cpp can
// check it without the game. The game draws water from a mesh it builds once, at world generation: for each
// water feature, a quad for every cell of the feature's box that has a corner at or below -0.75
// (WorldGeometry::BuildWaterShores, water_contour_mesh.cpp). This builds the same quads for the cells of a
// region of the heightmap that an edit has just taken under water, as one patch of vertices and indices that
// the brush appends to the mesh as a new draw range. The vertices are the game's own layout (16 bytes:
// position x y z and a pad, planar uv, and four bytes of which the third is the shore weight), the indices
// count from the patch's own first vertex (a draw range adds its base vertex).
// ============================================================================

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ext/terrain_edit/terrain_edit_math.h"

namespace Ext {
namespace TerrainEdit {

// a cell with a corner at or below this is water (the mesh builder's own test)
constexpr float kWaterTest   = -0.75f;
// a corner at this height or above has no shore weight; at or below kWaterTest it has all
constexpr float kShoreTop    = 0.25f;
// The z field of a water vertex is the height of its water: the mesh builder writes the feature's number
// minus 191, and the scale is 1/48 of a unit, so each lake lies a hair above the one before it (a lake's z
// is -191 + its index; the rivers use -192 and down). Two lakes whose z differ show a seam where they meet
// (a thin gap where the bed shows through), and the first version of this wrote -180, 0.23 above the first
// lake. A patch now takes the z of the water it touches (BuildWaterPatch is given it), or, when it touches
// none, kDefaultWaterZ, the first lake's.
constexpr int   kDefaultWaterZ = -191;
// Since z is not an identity, the edit's quads carry one in a byte the game leaves at 0 (the fourth
// component of the normal), so they can be told from the game's own when the mesh is read again after a
// load.
constexpr uint8_t kEditMarker = 1;
// position and uv are the coordinate times this, as int16 (the mesh builder's pack scale)
constexpr float kPosScale    = 48.0f;

struct WaterVertex {
    int16_t position[4];   // x * 48, y * 48, the feature number, 0
    int16_t uv[2];         // x * 48, y * 48
    uint8_t normal[4];     // 0x7f, 0x7f, the shore weight (0 to 255), kEditMarker
};
static_assert(sizeof(WaterVertex) == 16, "a water vertex is the game's 16 bytes");

// What a patch is made like. A lake's quads have planar uv (the position) and make slow ripples, in the lake
// draw ranges. A river's strips have the uv of the water along the flow: the game's mesh builder lays them
// from a centre line with u across the channel (0 to 1) and v = arc length / 32 - 16, and a river is drawn
// with its own material and a faster texture flow. A patch that joins a river is made river-style: the
// river's own texture mapping, taken from one of its triangles and continued over the patch as a plane (u
// and v each a linear function of the world position), so that the texture runs along the flow and across
// the channel at the river's own scale and phase. (A constant u gave the streaks of one texture column
// stretched.)
struct UvMap {
    float uBase = 0.5f;
    float uX = 0.0f;
    float uY = 0.0f;
    float vBase = 0.0f;
    float vX = 0.0f;
    float vY = 1.0f / 32.0f;
};

// The texture mapping of one river triangle, with where the triangle is. A river bends, so no one plane fits
// it for long: a river-style patch gives each of its vertices the mapping of the nearest anchor, which makes
// the patch continuous with the river where they meet and lets the mapping drift along with the river
// further out.
struct UvAnchor {
    float x = 0.0f;   // the triangle's centre, in world coordinates
    float y = 0.0f;
    UvMap map;
};

struct WaterStyle {
    int   z = kDefaultWaterZ;
    bool  river = false;
    UvMap map;                        // used when there are no anchors
    std::vector<UvAnchor> anchors;    // river style: the nearest of these gives a vertex its uv
};

// The mapping a river-style vertex at world (x, y) takes: the nearest anchor's, else the style's own.
inline const UvMap& MapAt(const WaterStyle& style, float x, float y) {
    const UvMap* best = &style.map;
    float bestD = 1e30f;
    for (const UvAnchor& a : style.anchors) {
        const float dx = a.x - x;
        const float dy = a.y - y;
        const float d = dx * dx + dy * dy;
        if (d < bestD) {
            bestD = d;
            best = &a.map;
        }
    }
    return *best;
}

// The texture mapping of a river triangle, from its three vertices' world positions and uv (as floats, uv /
// 1024): u and v as linear functions of x and y. False when the triangle is thin (its positions nearly in a
// line).
inline bool UvMapOfTriangle(const float x[3], const float y[3], const float u[3], const float v[3],
    UvMap& out) {
    const float e1x = x[1] - x[0];
    const float e1y = y[1] - y[0];
    const float e2x = x[2] - x[0];
    const float e2y = y[2] - y[0];
    const float det = e1x * e2y - e1y * e2x;
    if (det > -1e-4f && det < 1e-4f) return false;
    const float du1 = u[1] - u[0];
    const float du2 = u[2] - u[0];
    const float dv1 = v[1] - v[0];
    const float dv2 = v[2] - v[0];
    out.uX = (du1 * e2y - du2 * e1y) / det;
    out.uY = (e1x * du2 - e2x * du1) / det;
    out.vX = (dv1 * e2y - dv2 * e1y) / det;
    out.vY = (e1x * dv2 - e2x * dv1) / det;
    // the v of a river changes along its flow: a triangle where it does not (and u does not either) has no
    // map to continue
    if (std::fabs(out.vX) + std::fabs(out.vY) + std::fabs(out.uX) + std::fabs(out.uY) < 1e-6f) return false;
    out.uBase = u[0] - (out.uX * x[0] + out.uY * y[0]);
    out.vBase = v[0] - (out.vX * x[0] + out.vY * y[0]);
    return true;
}

// Whether the point (px, py) is inside the triangle (x, y)[3] (edges included).
inline bool PointInTriangle(float px, float py, const float x[3], const float y[3]) {
    const float d1 = (px - x[1]) * (y[0] - y[1]) - (x[0] - x[1]) * (py - y[1]);
    const float d2 = (px - x[2]) * (y[1] - y[2]) - (x[1] - x[2]) * (py - y[2]);
    const float d3 = (px - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (py - y[0]);
    const bool neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(neg && pos);
}

// Whether the brush may walk a contour draw range {start, base, count} over an index array of indexCount
// entries. NOT RETAIL: the ranges are loaded from the save unchecked (SerializeDrawRanges) and retail only
// hands them to the GPU; the brush reads them on the CPU. A range is read only when its indices lie inside
// the array (summed in 64 bits) and its base leaves room for any 16-bit index, so that neither
// `start + k` nor `base + idx[..]` can overflow an int (a hostile save read far outside both arrays).
inline bool ContourRangeFits(int start, int base, int count, int indexCount) {
    if (start < 0 || base < 0 || count < 0) return false;
    if (base > INT32_MAX - 0xFFFF) return false;
    return static_cast<int64_t>(start) + count <= indexCount;
}

struct WaterPatch {
    std::vector<WaterVertex> vertices;
    std::vector<uint16_t>    indices;   // relative to the patch's first vertex
    std::vector<uint64_t>    cells;     // WaterCellKey of each cell given a quad
    // more vertices than a 16-bit index reaches: the patch stops short
    bool                     overflow = false;
};

// The key of the cell whose lower corner is at world (col, row).
inline uint64_t WaterCellKey(int col, int row) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(col + 32768) & 0xffffu) << 16) |
           static_cast<uint64_t>(static_cast<uint32_t>(row + 32768) & 0xffffu);
}

// The shore weight of a corner at `height`: all of it at and below the water test, none at kShoreTop and
// above.
inline uint8_t ShoreWeight(float height) {
    const float w = ClampFloat(kShoreTop - height, 0.0f, 1.0f);
    return static_cast<uint8_t>(w * 255.0f + 0.5f);
}

// Quads for the cells [vx0, vx1) x [vy0, vy1) of the heightmap (a cell's corners are the vertices (vx, vy),
// (vx + 1, vy), (vx + 1, vy + 1) and (vx, vy + 1)), for each cell with a corner at or below kWaterTest that
// `skip(col, row)` does not refuse (a cell the mesh already draws). `height(vx, vy)` is the world height of
// a vertex, `halfGrid` the offset from a heightmap index to a world coordinate, `zFeature` the feature
// number written into every vertex.
template <class HeightFn, class SkipFn>
WaterPatch BuildWaterPatchStyled(HeightFn height, int vx0, int vy0, int vx1, int vy1, int halfGrid,
    const WaterStyle& style,
                                 SkipFn skip) {
    const int zFeature = style.z;
    auto pack = [&](float value) {
        float t = value * 1024.0f;
        if (t > 32767.0f) t = 32767.0f;
        if (t < -32768.0f) t = -32768.0f;
        return static_cast<int16_t>(static_cast<int>(t));
    };
    WaterPatch patch;
    std::unordered_map<uint64_t, uint16_t> weld;   // (x, y, weight) -> the index of the vertex already made
    for (int vy = vy0; vy < vy1; ++vy) {
        for (int vx = vx0; vx < vx1; ++vx) {
            const int dx[4] = { 0, 1, 1, 0 };
            const int dy[4] = { 0, 0, 1, 1 };
            float h[4];
            bool under = false;
            for (int k = 0; k < 4; ++k) {
                h[k] = height(vx + dx[k], vy + dy[k]);
                if (h[k] <= kWaterTest) under = true;
            }
            if (!under) continue;
            const int col = vx - halfGrid;
            const int row = vy - halfGrid;
            if (skip(col, row)) continue;
            uint16_t corner[4];
            bool full = false;
            for (int k = 0; k < 4; ++k) {
                const int x = col + dx[k];
                const int y = row + dy[k];
                const uint8_t weight = ShoreWeight(h[k]);
                const uint64_t kx = static_cast<uint64_t>(static_cast<uint32_t>(x + 32768) & 0xffffu);
                const uint64_t ky = static_cast<uint64_t>(static_cast<uint32_t>(y + 32768) & 0xffffu);
                const uint64_t key = (kx << 24) | (ky << 8) | weight;
                auto found = weld.find(key);
                if (found != weld.end()) {
                    corner[k] = found->second;
                    continue;
                }
                if (patch.vertices.size() >= 65535u) {
                    full = true;
                    break;
                }
                WaterVertex v;
                v.position[0] = static_cast<int16_t>(static_cast<int>(static_cast<float>(x) * kPosScale));
                v.position[1] = static_cast<int16_t>(static_cast<int>(static_cast<float>(y) * kPosScale));
                v.position[2] = static_cast<int16_t>(zFeature);
                v.position[3] = 0;
                if (style.river) {
                    const float fx = static_cast<float>(x);
                    const float fy = static_cast<float>(y);
                    const UvMap& m = MapAt(style, fx, fy);
                    v.uv[0] = pack(m.uBase + m.uX * fx + m.uY * fy);
                    v.uv[1] = pack(m.vBase + m.vX * fx + m.vY * fy);
                } else {
                    v.uv[0] = v.position[0];
                    v.uv[1] = v.position[1];
                }
                v.normal[0] = 0x7f;
                v.normal[1] = 0x7f;
                v.normal[2] = weight;
                v.normal[3] = kEditMarker;
                corner[k] = static_cast<uint16_t>(patch.vertices.size());
                weld.emplace(key, corner[k]);
                patch.vertices.push_back(v);
            }
            if (full) {
                patch.overflow = true;
                return patch;
            }
            const uint16_t quad[6] = { corner[0], corner[1], corner[2], corner[0], corner[2], corner[3] };
            for (const uint16_t i : quad) patch.indices.push_back(i);
            patch.cells.push_back(WaterCellKey(col, row));
        }
    }
    return patch;
}

// A lake patch: the quads above, as slow ripples at the height `zFeature`.
template <class HeightFn, class SkipFn>
WaterPatch BuildWaterPatch(HeightFn height, int vx0, int vy0, int vx1, int vy1, int halfGrid,
    int zFeature, SkipFn skip) {
    WaterStyle style;
    style.z = zFeature;
    return BuildWaterPatchStyled(height, vx0, vy0, vx1, vy1, halfGrid, style, skip);
}

}  // namespace TerrainEdit
}  // namespace Ext
