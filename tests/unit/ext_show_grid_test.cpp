// ============================================================================
// tests/unit/ext_show_grid_test.cpp -- ext.show_grid's decisions (src/ext/show_grid/show_grid_math.h)
// ============================================================================
// Which tiles the grid covers, the lines' color and width, and the triangles of the lines on a known ground,
// without the game. The drawing itself (the screen rays, the game's tile overlay material) needs the running
// game.
// ============================================================================
#include "unit.h"
#include "ext/show_grid/show_grid_math.h"

#include <cmath>
#include <vector>

namespace {

using namespace Ext::ShowGrid;

float Flat(int, int) { return 2.0f; }

}  // namespace

UNIT_TEST(ext_show_grid_box_has_a_margin_and_stays_in_the_map) {
    const int cols[] = {10, 20, 15};
    const int rows[] = {30, 31, 40};
    const Box b = BoxOf(cols, rows, 3, 256);
    CHECK(b.Valid());
    CHECK(b.col0 == 9 && b.col1 == 21 && b.row0 == 29 && b.row1 == 41);
    CHECK(b.Tiles() == 13 * 13);
    // against the map's edges
    const int ec[] = {0, 63};
    const int er[] = {0, 63};
    const Box e = BoxOf(ec, er, 2, 64);
    CHECK(e.Valid() && e.col0 == 0 && e.row0 == 0 && e.col1 == 63 && e.row1 == 63);
}

UNIT_TEST(ext_show_grid_box_is_empty_without_cells_or_when_too_big) {
    const int c[] = {1};
    const int r[] = {1};
    CHECK(!BoxOf(c, r, 0, 256).Valid());     // no ray hit the ground
    CHECK(!BoxOf(c, r, 1, 0).Valid());       // no map
    CHECK(BoxOf(c, r, 1, 256).Valid());      // one cell and its margin
    CHECK(BoxOf(c, r, 1, 256).Tiles() == 9);
    // a view that spans the whole 512 map is zoomed out too far
    const int bc[] = {0, 511};
    const int br[] = {0, 511};
    CHECK(!BoxOf(bc, br, 2, 512).Valid());
    // just inside the cap: 98 x 98 = 9604 tiles
    const int ic[] = {10, 10 + 95};
    const int ir[] = {10, 10 + 95};
    const Box inside = BoxOf(ic, ir, 2, 512);
    CHECK(inside.Valid() && inside.Tiles() <= kMaxTiles);
}

UNIT_TEST(ext_show_grid_color_width_and_bold_lines) {
    CHECK(IsBoldLine(0) && IsBoldLine(10) && IsBoldLine(70));
    CHECK(!IsBoldLine(1) && !IsBoldLine(9) && !IsBoldLine(15));
    // 100% is fully opaque either way; 50% is alpha 127, and a bold line is 60% of the way to 255
    CHECK(AlphaOf(100, false) == 255 && AlphaOf(100, true) == 255);
    CHECK(AlphaOf(50, false) == 127 && AlphaOf(50, true) == 127 + (255 - 127) * 6 / 10);
    CHECK(AlphaOf(0, false) == 0 && AlphaOf(-5, false) == 0 && AlphaOf(250, false) == 255);   // clamped
    CHECK(AlphaOf(75, true) > AlphaOf(75, false));
    LineStyle white;
    white.opacity = 100;
    white.shade = 100;
    CHECK(LineColor(white, false) == 0xFFFFFFFFu);
    LineStyle black = white;
    black.shade = 0;
    CHECK(LineColor(black, false) == 0xFF000000u);
    LineStyle grey = white;
    LineStyle red;
    red.shade = 100;
    red.saturation = 100;
    red.hue = 0;
    CHECK((LineColor(red, false) & 0x00FFFFFFu) == 0x00FF0000u);
    red.hue = 120;
    CHECK((LineColor(red, false) & 0x00FFFFFFu) == 0x0000FF00u);
    red.hue = 240;
    CHECK((LineColor(red, false) & 0x00FFFFFFu) == 0x000000FFu);
    red.hue = 300;
    CHECK((LineColor(red, false) & 0x00FFFFFFu) == 0x00FF00FFu);
    red.hue = 0;
    red.saturation = 0;   // no color: white again, whatever the hue
    CHECK((LineColor(red, false) & 0x00FFFFFFu) == 0x00FFFFFFu);
    grey.shade = 50;
    grey.opacity = 40;
    CHECK((LineColor(grey, false) & 0x00FFFFFFu) == 0x007F7F7Fu);
    CHECK((LineColor(grey, false) >> 24) == AlphaOf(40, false));
    CHECK((LineColor(grey, true) >> 24) == AlphaOf(40, true));
    // widths: hundredths of a tile, a bold line two thirds wider, always inside 0.01 .. 0.30
    LineStyle w;
    w.width = 6;
    CHECK(std::fabs(HalfWidth(w, false) - 0.03f) < 1e-6f);
    CHECK(std::fabs(HalfWidth(w, true) - 0.05f) < 1e-6f);
    w.width = 0;
    CHECK(std::fabs(HalfWidth(w, false) - 0.005f) < 1e-6f);
    w.width = 999;
    CHECK(std::fabs(HalfWidth(w, false) - 0.15f) < 1e-6f);
}

