// ============================================================================
// src/ext/sharp_ui/sharp_math.cpp -- ext.sharp_ui's pure decisions
// ============================================================================
// NOT RETAIL. sharp_math.h has the contract.
// ============================================================================
#include "ext/sharp_ui/sharp_math.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace SharpUI {

int32_t LayoutExtent(int32_t backBufferExtent, float uiScale) {
    return static_cast<int32_t>(static_cast<float>(backBufferExtent) * uiScale);
}

DrawChoice ChooseDraw(float uiScale, int32_t backBufferWidth, int32_t backBufferHeight) {
    DrawChoice c;
    c.layoutWidth  = LayoutExtent(backBufferWidth, uiScale);
    c.layoutHeight = LayoutExtent(backBufferHeight, uiScale);
    c.ratio        = uiScale > 0.0f ? 1.0f / uiScale : 0.0f;
    c.pxPerUnitX   = 0.0f;
    c.pxPerUnitY   = 0.0f;
    c.integerRatio = 0;
    c.integer      = false;
    c.snap         = false;
    c.point        = false;
    if (c.layoutWidth <= 0 || c.layoutHeight <= 0 || backBufferWidth <= 0 || backBufferHeight <= 0)
        return c;   // degenerate: retail's own frame, nothing to snap to

    c.pxPerUnitX = static_cast<float>(backBufferWidth) / static_cast<float>(c.layoutWidth);
    c.pxPerUnitY = static_cast<float>(backBufferHeight) / static_cast<float>(c.layoutHeight);
    c.snap = true;

    const float rx = std::floor(c.pxPerUnitX + 0.5f);
    const float ry = std::floor(c.pxPerUnitY + 0.5f);
    if (rx >= 1.0f && rx == ry &&
        std::fabs(c.pxPerUnitX - rx) <= kIntegerTolerance &&
        std::fabs(c.pxPerUnitY - ry) <= kIntegerTolerance) {
        c.integer      = true;
        c.integerRatio = static_cast<int>(rx);
        c.point        = c.integerRatio >= 2;
    }
    return c;
}

// round(baseHeight * num / 3) for a positive height, in integers. Callers keep
// baseHeight within kMaxFontBaseHeight (NOT RETAIL: review F-229, a _lineHeight
// from the font file would overflow 2 * num * baseHeight).
static int RoundThirds(int baseHeight, int num) {
    return (2 * num * baseHeight + 3) / 6;
}

int VariantLow(int baseHeight)  { return RoundThirds(baseHeight, 2); }
int VariantHigh(int baseHeight) { return RoundThirds(baseHeight, 4); }
int VariantStep(int baseHeight) { return baseHeight >= kVariantCoarseFrom ? 2 : 1; }

// True for a base height that has variants: positive and within kMaxFontBaseHeight.
static bool HasVariants(int baseHeight) {
    return baseHeight > 0 && baseHeight <= kMaxFontBaseHeight;
}

bool IsVariantHeight(int baseHeight, int height) {
    if (!HasVariants(baseHeight) || height == baseHeight) return false;
    const int lo = VariantLow(baseHeight);
    if (height < lo || height > VariantHigh(baseHeight)) return false;
    return (height - lo) % VariantStep(baseHeight) == 0;
}

int VariantCount(int baseHeight) {
    if (!HasVariants(baseHeight)) return 0;
    int n = 0;
    for (int h = VariantLow(baseHeight); h <= VariantHigh(baseHeight); ++h)
        if (IsVariantHeight(baseHeight, h)) ++n;
    return n;
}

int PickVariantHeight(int baseHeight, float pxPerUnit) {
    if (!HasVariants(baseHeight) || !(pxPerUnit > 0.0f)) return baseHeight;
    const float want = static_cast<float>(baseHeight) * pxPerUnit;
    if (std::fabs(want - static_cast<float>(baseHeight)) < 0.5f) return baseHeight;

    int   best = baseHeight;
    float bestErr = std::fabs(want - static_cast<float>(baseHeight));
    for (int h = VariantLow(baseHeight); h <= VariantHigh(baseHeight); ++h) {
        if (!IsVariantHeight(baseHeight, h)) continue;
        const float err = std::fabs(want - static_cast<float>(h));
        if (err < bestErr) {   // strict: a tie keeps the smaller (earlier) height
            best = h;
            bestErr = err;
        }
    }
    if (best != baseHeight && bestErr > static_cast<float>(VariantStep(baseHeight)))
        return baseHeight;   // outside the compiled range: retail's font
    return best;
}

int RetinaHigh(int baseHeight) { return RoundThirds(baseHeight, 8); }

bool IsRetinaVariantHeight(int baseHeight, int height) {
    if (!HasVariants(baseHeight)) return false;
    return height > VariantHigh(baseHeight) && height <= RetinaHigh(baseHeight) &&
           height % VariantStep(baseHeight) == 0;
}

