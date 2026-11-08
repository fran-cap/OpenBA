#pragma once
// ============================================================================
// src/ext/resources/font_sheet.h -- what the font provider needs from a sheet builder
// ============================================================================
// NOT RETAIL (KITFREE RSCFONT). The font provider turns glyph bitmaps into a
// UI::Font; the sprite sheet, its UI material and its texture are block
// RSCSPRITE's generic builder (sheet_builder.*). This is the thin interface
// the provider calls; font_sheet_stub.cpp answers "no builder" until the
// coordinator points it at RSCSPRITE's (a one-file swap: implement
// BuildFontSheet over sheet_builder.h and delete the stub).
//
// Contract the provider relies on:
//   * sprite i of the built sheet is bitmap i (UI::Font::_charToGlyph indexes
//     the sheet by glyph index; the order is the base font's sprite order);
//   * each sprite is named `names[i]` ("c65"), its width/height are exactly
//     `widths[i]` x `heights[i]` (a 1x1 fully transparent sprite is legal --
//     the space is one), and the sheet is padded for filtering the kit's way
//     (`_padForFiltering = true`: a transparent texel ring around each glyph);
//   * the bitmaps are coverage (alpha) only; the texel colour is white
//     (RGB 255, the kit's sheets), alpha = the byte;
//   * the three objects are registered with the factory under the three
//     paths (Ext::Resources::SetPath + Publish, in dependency order: texture,
//     material, sheet), the material derives from `baseMaterialPath`'s
//     Graphics::MaterialInstance by reference (its Material, parameters and
//     constants; its "diffuse" texture replaced by ours), and the sheet's
//     material pointer (UI::SpriteSheet::m_materialInstance) is that material;
//   * `sheetWidth` x `sheetHeight` is a suggestion (the kit's size); the
//     builder may choose another power-of-two size that fits.
// ============================================================================

#include <cstdint>

namespace UI { class SpriteSheet; }
namespace Graphics { class MaterialInstance; }
namespace Video { class ImageBuffer; }

namespace Ext {
namespace Resources {

struct FontSheetInput {
    int32_t               count = 0;
    const uint8_t* const* bitmaps = nullptr;   // count bitmaps, widths[i] * heights[i] bytes each, top row first
    const int32_t*        widths = nullptr;
    const int32_t*        heights = nullptr;
    const char* const*    names = nullptr;     // "c<codepoint>"
    int32_t               sheetWidth = 0, sheetHeight = 0;   // suggestion
    const char*           sheetPath = nullptr;      // "Build/SharpFont_32Sheet.rsc:resource"
    const char*           materialPath = nullptr;   // "Font/SharpFont_32.rsc:material"
    const char*           texturePath = nullptr;    // "Font/SharpFont_32.rsc:texture"
    const char*           baseMaterialPath = nullptr;   // "Font/FontMaterial.rsc"
};

struct FontSheetOutput {
    UI::SpriteSheet*            sheet = nullptr;
    Graphics::MaterialInstance* material = nullptr;
    Video::ImageBuffer*         texture = nullptr;
    int32_t                     sheetWidth = 0, sheetHeight = 0;   // what was built
};

// False when no builder is linked or the build failed (logged as [ExtRes]).
bool BuildFontSheet(const FontSheetInput& in, FontSheetOutput& out);

}  // namespace Resources
}  // namespace Ext
