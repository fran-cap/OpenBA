// ============================================================================
// tests/unit/ext_font_raster_test.cpp -- the run-time font rasteriser (KITFREE RSCFONT)
// ============================================================================
// src/ext/resources/font_tables.{h,cpp} (pure: the variant path grammar, the
// names, the measured widths) always; src/platform/win/font_raster_win.cpp
// against the mod kit's compiled variants when they exist
// (build/ext_resources/WinData/Font/, tools/ext_resources.sh; found from a
// worktree at the main checkout) and the face is available -- otherwise the
// comparison SKIPS, it never fails for a missing oracle. The posix twin
// is FreeType, so off Windows that comparison needs the kit's .crs (Windows
// output) and skips.
//
// Block FREETYPE (docs/extensions/handins/FREETYPE.md): the FreeType
// rasteriser (src/platform/common/font_raster_freetype.cpp, the posix twin's
// engine) on every platform where FrancophilSans.ttf is found by name, and on
// Windows the same heights through GDI and FreeType side by side -- per option
// set: bitmaps identical, differing pixels, the largest alpha step, metric rows
// equal. The table is printed; the numbers are the hand-in's, the choice to
// switch Windows over is the coordinator's, so only the structure is CHECKed.
// ============================================================================
#include "unit.h"
#include "ext/resources/font_raster.h"
#include "ext/resources/font_tables.h"
#include "font_raster_freetype.h"
#include "kit_font_reader.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace Ext::Resources;

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

// The kit's compiled fonts: the repo root's build/, else the main checkout's
// when this is a worktree under <root>/.claude/worktrees/<name>/.
std::string KitFontDir() {
    const char* candidates[] = { "build/ext_resources/WinData/Font", "../../../build/ext_resources/WinData/Font" };
    for (const char* c : candidates)
        if (Exists(std::string(c) + "/Font_resource.crs")) return c;
    return std::string();
}

}  // namespace

