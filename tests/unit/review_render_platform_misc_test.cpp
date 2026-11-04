// ============================================================================
// tests/unit/review_render_platform_misc_test.cpp -- review 2026-11-03, group render-platform-misc
// ============================================================================
// F-135 the log redirect on an unwritable path, F-167 the FreeType rasteriser on a face with no
// cell height, F-171 the refresh-rate index read against another mode's list, F-207 the font face
// lookup cache, F-211 the DXBC customdata length wrap. Malformed inputs are inline as hex.
// F-207's lookup is the FreeType twins' (Linux, macOS, Android); on Windows the same calls reach
// GDI, where only the first half of the test applies (it passes there before and after).
// ============================================================================
#include "unit.h"
#include "app/log_redirect.h"
#include "better_trees/dxbc.h"
#include "ext/resources/font_raster.h"
#include "font_raster_freetype.h"
#include "video/refresh_rate_at.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> FromHex(const char* hex) {
    std::vector<uint8_t> out;
    int nibbles = 0;
    uint8_t cur = 0;
    for (const char* p = hex; *p != 0; ++p) {
        int v = -1;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        if (v < 0) continue;
        cur = static_cast<uint8_t>((cur << 4) | v);
        if (++nibbles == 2) {
            out.push_back(cur);
            nibbles = 0;
            cur = 0;
        }
    }
    return out;
}

std::string ReadAll(const char* path) {
    std::string s;
    std::FILE* f = std::fopen(path, "r");   // text mode: the logs are written in text mode
    if (f == nullptr) return s;
    char buf[256];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// ---- a minimal TrueType face (big-endian tables) ------------------------------------------
void Be16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}
void Be32(std::vector<uint8_t>& v, uint32_t x) {
    Be16(v, x >> 16);
    Be16(v, x & 0xFFFF);
}

struct Table {
    const char*          tag;
    std::vector<uint8_t> data;
};

// One empty glyph, family `family`, hhea ascender / descender as given and NO OS/2 table
// (FreeType then takes the cell from hhea, as the rasteriser's CellMetrics does).
std::vector<uint8_t> MinimalFace(const char* family, int16_t ascender, int16_t descender) {
    std::vector<Table> t;
    {
        Table cmap{"cmap", {}};   // one format 4 subtable holding only the 0xFFFF segment
        std::vector<uint8_t>& d = cmap.data;
        Be16(d, 0);
        Be16(d, 1);
        Be16(d, 3);
        Be16(d, 1);
        Be32(d, 12);
        Be16(d, 4);
        Be16(d, 24);
        Be16(d, 0);
        Be16(d, 2);
        Be16(d, 2);
        Be16(d, 0);
        Be16(d, 0);
        Be16(d, 0xFFFF);
        Be16(d, 0);
        Be16(d, 0xFFFF);
        Be16(d, 1);
        Be16(d, 0);
        t.push_back(cmap);
    }
    t.push_back(Table{"glyf", {0, 0, 0, 0}});
    {
        Table head{"head", {}};
        std::vector<uint8_t>& d = head.data;
        Be32(d, 0x00010000);
        Be32(d, 0x00010000);
        Be32(d, 0);
        Be32(d, 0x5F0F3CF5);
        Be16(d, 0);
        Be16(d, 1000);   // unitsPerEm
        for (int i = 0; i < 16; ++i) d.push_back(0);
        for (int i = 0; i < 4; ++i) Be16(d, 0);
        Be16(d, 0);
        Be16(d, 8);
        Be16(d, 2);
        Be16(d, 0);   // short loca
        Be16(d, 0);
        t.push_back(head);
    }
    {
        Table hhea{"hhea", {}};
        std::vector<uint8_t>& d = hhea.data;
        Be32(d, 0x00010000);
        Be16(d, static_cast<uint16_t>(ascender));
        Be16(d, static_cast<uint16_t>(descender));
        Be16(d, 0);
        Be16(d, 500);
        for (int i = 0; i < 3; ++i) Be16(d, 0);
        Be16(d, 1);
        Be16(d, 0);
        Be16(d, 0);
        for (int i = 0; i < 4; ++i) Be16(d, 0);
        Be16(d, 0);
        Be16(d, 1);   // numberOfHMetrics
        t.push_back(hhea);
    }
    t.push_back(Table{"hmtx", {0x01, 0xF4, 0, 0}});
    t.push_back(Table{"loca", {0, 0, 0, 0}});
    {
        Table maxp{"maxp", {}};
        std::vector<uint8_t>& d = maxp.data;
        Be32(d, 0x00010000);
        Be16(d, 1);   // numGlyphs
        for (int i = 0; i < 13; ++i) Be16(d, i == 4 ? 2 : 0);   // maxZones 2
        t.push_back(maxp);
    }
    {
        Table name{"name", {}};
        std::vector<uint8_t>& d = name.data;
        const std::string f = family;
        Be16(d, 0);
        Be16(d, 1);
        Be16(d, 18);
        Be16(d, 3);
        Be16(d, 1);
        Be16(d, 0x409);
        Be16(d, 1);   // family
        Be16(d, static_cast<uint32_t>(f.size() * 2));
        Be16(d, 0);
        for (char c : f) Be16(d, static_cast<uint8_t>(c));
        t.push_back(name);
    }
    std::vector<uint8_t> out;
    const uint32_t n = static_cast<uint32_t>(t.size());
    Be32(out, 0x00010000);
    Be16(out, n);
    Be16(out, 128);   // searchRange for 8 tables
    Be16(out, 3);
    Be16(out, n * 16 - 128);
    uint32_t at = 12 + n * 16;
    for (const Table& x : t) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(x.tag[i]));
        Be32(out, 0);
        Be32(out, at);
        Be32(out, static_cast<uint32_t>(x.data.size()));
        at += (static_cast<uint32_t>(x.data.size()) + 3) & ~3u;
    }
    for (const Table& x : t) {
        out.insert(out.end(), x.data.begin(), x.data.end());
        while (out.size() & 3) out.push_back(0);
    }
    return out;
}

