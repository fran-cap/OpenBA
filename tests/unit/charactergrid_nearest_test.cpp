// ============================================================================
// tests/unit/charactergrid_nearest_test.cpp -- BESTTARGET: CharacterGrid::QueryNearestCharacter's
// fast walk (src/game/Component/charactergrid_nearest.h) against retail's walker body
// ============================================================================
// The production template CharGridNearest::Query runs here on a test grid (a record with
// CharacterGrid's arrays: slots with circular `prev` lists, cellHead, cellOccupancy, worldSize,
// levels). The reference below is retail's body line for line: CharacterGridWalker::Init
// 0x140018dd0 (src/game/Action/action_pf_geom.cpp, the 256-node clear included),
// CharacterGridWalker::Subdivide 0x140018880 (same file) and the query loop of 0x140092260
// (CharacterGrid::QueryNearestCharacterRetail, src/game/Action/action_pf_roots_misc.cpp:
// pop-time occupancy and box tests, the cell list walked through `prev` from the head, the
// exclude / type filter, Vector3::Dot(diff, diff) and the strict `dsq < best`). (The real
// bodies cannot be linked here: their TUs pull in the game; the runtime layer of the proof,
// the real retail body beside the fast one, is the COMPPROF=4 shadow VerifyNearest.)
//
// The grids are built to make the ORDER matter: characters on a coarse lattice (many equal
// distances: the first visited wins), duplicate positions, stale cells (a character listed in
// a cell its position is not in, as between two Place calls), characters at every level,
// occupancy counts that are sometimes inconsistent with the lists, odd world sizes (inexact
// float centers), z values around the walker's -1 / +1 pads, the type filter and the exclude.
// Compared after every query: the returned character (pointer identity).
// A mutation test proves the comparison has teeth: the reference with its tie rule flipped
// (`<=`) or its child order flipped disagrees with the fast walk (more than 500 and more than
// 30 times: the test's floors).
// ============================================================================
#include "unit.h"
#include "Component/charactergrid_nearest.h"
#include "math/Math.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s >> 11); }
    int Below(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
    bool Chance(int percent) { return Below(100) < percent; }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * (static_cast<float>(Next() & 0xffffff) / 16777216.0f); }
};

struct TChar {
    Math::Vector3 pos;
    int type;
};

struct TSlot {               // CharacterGridSlot's fields the query reads
    TChar* character;
    int32_t prev;
    int32_t next;
    int32_t cell;
};

struct TGrid {
    float worldSize = 0.0f;
    int levels = 0;
    std::vector<TSlot> slots;
    std::vector<int32_t> cellHead;
    std::vector<int32_t> cellOccupancy;
};

struct TPolicy {
    using Char = TChar;
    static TChar* Get(const TSlot& s) { return s.character; }
    static const Math::Vector3& Pos(TChar* c) { return c->pos; }
    static bool Keep(TChar* c, int typeId) { return c->type == typeId; }
    static void Prefetch(TChar* c) { __builtin_prefetch(c); }
    static void PrefetchPos(TChar* c) { __builtin_prefetch(&c->pos); }
};

// ---------------------------------------------------------------------------
// The reference: retail's walker and query loop.
// ---------------------------------------------------------------------------
struct RefNode {
    float center[3];
    float extent[3];
    int32_t cell;
    int32_t depth;
    uint32_t flag;
};

struct RefWalker {
    int32_t top;
    float padLo[3];
    float padHi[3];
    RefNode nodes[256];
    const TGrid* grid;
};

int g_mutation = 0;   // 0 = retail; 1 = ties go to the later candidate; 2 = children pushed 4..1

// 0x140018dd0
void RefInit(RefWalker& w, const TGrid* root, float lo, float hi) {
    w.top = -1;
    w.padLo[2] = lo;
    w.padLo[0] = 0.0f;
    w.padLo[1] = 0.0f;
    w.padHi[0] = 0.0f;
    w.padHi[1] = 0.0f;
    w.padHi[2] = hi;
    for (int i = 0; i < 256; ++i) {
        w.nodes[i].center[0] = 0.0f;
        w.nodes[i].center[1] = 0.0f;
        w.nodes[i].center[2] = 0.0f;
        w.nodes[i].extent[0] = 0.0f;
        w.nodes[i].extent[1] = 0.0f;
        w.nodes[i].extent[2] = 0.0f;
    }
    w.grid = root;
    float zero = 0.0f;
    float fVar4 = root->worldSize * 0.5f;
    Math::Vector3 c(zero, zero, zero);   // Vector3(Scalar(0)) splat
    w.top = w.top + 1;
    RefNode& n = w.nodes[w.top];
    n.center[0] = c.x;
    n.center[1] = c.y;
    n.center[2] = c.z;
    n.extent[0] = fVar4;
    n.extent[1] = fVar4;
    n.extent[2] = 0.0f;
    n.cell = 0;
    n.depth = 0;
    n.flag = 0;
}

