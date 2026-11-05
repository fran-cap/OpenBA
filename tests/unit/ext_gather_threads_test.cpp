// ============================================================================
// tests/unit/ext_gather_threads_test.cpp -- CULLPAR: ext.gather_threads (class A)
// ============================================================================
// The parallel walk (src/ext/draw_gather/gather_walk.h) and its helper pool
// (src/ext/gather_threads/gather_threads.{h,cpp}), on the REAL walk code over random quadtrees:
//   * the pool: the count is clamped to Cpu::kMaxCullHelpers, every published task
//     runs exactly once per pass whoever claims it, across wake / park cycles;
//   * the walk: random grids (levels, cells, chains, pruned / stale cell counts,
//     free node slots), random frusta and plane tables, random pass masks, detail
//     and shadow modes, random side-array states; with 0, 1, 2 and 3 (clamped to 2)
//     helpers the list is byte for byte the sequential walk's (Walk with the side
//     test, and Walk with retail's reads), and so are the counters, the box-test
//     count and the side array afterwards;
//   * the side array never changes size during a parallel walk.
// The Scene and the Geometry objects are raw storage with the fields the walk reads
// set (the walk never calls into either). docs/extensions/handins/CULLPAR.md
// ============================================================================
#include "unit.h"
#include "ext/draw_gather/gather_walk.h"
#include "ext/registry.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using Ext::DrawGather::SideArray;
using Ext::GatherThreads::HelperPool;

// ---------------------------------------------------------------------------
// The pool on its own.
// ---------------------------------------------------------------------------
struct CountCtx {
    std::atomic<int> runs[64];
    std::atomic<int> byHelper{0};
};

void CountTask(void* ctx, int32_t task, int32_t worker) {
    CountCtx* c = static_cast<CountCtx*>(ctx);
    c->runs[task].fetch_add(1, std::memory_order_relaxed);
    if (worker != 0) c->byHelper.fetch_add(1, std::memory_order_relaxed);
    volatile int spin = 0;                       // a little work, so helpers get some
    for (int i = 0; i < 200 * (task % 5); ++i) spin = spin + 1;
}

UNIT_TEST(ext_gather_threads_pool_clamps_and_runs_every_task_once) {
    {
        HelperPool p(3);
        CHECK(p.Helpers() == Cpu::kMaxCullHelpers);
        HelperPool q(-1);
        CHECK(q.Helpers() == 0);
    }
    for (int helpers = 0; helpers <= 3; ++helpers) {
        HelperPool pool(helpers);
        CountCtx c;
        int helperRuns = 0;
        for (int frame = 0; frame < 60; ++frame) {
            pool.Wake();
            CHECK(pool.Awake() == (pool.Helpers() > 0));
            for (int pass = 0; pass < 1 + frame % 4; ++pass) {
                const int n = (frame * 7 + pass * 3) % 64;
                for (int i = 0; i < 64; ++i) c.runs[i].store(0);
                c.byHelper.store(0);
                pool.Publish(&CountTask, &c, n);
                const HelperPool::PassResult r = pool.Finish();
                CHECK(r.tasks == n);
                CHECK(r.callerTasks + c.byHelper.load() == n);
                for (int i = 0; i < 64; ++i) CHECK(c.runs[i].load() == (i < n ? 1 : 0));
                helperRuns += c.byHelper.load();
            }
            pool.Park();
            CHECK(!pool.Awake());
        }
        if (pool.Helpers() == 0) CHECK(helperRuns == 0);
        CHECK(pool.Wakes() == (pool.Helpers() > 0 ? 60u : 0u));
    }
}

// ---------------------------------------------------------------------------
// Random scenes.
// ---------------------------------------------------------------------------
struct alignas(alignof(Graphics::Geometry)) GeoBlob {
    unsigned char b[sizeof(Graphics::Geometry)];
};
struct alignas(alignof(Graphics::Scene)) SceneBlob {
    unsigned char b[sizeof(Graphics::Scene)];
};

