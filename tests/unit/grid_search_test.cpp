// ============================================================================
// tests/unit/grid_search_test.cpp -- the grid search against retail's
// ============================================================================
// src/game/Action/grid_search.{h,cpp} on its own: the faithful grid search
// (GridPathSearch::Run, scale 1) and ext.fast_paths' (the real scale). The reference below is a
// port of retail's grid search -- GridPathSearch::Run 0x14001ce00, FindPath
// 0x14001d040, PathSearch::Relax 0x14001d320, Neighbour 0x1400a5ec0, Heuristic
// 0x1400a6030 (src/game/Action/action_pf_astar.cpp, action_pf_geom.cpp) --
// with the AVL open set replaced by a std::set under the same key (fcost asc,
// seq desc) and the same generation stamps. (The real GridPathSearch cannot be
// linked here: its bodies pull in the Map, the thread and the pools.)
//
//   (i)   heuristic scale 1: the search returns retail's cells, count, cost,
//         expansions, relaxations and open-set count exactly (the (f, LIFO)
//         claim; GridPathSearch::Run's production body)
//   (ii)  the real scale: the same found / not found and exactly retail's
//         cost; expansions fewer or equal (the per-case count is reported)
//   (iii) no-path cases agree
//   (iv)  the 0x4000 clamp (retail has no node budget and no partial path)
// Thousands of seeded random grids.
// ============================================================================
#include "unit.h"
#include "Action/grid_search.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// The reference: retail's search, line for line where it matters.
// ---------------------------------------------------------------------------
struct RefSearch {
    struct Node { int openStamp = 0, closedStamp = 0, parent = -1, gCost = 0, fKey = 0, seqKey = 0; };
    // PathOpenSet's order: key a before b when a.f < b.f, or equal f and a.seq > b.seq.
    struct Key {
        int f, seq, cell;
        bool operator<(const Key& o) const { return f != o.f ? f < o.f : seq > o.seq; }
    };
    std::vector<Node> nodes;
    std::set<Key> open;
    int generation = 1;
    int sequence = 0;
    int expanded = 0;

    // GridPathSearch's fields.
    int width = 0;
    const uint8_t* cellFlags = nullptr;
    const int32_t* stepCost = nullptr;
    uint32_t passableTypes = 0;
    bool allowDiagonal = false, manhattan = false, respectBlocked = false;

    int Neighbour(int index, int dir) const {   // 0x1400a5ec0
        const int w = width;
        int row = index / w, col = index % w;
        switch (dir) {
        case 0: col += 1; break;
        case 1: col -= 1; break;
        case 2: row += 1; break;
        case 3: row -= 1; break;
        case 4: col += 1; row += 1; break;
        case 5: col += 1; row -= 1; break;
        case 6: col -= 1; row -= 1; break;
        case 7: col -= 1; row += 1; break;
        default: break;
        }
        if (col < 0 || col >= w || row < 0 || row >= w) return -1;
        const int cell = w * row + col;
        if (respectBlocked && (cellFlags[cell] & 0x80) != 0) return -1;
        if (((passableTypes >> (cellFlags[cell] & 0x1f)) & 1u) == 0) return -1;
        return cell;
    }
    int Heuristic(int from, int to) const {    // 0x1400a6030
        const int w = width;
        int dx = from % w - to % w; if (dx < 0) dx = -dx;
        int dy = from / w - to / w; if (dy < 0) dy = -dy;
        if (allowDiagonal) return (dx + dy) * 10 - 6 * (dx < dy ? dx : dy);
        if (manhattan) return (dx + dy) * 10;
        return (dx > dy ? dx : dy) * 10;
    }
    void Relax(int v, int par, int g, int f) {  // 0x14001d320
        Node& n = nodes[v];
        if (n.openStamp == generation) open.erase(Key{n.fKey, n.seqKey, v});
        n.closedStamp -= 1;
        n.parent = par;
        n.openStamp = generation;
        n.gCost = g;
        n.fKey = f;
        n.seqKey = sequence++;
        open.insert(Key{f, n.seqKey, v});
    }
    bool FindPath(int start, int goal) {        // 0x14001d040
        open.clear();
        sequence = 0;
        Relax(start, -1, 0, Heuristic(start, goal));
        for (;;) {
            if (open.empty()) return false;
            const Key top = *open.begin();
            if (top.cell == goal) return true;
            open.erase(open.begin());
            Node& cur = nodes[top.cell];
            cur.openStamp -= 1;
            cur.closedStamp = generation;
            ++expanded;
            const int dirs = allowDiagonal ? 8 : 4;
            for (int d = 0; d < dirs; ++d) {
                const int nb = Neighbour(top.cell, d);
                if (nb < 0) continue;
                int dc = top.cell % width - nb % width; if (dc < 0) dc = -dc;
                int dr = top.cell / width - nb / width; if (dr < 0) dr = -dr;
                const int step = stepCost[cellFlags[nb] & 0x7f] * (dc + dr == 1 ? 10 : 14);
                const int t = nodes[top.cell].gCost + step;
                Node& n = nodes[nb];
                if (n.closedStamp == generation) {
                    if (n.gCost <= t) continue;
                    n.closedStamp -= 1;
                }
                if (n.openStamp != generation || t < n.gCost)
                    Relax(nb, top.cell, t, Heuristic(nb, goal) + t);
            }
        }
    }
    int Run(int start, int goal, int32_t* path) {   // 0x14001ce00
        if ((int)nodes.size() != width * width) nodes.assign(width * width, Node{});
        generation += 1;
        expanded = 0;
        if (!FindPath(start, goal)) return 0;
        int len = 0;
        for (int c = goal; c != -1; c = nodes[c].parent) ++len;
        if (path == nullptr) return len;
        int c = goal;
        for (int i = len - 1; i >= 0; --i) {
            if (i < GridSearch::kPathCap) path[i] = c;
            c = nodes[c].parent;
        }
        return len > GridSearch::kPathCap ? GridSearch::kPathCap : len;
    }
};

