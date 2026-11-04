// ============================================================================
// src/diag/font_dump/font_dump.cpp -- the font rasteriser's by-eye harness (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL, diagnostics only, a standalone debug-flavour executable (CMake
// target `font_dump`, Windows only, EXCLUDE_FROM_ALL; no engine, no game
// directory, no BANISHED_* flag -- the in-exe harnesses are flag-driven and
// new flags are closed, DIRECTION.md §2).
//
//   font_dump <Kit> <px> <kit WinData dir> <out dir> [<kit res/Build dir>]
//
// Rasterises the game font <Kit> (Font | FontSmall | FontMedium | FontLarge)
// at <px> with src/platform/win/font_raster_win.cpp, exactly as the provider
// does, reads the mod kit's compiled Font/Sharp<Kit>_<px>_resource.crs from
// <kit WinData dir> (tools/ext_resources.sh's build/ext_resources/WinData; the
// base <Kit>_resource.crs when <px> is the base height) for the codepoint
// set and the oracle tables, prints the metrics comparison, and writes
//   <out>/ours_Sharp<Kit>_<px>.png   our glyphs packed on a sheet (white + alpha)
//   <out>/kit_Sharp<Kit>_<px>.png    a copy of the kit's packed sheet
//                                    (<res/Build>/uiSharp<Kit>_<px>Image.png) when given
// for the coordinator to compare by eye. The PNG writer is minimal and
// uncompressed (stored deflate blocks). The PNGs are the game's glyphs: never
// commit them (KITFREE.md §1).
// ============================================================================
#include "ext/resources/font_raster.h"
#include "ext/resources/font_tables.h"
#include "kit_font_reader.h"
#include "app/clamped_atoi.h"   // review F-087: checked parse of the size argument

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0xffffffffu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xffu] ^ (crc >> 8);
    return crc;
}

void Be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24)); v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));  v.push_back(static_cast<uint8_t>(x));
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    Be32(out, Crc32(td.data(), td.size()) ^ 0xffffffffu);
}

// RGBA8, stored (uncompressed) deflate.
bool WritePng(const char* path, int32_t w, int32_t h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 4 + 1));
    for (int32_t y = 0; y < h; ++y) {
        raw.push_back(0);   // filter: none
        raw.insert(raw.end(), rgba.begin() + static_cast<size_t>(y) * w * 4, rgba.begin() + static_cast<size_t>(y + 1) * w * 4);
    }
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    while (pos < raw.size() || raw.empty()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n)); z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (raw.empty()) break;
    }
    Be32(z, (b << 16) | a);
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    std::vector<uint8_t> ihdr;
    Be32(ihdr, static_cast<uint32_t>(w)); Be32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", std::vector<uint8_t>());
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

