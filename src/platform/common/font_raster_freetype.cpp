// ============================================================================
// src/platform/common/font_raster_freetype.cpp -- the FreeType glyph rasteriser
// (KITFREE FREETYPE)
// ============================================================================
// NOT RETAIL. font_raster_freetype.h has the contract and the measured rule.
// FreeType is the pinned submodule third_party/freetype (VER-2-13-3) built by
// third_party/freetype_build with the TrueType driver, sfnt, smooth and
// raster1 only. Nothing here touches the engine; the unit tests link it on
// its own.
// ============================================================================
#include "font_raster_freetype.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_MODULE_H
#include FT_DRIVER_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include FT_ADVANCES_H

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace Ext {
namespace Resources {
namespace FreeTypeRaster {

namespace {

std::mutex g_lock;               // one FT_Library for the process; FreeType objects are not thread-safe
FT_Library g_library = nullptr;

bool Library(std::string* why) {
    if (g_library != nullptr) return true;
    const FT_Error e = FT_Init_FreeType(&g_library);
    if (e != 0) {
        g_library = nullptr;
        if (why) { char m[64]; std::snprintf(m, sizeof m, "FT_Init_FreeType failed (%d)", static_cast<int>(e)); *why = m; }
        return false;
    }
    return true;
}

bool SameFaceName(const char* a, const char* b) {
    // GDI's _wcsicmp on the face name: case-insensitive, ASCII is enough for a family name.
    for (;; ++a, ++b) {
        unsigned ca = static_cast<unsigned char>(*a), cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

// Opens `path` under the lock as face 0; null (and `why`) when it does not open.
FT_Face OpenFace(const char* path, std::string* why) {
    if (!Library(why)) return nullptr;
    FT_Face face = nullptr;
    const FT_Error e = FT_New_Face(g_library, path, 0, &face);
    if (e != 0 || face == nullptr) {
        if (why) { char m[96]; std::snprintf(m, sizeof m, "FT_New_Face failed (%d) on ", static_cast<int>(e)); *why = std::string(m) + path; }
        return nullptr;
    }
    return face;
}

// GDI's cell metrics: usWinAscent / usWinDescent, hhea when the OS/2 table is absent.
void CellMetrics(FT_Face face, int32_t& ascentUnits, int32_t& descentUnits) {
    const TT_OS2* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && os2->usWinAscent + os2->usWinDescent > 0) {
        ascentUnits  = os2->usWinAscent;
        descentUnits = os2->usWinDescent;
    } else {
        ascentUnits  = face->ascender;
        descentUnits = -face->descender;
    }
}

int32_t RoundScaled(int32_t units, double ppem, int32_t upem) {
    return static_cast<int32_t>(std::floor(units * ppem / upem + 0.5));
}

int32_t LoadFlags(const Options& o) {
    int32_t flags = o.hinting == Hinting::None ? FT_LOAD_NO_HINTING
                                               : (FT_LOAD_DEFAULT | FT_LOAD_NO_AUTOHINT | FT_LOAD_TARGET_NORMAL);
    if (o.sampling == Sampling::Grid8x8) flags |= FT_LOAD_NO_BITMAP;   // the outline is wanted, not a rendering
    return flags;
}

// GDI's blank glyph: a 1x1 empty box at the origin (font_raster.h, the space).
void BlankGlyph(RasterGlyph& g, int32_t advance) {
    g.width = 1; g.height = 1;
    g.preAdvance = 0; g.postAdvance = 1; g.postKern = advance - 1; g.offsetY = -1;
    g.alpha.assign(1, 0);
}

inline FT_Pos FloorDiv(FT_Pos a, FT_Pos b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline FT_Pos CeilDiv(FT_Pos a, FT_Pos b)  { return -FloorDiv(-a, b); }

// The smooth renderer's exact area coverage, quantised to GDI's levels when asked.
bool RenderExact(FT_GlyphSlot slot, const Options& options, int32_t advance, RasterGlyph& g, std::string* why) {
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) { if (why) *why = "FT_Render_Glyph failed"; return false; }
    const FT_Bitmap& bm = slot->bitmap;
    if (bm.width == 0 || bm.rows == 0) { BlankGlyph(g, advance); return true; }
    if (bm.pixel_mode != FT_PIXEL_MODE_GRAY) { if (why) *why = "unexpected pixel mode"; return false; }
    g.width       = static_cast<int32_t>(bm.width);
    g.height      = static_cast<int32_t>(bm.rows);
    g.preAdvance  = slot->bitmap_left;
    g.postAdvance = g.width;
    g.postKern    = advance - g.preAdvance - g.postAdvance;
    g.offsetY     = slot->bitmap_top - g.height;
    g.alpha.assign(static_cast<size_t>(g.width) * static_cast<size_t>(g.height), 0);
    for (int32_t y = 0; y < g.height; ++y) {
        const uint8_t* row = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;
        uint8_t* dst = g.alpha.data() + static_cast<size_t>(y) * static_cast<size_t>(g.width);
        for (int32_t x = 0; x < g.width; ++x) {
            const unsigned c = row[x];
            if (!options.quantize) { dst[x] = static_cast<uint8_t>(c); continue; }
            const unsigned level = (c * 64u + 127u) / 255u;            // 0..64, GDI's GRAY8 levels
            dst[x] = static_cast<uint8_t>(level == 0 ? 0 : level * 4 - 1);   // the kit's mapping
        }
    }
    return true;
}

// GDI's GGO_GRAY8 the way the measurement says it is made: the device-ppem
// outline scaled by 8 and scan-converted monochrome (a subsample is set when
// its centre is inside; raster1's drop-out control off), the set subsamples
// counted per device pixel (0..64), the box the pixels a subsample hit.
// `scratch` is the 1-bit 8x bitmap and `counts` the per-pixel tally, reused
// per glyph.
bool RenderGrid8(FT_Library library, FT_GlyphSlot slot, int32_t advance, RasterGlyph& g,
                 std::vector<uint8_t>& scratch, std::vector<uint16_t>& counts, std::string* why) {
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) { if (why) *why = "glyph is not an outline"; return false; }
    FT_Outline* outline = &slot->outline;
    if (outline->n_points == 0 || outline->n_contours == 0) { BlankGlyph(g, advance); return true; }

    // The exact outline box, in device pixels (26.6 -> pixels, floor / ceil).
    FT_BBox box;
    FT_Outline_Get_BBox(outline, &box);
    if (box.xMin >= box.xMax || box.yMin >= box.yMax) { BlankGlyph(g, advance); return true; }
    const FT_Pos left = FloorDiv(box.xMin, 64), right = CeilDiv(box.xMax, 64);
    const FT_Pos bottom = FloorDiv(box.yMin, 64), top = CeilDiv(box.yMax, 64);
    const int32_t W = static_cast<int32_t>(right - left), H = static_cast<int32_t>(top - bottom);
    if (W <= 0 || H <= 0 || W > 4096 || H > 4096) { if (why) *why = "outline box out of range"; return false; }

    // x8: the device outline scaled by 8, the box's bottom-left the 8x bitmap's origin.
    const FT_Matrix eight = { 8L << 16, 0, 0, 8L << 16 };
    FT_Outline_Transform(outline, &eight);
    FT_Outline_Translate(outline, -left * 512, -bottom * 512);
    outline->flags |= FT_OUTLINE_IGNORE_DROPOUTS;

    FT_Bitmap bm;
    std::memset(&bm, 0, sizeof bm);
    bm.width      = static_cast<unsigned>(W) * 8u;
    bm.rows       = static_cast<unsigned>(H) * 8u;
    bm.pitch      = static_cast<int>((bm.width + 7u) / 8u);     // positive: row 0 is the top row
    bm.pixel_mode = FT_PIXEL_MODE_MONO;
    bm.num_grays  = 2;
    scratch.assign(static_cast<size_t>(bm.pitch) * bm.rows, 0);
    bm.buffer = scratch.data();
    const FT_Error e = FT_Outline_Get_Bitmap(library, outline, &bm);
    if (e != 0) { if (why) { char m[64]; std::snprintf(m, sizeof m, "FT_Outline_Get_Bitmap failed (%d)", static_cast<int>(e)); *why = m; } return false; }

    // The set subsamples per device pixel.
    counts.assign(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
    for (unsigned y8 = 0; y8 < bm.rows; ++y8) {
        const uint8_t* row = bm.buffer + static_cast<size_t>(y8) * static_cast<size_t>(bm.pitch);
        uint16_t* dst = counts.data() + static_cast<size_t>(y8 / 8u) * static_cast<size_t>(W);
        for (unsigned x8 = 0; x8 < bm.width; ++x8)
            if (row[x8 >> 3] & (0x80u >> (x8 & 7u))) ++dst[x8 / 8u];
    }

    // GDI's box: the pixels a subsample hit (a touched-but-unsampled edge row / column is not in it).
    int32_t x0 = 0, x1 = W, y0 = 0, y1 = H;   // [x0, x1) x [y0, y1), y down from the top
    auto colEmpty = [&](int32_t x) { for (int32_t y = 0; y < H; ++y) if (counts[static_cast<size_t>(y) * W + x]) return false; return true; };
    auto rowEmpty = [&](int32_t y) { for (int32_t x = 0; x < W; ++x) if (counts[static_cast<size_t>(y) * W + x]) return false; return true; };
    while (x0 < x1 && colEmpty(x0)) ++x0;
    while (x1 > x0 && colEmpty(x1 - 1)) --x1;
    while (y0 < y1 && rowEmpty(y0)) ++y0;
    while (y1 > y0 && rowEmpty(y1 - 1)) --y1;
    if (x0 >= x1 || y0 >= y1) { BlankGlyph(g, advance); return true; }

    g.width       = x1 - x0;
    g.height      = y1 - y0;
    g.preAdvance  = static_cast<int32_t>(left) + x0;
    g.postAdvance = g.width;
    g.postKern    = advance - g.preAdvance - g.postAdvance;
    g.offsetY     = static_cast<int32_t>(bottom) + (H - y1);   // rows below the kept box, above the baseline
    g.alpha.assign(static_cast<size_t>(g.width) * static_cast<size_t>(g.height), 0);
    for (int32_t y = 0; y < g.height; ++y)
        for (int32_t x = 0; x < g.width; ++x) {
            const unsigned level = counts[static_cast<size_t>(y0 + y) * W + static_cast<size_t>(x0 + x)];   // 0..64
            g.alpha[static_cast<size_t>(y) * g.width + x] = static_cast<uint8_t>(level == 0 ? 0 : level * 4 - 1);
        }
    return true;
}

}  // namespace

const Options& DefaultOptions() {
    // FREETYPE.md: the GDI comparison's closest set. Updated by hand from the test's table.
    static const Options kDefault = { Sizing::IntegerPpem, Hinting::Native, Sampling::Grid8x8, 40, true };
    return kDefault;
}

bool FamilyName(const char* path, std::string& family, std::string* why) {
    family.clear();
    if (path == nullptr || *path == 0) { if (why) *why = "no path"; return false; }
    std::lock_guard<std::mutex> lock(g_lock);
    FT_Face face = OpenFace(path, why);
    if (face == nullptr) return false;
    if (face->family_name != nullptr) family = face->family_name;
    FT_Done_Face(face);
    if (family.empty() && why) *why = "face has no family name";
    return !family.empty();
}

bool RasterizeFile(const char* path, const char* faceName, int32_t pixelHeight, const int32_t* codepoints,
                   int32_t count, const Options& options, RasterFont& out, std::string* why) {
    out.face.clear();
    out.glyphs.clear();
    out.height = 0;
    out.baseLine = 0;
    if (path == nullptr || *path == 0) { if (why) *why = "no path"; return false; }
    if (faceName == nullptr || *faceName == 0) { if (why) *why = "no face name"; return false; }
    if (pixelHeight < 4 || pixelHeight > 256) { if (why) *why = "height out of range"; return false; }
    if (count < 0 || (count > 0 && codepoints == nullptr)) { if (why) *why = "no codepoints"; return false; }

    std::lock_guard<std::mutex> lock(g_lock);
    if (!Library(why)) return false;

    // The bytecode interpreter version is a driver property: set before the face opens.
    FT_UInt version = options.interpreter == 35 ? TT_INTERPRETER_VERSION_35 : TT_INTERPRETER_VERSION_40;
    FT_Property_Set(g_library, "truetype", "interpreter-version", &version);

    FT_Face face = OpenFace(path, why);
    if (face == nullptr) return false;
    if (face->family_name == nullptr || !SameFaceName(face->family_name, faceName)) {
        if (why) *why = std::string("file is '") + (face->family_name ? face->family_name : "?") + "', not " + faceName;
        FT_Done_Face(face);
        return false;
    }
    if (!FT_IS_SCALABLE(face) || face->units_per_EM == 0) {
        if (why) *why = "face is not scalable";
        FT_Done_Face(face);
        return false;
    }

    // The cell height -> the EM scale (the header's formula).
    const int32_t upem = face->units_per_EM;
    int32_t ascentUnits = 0, descentUnits = 0;
    CellMetrics(face, ascentUnits, descentUnits);
    // Review F-167: a face with no OS/2 table and zero hhea metrics has no cell; the scale below
    // would divide by zero and convert inf / NaN to integers (undefined behaviour).
    if (ascentUnits + descentUnits <= 0) {
        if (why) *why = "face has no cell height";
        FT_Done_Face(face);
        return false;
    }
    const double s = static_cast<double>(pixelHeight) * upem / (ascentUnits + descentUnits);
    double ppem = std::floor(s + 0.5);
    switch (options.sizing) {
        case Sizing::FractionalPpem:
            // A hinted TrueType load works at the integer ppem (head flag 3), whatever the request.
            if (options.hinting == Hinting::None) ppem = s;
            break;
        case Sizing::FittedPpem: {
            // The integer ppem nearest s whose rounded cell equals px; round(s) when none does.
            const int32_t base = static_cast<int32_t>(std::floor(s + 0.5));
            int32_t best = base;
            double bestDist = 1e9;
            for (int32_t p = base - 2; p <= base + 2; ++p) {
                if (p < 1) continue;
                if (RoundScaled(ascentUnits, p, upem) + RoundScaled(descentUnits, p, upem) != pixelHeight) continue;
                const double d = std::fabs(p - s);
                if (d < bestDist) { bestDist = d; best = p; }
            }
            ppem = best;
            break;
        }
        case Sizing::IntegerPpem:
        default:
            break;
    }
    // The device size in 26.6. The 8x8 grid is sampled from THIS outline scaled
    // by 8 (RenderGrid8), not from one loaded at 8x the size: measured, GDI
    // oversamples the device-ppem outline (loading at 8x the size, which keeps
    // 3 more bits, moved the comparison further from GDI: 88 instead of 188
    // identical bitmaps over 768, FREETYPE.md).
    const FT_F26Dot6 deviceSize = static_cast<FT_F26Dot6>(std::floor(ppem * 64.0 + 0.5));
    FT_Error e = FT_Set_Char_Size(face, 0, deviceSize, 72, 72);
    if (e != 0) {
        if (why) { char m[64]; std::snprintf(m, sizeof m, "FT_Set_Char_Size failed (%d)", static_cast<int>(e)); *why = m; }
        FT_Done_Face(face);
        return false;
    }
    // The device advance the TrueType driver gives: the phantom point FT_MulFix'ed
    // by x_scale and rounded to a pixel (ttgload.c); taken from the font units so
    // it does not depend on the sampling path's load flags.
    const FT_Fixed deviceScale = FT_DivFix(deviceSize, upem);

    out.face     = faceName;
    out.height   = pixelHeight;                                // the cell height asked for == tmHeight
    out.baseLine = RoundScaled(descentUnits, ppem, upem);      // == tmDescent

    const int32_t flags = LoadFlags(options);
    out.glyphs.resize(static_cast<size_t>(count));
    std::vector<uint8_t> scratch;
    std::vector<uint16_t> counts;
    bool ok = true;
    for (int32_t i = 0; i < count && ok; ++i) {
        RasterGlyph& g = out.glyphs[static_cast<size_t>(i)];
        g.codepoint = codepoints[i];
        const FT_UInt index = FT_Get_Char_Index(face, static_cast<FT_ULong>(g.codepoint));   // 0 = .notdef, as GDI draws it
        e = FT_Load_Glyph(face, index, flags);
        if (e != 0) {
            if (why) { char m[96]; std::snprintf(m, sizeof m, "FT_Load_Glyph failed (%d) on U+%04X", static_cast<int>(e), g.codepoint); *why = m; }
            ok = false;
            break;
        }
        FT_Fixed units = 0;
        FT_Get_Advance(face, index, FT_LOAD_NO_SCALE, &units);           // font units
        const int32_t advance = static_cast<int32_t>(((FT_MulFix(units, deviceScale) + 32) & ~63L) >> 6);
        ok = options.sampling == Sampling::Grid8x8
                 ? RenderGrid8(g_library, face->glyph, advance, g, scratch, counts, why)
                 : RenderExact(face->glyph, options, advance, g, why);
    }
    FT_Done_Face(face);
    if (!ok) out.glyphs.clear();
    return ok;
}

}  // namespace FreeTypeRaster
}  // namespace Resources
}  // namespace Ext
