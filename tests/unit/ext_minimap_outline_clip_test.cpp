// ============================================================================
// tests/unit/ext_minimap_outline_clip_test.cpp -- fix.minimap_outline_clip's segment clip
// ============================================================================
// src/ext/minimap_outline_clip/clip_rule.h without the world (docs/retail_bugs.md §31).
// ============================================================================
#include "unit.h"

#include "ext/minimap_outline_clip/clip_rule.h"

using Ext::MinimapOutlineClip::ClipSegment;

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
