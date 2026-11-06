#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_render_math.h -- ext.map_overlays: the terrain picture's triangles
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §5. Standard C++ only (unit tested in
// tests/unit/ext_map_overlays_render_test.cpp). overlay_render.cpp supplies the camera's frustum, the ground
// heights and the image, and draws the triangles through the game's tile overlay material, as ext.show_grid
// does; this header decides which triangles.
//
// WHAT THE SCREEN SEES. The part of the view frustum between the lowest and the highest ground of the map
// (FootprintOf): every visible point of the ground lies in it, whether or not a ray through a screen corner
// meets the ground (near the horizon it does not, and a box of ray hits left the far ground bare). Its outline
// seen from above is a convex polygon, the footprint; the picture covers the cells the footprint, widened by
// kTerrainMargin, overlaps (CellRowsOf), row by row.
//
// THE COST. A large map has 250k+ tiles; the view at the furthest zoom sees most of them. The picture is
// therefore drawn on a grid of CELLS `step` tiles a side, a power of two that keeps the cells under kMaxCells:
//   * step 1 (a view of up to kMaxCells tiles): one flat quad per tile, the tile's own colour, the quad's
//     corners on the ground's vertices -- the exact picture, "one building, one square";
//   * step > 1: one quad per cell whose corners carry the colour of the tiles around them (the GPU blends
//     between), each corner at the highest ground within half a cell of it, so the coarse quad lies over the
//     ground it spans rather than through it. The corners are the map's grid at that step (CoarseGrid): the
//     same cell always has the same quad, whatever the view.
// The step (StepFor) is chosen from the footprint's area and perimeter, which a turn of the camera or a pan
// does not change, and keeps its value until a finer one fits with room to spare, so turning the camera never
// flips the picture between two steps (it flickered while the box of the screen rays swung with the yaw).
// Only cells with a corner (or the tile) of non-zero alpha are emitted.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ext/show_grid/show_grid_math.h"   // LineVertex (the overlay renderer's 0x18-byte vertex), kSolidU / V

namespace Ext {
namespace MapOverlays {

using TerrainVertex = Ext::ShowGrid::LineVertex;

constexpr int kMaxCells = 16384;          // quads a frame, about (98304 vertices, 2.3 MB of the 8 MB stream)
constexpr int kMaxStep = 64;              // a 4096 map seen whole: 64 x 64 cells
constexpr float kFinerShare = 0.5f;       // a finer step is taken when its cells fit in this share of kMaxCells
constexpr float kTerrainLift = 0.05f;     // tile units above the ground (show_grid's lines float at 0.03)
constexpr float kTerrainMargin = 2.0f;    // tiles beyond the footprint

struct Point2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Point3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// A convex polygon, counter-clockwise, in tile coordinates (x = column, y = row; z up).
struct Footprint {
    static constexpr int kMaxPoints = 32;   // 8 corners + 12 edges x 2 planes
    int count = 0;
    Point2 points[kMaxPoints];
    bool Valid() const { return count >= 3; }
};

namespace detail {

inline float Cross(const Point2& o, const Point2& a, const Point2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

}  // namespace detail

// The footprint of the frustum part between z = zLow and z = zHigh. `corners` are the frustum's eight corners
// in tile coordinates, in Volume::Frustum::GetCorners' order: 0..3 the near plane (x lo/hi, then z lo/hi),
// 4..7 the far plane, i + 4 behind i. The slab's part of the frustum is a convex solid whose corners are the
// frustum's corners inside the slab and the points where the frustum's edges cross the slab's two planes; the
// footprint is the convex hull of their (x, y).
inline Footprint FootprintOf(const Point3 (&corners)[8], float zLow, float zHigh) {
    static const int kEdges[12][2] = {
        {0, 1}, {2, 3}, {0, 2}, {1, 3}, {4, 5}, {6, 7}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    Point2 pts[Footprint::kMaxPoints];
    int n = 0;
    for (const Point3& c : corners) {
        if (c.z >= zLow && c.z <= zHigh) {
            pts[n].x = c.x;
            pts[n].y = c.y;
            ++n;
        }
    }
    const float planes[2] = {zLow, zHigh};
    for (const auto& edge : kEdges) {
        const Point3& a = corners[edge[0]];
        const Point3& b = corners[edge[1]];
        for (float z0 : planes) {
            if ((a.z - z0) * (b.z - z0) >= 0.0f) continue;   // both on one side (or one on the plane: a corner)
            const float t = (z0 - a.z) / (b.z - a.z);
            pts[n].x = a.x + (b.x - a.x) * t;
            pts[n].y = a.y + (b.y - a.y) * t;
            ++n;
        }
    }
    Footprint hull;
    if (n < 3) return hull;
    // Andrew's monotone chain
    std::sort(pts, pts + n, [](const Point2& p, const Point2& q) { return p.x < q.x || (p.x == q.x && p.y < q.y); });
    Point2 h[2 * Footprint::kMaxPoints];
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && detail::Cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
        h[k++] = pts[i];
    }
    const int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && detail::Cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
        h[k++] = pts[i];
    }
    k = std::min(k - 1, Footprint::kMaxPoints);   // the last point repeats the first
    if (k < 3) return hull;
    hull.count = k;
    for (int i = 0; i < k; ++i) hull.points[i] = h[i];
    return hull;
}

inline float FootprintArea(const Footprint& f) {
    float twice = 0.0f;
    for (int i = 0; i < f.count; ++i) {
        const Point2& a = f.points[i];
        const Point2& b = f.points[(i + 1) % f.count];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::fabs(twice) * 0.5f;
}

inline float FootprintPerimeter(const Footprint& f) {
    float sum = 0.0f;
    for (int i = 0; i < f.count; ++i) {
        const Point2& a = f.points[i];
        const Point2& b = f.points[(i + 1) % f.count];
        sum += std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    }
    return sum;
}

// Cells `step` tiles a side over a map `mapSize` tiles a side, per side (the last one may be short).
inline int CellsPerSide(int mapSize, int step) {
    return (mapSize + step - 1) / step;
}

// An upper bound of the cells of `step` the footprint, widened by `margin`, overlaps: its area over the cell
// area plus two cells per cell length of its outline, at most the whole map. A turn or a pan of the camera
// does not change it.
inline double EstimatedCells(const Footprint& f, int mapSize, int step, float margin) {
    const double area = FootprintArea(f);
    const double perimeter = FootprintPerimeter(f);
    const double m = margin;
    const double wideArea = area + 2.0 * m * perimeter + 4.0 * m * m;
    const double widePerimeter = perimeter + 8.0 * m;
    const double s = step;
    const double estimate = wideArea / (s * s) + 2.0 * widePerimeter / s + 4.0;
    const double side = CellsPerSide(mapSize, step);
    return std::min(estimate, side * side);
}

// The step for the footprint: the smallest power of two (1 .. kMaxStep) whose cells stay under kMaxCells,
// or `previous` while that is coarser but a step between the two would not fit in kFinerShare of kMaxCells.
inline int StepFor(const Footprint& f, int mapSize, int previous) {
    int fine = 1;
    while (fine < kMaxStep && EstimatedCells(f, mapSize, fine, kTerrainMargin) > kMaxCells) fine *= 2;
    int roomy = fine;
    while (roomy < kMaxStep && EstimatedCells(f, mapSize, roomy, kTerrainMargin) > kMaxCells * kFinerShare)
        roomy *= 2;
    if (previous >= fine && previous <= roomy && (previous & (previous - 1)) == 0) return previous;
    return fine;
}

// The cells of `step` the footprint, widened by `margin` tiles, overlaps: for each cell row r (rows r * step
// .. (r + 1) * step), the inclusive column range first[r] .. last[r], empty when first > last.
struct CellRows {
    int step = 0;
    std::vector<int> first;
    std::vector<int> last;
};

inline bool SameCellRows(const CellRows& a, const CellRows& b) {
    return a.step == b.step && a.first == b.first && a.last == b.last;
}

inline void CellRowsOf(const Footprint& f, int mapSize, int step, float margin, CellRows& out) {
    const int side = CellsPerSide(mapSize, step);
    out.step = step;
    out.first.assign(static_cast<size_t>(side), 0);
    out.last.assign(static_cast<size_t>(side), -1);
    if (!f.Valid()) return;
    for (int r = 0; r < side; ++r) {
        // the polygon's x extent over the band of rows, the band widened by the margin; then the extent
        // widened by it (the polygon grown by a square of `margin`)
        const float ya = static_cast<float>(r * step) - margin;
        const float yb = static_cast<float>(std::min(mapSize, (r + 1) * step)) + margin;
        float lo = 1e30f;
        float hi = -1e30f;
        for (int i = 0; i < f.count; ++i) {
            const Point2& p = f.points[i];
            const Point2& q = f.points[(i + 1) % f.count];
            if (std::max(p.y, q.y) < ya || std::min(p.y, q.y) > yb) continue;
            float t0 = 0.0f;
            float t1 = 1.0f;
            if (q.y != p.y) {
                float ta = (ya - p.y) / (q.y - p.y);
                float tb = (yb - p.y) / (q.y - p.y);
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                if (t0 > t1) continue;
            }
            const float x0 = p.x + (q.x - p.x) * t0;
            const float x1 = p.x + (q.x - p.x) * t1;
            lo = std::min(lo, std::min(x0, x1));
            hi = std::max(hi, std::max(x0, x1));
        }
        if (lo > hi) continue;
        lo -= margin;
        hi += margin;
        if (hi < 0.0f || lo >= static_cast<float>(mapSize)) continue;
        const int c0 = std::max(0, static_cast<int>(std::floor(lo / static_cast<float>(step))));
        const int c1 = std::min(side - 1, static_cast<int>(std::floor(hi / static_cast<float>(step))));
        out.first[static_cast<size_t>(r)] = c0;
        out.last[static_cast<size_t>(r)] = c1;
    }
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

// Where tile (col, row) of an overlay image lands in the map window's texture. The minimap's texture holds
// the map upside down: tile row 0 is the texture's LAST row, as retail paints Minimap::overlayPixels
// (Minimap::ClearTerrain, mapcomponent.cpp: index (width - y - row - 1) * width + x) over the top-down
// terrain capture. The overlay image is tile order (row * size + col), so the upload flips the rows.
inline size_t MinimapTextureIndex(int col, int row, int size) {
    return static_cast<size_t>(size - 1 - row) * static_cast<size_t>(size) + static_cast<size_t>(col);
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

// The map's vertex grid at one step > 1: vertex (i, j) sits on tile vertex (GridCoord(i), GridCoord(j)), the
// last row and column on the map edge. Each vertex's colour and height are worked out the first time a draw
// needs them; InvalidateColors / InvalidateHeights (a new image, a changed ground) forget them all at once.
class CoarseGrid {
public:
    void Reset(int step, int mapSize) {
        step_ = step;
        mapSize_ = mapSize;
        side_ = CellsPerSide(mapSize, step) + 1;
        const size_t count = static_cast<size_t>(side_) * static_cast<size_t>(side_);
        colors_.assign(count, 0u);
        heights_.assign(count, 0.0f);
        colorStamp_.assign(count, 0u);
        heightStamp_.assign(count, 0u);
        colorEpoch_ = 1;
        heightEpoch_ = 1;
    }
    void InvalidateColors() { Bump(colorEpoch_, colorStamp_); }
    void InvalidateHeights() { Bump(heightEpoch_, heightStamp_); }
    int Step() const { return step_; }
    int MapSize() const { return mapSize_; }
    int GridCoord(int i) const { return std::min(mapSize_, i * step_); }

    uint32_t Color(int i, int j, const std::vector<uint32_t>& image, int imageSize, int opacityPercent) {
        const size_t k = Index(i, j);
        if (colorStamp_[k] != colorEpoch_) {
            colorStamp_[k] = colorEpoch_;
            colors_[k] = VertexColor(CornerColor(image, imageSize, mapSize_, GridCoord(i), GridCoord(j)),
                                     opacityPercent);
        }
        return colors_[k];
    }

    template <class HeightFn>
    float Height(int i, int j, HeightFn& height) {
        const size_t k = Index(i, j);
        if (heightStamp_[k] != heightEpoch_) {
            heightStamp_[k] = heightEpoch_;
            const int vc = GridCoord(i);
            const int vr = GridCoord(j);
            const int half = step_ / 2;
            float top = height(vc, vr);
            for (int dr = -half; dr <= half; ++dr) {
                const int r = vr + dr;
                if (r < 0 || r > mapSize_) continue;
                for (int dc = -half; dc <= half; ++dc) {
                    const int c = vc + dc;
                    if (c < 0 || c > mapSize_) continue;
                    top = std::max(top, height(c, r));
                }
            }
            heights_[k] = top + kTerrainLift;
        }
        return heights_[k];
    }

private:
    size_t Index(int i, int j) const {
        return static_cast<size_t>(j) * static_cast<size_t>(side_) + static_cast<size_t>(i);
    }
    static void Bump(uint32_t& epoch, std::vector<uint32_t>& stamps) {
        ++epoch;
        if (epoch == 0) {   // wrapped: no stamp may match by accident
            std::fill(stamps.begin(), stamps.end(), 0u);
            epoch = 1;
        }
    }

    int step_ = 0;
    int mapSize_ = 0;
    int side_ = 0;
    std::vector<uint32_t> colors_;
    std::vector<float> heights_;
    std::vector<uint32_t> colorStamp_;
    std::vector<uint32_t> heightStamp_;
    uint32_t colorEpoch_ = 1;
    uint32_t heightEpoch_ = 1;
};

// The triangles of the picture over `rows` (CellRowsOf). `originX` / `originY` are the world position of
// tile vertex 0 (a tile is one world unit); `height(vc, vr)` the ground at tile vertex (vc, vr), vertices 0 ..
// mapSize. For a step > 1, `grid` holds that step's vertices (Reset to it by the caller). `out` is cleared
// and filled.
template <class HeightFn>
void BuildTerrainVertices(const CellRows& rows, int mapSize, float originX, float originY,
                          const std::vector<uint32_t>& image, int imageSize, int opacityPercent, HeightFn height,
                          CoarseGrid& grid, std::vector<TerrainVertex>& out) {
    out.clear();
    const int step = rows.step;
    if (mapSize <= 0 || imageSize <= 0 || step < 1) return;
    // two triangles (0 = (c0, r0), 1 = (c1, r0), 2 = (c1, r1), 3 = (c0, r1)), clockwise seen from above (the
    // winding of show_grid's strips, which the overlay's cull mode keeps), split along 0-2 or along 1-3
    static const int kSplit02[6] = {0, 2, 1, 0, 3, 2};
    static const int kSplit13[6] = {0, 3, 1, 1, 3, 2};
    const auto emit = [&out](const float* px, const float* py, const float* pz, const uint32_t* colors,
                             const int* order) {
        for (int k = 0; k < 6; ++k) {
            const int i = order[k];
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
    const int side = static_cast<int>(rows.first.size());
    if (step == 1) {
        for (int r = 0; r < side; ++r) {
            for (int c = rows.first[static_cast<size_t>(r)]; c <= rows.last[static_cast<size_t>(r)]; ++c) {
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
                // the terrain mesh's own split (TerrainHeightOnly, worldgeometry_ray.cpp): 0-2 where the tile's
                // column + row is odd, 1-3 where it is even, so the picture lies on the ground's triangles
                emit(px, py, pz, colors, ((c + r) & 1) != 0 ? kSplit02 : kSplit13);
            }
        }
        return;
    }
    if (grid.Step() != step || grid.MapSize() != mapSize) return;
    for (int j = 0; j < side; ++j) {
        for (int i = rows.first[static_cast<size_t>(j)]; i <= rows.last[static_cast<size_t>(j)]; ++i) {
            const int gi[4] = {i, i + 1, i + 1, i};
            const int gj[4] = {j, j, j + 1, j + 1};
            uint32_t colors[4];
            uint32_t alpha = 0;
            for (int q = 0; q < 4; ++q) {
                colors[q] = grid.Color(gi[q], gj[q], image, imageSize, opacityPercent);
                alpha |= colors[q] >> 24;
            }
            if (alpha == 0) continue;
            float px[4];
            float py[4];
            float pz[4];
            for (int q = 0; q < 4; ++q) {
                px[q] = originX + static_cast<float>(grid.GridCoord(gi[q]));
                py[q] = originY + static_cast<float>(grid.GridCoord(gj[q]));
                pz[q] = grid.Height(gi[q], gj[q], height);
            }
            emit(px, py, pz, colors, kSplit02);
        }
    }
}

}  // namespace MapOverlays
}  // namespace Ext
