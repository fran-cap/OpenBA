// ============================================================================
// src/game/Action/grid_search.cpp -- the grid A* over a bucket open set
// ============================================================================
// FAITHFUL PERF WORK. grid_search.h has the contract and the rule list R1-R10;
// each rule is marked below where it is reproduced, with the retail line it
// mirrors (src/game/Action/action_pf_astar.cpp unless named otherwise).
//
// WHY THE POP ORDER IS RETAIL'S (R7).
//   Retail's open set is an AVL tree keyed (fcost asc, seq desc) and the
//   leftmost node is popped (PathOpenSet::Insert 0x14001d450 sends a key left
//   when `fcost < node.fcost || (fcost == node.fcost && node.seq < key.seq)`;
//   FindPath descends leftmost). seq is PathSearch::sequence, incremented on
//   every Relax 0x14001d320. A re-key deletes the old node and inserts a new
//   one with a new seq. Here every relax pushes (cell, g) on top of bucket f's
//   stack; nothing is pushed out of seq order, so within one bucket the
//   entries sit in seq order and the top live entry is the maximum seq: the
//   cell retail pops.
//
// WHY AN ENTRY'S g TELLS LIVE FROM STALE (the 8-byte cell).
//   Every relaxation of a cell already reached in this search needs a STRICTLY
//   smaller g: the open case relaxes only on `t < g` (R6), the closed case
//   skips on `g <= t` and reopens only on a strictly smaller t (R5). So a
//   cell's g strictly decreases over the search, its stale entries all carry
//   a larger g than its current one, and an entry is live iff its cell is open
//   in this generation and its g is the cell's g. A closed cell has no live
//   entry (its tag says closed). That is the region search's rule
//   (action_pf_region_search.cpp), and it frees the cell of the seq word.
//   The parent is a direction (3 bits in the tag): the chain is walked back by
//   the direction's offset until the start cell, whose g is 0 and which
//   nothing can relax again (no step cost is negative: Unsupported), so it is
//   retail's chain to the start's parent -1.
//
// WHY A SCALED HEURISTIC KEEPS RETAIL'S COST (ext.fast_paths, scale > 1).
//   Retail's h(n) = H(n) = the unit-cost distance to the goal: 10*(dx+dy) -
//   6*min(dx,dy) with diagonals, else 10*(dx+dy) (Manhattan) or 10*max(dx,dy)
//   (Chebyshev). With a scale, h(n) = s * H(n), s = heuristicScale >= 0.
//   An edge n -> n' (n' a cell the search may enter, R2) costs
//       c = stepCost[t(n')] * 10   orthogonal,   stepCost[t(n')] * 14   diagonal.
//   One move changes H by at most 10 (orthogonal) or 14 (diagonal): dx and dy
//   each change by at most 1, and 10*(dx+dy) - 6*min moves by 10 per axis
//   changed, less 6 when both change and min moves with them, so a diagonal
//   changes it by at most 20 - 6 = 14; Manhattan / Chebyshev by at most 10
//   (they only run without diagonals). So
//       h(n) - h(n') = s * (H(n) - H(n')) <= s * 10 (or 14) <= c
//   whenever s <= stepCost[t(n')]: h is CONSISTENT on every edge the search
//   can take as long as s is at most the cheapest step cost of any enterable
//   cell, which is what HeuristicScale returns (min over the passable types
//   present in the grid). With h(goal) = 0, consistency gives admissibility,
//   so A* returns a cost-optimal path: exactly retail's cost (retail's own h
//   is s = 1, also consistent since every cost is >= 1 in the profiles).
//   Consistency also makes f non-decreasing along every expansion, so a
//   closed cell is never reopened (R5 still reproduced, never taken) and the
//   popped f never goes down -- what the bucket queue needs.
//
// WHY THE BUCKETS ARE BOUNDED.
//   Let F be the f of the cell being expanded (the queue's minimum). A
//   neighbour gets f' = F - h(n) + c + h(n') = F + c + (h(n') - h(n)), so
//       F <= f' <= F + cMax + s * 14 = F + D,   D = 14 * (maxCost + s)
//   (10 instead of 14 without diagonals): the lower bound is consistency, the
//   upper the move bounds above. Every live entry therefore lies in
//   [F, F + D], and a ring of R > D buckets indexed f mod R never aliases.
//   With retail's profiles maxCost <= 16 and s <= 16: D <= 448, R = 512. The
//   ring is sized per request; an f outside [F, F + D] (inputs that break the
//   assumptions) returns Unsupported, so the caller falls back to retail.
// ============================================================================
#include "Action/grid_search.h"