void RefPush(RefWalker& w, float cx, float cy, float cz, float ex, float ey, float ez, int cell, int depth) {
    w.top = w.top + 1;
    RefNode* n = &w.nodes[w.top];
    n->center[0] = cx; n->center[1] = cy; n->center[2] = cz;
    n->extent[0] = ex; n->extent[1] = ey; n->extent[2] = ez;
    n->cell = cell; n->depth = depth; n->flag = 0;
}

// 0x140018880
void RefSubdivide(RefWalker& w) {
    const float c_half = 0.5f;
    const float c_negone = -1.0f;
    int iVar5 = w.top;
    w.top = iVar5 - 1;
    const RefNode parent = w.nodes[iVar5];
    int iVar9 = parent.depth + 1;
    if (iVar9 < w.grid->levels) {
        int iVar6 = parent.cell;
        float fVar1 = parent.center[2];
        float fVar2 = parent.extent[2];
        float fVar3 = parent.center[0];
        float fVar4 = parent.center[1];
        float fVar13 = parent.extent[0];
        float fVar12 = parent.extent[1] * c_half;
        w.top = iVar5 - 1;
        fVar13 = fVar13 * c_half;
        float fVar10 = fVar12 * c_negone;
        float fVar11 = fVar13 * c_negone;
        float k[4][3];
        k[0][0] = fVar3 + fVar11; k[0][1] = fVar4 + fVar10; k[0][2] = fVar1 + fVar2;
        Math::Vector3 A(fVar13, fVar10, fVar2);
        Math::Vector3 B(A.x + fVar3, A.y + fVar4, A.z + fVar1);
        k[1][0] = B.x; k[1][1] = B.y; k[1][2] = B.z;
        B = Math::Vector3(fVar11, fVar12, fVar2);
        A = Math::Vector3(B.x + fVar3, B.y + fVar4, B.z + fVar1);
        k[2][0] = A.x; k[2][1] = A.y; k[2][2] = A.z;
        float fVar10b = 1.0f;
        Math::Vector3 scaleVec(fVar10b, fVar10b, fVar10b);
        B = Math::Vector3(fVar13 * scaleVec.x, fVar12 * scaleVec.y, fVar2 * scaleVec.z);
        A = Math::Vector3(B.x + fVar3, B.y + fVar4, B.z + fVar1);
        k[3][0] = A.x; k[3][1] = A.y; k[3][2] = A.z;
        for (int j = 0; j < 4; ++j) {
            const int c = g_mutation == 2 ? 3 - j : j;
            RefPush(w, k[c][0], k[c][1], k[c][2], fVar13, fVar12, fVar2, iVar6 * 4 + 1 + c, iVar9);
        }
    }
}

struct RefStats {
    long long ties = 0;        // a candidate at exactly the current best distance (< r^2): order decides
    long long candidates = 0;
    long long nodes = 0;
};

