// ============================================================================
// src/engine/ui/SharpDraw.h -- the engine end of ext.sharp_ui (NOT RETAIL)
// ============================================================================
// docs/extensions/DESIGN.md §3 rule 5: an engine-side seam is a null-by-default
// static data pointer the game side sets. `UI::SpriteContext::_extSharp`
// (SpriteContext.h) points at one of these for the duration of one
// UI::UserInterface::Draw, and only while ext.sharp_ui is on (the game side's
// Ext::SharpUI::Draw sets it and nulls it again). SpriteContext::Flush reads the
// pointer ONCE per flush; null (always, in the faithful profile) = retail's
// vertices and retail's samplers, byte for byte. Non-null:
//   * each axis-aligned quad's corners move to the nearest whole device pixel;
//   * when `point` is set (an integer ratio >= 2), the flush picks the sampler
//     for its batch: POINT (Video::ImageBufferData::_extPointSampling) when
//     every axis-aligned quad in it is drawn pixel-exact (BatchQuadIsPixelExact),
//     else the image's own (linear) sampler. A picture stretched to the screen
//     (a dialog backdrop) stays filtered; glyphs, icons and 9-slice frames
//     drawn at their size times the ratio are point-sampled. (Retail's 75-150%
//     slider never gives an integer ratio >= 2, so this stays off in practice.)
//   * when `swaps` names the batch's sheet (FindSwap), the batch is drawn from
//     that sheet's TWIN: the twin's material is set up instead of the sheet's
//     and every record's UVs come from the twin's sprite `twinSprite[index]`.
//     A twin with `place` (a font variant: the same glyphs rasterised at the
//     device pixel height) also moves each glyph quad to the variant glyph's
//     own size in device pixels, anchored at the base glyph's pen and baseline
//     (PlaceGlyph). A twin without `place` (a 2x sprite sheet) keeps retail's
//     quad. The records -- and so ComputeStringLayout / ComputeLineLayout,
//     hit-testing and SpriteCapture, which all run before Flush -- never see a
//     twin: layout is the base font's, unit for unit.
//
// The vertex positions Flush emits are in LAYOUT units; UserInterface::Draw's
// orthographic camera maps layout [0, W) x [0, H) onto the back buffer, so a
// layout coordinate v sits at device pixel v * pxPerUnit.
//
// Standard C++ only: tests/unit/ext_sharp_ui_test.cpp includes it.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace Graphics { class MaterialInstance; }

