// ============================================================================
// tests/unit/ext_map_overlays_render_test.cpp -- ext.map_overlays' terrain triangles
// (src/ext/map_overlays/overlay_render_math.h)
// ============================================================================
// The footprint of the view, the cells it covers, the step that keeps the cost (and keeps still while the
// camera turns), the colour order, and the triangles on a known ground, without the game. The drawing itself
// (the camera's frustum, the overlay material, the map window's image) needs the game.
// ============================================================================
#include "unit.h"
#include "ext/map_overlays/overlay_render_math.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace Ext::MapOverlays;

float Flat(int, int) { return 3.0f; }

// A bump of 5 at vertex (8, 8), 0 elsewhere.
float Bump(int vc, int vr) { return (vc == 8 && vr == 8) ? 5.0f : 0.0f; }

constexpr uint32_t kRedOpaque = 0xFF0000FFu;   // 0xAABBGGRR: red, alpha 255

// A box-shaped "frustum": x in [x0, x1], y in [y0, y1], z in [z0, z1], in GetCorners' order (near 0..3 at
// y0, far 4..7 at y1).
void BoxCorners(float x0, float x1, float y0, float y1, float z0, float z1, Point3 (&c)[8]) {
    const float xs[2] = {x0, x1};
    const float zs[2] = {z0, z1};
    for (int i = 0; i < 4; ++i) {
        c[i].x = xs[i & 1];
        c[i].y = y0;
        c[i].z = zs[i >> 1];
        c[i + 4].x = xs[i & 1];
        c[i + 4].y = y1;
        c[i + 4].z = zs[i >> 1];
    }
}

// A perspective frustum looking down at `pitch` radians below the horizon from (cx, cy, height), turned by
// `yaw` radians about the vertical: near 1, far `far`, half-angles 0.6 (x) and 0.4 (z).
void ViewCorners(float cx, float cy, float height, float pitch, float yaw, float far, Point3 (&c)[8]) {
    const float sx[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
    const float sz[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 8; ++i) {
        const float d = i < 4 ? 1.0f : far;
        // camera space: x right, y forward, z up
        const float lx = sx[i & 3] * 0.6f * d;
        const float ly = d;
        const float lz = sz[i & 3] * 0.4f * d;
        // pitch down about x, then yaw about z
        const float py = ly * std::cos(pitch) + lz * std::sin(pitch);
        const float pz = -ly * std::sin(pitch) + lz * std::cos(pitch);
        c[i].x = cx + lx * std::cos(yaw) - py * std::sin(yaw);
        c[i].y = cy + lx * std::sin(yaw) + py * std::cos(yaw);
        c[i].z = height + pz;
    }
}

CellRows AllCells(int mapSize, int step) {
    CellRows rows;
    rows.step = step;
    const int side = CellsPerSide(mapSize, step);
    rows.first.assign(static_cast<size_t>(side), 0);
    rows.last.assign(static_cast<size_t>(side), side - 1);
    return rows;
}

}  // namespace

UNIT_TEST(ext_map_overlays_render_footprint_is_the_frustum_between_the_ground_planes) {
    Point3 c[8];
    BoxCorners(10.0f, 20.0f, 30.0f, 40.0f, -5.0f, 5.0f, c);
    const Footprint f = FootprintOf(c, 0.0f, 1.0f);
    CHECK(f.Valid());
    CHECK(f.count == 4);
    CHECK(std::fabs(FootprintArea(f) - 100.0f) < 1e-3f);
    CHECK(std::fabs(FootprintPerimeter(f) - 40.0f) < 1e-3f);
    // a frustum that never reaches the ground's slab sees nothing
    BoxCorners(10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, c);
    CHECK(!FootprintOf(c, 0.0f, 1.0f).Valid());
}

UNIT_TEST(ext_map_overlays_render_footprint_reaches_past_the_horizon_rays) {
    // looking 20 degrees down from 60 up: the top of the screen looks above the horizon, its rays meet no
    // ground, but the far plane's bottom edge still sees ground about 500 away
    Point3 c[8];
    ViewCorners(0.0f, 0.0f, 60.0f, 0.35f, 0.0f, 512.0f, c);
    const Footprint f = FootprintOf(c, 0.0f, 10.0f);
    CHECK(f.Valid());
    float far = 0.0f;
    for (int i = 0; i < f.count; ++i) far = std::max(far, f.points[i].y);
    CHECK(far > 400.0f);
}

