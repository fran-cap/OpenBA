// tests/unit/review_minimap_bounds_test.cpp
// review S-009: the counts Minimap::Serialize accepts on load
// (src/game/Game/minimap_bounds.h). Serialize uploads to the video device; it calls this.
// All platforms.

#include "unit.h"
#include "Game/minimap_bounds.h"
#include <cstdint>

using MinimapBounds::PixelsCoverSize;

UNIT_TEST(review_minimap_pixels_cover_size) {
    CHECK(PixelsCoverSize(768, 768 * 768, 768 * 768));
    CHECK(PixelsCoverSize(1, 1, 1));

    // The finding's input: one pixel in each array, edge 768.
    CHECK(!PixelsCoverSize(768, 1, 1));
    CHECK(!PixelsCoverSize(768, 768 * 768, 1));
    CHECK(!PixelsCoverSize(768, 1, 768 * 768));
    CHECK(!PixelsCoverSize(768, 768 * 768 - 1, 768 * 768));
    // A count above size * size (a valid save never has one either).
    CHECK(!PixelsCoverSize(768, 768 * 768 + 1, 768 * 768));

    // No edge: nothing to upload, and the repaint divides by it.
    CHECK(!PixelsCoverSize(0, 0, 0));
    CHECK(!PixelsCoverSize(-4, 16, 16));
    // size * size past int: the arrays (int counts) can never hold it.
    CHECK(!PixelsCoverSize(65536, 0, 0));
    CHECK(!PixelsCoverSize(INT32_MAX, 1, 1));
    // 46341^2 wraps a 32-bit product to a positive int.
    CHECK(!PixelsCoverSize(46341, static_cast<int32_t>(46341u * 46341u), static_cast<int32_t>(46341u * 46341u)));
}