// ---------------------------------------------------------------------------
// Seeded random grids.
// ---------------------------------------------------------------------------
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s >> 11); }
    int Below(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
    bool Chance(int percent) { return Below(100) < percent; }
};

// Map::SetMovementCosts 0x14009e550's four profiles (types 0..10); type 11
// and up keep the grid ctor's 1 (pathfinder_seed.cpp GridCtor).
void FillCosts(int profile, int32_t* cost) {
    static const int32_t kP[4][11] = {
        { 3, 3, 4, 8, 16, 16, 16, 16, 16, 3, 4 },
        { 16, 16, 4, 4, 16, 16, 16, 4, 4, 4, 4 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        { 10, 10, 10, 10, 10, 10, 10, 5, 1, 5, 10 },
    };
    for (int t = 0; t < GridSearch::kStepCosts; ++t) cost[t] = 1;
    for (int t = 0; t < 11; ++t) cost[t] = kP[profile][t];
}

struct Case {
    int width = 0;
    std::vector<uint8_t> flags;
    int32_t cost[GridSearch::kStepCosts] = {};
    uint32_t passable = 0;
    bool diag = true, manh = false, blocked = false;
    int start = 0, goal = 0;
};

// Terrain like a town: open ground (2), roads (0 / 1 / 9) in lines, patches of
// 3 / 4 / 10 / 6 / 5, water (7 / 8), a few walls (an impassable type for the
// mask), a blocked bit here and there. `wallPercent` adds random walls.
Case MakeCase(Rng& r, int width, int wallPercent, int profile) {
    Case c;
    c.width = width;
    c.flags.assign(width * width, 2);
    const int n = width * width;
    // patches
    const int patches = 1 + width / 4;
    for (int p = 0; p < patches; ++p) {
        static const uint8_t kTypes[] = { 3, 4, 5, 6, 7, 8, 10, 10, 6 };
        const uint8_t t = kTypes[r.Below(9)];
        const int cx = r.Below(width), cy = r.Below(width), rad = 1 + r.Below(1 + width / 6);
        for (int y = cy - rad; y <= cy + rad; ++y)
            for (int x = cx - rad; x <= cx + rad; ++x)
                if (x >= 0 && y >= 0 && x < width && y < width) c.flags[y * width + x] = t;
    }
    // roads
    const int roads = r.Below(1 + width / 6);
    for (int k = 0; k < roads; ++k) {
        static const uint8_t kRoad[] = { 0, 1, 9 };
        const uint8_t t = kRoad[r.Below(3)];
        if (r.Chance(50)) { const int y = r.Below(width); for (int x = 0; x < width; ++x) c.flags[y * width + x] = t; }
        else              { const int x = r.Below(width); for (int y = 0; y < width; ++y) c.flags[y * width + x] = t; }
    }
    // a rare type 11 (walkable, cost 1 in every profile but 2)
    if (r.Chance(10)) c.flags[r.Below(n)] = 11;
    // walls: type 12 (never in a mask below)
    for (int i = 0; i < n; ++i)
        if (r.Chance(wallPercent)) c.flags[i] = 12;
    // blocked bits
    const int blockedCells = r.Below(1 + n / 20);
    for (int k = 0; k < blockedCells; ++k) c.flags[r.Below(n)] |= 0x80;

    FillCosts(profile, c.cost);
    static const uint32_t kMasks[] = { 0xe1fu, 0xa17u, 0xff7u, 0x0e1fu & ~0x100u, 0x7ffu };
    c.passable = kMasks[r.Below(5)];
    c.diag = r.Chance(85);
    c.manh = !c.diag && r.Chance(50);
    c.blocked = r.Chance(30);
    c.start = r.Below(n);
    c.goal = r.Chance(5) ? c.start : r.Below(n);
    // R10: Map::RequestPath ORs the endpoints' types into the mask.
    c.passable |= 1u << (c.flags[c.start] & 0x1f);
    c.passable |= 1u << (c.flags[c.goal] & 0x1f);
    return c;
}

GridSearch::SearchInput InputOf(const Case& c, int32_t scale) {
    GridSearch::SearchInput in;
    in.width = c.width;
    in.cellFlags = c.flags.data();
    in.stepCost = c.cost;
    in.passableTypes = c.passable;
    in.allowDiagonal = c.diag;
    in.manhattan = c.manh;
    in.respectBlocked = c.blocked;
    in.start = c.start;
    in.goal = c.goal;
    in.heuristicScale = scale;
    return in;
}

RefSearch RefOf(const Case& c) {
    RefSearch ref;
    ref.width = c.width;
    ref.cellFlags = c.flags.data();
    ref.stepCost = c.cost;
    ref.passableTypes = c.passable;
    ref.allowDiagonal = c.diag;
    ref.manhattan = c.manh;
    ref.respectBlocked = c.blocked;
    return ref;
}

int32_t RealScale(const Case& c) {
    return GridSearch::HeuristicScale(c.cost, c.passable,
                                     GridSearch::PresentTypes(c.flags.data(), c.width * c.width));
}

struct Totals {
    int64_t cases = 0, found = 0, notFound = 0, mismatches = 0;
    int64_t refExpanded = 0, ourExpanded = 0, moreExpanded = 0, sameCells = 0;
};

const int kSizes[] = { 3, 8, 16, 31, 48, 64, 97, 128 };

}  // namespace

