#pragma once
// src/game/Game/minimap_bounds.h
// The test Minimap::Serialize (0x1400a57f0) applies on load to the three counts it reads: the
// overlay pixel count (+0x28), the terrain pixel count (+0x18) and the edge (+0x04). Pure, so
// the unit tests reach it (tests/unit/review_minimap_bounds_test.cpp).

#include <cstdint>

namespace MinimapBounds {

// NOT RETAIL (review S-009): retail trusts the three independent counts. The upload
// (CopyToTexture(pixels, size, size)), Minimap::ClearTerrain and the terrain repaint
// (CopyFromTexture) all read or write size * size pixels of both arrays, so a save whose
// arrays hold fewer is a heap over-read on load and an overflow write on the first repaint.
// A valid save holds exactly size * size pixels in each (GameUpdateLoop::Load's
// SetSize(dim * dim), dim > 0).
constexpr bool PixelsCoverSize(int32_t size, int32_t overlayCount, int32_t terrainCount) {
    if (size <= 0) {
        return false;
    }
    const int64_t pixels = static_cast<int64_t>(size) * size;
    return overlayCount == pixels && terrainCount == pixels;
}

}  // namespace MinimapBounds
