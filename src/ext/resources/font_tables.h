#pragma once
// ============================================================================
// src/ext/resources/font_tables.h -- the pure part of the font provider (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL. Standard C++ only (the unit tests link font_tables.cpp on its
// own): the variant path grammar, the names a provided variant carries (the
// kit's, so a provided Font/SharpFont_32.rsc is field for field what
// tools/ext_fonts.py + the kit compiled), the codepoint set from a sprite
// sheet's names, the two measured widths UI::Font::Serialize stores, and the
// sheet size rule tools/ext_fonts.py used.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace Resources {

// The game's four fonts (the kit's resource/Font/*.rsc names) and their
// compiled heights; sharp_ui.cpp's kFontNames / sharp_math's variant set.
struct KitFont { const char* name; int32_t height; };
constexpr KitFont kKitFonts[] = { {"Font", 24}, {"FontSmall", 14}, {"FontMedium", 18}, {"FontLarge", 48} };
constexpr int kKitFontCount = 4;

// What a provider path names. "Font/SharpFont_32.rsc:material" ->
// kit "Font", px 32, part "material"; "Build/SharpFontSmall_19Sheet.rsc:resource"
// -> kit "FontSmall", px 19, part "sheet". A missing ":ident" is "resource".
struct VariantPath {
    int32_t kit = -1;          // index into kKitFonts
    int32_t px = 0;
    std::string part;          // "resource" (the UI::Font), "material", "texture", "sheet"
};
bool ParseVariantPath(const char* path, VariantPath& out);

// The paths and names of one variant, as the kit compiled them
// (src/ext/ui/Font/Sharp<Kit>_<px>.rsc): the provider registers the objects
// under these and UI::Font carries the names in its own fields.
struct VariantNames {
    std::string file;          // "SharpFont_32"
    std::string fontPath;      // "Font/SharpFont_32.rsc:resource"
    std::string materialPath;  // "Font/SharpFont_32.rsc:material"
    std::string texturePath;   // "Font/SharpFont_32.rsc:texture"
    std::string sheetPath;     // "Build/SharpFont_32Sheet.rsc:resource"
    std::string sheetName;     // "Build/SharpFont_32Sheet.rsc"   (UI::Font::_sheetName)
    std::string materialName;  // "Font/SharpFont_32.rsc:material" (UI::Font::_materialName)
    std::string imageName;     // "Build/uiSharpFont_32Image.png"  (UI::Font::_imageName)
    std::string baseFontPath;      // "Font/Font.rsc"
    std::string baseMaterialPath;  // "Font/FontMaterial.rsc"
};
VariantNames MakeVariantNames(int32_t kit, int32_t px);

// The codepoint a glyph sprite is named for: "c65" -> 65; -1 for any other name.
int32_t CodepointFromSpriteName(const char* name);

// One glyph row as UI::Font stores it (UI::Font::Kerning, field for field).
struct GlyphRow { int32_t preAdvance, postKern, postAdvance, offsetY; };

// The advance of a run of glyphs by UI::Font::Serialize's MeasureLiteral rule
// (font.cpp): sum of pre + post + the carried kern, the last kern added at the
// end. `rows` are the glyphs of the run in order. UI::Font::_ellipsisWidth is
// this over "...", _spaceWidth over " ".
int32_t MeasureRun(const GlyphRow* rows, int32_t count);

// The sheet tools/ext_fonts.py asked the kit for: the smallest of 128x128,
// 256x128, 256x256, 512x256, 512x512 holding 64 * px^2 texels. Returns false
// above 512x512. A builder may pack tighter; this is the kit's size.
bool KitSheetSize(int32_t px, int32_t& width, int32_t& height);

}  // namespace Resources
}  // namespace Ext