namespace UI {

class SpriteSheet;

// One base glyph drawn from a font variant. Base metrics in LAYOUT units (the
// retail Font's), variant metrics in DEVICE pixels (the variant is rasterised
// at the device pixel height, so its texels are device pixels):
//   pen      = x1 + originX     (x1 = pen + the base _preAdvance: ComputeLineLayout)
//   baseline = y2 + originY     (y2 = baseline - the base _offsetY)
//   left     = snap(pen) + devX                 devX = the variant _preAdvance
//   bottom   = snap(baseline) + devY            devY = -(the variant _offsetY)
//              snap = SnapDevice against the dialog's upper-left
//   the quad = [left, left + devW) x [bottom - devH, bottom), devW x devH the
//              variant sprite: one texel per device pixel.
struct SharpGlyphPlace {
    int32_t originX, originY;   // layout units
    int32_t devX, devY;         // device pixels
    int32_t devW, devH;         // device pixels
    // The pen step to the next glyph (_preAdvance + _postAdvance + _postKern:
    // ComputeLineLayout's `penX += _preAdvance + kern`, then `+= _postAdvance`),
    // the base glyph's in layout units, the variant's in device pixels; and
    // whether this is the space glyph (a word run ends on either side of it).
    int32_t baseAdvance;
    int32_t devAdvance;
    bool    wordBreak;
};

// WORD RUNS (docs/extensions/handins/SHARP-SPACING.md). Snapping every glyph's
// pen on its own, round(pen * px), puts the gap between two letters up to a
// pixel either way off the variant's own hinted gap, and differently at every
// slider step (the rounding pattern of pen * px changes with px): letter
// spacing that looks uneven and reshuffles as the slider moves. A run draws a
// word the way the variant was hinted: the first glyph's pen is snapped, each
// following glyph's device pen is the previous one plus the previous glyph's
// variant advance, as long as the base layout is contiguous (the next pen is
// exactly the last pen plus its base advance, on the same baseline, no space
// between). The run is held within kSharpRunTolerance device pixels of the
// base pen's ideal position (retail's fractional pen): past that it is nudged
// by whole pixels, so a word is never more than a pixel wider or narrower than
// retail's, and the next word, which starts from its own snapped pen, lands
// where retail puts it. A space, a baseline change or any pen the layout did
// not advance to (a new string, the label's shadow pass) starts a new run.
struct SharpRun {
    bool    live = false;
    bool    wordBreak = false;   // the last glyph was a space
    int32_t pen = 0;             // the last glyph's base pen (layout units)
    int32_t baseline = 0;        // its baseline (layout units)
    int32_t advance = 0;         // its base advance
    int32_t devAdvance = 0;      // its variant advance
    float   devPen = 0.0f;       // its device pen (a whole number)
};
constexpr float kSharpRunTolerance = 1.0f;

// A sheet drawn from its twin (see the banner). `twinSprite` and `place` (when
// set) have `count` entries, indexed by the BASE sprite index; a twin index < 0
// draws nothing (zero UVs, as retail's negative index does).
// A SCALED sheet (ext/sharp_ui/sharp_sheet.h: the base atlas rebuilt at a whole
// multiple, every rectangle the base one times the factor) has no twin sheet:
// `material` is set, `twin` and `twinSprite` are null, and the batch keeps the
// BASE sheet's UVs, drawing from `material` instead of the sheet's own.
struct SharpSheetSwap {
    const SpriteSheet*     base;
    const SpriteSheet*     twin;
    const int32_t*         twinSprite;
    const SharpGlyphPlace* place;   // null: retail's quad (a 2x sprite twin)
    int32_t                count;
    Graphics::MaterialInstance* material = nullptr;   // non-null: a scaled sheet (base UVs)
};

struct SharpDraw {
    float pxPerUnitX;   // device pixels per layout unit, x (back buffer / layout width)
    float pxPerUnitY;   // ... y
    float unitsPerPxX;  // 1 / pxPerUnitX
    float unitsPerPxY;  // 1 / pxPerUnitY
    bool  point;        // the frame is at an integer ratio >= 2: batches may point-sample
    const SharpSheetSwap* swaps;   // the twins in force this frame (may be null)
    int32_t               swapCount;
    // The snap origin (SnapDevice) of the quads flushed next, in layout units:
    // UserInterface::Draw sets it to each dialog's upper-left before the
    // dialog's quads are added and back to 0 after the dialog loop (each dialog
    // ends in a Flush). Mutable: the engine end holds the seam as const.
    mutable int32_t originX = 0;
    mutable int32_t originY = 0;
};

// The swap for `sheet`, or null. Called once per Flush, never per record.
inline const SharpSheetSwap* FindSwap(const SharpDraw& s, const SpriteSheet* sheet) {
    for (int32_t i = 0; i < s.swapCount; ++i)
        if (s.swaps[i].base == sheet) return &s.swaps[i];
    return nullptr;
}

// Layout coordinate v on a whole device pixel, RELATIVE TO `origin`:
// round((v - origin) * px) + round(origin * px). At a non-integer ratio
// rounding each coordinate on its own (origin 0) is not translation-invariant:
// a dialog moved by one layout unit (a panel following its entity while the
// camera turns, a drag) re-rounds every glyph differently and the gaps between
// them change by a pixel from frame to frame -- the text shimmers. Rounded
// against the dialog's own upper-left, every quad of the dialog moves by the
// same whole number of pixels: the dialog moves as one picture.
// The fractional device coordinate of v in the origin's rounded frame: where
// retail's filtered draw puts v, shifted so that the origin itself is whole.
inline float IdealDevice(int32_t v, int32_t origin, float pxPerUnit) {
    return static_cast<float>(v - origin) * pxPerUnit +
           std::floor(static_cast<float>(origin) * pxPerUnit + 0.5f);
}
inline float SnapDevice(int32_t v, int32_t origin, float pxPerUnit) {
    return std::floor(IdealDevice(v, origin, pxPerUnit) + 0.5f);
}

// The device pen of one placed glyph: continues `run` or starts one (the
// word-run banner above SharpRun), then makes this glyph the run's last.
// `x1` / `y2` are the record's corners, `originX` the snap origin.
inline float SharpRunPen(SharpRun& run, const SharpGlyphPlace& g, int32_t x1, int32_t y2,
                         int32_t originX, float pxPerUnitX) {
    const int32_t pen      = x1 + g.originX;
    const int32_t baseline = y2 + g.originY;
    float devPen;
    if (run.live && !run.wordBreak && !g.wordBreak &&
        baseline == run.baseline && pen == run.pen + run.advance) {
        devPen = run.devPen + static_cast<float>(run.devAdvance);
        const float err = devPen - IdealDevice(pen, originX, pxPerUnitX);
        if (err > kSharpRunTolerance)       devPen -= std::ceil(err - kSharpRunTolerance);
        else if (err < -kSharpRunTolerance) devPen += std::ceil(-err - kSharpRunTolerance);
    } else {
        devPen = SnapDevice(pen, originX, pxPerUnitX);
    }
    run.live       = true;
    run.wordBreak  = g.wordBreak;
    run.pen        = pen;
    run.baseline   = baseline;
    run.advance    = g.baseAdvance;
    run.devAdvance = g.devAdvance;
    run.devPen     = devPen;
    return devPen;
}

// A glyph quad in layout units (the corners Flush emits before the half-pixel).
struct SharpQuad { float x1, y1, x2, y2; };
// The glyph quad from a device pen already chosen (SharpRunPen).
inline SharpQuad PlaceGlyphAt(const SharpGlyphPlace& g, float devPen, int32_t y2,
                              float pxPerUnitY, float unitsPerPxX, float unitsPerPxY,
                              int32_t snapOriginY = 0) {
    const float left   = devPen + static_cast<float>(g.devX);
    const float bottom = SnapDevice(y2 + g.originY, snapOriginY, pxPerUnitY) +
                         static_cast<float>(g.devY);
    SharpQuad q;
    q.x1 = left * unitsPerPxX;
    q.y1 = (bottom - static_cast<float>(g.devH)) * unitsPerPxY;
    q.x2 = (left + static_cast<float>(g.devW)) * unitsPerPxX;
    q.y2 = bottom * unitsPerPxY;
    return q;
}
// A glyph on its own (the first of a run): its pen snapped.
inline SharpQuad PlaceGlyph(const SharpGlyphPlace& g, int32_t x1, int32_t y2,
                            float pxPerUnitX, float pxPerUnitY,
                            float unitsPerPxX, float unitsPerPxY,
                            int32_t snapOriginX = 0, int32_t snapOriginY = 0) {
    return PlaceGlyphAt(g, SnapDevice(x1 + g.originX, snapOriginX, pxPerUnitX), y2,
                        pxPerUnitY, unitsPerPxX, unitsPerPxY, snapOriginY);
}

// v (layout units) moved to the nearest whole device pixel, back in layout
// units. Halves round up (floor(x + 0.5)), the same way for every quad, so two
// quads of one dialog that share an edge in layout space share it in device
// space. `origin` = SnapDevice's (the dialog's upper-left; 0 = absolute).
inline float SnapToPixel(float v, float pxPerUnit, float unitsPerPx, int32_t origin = 0) {
    return (std::floor((v - static_cast<float>(origin)) * pxPerUnit + 0.5f) +
            std::floor(static_cast<float>(origin) * pxPerUnit + 0.5f)) * unitsPerPx;
}

// A stretch at least this large is a fill (a 9-slice edge or centre, a bar):
// its texels are uniform along the stretch, where POINT and LINEAR agree, and
// POINT keeps the other axis crisp.
constexpr float kSharpStretchFill = 4.0f;
constexpr float kSharpExactTolerance = 0.01f;

// One axis of one quad: `layoutExtent` layout units drawn from `spriteExtent`
// texels. Pixel-exact when each texel covers a whole number (>= 1) of device
// pixels, or when it is a fill (above). A zero / negative sprite extent has no
// texels to misplace.
inline bool AxisIsPixelExact(int32_t layoutExtent, float pxPerUnit, int32_t spriteExtent) {
    if (spriteExtent <= 0) return true;
    float m = static_cast<float>(layoutExtent) * pxPerUnit / static_cast<float>(spriteExtent);
    if (m < 0.0f) m = -m;
    if (m >= kSharpStretchFill) return true;
    const float r = std::floor(m + 0.5f);
    return r >= 1.0f && std::fabs(m - r) <= kSharpExactTolerance * r;
}

inline bool BatchQuadIsPixelExact(const SharpDraw& s, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                  int32_t spriteWidth, int32_t spriteHeight) {
    return AxisIsPixelExact(x2 - x1, s.pxPerUnitX, spriteWidth) &&
           AxisIsPixelExact(y2 - y1, s.pxPerUnitY, spriteHeight);
}

}  // namespace UI