// (i) scale 1: identical to retail, every field.
UNIT_TEST(grid_search_scale1_identical_to_retail) {
    Rng r(1234);
    GridSearch::Searcher fast;
    Totals t;
    std::vector<int32_t> a(GridSearch::kPathCap), b(GridSearch::kPathCap);
    for (int iter = 0; iter < 4000; ++iter) {
        const int w = kSizes[r.Below(8)];
        Case c = MakeCase(r, w, r.Chance(30) ? r.Below(35) : 0, r.Below(4));
        RefSearch ref = RefOf(c);
        const int refCount = ref.Run(c.start, c.goal, a.data());
        const GridSearch::SearchResult res = fast.Run(InputOf(c, 1), b.data());
        ++t.cases;
        bool ok = res.status != GridSearch::Status::Unsupported && res.count == refCount &&
                  res.expanded == ref.expanded && res.inserted == ref.sequence &&
                  res.openLeft == static_cast<int>(ref.open.size());
        for (int i = 0; ok && i < refCount; ++i) ok = a[i] == b[i];
        if (ok && refCount > 0) ok = res.cost == ref.nodes[c.goal].gCost;
        if (refCount > 0) ++t.found; else ++t.notFound;
        if (!ok) {
            if (t.mismatches == 0)
                std::printf("  first mismatch: iter %d w %d start %d goal %d ref %d/%d ours %d/%d st %d\n",
                            iter, w, c.start, c.goal, refCount, ref.expanded, res.count, res.expanded,
                            static_cast<int>(res.status));
            ++t.mismatches;
        }
    }
    std::printf("  scale 1: %ld cases (%ld found, %ld no path), %ld mismatches\n",
                t.cases, t.found, t.notFound, t.mismatches);
    CHECK_EQ(t.mismatches, 0L);
    CHECK(t.found > 1000);
    CHECK(t.notFound > 50);
}