struct World {
    SceneBlob                      sceneBlob;
    std::vector<int32_t>           counts, heads;
    std::vector<Graphics::SceneNode> nodes;
    std::vector<GeoBlob>           geos;

    Graphics::Scene*    scene() { return reinterpret_cast<Graphics::Scene*>(&sceneBlob); }
    Graphics::Geometry* geo(size_t i) { return reinterpret_cast<Graphics::Geometry*>(&geos[i]); }
};

float Rand(std::mt19937& rng, float lo, float hi) {
    return lo + (hi - lo) * static_cast<float>(rng() % 100000) / 100000.0f;
}

void BuildWorld(World& w, std::mt19937& rng) {
    const int32_t levels = 1 + static_cast<int32_t>(rng() % 6);
    int32_t cells = 0;
    for (int32_t l = 0, n = 1; l < levels; ++l, n *= 4) cells += n;
    const float cellSize = 2048.0f;

    const int32_t objects = static_cast<int32_t>(rng() % 500);
    const int32_t freeSlots = static_cast<int32_t>(rng() % 20);
    const int32_t slots = objects + freeSlots;
    w.geos.assign(static_cast<size_t>(objects), GeoBlob{});
    w.nodes.assign(static_cast<size_t>(slots), Graphics::SceneNode{});
    w.counts.assign(static_cast<size_t>(cells), 0);
    w.heads.assign(static_cast<size_t>(cells), -1);

    // Node ids: a random permutation; the free slots are the last ones.
    std::vector<int32_t> ids(static_cast<size_t>(slots));
    for (int32_t i = 0; i < slots; ++i) ids[static_cast<size_t>(i)] = i;
    std::shuffle(ids.begin(), ids.end(), rng);

    std::vector<std::vector<int32_t>> chain(static_cast<size_t>(cells));
    for (int32_t o = 0; o < objects; ++o) {
        Graphics::Geometry* g = w.geo(static_cast<size_t>(o));
        float box[6];
        const float cx = Rand(rng, -cellSize, cellSize), cy = Rand(rng, -cellSize, cellSize);
        const float cz = Rand(rng, -50.0f, 50.0f);
        const float hx = Rand(rng, 0.5f, 300.0f), hy = Rand(rng, 0.5f, 300.0f), hz = Rand(rng, 0.5f, 60.0f);
        box[0] = cx - hx; box[1] = cy - hy; box[2] = cz - hz;
        box[3] = cx + hx; box[4] = cy + hy; box[5] = cz + hz;
        std::memcpy(&g->_worldBounds, box, sizeof(box));
        g->_materialTypeFlags = 1u << (rng() % 5);
        g->_detailId          = static_cast<int32_t>(rng() % 4);
        const int32_t cell = (rng() % 4 == 0) ? static_cast<int32_t>(rng() % static_cast<uint32_t>(cells > 21 ? 21 : cells))
                                              : static_cast<int32_t>(rng() % static_cast<uint32_t>(cells));
        const int32_t id = ids[static_cast<size_t>(o)];
        w.nodes[static_cast<size_t>(id)]._object = g;
        w.nodes[static_cast<size_t>(id)]._cell   = cell;
        chain[static_cast<size_t>(cell)].push_back(id);
    }
    for (int32_t f = objects; f < slots; ++f) {
        Graphics::SceneNode& n = w.nodes[static_cast<size_t>(ids[static_cast<size_t>(f)])];
        n._object = nullptr;
        n._cell   = -1;
        n._next   = -1;
    }
    for (int32_t c = 0; c < cells; ++c) {
        const std::vector<int32_t>& ch = chain[static_cast<size_t>(c)];
        if (ch.empty()) continue;
        w.heads[static_cast<size_t>(c)] = ch[0];
        for (size_t k = 0; k < ch.size(); ++k)
            w.nodes[static_cast<size_t>(ch[k])]._next = ch[(k + 1) % ch.size()];
    }
    // Subtree counts (children of c are 4c+1..4c+4), then a few wrong ones: a zero
    // prunes a subtree, a one on an empty cell walks it for nothing.
    for (int32_t c = cells - 1; c >= 0; --c) {
        int32_t n = static_cast<int32_t>(chain[static_cast<size_t>(c)].size());
        for (int32_t k = 1; k <= 4; ++k)
            if (4 * c + k < cells) n += w.counts[static_cast<size_t>(4 * c + k)];
        w.counts[static_cast<size_t>(c)] = n;
    }
    for (int32_t c = 0; c < cells; ++c) {
        const unsigned r = rng() % 40;
        if (r == 0) w.counts[static_cast<size_t>(c)] = 0;
        else if (r == 1 && w.counts[static_cast<size_t>(c)] == 0) w.counts[static_cast<size_t>(c)] = 1;
    }

    std::memset(&w.sceneBlob, 0, sizeof(w.sceneBlob));
    Graphics::Scene* s = w.scene();
    s->_grid._cellSize          = cellSize;
    s->_grid._levelCount        = levels;
    s->_grid._cellCounts._data  = w.counts.data();
    s->_grid._cellCounts._size  = cells;
    s->_grid._cellHeads._data   = w.heads.data();
    s->_grid._cellHeads._size   = cells;
    s->_grid._nodes._data       = w.nodes.data();
    s->_grid._nodes._size       = slots;
}