bool CopyFile(const char* from, const char* to) {
    std::vector<uint8_t> d;
    if (!KitFont::ReadFile(from, d)) return false;
    std::FILE* f = std::fopen(to, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: font_dump <Kit> <px> <kit WinData dir> <out dir> [<kit res/Build dir>]\n");
        return 2;
    }
    using namespace Ext::Resources;
    int32_t kit = -1;
    for (int32_t k = 0; k < kKitFontCount; ++k) if (std::strcmp(argv[1], kKitFonts[k].name) == 0) kit = k;
    if (kit < 0) { std::fprintf(stderr, "font_dump: unknown kit font %s\n", argv[1]); return 2; }
    const int32_t px = HarnessAtoi(argv[2]);
    const std::string winData = argv[3], outDir = argv[4];
    const VariantNames names = MakeVariantNames(kit, px);
    const std::string crsName = px == kKitFonts[kit].height ? std::string(kKitFonts[kit].name) : names.file;
    const std::string crs = winData + "/Font/" + crsName + "_resource.crs";

    KitFont::Font oracle;
    if (!KitFont::Load(crs.c_str(), kGameFace, oracle)) { std::fprintf(stderr, "font_dump: cannot read %s\n", crs.c_str()); return 1; }
    const std::vector<int32_t> cps = KitFont::GlyphCodepoints(oracle);
    for (size_t i = 0; i < cps.size(); ++i)
        if (cps[i] < 0) { std::fprintf(stderr, "font_dump: kit glyph %zu has no codepoint\n", i); return 1; }

    RasterFont ours;
    std::string why;
    if (!RasterizeFont(kGameFace, px, cps.data(), static_cast<int32_t>(cps.size()), ours, &why)) {
        std::fprintf(stderr, "font_dump: rasterise failed: %s\n", why.c_str());
        return 1;
    }

    std::printf("%s at %d px: kit line %d baseline %d ellipsis %d space %d, %zu glyphs; ours line %d baseline %d\n",
                kKitFonts[kit].name, px, oracle.height, oracle.baseLine, oracle.ellipsisWidth, oracle.spaceWidth,
                oracle.glyphs.size(), ours.height, ours.baseLine);
    std::printf(" cp  | kit pre kern post offY | ours pre kern post offY  w x h\n");
    int32_t exact = 0, off1 = 0, worse = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        const KitFont::GlyphRow& k = oracle.glyphs[i];
        const RasterGlyph& g = ours.glyphs[i];
        // In 64 bits: the kit row comes unchecked from the .crs, and a value near 2^31
        // overflowed the int32 differences and sums (review F-283).
        const int64_t d = std::llabs(int64_t{k.preAdvance} - g.preAdvance) +
                          std::llabs(int64_t{k.postKern} - g.postKern) +
                          std::llabs(int64_t{k.postAdvance} - g.postAdvance) +
                          std::llabs(int64_t{k.offsetY} - g.offsetY);
        const bool advanceSame = int64_t{k.preAdvance} + k.postKern + k.postAdvance ==
                                 int64_t{g.preAdvance} + g.postKern + g.postAdvance;
        if (d == 0) ++exact; else if (d <= 2 && advanceSame && k.offsetY == g.offsetY) ++off1; else ++worse;
        if (d != 0)
            std::printf("%4d | %4d %4d %4d %4d | %4d %4d %4d %4d  %dx%d%s\n", cps[i], k.preAdvance, k.postKern, k.postAdvance, k.offsetY,
                        g.preAdvance, g.postKern, g.postAdvance, g.offsetY, g.width, g.height, d > 2 || !advanceSame ? "  <-- more than one pixel" : "");
    }
    GlyphRow dot{0, 0, 0, 0}, sp{0, 0, 0, 0};
    for (size_t i = 0; i < cps.size(); ++i) {
        const RasterGlyph& g = ours.glyphs[i];
        if (cps[i] == '.') dot = GlyphRow{g.preAdvance, g.postKern, g.postAdvance, g.offsetY};
        if (cps[i] == ' ') sp = GlyphRow{g.preAdvance, g.postKern, g.postAdvance, g.offsetY};
    }
    const GlyphRow dots[3] = { dot, dot, dot };
    std::printf("rows: %d exact, %d one pixel moved between pre/post and kern (same advance), %d other; ours ellipsis %d space %d\n",
                exact, off1, worse, MeasureRun(dots, 3), MeasureRun(&sp, 1));

    // ---- our sheet: shelf packing with a one-texel transparent ring -------
    int32_t sw = 0, sh = 0;
    KitSheetSize(px, sw, sh);
    if (sw == 0) { sw = 1024; sh = 1024; }
    std::vector<uint8_t> rgba(static_cast<size_t>(sw) * sh * 4, 0);
    int32_t x = 1, y = 1, rowH = 0;
    for (const RasterGlyph& g : ours.glyphs) {
        if (x + g.width + 1 > sw) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + g.height + 1 > sh) { std::fprintf(stderr, "font_dump: sheet %dx%d too small\n", sw, sh); break; }
        for (int32_t gy = 0; gy < g.height; ++gy)
            for (int32_t gx = 0; gx < g.width; ++gx) {
                uint8_t* p = &rgba[(static_cast<size_t>(y + gy) * sw + static_cast<size_t>(x + gx)) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = g.alpha[static_cast<size_t>(gy) * g.width + gx];
            }
        x += g.width + 1;
        if (g.height > rowH) rowH = g.height;
    }
    const std::string oursPng = outDir + "/ours_" + names.file + ".png";
    if (!WritePng(oursPng.c_str(), sw, sh, rgba)) { std::fprintf(stderr, "font_dump: cannot write %s\n", oursPng.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", oursPng.c_str(), sw, sh);
    if (argc > 5) {
        const std::string kitPng = std::string(argv[5]) + "/ui" + names.file + "Image.png";
        const std::string copy = outDir + "/kit_" + names.file + ".png";
        if (CopyFile(kitPng.c_str(), copy.c_str())) std::printf("copied the kit's sheet to %s\n", copy.c_str());
        else std::printf("no kit sheet at %s\n", kitPng.c_str());
    }
    return worse == 0 ? 0 : 1;
}
