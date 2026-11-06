// ============================================================================
// tests/unit/ext_minimap_outline_clip_test.cpp -- fix.minimap_outline_clip's segment clip
// ============================================================================
// src/ext/minimap_outline_clip/clip_rule.h without the world (docs/retail_bugs.md §31).
// ============================================================================
#include "unit.h"

#include "ext/minimap_outline_clip/clip_rule.h"

using Ext::MinimapOutlineClip::ClipSegment;
using Ext::MinimapOutlineClip::CullRect;

UNIT_TEST(ext_minimap_outline_clip_inside_is_unchanged) {
    float x0 = 10.0f, y0 = 20.0f, x1 = 100.0f, y1 = 150.0f;
    CHECK(ClipSegment(x0, y0, x1, y1, 0.0f, 0.0f, 192.0f, 192.0f));
    CHECK(x0 == 10.0f && y0 == 20.0f && x1 == 100.0f && y1 == 150.0f);
}

UNIT_TEST(ext_minimap_outline_clip_end_left_of_the_map_is_cut_at_the_edge) {
    // the retail case: drawn right to left, toward a corner past the left edge
    float x0 = 100.0f, y0 = 50.0f, x1 = -100.0f, y1 = 50.0f;
    CHECK(ClipSegment(x0, y0, x1, y1, 0.0f, 0.0f, 192.0f, 192.0f));
    CHECK(x0 == 100.0f && x1 == 0.0f && y0 == 50.0f && y1 == 50.0f);
}

UNIT_TEST(ext_minimap_outline_clip_both_ends_outside_crossing) {
    float x0 = -50.0f, y0 = 96.0f, x1 = 250.0f, y1 = 96.0f;
    CHECK(ClipSegment(x0, y0, x1, y1, 0.0f, 0.0f, 192.0f, 192.0f));
    CHECK(x0 == 0.0f && x1 == 192.0f);
}

UNIT_TEST(ext_minimap_outline_clip_wholly_outside_is_dropped) {
    float x0 = -50.0f, y0 = -10.0f, x1 = -5.0f, y1 = -40.0f;
    CHECK(!ClipSegment(x0, y0, x1, y1, 0.0f, 0.0f, 192.0f, 192.0f));
}

UNIT_TEST(ext_minimap_outline_clip_diagonal_above_the_top) {
    float x0 = 96.0f, y0 = 96.0f, x1 = 196.0f, y1 = -104.0f;
    CHECK(ClipSegment(x0, y0, x1, y1, 0.0f, 0.0f, 192.0f, 192.0f));
    CHECK(y1 == 0.0f);
    CHECK(x1 > 143.9f && x1 < 144.1f);
}

UNIT_TEST(ext_minimap_outline_clip_rect_is_the_culls_after_the_scale_round_trip) {
    // UI scale 1: the element's own rect
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;
    CullRect(818, 34, 192, 192, 1.0f, 818, 34, l, t, r, b);
    CHECK(l == 0.0f && t == 0.0f && r == 192.0f && b == 192.0f);
    // UI scale 0.37 (GameUIScale 37), the element at (489, 34) 192 wide: SetScissor keeps 1321, 91, 518 x 518,
    // the cull's right edge is 1839 * 0.37 = 680.43, so 681 (the element's right edge) is past it
    CullRect(1321, 91, 518, 518, 0.37f, 489, 34, l, t, r, b);
    CHECK(l == 0.0f && t == 0.0f);
    CHECK(r == 191.0f && b == 191.0f);
    // a side clipped to that rect lands, as whole units, where the cull keeps it
    float x0 = 332.0f, y0 = 53.0f, x1 = 137.0f, y1 = 156.0f;
    CHECK(ClipSegment(x0, y0, x1, y1, l, t, r, b));
    const int first = static_cast<int>(x0) + 489;
    CHECK(static_cast<float>(first) <= static_cast<float>(1321 + 518) * 0.37f);
}
