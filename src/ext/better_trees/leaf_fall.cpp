// ============================================================================
// src/ext/better_trees/leaf_fall.cpp -- natural tree growth's falling leaves, the pure part
// (docs/extensions/NATURAL_GROWTH.md §7). NOT RETAIL; the header has the design.
// ============================================================================
#include "ext/better_trees/leaf_fall.h"

#include "better_trees/natural_growth.h"   // NgRewrite::TermsOf / kCurves: the canopy's own terms

#include <cmath>

namespace Ext {
namespace BetterTrees {
namespace LeafFall {

namespace {

float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The leaf colours, straight RGB 0..1: the canopy palette's gold / orange / red and the browning's brown,
// deeper than the canopy's (the weather program is unlit: a leaf in the canopy's shade shows these as is).
constexpr float kRow[kColours][3] = {
    {0.68f, 0.52f, 0.17f},
    {0.70f, 0.35f, 0.10f},
    {0.58f, 0.15f, 0.07f},
    {0.42f, 0.27f, 0.13f},
};

// Inside test of leaf shape `s` at (u, v), both in [-1, 1], the stem along -v: the half width at height v.
float HalfWidth(int s, float v) {
    const float t = (v + 1.0f) * 0.5f;   // 0 at the stem, 1 at the tip
    switch (s) {
    case 0: return 0.62f * std::sin(3.14159265f * std::pow(t, 0.8f));                       // oval
    case 1: return 0.36f * std::sin(3.14159265f * std::pow(t, 0.6f));                       // lance
    case 2: {                                                                                // lobed
        const float body = 0.55f * std::sin(3.14159265f * std::pow(t, 0.7f));
        return body * (0.78f + 0.22f * std::cos(3.0f * 3.14159265f * t));
    }
    default: return 0.70f * std::sqrt(Sat(1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f)));   // round
    }
}

}  // namespace

std::vector<uint8_t> PaintAtlas() {
    std::vector<uint8_t> px(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 0);
    constexpr int kSub = 4;   // 4 x 4 samples per texel for the edge
    for (int row = 0; row < kColours; ++row) {
        for (int col = 0; col < kShapes; ++col) {
            for (int y = 0; y < kCell; ++y) {
                for (int x = 0; x < kCell; ++x) {
                    int inside = 0;
                    float rib = 0.0f;
                    float shade = 0.0f;
                    for (int sy = 0; sy < kSub; ++sy) {
                        for (int sx = 0; sx < kSub; ++sx) {
                            // a 3-texel margin: the 1/255 corner steps and bilinear filtering stay in the cell
                            const float u = ((x + (sx + 0.5f) / kSub) - kCell * 0.5f) / (kCell * 0.5f - 3.0f);
                            const float v = -(((y + (sy + 0.5f) / kSub) - kCell * 0.5f) / (kCell * 0.5f - 3.0f));
                            const float w = HalfWidth(col, v);
                            const bool blade = v > -1.0f && v < 1.0f && std::fabs(u) < w;
                            const bool stem = v <= -0.80f && v > -1.0f && std::fabs(u) < 0.06f;
                            if (!blade && !stem) continue;
                            ++inside;
                            if (std::fabs(u) < 0.05f) rib += 1.0f;
                            shade += 0.5f + 0.5f * (w > 0.0f ? std::fabs(u) / w : 0.0f);
                        }
                    }
                    const size_t at = (static_cast<size_t>(row * kCell + y) * kAtlasSize + col * kCell + x) * 4;
                    const float* c = kRow[row];
                    // the colour everywhere (no dark fringe under bilinear filtering), the leaf in the alpha
                    float k = 1.0f;
                    if (inside > 0) k = 1.0f - 0.18f * (shade / inside - 0.5f) - 0.25f * (rib / inside);
                    for (int ch = 0; ch < 3; ++ch) {
                        px[at + ch] = static_cast<uint8_t>(std::lround(Sat(c[ch] * k) * 255.0f));
                    }
                    px[at + 3] = static_cast<uint8_t>(std::lround(255.0f * inside / (kSub * kSub)));
                }
            }
        }
    }
    return px;
}

float AutumnLoss(float x, float y, float months) {
    const NgRewrite::TreeTerms t = NgRewrite::TermsOf(x, y);
    const NgRewrite::Curves& c = NgRewrite::kCurves;
    const float o = c.autumnSpread * (1.2f * t.h[0] - 0.6f + 0.2f * t.grove);
    return Sat((months - o - c.fallStart) / c.fallLength);
}

float DropRate(float loss) {
    if (loss <= 0.0f || loss >= 1.0f) return 0.0f;
    return 0.1f + 1.6f * loss * (1.0f - loss);   // a slow start and end, 0.5 per second at the peak
}

int ColourOf(float x, float y) {
    const float hue = NgRewrite::TermsOf(x, y).h[1];
    if (hue < 0.31f) return 0;
    if (hue < 0.875f) return 1;
    return 2;
}

float Field::Rand() {
    _seed ^= _seed << 13;
    _seed ^= _seed >> 17;
    _seed ^= _seed << 5;
    return static_cast<float>(_seed & 0xFFFFFFu) / 16777216.0f;
}

void Field::Step(float dt, float months, const std::vector<Tree>& trees) {
    if (dt <= 0.0f) return;
    // move, and drop the leaves that reached the ground
    size_t keep = 0;
    for (size_t i = 0; i < _leaves.size(); ++i) {
        Leaf l = _leaves[i];
        l.phase += dt;
        l.pos[0] += (l.drift[0] + l.flutterAmp * std::cos(l.flutterRate * l.phase)) * dt;
        l.pos[1] += (l.drift[1] + 0.5f * l.flutterAmp * std::sin(0.7f * l.flutterRate * l.phase)) * dt;
        l.pos[2] -= l.fall * (0.75f + 0.5f * std::fabs(std::sin(l.flutterRate * l.phase))) * dt;
        l.spin += l.spinRate * dt;
        if (l.pos[2] > l.floorZ) _leaves[keep++] = l;
    }
    _leaves.resize(keep);
    // drop new ones, each tree at its own rate: one leaf with probability rate * dt (the set of trees near
    // the camera changes from frame to frame, so nothing is carried per tree)
    // (from a random tree onward: when the cap is reached no part of the view is favoured)
    const size_t n = trees.size();
    const size_t first = n > 0 ? static_cast<size_t>(Rand() * static_cast<float>(n)) % n : 0;
    for (size_t k = 0; k < n; ++k) {
        const Tree& t = trees[(first + k) % n];
        if (_leaves.size() >= static_cast<size_t>(kMaxLeaves)) break;
        if (Rand() >= DropRate(AutumnLoss(t.x, t.y, months)) * dt) continue;
        const int colour = ColourOf(t.x, t.y);
        {
            Leaf l;
            // from the crown's outer edge and its lower part, where a leaf is not hidden by the canopy
            const float h = t.crown[5] - t.crown[2];
            const float angle = 6.2831853f * Rand();
            const float reach = 0.35f + 0.15f * Rand();
            l.pos[0] = t.x + std::cos(angle) * reach * (t.crown[3] - t.crown[0]);
            l.pos[1] = t.y + std::sin(angle) * reach * (t.crown[4] - t.crown[1]);
            l.pos[2] = t.crown[2] + h * (0.35f + 0.35f * Rand());
            l.fall = 0.7f + 0.5f * Rand();
            l.drift[0] = 0.15f + 0.2f * Rand();
            l.drift[1] = 0.05f + 0.1f * Rand();
            l.flutterAmp = 0.3f + 0.4f * Rand();
            l.flutterRate = 2.0f + 2.5f * Rand();
            l.phase = 6.2831853f * Rand();
            l.spin = 6.2831853f * Rand();
            l.spinRate = (Rand() - 0.5f) * 4.0f;
            l.tumbleRate = 2.0f + 3.0f * Rand();
            l.floorZ = t.baseZ;
            l.shape = static_cast<uint8_t>(Rand() * kShapes) % kShapes;
            l.colour = static_cast<uint8_t>(Rand() < 0.25f ? 3 : colour);
            _leaves.push_back(l);
        }
    }
}

void BuildQuads(const std::vector<Leaf>& leaves, const float right[3], const float up[3], std::vector<Vertex>& out) {
    out.reserve(out.size() + leaves.size() * 4);
    const float half = kLeafSize * 0.5f;
    for (const Leaf& l : leaves) {
        const float c = std::cos(l.spin);
        const float s = std::sin(l.spin);
        const float tumble = 0.2f + 0.8f * std::fabs(std::cos(l.tumbleRate * l.phase));
        float a[3], b[3];
        for (int k = 0; k < 3; ++k) {
            a[k] = (c * right[k] + s * up[k]) * half;
            b[k] = (-s * right[k] + c * up[k]) * half * tumble;
        }
        const uint32_t u0 = 64u * (l.shape % kShapes);
        const uint32_t v0 = 64u * (l.colour % kColours);
        const uint32_t u1 = u0 + 63u;
        const uint32_t v1 = v0 + 63u;
        // the corners in WeatherParticleGeometry's order: (+a -b), (+a +b), (-a +b), (-a -b)
        const float sa[4] = {1.0f, 1.0f, -1.0f, -1.0f};
        const float sb[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const uint32_t uv[4] = {u1 | (v1 << 8), u1 | (v0 << 8), u0 | (v0 << 8), u0 | (v1 << 8)};
        for (int q = 0; q < 4; ++q) {
            Vertex v;
            for (int k = 0; k < 3; ++k) v.position[k] = l.pos[k] + sa[q] * a[k] + sb[q] * b[k];
            v.position[3] = 0.0f;
            v.corner = uv[q];
            out.push_back(v);
        }
    }
}

}  // namespace LeafFall
}  // namespace BetterTrees
}  // namespace Ext
