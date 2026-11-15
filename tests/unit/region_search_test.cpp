// ============================================================================
// tests/unit/region_search_test.cpp -- PATHDIST: RegionGraph::FindPath's bucket open set
// against retail's AVL order
// ============================================================================
// The production search (src/game/Action/action_pf_region_search.cpp, linked here on its
// own) runs on a raw RegionGraph record, as retail builds it (MemAlloc + field stores; no
// C++ ctor or dtor). The reference below is retail's region search -- RegionGraph::Run
// 0x14001dc00, FindPath 0x14001dcd0, PathSearch::Relax 0x14001d320 and PathLength
// 0x1400a8ba0's sum (src/game/Action/action_pf_astar.cpp, action_pf_region.cpp) -- with the
// AVL PathOpenSet replaced by a std::set under the AVL's own key order (PathOpenSet::Insert
// 0x14001d450 goes left while key.f < node.f or (equal f and node.seq < key.seq), and the
// pop is the leftmost node, so: f ascending, then seq DESCENDING), the same delete-then-
// insert re-key and the same generation stamps. (The real PathOpenSet cannot be linked
// here: its bodies sit with the worker thread and the pools.) The runtime layer of the
// proof, the real AVL beside the bucket set, is the COMPPROF=4 shadow
// (RegionGraph::VerifyPathLength).
//
// Each graph runs a sequence of queries on ONE persistent search state (generation bumps,
// stale stamps from earlier queries, nodes reopened), and after every query compares:
// found, the path (Run's extraction, 0x2000 clamp), PathLength's sum, search.sequence,
// search.open.count, the expansion count and every node's parent / openStamp / closedStamp
// / gCost.
// ============================================================================
#include "unit.h"
#include "Action/PathSearch.h"
#include "Action/region_search.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s >> 11); }
    int Below(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
    bool Chance(int percent) { return Below(100) < percent; }
};

struct Graph {
    std::vector<int> x, y;
    std::vector<std::vector<int32_t>> adj;
    int Size() const { return static_cast<int>(x.size()); }
};

int Dist(const Graph& g, int a, int b) {   // retail's int(sqrtf(float(dy*dy + dx*dx)))
    const int dy = g.y[a] - g.y[b];
    const int dx = g.x[a] - g.x[b];
    return static_cast<int>(sqrtf(static_cast<float>(dy * dy + dx * dx)));
}

// ---------------------------------------------------------------------------
// The reference: retail's region search, line for line where it matters.
// ---------------------------------------------------------------------------
struct RefSearch {
    struct Node { int parent = -1, openStamp = 0, closedStamp = 0, gCost = 0, fKey = 0, seqKey = 0; };
    struct Key {
        int f, seq, v;
        bool operator<(const Key& o) const { return f != o.f ? f < o.f : seq > o.seq; }
    };
    const Graph* g = nullptr;
    std::vector<Node> nodes;
    std::set<Key> open;
    int generation = 1;
    int sequence = 0;
    long long expanded = 0;
    // coverage
    long long tiePops = 0, rekeys = 0, reopens = 0, fDrops = 0;

    void Relax(int v, int par, int gc, int f) {   // 0x14001d320
        Node& n = nodes[v];
        if (n.openStamp == generation) {
            open.erase(Key{n.fKey, n.seqKey, v});
            ++rekeys;
        }
        n.closedStamp -= 1;
        n.parent = par;
        n.openStamp = generation;
        n.gCost = gc;
        n.fKey = f;
        n.seqKey = sequence++;
        open.insert(Key{f, n.seqKey, v});
    }
    bool FindPath(int start, int goal) {          // 0x14001dcd0
        open.clear();
        sequence = 0;
        expanded = 0;
        Relax(start, -1, 0, Dist(*g, start, goal));
        int lastF = -1;
        for (;;) {
            if (open.empty()) return false;
            auto it = open.begin();
            const Key top = *it;
            auto nx = it; ++nx;
            if (nx != open.end() && nx->f == top.f) ++tiePops;
            if (top.f < lastF) ++fDrops;
            lastF = top.f;
            if (top.v == goal) return true;
            open.erase(it);
            Node& cur = nodes[top.v];
            cur.openStamp -= 1;
            cur.closedStamp = generation;
            ++expanded;
            for (int32_t nb : g->adj[top.v]) {
                if (nb < 0) continue;
                const int t = Dist(*g, top.v, nb) + nodes[top.v].gCost;
                Node& n = nodes[nb];
                if (n.closedStamp == generation) {
                    if (n.gCost <= t) continue;
                    n.closedStamp -= 1;
                    ++reopens;
                }
                if (n.openStamp != generation || t < n.gCost)
                    Relax(nb, top.v, t, Dist(*g, nb, goal) + t);
            }
        }
    }
};