// (ii) + (iii) the real scale: retail's cost exactly, fewer expansions.
UNIT_TEST(ext_fast_paths_real_scale_keeps_cost) {
    Rng r(98765);
    GridSearch::Searcher fast;
    Totals t, citizens, bigCitizens;
    std::vector<int32_t> a(GridSearch::kPathCap), b(GridSearch::kPathCap);
    for (int iter = 0; iter < 4000; ++iter) {
        const int w = kSizes[r.Below(8)];
        const int profile = r.Below(4);
        Case c = MakeCase(r, w, r.Chance(30) ? r.Below(35) : 0, profile);
        RefSearch ref = RefOf(c);
        const int refCount = ref.Run(c.start, c.goal, a.data());
        const int32_t scale = RealScale(c);
        const GridSearch::SearchResult res = fast.Run(InputOf(c, scale), b.data());
        ++t.cases;
        bool ok = res.status != GridSearch::Status::Unsupported;
        ok = ok && (refCount > 0) == (res.status == GridSearch::Status::Found);
        if (ok && refCount > 0) {
            ok = res.cost == ref.nodes[c.goal].gCost && res.count > 0 &&
                 b[0] == c.start && b[res.count - 1] == c.goal;
            // The path is walkable and its step costs sum to the cost.
            int64_t sum = 0;
            for (int i = 1; ok && i < res.count; ++i) {
                const int p = b[i - 1], q = b[i];
                const int dx = q % w - p % w, dy = q / w - p / w;
                ok = dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dx | dy) != 0 &&
                     (c.diag || dx == 0 || dy == 0) &&
                     ((c.passable >> (c.flags[q] & 0x1f)) & 1u) != 0 &&
                     !(c.blocked && (c.flags[q] & 0x80) != 0);
                sum += static_cast<int64_t>(c.cost[c.flags[q] & 0x7f]) * ((dx != 0 && dy != 0) ? 14 : 10);
            }
            ok = ok && sum == res.cost;
            bool same = res.count == refCount;
            for (int i = 0; same && i < refCount; ++i) same = a[i] == b[i];
            if (same) ++t.sameCells;
        }
        if (refCount > 0) ++t.found; else ++t.notFound;
        t.refExpanded += ref.expanded;
        t.ourExpanded += res.expanded;
        if (res.expanded > ref.expanded) ++t.moreExpanded;
        if (profile == 0 && c.diag && refCount > 0) {
            ++citizens.cases;
            citizens.refExpanded += ref.expanded;
            citizens.ourExpanded += res.expanded;
            if (w >= 48) {
                ++bigCitizens.cases;
                bigCitizens.refExpanded += ref.expanded;
                bigCitizens.ourExpanded += res.expanded;
            }
        }
        if (!ok) {
            if (t.mismatches == 0)
                std::printf("  first mismatch: iter %d w %d start %d goal %d ref %d cost %d ours %d cost %lld st %d\n",
                            iter, w, c.start, c.goal, refCount, refCount ? ref.nodes[c.goal].gCost : -1,
                            res.count, static_cast<long long>(res.cost), static_cast<int>(res.status));
            ++t.mismatches;
        }
    }
    std::printf("  real scale: %ld cases (%ld found, %ld no path), %ld mismatches, same cells %ld, "
                "expanded %ld vs retail %ld (ratio %.3f), %ld cases expanded more\n",
                t.cases, t.found, t.notFound, t.mismatches, t.sameCells, t.ourExpanded, t.refExpanded,
                t.refExpanded ? static_cast<double>(t.ourExpanded) / t.refExpanded : 0.0, t.moreExpanded);
    std::printf("  citizen profile, diagonal, found: %ld cases, expanded %ld vs retail %ld (ratio %.3f)\n",
                citizens.cases, citizens.ourExpanded, citizens.refExpanded,
                citizens.refExpanded ? static_cast<double>(citizens.ourExpanded) / citizens.refExpanded : 0.0);
    std::printf("  ... of which 48 / 64 cells wide: %ld cases, expanded %ld vs retail %ld (ratio %.3f)\n",
                bigCitizens.cases, bigCitizens.ourExpanded, bigCitizens.refExpanded,
                bigCitizens.refExpanded ? static_cast<double>(bigCitizens.ourExpanded) / bigCitizens.refExpanded : 0.0);
    CHECK_EQ(t.mismatches, 0L);
    CHECK(t.ourExpanded <= t.refExpanded);
    CHECK(t.notFound > 50);
}

