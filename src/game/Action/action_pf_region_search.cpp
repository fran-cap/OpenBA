// src/game/Action/action_pf_region_search.cpp
// 0x14001dcd0 RegionGraph::FindPath -- the region A* (Map::PathDistance -> PathLength
// 0x1400a8ba0 -> Run 0x14001dc00 -> here), over a bucket open set instead of retail's AVL
// PathOpenSet. FAITHFUL PERF WORK (PATHDIST, docs/extensions/handins/PATHDIST.md): same
// pops in the same order, so the same node stamps, parents, g costs, `search.sequence`,
// `search.open.count`, region path and PathLength sum as retail. No toggle.
//
// RETAIL'S POP ORDER (action_pf_astar.cpp):
//   PathOpenSet::Insert 0x14001d450 descends LEFT while
//       key.fcost < node.fcost  ||  (key.fcost == node.fcost && node.seq < key.seq)
//   and the pop is the leftmost node (FindPath's `descend leftmost` loop). The key is
//   unique (seq is the search's insert counter, PathSearch::Relax 0x14001d320), so the
//   open set is a strict total order and the leftmost node is its minimum:
//       the smallest fcost, and among equal fcost the LARGEST seq (the newest insert).
//   Relax re-keys an already-open vertex by deleting its old entry (the key walk ->
//   PathOpenSet::Delete 0x14001d550) and inserting a new one with a new seq; the pop
//   deletes the popped entry unless it is the goal (FindPath returns with it in the set).
//
// THE BUCKET SET reproduces that order exactly:
//   - one LIFO stack per integer f, so the top of a bucket is its newest push = the
//     largest seq (seq grows by one per push, nothing is pushed out of seq order);
//   - a cursor at the lowest non-empty bucket; a push below it moves it down (the region
//     heuristic is a truncated Euclidean distance, NOT consistent: f can drop by one or
//     two per edge, so the cursor is not monotone and the set must not assume it is);
//   - a re-key leaves the old entry where it is (lazy delete) and the pop skips it. An
//     entry is LIVE iff its vertex is open in this generation and its g is the vertex's
//     current gCost: every relaxation of a vertex already reached in this search needs a
//     STRICTLY smaller g (the open case `t < gCost`, the closed case `gCost <= t` skips),
//     so a vertex's stale entries all carry a larger g than its live one, and a closed
//     vertex (openStamp decremented by the pop) has none live.
//   Popping the first live entry from the lowest bucket's top = retail's leftmost node.
//
// What the AVL set left that this one does not: `search.open.root` / `pool` stay as they
// were (null / the pool as built, unless the COMPPROF=4 shadow FindPathAvl left a tree:
// its own entry releases it next time), and `PathSearchNode::openNode` is not written
// (only Relax 0x14001d320 reads it, in the AVL path, for vertices it opened itself).
// `search.open.count` is kept exactly as retail's (+1 opening, -1 popping, 0 net for a
// re-key): it is retail's loop condition here too, and the shadow compares it.
#include "Action/PathSearch.h"
#include "Action/region_search.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <vector>

namespace {

// One pushed open-set entry; `next` is the entry below it in its bucket's stack.
struct RegionOpenEntry {
    int32_t vertex;
    int32_t g;
    int32_t next;   // -1 = bottom of the stack
};

// The region search's open set. Process-lifetime scratch (RegionGraph is a pinned 0x108
// record with no room for it); RegionGraph's search runs on the sim thread only, as its
// own `search` member already requires.
struct RegionOpenBuckets {
    std::vector<int32_t>         head;   // per f: top entry index into `pool`, -1 = empty
    std::vector<RegionOpenEntry> pool;   // entries of the current search, never freed between searches
    int32_t cursor = INT_MAX;            // lowest bucket that may be non-empty
    int32_t lo = INT_MAX;                // touched f range of the current search
    int32_t hi = -1;
    int32_t expanded = 0;                // pops that closed a vertex (the goal pop excluded)

    void Reset() {
        if (hi >= lo) {
            std::fill(head.begin() + lo, head.begin() + hi + 1, -1);
        }
        lo = INT_MAX;
        hi = -1;
        cursor = INT_MAX;
        pool.clear();
        expanded = 0;
    }

