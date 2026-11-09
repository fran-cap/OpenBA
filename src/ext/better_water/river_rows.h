#pragma once
// ============================================================================
// src/ext/better_water/river_rows.h -- ext.better_water: the river's rows turn with a smoothed centreline
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERWATER.md section 10.
//
// Retail's river water (phase 1, WorldGeometry::BuildWaterBodies, the 0x6000 ranges) is one strip per body:
// rows of five vertices across the channel (u 0, 0.25 .. 1), each row a cross-section between a point on the
// left bank and one on the right. The banks are resampled between matched centreline nodes independently of
// each other, so where the river turns sharply one row can swing through 40 degrees or more against its
// neighbours: the whole strip kinks on that row and the water shows a straight seam across the channel. The
// outer bank there is a chord between two far-apart row ends, which cuts the corner and leaves wet ground
// uncovered.
//
// The rebuild: the strip's rows are read back from the range's indices (retail's quad order, below). Each
// row's midpoint and half-width are smoothed along the strip (a gaussian over the neighbouring rows, its
// window shrunk symmetrically at the ends so the first and last rows stay where they are and a straight run
// is unchanged). Each row's ends stay on retail's banks: the row slides along each bank to where the line
// across the smoothed centreline, perpendicular to it, meets that bank, the distance along the bank never
// going back (a sharp inner corner holds the rows on the corner and they fan round it) and smoothed like the
// centreline (the fan opens gradually, no row is a crease). The water covers what retail's banks enclose.
// Each end is then pushed out along the row toward the smoothed width, by at most kMaxPush tiles, which
// fills the outside of a bend where retail's bank is a chord across the corner. The middle three vertices
// are spaced evenly between the ends, as retail's are. Vertex count, indices and uvs are unchanged: only positions move.
// A range whose indices are not retail's strip is left as it is. Pure: the caller uploads the copy.
// ============================================================================

