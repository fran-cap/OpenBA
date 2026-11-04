// ============================================================================
// src/ext/sharp_ui/sharp_sheet.cpp -- the magnified sheet's pure arithmetic
// (NOT RETAIL; sharp_sheet.h has the contract)
// ============================================================================
#include "ext/sharp_ui/sharp_sheet.h"

#include <cmath>
#include <cstring>

namespace Ext {
namespace SharpUI {

namespace {

int Clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

SheetRect RectFromUvs(float u0, float v0, float u1, float v1, int sheetW, int sheetH) {
    const int x0 = static_cast<int>(std::lround(u0 * static_cast<float>(sheetW)));
    const int y0 = static_cast<int>(std::lround(v0 * static_cast<float>(sheetH)));
    const int x1 = static_cast<int>(std::lround(u1 * static_cast<float>(sheetW)));
    const int y1 = static_cast<int>(std::lround(v1 * static_cast<float>(sheetH)));
    return SheetRect{Clamp(x0, 0, sheetW), Clamp(y0, 0, sheetH), Clamp(x1 - x0, 0, sheetW), Clamp(y1 - y0, 0, sheetH)};
}

SheetRect ScaleRect(const SheetRect& r, int scale) {
    return SheetRect{r.x * scale, r.y * scale, r.width * scale, r.height * scale};
}

SheetRect PadRect(const SheetRect& r, int ring, int sheetW, int sheetH) {
    const int x0 = Clamp(r.x - ring, 0, sheetW);
    const int y0 = Clamp(r.y - ring, 0, sheetH);
    const int x1 = Clamp(r.x + r.width + ring, 0, sheetW);
    const int y1 = Clamp(r.y + r.height + ring, 0, sheetH);
    return SheetRect{x0, y0, x1 - x0, y1 - y0};
}

const ArtSprite* FindArt(const ArtSprite* table, int count, const char* name, int baseW, int baseH,
                         int scale) {
    if (name == nullptr) return nullptr;
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(table[i].name, name) != 0) continue;
        if (table[i].width != baseW * scale || table[i].height != baseH * scale) return nullptr;
        return &table[i];
    }
    return nullptr;
}

void UpscaleRect(const uint8_t* src, int srcW, uint8_t* dst, const SheetRect& from, int scale) {
    if (from.width <= 0 || from.height <= 0) return;
    const int dstW = srcW * scale;
    const SheetRect to = ScaleRect(from, scale);
    const float inv = 1.0f / static_cast<float>(scale);
    const int xMax = from.x + from.width - 1;
    const int yMax = from.y + from.height - 1;
    for (int ty = 0; ty < to.height; ++ty) {
        // The source position of this texel's centre, in source texels.
        const float sy = (static_cast<float>(ty) + 0.5f) * inv - 0.5f + static_cast<float>(from.y);
        const float fy0 = std::floor(sy);
        const float wy = sy - fy0;
        const int y0 = Clamp(static_cast<int>(fy0), from.y, yMax);
        const int y1 = Clamp(static_cast<int>(fy0) + 1, from.y, yMax);
        uint8_t* row = dst + (static_cast<size_t>(to.y + ty) * dstW + to.x) * 4;
        for (int tx = 0; tx < to.width; ++tx) {
            const float sx = (static_cast<float>(tx) + 0.5f) * inv - 0.5f + static_cast<float>(from.x);
            const float fx0 = std::floor(sx);
            const float wx = sx - fx0;
            const int x0 = Clamp(static_cast<int>(fx0), from.x, xMax);
            const int x1 = Clamp(static_cast<int>(fx0) + 1, from.x, xMax);
            const uint8_t* a = src + (static_cast<size_t>(y0) * srcW + x0) * 4;
            const uint8_t* b = src + (static_cast<size_t>(y0) * srcW + x1) * 4;
            const uint8_t* c = src + (static_cast<size_t>(y1) * srcW + x0) * 4;
            const uint8_t* d = src + (static_cast<size_t>(y1) * srcW + x1) * 4;
            for (int k = 0; k < 4; ++k) {
                const float top = static_cast<float>(a[k]) + (static_cast<float>(b[k]) - a[k]) * wx;
                const float bot = static_cast<float>(c[k]) + (static_cast<float>(d[k]) - c[k]) * wx;
                const float v = top + (bot - top) * wy;
                row[tx * 4 + k] = static_cast<uint8_t>(Clamp(static_cast<int>(v + 0.5f), 0, 255));
            }
        }
    }
}

void PlaceArt(uint8_t* dst, int dstW, int dstH, const SheetRect& r, const uint8_t* rgba, int ring) {
    const SheetRect p = PadRect(r, ring, dstW, dstH);
    for (int y = p.y; y < p.y + p.height; ++y) {
        const int ay = Clamp(y - r.y, 0, r.height - 1);
        for (int x = p.x; x < p.x + p.width; ++x) {
            const int ax = Clamp(x - r.x, 0, r.width - 1);
            const uint8_t* s = rgba + (static_cast<size_t>(ay) * r.width + ax) * 4;
            uint8_t* o = dst + (static_cast<size_t>(y) * dstW + x) * 4;
            o[0] = s[2];   // RGBA -> BGRA
            o[1] = s[1];
            o[2] = s[0];
            o[3] = s[3];
        }
    }
}

int BuildScaledSheet(const uint8_t* baseBgra, int baseW, int baseH, int scale, int pad,
                     const ScaledSprite* sprites, int count, std::vector<uint8_t>& out) {
    const int w = baseW * scale;
    const int h = baseH * scale;
    out.assign(static_cast<size_t>(w) * h * 4, 0);
    // Upscale first, every sprite; then the art over its own sprites, so an
    // art sprite's ring wins over a neighbour's upscaled ring if they touch.
    for (int i = 0; i < count; ++i)
        UpscaleRect(baseBgra, baseW, out.data(), PadRect(sprites[i].rect, pad, baseW, baseH), scale);
    int drawn = 0;
    for (int i = 0; i < count; ++i) {
        if (sprites[i].art == nullptr) continue;
        PlaceArt(out.data(), w, h, ScaleRect(sprites[i].rect, scale), sprites[i].art, pad * scale);
        ++drawn;
    }
    return drawn;
}

}  // namespace SharpUI
}  // namespace Ext
