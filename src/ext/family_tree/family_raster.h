// src/ext/family_tree/family_raster.h -- the whole-town family tree as pixels (the PNG file)
//
// NOT RETAIL. Docs: docs/extensions/handins/FAMILYTREE.md section 4c. The same layout and the same lines as
// the SVG (family_picture.h: BuildPictureLayout, BuildPictureStrokes), drawn into an RGBA buffer. Standard
// C++: the text comes from a GlyphSource, which the game side gives the platform's font rasterizer
// (ext/resources/font_raster.h) and a unit test gives a fake one.
//
// SIZE. A big town is wider than a PNG can sensibly be: the picture is drawn at a smaller scale when it would
// pass kRasterMaxSide on a side or kRasterMaxPixels in all, and `scale` says so (the SVG stays sharp at any
// size).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ext/family_tree/family_picture.h"
#include "ext/resources/font_raster.h"

namespace Ext {
namespace FamilyTree {

constexpr int kRasterMaxSide = 16384;
constexpr int64_t kRasterMaxPixels = 40000000;   // 160 MB of RGBA

// Where the glyphs come from: `cps` rasterized at a cell height of `cellPx`.
class GlyphSource {
public:
    virtual ~GlyphSource() = default;
    virtual bool Glyphs(int cellPx, const std::vector<int32_t>& cps, Resources::RasterFont& out,
                        std::string* why) = 0;
};

// The platform's font rasterizer over the first of a few faces the system has (Segoe UI, Arial, Helvetica,
// ...).
GlyphSource& SystemGlyphs();

struct PictureRaster {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   // width * height * 4, opaque
    double scale = 1.0;          // below 1: drawn smaller than the layout's units
    bool textDrawn = false;      // false: no glyphs at any size (too small a scale, or no font)
    std::string why;             // what went wrong, when RenderPictureRaster is false
};

// False (and `why`) when there is nothing to draw or no font could be had at the normal size.
bool RenderPictureRaster(const Tree& tree, const PictureLayout& layout, const PictureOptions& options,
                         GlyphSource& glyphs,
                         PictureRaster& out);

}  // namespace FamilyTree
}  // namespace Ext