// 0x140092260 (QueryNearestCharacterRetail)
void RefQuery(const TGrid& g, TChar** best, const float* param_3, const Math::Scalar* radius, int param_5,
              TChar* const* exclude, RefStats& st) {
    const float fUV = 1.0f;
    const float fNeg1 = -1.0f;
    const float f2 = 2.0f;
    static RefWalker walker;   // 9 KB; static keeps the test's stack small
    RefInit(walker, &g, fNeg1, fUV);
    float fr = radius->v;
    *best = nullptr;
    float fr2 = fr * fr;
    Math::Vector3 vmin(*radius);
    Math::Vector3 vmax(*radius);
    Math::Vector3 boxMax(vmin.x + param_3[0], vmin.y + param_3[1], vmin.z + param_3[2]);
    Math::Vector3 boxMin(param_3[0] - vmax.x, param_3[1] - vmax.y, param_3[2] - vmax.z);
    float queryBox[6] = {boxMin.x, boxMin.y, boxMin.z, boxMax.x, boxMax.y, boxMax.z};

    int top = walker.top;
    while (true) {
        int recIdx;
        int cellIdx;
        while (true) {
            if (top < 0) return;
            recIdx = top;
            cellIdx = walker.nodes[recIdx].cell;
            if (0 < g.cellOccupancy[cellIdx]) break;
            top = top - 1;
        }
        walker.top = top;
        const RefNode& node = walker.nodes[recIdx];
        Math::Vector3 scaleVec(f2, f2, fUV);
        float e0 = node.extent[0];
        float e1 = node.extent[1];
        float e2 = node.extent[2];
        Math::Vector3 ext(scaleVec.x * e0, scaleVec.y * e1, scaleVec.z * e2);
        float c0 = node.center[2];
        float c1 = node.center[1];
        float c2 = node.center[0];
        Math::Vector3 nmin(c2 - ext.x, c1 - ext.y, c0 - ext.z);
        Math::Vector3 nodeMin(walker.padLo[0] + nmin.x, walker.padLo[1] + nmin.y, walker.padLo[2] + nmin.z);
        Math::Vector3 ext2(scaleVec.x * e0, scaleVec.y * e1, scaleVec.z * e2);
        Math::Vector3 nmax(ext2.x + c2, ext2.y + c1, ext2.z + c0);
        Math::Vector3 nodeMax(walker.padHi[0] + nmax.x, walker.padHi[1] + nmax.y, walker.padHi[2] + nmax.z);
        const float* p = queryBox;
        const float q[6] = {nodeMin.x, nodeMin.y, nodeMin.z, nodeMax.x, nodeMax.y, nodeMax.z};
        if (!(p[0] <= q[3] && q[0] <= p[3] && p[1] <= q[4] && q[1] <= p[4] && p[2] <= q[5] && q[2] <= p[5])) {
            top = top - 1;
            continue;
        }
        ++st.nodes;
        int ent = g.cellHead[cellIdx];
        int headSentinel = ent;
        if (-1 < ent) {
            do {
                TChar* entObj = g.slots[ent].character;
                if (entObj != *exclude) {
                    bool dropped = false;
                    if (param_5 != 0 && entObj->type != param_5) dropped = true;
                    if (!dropped) {
                        ++st.candidates;
                        const Math::Vector3& pos = entObj->pos;
                        Math::Vector3 diff(pos.x - param_3[0], pos.y - param_3[1], pos.z - param_3[2]);
                        float dsq = diff.Dot(diff).v;
                        if (dsq == fr2 && *best != nullptr) ++st.ties;
                        if (dsq < fr2 || (g_mutation == 1 && dsq == fr2 && *best != nullptr)) {
                            *best = entObj;
                            fr2 = dsq;
                        }
                    }
                }
                ent = g.slots[ent].prev;
            } while (ent != headSentinel);
        }
        RefSubdivide(walker);
        top = walker.top;
    }
}

// ---------------------------------------------------------------------------
// Grid building
// ---------------------------------------------------------------------------
int LevelBase(int level) { int b = 0, p = 1; for (int i = 0; i < level; ++i) { b += p; p *= 4; } return b; }

// The cell at `level` whose nominal box holds (x, y), descending the walker's own geometry
// (child 1 = (-x, -y), 2 = (+x, -y), 3 = (-x, +y), 4 = (+x, +y)).
int CellOf(const TGrid& g, float x, float y, int level) {
    float cx = 0.0f, cy = 0.0f, e = g.worldSize * 0.5f;
    int cell = 0;
    for (int l = 0; l < level; ++l) {
        e = e * 0.5f;
        const int k = (x >= cx ? 1 : 0) + (y >= cy ? 2 : 0);
        cx += (k & 1) ? e : -e;
        cy += (k & 2) ? e : -e;
        cell = cell * 4 + 1 + k;
    }
    return cell;
}

void Link(TGrid& g, int idx) {   // CharacterGrid::Link 0x140018c40's list insert + occupancy walk
    TSlot* slot = &g.slots[idx];
    const int cellIdx = slot->cell;
    const int head = g.cellHead[cellIdx];
    if (head < 0) {
        g.cellHead[cellIdx] = idx;
        slot->prev = idx;
        slot->next = idx;
    } else {
        const int headNext = g.slots[head].next;
        slot->next = headNext;
        slot->prev = head;
        g.slots[headNext].prev = idx;
        g.slots[head].next = idx;
    }
    uint32_t c = static_cast<uint32_t>(slot->cell);
    g.cellOccupancy[c] += 1;
    while (c != 0) {
        c = static_cast<uint32_t>((static_cast<int>(c) - 1) >> 2);
        g.cellOccupancy[c] += 1;
    }
}

struct Scenario {
    float worldSize;
    int levels;
    int chars;
    float spread;      // characters within +/- spread of the origin cluster
    float lattice;     // 0 = continuous positions
    int stalePct;      // characters listed in a neighbouring / random cell
    int types;         // 0 = no type field use
    bool badOccupancy; // perturb occupancy counts
};