// Run 0x14001dc00's extraction over any parent array.
template <class ParentOf>
int ExtractPath(bool found, int goal, ParentOf parentOf, int32_t* path) {
    if (!found) return 0;
    int n = 0;
    for (int v = goal; v != -1; v = parentOf(v)) ++n;
    int v = goal;
    for (int i = n - 1; i >= 0; --i) {
        if (i < 0x2000) path[i] = v;
        v = parentOf(v);
    }
    return n > 0x2000 ? 0x2000 : n;
}

int PathSum(const Graph& g, const int32_t* path, int n) {   // PathLength 0x1400a8ba0
    int s = 0;
    for (int i = 0; i + 1 < n; ++i) s += Dist(g, path[i], path[i + 1]);
    return n < 2 ? 0 : s;
}

// The production record, raw (retail: MemAlloc + Init 0x1400a8670; never a C++ ctor).
struct Production {
    RegionGraph* rg = nullptr;
    RegionNode* regions = nullptr;
    PathSearchNode* nodes = nullptr;
    int n = 0;

    explicit Production(Graph& g) {
        n = g.Size();
        rg = static_cast<RegionGraph*>(std::calloc(1, sizeof(RegionGraph)));
        regions = static_cast<RegionNode*>(std::calloc(n, sizeof(RegionNode)));
        nodes = static_cast<PathSearchNode*>(std::calloc(n, sizeof(PathSearchNode)));
        for (int i = 0; i < n; ++i) {
            regions[i].x = g.x[i];
            regions[i].y = g.y[i];
            regions[i].neighbours._data = g.adj[i].empty() ? nullptr : g.adj[i].data();
            regions[i].neighbours._size = static_cast<int32_t>(g.adj[i].size());
            nodes[i].parent = -1;   // 0x14001d830's fill {0, -1, 0, 0, 0}
        }
        rg->search.generation = 1;
        rg->search.nodes.data = nodes;
        rg->search.nodes.size = n;
        rg->search.nodes.capacity = n;
        rg->regions.data = regions;
        rg->regions.size = n;
        rg->regions.capacity = n;
    }
    ~Production() {
        std::free(nodes);
        std::free(regions);
        std::free(rg);
    }
    bool Run(int start, int goal) {
        rg->search.generation += 1;   // Run 0x14001dc00
        return static_cast<char>(rg->FindPath(start, goal)) != '\0';
    }
};

struct Tally {
    long long graphs = 0, queries = 0, found = 0, notFound = 0, mismatches = 0;
    long long expansions = 0, maxOpen = 0, longestPath = 0;
    long long tiePops = 0, rekeys = 0, reopens = 0, fDrops = 0;
};