#include <climits>
#include <cstdint>

namespace GridSearch {

namespace {

constexpr int32_t kWall     = -1;   // not walkable (R2)
constexpr int32_t kOffTable = -2;   // walkable but its type indexes past stepCost[0x20]

constexpr uint32_t kOpen   = 1;
constexpr uint32_t kClosed = 2;
constexpr uint32_t kGenShift = 5;
constexpr uint32_t kGenLimit = 1u << (32 - kGenShift);
constexpr uint32_t kDirMask  = 7u;

constexpr int kMaxBuckets = 1 << 16;

// The heuristic modes (FindPath :373-388): octile with 8 directions, else
// Manhattan or Chebyshev with 4.
constexpr int kOctile    = 0;
constexpr int kManhattan = 1;
constexpr int kChebyshev = 2;

// R1: Neighbour 0x1400a5ec0's direction order (action_pf_geom.cpp:66):
// 0 col+1, 1 col-1, 2 row+1, 3 row-1, 4 col+1 row+1, 5 col+1 row-1,
// 6 col-1 row-1, 7 col-1 row+1. 0-3 orthogonal, 4-7 diagonal.
constexpr int kDx[8] = { +1, -1,  0,  0, +1, +1, -1, -1 };
constexpr int kDy[8] = {  0,  0, +1, -1, +1, -1, -1, +1 };

inline int Abs(int v) { return v < 0 ? -v : v; }

// Heuristic 0x1400a6030 (action_pf_geom.cpp:101), inlined in FindPath at
// action_pf_astar.cpp:367-388, at unit cost.
template <int kMode>
inline int32_t UnitDistance(int dx, int dy) {
    if (kMode == kOctile) {
        const int mn = dx < dy ? dx : dy;
        return (dx + dy) * 10 - 6 * mn;
    }
    if (kMode == kManhattan) return (dx + dy) * 10;
    return (dx > dy ? dx : dy) * 10;
}

}  // namespace

int32_t HeuristicScale(const int32_t* stepCost, uint32_t passableTypes, uint32_t presentTypes) {
    int32_t best = INT32_MAX;
    const uint32_t enterable = passableTypes & presentTypes;
    for (int t = 0; t < kStepCosts; ++t) {
        if (((enterable >> t) & 1u) == 0) continue;
        if (stepCost[t] < 0) return -1;
        if (stepCost[t] < best) best = stepCost[t];
    }
    return best == INT32_MAX ? 0 : best;
}

uint32_t PresentTypes(const uint8_t* cellFlags, int cellCount) {
    bool seen[kCellTypeCount] = {};
    for (int i = 0; i < cellCount; ++i) seen[cellFlags[i] & kCellTypeMask] = true;
    uint32_t mask = 0;
    for (int t = 0; t < kStepCosts; ++t)
        if (seen[t]) mask |= 1u << t;
    return mask;
}

size_t Searcher::ScratchBytes() const {
    return cells_.capacity() * sizeof(CellState) + head_.capacity() * sizeof(int32_t) +
           entries_.capacity() * sizeof(Entry);
}

void Searcher::Prepare(int cellCount, int bucketCount) {
    if (static_cast<int>(cells_.size()) != cellCount) {
        cells_.assign(cellCount, CellState{0, 0u});
        gen_ = 0;
    }
    // Retail bumps PathSearch::generation per run (Run 0x14001ce00, :415) and
    // never clears its node array; so do we, resetting on the wrap. Tag 0 is
    // generation 0, which no search uses.
    if (++gen_ >= kGenLimit) {
        for (CellState& c : cells_) c.tag = 0u;
        gen_ = 1;
    }
    if (static_cast<int>(head_.size()) < bucketCount) head_.resize(bucketCount);
    for (int i = 0; i < bucketCount; ++i) head_[i] = -1;
    entries_.clear();
}

SearchResult Searcher::Run(const SearchInput& in, int32_t* path) {
    SearchResult out;
    const int w = in.width;
    const int cellCount = w * w;
    const int32_t s = in.heuristicScale;
    if (w <= 0 || s < 0) {
        out.status = Status::Unsupported;
        return out;
    }

    // R2 + R3 folded into one table over the whole flag byte: the walkable
    // test of Neighbour (action_pf_geom.cpp:85-88: `respectBlocked == 0 ||
    // (b & 0x80) == 0`, then `(passableTypes >> (b & 0x1f)) & 1`) and the cost
    // index of FindPath (action_pf_astar.cpp:351: `cellFlags[nb] & 0x7f`).
    int32_t cost[256];
    int32_t maxCost = 0;
    for (int b = 0; b < 256; ++b) {
        int32_t c;
        if (in.respectBlocked && (b & kCellBlocked) != 0) {
            c = kWall;
        } else if (((in.passableTypes >> (b & 0x1f)) & 1u) == 0) {
            c = kWall;
        } else if ((b & kCellTypeMask) >= kStepCosts) {
            c = kOffTable;
        } else {
            c = in.stepCost[b & kCellTypeMask];
            if (c < 0) {
                out.status = Status::Unsupported;
                return out;
            }
            if (c > maxCost) maxCost = c;
        }
        cost[b] = c;
    }

    const int moveMax = in.allowDiagonal ? 14 : 10;
    // Int32 exactness: the largest g is below cellCount * moveMax * maxCost and
    // the largest h below 2 * w * 10 * s. Retail computes in int; refusing
    // inputs that could overflow keeps both searches exact and identical.
    const int64_t gBound = static_cast<int64_t>(cellCount) * moveMax * maxCost;
    const int64_t hBound = static_cast<int64_t>(w) * 20 * s;
    const int64_t span = static_cast<int64_t>(moveMax) * (static_cast<int64_t>(maxCost) + s);
    if (gBound + hBound >= INT32_MAX || span >= kMaxBuckets) {
        out.status = Status::Unsupported;
        return out;
    }
    int buckets = 1;
    while (buckets <= span) buckets <<= 1;

    Prepare(cellCount, buckets);
    // FindPath :336-338: 4 directions, or 8 when allowDiagonal; :373-388 the heuristic.
    if (in.allowDiagonal) return RunMode<kOctile>(in, path, cost, buckets, static_cast<uint32_t>(span));
    if (in.manhattan) return RunMode<kManhattan>(in, path, cost, buckets, static_cast<uint32_t>(span));
    return RunMode<kChebyshev>(in, path, cost, buckets, static_cast<uint32_t>(span));
}

template <int kMode>
SearchResult Searcher::RunMode(const SearchInput& in, int32_t* path, const int32_t* cost, int buckets,
                               uint32_t span) {
    constexpr int kDirs = kMode == kOctile ? 8 : 4;
    SearchResult out;
    const int w = in.width;
    const int32_t s = in.heuristicScale;
    const int32_t bucketMask = buckets - 1;
    const uint32_t openTag = (gen_ << kGenShift) | (kOpen << 3);
    const uint32_t closedTag = (gen_ << kGenShift) | (kClosed << 3);

    int off[8];
    for (int d = 0; d < 8; ++d) off[d] = kDy[d] * w + kDx[d];

    const int start = in.start;
    const int goal = in.goal;
    const int gx = goal % w;
    const int gy = goal / w;
    CellState* const cells = cells_.data();
    int32_t* const head = head_.data();
    const uint8_t* const flags = in.cellFlags;
    // An interior cell (not on the border) has all 8 neighbours in the grid.
    const unsigned inner = static_cast<unsigned>(w - 2);

    int32_t sequence = 0;   // PathSearch::sequence, reset per FindPath (:300)
    int liveOpen = 0;       // PathOpenSet::count

    // Relax 0x14001d320: set parent / g, mark open, push (cell, g) at f. The old
    // entry of an already open cell stays behind, stale (R6).
    auto relax = [&](int cell, uint32_t dir, int32_t gCost, int32_t f) {
        CellState& c = cells[cell];
        if ((c.tag & ~kDirMask) != openTag) ++liveOpen;
        c.tag = openTag | dir;
        c.g = gCost;
        entries_.push_back(Entry{cell, gCost, head[f & bucketMask]});
        head[f & bucketMask] = static_cast<int32_t>(entries_.size()) - 1;
        ++sequence;
    };

    // FindPath :308-309: Relax(start, -1, 0, Heuristic(start, goal)).
    int32_t cur = s * UnitDistance<kMode>(Abs(start % w - gx), Abs(start / w - gy));
    relax(start, 0u, 0, cur);

    bool found = false;
    for (;;) {
        // R8: FindPath :313-315: an empty open set is "no path".
        if (liveOpen < 1) break;
        // R7: the minimum f, newest first. Live entries lie in [cur, cur + span].
        int32_t e = head[cur & bucketMask];
        while (e < 0) {
            ++cur;
            e = head[cur & bucketMask];
        }
        const Entry top = entries_[e];
        head[cur & bucketMask] = top.next;
        const int cell = top.cell;
        CellState& here = cells[cell];
        if ((here.tag & ~kDirMask) != openTag || here.g != top.g) continue;   // stale
        // R4: FindPath :324-326: the goal is tested when POPPED.
        if (cell == goal) {
            found = true;
            break;
        }
        // FindPath :327-332: delete, close.
        --liveOpen;
        here.tag = closedTag | (here.tag & kDirMask);
        ++out.expanded;

        const int x = cell % w;
        const int y = cell / w;
        const int32_t gHere = top.g;
        const bool interior = static_cast<unsigned>(x - 1) < inner && static_cast<unsigned>(y - 1) < inner;
#pragma GCC unroll 8
        for (int dir = 0; dir < kDirs; ++dir) {
            const int nx = x + kDx[dir];
            const int ny = y + kDy[dir];
            // R1: Neighbour's bounds test (action_pf_geom.cpp:81), only on the border.
            if (!interior && (static_cast<unsigned>(nx) >= static_cast<unsigned>(w) ||
                              static_cast<unsigned>(ny) >= static_cast<unsigned>(w))) {
                continue;
            }
            const int nb = cell + off[dir];
            const int32_t c = cost[flags[nb]];
            if (c < 0) {
                if (c == kOffTable) {
                    out.status = Status::Unsupported;
                    return out;
                }
                continue;   // R2: Neighbour returned -1
            }
            // R3: FindPath :352-356: |dcol| + |drow| == 1 -> * 10, else * 14.
            const int32_t t = gHere + c * (dir < 4 ? 10 : 14);
            CellState& n = cells[nb];
            uint32_t state = n.tag & ~kDirMask;
            // R5: FindPath :360-363: closed and not cheaper -> skip; cheaper ->
            // un-close (closedStamp - 1) and fall through to the open test.
            if (state == closedTag) {
                if (n.g <= t) continue;
                state = 0u;
            }
            // R6: FindPath :365-366, Relax :389: relax when not open, or strictly cheaper.
            if (state != openTag || t < n.g) {
                const int32_t f = t + s * UnitDistance<kMode>(Abs(nx - gx), Abs(ny - gy));
                // The bucket bound (header): a violation means the scale broke
                // consistency for these inputs -- let retail run instead.
                if (static_cast<uint32_t>(f - cur) > span) {
                    out.status = Status::Unsupported;
                    return out;
                }
                relax(nb, static_cast<uint32_t>(dir), t, f);
            }
        }
    }

    out.inserted = sequence;
    out.openLeft = liveOpen;
    if (!found) {
        out.status = Status::NotFound;   // Run :417-419: return 0
        return out;
    }
    out.status = Status::Found;
    out.cost = cells[goal].g;

    // R9: Run 0x14001ce00 (:420-451): count the chain from the goal; with a
    // buffer, write it back to front keeping indices < 0x4000, clamp the count.
    int len = 1;
    for (int c = goal; c != start; c -= off[cells[c].tag & kDirMask]) ++len;
    if (path != nullptr) {
        int c = goal;
        for (int i = len - 1; i >= 0; --i) {
            if (i < kPathCap) path[i] = c;
            if (i > 0) c -= off[cells[c].tag & kDirMask];
        }
        if (len > kPathCap) len = kPathCap;
    }
    out.count = len;
    return out;
}

}  // namespace GridSearch