bool WriteFile(const char* path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace

// F-211: a customdata length of 2^32 - 1 at dword 1 wraps `at + length` to 0, which passed the
// bound and moved `at` back to 0: ParseProgram looped forever, growing `instrs` until OOM.
UNIT_TEST(review_dxbc_customdata_length_wrap_is_refused) {
    Dxbc::Chunk shdr;
    shdr.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    // ps_4_0, 5 dwords: nop (length 1), customdata (opcode 53) with length 0xFFFFFFFF.
    shdr.data = FromHex("40000000 05000000 3a000001 35000000 ffffffff");
    Dxbc::Program p;
    std::string err;
    CHECK(!Dxbc::ParseProgram(shdr, &p, &err));
    CHECK(err.find("bad instruction length") != std::string::npos);
    // A customdata block that fits is still taken whole.
    shdr.data = FromHex("40000000 06000000 35000000 03000000 aabbccdd 3a000001");
    CHECK(Dxbc::ParseProgram(shdr, &p, &err));
    CHECK_EQ(p.instrs.size(), static_cast<size_t>(2));
}

// F-167: a face with no OS/2 table and hhea ascender = descender = 0 has a zero cell height;
// RasterizeFile divided by it and converted inf / NaN to integers (UB). It is refused now.
UNIT_TEST(review_freetype_zero_cell_height_is_refused) {
    using namespace Ext::Resources;
    const char* path = "review_rpm_zero_cell.ttf";
    if (!WriteFile(path, MinimalFace("ZeroCellTest", 0, 0))) UNIT_SKIP("cannot write the test face");
    std::string family;
    std::string why;
    CHECK(FreeTypeRaster::FamilyName(path, family, &why));
    CHECK_STREQ(family.c_str(), "ZeroCellTest");
    RasterFont out;
    const bool ok = FreeTypeRaster::RasterizeFile(path, "ZeroCellTest", 16, nullptr, 0,
                                                  FreeTypeRaster::DefaultOptions(), out, &why);
    CHECK(!ok);
    CHECK_STREQ(why.c_str(), "face has no cell height");
    // The same face with a cell rasterises (no glyphs asked for).
    const char* good = "review_rpm_good_cell.ttf";
    if (WriteFile(good, MinimalFace("ZeroCellTest", 800, -200))) {
        CHECK(FreeTypeRaster::RasterizeFile(good, "ZeroCellTest", 16, nullptr, 0,
                                            FreeTypeRaster::DefaultOptions(), out, &why));
        CHECK_EQ(out.height, 16);
        std::remove(good);
    }
    std::remove(path);
}

// F-207: the FreeType twins cached the FIRST face asked for and answered every later face with
// it. Two absent faces: the second answer must name the second face.
UNIT_TEST(review_font_face_lookup_is_per_face) {
    using namespace Ext::Resources;
    std::string whyA;
    std::string whyB;
    CHECK(!FaceAvailable("NoSuchFaceA_OpenBA_Review", &whyA));
    CHECK(!FaceAvailable("NoSuchFaceB_OpenBA_Review", &whyB));
    CHECK(whyB.find("NoSuchFaceA_OpenBA_Review") == std::string::npos);
#ifndef _WIN32
    // The FreeType twins name the file they looked for (GDI may answer "GDI substituted").
    CHECK(whyB.find("NoSuchFaceB_OpenBA_Review") != std::string::npos);
#endif
}

// F-171: Device::refreshRateIndex is chosen for the boot mode; after an Options resolution change
// it indexed another mode's (shorter) refresh list. Out of range now reads entry 0.
UNIT_TEST(review_refresh_rate_index_out_of_the_mode) {
    // One rate in the mode; the storage behind it stands in for whatever follows the list.
    Video::Device::RefreshRate storage[4] = {{60, 1}, {999, 7}, {999, 7}, {999, 7}};
    Video::Device::DisplayMode mode;
    mode.width = 1280;
    mode.height = 720;
    mode.refreshData = storage;
    mode.refreshCount = 1;
    mode.refreshCapacity = 1;
    Video::Device::RefreshRate r = Video::RefreshRateAt(mode, 3);   // the boot mode's 165 Hz index
    CHECK_EQ(r.numerator, 60);
    CHECK_EQ(r.denominator, 1);
    r = Video::RefreshRateAt(mode, 0);
    CHECK_EQ(r.numerator, 60);
    // An empty list gives 0 / 1 (DXGI: unspecified).
    mode.refreshCount = 0;
    r = Video::RefreshRateAt(mode, 0);
    CHECK_EQ(r.numerator, 0);
    CHECK_EQ(r.denominator, 1);
    // In range: retail's read.
    Video::Device::RefreshRate four[4] = {{60, 1}, {120, 1}, {144, 1}, {165, 1}};
    mode.refreshData = four;
    mode.refreshCount = 4;
    r = Video::RefreshRateAt(mode, 3);
    CHECK_EQ(r.numerator, 165);
}

// F-135: freopen closes the stream first; on a path that cannot be created the stream was left
// closed and every later write was lost (use of a closed FILE). The stream now stays as it was.
UNIT_TEST(review_log_redirect_keeps_the_stream_on_failure) {
    const char* path = "review_rpm_log_redirect.txt";
    std::FILE* f = std::fopen(path, "w");
    CHECK(f != nullptr);
    if (f == nullptr) return;
    std::fputs("before\n", f);
    const bool redirected = RedirectLogStream("no_such_dir_openba_review/sub/standalone_debug.log", "w", f);
    CHECK(!redirected);
    std::fputs("after\n", f);
    std::fclose(f);
    CHECK_STREQ(ReadAll(path).c_str(), "before\nafter\n");
    std::remove(path);
    // A writable path is taken as before.
    std::FILE* g = std::fopen(path, "w");
    CHECK(g != nullptr);
    if (g == nullptr) return;
    const char* other = "review_rpm_log_redirect_2.txt";
    CHECK(RedirectLogStream(other, "w", g));
    std::fputs("moved\n", g);
    std::fclose(g);
    CHECK_STREQ(ReadAll(other).c_str(), "moved\n");
    std::remove(path);
    std::remove(other);
}
