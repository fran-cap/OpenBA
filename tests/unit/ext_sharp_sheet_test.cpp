// ============================================================================
// tests/unit/ext_sharp_sheet_test.cpp -- ext.sharp_ui's magnified sheet, pure
// ============================================================================
// src/ext/sharp_ui/sharp_sheet.{h,cpp}: the rect scaling, the name + size match
// of our art, the per-sprite upscale (no neighbour bleed), the art placement
// with its pad ring and the RGBA -> BGRA swap, at factors 2 and 4. Plus the
// generated art table (sharp_sprites_data.h): every sprite sized as a whole
// multiple, every name unique.
// ============================================================================
#include "unit.h"
#include "ext/sharp_ui/sharp_sheet.h"
#include "ext/sharp_ui/sharp_sprites_data.h"

#include <cstring>
#include <vector>

using namespace Ext::SharpUI;

namespace {

const uint8_t* Texel(const std::vector<uint8_t>& img, int w, int x, int y) {
    return img.data() + (static_cast<size_t>(y) * w + x) * 4;
}

// A base atlas `w x h`, BGRA, every texel (b, g, r, a) = (v, v, v, 255) with v
// the value `fill(x, y)` returns.
template <class F>
std::vector<uint8_t> Atlas(int w, int h, F fill) {
    std::vector<uint8_t> a(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* t = a.data() + (static_cast<size_t>(y) * w + x) * 4;
            const int v = fill(x, y);
            t[0] = t[1] = t[2] = static_cast<uint8_t>(v);
            t[3] = 255;
        }
    return a;
}

}  // namespace

UNIT_TEST(ext_sharp_sheet_scale_rect) {
    for (int n : {2, 4}) {
        const SheetRect r = ScaleRect(SheetRect{3, 5, 7, 1}, n);
        CHECK(r.x == 3 * n && r.y == 5 * n && r.width == 7 * n && r.height == 1 * n);
    }
    const SheetRect p = PadRect(SheetRect{0, 4, 10, 2}, 1, 16, 16);   // clipped at the left edge
    CHECK(p.x == 0 && p.y == 3 && p.width == 11 && p.height == 4);
    const SheetRect q = PadRect(SheetRect{12, 12, 4, 4}, 2, 16, 16);  // and at the far corner
    CHECK(q.x == 10 && q.y == 10 && q.width == 6 && q.height == 6);
}

UNIT_TEST(ext_sharp_sheet_rect_from_uvs) {
    // A 16 x 8 sprite at (37, 101) in a 1024 x 1024 atlas: the record's x / y
    // say 1 / 1 (the pad offset); the UVs carry the place.
    const float s = 1024.0f;
    const SheetRect r = RectFromUvs(37 / s, 101 / s, 53 / s, 109 / s, 1024, 1024);
    CHECK(r.x == 37 && r.y == 101 && r.width == 16 && r.height == 8);
    const SheetRect z = RectFromUvs(0.0f, 0.0f, 0.0f, 0.0f, 1024, 1024);   // a degenerate record
    CHECK(z.width == 0 && z.height == 0);
}

UNIT_TEST(ext_sharp_sheet_find_art_by_name_and_size) {
    static const uint8_t px[4 * 4 * 4] = {};
    const ArtSprite table[] = {
        {"ArrowRight", 4, 4, px},
        {"ArrowRightThin", 2, 4, px},
    };
    CHECK(FindArt(table, 2, "ArrowRight", 2, 2, 2) == &table[0]);
    CHECK(FindArt(table, 2, "ArrowRightThin", 1, 2, 2) == &table[1]);   // a sub-rectangle sprite
    CHECK(FindArt(table, 2, "ArrowRight", 3, 2, 2) == nullptr);         // a modded sheet's size: keep its own
    CHECK(FindArt(table, 2, "ArrowRight", 1, 1, 4) == &table[0]);       // the same art read as 4x of 1x1
    CHECK(FindArt(table, 2, "ArrowLeft", 2, 2, 2) == nullptr);
    CHECK(FindArt(table, 2, nullptr, 2, 2, 2) == nullptr);
}

UNIT_TEST(ext_sharp_sheet_upscale_stays_inside_its_rect) {
    // Two sprites side by side with one-texel pad rings, 2 texels apart (the
    // kit's padded layout): left all 10, right all 200, gap 0.
    const int w = 12, h = 4;
    auto base = Atlas(w, h, [](int x, int) { return x < 6 ? (x >= 1 && x <= 4 ? 10 : 0) : (x >= 7 && x <= 10 ? 200 : 0); });
    for (int n : {2, 4}) {
        std::vector<uint8_t> out(static_cast<size_t>(w * n) * h * n * 4, 0);
        // the left sprite is texels 2..3, its ring 1..4: upscale only that
        UpscaleRect(base.data(), w, out.data(), PadRect(SheetRect{2, 1, 2, 2}, 1, w, h), n);
        for (int y = 0 * n; y < 4 * n; ++y)
            for (int x = 1 * n; x < 5 * n; ++x)
                CHECK(Texel(out, w * n, x, y)[0] == 10);   // flat: no 0 from the gap, no 200
        CHECK(Texel(out, w * n, 5 * n, 0)[3] == 0);        // outside the ring: untouched
    }
}