// One graph, `queries` queries on one persistent state; adds the mismatches to t.
void RunGraph(Graph& g, Rng& r, int queries, Tally& t, const char* label) {
    Production prod(g);
    RefSearch ref;
    ref.g = &g;
    ref.nodes.assign(g.Size(), RefSearch::Node{});
    std::vector<int32_t> pRef(0x2000), pProd(0x2000);
    ++t.graphs;
    for (int q = 0; q < queries; ++q) {
        const int start = r.Below(g.Size());
        const int goal = r.Chance(5) ? start : r.Below(g.Size());
        ref.generation += 1;
        const bool fRef = ref.FindPath(start, goal);
        const bool fProd = prod.Run(start, goal);
        const int nRef = ExtractPath(fRef, goal, [&](int v) { return ref.nodes[v].parent; }, pRef.data());
        const int nProd = ExtractPath(fProd, goal, [&](int v) { return prod.nodes[v].parent; }, pProd.data());
        bool ok = fRef == fProd && nRef == nProd &&
                  PathSum(g, pRef.data(), nRef) == PathSum(g, pProd.data(), nProd) &&
                  ref.sequence == prod.rg->search.sequence &&
                  static_cast<int>(ref.open.size()) == prod.rg->search.open.count &&
                  ref.expanded == RegionSearch::LastExpanded() &&
                  ref.generation == prod.rg->search.generation;
        for (int i = 0; ok && i < nRef; ++i) ok = pRef[i] == pProd[i];
        for (int v = 0; ok && v < g.Size(); ++v) {
            const RefSearch::Node& a = ref.nodes[v];
            const PathSearchNode& b = prod.nodes[v];
            ok = a.parent == b.parent && a.openStamp == b.openStamp &&
                 a.closedStamp == b.closedStamp && a.gCost == b.gCost;
        }
        ++t.queries;
        if (fRef) ++t.found; else ++t.notFound;
        t.expansions += ref.expanded;
        if (static_cast<long long>(ref.open.size()) > t.maxOpen) t.maxOpen = static_cast<long long>(ref.open.size());
        if (nRef > t.longestPath) t.longestPath = nRef;
        if (!ok) {
            if (t.mismatches == 0)
                std::printf("  first mismatch (%s): graph %lld query %d n %d start %d goal %d "
                            "found %d/%d len %d/%d seq %d/%d open %d/%d exp %lld/%d\n",
                            label, t.graphs, q, g.Size(), start, goal, fRef, fProd, nRef, nProd,
                            ref.sequence, prod.rg->search.sequence,
                            static_cast<int>(ref.open.size()), prod.rg->search.open.count,
                            ref.expanded, RegionSearch::LastExpanded());
            ++t.mismatches;
        }
    }
    t.tiePops += ref.tiePops;
    t.rekeys += ref.rekeys;
    t.reopens += ref.reopens;
    t.fDrops += ref.fDrops;
}

void AddEdge(Graph& g, int a, int b, bool both) {
    g.adj[a].push_back(b);
    if (both) g.adj[b].push_back(a);
}

// Like the game's graph (RegionGraph: 8x8-cell blocks, 1-4 regions per block, centroids in
// cells inside their block, neighbours in the same or the 8 adjacent blocks). Integer
// centroids on a small lattice: equal distances, equal f, ties everywhere.
Graph MakeBlockGraph(Rng& r, int blocksWide) {
    Graph g;
    std::vector<std::vector<int>> inBlock(blocksWide * blocksWide);
    for (int by = 0; by < blocksWide; ++by)
        for (int bx = 0; bx < blocksWide; ++bx) {
            const int k = r.Chance(10) ? 0 : 1 + r.Below(r.Chance(70) ? 1 : 4);
            for (int i = 0; i < k; ++i) {
                inBlock[by * blocksWide + bx].push_back(g.Size());
                g.x.push_back(bx * 8 + (r.Chance(60) ? 3 + r.Below(2) : r.Below(8)));
                g.y.push_back(by * 8 + (r.Chance(60) ? 3 + r.Below(2) : r.Below(8)));
                g.adj.emplace_back();
            }
        }
    for (int by = 0; by < blocksWide; ++by)
        for (int bx = 0; bx < blocksWide; ++bx)
            for (int a : inBlock[by * blocksWide + bx])
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dy == 0 && dx <= 0) continue;
                        const int nx = bx + dx, ny = by + dy;
                        if (nx < 0 || ny < 0 || nx >= blocksWide || ny >= blocksWide) continue;
                        for (int b : inBlock[ny * blocksWide + nx])
                            if (r.Chance(dx != 0 && dy != 0 ? 35 : 75)) AddEdge(g, a, b, true);
                    }
    // same-block pairs
    for (auto& blk : inBlock)
        for (size_t i = 0; i < blk.size(); ++i)
            for (size_t j = i + 1; j < blk.size(); ++j)
                if (r.Chance(40)) AddEdge(g, blk[i], blk[j], true);
    if (g.Size() == 0) { g.x.push_back(0); g.y.push_back(0); g.adj.emplace_back(); }
    return g;
}