// Six planes around a random point of the world (inside = n.p - w > 0, frustum.cpp's
// RtVolume_Frustum_Contains), each at a random distance: cells in, out and straddling.
void RandomFrustum(::Frustum& fr, std::mt19937& rng) {
    fr = ::Frustum{};
    const float px = Rand(rng, -2500.0f, 2500.0f), py = Rand(rng, -2500.0f, 2500.0f), pz = Rand(rng, -20.0f, 20.0f);
    for (int i = 0; i < 6; ++i) {
        float x = Rand(rng, -1.0f, 1.0f), y = Rand(rng, -1.0f, 1.0f), z = Rand(rng, -0.3f, 0.3f);
        if (rng() % 5 == 0) x = 0.0f;                  // axis-parallel planes too
        const float len = RtLibm::Sqrt(x * x + y * y + z * z) + 1e-3f;
        fr.planes[i].x = x / len;
        fr.planes[i].y = y / len;
        fr.planes[i].z = z / len;
        fr.planes[i].w = fr.planes[i].x * px + fr.planes[i].y * py + fr.planes[i].z * pz -
                         Rand(rng, 0.0f, 2500.0f);
    }
}

void RandomFrame(Ext::DrawGather::Frame& f, std::mt19937& rng) {
    RandomFrustum(f.cull, rng);
    RandomFrustum(f.shadow, rng);
    if (rng() % 6 == 0) {                            // a frustum everything passes
        for (int i = 0; i < 6; ++i) f.cull.planes[i].w = -100000.0f;
    }
    FrustumCull_Prepare(&f.cull, &f.cullPlanes);
    FrustumCull_Prepare(&f.shadow, &f.shadowPlanes);
    f.extX = Rand(rng, -1.0f, 1.0f);
    f.extY = Rand(rng, -1.0f, 1.0f);
    f.extZ = Rand(rng, -1.0f, 1.0f);
    f.passMask       = 1u + rng() % 31u;
    f.rendererDetail = static_cast<int32_t>(rng() % 4);
    f.notShadow      = rng() % 2 == 0;
    f.cullVerify     = false;
    f.boxes          = 0;
}

// Random side-array state: valid records (live box, true or stale hints), invalid
// ones, and ids past the end.
void RandomSide(SideArray& side, World& w, std::mt19937& rng) {
    for (size_t i = 0; i < w.nodes.size(); ++i) {
        const Graphics::Geometry* g = w.nodes[i]._object;
        if (g == nullptr || rng() % 3 == 0) continue;
        float box[6];
        std::memcpy(box, &g->_worldBounds, sizeof(box));
        const bool stale = rng() % 4 == 0;
        side.Store(static_cast<int32_t>(i), g, box, stale ? ~g->_materialTypeFlags : g->_materialTypeFlags,
                   stale ? 9 : g->_detailId);
    }
}

