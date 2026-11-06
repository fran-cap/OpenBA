#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_render_math.h -- ext.map_overlays: the terrain picture's triangles
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §5. Standard C++ only (unit tested in
// tests/unit/ext_map_overlays_render_test.cpp). overlay_render.cpp finds the tiles the screen looks at,
// supplies the ground heights and draws the triangles through the game's tile overlay material, as
// ext.show_grid does; this header decides which triangles.
//
// THE COST. A large map has 250k+ tiles; the view at the furthest zoom sees most of them. The picture is
// therefore drawn on a grid of CELLS `step` tiles a side, the step the smallest power of two that keeps the
// cells of the box under kMaxCells:
//   * step 1 (a view of up to kMaxCells tiles): one flat quad per tile, the tile's own colour, the quad's
//     corners on the ground's vertices -- the exact picture, "one building, one square";
//   * step > 1: one quad per cell whose corners carry the colour of the tiles around them (the GPU blends
//     between), each corner at the highest ground within half a cell of it, so the coarse quad lies over the
//     ground it spans rather than through it.
// Only cells with a corner (or the tile) of non-zero alpha are emitted. At most kMaxCells * 6 vertices.
// ============================================================================
#include <algorithm>
#include <cstdint>
#include <vector>

#include "ext/show_grid/show_grid_math.h"   // LineVertex (the overlay renderer's 0x18-byte vertex), kSolidU / V

namespace Ext {
namespace MapOverlays {

using TerrainVertex = Ext::ShowGrid::LineVertex;

constexpr int kMaxCells = 16384;     // quads a frame at most (98304 vertices, 2.3 MB of the 8 MB stream)
constexpr int kMaxStep = 64;         // a 4096 map seen whole: 64 x 64 cells
constexpr float kTerrainLift = 0.05f;   // tile units above the ground (show_grid's lines float at 0.03)
constexpr int kTerrainMargin = 2;    // tiles beyond the ones the screen's sample rays hit

// An inclusive box of tiles.
struct TileBox {
    int col0 = 0;
    int row0 = 0;
    int col1 = -1;
    int row1 = -1;
    bool Valid() const { return col1 >= col0 && row1 >= row0; }
};

inline bool SameTileBox(const TileBox& a, const TileBox& b) {
    return a.col0 == b.col0 && a.row0 == b.row0 && a.col1 == b.col1 && a.row1 == b.row1;
}

// The box around the `n` tiles the sample rays hit, kTerrainMargin wider, inside the map. No cap: the step
// keeps the cost (StepFor).
inline TileBox TileBoxOf(const int* cols, const int* rows, int n, int mapSize) {
    TileBox none;
    if (n <= 0 || mapSize <= 0) return none;
    int c0 = cols[0];
    int c1 = cols[0];
    int r0 = rows[0];
    int r1 = rows[0];
    for (int i = 1; i < n; ++i) {
        c0 = std::min(c0, cols[i]);
        c1 = std::max(c1, cols[i]);
        r0 = std::min(r0, rows[i]);
        r1 = std::max(r1, rows[i]);
    }
    TileBox b;
    b.col0 = std::max(0, c0 - kTerrainMargin);
    b.row0 = std::max(0, r0 - kTerrainMargin);
    b.col1 = std::min(mapSize - 1, c1 + kTerrainMargin);
    b.row1 = std::min(mapSize - 1, r1 + kTerrainMargin);
    if (!b.Valid()) return none;
    return b;
}

// The smallest power of two (1 .. kMaxStep) whose cells over `box` number at most kMaxCells.
inline int StepFor(const TileBox& box) {
    if (!box.Valid()) return 1;
    const int w = box.col1 - box.col0 + 1;
    const int h = box.row1 - box.row0 + 1;
    int step = 1;
    while (step < kMaxStep) {
        const int64_t cw = (w + step - 1) / step;
        const int64_t ch = (h + step - 1) / step;
        if (cw * ch <= kMaxCells) break;
        step *= 2;
    }
    return step;
}

// An overlay pixel (0xAABBGGRR, the minimap's order) as the vertex colour (0xAARRGGBB), its alpha scaled by
// `opacityPercent`.
inline uint32_t VertexColor(uint32_t abgr, int opacityPercent) {
    const uint32_t r = abgr & 0xFFu;
    const uint32_t g = (abgr >> 8) & 0xFFu;
    const uint32_t b = (abgr >> 16) & 0xFFu;
    const int percent = std::min(100, std::max(0, opacityPercent));
    const uint32_t a = ((abgr >> 24) & 0xFFu) * static_cast<uint32_t>(percent) / 100u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// An overlay pixel (0xAABBGGRR) as the map window's texture holds it: the minimap images are format 5,
// DXGI_FORMAT_B8G8R8A8_UNORM (imagebufferdata.cpp's kFormatToDxgi), so a uint32 there reads 0xAARRGGBB.
inline uint32_t TexturePixel(uint32_t abgr) {
    return (abgr & 0xFF00FF00u) | ((abgr & 0xFFu) << 16) | ((abgr >> 16) & 0xFFu);
}

// The pixel of tile (col, row) in an image `imageSize` a side over a map `mapSize` a side (the image is the
// map's size in the normal case; another size is scaled, nearest). 0 outside.
inline uint32_t PixelAt(const std::vector<uint32_t>& image, int imageSize, int mapSize, int col, int row) {
    if (col < 0 || row < 0 || col >= mapSize || row >= mapSize || imageSize <= 0) return 0;
    int x = col;
    int y = row;
    if (imageSize != mapSize) {
        x = static_cast<int>(static_cast<int64_t>(col) * imageSize / mapSize);
        y = static_cast<int>(static_cast<int64_t>(row) * imageSize / mapSize);
    }
    const size_t i = static_cast<size_t>(y) * static_cast<size_t>(imageSize) + static_cast<size_t>(x);
    return i < image.size() ? image[i] : 0u;
}

// The colour at grid vertex (vc, vr) for step > 1: the average of the four tiles that share the vertex,
// each colour channel weighted by its alpha (a transparent tile does not darken its neighbour), the alpha
// the plain average.
inline uint32_t CornerColor(const std::vector<uint32_t>& image, int imageSize, int mapSize, int vc, int vr) {
    uint32_t sa = 0;
    uint32_t sr = 0;
    uint32_t sg = 0;
    uint32_t sb = 0;
    for (int dr = -1; dr <= 0; ++dr) {
        for (int dc = -1; dc <= 0; ++dc) {
            const uint32_t p = PixelAt(image, imageSize, mapSize, vc + dc, vr + dr);
            const uint32_t a = p >> 24;
            sa += a;
            sr += (p & 0xFFu) * a;
            sg += ((p >> 8) & 0xFFu) * a;
            sb += ((p >> 16) & 0xFFu) * a;
        }
    }
    if (sa == 0) return 0;
    const uint32_t r = sr / sa;
    const uint32_t g = sg / sa;
    const uint32_t b = sb / sa;
    const uint32_t a = sa / 4u;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// The triangles of the picture over `box` at `step` (StepFor). `originX` / `originY` are the world position
// of vertex 0 (a tile is one world unit); `height(vc, vr)` the ground at vertex (vc, vr), vertices 0 ..
// mapSize. `out` is cleared and filled.
template <class HeightFn>
void BuildTerrainVertices(const TileBox& box, int step, int mapSize, float originX, float originY,
                          const std::vector<uint32_t>& image, int imageSize, int opacityPercent,
                          HeightFn height, std::vector<TerrainVertex>& out) {
    out.clear();
    if (!box.Valid() || mapSize <= 0 || imageSize <= 0 || step < 1) return;
    const auto emit = [&out](const float* px, const float* py, const float* pz, const uint32_t* colors) {
        // two triangles, 0 2 1 and 0 3 2 (0 = (c0, r0), 1 = (c1, r0), 2 = (c1, r1), 3 = (c0, r1)): the winding
        // of show_grid's strips (clockwise seen from above), which the overlay's cull mode keeps
        static const int kOrder[6] = {0, 2, 1, 0, 3, 2};
        for (int k = 0; k < 6; ++k) {
            const int i = kOrder[k];
            TerrainVertex v;
            v.x = px[i];
            v.y = py[i];
            v.z = pz[i];
            v.u = Ext::ShowGrid::kSolidU;
            v.v = Ext::ShowGrid::kSolidV;
            v.color = colors[i];
            out.push_back(v);
        }
    };
    if (step == 1) {
        for (int r = box.row0; r <= box.row1; ++r) {
            for (int c = box.col0; c <= box.col1; ++c) {
                const uint32_t p = PixelAt(image, imageSize, mapSize, c, r);
                const uint32_t color = VertexColor(p, opacityPercent);
                if ((color >> 24) == 0) continue;
                const int vc[4] = {c, c + 1, c + 1, c};
                const int vr[4] = {r, r, r + 1, r + 1};
                float px[4];
                float py[4];
                float pz[4];
                uint32_t colors[4];
                for (int i = 0; i < 4; ++i) {
                    px[i] = originX + static_cast<float>(vc[i]);
                    py[i] = originY + static_cast<float>(vr[i]);
                    pz[i] = height(vc[i], vr[i]) + kTerrainLift;
                    colors[i] = color;
                }
                emit(px, py, pz, colors);
            }
        }
        return;
    }
    // step > 1: the cell grid's vertices, aligned to multiples of `step` so a pan does not shimmer
    const int vc0 = (box.col0 / step) * step;
    const int vr0 = (box.row0 / step) * step;
    const int vc1 = std::min(mapSize, ((box.col1 + 1 + step - 1) / step) * step);
    const int vr1 = std::min(mapSize, ((box.row1 + 1 + step - 1) / step) * step);
    const int nc = (vc1 - vc0 + step - 1) / step + 1;   // vertex columns (the last one clamped to the map edge)
    const int nr = (vr1 - vr0 + step - 1) / step + 1;
    std::vector<uint32_t> colors(static_cast<size_t>(nc) * static_cast<size_t>(nr));
    std::vector<float> heights(colors.size());
    const int half = step / 2;
    for (int j = 0; j < nr; ++j) {
        const int vr = std::min(vr1, vr0 + j * step);
        for (int i = 0; i < nc; ++i) {
            const int vc = std::min(vc1, vc0 + i * step);
            const size_t k = static_cast<size_t>(j) * static_cast<size_t>(nc) + static_cast<size_t>(i);
            colors[k] = VertexColor(CornerColor(image, imageSize, mapSize, vc, vr), opacityPercent);
            float top = height(vc, vr);
            for (int dr = -half; dr <= half; ++dr) {
                const int r = vr + dr;
                if (r < 0 || r > mapSize) continue;
                for (int dc = -half; dc <= half; ++dc) {
                    const int c = vc + dc;
                    if (c < 0 || c > mapSize) continue;
                    top = std::max(top, height(c, r));
                }
            }
            heights[k] = top + kTerrainLift;
        }
    }
    for (int j = 0; j + 1 < nr; ++j) {
        for (int i = 0; i + 1 < nc; ++i) {
            const size_t k[4] = {
                static_cast<size_t>(j) * static_cast<size_t>(nc) + static_cast<size_t>(i),
                static_cast<size_t>(j) * static_cast<size_t>(nc) + static_cast<size_t>(i + 1),
                static_cast<size_t>(j + 1) * static_cast<size_t>(nc) + static_cast<size_t>(i + 1),
                static_cast<size_t>(j + 1) * static_cast<size_t>(nc) + static_cast<size_t>(i),
            };
            uint32_t cellColors[4];
            uint32_t alpha = 0;
            for (int q = 0; q < 4; ++q) {
                cellColors[q] = colors[k[q]];
                alpha |= cellColors[q] >> 24;
            }
            if (alpha == 0) continue;
            const int cx[4] = {i, i + 1, i + 1, i};
            const int cy[4] = {j, j, j + 1, j + 1};
            float px[4];
            float py[4];
            float pz[4];
            for (int q = 0; q < 4; ++q) {
                px[q] = originX + static_cast<float>(std::min(vc1, vc0 + cx[q] * step));
                py[q] = originY + static_cast<float>(std::min(vr1, vr0 + cy[q] * step));
                pz[q] = heights[k[q]];
            }
            emit(px, py, pz, cellColors);
        }
    }
}

}  // namespace MapOverlays
}  // namespace Ext