struct Totals {
    long long queries = 0, found = 0, mismatches = 0, ties = 0, candidates = 0, nodes = 0;
};

void RunScenario(const Scenario& sc, uint64_t seed, int grids, int queriesPerGrid, Totals& t) {
    Rng r(seed);
    for (int gi = 0; gi < grids; ++gi) {
        TGrid g;
        g.worldSize = sc.worldSize;
        g.levels = sc.levels;
        const int cells = LevelBase(sc.levels > 0 ? sc.levels : 1);
        g.cellHead.assign(static_cast<size_t>(cells), -1);
        g.cellOccupancy.assign(static_cast<size_t>(cells), 0);
        std::vector<TChar> chars(static_cast<size_t>(sc.chars));
        const float ox = r.Uniform(-sc.worldSize * 0.4f, sc.worldSize * 0.4f);
        const float oy = r.Uniform(-sc.worldSize * 0.4f, sc.worldSize * 0.4f);
        for (int i = 0; i < sc.chars; ++i) {
            float x, y, z;
            if (i > 0 && r.Chance(8)) {   // a duplicate position
                const TChar& o = chars[static_cast<size_t>(r.Below(i))];
                x = o.pos.x; y = o.pos.y; z = o.pos.z;
            } else {
                x = ox + r.Uniform(-sc.spread, sc.spread);
                y = oy + r.Uniform(-sc.spread, sc.spread);
                z = r.Uniform(-2.0f, 2.0f);
                if (sc.lattice > 0.0f) {
                    x = static_cast<float>(static_cast<int>(x / sc.lattice)) * sc.lattice;
                    y = static_cast<float>(static_cast<int>(y / sc.lattice)) * sc.lattice;
                    z = static_cast<float>(static_cast<int>(z / sc.lattice)) * sc.lattice;
                }
            }
            chars[static_cast<size_t>(i)].pos = Math::Vector3(x, y, z);
            chars[static_cast<size_t>(i)].type = sc.types ? 1 + r.Below(sc.types) : 0;
        }
        g.slots.resize(static_cast<size_t>(sc.chars));
        std::vector<int> order(static_cast<size_t>(sc.chars));
        for (int i = 0; i < sc.chars; ++i) order[static_cast<size_t>(i)] = i;
        for (int i = sc.chars - 1; i > 0; --i) { const int j = r.Below(i + 1); const int tmp = order[i]; order[i] = order[j]; order[j] = tmp; }
        for (int i = 0; i < sc.chars; ++i) {
            const int idx = order[static_cast<size_t>(i)];   // slot index != char index: list order is shuffled
            TSlot& s = g.slots[static_cast<size_t>(idx)];
            s.character = &chars[static_cast<size_t>(i)];
            const TChar& c = chars[static_cast<size_t>(i)];
            int level = sc.levels > 0 ? sc.levels - 1 : 0;
            if (r.Chance(15)) level = r.Below(sc.levels > 0 ? sc.levels : 1);   // a bigger character
            float px = c.pos.x, py = c.pos.y;
            if (r.Chance(sc.stalePct)) {   // listed where it was, not where it is
                px += r.Uniform(-6.0f, 6.0f);
                py += r.Uniform(-6.0f, 6.0f);
            }
            s.cell = CellOf(g, px, py, level);
            Link(g, idx);
        }
        if (sc.badOccupancy) {
            for (int k = 0; k < 40; ++k) {
                const int c = r.Below(cells);
                g.cellOccupancy[static_cast<size_t>(c)] = r.Chance(50) ? 0 : r.Below(3) - 1;
            }
        }

        for (int qi = 0; qi < queriesPerGrid; ++qi) {
            float point[3];
            if (sc.chars > 0 && r.Chance(70)) {   // at or near a character
                const TChar& c = chars[static_cast<size_t>(r.Below(sc.chars))];
                const float j = sc.lattice > 0.0f ? sc.lattice * static_cast<float>(r.Below(3) - 1) : r.Uniform(-1.0f, 1.0f);
                point[0] = c.pos.x + j;
                point[1] = c.pos.y + (r.Chance(50) ? j : 0.0f);
                point[2] = c.pos.z;
            } else {
                point[0] = ox + r.Uniform(-sc.spread, sc.spread);
                point[1] = oy + r.Uniform(-sc.spread, sc.spread);
                point[2] = r.Uniform(-3.0f, 3.0f);
            }
            static const float kRadii[] = {0.5f, 0.5f, 0.5f, 1.0f, 2.5f, 0.0f, 8.0f, 40.0f, 0.75f, 3.0f};
            Math::Scalar radius(kRadii[r.Below(10)]);
            const int typeId = (sc.types && r.Chance(40)) ? 1 + r.Below(sc.types) : 0;
            TChar* exclude = (sc.chars > 0 && r.Chance(80)) ? &chars[static_cast<size_t>(r.Below(sc.chars))] : nullptr;

            RefStats st;
            TChar* ref = reinterpret_cast<TChar*>(uintptr_t{1});
            RefQuery(g, &ref, point, &radius, typeId, &exclude, st);
            TChar* fast = reinterpret_cast<TChar*>(uintptr_t{1});
            const bool handled = CharGridNearest::Query<TPolicy>(&fast, point, radius.v, typeId, &exclude,
                                                                 g.slots.data(), g.cellHead.data(),
                                                                 g.cellOccupancy.data(), g.worldSize, g.levels);
            ++t.queries;
            if (ref) ++t.found;
            t.ties += st.ties;
            t.candidates += st.candidates;
            t.nodes += st.nodes;
            if (!handled || fast != ref) {
                if (t.mismatches < 5 && g_mutation == 0)
                    std::printf("  mismatch: grid %d query %d ref %p fast %p\n", gi, qi,
                                static_cast<void*>(ref), static_cast<void*>(fast));
                ++t.mismatches;
            }
        }
    }
}

