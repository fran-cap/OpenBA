// ============================================================================
// src/ext/sharp_ui/sharp_math.h -- ext.sharp_ui's pure decisions
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_sharp_ui_test.cpp links
// sharp_math.cpp on its own). docs/extensions/handins/SHARPUI.md has the why
// (section 12: the user's verdict -- no scale snap; scale-matched fonts).
//
// UNITS. Retail's `UserInterface::_uiScale` is LAYOUT UNITS PER DEVICE PIXEL:
// the layout space is (int)(backBuffer * _uiScale) (userinterface_draw.cpp,
// element_common.h:169) and UserInterface::Draw's orthographic camera maps it
// onto the whole back buffer. So the device-pixel RATIO (device pixels per
// layout unit) is 1 / _uiScale: the Options slider's 75% (_uiScale 0.75) is
// ratio 1.333 (a bigger UI), 150% is ratio 0.667 (a smaller UI). _uiScale is
// retail's slider / 100 on every path; this extension never changes it.
//
// FONT VARIANTS. A UI::Font's `_lineHeight` is the pixel height the kit's
// compiler rasterised it at (`_fontHeight` in Font/*.rsc; Font::Serialize reads
// it second, after the face name). Text is crisp when its glyphs are drawn one
// texel per device pixel, i.e. from a font rasterised at
// `_lineHeight * pxPerUnit` pixels. src/ext/ui/Font/SharpFont<Name>_<px>.rsc
// (written by tools/ext_fonts.py, which mirrors VariantLow / VariantHigh /
// VariantStep below) holds those variants for the slider's whole range:
//   heights  = every VariantStep(H)-th pixel height from round(H * 2/3)
//              (slider 150%) to round(H * 4/3) (slider 75%), H itself excluded
//   step     = 1, or 2 for a base height >= kVariantCoarseFrom (FontLarge, 48:
//              a 2-pixel step is a 2% size error at most)
// PickVariantHeight takes the nearest; the base font itself when it is nearer
// (or the ratio is outside the compiled range).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/SharpDraw.h"   // UI::SharpGlyphPlace (standard C++)