    // f >= 0: g is a sum of truncated non-negative distances and h one more (the map's
    // centroid coordinates keep every square far below 2^31).
    void Push(int32_t f, int32_t vertex, int32_t g) {
        if (f >= static_cast<int32_t>(head.size())) {
            size_t grown = head.size() * 2;
            if (grown < static_cast<size_t>(f) + 1) grown = static_cast<size_t>(f) + 1;
            if (grown < 256) grown = 256;
            head.resize(grown, -1);
        }
        const int32_t index = static_cast<int32_t>(pool.size());
        pool.push_back(RegionOpenEntry{vertex, g, head[f]});
        head[f] = index;
        if (f < lo) lo = f;
        if (f > hi) hi = f;
        if (f < cursor) cursor = f;
    }

    // The first live entry from the lowest bucket's top: retail's leftmost node. The
    // caller has search.open.count >= 1, so one exists (every open vertex has exactly one
    // live entry) and the scan stays inside [cursor, hi].
    int32_t PopLive(const PathSearchNode* nodes, int32_t generation) {
        for (;;) {
            const int32_t index = head[cursor];
            if (index < 0) {
                ++cursor;
                continue;
            }
            const RegionOpenEntry& e = pool[index];
            head[cursor] = e.next;
            const PathSearchNode& n = nodes[e.vertex];
            if (n.openStamp == generation && n.gCost == e.g) {
                return e.vertex;
            }
        }
    }
};

RegionOpenBuckets s_regionOpen;

// NOT RETAIL (faithful perf): each region's edges as the search reads them, in one contiguous
// pool: the neighbour id, the edge length int(sqrtf(dy*dy + dx*dx)) between the two centroids
// (the very expression FindPath evaluates per edge), and the neighbour's centroid (the heuristic's
// input). Negative neighbour ids are left out: the search skips them with no other effect.
// The values depend on the centroids and neighbour lists only, and every write to those happens
// inside RegionGraph::Flush 0x1400a8a90, which ends in NoteGraphChanged (region_search.h); so a
// region's slice stays valid until the graph epoch moves (or another graph, a moved or resized
// region array asks). A slice is built the first time the search expands its region.
struct RegionEdge {
    int32_t vertex;   // the neighbour id (>= 0)
    int32_t length;   // int(sqrtf(dy*dy + dx*dx)), u's centroid minus the neighbour's
    int32_t x;        // the neighbour's centroid
    int32_t y;
};

uint64_t s_graphEpoch = 0;   // bumped by NoteGraphChanged

struct RegionAdjacency {
    const void*       graph = nullptr;
    const RegionNode* data = nullptr;
    int32_t           size = -1;
    uint64_t          epoch = ~0ull;
    std::vector<uint8_t>    built;   // per region: its slice is in `edges`
    std::vector<int32_t>    first;   // per region: its slice's start in `edges`
    std::vector<int32_t>    count;   // per region: its slice's length
    std::vector<RegionEdge> edges;

    void Sync(const void* g, const RegionNode* d, int32_t n) {
        if (g == graph && d == data && n == size && epoch == s_graphEpoch) {
            return;
        }
        graph = g;
        data = d;
        size = n;
        epoch = s_graphEpoch;
        const size_t slots = n > 0 ? static_cast<size_t>(n) : 0;
        built.assign(slots, 0);
        first.resize(slots);
        count.resize(slots);
        edges.clear();
    }

    // Region u's slice. The pointer holds until the next call (the pool may grow then).
    const RegionEdge* Edges(int32_t u, int32_t* n) {
        if (built[u] == 0) {
            const RegionNode& from = data[u];
            first[u] = static_cast<int32_t>(edges.size());
            const int32_t* nb = from.neighbours._data;
            const int32_t total = from.neighbours._size;
            for (int32_t k = 0; k < total; ++k) {
                const int32_t v = nb[k];
                if (v < 0) {
                    continue;
                }
                const RegionNode& to = data[v];
                const int dy = from.y - to.y;
                const int dx = from.x - to.x;
                const float d = __builtin_sqrtf(static_cast<float>(dy * dy + dx * dx));   // 0x140132010, inline
                edges.push_back(RegionEdge{v, static_cast<int>(d), to.x, to.y});
            }
            count[u] = static_cast<int32_t>(edges.size()) - first[u];
            built[u] = 1;
        }
        *n = count[u];
        return edges.data() + first[u];
    }
};

RegionAdjacency s_regionAdj;

}  // namespace