// Anything the search must survive: a tiny coordinate range (duplicate centroids,
// zero-length edges), directed edges, -1 entries, duplicate entries, self-loops, long
// edges across the map.
Graph MakeWildGraph(Rng& r, int n, int range) {
    Graph g;
    for (int i = 0; i < n; ++i) {
        g.x.push_back(r.Below(range));
        g.y.push_back(r.Below(range));
        g.adj.emplace_back();
    }
    const int edges = n * (1 + r.Below(4));
    for (int e = 0; e < edges; ++e) {
        const int a = r.Below(n), b = r.Below(n);
        AddEdge(g, a, b, r.Chance(70));
        if (r.Chance(5)) g.adj[a].push_back(-1);
        if (r.Chance(5)) g.adj[a].push_back(b);    // duplicate
        if (r.Chance(3)) g.adj[a].push_back(a);    // self-loop
    }
    return g;
}

void Report(const char* label, const Tally& t) {
    std::printf("  %s: %lld graphs, %lld queries (%lld found, %lld no path), %lld mismatches; "
                "expansions %lld, max open %lld, longest path %lld; tie-decided pops %lld, "
                "re-keys %lld, reopened closed %lld, f drops %lld\n",
                label, t.graphs, t.queries, t.found, t.notFound, t.mismatches, t.expansions,
                t.maxOpen, t.longestPath, t.tiePops, t.rekeys, t.reopens, t.fDrops);
}

}  // namespace

UNIT_TEST(region_search_block_graphs_identical_to_retail) {
    Rng r(0x5EED0001);
    Tally t;
    for (int i = 0; i < 400; ++i) {
        Graph g = MakeBlockGraph(r, 2 + r.Below(i < 380 ? 14 : 40));
        RunGraph(g, r, 16, t, "block");
    }
    Report("block graphs", t);
    CHECK_EQ(t.mismatches, 0LL);
    CHECK(t.found > 2000);
    CHECK(t.notFound > 50);
    CHECK(t.tiePops > 10000);
    CHECK(t.rekeys > 1000);
}

UNIT_TEST(region_search_wild_graphs_identical_to_retail) {
    Rng r(0x5EED0002);
    Tally t;
    for (int i = 0; i < 400; ++i) {
        const int n = 2 + r.Below(i % 4 == 0 ? 700 : 150);
        Graph g = MakeWildGraph(r, n, r.Chance(50) ? 6 + r.Below(10) : 200 + r.Below(800));
        RunGraph(g, r, 16, t, "wild");
    }
    Report("wild graphs", t);
    CHECK_EQ(t.mismatches, 0LL);
    CHECK(t.found > 1000);
    CHECK(t.notFound > 100);
    CHECK(t.reopens > 30);   // the inconsistent heuristic: closed vertices reopened
    CHECK(t.fDrops > 100);    // ... and pops whose f is below the previous pop's
}