#include "Game/WorldGeometryElements.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace BetterWater {
namespace RiverRows {

constexpr float kPosScale = 48.0f;    // water_contour_mesh.cpp: position = tile * 48
constexpr int kColumns = 5;           // water_contour_mesh.cpp Stage C: u 0 -> 1 by 0.25
constexpr int kIndicesPerRow = 24;    // four quads of two triangles
constexpr float kSigmaRows = 2.5f;    // the centreline's smoothing (rows are about a tile apart)
constexpr int kReachRows = 8;         // the smoothing window's half-width, about 3 sigma
constexpr float kMaxPush = 1.5f;      // tiles an end may be pushed past retail's bank

// One river range: GeometryDrawRange::values {first index, base vertex, vertex count, index count}.
struct Range {
    int32_t indexStart;
    int32_t vertexStart;
    int32_t vertexCount;
    int32_t indexCount;
};

struct P2 {
    float x;
    float y;
};

inline P2 Add(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
inline P2 Sub(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
inline P2 Mul(P2 a, float s) { return {a.x * s, a.y * s}; }
inline float Dot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
inline float Cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
inline float Len(P2 a) { return std::sqrt(Dot(a, a)); }

using Row = std::array<int32_t, kColumns>;   // range-local vertex indices, u = 0 .. 1

// The strip's rows, from retail's index order: per row step, four quads (column c, c + 1), each emitted as
// (V0, V2, V1), (V0, V3, V2) with V0 = (row, c), V1 = (row, c + 1), V2 = (row + 1, c + 1), V3 = (row + 1, c).
// False when the indices are not that strip (the range is then left alone).
inline bool ReadRows(const uint16_t* indices, int32_t indexCount, int32_t vertexCount, std::vector<Row>& rows) {
    rows.clear();
    if (indices == nullptr || indexCount < kIndicesPerRow || indexCount % kIndicesPerRow != 0) return false;
    const int32_t steps = indexCount / kIndicesPerRow;
    for (int32_t s = 0; s < steps; ++s) {
        const uint16_t* q = indices + static_cast<size_t>(s) * kIndicesPerRow;
        Row top{};
        Row bottom{};
        for (int c = 0; c < kColumns - 1; ++c) {
            const uint16_t* t = q + c * 6;
            if (t[3] != t[0] || t[5] != t[1]) return false;
            const int32_t v0 = t[0];
            const int32_t v2 = t[1];
            const int32_t v1 = t[2];
            const int32_t v3 = t[4];
            if (c == 0) {
                top[0] = v0;
                bottom[0] = v3;
            } else if (top[c] != v0 || bottom[c] != v3) {
                return false;
            }
            top[c + 1] = v1;
            bottom[c + 1] = v2;
        }
        for (int c = 0; c < kColumns; ++c) {
            if (top[c] >= vertexCount || bottom[c] >= vertexCount) return false;
        }
        if (s == 0) {
            rows.push_back(top);
        } else if (rows.back() != top) {
            return false;
        }
        rows.push_back(bottom);
    }
    return true;
}

// A bank line made to run downstream: retail's row ends on one side, keeping only those that reach further along
// the smoothed centreline than every one before them (a skewed row's end can lie past the next rows' ends, and
// the bank then doubles back on itself). `arc` is the cumulative length, `at[r]` the kept point at or before
// row r's end.
struct Bank {
    std::vector<P2> pts;
    std::vector<float> arc;
    std::vector<int> at;
};

inline Bank CleanBank(const std::vector<P2>& ends, const std::vector<P2>& centre) {
    const int n = static_cast<int>(ends.size());
    Bank bank;
    bank.at.resize(n);
    float along = 0.0f;
    float furthest = 0.0f;
    for (int r = 0; r < n; ++r) {
        if (r > 0) along += Len(Sub(centre[r], centre[r - 1]));
        const int lo = r > 0 ? r - 1 : r;
        const int hi = r < n - 1 ? r + 1 : r;
        const P2 tangent = Sub(centre[hi], centre[lo]);
        const float tl = Len(tangent);
        const float progress = along + (tl > 1e-6f ? Dot(Sub(ends[r], centre[r]), tangent) / tl : 0.0f);
        const bool last = r == n - 1;
        if (r == 0 || progress > furthest || last) {
            if (last && !bank.pts.empty() && progress <= furthest) {
                bank.pts.back() = ends[r];   // the last row's end stays; it replaces the point it does not pass
                bank.arc.back() = bank.arc.size() > 1
                                      ? bank.arc[bank.arc.size() - 2] + Len(Sub(ends[r], bank.pts[bank.pts.size() - 2]))
                                      : 0.0f;
            } else {
                const float prev = bank.pts.empty() ? 0.0f : bank.arc.back() + Len(Sub(ends[r], bank.pts.back()));
                bank.pts.push_back(ends[r]);
                bank.arc.push_back(prev);
            }
            if (progress > furthest || r == 0) furthest = progress;
        }
        bank.at[r] = static_cast<int>(bank.pts.size()) - 1;
    }
    return bank;
}

// Where the ray from `origin` in `dir` (unit) first crosses the bank between rows `first` and `last`: the
// distance along the ray (negative: no crossing) and along the bank.
struct BankHit {
    float distance;
    float arc;
};

inline BankHit RayToBank(P2 origin, P2 dir, const Bank& bank, int first, int last) {
    BankHit best{-1.0f, 0.0f};
    const int lo = bank.at[first];
    int hi = bank.at[last];
    const int top = static_cast<int>(bank.pts.size()) - 1;
    if (hi <= lo) hi = lo + 1;
    if (hi > top) hi = top;
    for (int k = lo; k < hi; ++k) {
        const P2 a = bank.pts[k];
        const P2 e = Sub(bank.pts[k + 1], a);
        const float denom = Cross(dir, e);
        if (std::fabs(denom) < 1e-6f) continue;
        const P2 ao = Sub(a, origin);
        const float t = Cross(ao, e) / denom;
        const float u = Cross(ao, dir) / denom;
        if (t <= 0.0f || u < 0.0f || u > 1.0f) continue;
        if (best.distance < 0.0f || t < best.distance) best = {t, bank.arc[k] + u * (bank.arc[k + 1] - bank.arc[k])};
    }
    return best;
}

// The point at distance `at` along the bank.
inline P2 AtArc(const Bank& bank, float at) {
    const int n = static_cast<int>(bank.pts.size());
    if (n == 1 || at <= bank.arc[0]) return bank.pts[0];
    for (int k = 0; k + 1 < n; ++k) {
        if (at > bank.arc[k + 1]) continue;
        const float len = bank.arc[k + 1] - bank.arc[k];
        const float u = len > 1e-6f ? (at - bank.arc[k]) / len : 0.0f;
        return Add(bank.pts[k], Mul(Sub(bank.pts[k + 1], bank.pts[k]), u));
    }
    return bank.pts[n - 1];
}

// Never decreasing, with the first and last values kept: forward, each at least the one before; then backward,
// each at most the one after.
inline void Advance(std::vector<float>& at) {
    const int n = static_cast<int>(at.size());
    for (int r = 1; r < n; ++r) {
        if (at[r] < at[r - 1]) at[r] = at[r - 1];
    }
    for (int r = n - 2; r >= 0; --r) {
        if (at[r] > at[r + 1]) at[r] = at[r + 1];
    }
}

// The centreline's gaussian (symmetric window, the ends kept), then Advance again for the shrunk windows.
inline void Smooth(std::vector<float>& at) {
    const int n = static_cast<int>(at.size());
    std::vector<float> out(at);
    for (int r = 1; r < n - 1; ++r) {
        int reach = kReachRows;
        if (reach > r) reach = r;
        if (reach > n - 1 - r) reach = n - 1 - r;
        float sum = 0.0f;
        float total = 0.0f;
        for (int k = -reach; k <= reach; ++k) {
            const float g = std::exp(-static_cast<float>(k * k) / (2.0f * kSigmaRows * kSigmaRows));
            sum += at[r + k] * g;
            total += g;
        }
        out[r] = sum / total;
    }
    at.swap(out);
    Advance(at);
}

// Lays the rows of one strip again (the header). `pos` holds the range's vertex positions in tiles and is
// rewritten in place for the vertices that move; `moved` marks them. Returns the number moved.
inline int RelayRows(const std::vector<Row>& rows, std::vector<P2>& pos, std::vector<uint8_t>& moved) {
    const int n = static_cast<int>(rows.size());
    if (n < 3) return 0;
    std::vector<P2> left(n);
    std::vector<P2> right(n);
    std::vector<P2> mid(n);
    std::vector<float> half(n);
    for (int r = 0; r < n; ++r) {
        left[r] = pos[rows[r][0]];
        right[r] = pos[rows[r][kColumns - 1]];
        mid[r] = Mul(Add(left[r], right[r]), 0.5f);
        half[r] = 0.5f * Len(Sub(right[r], left[r]));
    }

    // The smoothed centreline and half-width: a symmetric window, so the ends stay put and a line stays a line.
    std::vector<P2> centre(n);
    std::vector<float> width(n);
    for (int r = 0; r < n; ++r) {
        int reach = kReachRows;
        if (reach > r) reach = r;
        if (reach > n - 1 - r) reach = n - 1 - r;
        P2 c{0.0f, 0.0f};
        float w = 0.0f;
        float total = 0.0f;
        for (int k = -reach; k <= reach; ++k) {
            const float g = std::exp(-static_cast<float>(k * k) / (2.0f * kSigmaRows * kSigmaRows));
            c = Add(c, Mul(mid[r + k], g));
            w += half[r + k] * g;
            total += g;
        }
        centre[r] = Mul(c, 1.0f / total);
        width[r] = w / total;
    }

    // Where each row meets retail's banks, as a distance along each bank: the crossing of the line across the
    // smoothed centreline, perpendicular to it (a crossing more than twice the width away is another stretch of
    // the bank; no crossing keeps retail's end). Then made to advance (a sharp inner corner holds its rows on the
    // corner, fanning round it) and smoothed like the centreline, so the fan opens gradually.
    const Bank bankL = CleanBank(left, centre);
    const Bank bankR = CleanBank(right, centre);
    std::vector<float> atL(n);
    std::vector<float> atR(n);
    for (int r = 0; r < n; ++r) {
        atL[r] = bankL.arc[bankL.at[r]];
        atR[r] = bankR.arc[bankR.at[r]];
    }
    for (int r = 1; r < n - 1; ++r) {
        const P2 tangent = Sub(centre[r + 1], centre[r - 1]);
        const float tl = Len(tangent);
        const P2 across = Sub(left[r], right[r]);
        if (tl < 1e-4f || width[r] < 1e-3f || Len(across) < 1e-3f) continue;
        P2 nrm{-tangent.y / tl, tangent.x / tl};
        if (Dot(nrm, across) < 0.0f) nrm = Mul(nrm, -1.0f);
        int first = r - kReachRows - 1;
        int last = r + kReachRows + 1;
        if (first < 0) first = 0;
        if (last > n - 1) last = n - 1;
        const BankHit hitL = RayToBank(centre[r], nrm, bankL, first, last);
        const BankHit hitR = RayToBank(centre[r], Mul(nrm, -1.0f), bankR, first, last);
        if (hitL.distance > 0.0f && hitL.distance < 2.0f * width[r]) atL[r] = hitL.arc;
        if (hitR.distance > 0.0f && hitR.distance < 2.0f * width[r]) atR[r] = hitR.arc;
    }
    Advance(atL);
    Advance(atR);
    Smooth(atL);
    Smooth(atR);

    // The row's ends on retail's banks, each then pushed out along the row toward the smoothed width, by at most
    // kMaxPush past the bank (the outside of a bend, where retail's bank is a chord across the corner). A push
    // that would put an end behind the previous row's is dropped.
    std::vector<P2> endL(n);
    std::vector<P2> endR(n);
    endL[0] = left[0];
    endR[0] = right[0];
    endL[n - 1] = left[n - 1];
    endR[n - 1] = right[n - 1];
    for (int r = 1; r < n - 1; ++r) {
        const P2 a = AtArc(bankL, atL[r]);
        const P2 b = AtArc(bankR, atR[r]);
        const P2 m = Mul(Add(a, b), 0.5f);
        const float halfLen = 0.5f * Len(Sub(a, b));
        if (halfLen < 1e-3f) {
            endL[r] = a;
            endR[r] = b;
            continue;
        }
        const P2 dir = Mul(Sub(a, b), 0.5f / halfLen);
        float push = width[r] - halfLen;
        if (push < 0.0f) push = 0.0f;
        if (push > kMaxPush) push = kMaxPush;
        const P2 pushedL = Add(a, Mul(dir, push));
        const P2 pushedR = Sub(b, Mul(dir, push));
        const P2 flow = Sub(m, Mul(Add(endL[r - 1], endR[r - 1]), 0.5f));
        endL[r] = Dot(Sub(pushedL, endL[r - 1]), flow) > 0.0f ? pushedL : a;
        endR[r] = Dot(Sub(pushedR, endR[r - 1]), flow) > 0.0f ? pushedR : b;
    }

    int count = 0;
    for (int r = 1; r < n - 1; ++r) {
        const P2 a = endL[r];
        const P2 b = endR[r];
        for (int c = 0; c < kColumns; ++c) {
            const int32_t v = rows[r][c];
            if (moved[v]) continue;   // a welded vertex two rows share keeps its first row's place
            const float t = static_cast<float>(c) / static_cast<float>(kColumns - 1);
            pos[v] = Add(a, Mul(Sub(b, a), t));
            moved[v] = 1;
            ++count;
        }
    }
    return count;
}

inline int16_t Pack(float tiles) {
    float v = std::round(tiles * kPosScale);
    if (v < -32768.0f) v = -32768.0f;
    if (v > 32767.0f) v = 32767.0f;
    return static_cast<int16_t>(v);
}

// `out` (a copy of the water mesh, already sized to it) with every river range's rows laid again. Returns the
// number of vertices whose position changed; `skipped` counts the ranges that were not retail's strip.
inline int Relay(std::vector<TerrainVertex>& out, const uint16_t* indices, int32_t indexTotal, const Range* ranges,
                 int32_t rangeCount, int* skipped) {
    int changed = 0;
    std::vector<Row> rows;
    std::vector<P2> pos;
    std::vector<uint8_t> moved;
    const int32_t count = static_cast<int32_t>(out.size());
    for (int32_t ri = 0; ri < rangeCount; ++ri) {
        const Range& rg = ranges[ri];
        if (rg.vertexCount <= 0 || rg.indexCount <= 0) continue;
        if (rg.vertexStart < 0 || rg.vertexStart > count - rg.vertexCount || rg.indexStart < 0 ||
            rg.indexStart > indexTotal - rg.indexCount ||
            !ReadRows(indices + rg.indexStart, rg.indexCount, rg.vertexCount, rows)) {
            if (skipped != nullptr) ++*skipped;
            continue;
        }
        TerrainVertex* v = out.data() + rg.vertexStart;
        pos.resize(static_cast<size_t>(rg.vertexCount));
        moved.assign(static_cast<size_t>(rg.vertexCount), 0);
        for (int32_t i = 0; i < rg.vertexCount; ++i) {
            pos[i] = {static_cast<float>(v[i].position[0]) / kPosScale, static_cast<float>(v[i].position[1]) / kPosScale};
        }
        if (RelayRows(rows, pos, moved) == 0) continue;
        for (int32_t i = 0; i < rg.vertexCount; ++i) {
            if (!moved[i]) continue;
            const int16_t x = Pack(pos[i].x);
            const int16_t y = Pack(pos[i].y);
            if (x == v[i].position[0] && y == v[i].position[1]) continue;
            v[i].position[0] = x;
            v[i].position[1] = y;
            ++changed;
        }
    }
    return changed;
}

}  // namespace RiverRows
}  // namespace BetterWater
}  // namespace Ext