void Report(const char* name, const Totals& t) {
    std::printf("  %s: %lld queries, %lld found, %lld mismatches, %lld ties, %lld candidates, %lld nodes\n",
                name, t.queries, t.found, t.mismatches, t.ties, t.candidates, t.nodes);
}

const Scenario kTown   {2048.0f, 8, 600, 60.0f, 0.5f, 10, 0, false};   // the game's grid, a dense town
const Scenario kTyped  {2048.0f, 8, 400, 40.0f, 0.25f, 20, 3, false};  // HuntAnimal's type filter
const Scenario kOdd    {2047.3f, 6, 300, 80.0f, 0.0f, 30, 2, true};    // inexact centers, bad occupancy
const Scenario kSmall  {100.7f, 4, 120, 30.0f, 1.0f, 50, 0, true};     // shallow grid, coarse lattice, stale

}  // namespace

UNIT_TEST(charactergrid_nearest_town_identical_to_retail) {
    g_mutation = 0;
    Totals t;
    RunScenario(kTown, 11, 40, 400, t);
    Report("town", t);
    CHECK_EQ(t.mismatches, 0LL);
    CHECK(t.found > 3000);
    CHECK(t.ties > 1000);   // equal distances: the visit order decides
}

UNIT_TEST(charactergrid_nearest_filter_odd_stale_identical_to_retail) {
    g_mutation = 0;
    Totals a, b, c;
    RunScenario(kTyped, 21, 40, 300, a);
    RunScenario(kOdd, 31, 40, 300, b);
    RunScenario(kSmall, 41, 60, 300, c);
    Report("typed", a);
    Report("odd", b);
    Report("small", c);
    CHECK_EQ(a.mismatches, 0LL);
    CHECK_EQ(b.mismatches, 0LL);
    CHECK_EQ(c.mismatches, 0LL);
    CHECK(a.found > 1000);
    CHECK(b.found > 1000);
    CHECK(c.found > 1000);
    CHECK(a.ties + c.ties > 500);
}

UNIT_TEST(charactergrid_nearest_mutations_are_caught) {
    // The comparison has teeth: retail with its tie rule or its child order changed is a
    // different function, and the same queries say so.
    Totals tie, order;
    g_mutation = 1;
    RunScenario(kTown, 11, 40, 400, tie);
    g_mutation = 2;
    RunScenario(kTown, 11, 40, 400, order);
    g_mutation = 0;
    Report("mutation ties-to-later", tie);
    Report("mutation child-order", order);
    CHECK(tie.mismatches > 500);
    CHECK(order.mismatches > 30);   // only ties ACROSS cells see the child order (88 at these seeds)
}

UNIT_TEST(charactergrid_nearest_deep_grid_falls_back) {
    // A grid deeper than the node stack is left to the retail body (nothing written).
    TChar* best = reinterpret_cast<TChar*>(uintptr_t{1});
    TChar* exclude = nullptr;
    const float point[3] = {0.0f, 0.0f, 0.0f};
    const bool handled = CharGridNearest::Query<TPolicy, TSlot>(&best, point, 0.5f, 0, &exclude, nullptr, nullptr,
                                                               nullptr, 2048.0f, CharGridNearest::kMaxLevels + 1);
    CHECK(!handled);
    CHECK(best == reinterpret_cast<TChar*>(uintptr_t{1}));
}
