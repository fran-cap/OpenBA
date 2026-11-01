#pragma once
// ============================================================================
// src/ext/resources/font_raster.h -- the run-time glyph rasteriser (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL. ext.sharp_ui's font variants (Font/Sharp<Kit>_<px>.rsc) are the
// game's four fonts rasterised at another pixel height. The mod kit did that
// with its profile Runtime DLL's UI::Font::Compile (Runtime-x64-profile.dll
// 0x180159820, read in Ghidra; docs/extensions/handins/RSCFONT.md "The kit's
// rule"), which is Win32 GDI on a display DC:
//   CreateFontW(+px, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_RASTER_PRECIS,
//               0, PROOF_QUALITY, 0, face)    (+px = the CELL height)
//   GetTextMetricsW: _lineHeight = tmHeight (== px), _baseLine = tmDescent
//   per codepoint in [tmFirstChar, tmLastChar], ascending:
//     GetCharABCWidthsW (else GetCharABCWidthsFloatW, truncated) ->
//       preAdvance  = abcA
//       postKern    = abcC
//       postAdvance = abcB
//     GetGlyphOutlineW(GGO_GRAY8_BITMAP, identity MAT2) ->
//       offsetY     = gmptGlyphOrigin.y - gmBlackBoxY
//       the sprite  = gmBlackBoxX x gmBlackBoxY, white, alpha =
//                     (int)(level / 64 * 255) == level * 4 - 1 (0 stays 0);
//                     a 0-byte glyph (the space) is a 1x1 transparent sprite
// The ABC widths are the font's realisation on the DC: a display DC realises
// it antialiased (B == the GRAY8 box on most glyphs); a monochrome memory DC
// would answer the bilevel box. Matches all 54 kit-compiled variants and the
// three base fonts row for row (5472 / 5472, tests/unit/ext_font_raster_test.cpp).
//
// Standard C++ interface; the body is the platform seam
// src/platform/win/font_raster_win.cpp (GDI) with the posix twin
// src/platform/posix/font_raster_posix.cpp answering "absent".
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace Resources {

// The typeface the game's fonts are compiled from (the kit's Font/*.rsc
// `_fontName`); sharp_ui checks the same name on UI::Font::_string20.
constexpr const char* kGameFace = "FrancophilSans";

struct RasterGlyph {
    int32_t codepoint = 0;
    int32_t preAdvance = 0, postKern = 0, postAdvance = 0, offsetY = 0;   // UI::Font::Kerning, field for field
    int32_t width = 0, height = 0;    // the sprite: width x height coverage bytes, row-major, top row first
    std::vector<uint8_t> alpha;       // 0..255, the kit's level * 4 - 1 mapping already applied
};

struct RasterFont {
    std::string face;          // what GetTextFace returned (== the face asked for, or the call failed)
    int32_t     height = 0;    // the pixel height asked for == tmHeight == UI::Font::_lineHeight
    int32_t     baseLine = 0;  // tmDescent == UI::Font::_baseLine
    std::vector<RasterGlyph> glyphs;   // in the order of `codepoints`
};

// Rasterises `count` codepoints of `face` at `pixelHeight`. False (and `why`
// filled) when the face is not available (never a substitute: the face name
// GDI hands back is compared to the one asked for), when the height is not
// sane, or on a GDI failure. The face is looked up installed first, then as
// `<exe dir>/<face>.ttf` loaded privately (AddFontResourceEx FR_PRIVATE) --
// the release package ships it beside the exe; it is never in the git tree.
bool RasterizeFont(const char* face, int32_t pixelHeight, const int32_t* codepoints,
                   int32_t count, RasterFont& out, std::string* why);

// True when `face` can be selected (installed or the private file beside the
// exe); the private load is remembered for the process. `why` says which.
bool FaceAvailable(const char* face, std::string* why);

// The faces a text picture (the family tree's) tries, the likeliest first, and how many. Each platform's
// rasterizer says what it does best: Windows asks GDI for installed faces, so a system face leads; the
// others look up one face file per process, so the game's own face (kGameFace) leads there.
const char* const* DefaultFaces(size_t& count);

}  // namespace Resources
}  // namespace Ext