// (iii) no path: a closed wall ring around the goal; both exhaust the start's
// side and agree, at either scale, with the same expansion count (every cell
// reachable from the start is closed once).
UNIT_TEST(ext_fast_paths_no_path_agrees) {
    Rng r(555);
    GridSearch::Searcher fast;
    int64_t bad = 0, cases = 0;
    for (int iter = 0; iter < 500; ++iter) {
        const int w = 8 + r.Below(40);
        Case c = MakeCase(r, w, 0, r.Below(4));
        const int gx = 2 + r.Below(w - 4), gy = 2 + r.Below(w - 4);
        c.goal = gy * w + gx;
        for (int y = gy - 1; y <= gy + 1; ++y)
            for (int x = gx - 1; x <= gx + 1; ++x)
                if (x != gx || y != gy) c.flags[y * w + x] = 12;
        do { c.start = r.Below(w * w); } while (c.flags[c.start] == 12 || c.start == c.goal);
        c.passable = 0xe1fu | (1u << (c.flags[c.start] & 0x1f)) | (1u << (c.flags[c.goal] & 0x1f));
        RefSearch ref = RefOf(c);
        const int refCount = ref.Run(c.start, c.goal, nullptr);
        const GridSearch::SearchResult s1 = fast.Run(InputOf(c, 1), nullptr);
        const GridSearch::SearchResult sr = fast.Run(InputOf(c, RealScale(c)), nullptr);
        ++cases;
        if (refCount != 0 || s1.status != GridSearch::Status::NotFound ||
            sr.status != GridSearch::Status::NotFound || s1.count != 0 || sr.count != 0 ||
            s1.expanded != ref.expanded || sr.expanded != ref.expanded)
            ++bad;
    }
    std::printf("  no path: %ld cases, %ld disagree\n", cases, bad);
    CHECK_EQ(bad, 0L);
}

// (iv) retail has no node budget and no partial path (R8); its only bound is
// Run's 0x4000-cell buffer (R9). A serpentine longer than 0x4000 cells: the
// first 0x4000 cells, the count clamped, and the unclamped length without a
// buffer.
UNIT_TEST(ext_fast_paths_path_cap_clamp) {
    for (int diag = 0; diag < 2; ++diag) {
        const int w = 200;
        Case c;
        c.width = w;
        c.flags.assign(w * w, 2);
        for (int y = 1; y < w; y += 2) {
            for (int x = 0; x < w; ++x) c.flags[y * w + x] = 12;
            const int gap = ((y / 2) % 2 == 0) ? w - 1 : 0;
            c.flags[y * w + gap] = 2;
        }
        FillCosts(0, c.cost);
        c.passable = 0xe1fu;
        c.diag = diag != 0;
        c.start = 0;
        c.goal = (w - 2) * w + (((w - 2) / 2) % 2 == 0 ? 0 : w - 1);
        RefSearch ref = RefOf(c);
        std::vector<int32_t> a(GridSearch::kPathCap), b(GridSearch::kPathCap), d(GridSearch::kPathCap);
        const int refCount = ref.Run(c.start, c.goal, a.data());
        const int refLen = RefOf(c).Run(c.start, c.goal, nullptr);
        GridSearch::Searcher fast;
        const GridSearch::SearchResult s1 = fast.Run(InputOf(c, 1), b.data());
        const GridSearch::SearchResult sr = fast.Run(InputOf(c, RealScale(c)), d.data());
        const GridSearch::SearchResult sn = fast.Run(InputOf(c, RealScale(c)), nullptr);
        CHECK(refLen > GridSearch::kPathCap);
        CHECK_EQ(refCount, GridSearch::kPathCap);
        CHECK_EQ(s1.count, GridSearch::kPathCap);
        CHECK_EQ(sr.count, GridSearch::kPathCap);
        CHECK_EQ(sn.count, refLen);
        CHECK_EQ(sr.cost, static_cast<int64_t>(ref.nodes[c.goal].gCost));
        bool same1 = true;
        for (int i = 0; i < GridSearch::kPathCap; ++i) same1 = same1 && a[i] == b[i];
        CHECK(same1);
        CHECK_EQ(d[0], c.start);
    }
}