bool SameSide(const SideArray& a, const SideArray& b, const World& w) {
    for (size_t i = 0; i < w.nodes.size(); ++i) {
        const void* g = w.nodes[i]._object;
        const int32_t id = static_cast<int32_t>(i);
        if (a.Valid(id, g) != b.Valid(id, g)) return false;
        if (a.Valid(id, g) && std::memcmp(&a.At(id), &b.At(id), sizeof(Ext::DrawGather::SideRecord)) != 0) return false;
    }
    return true;
}

UNIT_TEST(ext_gather_threads_parallel_walk_equals_sequential) {
    int cases = 0, withTasks = 0, nonEmpty = 0;
    for (int helpers = 0; helpers <= 3; ++helpers) {
        long long tasks = 0, helperTasks = 0, visible = 0;
        HelperPool pool(helpers);
        std::mt19937 rng(9100u + static_cast<unsigned>(helpers));
        Ext::DrawGather::ParWalk par;
        par.pool = &pool;
        for (int c = 0; c < 250; ++c) {
            World w;
            BuildWorld(w, rng);
            Ext::DrawGather::Frame fSeq;
            RandomFrame(fSeq, rng);
            Ext::DrawGather::Frame fPar = fSeq;
            Ext::DrawGather::Frame fRef = fSeq;

            SideArray sideSeq, sidePar;
            RandomSide(sideSeq, w, rng);
            sidePar = sideSeq;

            // The sequential walks: the extension's (side test) and retail's reads.
            std::vector<Graphics::Geometry*> seq, ref, got;
            Ext::DrawGather::TaskStats seqStats;
            Ext::DrawGather::TaskSideTest seqTest{ fSeq, sideSeq, true, seqStats };
            Ext::DrawGather::Walk(w.scene(), fSeq, seq, seqTest);
            Ext::DrawGather::RetailTest rt{ fRef };
            Ext::DrawGather::Walk(w.scene(), fRef, ref, rt);

            // The parallel walk; a "frame" of 1-3 passes per wake, the pool parked
            // in between, and every fifth case run on a parked pool (the caller alone).
            if (c % 5 != 4) pool.Wake();
            const Ext::DrawGather::ParPassResult r = Ext::DrawGather::ParGather(par, w.scene(), fPar, sidePar, true, got);
            if (c % 3 == 2) pool.Park();

            CHECK(got == seq);
            CHECK(got == ref);
            CHECK(r.stats.nodesTested == seqStats.nodesTested);
            CHECK(r.stats.geoTouched == seqStats.geoTouched);
            CHECK(r.stats.refreshes == seqStats.refreshes);
            CHECK(r.stats.sideChecks == seqStats.sideChecks);
            CHECK(r.stats.sideMismatches == 0 && seqStats.sideMismatches == 0);
            CHECK(fPar.boxes == fSeq.boxes);
            CHECK(!r.sideGrew);
            CHECK(r.helperTasks <= r.tasks);
            CHECK(SameSide(sidePar, sideSeq, w));
            ++cases;
            tasks += r.tasks;
            helperTasks += r.helperTasks;
            visible += static_cast<long long>(got.size());
            if (r.tasks > 1) ++withTasks;
            if (!got.empty()) ++nonEmpty;
        }
        pool.Park();
        // Informational (timing decides who runs what; the lists must not care).
        std::printf("  [gather_threads] helpers=%d: 250 cases, %lld tasks, %lld run by helpers, %lld visible\n",
                    pool.Helpers(), tasks, helperTasks, visible);
        if (pool.Helpers() == 0) CHECK(helperTasks == 0);
        // The plain caller-only path (no pool at all) on a few more.
        Ext::DrawGather::ParWalk alone;
        for (int c = 0; c < 20; ++c) {
            World w;
            BuildWorld(w, rng);
            Ext::DrawGather::Frame f;
            RandomFrame(f, rng);
            Ext::DrawGather::Frame f2 = f;
            SideArray s1, s2;
            std::vector<Graphics::Geometry*> a, b;
            Ext::DrawGather::TaskStats st;
            Ext::DrawGather::TaskSideTest t{ f, s1, false, st };
            Ext::DrawGather::Walk(w.scene(), f, a, t);
            Ext::DrawGather::ParGather(alone, w.scene(), f2, s2, false, b);
            CHECK(a == b);
            ++cases;
        }
    }
    CHECK(cases >= 1000);
    CHECK(withTasks >= 300);   // the random worlds do reach the frontier
    CHECK(nonEmpty >= 300);    // and the frusta let objects through
}