UNIT_TEST(ext_sharp_sheet_upscale_is_bilinear) {
    // A 2-texel ramp 0 -> 200 doubled: centres at 1/4 and 3/4 of each source
    // texel; clamped at the rect's ends.
    const int w = 2, h = 1;
    auto base = Atlas(w, h, [](int x, int) { return x == 0 ? 0 : 200; });
    std::vector<uint8_t> out(4 * 2 * 4, 0);
    UpscaleRect(base.data(), w, out.data(), SheetRect{0, 0, 2, 1}, 2);
    CHECK(Texel(out, 4, 0, 0)[0] == 0);     // clamped to texel 0
    CHECK(Texel(out, 4, 1, 0)[0] == 50);    // 0.25 of the way
    CHECK(Texel(out, 4, 2, 0)[0] == 150);   // 0.75
    CHECK(Texel(out, 4, 3, 0)[0] == 200);   // clamped to texel 1
}

UNIT_TEST(ext_sharp_sheet_place_art_with_ring) {
    for (int n : {2, 4}) {
        // One base sprite 1x1 at (2, 2) with a pad ring of 1: the art is n x n.
        std::vector<uint8_t> art(static_cast<size_t>(n) * n * 4);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                uint8_t* p = art.data() + (static_cast<size_t>(y) * n + x) * 4;
                p[0] = static_cast<uint8_t>(10 + x);   // R
                p[1] = static_cast<uint8_t>(20 + y);   // G
                p[2] = 30;                              // B
                p[3] = 255;
            }
        const int bw = 6, bh = 6;
        auto base = Atlas(bw, bh, [](int, int) { return 99; });
        const ScaledSprite one{SheetRect{2, 2, 1, 1}, art.data()};
        std::vector<uint8_t> out;
        CHECK(BuildScaledSheet(base.data(), bw, bh, n, 1, &one, 1, out) == 1);
        CHECK(out.size() == static_cast<size_t>(bw * n) * bh * n * 4);
        const int W = bw * n;
        // the art itself, BGRA
        const uint8_t* t = Texel(out, W, 2 * n, 2 * n);
        CHECK(t[0] == 30 && t[1] == 20 && t[2] == 10 && t[3] == 255);
        const uint8_t* last = Texel(out, W, 3 * n - 1, 3 * n - 1);
        CHECK(last[2] == 10 + n - 1 && last[1] == 20 + n - 1);
        // the ring (n texels wide) repeats the edge: left of the first column, above the first row
        const uint8_t* left = Texel(out, W, 2 * n - n, 2 * n);
        CHECK(left[2] == 10 && left[1] == 20);
        const uint8_t* corner = Texel(out, W, n, n);   // the top-left corner of the ring
        CHECK(corner[2] == 10 && corner[1] == 20);
        const uint8_t* farCorner = Texel(out, W, 4 * n - 1, 4 * n - 1);
        CHECK(farCorner[2] == 10 + n - 1 && farCorner[1] == 20 + n - 1);
        // beyond the ring: nothing (no sprite there)
        CHECK(Texel(out, W, 4 * n, 4 * n)[3] == 0);
    }
}

UNIT_TEST(ext_sharp_sheet_upscaled_sprite_without_art) {
    // A sprite with no art is the base one, upscaled, at the doubled place.
    const int bw = 4, bh = 4;
    auto base = Atlas(bw, bh, [](int x, int y) { return (x == 1 && y == 1) ? 77 : 0; });
    const ScaledSprite one{SheetRect{1, 1, 1, 1}, nullptr};
    std::vector<uint8_t> out;
    CHECK(BuildScaledSheet(base.data(), bw, bh, 2, 0, &one, 1, out) == 0);
    for (int y = 2; y < 4; ++y)
        for (int x = 2; x < 4; ++x)
            CHECK(Texel(out, 8, x, y)[0] == 77 && Texel(out, 8, x, y)[3] == 255);
    CHECK(Texel(out, 8, 1, 1)[3] == 0);
}

UNIT_TEST(ext_sharp_sheet_art_table) {
    using namespace Ext::SharpUI::SpritesData;
    CHECK(kScale == 2);
    CHECK(kSpriteCount > 200);
    for (int i = 0; i < kSpriteCount; ++i) {
        const ArtSprite& a = kSprites[i];
        CHECK(a.width > 0 && a.height > 0 && a.rgba != nullptr);
        CHECK(a.width % kScale == 0 && a.height % kScale == 0);
        for (int j = i + 1; j < kSpriteCount; ++j) CHECK(std::strcmp(a.name, kSprites[j].name) != 0);
    }
}