// The lines are cached by the view box; the ground under the box is part of what they were built from.
UNIT_TEST(ext_show_grid_height_hash_follows_the_ground_under_the_box) {
    Box box;
    box.col0 = 2;
    box.row0 = 3;
    box.col1 = 4;
    box.row1 = 5;
    const auto flat = [](int, int) { return 100; };
    const uint64_t base = HeightHash(box, flat);
    CHECK(HeightHash(box, flat) == base);
    // one vertex of the box (the far corner, a tile's last corner) changes
    const auto edited = [](int vc, int vr) { return (vc == 5 && vr == 6) ? 101 : 100; };
    CHECK(HeightHash(box, edited) != base);
    // a vertex outside the box does not matter
    const auto outside = [](int vc, int vr) { return (vc == 6 || vr == 7) ? 101 : 100; };
    CHECK(HeightHash(box, outside) == base);
    // a different ground, the same box
    const auto other = [](int vc, int vr) { return 100 + vc - vr; };
    CHECK(HeightHash(box, other) != base);
}

UNIT_TEST(ext_show_grid_same_box) {
    Box a;
    a.col0 = 1;
    a.row0 = 2;
    a.col1 = 3;
    a.row1 = 4;
    Box b = a;
    CHECK(SameBox(a, b));
    b.row1 = 5;
    CHECK(!SameBox(a, b));
}

UNIT_TEST(ext_show_grid_lines_cover_every_tile_edge_once) {
    Box box;
    box.col0 = 0;
    box.row0 = 0;
    box.col1 = 1;     // 2 columns
    box.row1 = 2;     // 3 rows
    LineStyle style;
    const std::vector<LineVertex> tri = BuildLines(box, -10.0f, -20.0f, style, Flat);
    // 3 vertex columns x 3 segments + 4 vertex rows x 2 segments = 17 segments of 6 vertices
    CHECK(tri.size() == 17u * 6u);
    for (const LineVertex& v : tri) {
        CHECK(std::fabs(v.z - (2.0f + kLift)) < 1e-6f);   // flat ground plus the lift
        CHECK(v.u == kSolidU && v.v == kSolidV);
    }
    // the first strip is the vertical line at vertex column 0 (a bold one: 0 is a multiple of 10) from row 0
    // to row 1: its corners are x = -10 +- 0.05, y = -20 and -19
    const float hw = HalfWidth(style, true);
    CHECK(std::fabs(tri[0].x - (-10.0f - hw)) < 1e-5f && std::fabs(tri[0].y - (-20.0f)) < 1e-5f);
    CHECK(std::fabs(tri[1].x - (-10.0f + hw)) < 1e-5f && std::fabs(tri[1].y - (-19.0f)) < 1e-5f);
    CHECK((tri[0].color >> 24) == AlphaOf(style.opacity, true));
    // vertex line 1 is not bold
    const size_t second = 6u * 3u;   // after the three segments of column 0
    CHECK((tri[second].color >> 24) == AlphaOf(style.opacity, false));
    // no box, no triangles
    CHECK(BuildLines(Box(), 0.0f, 0.0f, style, Flat).empty());
}

UNIT_TEST(ext_show_grid_lines_follow_the_ground) {
    Box box;
    box.col0 = 3;
    box.row0 = 4;
    box.col1 = 3;
    box.row1 = 4;     // one tile
    LineStyle style;
    auto slope = [](int vc, int vr) {
        return 0.5f * static_cast<float>(vc) + 0.25f * static_cast<float>(vr);
    };
    const std::vector<LineVertex> tri = BuildLines(box, 0.0f, 0.0f, style, slope);
    CHECK(tri.size() == 4u * 6u);   // 2 vertical + 2 horizontal segments
    // the first strip runs from vertex (3, 4) to (3, 5): its heights are the ground there plus the lift
    CHECK(std::fabs(tri[0].z - (slope(3, 4) + kLift)) < 1e-6f);
    CHECK(std::fabs(tri[1].z - (slope(3, 5) + kLift)) < 1e-6f);
}