// ---------------------------------------------------------------------------
// FUSED (issue #253): the shadow group's and the main group's walks in one traversal.
// ---------------------------------------------------------------------------
// Each group's list out of FusedWalk + FusedSplit is byte for byte that group's own
// sequential walk (retail's reads); the parallel fused walk's hits are the sequential
// fused walk's for 0..3 helpers, with the same counters, box tests and side array.
UNIT_TEST(ext_gather_threads_fused_walk_equals_two_walks) {
    int cases = 0, bothNonEmpty = 0, withTasks = 0;
    for (int helpers = 0; helpers <= 3; ++helpers) {
        HelperPool pool(helpers);
        std::mt19937 rng(7300u + static_cast<unsigned>(helpers));
        Ext::DrawGather::FusedParWalk par;
        par.pool = &pool;
        for (int c = 0; c < 250; ++c) {
            World w;
            BuildWorld(w, rng);
            Ext::DrawGather::Frame fa, fb;
            RandomFrame(fa, rng);
            RandomFrame(fb, rng);
            if (c % 4 != 3) {
                fa.notShadow = false;              // the sun's group ...
                fb.notShadow = true;               // ... and the main group, mostly
            }
            if (c % 7 == 0) fb.cull = fa.shadow;   // the sun case: B's cull frustum is A's draw frustum
            FrustumCull_Prepare(&fb.cull, &fb.cullPlanes);

            // The references: each group's own walk, retail's reads.
            Ext::DrawGather::Frame ra = fa, rb = fb;
            std::vector<Graphics::Geometry*> refA, refB;
            Ext::DrawGather::RetailTest rta{ ra };
            Ext::DrawGather::Walk(w.scene(), ra, refA, rta);
            Ext::DrawGather::RetailTest rtb{ rb };
            Ext::DrawGather::Walk(w.scene(), rb, refB, rtb);

            SideArray sideSeq, sidePar;
            RandomSide(sideSeq, w, rng);
            sidePar = sideSeq;

            // The sequential fused walk.
            Ext::DrawGather::Frame sa = fa, sb = fb;
            std::vector<Ext::DrawGather::FusedHit> hitsSeq;
            Ext::DrawGather::TaskStats seqStats;
            Ext::DrawGather::FusedBoxTest test{ sa, sb, sideSeq, true, seqStats };
            Ext::DrawGather::FusedWalk(w.scene(), sa, sb, hitsSeq, test);
            std::vector<Graphics::Geometry*> gotA, gotB;
            Ext::DrawGather::FusedSplit(sa, sb, hitsSeq, gotA, gotB);
            CHECK(gotA == refA);
            CHECK(gotB == refB);
            CHECK(seqStats.sideMismatches == 0);

            // The parallel fused walk.
            Ext::DrawGather::Frame pa = fa, pb = fb;
            std::vector<Ext::DrawGather::FusedHit> hitsPar;
            if (c % 5 != 4) pool.Wake();
            const Ext::DrawGather::ParPassResult r =
                Ext::DrawGather::FusedParGather(par, w.scene(), pa, pb, sidePar, true, hitsPar);
            if (c % 3 == 2) pool.Park();
            CHECK(hitsPar.size() == hitsSeq.size());
            bool sameHits = hitsPar.size() == hitsSeq.size();
            for (size_t i = 0; sameHits && i < hitsPar.size(); ++i)
                sameHits = hitsPar[i].geo == hitsSeq[i].geo && hitsPar[i].bits == hitsSeq[i].bits;
            CHECK(sameHits);
            CHECK(r.stats.nodesTested == seqStats.nodesTested);
            CHECK(r.stats.refreshes == seqStats.refreshes);
            CHECK(r.stats.sideChecks == seqStats.sideChecks);
            CHECK(r.stats.sideMismatches == 0);
            CHECK(pa.boxes == sa.boxes);
            CHECK(pb.boxes == sb.boxes);
            CHECK(!r.sideGrew);
            CHECK(SameSide(sidePar, sideSeq, w));
            ++cases;
            if (!refA.empty() && !refB.empty()) ++bothNonEmpty;
            if (r.tasks > 1) ++withTasks;
        }
        pool.Park();
    }
    CHECK(cases == 1000);
    CHECK(bothNonEmpty >= 200);   // both groups do get objects
    CHECK(withTasks >= 300);      // and the walks reach the frontier
}

