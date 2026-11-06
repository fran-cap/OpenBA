// ============================================================================
// tests/unit/ext_map_overlays_render_test.cpp -- ext.map_overlays' terrain triangles
// (src/ext/map_overlays/overlay_render_math.h)
// ============================================================================
// The box, the step that keeps the cost, the colour order, and the triangles on a known ground, without the
// game. The drawing itself (the screen rays, the overlay material, the map window's image) needs the game.
// ============================================================================
#include "unit.h"
#include "ext/map_overlays/overlay_render_math.h"

#include <cstdint>
#include <vector>

namespace {

using namespace Ext::MapOverlays;

float Flat(int, int) { return 3.0f; }

// A bump of 5 at vertex (8, 8), 0 elsewhere.
float Bump(int vc, int vr) { return (vc == 8 && vr == 8) ? 5.0f : 0.0f; }

constexpr uint32_t kRedOpaque = 0xFF0000FFu;   // 0xAABBGGRR: red, alpha 255

}  // namespace

UNIT_TEST(ext_map_overlays_render_box_has_a_margin_and_no_cap) {
    const int cols[] = {10, 20};
    const int rows[] = {30, 40};
    const TileBox b = TileBoxOf(cols, rows, 2, 256);
    CHECK(b.Valid());
    CHECK(b.col0 == 8 && b.col1 == 22 && b.row0 == 28 && b.row1 == 42);
    const int ec[] = {0, 1023};
    const int er[] = {0, 1023};
    const TileBox e = TileBoxOf(ec, er, 2, 1024);
    CHECK(e.Valid() && e.col0 == 0 && e.row0 == 0 && e.col1 == 1023 && e.row1 == 1023);
    CHECK(!TileBoxOf(cols, rows, 0, 256).Valid());
}

UNIT_TEST(ext_map_overlays_render_step_keeps_the_cells_under_the_cap) {
    TileBox small;
    small.col0 = 0;
    small.row0 = 0;
    small.col1 = 99;
    small.row1 = 99;
    CHECK(StepFor(small) == 1);   // 10000 tiles
    TileBox whole;
    whole.col0 = 0;
    whole.row0 = 0;
    whole.col1 = 511;
    whole.row1 = 511;
    CHECK(StepFor(whole) == 4);   // 128 x 128 cells
    whole.col1 = 4095;
    whole.row1 = 4095;
    CHECK(StepFor(whole) == 32);   // 128 x 128 cells
}

UNIT_TEST(ext_map_overlays_render_colour_is_swizzled_and_scaled) {
    CHECK(VertexColor(kRedOpaque, 100) == 0xFFFF0000u);
    CHECK(VertexColor(0x80332211u, 100) == 0x80112233u);
    CHECK((VertexColor(kRedOpaque, 50) >> 24) == 127u);
    CHECK(VertexColor(0x00FFFFFFu, 100) >> 24 == 0u);
}

UNIT_TEST(ext_map_overlays_render_exact_step_draws_only_coloured_tiles) {
    const int size = 16;
    std::vector<uint32_t> image(size * size, 0u);
    image[5 * size + 4] = kRedOpaque;   // tile (4, 5)
    image[9 * size + 9] = kRedOpaque;   // tile (9, 9), outside the box below
    TileBox box;
    box.col0 = 0;
    box.row0 = 0;
    box.col1 = 7;
    box.row1 = 7;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(box, 1, size, -8.0f, -8.0f, image, size, 100, Flat, out);
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
    // clockwise seen from above (the winding of show_grid's strips): the first triangle's signed area < 0
    const float area = (out[1].x - out[0].x) * (out[2].y - out[0].y) - (out[1].y - out[0].y) * (out[2].x - out[0].x);
    CHECK(area < 0.0f);
}

UNIT_TEST(ext_map_overlays_render_coarse_step_blends_and_lies_over_the_ground) {
    const int size = 16;
    std::vector<uint32_t> image(size * size, 0u);
    image[7 * size + 7] = kRedOpaque;   // tile (7, 7): vertex (8, 8) touches it
    TileBox box;
    box.col0 = 0;
    box.row0 = 0;
    box.col1 = 15;
    box.row1 = 15;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(box, 4, size, 0.0f, 0.0f, image, size, 100, Bump, out);
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
}

UNIT_TEST(ext_map_overlays_render_empty_image_draws_nothing) {
    const int size = 8;
    std::vector<uint32_t> image(size * size, 0u);
    TileBox box;
    box.col0 = 0;
    box.row0 = 0;
    box.col1 = 7;
    box.row1 = 7;
    std::vector<TerrainVertex> out;
    BuildTerrainVertices(box, 1, size, 0.0f, 0.0f, image, size, 100, Flat, out);
    CHECK(out.empty());
    BuildTerrainVertices(box, 2, size, 0.0f, 0.0f, image, size, 100, Flat, out);
    CHECK(out.empty());
}