UNIT_TEST(ext_font_variant_path_grammar) {
    VariantPath vp;
    CHECK(ParseVariantPath("Font/SharpFont_32.rsc:resource", vp));
    CHECK(vp.kit == 0 && vp.px == 32 && vp.part == "resource");
    CHECK(ParseVariantPath("Font/SharpFont_32.rsc", vp) && vp.part == "resource");
    CHECK(ParseVariantPath("Font/SharpFontSmall_19.rsc:material", vp));
    CHECK(vp.kit == 1 && vp.px == 19 && vp.part == "material");
    CHECK(ParseVariantPath("Font\\SharpFontLarge_64.rsc:texture", vp));
    CHECK(vp.kit == 3 && vp.px == 64 && vp.part == "texture");
    CHECK(ParseVariantPath("Build/SharpFontMedium_12Sheet.rsc:resource", vp));
    CHECK(vp.kit == 2 && vp.px == 12 && vp.part == "sheet");
    CHECK(!ParseVariantPath("Font/Font.rsc:resource", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_32.rsc:glyphs", vp));
    CHECK(!ParseVariantPath("Font/SharpFontX_32.rsc", vp));
    CHECK(!ParseVariantPath("Build/SharpFont_32.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_.rsc", vp));
    CHECK(!ParseVariantPath(nullptr, vp));
    // Review F-239: only MakeVariantNames' own spelling of the size. strtol took a sign, blanks
    // and leading zeros (and, with a 64-bit long, wrapped 4294967328 to 32), so an alias path
    // parsed to the canonical variant and the provide seam re-pathed and re-added that object.
    CHECK(!ParseVariantPath("Font/SharpFont_032.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_+32.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_ 32.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_-32.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_4294967328.rsc", vp));
    CHECK(!ParseVariantPath("Font/SharpFont_1000.rsc", vp));
    CHECK(!ParseVariantPath("Build/SharpFont_012Sheet.rsc", vp));
    CHECK(ParseVariantPath("Font/SharpFont_999.rsc", vp) && vp.px == 999);
    CHECK(ParseVariantPath("Font/SharpFont_1.rsc", vp) && vp.px == 1);

    const VariantNames n = MakeVariantNames(0, 32);
    CHECK_STREQ(n.file.c_str(), "SharpFont_32");
    CHECK_STREQ(n.fontPath.c_str(), "Font/SharpFont_32.rsc:resource");
    CHECK_STREQ(n.materialPath.c_str(), "Font/SharpFont_32.rsc:material");
    CHECK_STREQ(n.texturePath.c_str(), "Font/SharpFont_32.rsc:texture");
    CHECK_STREQ(n.sheetPath.c_str(), "Build/SharpFont_32Sheet.rsc:resource");
    CHECK_STREQ(n.sheetName.c_str(), "Build/SharpFont_32Sheet.rsc");
    CHECK_STREQ(n.imageName.c_str(), "Build/uiSharpFont_32Image.png");
    CHECK_STREQ(n.baseFontPath.c_str(), "Font/Font.rsc");
    CHECK_STREQ(n.baseMaterialPath.c_str(), "Font/FontMaterial.rsc");
    CHECK_STREQ(MakeVariantNames(1, 19).baseMaterialPath.c_str(), "Font/FontSmallMaterial.rsc");

    CHECK(CodepointFromSpriteName("c65") == 65);
    CHECK(CodepointFromSpriteName("c169") == 169);
    CHECK(CodepointFromSpriteName("c") == -1);
    CHECK(CodepointFromSpriteName("x65") == -1);
    CHECK(CodepointFromSpriteName("c65x") == -1);
    CHECK(CodepointFromSpriteName(nullptr) == -1);

    // The kit's measured widths at 32 px: '.' is (0, 1, 4) -> "..." = 15, ' ' is (0, 5, 1) -> 6.
    const GlyphRow dot{0, 1, 4, 0};
    const GlyphRow dots[3] = { dot, dot, dot };
    CHECK(MeasureRun(dots, 3) == 15);
    const GlyphRow space{0, 5, 1, -1};
    CHECK(MeasureRun(&space, 1) == 6);

    int32_t w = 0, h = 0;
    CHECK(KitSheetSize(32, w, h) && w == 256 && h == 256);    // 64 * 32^2 = 65536
    CHECK(KitSheetSize(9, w, h) && w == 128 && h == 128);
    CHECK(KitSheetSize(64, w, h) && w == 512 && h == 512);
    CHECK(KitSheetSize(96, w, h) && w == 1024 && h == 1024);   // FontLarge at 2x
    CHECK(KitSheetSize(65, w, h) && w == 1024 && h == 512);
    CHECK(!KitSheetSize(129, w, h));
}

// One compiled font against the rasteriser: same codepoints, glyph count,
// line height, baseline, ellipsis and space widths, every glyph row
// (preAdvance, postKern, postAdvance, offsetY). The kit's rule (Ghidra,
// UI::Font::Compile in Runtime-x64-profile.dll, RSCFONT.md "The kit's rule"):
// the row is the GetCharABCWidthsW triple (A, C, B) plus
// gmptGlyphOrigin.y - gmBlackBoxY, so a variant the kit compiled on this box
// matches EXACTLY (`exact`; otherwise each field within one pixel). `px` 0 =
// the compiled font's own height (the base fonts).
static void CompareVariant(const std::string& dir, const std::string& kitName, int32_t px, bool exact,
                           int32_t& rows, int32_t& rowsOff) {
    const std::string crs = dir + "/" + kitName + "_resource.crs";
    if (!Exists(crs)) return;
    ::KitFont::Font oracle;
    CHECK(::KitFont::Load(crs.c_str(), kGameFace, oracle));
    if (oracle.glyphs.empty()) return;
    const std::vector<int32_t> cps = ::KitFont::GlyphCodepoints(oracle);
    if (px <= 0) px = oracle.height;
    RasterFont ours;
    std::string why;
    CHECK(RasterizeFont(kGameFace, px, cps.data(), static_cast<int32_t>(cps.size()), ours, &why));
    if (ours.glyphs.size() != cps.size()) { std::printf("  %s: rasterised %zu of %zu\n", kitName.c_str(), ours.glyphs.size(), cps.size()); return; }
    CHECK_STREQ(ours.face.c_str(), kGameFace);
    CHECK_EQ(ours.height, oracle.height);
    CHECK_EQ(ours.height, px);
    CHECK_EQ(ours.baseLine, oracle.baseLine);
    CHECK_EQ(static_cast<int32_t>(ours.glyphs.size()), static_cast<int32_t>(oracle.glyphs.size()));
    GlyphRow dot{0, 0, 0, 0}, sp{0, 0, 0, 0};
    for (size_t i = 0; i < cps.size(); ++i) {
        const ::KitFont::GlyphRow& k = oracle.glyphs[i];
        const RasterGlyph& g = ours.glyphs[i];
        CHECK_EQ(g.codepoint, cps[i]);
        CHECK_EQ(static_cast<int32_t>(g.alpha.size()), g.width * g.height);
        const bool same = g.preAdvance == k.preAdvance && g.postKern == k.postKern &&
                          g.postAdvance == k.postAdvance && g.offsetY == k.offsetY;
        ++rows;
        if (!same) {
            ++rowsOff;
            std::printf("  %s U+%04X: kit (%d %d %d %d) ours (%d %d %d %d)\n", kitName.c_str(), cps[i],
                        k.preAdvance, k.postKern, k.postAdvance, k.offsetY,
                        g.preAdvance, g.postKern, g.postAdvance, g.offsetY);
        }
        if (exact) {
            CHECK(same);
        } else {
            CHECK(std::abs(g.preAdvance - k.preAdvance) <= 1);
            CHECK(std::abs(g.postAdvance - k.postAdvance) <= 1);
            CHECK(std::abs(g.postKern - k.postKern) <= 1);
            CHECK(std::abs(g.offsetY - k.offsetY) <= 1);
        }
        if (cps[i] == '.') dot = GlyphRow{g.preAdvance, g.postKern, g.postAdvance, g.offsetY};
        if (cps[i] == ' ') sp = GlyphRow{g.preAdvance, g.postKern, g.postAdvance, g.offsetY};
    }
    const GlyphRow dots[3] = { dot, dot, dot };
    CHECK_EQ(MeasureRun(dots, 3), oracle.ellipsisWidth);
    CHECK_EQ(MeasureRun(&sp, 1), oracle.spaceWidth);
}

UNIT_TEST(ext_font_raster_matches_kit_variant) {
    const std::string dir = KitFontDir();
    if (dir.empty()) UNIT_SKIP("no kit-compiled fonts under build/ext_resources/WinData/Font (tools/ext_resources.sh)");
    std::string why;
    if (!FaceAvailable(kGameFace, &why)) UNIT_SKIP("typeface FrancophilSans not available");
    // Every variant tools/ext_fonts.py has the kit compile: Sharp<Kit>_<px>.
    static const char* const kKits[] = { "SharpFont", "SharpFontSmall", "SharpFontMedium", "SharpFontLarge" };
    int32_t rows = 0, rowsOff = 0, variants = 0;
    for (const char* kit : kKits) {
        for (int32_t px = 4; px <= 128; ++px) {
            const std::string name = std::string(kit) + "_" + std::to_string(px);
            if (!Exists(dir + "/" + name + "_resource.crs")) continue;
            ++variants;
            CompareVariant(dir, name, px, true, rows, rowsOff);
        }
    }
    std::printf("  font raster vs kit: %d variants, %d rows, %d differ\n", variants, rows, rowsOff);
    CHECK(variants > 0);
    CHECK_EQ(rowsOff, 0);
    // The game's base fonts as they sit in the kit's output (Font is recompiled
    // by tools/ext_resources.sh on every run; FontSmall / FontMedium as found).
    int32_t baseRows = 0, baseOff = 0;
    for (const char* base : { "Font", "FontSmall", "FontMedium", "FontLarge" })
        CompareVariant(dir, base, 0, true, baseRows, baseOff);
    std::printf("  font raster vs the base fonts: %d rows, %d differ\n", baseRows, baseOff);
    CHECK_EQ(baseOff, 0);
}

UNIT_TEST(ext_font_raster_rejects_absent_face) {
    RasterFont out;
    std::string why;
    // A face no machine has: never substituted (GDI would hand back another
    // face; the name check refuses it).
    const int32_t cps[] = { 'A' };
    CHECK(!RasterizeFont("NoSuchFace_OpenBA_Test", 24, cps, 1, out, &why));
    CHECK(out.glyphs.empty());
    CHECK(!why.empty());
    CHECK(!RasterizeFont(kGameFace, 1, cps, 1, out, &why));   // height out of range
}

// Review S-050 / S-051: the face file beside the exe is looked up per face. A missing face must
// not decide the answer for the next one (the Windows rasteriser latched its first private-load
// attempt for every face, so a missing "Segoe UI" hid FrancophilSans.ttf beside the exe).
UNIT_TEST(ext_font_raster_private_face_lookup_is_per_face) {
    std::string whyA;
    std::string whyB;
    CHECK(!FaceAvailable("NoSuchFace_S051_A", &whyA));
    CHECK(!FaceAvailable("NoSuchFace_S051_B", &whyB));
    // Each answer names its own face's file: the second face was looked up, not answered from
    // the first face's attempt.
    CHECK(whyA.find("NoSuchFace_S051_A.ttf") != std::string::npos);
    CHECK(whyB.find("NoSuchFace_S051_B.ttf") != std::string::npos);
}

// ============================================================================
// FREETYPE: the FreeType rasteriser, and its comparison with GDI on Windows
// ============================================================================
namespace {

using namespace Ext::Resources::FreeTypeRaster;

// The game's character set (Font/CharacterSet.rsc): ASCII 32..126 and U+00A9.
std::vector<int32_t> GameCharset() {
    std::vector<int32_t> cps;
    for (int32_t c = 32; c <= 126; ++c) cps.push_back(c);
    cps.push_back(0xA9);
    return cps;
}

// FrancophilSans.ttf by name (beside the exe, the font directories / registry); empty when absent.
std::string FaceFile(std::string* why) {
    std::string path;
    if (!FindFaceFile(kGameFace, path, why)) return std::string();
    return path;
}

struct Variant { const char* name; Options o; };

// { sizing, hinting, sampling, interpreter, quantize }
const Variant kVariants[] = {
    { "grid8 int-ppem native v40", { Sizing::IntegerPpem,    Hinting::Native, Sampling::Grid8x8,   40, true } },
    { "grid8 int-ppem native v35", { Sizing::IntegerPpem,    Hinting::Native, Sampling::Grid8x8,   35, true } },
    { "grid8 int-ppem unhinted",   { Sizing::IntegerPpem,    Hinting::None,   Sampling::Grid8x8,   40, true } },
    { "grid8 frac-ppem unhinted",  { Sizing::FractionalPpem, Hinting::None,   Sampling::Grid8x8,   40, true } },
    { "grid8 fit-ppem native v40", { Sizing::FittedPpem,     Hinting::Native, Sampling::Grid8x8,   40, true } },
    { "area  int-ppem native v40", { Sizing::IntegerPpem,    Hinting::Native, Sampling::ExactArea, 40, true } },
    { "area  int-ppem unhinted",   { Sizing::IntegerPpem,    Hinting::None,   Sampling::ExactArea, 40, true } },
    { "area  frac-ppem unhinted",  { Sizing::FractionalPpem, Hinting::None,   Sampling::ExactArea, 40, true } },
    { "area  fit-ppem native v40", { Sizing::FittedPpem,     Hinting::Native, Sampling::ExactArea, 40, true } },
};

// One rasteriser's output against another's, glyph for glyph.
struct Score {
    int32_t glyphs = 0, sizeSame = 0, bitmapSame = 0, rowsSame = 0, advanceSame = 0, offsetYSame = 0;
    int32_t baselineSame = 0, heights = 0;
    int64_t pixels = 0, differing = 0, offByOneLevel = 0;
    int32_t maxDelta = 0;      // alpha (0..255)
    int32_t maxLevelDelta = 0; // GDI levels (0..64)
};

void Compare(const RasterFont& a, const RasterFont& b, Score& s) {
    ++s.heights;
    if (a.baseLine == b.baseLine && a.height == b.height) ++s.baselineSame;
    const size_t n = a.glyphs.size() < b.glyphs.size() ? a.glyphs.size() : b.glyphs.size();
    for (size_t i = 0; i < n; ++i) {
        const RasterGlyph& ga = a.glyphs[i];
        const RasterGlyph& gb = b.glyphs[i];
        ++s.glyphs;
        const bool rows = ga.preAdvance == gb.preAdvance && ga.postAdvance == gb.postAdvance &&
                          ga.postKern == gb.postKern && ga.offsetY == gb.offsetY;
        if (rows) ++s.rowsSame;
        if (ga.offsetY == gb.offsetY) ++s.offsetYSame;
        if (ga.preAdvance + ga.postKern + ga.postAdvance == gb.preAdvance + gb.postKern + gb.postAdvance) ++s.advanceSame;
        if (ga.width != gb.width || ga.height != gb.height) continue;
        ++s.sizeSame;
        bool same = true;
        for (size_t p = 0; p < ga.alpha.size(); ++p) {
            ++s.pixels;
            const int32_t d = std::abs(static_cast<int32_t>(ga.alpha[p]) - static_cast<int32_t>(gb.alpha[p]));
            if (d == 0) continue;
            same = false;
            ++s.differing;
            if (d > s.maxDelta) s.maxDelta = d;
            const int32_t la = (ga.alpha[p] + 1) / 4, lb = (gb.alpha[p] + 1) / 4;   // alpha = level * 4 - 1
            const int32_t ld = std::abs(la - lb);
            if (ld == 1) ++s.offByOneLevel;
            if (ld > s.maxLevelDelta) s.maxLevelDelta = ld;
        }
        if (same) ++s.bitmapSame;
    }
}

void PrintScore(const char* name, const Score& s) {
    std::printf("  %-22s bitmaps %3d/%3d identical, sizes %3d/%3d, pixels %6lld/%6lld differ (%lld off by one level, max level step %d, max alpha %3d); rows %3d/%3d, advance %3d/%3d, offsetY %3d/%3d, baseline %d/%d\n",
                name, s.bitmapSame, s.glyphs, s.sizeSame, s.glyphs,
                static_cast<long long>(s.differing), static_cast<long long>(s.pixels),
                static_cast<long long>(s.offByOneLevel), s.maxLevelDelta, s.maxDelta,
                s.rowsSame, s.glyphs, s.advanceSame, s.glyphs, s.offsetYSame, s.glyphs, s.baselineSame, s.heights);
}

}  // namespace

// Every platform: the FreeType rasteriser on the face found by name.
UNIT_TEST(ext_font_raster_freetype_rasterises) {
    std::string why;
    const std::string file = FaceFile(&why);
    if (file.empty()) UNIT_SKIP("FrancophilSans.ttf not found by name (beside the exe / the font directories)");
    std::string family;
    CHECK(FamilyName(file.c_str(), family, &why));
    CHECK_STREQ(family.c_str(), kGameFace);

    const std::vector<int32_t> cps = GameCharset();
    RasterFont f;
    CHECK(RasterizeFile(file.c_str(), kGameFace, 32, cps.data(), static_cast<int32_t>(cps.size()), DefaultOptions(), f, &why));
    CHECK_STREQ(f.face.c_str(), kGameFace);
    CHECK_EQ(f.height, 32);
    CHECK_EQ(f.baseLine, 6);                       // round(usWinDescent 216 * 28 / 1000), the kit's 6 at 32 px
    CHECK_EQ(static_cast<int32_t>(f.glyphs.size()), static_cast<int32_t>(cps.size()));
    if (f.glyphs.size() == cps.size()) {
        const RasterGlyph& sp = f.glyphs[0];       // ' ': GDI's 1x1 blank box
        CHECK_EQ(sp.codepoint, 32);
        CHECK(sp.width == 1 && sp.height == 1 && sp.alpha.size() == 1 && sp.alpha[0] == 0);
        CHECK(sp.preAdvance == 0 && sp.postAdvance == 1 && sp.offsetY == -1);
        const RasterGlyph& A = f.glyphs['A' - 32];
        CHECK(A.width > 8 && A.height > 12 && A.height < 32);
        CHECK_EQ(static_cast<int32_t>(A.alpha.size()), A.width * A.height);
        int32_t maxAlpha = 0;
        for (uint8_t a : A.alpha) if (a > maxAlpha) maxAlpha = a;
        CHECK_EQ(maxAlpha, 255);                   // quantised: the kit's level set tops at 255
        CHECK(A.preAdvance + A.postKern + A.postAdvance > 10);
    }
    // The face name is checked, never substituted; the height range is GDI's.
    RasterFont bad;
    CHECK(!RasterizeFile(file.c_str(), "NoSuchFace_OpenBA_Test", 32, cps.data(), 1, DefaultOptions(), bad, &why));
    CHECK(bad.glyphs.empty() && !why.empty());
    CHECK(!RasterizeFile(file.c_str(), kGameFace, 1, cps.data(), 1, DefaultOptions(), bad, &why));
    CHECK(!RasterizeFile("no/such/file.ttf", kGameFace, 32, cps.data(), 1, DefaultOptions(), bad, &why));
    // The platform contract over it (the posix twin; on Windows this is GDI, which must agree on the shape).
    RasterFont viaContract;
    if (FaceAvailable(kGameFace, &why)) {
        CHECK(RasterizeFont(kGameFace, 32, cps.data(), static_cast<int32_t>(cps.size()), viaContract, &why));
        CHECK_EQ(viaContract.height, 32);
        CHECK_EQ(viaContract.baseLine, f.baseLine);
        CHECK_EQ(viaContract.glyphs.size(), f.glyphs.size());
    }
}

// Windows: GDI and FreeType over the same heights, every option set (FREETYPE.md's table).
UNIT_TEST(ext_font_raster_freetype_vs_gdi) {
#ifndef _WIN32
    UNIT_SKIP("GDI comparison is Windows-only");
#else
    std::string why;
    const std::string file = FaceFile(&why);
    if (file.empty()) UNIT_SKIP("FrancophilSans.ttf not found by name");
    if (!FaceAvailable(kGameFace, &why)) UNIT_SKIP("typeface FrancophilSans not available to GDI");
    const std::vector<int32_t> cps = GameCharset();
    const int32_t heights[] = { 9, 12, 16, 19, 24, 32, 48, 64 };
    const int32_t nHeights = static_cast<int32_t>(sizeof heights / sizeof heights[0]);
    std::vector<RasterFont> gdi(static_cast<size_t>(nHeights));
    for (int32_t h = 0; h < nHeights; ++h)
        CHECK(RasterizeFont(kGameFace, heights[h], cps.data(), static_cast<int32_t>(cps.size()), gdi[static_cast<size_t>(h)], &why));
    std::printf("  FreeType vs GDI, %d glyphs x %d heights (9..64 px):\n", static_cast<int>(cps.size()), nHeights);
    int bestIdx = -1;
    int32_t bestBitmaps = -1;
    for (size_t v = 0; v < sizeof kVariants / sizeof kVariants[0]; ++v) {
        Score s;
        for (int32_t h = 0; h < nHeights; ++h) {
            RasterFont ft;
            CHECK(RasterizeFile(file.c_str(), kGameFace, heights[h], cps.data(), static_cast<int32_t>(cps.size()), kVariants[v].o, ft, &why));
            Compare(gdi[static_cast<size_t>(h)], ft, s);
        }
        PrintScore(kVariants[v].name, s);
        if (s.bitmapSame > bestBitmaps) { bestBitmaps = s.bitmapSame; bestIdx = static_cast<int>(v); }
    }
    if (bestIdx >= 0) {
        std::printf("  closest on bitmaps: %s; per height:\n", kVariants[bestIdx].name);
        for (int32_t h = 0; h < nHeights; ++h) {
            RasterFont ft;
            CHECK(RasterizeFile(file.c_str(), kGameFace, heights[h], cps.data(), static_cast<int32_t>(cps.size()), kVariants[bestIdx].o, ft, &why));
            Score s;
            Compare(gdi[static_cast<size_t>(h)], ft, s);
            char name[48];
            std::snprintf(name, sizeof name, "  %3d px (base GDI %d FT %d)", heights[h], gdi[static_cast<size_t>(h)].baseLine, ft.baseLine);
            PrintScore(name, s);
        }
    }
#endif
}

// The FreeType output against the kit's compiled variants (the oracle CompareVariant uses), counted, never failed.
UNIT_TEST(ext_font_raster_freetype_vs_kit_variant) {
    const std::string dir = KitFontDir();
    if (dir.empty()) UNIT_SKIP("no kit-compiled fonts under build/ext_resources/WinData/Font (tools/ext_resources.sh)");
    std::string why;
    const std::string file = FaceFile(&why);
    if (file.empty()) UNIT_SKIP("FrancophilSans.ttf not found by name");
    struct Case { const char* kitName; int32_t px; };
    const Case cases[] = { { "SharpFont_32", 32 }, { "Font", 24 }, { "SharpFontSmall_19", 19 }, { "SharpFontMedium_12", 12 }, { "SharpFontLarge_64", 64 } };
    std::printf("  FreeType (default options) vs the kit's variants:\n");
    for (const Case& c : cases) {
        const std::string crs = dir + "/" + c.kitName + "_resource.crs";
        if (!Exists(crs)) continue;
        ::KitFont::Font oracle;
        if (!::KitFont::Load(crs.c_str(), kGameFace, oracle) || oracle.glyphs.empty()) continue;
        const std::vector<int32_t> cps = ::KitFont::GlyphCodepoints(oracle);
        RasterFont ours;
        CHECK(RasterizeFile(file.c_str(), kGameFace, c.px, cps.data(), static_cast<int32_t>(cps.size()), DefaultOptions(), ours, &why));
        if (ours.glyphs.size() != cps.size()) continue;
        int32_t rowsExact = 0, advExact = 0, offExact = 0, withinOne = 0;
        for (size_t i = 0; i < cps.size(); ++i) {
            const ::KitFont::GlyphRow& k = oracle.glyphs[i];
            const RasterGlyph& g = ours.glyphs[i];
            const bool adv = g.preAdvance + g.postKern + g.postAdvance == k.preAdvance + k.postKern + k.postAdvance;
            if (adv) ++advExact;
            if (g.offsetY == k.offsetY) ++offExact;
            if (g.preAdvance == k.preAdvance && g.postAdvance == k.postAdvance && g.postKern == k.postKern && g.offsetY == k.offsetY) ++rowsExact;
            if (adv && g.offsetY == k.offsetY && std::abs(g.preAdvance - k.preAdvance) <= 1 && std::abs(g.postAdvance - k.postAdvance) <= 1) ++withinOne;
        }
        std::printf("    %-20s %2d px: line %d/%d baseline %d/%d; rows exact %3d/%3zu, within one %3d/%3zu, advance %3d/%3zu, offsetY %3d/%3zu\n",
                    c.kitName, c.px, ours.height, oracle.height, ours.baseLine, oracle.baseLine,
                    rowsExact, cps.size(), withinOne, cps.size(), advExact, cps.size(), offExact, cps.size());
    }
}
