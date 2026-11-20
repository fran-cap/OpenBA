#pragma once
// ============================================================================
// src/game/Component/charactergrid_nearest.h -- BESTTARGET (perf, byte-identical)
//
// The walk of CharacterGrid::QueryNearestCharacter 0x140092260 (retail body:
// CharacterGrid::QueryNearestCharacterRetail, action_pf_roots_misc.cpp), with the same
// result, the same visit order and the same float expressions, at a lower cost:
//
//  * No CharacterGridWalker. Retail's walker::Init 0x140018dd0 zeroes all 256 nodes
//    (6 KB of stores on a stack local) before pushing the root; every node the query ever
//    reads was written by a push first, so those stores are dead. Here a small stack of
//    nodes is used and only pushes write it.
//  * Children are tested when they are made, not when they are popped. Retail's
//    Subdivide 0x140018880 pushes all four children (cell 4i+1..4i+4) and the query pops
//    and drops the ones whose subtree is empty (cellOccupancy <= 0) or whose loose box
//    misses the query box. The drop has no side effect and nothing the test reads changes
//    during a query (the query writes only *best and locals), so testing at push time
//    keeps exactly the nodes retail processes, in the same LIFO order (4, 3, 2, 1 among
//    the survivors).
//  * The candidates of a cell are gathered (slot array only) in retail's order, a chunk
//    at a time, and their CharacterComponent / ModelComponent lines are prefetched before
//    the chunk is scored in that same order. The scoring is retail's: skip *exclude,
//    the optional exact-type filter, dsq = Vector3::Dot(diff, diff), keep it on a strict
//    `dsq < best` (the first of equal distances wins).
//
// The policy P supplies the character type and three reads:
//   P::Char                                  the character type
//   static Char* Get(const Slot&)            the slot's character
//   static const Math::Vector3& Pos(Char*)   the character's model translation
//   static bool Keep(Char*, int typeId)      the type filter (typeId != 0 only)
//   static void Prefetch(Char*)              a hint (no load that can fault)
//   static void PrefetchPos(Char*)           may load Char's model pointer (typeId == 0,
//                                            non-excluded candidates only: the reads retail
//                                            itself makes)
// Slot is any record with int32 `prev` (CharacterGridSlot; the unit test's own record).
//
// Returns false (and writes nothing) only when `levels` is deeper than the node stack can
// hold; the caller then runs the retail body. The game grid has levels == 8 (22 nodes at
// most on the stack).
// ============================================================================
#include <cmath>
#include <cstdint>
#include <cstring>
#include "math/Math.h"   // Math::Vector3 / Scalar / Dot: the retail dsq expression