namespace RegionSearch {
int LastExpanded() { return s_regionOpen.expanded; }
size_t ScratchBytes() {
    return s_regionOpen.head.capacity() * sizeof(int32_t) +
           s_regionOpen.pool.capacity() * sizeof(RegionOpenEntry);
}
}  // namespace RegionSearch

// ============================================================
// 0x14001dcd0 — A* over the REGION graph (Euclidean h between centroids). (Ghidra:
// ..._dcd0.) The graph is the region node array (centroid +0x00 / +0x04, neighbour list
// +0x10). h = sqrtf(dx^2+dy^2) via 0x140132010 (inlined: sqrtss, bit-identical).
// Every store to the search nodes and the counters is retail's, in retail's order; the
// open-set calls (Insert / Delete / the leftmost descent) are the bucket set's.
// Returns retail's RAX low byte: 1 found, 0 not (the caller 0x14001dc00 tests the low byte).
// ============================================================
uint64_t RegionGraph::FindPath(int start, int goal) {
    RegionOpenBuckets& open = s_regionOpen;
    open.Reset();
    search.open.count = 0;
    const RegionNode* regions14 = regions.data;
    search.sequence = 0;
    const int64_t lVar17 = static_cast<int64_t>(goal);   // the goal, fixed at entry (retail's lVar17)
    int iVar8 = regions14[start].y - regions14[lVar17].y;
    int iVar12 = regions14[start].x - regions14[lVar17].x;
    float fVar20 = __builtin_sqrtf(static_cast<float>(iVar8 * iVar8 + iVar12 * iVar12));   // PERF: 0x140132010 is our sqrtf; bit-identical inline
    s_regionAdj.Sync(this, regions.data, regions.size);   // NOT RETAIL: the edge pool's validity
    const RegionNode& goalNode = regions14[lVar17];

    // PathSearch::Relax 0x14001d320, the open-set half replaced: the vertex node's stores
    // in retail's order, then the push (retail: delete the old entry, insert the new key).
    PathSearchNode* const nodes = search.nodes.data;
    const int32_t generation = search.generation;
    auto relax = [&](int vertex, int parentVertex, int gCost, int fCost) {
        PathSearchNode* plVar1 = &nodes[vertex];
        if (plVar1->openStamp != generation) {
            search.open.count = search.open.count + 1;   // a new entry (a re-key nets 0)
        }
        plVar1->closedStamp = plVar1->closedStamp + -1;
        plVar1->parent = parentVertex;
        plVar1->openStamp = generation;
        plVar1->gCost = gCost;
        search.sequence = search.sequence + 1;
        open.Push(fCost, vertex, gCost);
    };

    relax(start, -1, 0, static_cast<int>(fVar20));
    for (;;) {
        if (search.open.count < 1) {
            return 0;
        }
        iVar8 = open.PopLive(nodes, generation);   // the leftmost node
        const int64_t lVar14 = static_cast<int64_t>(iVar8);
        if (iVar8 == goal) {
            return 1;
        }
        search.open.count = search.open.count + -1;   // PathOpenSet::Delete 0x14001d550
        open.expanded = open.expanded + 1;
        PathSearchNode* cur = &nodes[lVar14];
        cur->openStamp = cur->openStamp + -1;
        cur->closedStamp = generation;
        // PERF (NOT RETAIL): the region's edges from the adjacency pool (s_regionAdj above): the same
        // neighbours in the same order, negative ids left out, each with the edge length retail
        // computes here (`int(sqrtf(dy*dy + dx*dx))`, u's centroid minus the neighbour's) and the
        // neighbour's centroid. cur->gCost is read once: no relaxation in this loop can write it (a
        // self edge has length >= 0 and cur is closed this generation, so `gCost <= t` skips it).
        const int32_t goalX = goalNode.x;
        const int32_t goalY = goalNode.y;
        const int curG = cur->gCost;
        int32_t uVar11 = 0;
        const RegionEdge* nb = s_regionAdj.Edges(iVar8, &uVar11);
        for (int32_t lVar15 = 0; lVar15 < uVar11; ++lVar15) {
            const RegionEdge& e = nb[lVar15];
            const int iVar12n = e.vertex;
            int iVar9 = e.length + curG;
            PathSearchNode* n = &nodes[iVar12n];
            if (n->closedStamp == generation) {
                if (n->gCost <= iVar9) continue;
                n->closedStamp = n->closedStamp + -1;
            }
            if ((n->openStamp != generation) || (iVar9 < n->gCost)) {
                const int iVar13 = e.y - goalY;
                const int iVar16 = e.x - goalX;
                fVar20 = __builtin_sqrtf(static_cast<float>(iVar13 * iVar13 + iVar16 * iVar16));   // 0x140132010, inline
                relax(iVar12n, iVar8, iVar9, static_cast<int>(fVar20) + iVar9);
            }
        }
    }
}

