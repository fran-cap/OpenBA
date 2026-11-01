// ============================================================================
// src/render/common/better_trees/blossom.cpp -- blossom.h
// ============================================================================
// NOT RETAIL (ext.better_trees' natural tree growth, docs/extensions/BLOSSOM.md). Standard C++ only: the
// unit tests link it.
// ============================================================================
#include "better_trees/blossom.h"

#include <cmath>

namespace Blossom {
namespace {

float Frc(float v) { return v - std::floor(v); }
float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The table (BLOSSOM.md §2; peaks in months from the start of Early Spring, where leaf regrowth starts at
// about 0.75). Matched by substring of the draw's names, in this order.
const Species kTable[] = {
    // name       colour              density  peak  flags
    {"plum",     {0xF4, 0xF2, 0xEC}, 0.85f,   0.40f, 0},           // white, first, on bare wood
    {"cherry",   {0xF6, 0xE1, 0xE6}, 0.85f,   0.60f, kPinkBuds},   // white to light pink
    {"peach",    {0xE5, 0x8A, 0xA8}, 0.70f,   0.60f, 0},           // pink to magenta
    {"pear",     {0xF2, 0xF1, 0xEA}, 0.80f,   0.70f, 0},           // white
    {"apple",    {0xF5, 0xE4, 0xE6}, 0.50f,   1.10f, kPinkBuds},   // pink buds open white, among leaves
    // Catkins: muted (about 70 % brightness, less saturated) and sparser after the first look (the user,
    // 2026-10-31: the first pass read as bright yellow sparks on the bare trees).
    {"birch",    {0xA0, 0x8A, 0x55}, 0.25f,   0.90f, kCatkin},     // yellow-brown lamb's tails
    {"oak",      {0xC4, 0xC2, 0x6E}, 0.28f,   1.10f, kCatkin},     // yellow-green tassels among new leaves
    {"pecan",    {0xE2, 0xDC, 0x8E}, 0.30f,   1.70f, kCatkin},     // greenish-yellow; later than real (1.4) so they hang among leaves, not in air
    {"walnut",   {0x7A, 0x86, 0x4A}, 0.0625f, 1.00f, kCatkin},     // green catkins: next to invisible
    {"chestnut", {0xE8, 0xDF, 0xA6}, 0.40f,   3.20f, kCatkin},     // showy cream spikes, early summer
};

// A mod's tree: pale blossom colours (whites, creams, pinks, a pale yellow, a lilac).
const uint8_t kPalette[][3] = {
    {0xF4, 0xF2, 0xEC}, {0xF3, 0xEB, 0xD8}, {0xF6, 0xE1, 0xE6}, {0xEF, 0xC0, 0xCF},
    {0xE5, 0x8A, 0xA8}, {0xF2, 0xEB, 0xB8}, {0xE8, 0xD6, 0xEE}, {0xF7, 0xE3, 0xD4},
};

}  // namespace

uint32_t Fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

bool SpeciesFor(const std::string& materialPath, const std::string& names, Species* out, bool* known) {
    if (known != nullptr) *known = false;
    if (materialPath.find("billboard") == std::string::npos) return false;
    if (materialPath.find("evergreen") != std::string::npos) return false;
    // The retail data swaps two: chestnuttreemesh draws with pecantree.rsc and pecantreemesh with
    // chestnuttree.rsc (showroom tree run, 2026-10-31). The species follows the tree, so a draw with the
    // pecan material is a chestnut and the reverse.
    const char* want = nullptr;
    if (names.find("pecantree") != std::string::npos) want = "chestnut";
    else if (names.find("chestnuttree") != std::string::npos) want = "pecan";
    // "chestnut" and "walnut" before nothing they contain; "pecan" and "peach" share no prefix with "pear".
    for (const Species& s : kTable) {
        if (want != nullptr ? std::string(s.name) != want : names.find(s.name) == std::string::npos) continue;
        *out = s;
        if (known != nullptr) *known = true;
        return true;
    }
    const uint32_t h = Fnv1a(names);
    const uint8_t* c = kPalette[h & 7u];
    out->name     = "hashed";
    out->rgb[0]   = c[0];
    out->rgb[1]   = c[1];
    out->rgb[2]   = c[2];
    out->density  = 0.45f + 0.30f * static_cast<float>((h >> 3) & 15u) / 15.0f;
    out->peak     = 0.50f + 0.60f * static_cast<float>((h >> 7) & 15u) / 15.0f;
    out->flags    = ((h >> 11) % 3u) == 0 ? kPinkBuds : 0u;
    return true;
}

void Pack(const Species& s, float* z, float* w) {
    int q = static_cast<int>(std::lround(s.density * 16.0f)) - 1;
    if (q < 0) q = 0;
    if (q > 15) q = 15;
    float peak = s.peak;
    if (peak < 0.0f) peak = 0.0f;
    if (peak > 11.99f) peak = 11.99f;
    *z = 1.0f + peak + 16.0f * static_cast<float>(s.flags & 3u) + 64.0f * static_cast<float>(q);
    *w = static_cast<float>(s.rgb[0]) * 65536.0f + static_cast<float>(s.rgb[1]) * 256.0f + static_cast<float>(s.rgb[2]);
}

void Hash33(float x, float y, float out[3]) {
    float a[3] = {Frc(x * NgRewrite::kHashK[0]), Frc(y * NgRewrite::kHashK[1]), Frc(x * NgRewrite::kHashK[2])};
    const float d = a[0] * (a[1] + NgRewrite::kHashAdd) + a[1] * (a[0] + NgRewrite::kHashAdd) +
                    a[2] * (a[2] + NgRewrite::kHashAdd);
    for (float& v : a) v += d;
    out[0] = Frc((a[0] + a[1]) * a[2]);
    out[1] = Frc((a[0] + a[2]) * a[1]);
    out[2] = Frc((a[1] + a[2]) * a[0]);
}

void VertexOut(const NgRewrite::TreeTerms& t, float months, bool stamped, float z, float w, float out[4]) {
    // decode z (the shader's order)
    const float q    = std::floor(z * (1.0f / 64.0f));
    const float rem  = q * -64.0f + z;
    const float s    = std::floor(rem * (1.0f / 16.0f));
    const float pk1  = s * -16.0f + rem;   // peak + 1
    const bool valid = pk1 >= 0.5f && stamped;
    // the tree's offset and its distance from the peak, wrapped to [-6, 6)
    float j = Frc(t.h[0] + t.h[1]) * (2.0f * kJitter) - kJitter;
    j = t.grove * kGroveShift + j;
    float x = months - pk1;
    x = x - j;
    x = Frc(x * (1.0f / 12.0f) + 7.0f / 12.0f) * 12.0f - 6.0f;
    float b = Sat(std::fabs(x) * (-kPlateau / kHalfWidth) + kPlateau);
    if (!valid) b = 0.0f;
    const float open = Sat(x * (1.0f / kHalfWidth) + 1.0f);
    const float dens = q * (1.0f / 16.0f) + 1.0f / 16.0f;
    float cov = b * dens;
    const float half = s * 0.5f;
    const float catk = Frc(half);
    const float pink = std::floor(half);
    cov *= catk * -4.0f + 1.0f;
    const float amt = (1.0f - open) * pink;
    // decode w
    const float r    = std::floor(w * (1.0f / 65536.0f));
    const float rest = r * -65536.0f + w;
    const float g    = std::floor(rest * (1.0f / 256.0f));
    const float bl   = g * -256.0f + rest;
    out[0] = r * (amt * kBudTint[0] + 1.0f) * (1.0f / 255.0f);
    out[1] = g * (amt * kBudTint[1] + 1.0f) * (1.0f / 255.0f);
    out[2] = bl * (amt * kBudTint[2] + 1.0f) * (1.0f / 255.0f);
    out[3] = cov;
}

namespace {

// One layer's blob at (u, v): its mask and its brightness (EmitBloomLayer's arithmetic).
void LayerBlob(int layer, float u, float v, const float n2[4], float* e, float* bright) {
    const Layer& L = kLayers[layer];
    const float qx = u * L.row0[0] + v * L.row0[1] + L.offset[0];
    const float qy = u * L.row1[0] + v * L.row1[1] + L.offset[1];
    float h[3];
    Hash33(std::floor(qx), std::floor(qy), h);
    float fx = Frc(qx) + (h[1] * -kCentreSpan - kCentreLo);
    float fy = Frc(qy) + (h[2] * -kCentreSpan - kCentreLo);
    float sx = Frc(h[0] * 7.0f + h[1]) * kAspect1 + kAspect0;
    float sy = -sx + 2.0f;
    if (n2[3] < 0.0f) {
        sy = h[1] * kCatkinYVar + kCatkinY;
        sx = kCatkinX;
    }
    fx *= sx;
    fy *= sy;
    const float d2 = fx * fx + fy * fy;
    float r = h[2] * kRadius1 + kRadius0;
    r = r * r;
    const float soft = Sat((r - d2) * kEdge);
    const float fade = Sat((std::fabs(n2[3]) * kShare - h[0]) * kFade);
    *e = soft * fade;
    *bright = h[1] * kBright1 + kBright0;
}

}  // namespace

PixelTerms Pixel(float u, float v, const float n2[4], float l, float move, float aa) {
    float e0, b0, e1, b1;
    LayerBlob(0, u, v, n2, &e0, &b0);
    LayerBlob(1, u, v, n2, &e1, &b1);
    const float best = e0 < e1 ? e1 : e0;
    const float bright = e0 < e1 ? b1 : b0;
    const float cov = std::fabs(n2[3]);
    const float avg = cov * kArea;
    const float f = aa * (avg - best) + best;
    const float kept = Sat(f * kKeep);
    PixelTerms p;
    p.mask    = f;
    p.lossCut = -kept * l + l;
    p.amount  = (l * (kept - f) + f) * move;
    p.bright  = bright;
    return p;
}

}  // namespace Blossom