// A long serpentine corridor on a large map: f in the thousands (the bucket array grows
// mid-search) and the path clamp: more than 0x2000 regions in one path.
UNIT_TEST(region_search_long_corridor_and_clamp) {
    Graph g;
    const int rows = 90, cols = 100;   // 9000 regions, one path through all of them
    for (int row = 0; row < rows; ++row)
        for (int c = 0; c < cols; ++c) {
            const int col = (row % 2 == 0) ? c : cols - 1 - c;
            g.x.push_back(col * 8 + 4);
            g.y.push_back(row * 8 + 4);
            g.adj.emplace_back();
        }
    for (int i = 0; i + 1 < g.Size(); ++i) AddEdge(g, i, i + 1, true);
    Production prod(g);
    RefSearch ref;
    ref.g = &g;
    ref.nodes.assign(g.Size(), RefSearch::Node{});
    std::vector<int32_t> pRef(0x2000), pProd(0x2000);
    const int queries[][2] = { {0, g.Size() - 1}, {g.Size() - 1, 0}, {17, 8500}, {4000, 4001}, {5, 5} };
    long long bad = 0;
    for (const auto& qv : queries) {
        ref.generation += 1;
        const bool fRef = ref.FindPath(qv[0], qv[1]);
        const bool fProd = prod.Run(qv[0], qv[1]);
        const int nRef = ExtractPath(fRef, qv[1], [&](int v) { return ref.nodes[v].parent; }, pRef.data());
        const int nProd = ExtractPath(fProd, qv[1], [&](int v) { return prod.nodes[v].parent; }, pProd.data());
        bool ok = fRef && fProd && nRef == nProd && ref.sequence == prod.rg->search.sequence &&
                  ref.expanded == RegionSearch::LastExpanded() &&
                  PathSum(g, pRef.data(), nRef) == PathSum(g, pProd.data(), nProd);
        for (int i = 0; ok && i < nRef; ++i) ok = pRef[i] == pProd[i];
        if (!ok) ++bad;
    }
    std::printf("  corridor: 9000 regions, 5 queries, %lld mismatches, first path clamped to %d, "
                "scratch %lld bytes\n", bad, 0x2000, static_cast<long long>(RegionSearch::ScratchBytes()));
    CHECK_EQ(bad, 0LL);
}

// HITCH_PAIRSPREAD: the PathLength memo (region_search.h). Stores and finds by
// (graph, start, goal) in order, misses after a graph change or for another graph, and
// keeps every entry across the table's growth (2^14 -> 2^19 slots).
UNIT_TEST(region_search_path_length_memo) {
    static int graphA = 0, graphB = 0;
    RegionSearch::NoteGraphChanged();
    int sum = -1;
    CHECK(!RegionSearch::MemoFind(&graphA, 3, 7, &sum));
    RegionSearch::MemoStore(&graphA, 3, 7, 41);
    CHECK(RegionSearch::MemoFind(&graphA, 3, 7, &sum));
    CHECK_EQ(sum, 41);
    CHECK(!RegionSearch::MemoFind(&graphA, 7, 3, &sum));   // ordered: direction matters
    RegionSearch::NoteGraphChanged();
    CHECK(!RegionSearch::MemoFind(&graphA, 3, 7, &sum));
    RegionSearch::MemoStore(&graphA, 3, 7, 42);
    CHECK(!RegionSearch::MemoFind(&graphB, 3, 7, &sum));   // another graph drops it
    CHECK(!RegionSearch::MemoFind(&graphA, 3, 7, &sum));
    // Growth: 200k entries (more than the 2^19 / 2 cap is NOT reached), all found.
    RegionSearch::NoteGraphChanged();
    const int n = 200000;
    for (int i = 0; i < n; ++i) RegionSearch::MemoStore(&graphA, i, i * 7 + 1, i ^ 0x5a5a);
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        if (!RegionSearch::MemoFind(&graphA, i, i * 7 + 1, &sum) || sum != (i ^ 0x5a5a)) ++bad;
    }
    CHECK_EQ(bad, 0);
    CHECK(!RegionSearch::MemoFind(&graphA, n, n * 7 + 1, &sum));
    // Past the cap the memo starts over: the newest entry is still there.
    for (int i = n; i < 300000; ++i) RegionSearch::MemoStore(&graphA, i, 1, i);
    CHECK(RegionSearch::MemoFind(&graphA, 299999, 1, &sum));
    CHECK_EQ(sum, 299999);
    CHECK(RegionSearch::Stats().full >= 1);
    RegionSearch::NoteGraphChanged();
}