// ============================================================
// NOT RETAIL -- HITCH_PAIRSPREAD: the region path-length memo (region_search.h).
// Open addressing, linear probing; a slot is live when its stamp is the current one, so
// dropping the memo is one increment. The table doubles from 2^14 up to 2^19 slots
// (16 bytes each, 8 MB at the cap) while it is under half full, then starts over.
// ============================================================
namespace {

struct MemoSlot {
    uint64_t key;
    int32_t  sum;
    uint32_t stamp;
};

struct PathLengthMemo {
    std::vector<MemoSlot> slots;
    uint32_t    stamp = 1;
    int         bits = 0;
    int         live = 0;
    const void* owner = nullptr;
    RegionSearch::MemoStats stats{};

    static uint64_t Key(int start, int goal) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(start)) << 32) | static_cast<uint32_t>(goal);
    }
    size_t Home(uint64_t key) const {
        return static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> (64 - bits));
    }
    void Drop() {
        live = 0;
        if (++stamp == 0) {   // wrapped: no slot may keep a stale stamp that matches
            for (MemoSlot& s : slots) s.stamp = 0;
            stamp = 1;
        }
    }
    void Own(const void* graph) {
        if (graph != owner) {
            owner = graph;
            Drop();
        }
    }
    void Grow() {
        std::vector<MemoSlot> old;
        old.swap(slots);
        const uint32_t oldStamp = stamp;
        bits = bits == 0 ? 14 : bits + 1;
        slots.assign(static_cast<size_t>(1) << bits, MemoSlot{0, 0, 0});
        stamp = 1;
        live = 0;
        const size_t mask = slots.size() - 1;
        for (const MemoSlot& s : old) {
            if (s.stamp != oldStamp) continue;
            size_t i = Home(s.key);
            while (slots[i].stamp == stamp) i = (i + 1) & mask;
            slots[i] = MemoSlot{s.key, s.sum, stamp};
            ++live;
        }
    }
};

PathLengthMemo s_memo;

}  // namespace

namespace RegionSearch {

const MemoStats& Stats() { return s_memo.stats; }

void NoteGraphChanged() {
    ++s_graphEpoch;   // FindPath's edge pool (s_regionAdj) is rebuilt slice by slice from here on
    ++s_memo.stats.graphChanges;
    s_memo.Drop();
}

bool MemoFind(const void* graph, int start, int goal, int* sum) {
    PathLengthMemo& m = s_memo;
    ++m.stats.lookups;
    m.Own(graph);
    if (m.bits == 0 || m.live == 0) return false;
    const uint64_t key = PathLengthMemo::Key(start, goal);
    const size_t mask = m.slots.size() - 1;
    for (size_t i = m.Home(key);; i = (i + 1) & mask) {
        const MemoSlot& s = m.slots[i];
        if (s.stamp != m.stamp) return false;
        if (s.key == key) {
            *sum = s.sum;
            ++m.stats.hits;
            return true;
        }
    }
}

void MemoStore(const void* graph, int start, int goal, int sum) {
    PathLengthMemo& m = s_memo;
    m.Own(graph);
    if (m.bits == 0 || (m.live + 1) * 2 > static_cast<int>(m.slots.size())) {
        if (m.bits < 19) {
            m.Grow();
        } else {
            ++m.stats.full;
            m.Drop();
        }
    }
    const uint64_t key = PathLengthMemo::Key(start, goal);
    const size_t mask = m.slots.size() - 1;
    size_t i = m.Home(key);
    while (m.slots[i].stamp == m.stamp) {
        if (m.slots[i].key == key) {   // a verify-mode re-store of a hit
            m.slots[i].sum = sum;
            return;
        }
        i = (i + 1) & mask;
    }
    m.slots[i] = MemoSlot{key, sum, m.stamp};
    ++m.live;
}

}  // namespace RegionSearch