UNIT_TEST(ext_map_overlays_render_cell_rows_cover_the_footprint) {
    Point3 c[8];
    BoxCorners(10.0f, 20.0f, 30.0f, 40.0f, -5.0f, 5.0f, c);
    const Footprint f = FootprintOf(c, 0.0f, 1.0f);
    CellRows rows;
    CellRowsOf(f, 64, 1, 2.0f, rows);
    CHECK(rows.step == 1 && rows.first.size() == 64u);
    CHECK(rows.first[26] > rows.last[26]);   // more than the margin above
    CHECK(rows.first[27] == 8 && rows.last[27] == 22);   // a tile row touching the widened edge counts
    CHECK(rows.first[35] == 8 && rows.last[35] == 22);
    CHECK(rows.first[42] == 8 && rows.last[42] == 22);
    CHECK(rows.first[43] > rows.last[43]);
    // step 4: rows of four tiles, clamped to the map
    CellRowsOf(f, 64, 4, 2.0f, rows);
    CHECK(rows.first.size() == 16u);
    CHECK(rows.first[7] == 2 && rows.last[7] == 5);
    BoxCorners(-100.0f, 500.0f, -100.0f, 500.0f, -5.0f, 5.0f, c);
    CellRowsOf(FootprintOf(c, 0.0f, 1.0f), 64, 4, 2.0f, rows);
    for (size_t r = 0; r < rows.first.size(); ++r) CHECK(rows.first[r] == 0 && rows.last[r] == 15);
}

UNIT_TEST(ext_map_overlays_render_step_keeps_the_cells_under_the_cap_and_still_while_turning) {
    Point3 c[8];
    BoxCorners(0.0f, 99.0f, 0.0f, 99.0f, -5.0f, 5.0f, c);
    CHECK(StepFor(FootprintOf(c, 0.0f, 1.0f), 512, 0) == 1);
    BoxCorners(-10.0f, 600.0f, -10.0f, 600.0f, -5.0f, 5.0f, c);
    CHECK(StepFor(FootprintOf(c, 0.0f, 1.0f), 512, 0) == 4);   // the whole map: 128 x 128 cells
    BoxCorners(-10.0f, 5000.0f, -10.0f, 5000.0f, -5.0f, 5.0f, c);
    CHECK(StepFor(FootprintOf(c, 0.0f, 1.0f), 4096, 0) == 32);
    // a far view turned through a full circle: one step at every yaw, and the cells under the cap
    int first = 0;
    for (int k = 0; k < 64; ++k) {
        ViewCorners(256.0f, 256.0f, 150.0f, 0.8f, 6.2831853f * static_cast<float>(k) / 64.0f, 512.0f, c);
        const Footprint f = FootprintOf(c, 0.0f, 20.0f);
        const int step = StepFor(f, 512, first);
        if (k == 0) first = step;
        CHECK(step == first);
        CellRows rows;
        CellRowsOf(f, 512, step, kTerrainMargin, rows);
        int cells = 0;
        for (size_t r = 0; r < rows.first.size(); ++r) cells += std::max(0, rows.last[r] - rows.first[r] + 1);
        CHECK(cells <= kMaxCells);
        CHECK(static_cast<double>(cells) <= EstimatedCells(f, 512, step, kTerrainMargin));
    }
    CHECK(first > 1);
    // a coarser step is kept until a finer one fits in kFinerShare of the cap
    BoxCorners(0.0f, 200.0f, 0.0f, 200.0f, -5.0f, 5.0f, c);   // step 2 fits, in 0.71 of the cap
    const Footprint f = FootprintOf(c, 0.0f, 1.0f);
    CHECK(StepFor(f, 512, 0) == 2);
    CHECK(StepFor(f, 512, 4) == 4);
    CHECK(StepFor(f, 512, 16) == 2);
}

UNIT_TEST(ext_map_overlays_render_colour_is_swizzled_and_scaled) {
    CHECK(VertexColor(kRedOpaque, 100) == 0xFFFF0000u);
    CHECK(VertexColor(0x80332211u, 100) == 0x80112233u);
    CHECK((VertexColor(kRedOpaque, 50) >> 24) == 127u);
    CHECK(VertexColor(0x00FFFFFFu, 100) >> 24 == 0u);
    // the map window's texture (B8G8R8A8): red and blue trade places, alpha and green stay
    CHECK(TexturePixel(kRedOpaque) == 0xFFFF0000u);
    CHECK(TexturePixel(0x80332211u) == 0x80112233u);
}

// The minimap texture is upside down against the tiles, as retail's overlayPixels
// (Minimap::ClearTerrain: (width - y - 1) * width + x): tile row 0 is the texture's last row.
UNIT_TEST(ext_map_overlays_render_minimap_texture_flips_the_rows) {
    const int size = 8;
    CHECK(MinimapTextureIndex(0, 0, size) == static_cast<size_t>(7 * size));
    CHECK(MinimapTextureIndex(3, 0, size) == static_cast<size_t>(7 * size + 3));
    CHECK(MinimapTextureIndex(5, 7, size) == 5u);
    // retail's own paint of a 1x1 footprint at (x, y) lands on the same index
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const size_t retail = static_cast<size_t>((size - y - 0 - 1) * size + x);
            CHECK(MinimapTextureIndex(x, y, size) == retail);
        }
    }
}

