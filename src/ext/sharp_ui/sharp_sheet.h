// ============================================================================
// src/ext/sharp_ui/sharp_sheet.h -- ext.sharp_ui's magnified sprite sheet, the
// pure half (NOT RETAIL; docs/extensions/handins/SHARP_SPRITES.md)
// ============================================================================
// The game draws its dialog sprites from ONE sheet (Dialog/SpriteSheet.rsc,
// ~550 sprites in a 1024 x 1024 atlas). While the UI is magnified, that sheet
// is drawn from a copy of its texture `scale` times larger, built at load from
// the player's own sheet:
//   1. every sprite rectangle of the base atlas (with its filtering pad ring)
//      is upscaled bilinearly into the same rectangle times `scale`, sampling
//      only inside that rectangle, so no neighbour bleeds in;
//   2. the sprites we have art for (sharp_sprites_data_<N>x.h: our own drawing and
//      third-party icons, matched by sprite NAME, exactly `scale` x the base
//      sprite's size) are written over their rectangle, and the pad ring
//      around it is refilled from the art's own edge, `pad * scale` wide.
// Every rectangle is the base one times `scale`, so a sprite's normalised UVs
// are the base sheet's: the swap changes the material, never a UV.
// Nothing here ships retail pixels: the base atlas is read back from the
// player's install at run time.
//
// The factor is a parameter: the art is baked at 2 and 4 (one generated header
// per factor, tools/sharp_sprites/pack.py --scale N); the factor is picked in
// sharp_scaled.cpp. Nothing here depends on it.
//
// Standard C++ only: tests/unit/ext_sharp_sheet_test.cpp includes it.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace Ext {
namespace SharpUI {

// One sprite of our art: its sheet NAME and straight RGBA8 pixels, row-major,
// top-down, `width * height * 4` bytes (sharp_sprites_data_<N>x.h).
struct ArtSprite {
    const char*    name;
    int            width;
    int            height;
    const uint8_t* rgba;
};

// A sprite's rectangle in an atlas, in texels.
struct SheetRect {
    int x;
    int y;
    int width;
    int height;
};

// A sprite's rectangle in a `sheetW x sheetH` atlas from its UVs (pixel edge /
// sheet size), rounded to whole texels. A compiled record's x / y are NOT its
// atlas position (the kit writes its pad offset, 1 / 1, there): the UVs are.
SheetRect RectFromUvs(float u0, float v0, float u1, float v1, int sheetW, int sheetH);

// `r` in a sheet `scale` times larger.
SheetRect ScaleRect(const SheetRect& r, int scale);

// `r` grown by `ring` on every side and clipped to a `sheetW x sheetH` atlas.
SheetRect PadRect(const SheetRect& r, int ring, int sheetW, int sheetH);

// The art for the base sprite `name` of size `baseW x baseH`, or null: the
// name must match and the art must be exactly `scale` times the base size (a
// mod that replaced the sheet keeps its own sprite).
const ArtSprite* FindArt(const ArtSprite* table, int count, const char* name, int baseW, int baseH,
                         int scale);

// Bilinear upscale of the texels of `src` (BGRA8, srcW x srcH) inside `from`
// into `to` = ScaleRect(from, scale) of `dst` (BGRA8, scale * srcW wide):
// samples are clamped to `from`, so nothing outside it contributes.
void UpscaleRect(const uint8_t* src, int srcW, uint8_t* dst, const SheetRect& from, int scale);

// Writes art (RGBA8, r.width x r.height) into `r` of `dst` (BGRA8, dstW x dstH)
// and refills the `ring` texels around it from the art's own edge (corners
// included), clipped to the atlas: the kit's _padForFiltering rule, at scale.
void PlaceArt(uint8_t* dst, int dstW, int dstH, const SheetRect& r, const uint8_t* rgba, int ring);

// One base sprite: its rectangle and the art drawn over it (null: upscaled).
struct ScaledSprite {
    SheetRect      rect;
    const uint8_t* art;
};

// The ScaledSprite of one record of a loaded sheet (UI::SpriteInfo): its
// rectangle from the UVs (RectFromUvs), and the art FindArt gives its name and
// record size, only when that rectangle is the record's own width x height
// (PlaceArt reads the art at the rectangle's size): else no art, upscaled.
ScaledSprite SpriteFromRecord(const ArtSprite* table, int count, const char* name, int recordW, int recordH,
                              float u0, float v0, float u1, float v1, int sheetW, int sheetH, int scale);

// The whole sheet: `out` = (scale * baseW) x (scale * baseH) BGRA8, zero where
// no sprite is. `pad` is the base atlas's ring (1 with _padForFiltering, else
// 0). Returns the number of sprites drawn from art.
int BuildScaledSheet(const uint8_t* baseBgra, int baseW, int baseH, int scale, int pad,
                     const ScaledSprite* sprites, int count, std::vector<uint8_t>& out);

}  // namespace SharpUI
}  // namespace Ext