namespace Ext {
namespace SharpUI {

// The layout extent retail derives from a back-buffer extent and _uiScale:
// (int)((float)extent * scale), exactly element_common.h:169's arithmetic.
int32_t LayoutExtent(int32_t backBufferExtent, float uiScale);

// What a frame drawn at `uiScale` gets.
struct DrawChoice {
    int32_t layoutWidth, layoutHeight;   // LayoutExtent of each axis
    float   pxPerUnitX, pxPerUnitY;      // backBuffer / layout, per axis (0 when degenerate)
    float   ratio;                       // 1 / uiScale (the trace's ratio)
    int     integerRatio;                // round(pxPerUnitX) when `integer`, else 0
    bool    integer;   // both axes within kIntegerTolerance of the same integer >= 1
    bool    snap;      // snap quad corners to whole device pixels (any sane frame)
    bool    point;     // point-sample the UI sheets: integer ratio >= 2 only (never
                       // on retail's 75-150% slider; kept, inert -- SHARPUI.md §12)
};
constexpr float kIntegerTolerance = 0.01f;
DrawChoice ChooseDraw(float uiScale, int32_t backBufferWidth, int32_t backBufferHeight);

// ---- font variants ---------------------------------------------------------
constexpr int kVariantCoarseFrom = 40;
// The largest base height that has variants (FontLarge is 48). A font's
// _lineHeight comes from its file unchecked (review F-229): a larger base gets
// none, so every function below keeps the retail font for it, and the thirds
// arithmetic (at most 8 x base) never nears INT_MAX.
constexpr int kMaxFontBaseHeight = 512;
int VariantLow(int baseHeight);    // round(baseHeight * 2 / 3)
int VariantHigh(int baseHeight);   // round(baseHeight * 4 / 3)
int VariantStep(int baseHeight);   // 1, or 2 from kVariantCoarseFrom
bool IsVariantHeight(int baseHeight, int height);
int VariantCount(int baseHeight);

// The compiled height nearest to baseHeight * pxPerUnit; baseHeight (draw the
// retail font) when that is nearer, when the two are within half a pixel, or
// when the wanted height is more than one step outside the compiled range.
// Ties go to the smaller height (a glyph a little narrower than its advance
// rather than one that touches its neighbour).
int PickVariantHeight(int baseHeight, float pxPerUnit);

// ---- Retina (density 2) variants -------------------------------------------
// A 2x window works in device pixels, so the wanted height is base * ratio with
// the ratio up to twice the slider's (1.33 .. 2.67). Those heights lie above
// VariantHigh: RetinaHigh(H) = round(H * 8/3) is the top, every VariantStep-th
// height (a multiple of the step) above VariantHigh(H) is a Retina variant.
// Nothing here changes IsVariantHeight / PickVariantHeight: a 1x frame never
// reaches these heights, so its picks are identical to before.
int RetinaHigh(int baseHeight);
bool IsRetinaVariantHeight(int baseHeight, int height);
// PickVariantHeight when that finds a variant or the base is right; else, for
// a wanted height above the 1x range, the nearest Retina variant; baseHeight
// when the ratio is beyond RetinaHigh (retail's scaled quads).
int PickRetinaVariantHeight(int baseHeight, float pxPerUnit);

// True when drawing the base font through the word run (one texel per device
// pixel) is right at `pxPerUnit`: the wanted height is within half a pixel of
// the base. PickVariantHeight also returns baseHeight for a ratio OUTSIDE the
// compiled range (a Retina 2x frame); there the base glyphs must stay retail's
// scaled quads, or the text is drawn at its layout size in device pixels
// (half size, letter-spaced by the layout advances).
bool NativePickFitsRatio(int baseHeight, float pxPerUnit);

// The highest device-pixel ratio retail's slider reaches (75 %, 1 / 0.75) plus a hair, and the
// lowest (150 %, 1 / 1.5) less one.
constexpr float kRetailRatioMax = 4.0f / 3.0f + 0.01f;
constexpr float kRetailRatioMin = 2.0f / 3.0f - 0.01f;

// The font height SelectFont draws with, and whether to keep retail's scaled
// quads instead ("retail(range)"). At `density` <= 1 (every window that is not
// high-density: Windows, Linux, non-Retina) this is exactly PickVariantHeight,
// i.e. upstream's selection, and keeps retail only for a ratio outside the
// compiled range (reached only below the slider's retail 75 %, ext.phone_menu,
// or with a render scale under Full, which multiplies _uiScale).
// Only above 1 do the Retina variants apply.
int PickFontHeight(int baseHeight, float pxPerUnit, float density, bool* keepRetail);

// "Font/Sharp<baseName>_<height>.rsc" (baseName "Font", "FontSmall", ...).
// False when `cap` is too small.
bool VariantFontPath(const char* baseName, int height, char* out, size_t cap);

// One row of a UI::Font's glyph table (UI::Font::Kerning, field for field).
struct GlyphMetrics {
    int32_t preAdvance, postKern, postAdvance, offsetY;
};

// One base glyph's placement from a variant (ui/SharpDraw.h has the math):
// the base glyph's row (layout units), the variant glyph's row and sprite size
// (device pixels), and whether the glyph is the space (a word-run break).
UI::SharpGlyphPlace MakeGlyphPlace(const GlyphMetrics& base, const GlyphMetrics& variant,
                                   int32_t variantWidth, int32_t variantHeight, bool wordBreak);

// ---- 2x sprite sheets ------------------------------------------------------
// A sprite sheet `<dir>/<name>.rsc` (its factory path, optionally with
// ":resource") has a 2x twin when `<dir>/<name>2x.rsc` is compiled and
// deployed: the same sprite names, each drawn at twice the pixel size. False
// for a path that is not a plain .rsc resource or is itself a twin.
bool Twin2xPath(const char* path, char* out, size_t cap);

// The twin is drawn when the UI is magnified (more than one device pixel per
// layout unit on both axes): its texels are then closer to the screen's.
bool WantTwin2x(float pxPerUnitX, float pxPerUnitY);

// The factor of the magnified sheet copy (sharp_scaled.h) to draw `pxPerUnit`
// device pixels per layout unit from: 0 (retail's sheet) unless WantTwin2x;
// else the smallest of `factors` (ascending) at or above the larger axis's
// ratio, so the copy is never magnified while a bigger one exists; the largest
// above them all. `pxPerUnit` is already in back-buffer pixels: the slider, the
// window density and the render scale are all in it, and nothing else counts
// (the Windows exe is DPI-unaware, as retail: display scaling never reaches the
// back buffer). Ties go to the smaller factor (2.0 exactly is the 2x sheet,
// texel for pixel).
int PickSheetFactor(float pxPerUnitX, float pxPerUnitY, const int* factors, int count);

// The largest side of a magnified sheet copy: D3D11's 2D texture limit
// (D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION). At this size one BGRA8 level is
// 1 GiB, so every level's byte size fits the int the engine's level array takes.
constexpr int64_t kMaxScaledSheetSide = 16384;

// True when a `baseW x baseH` sheet can be copied at `factor`: both sides
// positive and each, times `factor`, at most kMaxScaledSheetSide. A sheet's size
// comes from its file unbounded (review F-228); past this the copy is not built
// and retail's sheet is drawn.
bool ScaledSheetFits(int32_t baseW, int32_t baseH, int factor);

}  // namespace SharpUI
}  // namespace Ext