// What the search refuses, so the seam runs retail's instead.
UNIT_TEST(ext_fast_paths_unsupported_inputs) {
    Case c;
    c.width = 8;
    c.flags.assign(64, 2);
    FillCosts(0, c.cost);
    c.passable = 0xe1fu;
    c.start = 0;
    c.goal = 63;
    GridSearch::Searcher fast;
    // A passable cell whose type (& 0x7f) indexes past stepCost[0x20]: retail
    // reads the bytes after the table; we do not guess them.
    c.flags[9] = 0x22;   // type 34: & 0x1f = 2, passable
    CHECK(fast.Run(InputOf(c, 1), nullptr).status == GridSearch::Status::Unsupported);
    c.flags[9] = 2;
    // A negative step cost on a passable type.
    c.cost[2] = -1;
    CHECK(fast.Run(InputOf(c, 1), nullptr).status == GridSearch::Status::Unsupported);
    c.cost[2] = 4;
    CHECK(fast.Run(InputOf(c, 1), nullptr).status == GridSearch::Status::Found);
    // The scale: min over passable AND present types.
    CHECK_EQ(GridSearch::HeuristicScale(c.cost, 0xe1fu, ~0u), 1);           // type 11 costs 1
    CHECK_EQ(GridSearch::HeuristicScale(c.cost, 0xe1fu, 1u << 2), 4);       // ground only
    CHECK_EQ(GridSearch::HeuristicScale(c.cost, 0xe1fu, (1u << 2) | 1u), 3);   // + a road
    CHECK_EQ(GridSearch::HeuristicScale(c.cost, 0x0u, ~0u), 0);
    CHECK_EQ(GridSearch::PresentTypes(c.flags.data(), 64), 1u << 2);
}

// The regime PATHRESEARCH §1.6 reasons about: citizens (profile 0, mask 0xe1f,
// diagonals) on open ground with a few roads and no type 11, 128 cells wide,
// trips of every length. Retail's cost exactly; the expansion ratio printed.
UNIT_TEST(ext_fast_paths_open_ground_expansions) {
    Rng r(4242);
    GridSearch::Searcher fast;
    int64_t bad = 0, cases = 0, refExp = 0, ourExp = 0, roadCases = 0, roadRef = 0, roadOur = 0;
    for (int iter = 0; iter < 300; ++iter) {
        const int w = 128;
        Case c;
        c.width = w;
        c.flags.assign(w * w, 2);
        const bool roads = iter % 2 == 1;
        if (roads)
            for (int k = 0; k < 6; ++k) {
                if (k % 2) { const int y = r.Below(w); for (int x = 0; x < w; ++x) c.flags[y * w + x] = 1; }
                else       { const int x = r.Below(w); for (int y = 0; y < w; ++y) c.flags[y * w + x] = 1; }
            }
        FillCosts(0, c.cost);
        c.passable = 0xe1fu;
        c.diag = true;
        c.start = r.Below(w * w);
        c.goal = r.Below(w * w);
        RefSearch ref = RefOf(c);
        const int refCount = ref.Run(c.start, c.goal, nullptr);
        const GridSearch::SearchResult res = fast.Run(InputOf(c, RealScale(c)), nullptr);
        ++cases;
        if (refCount == 0 || res.status != GridSearch::Status::Found || res.count != refCount ||
            res.cost != ref.nodes[c.goal].gCost)
            ++bad;
        if (roads) { ++roadCases; roadRef += ref.expanded; roadOur += res.expanded; }
        else       { refExp += ref.expanded; ourExp += res.expanded; }
    }
    std::printf("  open ground 128x128: %ld trips, expanded %ld vs retail %ld (ratio %.3f); "
                "with roads: %ld trips, %ld vs %ld (ratio %.3f); %ld disagree\n",
                cases - roadCases, ourExp, refExp, refExp ? static_cast<double>(ourExp) / refExp : 0.0,
                roadCases, roadOur, roadRef, roadRef ? static_cast<double>(roadOur) / roadRef : 0.0, bad);
    CHECK_EQ(bad, 0L);
    CHECK(ourExp * 2 < refExp);
}