UNIT_TEST(ext_map_overlays_render_exact_step_draws_only_coloured_tiles) {
    const int size = 16;
    std::vector<uint32_t> image(size * size, 0u);
    image[5 * size + 4] = kRedOpaque;   // tile (4, 5)
    image[9 * size + 9] = kRedOpaque;   // tile (9, 9), outside the cells below
    CellRows rows = AllCells(size, 1);
    for (int r = 0; r < size; ++r) {
        rows.first[static_cast<size_t>(r)] = 0;
        rows.last[static_cast<size_t>(r)] = r < 8 ? 7 : -1;
    }
    CoarseGrid grid;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(rows, size, -8.0f, -8.0f, image, size, 100, Flat, grid, out);
    CHECK(out.size() == 6u);
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    for (const TerrainVertex& v : out) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
        CHECK(v.z == 3.0f + kTerrainLift);
        CHECK(v.color == 0xFFFF0000u);
        CHECK(v.u == Ext::ShowGrid::kSolidU && v.v == Ext::ShowGrid::kSolidV);
    }
    CHECK(minX == -4.0f && maxX == -3.0f && minY == -3.0f && maxY == -2.0f);
    // clockwise seen from above (the winding of show_grid's strips): both triangles' signed areas < 0
    for (int t = 0; t < 2; ++t) {
        const TerrainVertex* v = &out[static_cast<size_t>(3 * t)];
        CHECK((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x) < 0.0f);
    }
    // tile (4, 5), column + row odd: split along (4, 5)-(5, 6), as the terrain mesh
    CHECK(out[0].x == -4.0f && out[0].y == -3.0f && out[1].x == -3.0f && out[1].y == -2.0f);
    // tile (4, 4), even: split along (5, 4)-(4, 5)
    image[5 * size + 4] = 0u;
    image[4 * size + 4] = kRedOpaque;
    BuildTerrainVertices(rows, size, 0.0f, 0.0f, image, size, 100, Flat, grid, out);
    CHECK(out.size() == 6u);
    CHECK(out[1].x == 4.0f && out[1].y == 5.0f && out[2].x == 5.0f && out[2].y == 4.0f);
    for (int t = 0; t < 2; ++t) {
        const TerrainVertex* v = &out[static_cast<size_t>(3 * t)];
        CHECK((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x) < 0.0f);
    }
}

UNIT_TEST(ext_map_overlays_render_coarse_step_blends_and_lies_over_the_ground) {
    const int size = 16;
    std::vector<uint32_t> image(size * size, 0u);
    image[7 * size + 7] = kRedOpaque;   // tile (7, 7): vertex (8, 8) touches it
    CoarseGrid grid;
    grid.Reset(4, size);
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(AllCells(size, 4), size, 0.0f, 0.0f, image, size, 100, Bump, grid, out);
    // vertex (8, 8) is the only coloured corner: the four cells around it
    CHECK(out.size() == 4u * 6u);
    bool sawCorner = false;
    for (const TerrainVertex& v : out) {
        if (v.x == 8.0f && v.y == 8.0f) {
            sawCorner = true;
            CHECK(v.color >> 24 == 0xFFu / 4u);    // one tile of four
            CHECK((v.color & 0x00FFFFFFu) == 0x00FF0000u);   // its colour, not darkened by the clear ones
            CHECK(v.z == 5.0f + kTerrainLift);    // over the bump
        }
    }
    CHECK(sawCorner);
    // a corner within half a cell of the bump also lies over it: vertex (8, 4) is 4 away (more than 2), flat
    for (const TerrainVertex& v : out) {
        if (v.x == 8.0f && v.y == 4.0f) CHECK(v.z == 0.0f + kTerrainLift);
    }
    // a new image is picked up once the grid's colours are invalidated
    std::vector<uint32_t> empty(size * size, 0u);
    BuildTerrainVertices(AllCells(size, 4), size, 0.0f, 0.0f, empty, size, 100, Bump, grid, out);
    CHECK(out.size() == 4u * 6u);   // the remembered colours
    grid.InvalidateColors();
    BuildTerrainVertices(AllCells(size, 4), size, 0.0f, 0.0f, empty, size, 100, Bump, grid, out);
    CHECK(out.empty());
}

UNIT_TEST(ext_map_overlays_render_empty_image_draws_nothing) {
    const int size = 8;
    std::vector<uint32_t> image(size * size, 0u);
    CoarseGrid grid;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(AllCells(size, 1), size, 0.0f, 0.0f, image, size, 100, Flat, grid, out);
    CHECK(out.empty());
    grid.Reset(2, size);
    BuildTerrainVertices(AllCells(size, 2), size, 0.0f, 0.0f, image, size, 100, Flat, grid, out);
    CHECK(out.empty());
}