namespace CharGridNearest {

struct Node {
    float cx, cy, cz;   // walker node center[0..2]
    float ex, ey, ez;   // walker node extent[0..2]
    int32_t cell;
    int32_t depth;
};

// Node stack capacity. A pop pushes at most four, so the stack holds at most
// 1 + 3 * (levels - 1) nodes.
constexpr int kStack = 96;
constexpr int kMaxLevels = (kStack - 1) / 3;   // 31
constexpr int kChunk = 16;

// 0x140092260's node box against the query box, every float operation as retail writes
// it: ext = (2.0 * e0, 2.0 * e1, g_fUVScale * e2); min = padLo + (center - ext),
// max = padHi + (ext + center), padLo = (0, 0, -1), padHi = (0, 0, 1) (walker::Init's
// lo / hi); then Volume::Box::Intersect (InlBoxIsect).
inline bool NodeHits(const float* q, const Node& n) {
    const float f2 = 2.0f;
    const float fUV = 1.0f;
    const float extx = f2 * n.ex;
    const float exty = f2 * n.ey;
    const float extz = fUV * n.ez;
    const float minx = 0.0f + (n.cx - extx);
    const float miny = 0.0f + (n.cy - exty);
    const float minz = -1.0f + (n.cz - extz);
    const float maxx = 0.0f + (extx + n.cx);
    const float maxy = 0.0f + (exty + n.cy);
    const float maxz = 1.0f + (extz + n.cz);
    return q[0] <= maxx && minx <= q[3] && q[1] <= maxy && miny <= q[4] && q[2] <= maxz && minz <= q[5];
}

// Subdivide 0x140018880's child k (1..4) of `p`, its float operations in its order:
// h = e * 0.5, n = h * -1; child 1 = (cx + nx, cy + ny, cz + ez), 2 = (hx + cx, ny + cy,
// ez + cz), 3 = (nx + cx, hy + cy, ez + cz), 4 = (hx * 1 + cx, hy * 1 + cy, ez * 1 + cz);
// extent (hx, hy, ez), cell 4i + k, depth + 1.
inline void MakeChildren(const Node& p, Node* out) {
    const float hy = p.ey * 0.5f;
    const float hx = p.ex * 0.5f;
    const float ny = hy * -1.0f;
    const float nx = hx * -1.0f;
    const float one = 1.0f;
    const int d = p.depth + 1;
    out[0] = Node{p.cx + nx, p.cy + ny, p.cz + p.ez, hx, hy, p.ez, p.cell * 4 + 1, d};
    out[1] = Node{hx + p.cx, ny + p.cy, p.ez + p.cz, hx, hy, p.ez, p.cell * 4 + 2, d};
    out[2] = Node{nx + p.cx, hy + p.cy, p.ez + p.cz, hx, hy, p.ez, p.cell * 4 + 3, d};
    out[3] = Node{hx * one + p.cx, hy * one + p.cy, p.ez * one + p.cz, hx, hy, p.ez, p.cell * 4 + 4, d};
}

// One cell's list from `head` (>= 0), retail's order and scoring: the slots gathered through
// `prev` a chunk at a time, prefetched, then scored in that order (skip *exclude, the type
// filter, dsq = Dot(diff, diff), keep on a strict `dsq < *fr2`). Query and QueryLeaves both
// score through this one body.
// One candidate, retail's tests in retail's order.
template <class P>
inline void ScoreOne(typename P::Char* c, const float* point, int typeId, typename P::Char* const* exclude,
                     typename P::Char** best, float* fr2) {
    if (c == *exclude) return;
    if (typeId != 0 && !P::Keep(c, typeId)) return;
    const Math::Vector3& pos = P::Pos(c);
    Math::Vector3 diff(pos.x - point[0], pos.y - point[1], pos.z - point[2]);
    const float dsq = diff.Dot(diff).v;
    if (dsq < *fr2) {
        *best = c;
        *fr2 = dsq;
    }
}

template <class P, class Slot>
inline void ScoreCell(int head, const Slot* slots, const float* point, int typeId,
                      typename P::Char* const* exclude, typename P::Char** best, float* fr2) {
    using Char = typename P::Char;
    int ent = head;
    do {
        Char* buf[kChunk];
        int cnt = 0;
        do {
            buf[cnt++] = P::Get(slots[ent]);
            ent = slots[ent].prev;
        } while (ent != head && cnt < kChunk);
        for (int k = 0; k < cnt; ++k) P::Prefetch(buf[k]);
        if (typeId == 0) {
            for (int k = 0; k < cnt; ++k)
                if (buf[k] != *exclude) P::PrefetchPos(buf[k]);
        }
        for (int k = 0; k < cnt; ++k) ScoreOne<P>(buf[k], point, typeId, exclude, best, fr2);
    } while (ent != head);
}

template <class P, class Slot>
bool Query(typename P::Char** best, const float* point, float radius, int typeId,
           typename P::Char* const* exclude, const Slot* slots, const int32_t* cellHead,
           const int32_t* cellOccupancy, float worldSize, int levels) {
    if (levels > kMaxLevels) return false;

    *best = nullptr;
    float fr2 = radius * radius;
    // the query box: min = point - radius, max = radius + point (retail's operand order)
    float q[6];
    q[0] = point[0] - radius; q[1] = point[1] - radius; q[2] = point[2] - radius;
    q[3] = radius + point[0]; q[4] = radius + point[1]; q[5] = radius + point[2];

    Node stack[kStack];
    int top = -1;
    {
        // walker::Init's root: center splat(0), extent (worldSize * 0.5, same, 0), cell 0
        const float half = worldSize * 0.5f;
        const Node root{0.0f, 0.0f, 0.0f, half, half, 0.0f, 0, 0};
        if (0 < cellOccupancy[0] && NodeHits(q, root)) stack[++top] = root;
    }

    while (top >= 0) {
        const Node n = stack[top--];

        const int head = cellHead[n.cell];
        if (-1 < head) ScoreCell<P, Slot>(head, slots, point, typeId, exclude, best, &fr2);

        if (n.depth + 1 < levels) {
            Node kids[4];
            MakeChildren(n, kids);
            for (int k = 0; k < 4; ++k) {
                if (0 < cellOccupancy[kids[k].cell] && NodeHits(q, kids[k])) stack[++top] = kids[k];
            }
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// LEAFWALK (perf, byte-identical): Query's answer without its node walk, for a grid whose
// linked characters all sit in leaf cells (depth levels - 1) and whose occupancy counts are
// the ones Link / Unlink keep (occupancy[c] = the characters linked in c's subtree). The
// caller checks the first (CharacterGrid::AllLinkedAtLeaves); the second holds by
// construction (Link / Unlink are the only writers of cellHead and cellOccupancy).
//
// Why it is the same answer:
//  * Retail visits a leaf when it and every ancestor have occupancy > 0 and every one of
//    their loose boxes hits the query box. A leaf with a list has occupancy >= 1, so do its
//    ancestors; a leaf without one adds nothing. An ancestor's loose box holds its child's
//    (child center = parent center +/- e/2, child half-width = e, parent half-width = 2e: a
//    margin of e/2, never less than 4 units against a float ulp of 2^-12 at 2048), so the
//    leaf's own box test decides. The z bound is the same (-1, +1) for every node: the root's
//    test is its test. Internal nodes hold no list (the precondition), so they add nothing.
//  * Order: Query pushes children 4i+1..4i+4 and pops them 4..1, a pre-order walk with the
//    children in descending index, so the leaves come in descending cell index. Here the hit
//    leaves are sorted the same way and each list is scored by ScoreCell, Query's own body.
//  * Floats: for worldSize = 2^k (LeafWalkExact) every Subdivide center is an odd multiple of
//    the leaf extent e = worldSize / 2^levels with at most 15 significant bits, so the leaf
//    center (2 ix + 1 - 2^D) * e, ext = 2 * e and both bounds are exact: the same bits as the
//    chain of halvings and adds Query computes. The compares are NodeHits' per axis.
// Returns false (and writes nothing) when the grid is not of that shape or a hit range is
// wider than kLeafAxis cells (a radius above a cell); the caller then runs Query.
// ----------------------------------------------------------------------------
constexpr int kLeafMaxLevels = 15;   // 4^14 leaves: every cell index fits an int32
constexpr int kLeafAxis = 3;         // a 0.5 probe hits 2 or 3 leaves of 16 per axis

inline bool LeafWalkExact(float worldSize, int levels) {
    if (levels < 1 || levels > kLeafMaxLevels) return false;
    uint32_t b;
    std::memcpy(&b, &worldSize, sizeof b);
    if ((b & 0x807fffffu) != 0) return false;   // negative, or not a power of two
    const int exp = static_cast<int>((b >> 23) & 0xffu);
    if (exp == 0 || exp == 0xff) return false;
    return exp - levels >= 1;   // the leaf extent is a normal float
}

// NodeHits' compares on one axis for a node at center c, extent e.
inline bool AxisHits(float qlo, float qhi, float c, float e) {
    const float ext = 2.0f * e;
    const float lo = 0.0f + (c - ext);
    const float hi = 0.0f + (ext + c);
    return qlo <= hi && lo <= qhi;
}

// The leaves 0..n-1 on one axis whose box hits [qlo, qhi], ascending, into out[kLeafAxis].
// False when more than kLeafAxis hit, or when a leaf at the edge of the scanned window hits
// (the window is the exact range widened by one each side, so that never happens; the test
// keeps the answer independent of the estimate's arithmetic).
inline bool AxisRange(float qlo, float qhi, float e, int n, int* out, int* count) {
    double dlo = std::ceil((static_cast<double>(qlo) / e + (n - 3)) * 0.5) - 1.0;
    double dhi = std::floor((static_cast<double>(qhi) / e + (n + 1)) * 0.5) + 1.0;
    if (dlo < 0.0) dlo = 0.0;
    if (dlo > n) dlo = n;
    if (dhi > n - 1) dhi = n - 1;
    if (dhi < -1.0) dhi = -1.0;
    *count = 0;
    if (!(dlo <= dhi)) return true;
    const int lo = static_cast<int>(dlo);
    const int hi = static_cast<int>(dhi);
    for (int i = lo; i <= hi; ++i) {
        const float c = static_cast<float>(2 * i + 1 - n) * e;
        if (!AxisHits(qlo, qhi, c, e)) continue;
        if ((i == lo && lo > 0) || (i == hi && hi < n - 1)) return false;
        if (*count == kLeafAxis) return false;
        out[(*count)++] = i;
    }
    return true;
}

// The Morton index of leaf (ix, iy): x in the even bits (child k - 1 = xbit + 2 * ybit).
inline uint32_t LeafMorton(uint32_t ix, uint32_t iy) {
    auto spread = [](uint32_t v) {
        v &= 0xffffu;
        v = (v | (v << 8)) & 0x00ff00ffu;
        v = (v | (v << 4)) & 0x0f0f0f0fu;
        v = (v | (v << 2)) & 0x33333333u;
        v = (v | (v << 1)) & 0x55555555u;
        return v;
    };
    return spread(ix) | (spread(iy) << 1);
}

// LEAFGATHER (perf, byte-identical): the hit leaves' lists are independent chains through
// `prev`, so they are walked side by side (one step of each in turn: their slot loads overlap
// instead of queueing behind one another), each into its own segment of one buffer; then
// every candidate is prefetched and the buffer is scored front to back: cell by cell in
// `heads` order, each list in its `prev` order, which is the order ScoreCell scores them in,
// with ScoreCell's tests. A leaf's occupancy is its list length (Link / Unlink), which sizes
// its segment. Returns false, having written nothing, when the lists need more than kGather
// slots or a list runs past its occupancy; the caller then scores cell by cell.
constexpr int kGather = 256;

template <class P, class Slot>
bool ScoreLeavesInterleaved(const int32_t* heads, const int32_t* lens, int nc, const Slot* slots,
                            const float* point, int typeId, typename P::Char* const* exclude,
                            typename P::Char** best, float* fr2) {
    using Char = typename P::Char;
    constexpr int kCells = kLeafAxis * kLeafAxis;
    int off[kCells];
    int cnt[kCells];
    int ent[kCells];
    int live[kCells];
    int nlive = 0;
    int total = 0;
    for (int k = 0; k < nc; ++k) {
        off[k] = total;
        cnt[k] = 0;
        if (heads[k] < 0) continue;
        if (lens[k] < 1 || lens[k] > kGather - total) return false;
        total += lens[k];
        ent[k] = heads[k];
        live[nlive++] = k;
    }
    Char* buf[kGather];
    while (nlive > 0) {
        int keep = 0;
        for (int a = 0; a < nlive; ++a) {
            const int k = live[a];
            if (cnt[k] == lens[k]) return false;   // longer than its occupancy
            buf[off[k] + cnt[k]] = P::Get(slots[ent[k]]);
            ++cnt[k];
            ent[k] = slots[ent[k]].prev;
            if (ent[k] != heads[k]) live[keep++] = k;
        }
        nlive = keep;
    }
    for (int k = 0; k < nc; ++k) {
        for (int i = 0; i < cnt[k]; ++i) P::Prefetch(buf[off[k] + i]);
    }
    if (typeId == 0) {
        for (int k = 0; k < nc; ++k) {
            for (int i = 0; i < cnt[k]; ++i) {
                if (buf[off[k] + i] != *exclude) P::PrefetchPos(buf[off[k] + i]);
            }
        }
    }
    for (int k = 0; k < nc; ++k) {
        for (int i = 0; i < cnt[k]; ++i) ScoreOne<P>(buf[off[k] + i], point, typeId, exclude, best, fr2);
    }
    return true;
}

template <class P, class Slot>
bool QueryLeaves(typename P::Char** best, const float* point, float radius, int typeId,
                 typename P::Char* const* exclude, const Slot* slots, const int32_t* cellHead,
                 const int32_t* cellOccupancy, float worldSize, int levels) {
    if (!LeafWalkExact(worldSize, levels)) return false;

    float fr2 = radius * radius;
    float q[6];
    q[0] = point[0] - radius; q[1] = point[1] - radius; q[2] = point[2] - radius;
    q[3] = radius + point[0]; q[4] = radius + point[1]; q[5] = radius + point[2];

    const float half = worldSize * 0.5f;
    const Node root{0.0f, 0.0f, 0.0f, half, half, 0.0f, 0, 0};
    if (!(0 < cellOccupancy[0] && NodeHits(q, root))) {
        *best = nullptr;
        return true;
    }

    const int depth = levels - 1;
    const int n = 1 << depth;
    float e = half;
    for (int d = 0; d < depth; ++d) e = e * 0.5f;   // Subdivide's halvings (exact)

    int xs[kLeafAxis];
    int ys[kLeafAxis];
    int nx = 0;
    int ny = 0;
    if (!AxisRange(q[0], q[3], e, n, xs, &nx)) return false;
    if (!AxisRange(q[1], q[4], e, n, ys, &ny)) return false;

    *best = nullptr;
    const int32_t base = static_cast<int32_t>(((int64_t{1} << (2 * depth)) - 1) / 3);
    int32_t cells[kLeafAxis * kLeafAxis];
    int nc = 0;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int32_t cell = base + static_cast<int32_t>(LeafMorton(static_cast<uint32_t>(xs[i]),
                                                                        static_cast<uint32_t>(ys[j])));
            int k = nc++;
            while (k > 0 && cells[k - 1] < cell) {   // descending: Query's visit order
                cells[k] = cells[k - 1];
                --k;
            }
            cells[k] = cell;
        }
    }
    int32_t heads[kLeafAxis * kLeafAxis];
    int32_t lens[kLeafAxis * kLeafAxis];
    for (int k = 0; k < nc; ++k) heads[k] = cellHead[cells[k]];
    for (int k = 0; k < nc; ++k) lens[k] = cellOccupancy[cells[k]];
    if (ScoreLeavesInterleaved<P, Slot>(heads, lens, nc, slots, point, typeId, exclude, best, &fr2))
        return true;
    for (int k = 0; k < nc; ++k) {
        if (-1 < heads[k]) ScoreCell<P, Slot>(heads[k], slots, point, typeId, exclude, best, &fr2);
    }
    return true;
}

}  // namespace CharGridNearest