UNIT_TEST(ext_gather_threads_fused_same_walk) {
    std::mt19937 rng(515);
    Ext::DrawGather::Frame a;
    RandomFrame(a, rng);
    a.notShadow = true;
    Ext::DrawGather::Frame b = a;
    b.boxes = 77;                                  // counters are not inputs
    b.cullVerify = !a.cullVerify;
    CHECK(Ext::DrawGather::SameWalk(a, b));
    b.shadow.planes[2].x += 1.0f;                  // unread without a shadow pass
    CHECK(Ext::DrawGather::SameWalk(a, b));
    a.notShadow = false;
    b.notShadow = false;
    CHECK(!Ext::DrawGather::SameWalk(a, b));       // read with one
    b = a;
    b.passMask ^= 1u;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
    b = a;
    b.rendererDetail += 1;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
    b = a;
    b.notShadow = !a.notShadow;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
    b = a;
    b.cull.planes[5].w = -b.cull.planes[5].w - 1.0f;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
    b = a;
    b.cull.isOrthographic = !a.cull.isOrthographic;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
    b = a;
    b.cull.planes[0].x = -0.0f;                    // bits, not values: -0 is not +0
    a.cull.planes[0].x = 0.0f;
    CHECK(!Ext::DrawGather::SameWalk(a, b));
}

UNIT_TEST(ext_gather_threads_side_array_never_grows_during_a_parallel_walk) {
    std::mt19937 rng(4242);
    HelperPool pool(2);
    Ext::DrawGather::ParWalk par;
    par.pool = &pool;
    pool.Wake();
    for (int c = 0; c < 100; ++c) {
        World w;
        BuildWorld(w, rng);
        Ext::DrawGather::Frame f;
        RandomFrame(f, rng);
        const int32_t n = w.scene()->_grid._nodes.GetSize();

        // Reserved first: the size is the same before and after.
        SideArray side;
        side.Reserve(n);
        const int32_t before = side.Size();
        CHECK(n == 0 || before >= n);
        std::vector<Graphics::Geometry*> out;
        Ext::DrawGather::ParPassResult r = Ext::DrawGather::ParGather(par, w.scene(), f, side, false, out);
        CHECK(side.Size() == before);
        CHECK(!r.sideGrew);

        // Empty at first: the walk's own Reserve gives the size, nothing more.
        SideArray empty, sized;
        sized.Reserve(n);
        Ext::DrawGather::Frame f2 = f;
        f2.boxes = 0;
        out.clear();
        r = Ext::DrawGather::ParGather(par, w.scene(), f2, empty, false, out);
        CHECK(empty.Size() == sized.Size());
        CHECK(!r.sideGrew);
    }
    pool.Park();
}

UNIT_TEST(ext_gather_threads_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("gather_threads");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2, ruled 2026-10-18)
    CHECK(e->formatVersion == 0);
    // The count rule the row runs on (THREADRESEARCH §7.4): none on 4 cores.
    CHECK(Cpu::CullHelpers(4, false) == 0);
    CHECK(Cpu::CullHelpers(8, false) == 2);
}

}  // namespace