// The exact views (Land): white vertices with the image's texture coordinates, at step 1 and on a coarse grid.
UNIT_TEST(ext_map_overlays_render_textured_vertices_carry_the_image_coordinates) {
    const int size = 8;
    std::vector<uint32_t> image(static_cast<size_t>(size) * size, 0u);
    image[static_cast<size_t>(3) * size + 5] = 0x80112233u;   // tile (col 5, row 3)
    CoarseGrid grid;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(AllCells(size, 1), size, -4.0f, -4.0f, image, size, 50, Flat, grid, out, true);
    CHECK(out.size() == 6);   // the one tile with a colour
    for (const TerrainVertex& v : out) {
        CHECK(v.color == 0x7FFFFFFFu);   // white, alpha 255 * 50 %
        CHECK(v.u >= 5.0f / 8.0f - 1e-6f && v.u <= 6.0f / 8.0f + 1e-6f);
        CHECK(v.v >= 3.0f / 8.0f - 1e-6f && v.v <= 4.0f / 8.0f + 1e-6f);
        CHECK(std::fabs((v.x + 4.0f) / 8.0f - v.u) < 1e-6f);
        CHECK(std::fabs((v.y + 4.0f) / 8.0f - v.v) < 1e-6f);
    }
    // A coarse grid draws every cell (the texture carries the picture), over the whole image.
    grid.Reset(4, size);
    BuildTerrainVertices(AllCells(size, 4), size, -4.0f, -4.0f, image, size, 100, Flat, grid, out, true);
    CHECK(out.size() == 4 * 6);
    float lowU = 1.0f;
    float highU = 0.0f;
    for (const TerrainVertex& v : out) {
        CHECK(v.color == 0xFFFFFFFFu);
        lowU = std::min(lowU, v.u);
        highU = std::max(highU, v.u);
    }
    CHECK(lowU == 0.0f && highU == 1.0f);
}

UNIT_TEST(ext_map_overlays_render_near_steep_cells_are_refined) {
    const int size = 16;
    CoarseGrid grid;
    grid.Reset(4, size);
    std::vector<uint8_t> mask;
    // the eye just above the bump: the four cells that share vertex (8, 8) float 5 over their low ground
    Point3 nearEye;
    nearEye.x = 8.0f;
    nearEye.y = 8.0f;
    nearEye.z = 20.0f;
    RefineCells(AllCells(size, 4), size, nearEye, Bump, grid, mask);
    CHECK(mask.size() == 16u);
    int refined = 0;
    for (uint8_t m : mask) refined += m;
    CHECK(refined == 4);
    CHECK(mask[1 * 4 + 1] == 1 && mask[1 * 4 + 2] == 1 && mask[2 * 4 + 1] == 1 && mask[2 * 4 + 2] == 1);
    // from far away the same float is under kFloatPerDistance of the distance: nothing is refined
    Point3 farEye = nearEye;
    farEye.z = 5.0f / kFloatPerDistance + 100.0f;
    RefineCells(AllCells(size, 4), size, farEye, Bump, grid, mask);
    refined = 0;
    for (uint8_t m : mask) refined += m;
    CHECK(refined == 0);
    // flat ground is never refined (the grid forgets the bump's heights first)
    grid.InvalidateHeights();
    RefineCells(AllCells(size, 4), size, nearEye, Flat, grid, mask);
    refined = 0;
    for (uint8_t m : mask) refined += m;
    CHECK(refined == 0);
}

UNIT_TEST(ext_map_overlays_render_refined_cell_lies_on_the_ground) {
    const int size = 16;
    std::vector<uint32_t> image(size * size, kRedOpaque);
    CoarseGrid grid;
    grid.Reset(4, size);
    std::vector<uint8_t> mask(16, 0);
    mask[1 * 4 + 1] = 1;   // cell (1, 1): tiles 4..7 x 4..7, its corner (8, 8) on the bump
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(AllCells(size, 4), size, 0.0f, 0.0f, image, size, 100, Bump, grid, out, false, &mask);
    // 15 coarse quads and the refined cell's 16 tiles
    CHECK(out.size() == (15u + 16u) * 6u);
    // inside the refined cell the picture is on the ground: vertex (6, 6) is 0 high, where the coarse quad
    // (corner (8, 8) at 5) passed well above it
    bool sawInner = false;
    for (const TerrainVertex& v : out) {
        if (v.x == 6.0f && v.y == 6.0f) {
            sawInner = true;
            CHECK(v.z == 0.0f + kTerrainLift);
        }
    }
    CHECK(sawInner);
}