int PickRetinaVariantHeight(int baseHeight, float pxPerUnit) {
    const int pick = PickVariantHeight(baseHeight, pxPerUnit);
    if (pick != baseHeight || !HasVariants(baseHeight) || !(pxPerUnit > 0.0f)) return pick;
    const int step = VariantStep(baseHeight);
    const float want = static_cast<float>(baseHeight) * pxPerUnit;
    if (want <= static_cast<float>(VariantHigh(baseHeight) + step) ||
        want > static_cast<float>(RetinaHigh(baseHeight) + step))
        return baseHeight;
    int h = static_cast<int>(std::floor(want / static_cast<float>(step) + 0.5f)) * step;
    if (h > RetinaHigh(baseHeight)) h = RetinaHigh(baseHeight) / step * step;
    if (h <= VariantHigh(baseHeight)) h = (VariantHigh(baseHeight) / step + 1) * step;
    return IsRetinaVariantHeight(baseHeight, h) ? h : baseHeight;
}

bool NativePickFitsRatio(int baseHeight, float pxPerUnit) {
    if (baseHeight <= 0 || !(pxPerUnit > 0.0f)) return false;
    return std::fabs(static_cast<float>(baseHeight) * pxPerUnit - static_cast<float>(baseHeight)) < 0.5f;
}

bool VariantFontPath(const char* baseName, int height, char* out, size_t cap) {
    const int n = std::snprintf(out, cap, "Font/Sharp%s_%d.rsc", baseName, height);
    return n > 0 && static_cast<size_t>(n) < cap;
}

UI::SharpGlyphPlace MakeGlyphPlace(const GlyphMetrics& base, const GlyphMetrics& variant,
                                   int32_t variantWidth, int32_t variantHeight, bool wordBreak) {
    UI::SharpGlyphPlace g;
    g.originX     = -base.preAdvance;    // x1 = pen + _preAdvance
    g.originY     = base.offsetY;        // y2 = baseline - _offsetY
    g.devX        = variant.preAdvance;
    g.devY        = -variant.offsetY;
    g.devW        = variantWidth;
    g.devH        = variantHeight;
    g.baseAdvance = base.preAdvance + base.postAdvance + base.postKern;
    g.devAdvance  = variant.preAdvance + variant.postAdvance + variant.postKern;
    g.wordBreak   = wordBreak;
    return g;
}

bool Twin2xPath(const char* path, char* out, size_t cap) {
    if (path == nullptr) return false;
    size_t len = std::strlen(path);
    static const char kResource[] = ":resource";
    const size_t kResourceLen = sizeof(kResource) - 1;
    if (len > kResourceLen && std::strcmp(path + len - kResourceLen, kResource) == 0)
        len -= kResourceLen;
    if (len < 5) return false;
    for (size_t i = 0; i < len; ++i)
        if (path[i] == ':') return false;   // a named object, not a sheet file
    const char* ext = path + len - 4;
    const bool isRsc = ext[0] == '.' && (ext[1] == 'r' || ext[1] == 'R') &&
                       (ext[2] == 's' || ext[2] == 'S') && (ext[3] == 'c' || ext[3] == 'C');
    if (!isRsc) return false;
    const size_t stem = len - 4;
    if (stem >= 2 && path[stem - 2] == '2' && (path[stem - 1] == 'x' || path[stem - 1] == 'X'))
        return false;   // already a twin
    const int n = std::snprintf(out, cap, "%.*s2x.rsc", static_cast<int>(stem), path);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool WantTwin2x(float pxPerUnitX, float pxPerUnitY) {
    return pxPerUnitX > 1.0f + kIntegerTolerance && pxPerUnitY > 1.0f + kIntegerTolerance;
}

int PickSheetFactor(float pxPerUnitX, float pxPerUnitY, const int* factors, int count) {
    if (count <= 0 || !WantTwin2x(pxPerUnitX, pxPerUnitY)) return 0;
    const float px = pxPerUnitX > pxPerUnitY ? pxPerUnitX : pxPerUnitY;
    for (int i = 0; i < count; ++i) {
        if (px <= static_cast<float>(factors[i]) + kIntegerTolerance) return factors[i];
    }
    return factors[count - 1];
}

bool ScaledSheetFits(int32_t baseW, int32_t baseH, int factor) {
    if (baseW <= 0 || baseH <= 0 || factor <= 0) return false;
    return static_cast<int64_t>(baseW) * factor <= kMaxScaledSheetSide &&
           static_cast<int64_t>(baseH) * factor <= kMaxScaledSheetSide;
}

int PickFontHeight(int baseHeight, float pxPerUnit, float density, bool* keepRetail) {
    *keepRetail = false;
    if (!(density > 1.0f)) {
        // Outside the slider's retail range at 1x, on either side: retail's scaled quads, not the
        // base glyphs at their own pixel size spaced by the layout's advances. Above it
        // (ext.phone_menu's slider below 75 %, a ratio up to 4) the text came out small and
        // letter-spaced; below it (a render scale under Full, where _uiScale is the UI percent over
        // the render percent: UI 42 at Quarter is ratio 0.595) the glyphs overlapped. Inside
        // retail's 75..150 % the selection is upstream's, unchanged.
        const int pick = PickVariantHeight(baseHeight, pxPerUnit);
        if ((pxPerUnit > kRetailRatioMax || pxPerUnit < kRetailRatioMin) && pick == baseHeight &&
            !NativePickFitsRatio(baseHeight, pxPerUnit))
            *keepRetail = true;
        return pick;
    }
    const int pick = PickRetinaVariantHeight(baseHeight, pxPerUnit);
    if (pick == baseHeight && !NativePickFitsRatio(baseHeight, pxPerUnit)) *keepRetail = true;
    return pick;
}

}  // namespace SharpUI
}  // namespace Ext
