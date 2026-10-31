// ============================================================================
// src/ext/better_trees/leaf_fall.cpp -- natural tree growth's falling leaves, the pure part
// (docs/extensions/NATURAL_GROWTH.md §7). NOT RETAIL; the header has the design.
// ============================================================================
#include "ext/better_trees/leaf_fall.h"

#include "better_trees/natural_growth.h"   // NgRewrite::TermsOf / kCurves: the canopy's own terms
#include "ext/better_trees/leaf_textures_data.h"

#include <cmath>

namespace Ext {
namespace BetterTrees {
namespace LeafFall {

namespace {

float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The leaf colours, straight RGB 0..1, multiplied into the photograph's luminance (mean 0.8): the canopy
// palette's gold / orange / red and the browning's brown. AlphaTestMaterial lights them as the herbs.
constexpr float kRow[kColours][3] = {
    {0.90f, 0.66f, 0.22f},
    {0.90f, 0.45f, 0.13f},
    {0.76f, 0.20f, 0.09f},
    {0.52f, 0.34f, 0.17f},
};

}  // namespace

std::vector<uint8_t> PaintAtlas() {
    static_assert(kLeafCell == kCell, "the baked leaves are one atlas cell each");
    std::vector<uint8_t> px(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 0);
    for (int row = 0; row < kColours; ++row) {
        for (int col = 0; col < kShapes; ++col) {
            // the three photographs, then the first one mirrored
            const int photo = col < kLeafPhotos ? col : 0;
            const bool mirror = col >= kLeafPhotos;
            for (int y = 0; y < kCell; ++y) {
                for (int x = 0; x < kCell; ++x) {
                    const int sx = mirror ? kCell - 1 - x : x;
                    const float lum = kLeafLum[photo][y * kCell + sx] / 255.0f;
                    const size_t at = (static_cast<size_t>(row * kCell + y) * kAtlasSize + col * kCell + x) * 4;
                    for (int ch = 0; ch < 3; ++ch) {
                        px[at + ch] = static_cast<uint8_t>(std::lround(Sat(kRow[row][ch] * lum) * 255.0f));
                    }
                    px[at + 3] = kLeafAlpha[photo][y * kCell + sx];
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

void BuildQuads(const std::vector<Leaf>& leaves, const float right[3], const float up[3], const float facing[3],
                const float origin[3], std::vector<Vertex>& out) {
    out.reserve(out.size() + leaves.size() * 4);
    const float half = kLeafSize * 0.5f;
    uint8_t normal[4];
    for (int k = 0; k < 3; ++k) normal[k] = static_cast<uint8_t>(std::lround(Sat(facing[k] * 0.5f + 0.5f) * 255.0f));
    normal[3] = 0;
    for (const Leaf& l : leaves) {
        float rel[3];
        bool inside = true;
        for (int k = 0; k < 3; ++k) {
            rel[k] = l.pos[k] - origin[k];
            if (std::fabs(rel[k]) > kReach) inside = false;
        }
        if (!inside) continue;
        const float c = std::cos(l.spin);
        const float s = std::sin(l.spin);
        const float tumble = 0.2f + 0.8f * std::fabs(std::cos(l.tumbleRate * l.phase));
        float a[3], b[3];
        for (int k = 0; k < 3; ++k) {
            a[k] = (c * right[k] + s * up[k]) * half;
            b[k] = (-s * right[k] + c * up[k]) * half * tumble;
        }
        // the cell, in texels * 512 / kAtlasSize (int16 / 512 = uv)
        const int u0 = (l.shape % kShapes) * kCell * 512 / kAtlasSize;
        const int v0 = (l.colour % kColours) * kCell * 512 / kAtlasSize;
        const int u1 = u0 + kCell * 512 / kAtlasSize;
        const int v1 = v0 + kCell * 512 / kAtlasSize;
        const float sa[4] = {1.0f, 1.0f, -1.0f, -1.0f};
        const float sb[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const int uu[4] = {u1, u1, u0, u0};
        const int vv[4] = {v1, v0, v0, v1};
        for (int q = 0; q < 4; ++q) {
            Vertex v;
            for (int k = 0; k < 3; ++k) {
                v.position[k] = static_cast<int16_t>(std::lround((rel[k] + sa[q] * a[k] + sb[q] * b[k]) * 512.0f));
            }
            v.position[3] = 0;
            v.texcoord[0] = static_cast<int16_t>(uu[q]);
            v.texcoord[1] = static_cast<int16_t>(vv[q]);
            v.texcoord[2] = 4096;   // the ambient lookup's middle texel (8192 = 1.0)
            v.texcoord[3] = 4096;
            for (int k = 0; k < 4; ++k) v.normal[k] = normal[k];
            out.push_back(v);
        }
    }
}

}  // namespace LeafFall
}  // namespace BetterTrees
}  // namespace Ext
