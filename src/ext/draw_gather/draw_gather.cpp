// ============================================================================
// src/ext/draw_gather/draw_gather.cpp -- ext.draw_gather (class A): the bodies
// ============================================================================
// NOT RETAIL. draw_gather.h and gather_side.h have the contract;
// docs/extensions/handins/DRAWGATHER.md the design, the writers and the shadow.
//
// Two replaced bodies, reached through the null-by-default statics on
// Graphics::Scene (src/engine/graphics/scene_ext.h):
//
//   Clean        retail's Scene::Clean (scene.cpp, 0x1801023f0) line for line --
//                the same geometry cleans, the same GridReindex calls, the same
//                re-read of the node count -- plus, per live node, the side
//                record's upkeep (full refresh when Clean reported a change or
//                the record is not this geometry's; the two hints otherwise) and
//                a prefetch of the geometry kCleanAhead nodes on.
//   Gather       retail's AddCulledDrawItems (cull.cpp, 0x1801017a0): the same
//                quadtree walk (the CullStack below is a copy of cull.cpp's,
//                record for record), the same frusta, the same per-cell masks;
//                per object the test reads the side record first (NodeVisible),
//                then the same virtual AddDrawItems over the visible list in
//                its order, with a prefetch kDispatchAhead geometries on.
//
// The presentation is identical by construction (same list, same order, same
// virtual call per geometry, so the same draw items, sort keys, transforms and
// bone sets); the COMPPROF=4 shadows `DrawGather` (the list against retail's
// reads) and `DrawGatherSide` (each record used against the live box) check it.
// Nothing here is read by the sim or the pick scene (Picking::Scene is a
// different class), nothing is serialized.
// ============================================================================
#include "ext/draw_gather/draw_gather.h"
#include "ext/draw_gather/gather_walk.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rt_libm.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "volume/frustum_cull.h"   // CULLROUTE: the prepared tables
#include "graphics/Camera.h"
#include "graphics/Geometry.h"
#include "graphics/Material.h"   // P13: MaterialInstance::_shaderClass (kDispatchMaterials)
#include "graphics/Mesh.h"       // P13: MeshInstance::_materials (kDispatchMaterials)
#include "graphics/Scene.h"
#include "graphics/scene_ext.h"
#include "graphics/scene_grid.h"   // rtgfxgrid::GridReindex, Clean's re-index
#include "ext/registry.h"
#include "object/class_vptr.h"   // P13: Object::ClassVptr (the vptr picks the stage)
#include "platform/common/cpu/roster.h"      // CULLPAR: Cpu::CullHelpers
#include "platform/common/cpu/topology.h"    // CULLPAR: Cpu::Get().physical
#include "stress_harness.h"        // g_verifyOn / StressHarness_Verify (COMPPROF=4 shadows)

namespace Ext {
namespace DrawGather {

namespace {

using Graphics::Geometry;
using Graphics::Renderer;
using Graphics::RendererDrawGroup;
using Graphics::Scene;
using Graphics::SceneNode;

static_assert(sizeof(::Box) == sizeof(float) * 6, "a side record's box is a ::Box's bits");

// Prefetch distances (nodes / geometries ahead). Exact: a prefetch changes no value.
constexpr int32_t kCleanAhead    = 8;
constexpr int32_t kDispatchAhead = 4;

// P13 (docs/android/P13_CPU.md), ARM only: measured on the Galaxy S10, whose gather waits on one
// Geometry miss at a time; the desktop object stays as it was.
//   kLiveAfterWalk     the walk tests the side record's box alone and collects the survivors; a pass
//                      over that array then makes retail's live flag / detail test with the
//                      Geometries kLiveAhead entries ahead prefetched. Retail's per-object test is
//                      `flagsOK && detailOK && boxOK`, three pure reads; all of them are still made
//                      before the first AddDrawItems and the survivors keep the walk's order, so the
//                      visible list is the same (the COMPPROF=4 `DrawGather` shadow checks it).
//   kDispatchMaterials the dispatch also names a MeshInstance's `_materials._data` line (at i+3) and
//                      the first kDispatchMaterialCount MaterialInstances' `_shaderClass` / `_material`
//                      line (at i+2), which Renderer::AddDrawItem reads first.
#if defined(__aarch64__)
constexpr bool    kLiveAfterWalk     = true;
constexpr bool    kDispatchMaterials = true;
#else
constexpr bool    kLiveAfterWalk     = false;
constexpr bool    kDispatchMaterials = false;
#endif
constexpr size_t  kLiveAhead             = 16;
constexpr int32_t kDispatchMaterialCount = 2;

// ---------------------------------------------------------------------------
// The state. Main thread only (Clean and the gather run there), except the add
// log, which locks itself (scene_ext.h).
// ---------------------------------------------------------------------------
Graphics::SceneAddLog g_addLog;      // static storage: never freed, so a racing AddObject
                                     // on the loader thread never sees a dangling pointer
SceneSides            g_sides;
Counters              g_counters;
bool                  g_installed = false;

// Every id AddObject logged: that node's record is not to be trusted. An
// overflow forgets every record of every scene.
void DrainLog() {
    bool overflow = false;
    g_addLog.Drain(
        [](const Graphics::SceneAddLog::Entry& e) {
            ++g_counters.logged;
            if (SideArray* s = g_sides.Find(e.scene)) s->Invalidate(static_cast<int32_t>(e.id));
        },
        [&overflow](bool o) { overflow = o; });
    if (overflow) {
        g_sides.ForgetAll();
        ++g_counters.rebuilds;
    }
}

void StoreFull(SideArray& side, int32_t id, const Geometry* geo) {
    float box[6];
    std::memcpy(box, &geo->_worldBounds, sizeof(box));
    side.Store(id, geo, box, geo->_materialTypeFlags, geo->_detailId);
    ++g_counters.refreshes;
}

// ===========================================================================
// Clean -- retail's Scene::Clean (scene.cpp) plus the side upkeep.
// ===========================================================================
void ExtClean(Scene& scene) {
    using List = Collection::LinkedList<Geometry*>;

    DrainLog();
    SideArray& side = g_sides.Acquire(&scene);
    ++g_counters.cleans;

    int32_t count = scene._grid._nodes.GetSize();
    int32_t i = 0;
    if (count > 0) {
        while (i < count && scene._grid._nodes[i]._cell < 0) ++i;
    }
    if (i < count) {
        do {
            // The node array is dense; the Geometry it names is the miss.
            if (i + kCleanAhead < count) {
                const Geometry* ahead = scene._grid._nodes[i + kCleanAhead]._object;
                if (ahead != nullptr) {
                    __builtin_prefetch(ahead);                  // vptr, _detailId, _dirtyFlags
                    __builtin_prefetch(&ahead->_worldBounds);   // a box refresh, and the next line
                }
            }

            Geometry* geo = scene._grid._nodes[i]._object;
            bool changed = false;
            if (geo->_dirtyFlags != 0 && geo->Clean()) {        // VIRTUAL, slot 8 (retail's test)
                rtgfxgrid::GridReindex(&scene, i);              // = RtSceneGridReindex
                changed = true;
            }
            // Side upkeep. A box recompute always reports a change, so an unchanged
            // node's box is the one the record holds when the record is valid.
            if (UpkeepAfterClean(side, i, geo, changed,
                                 [geo](float* b) { std::memcpy(b, &geo->_worldBounds, sizeof(float) * 6); },
                                 geo->_materialTypeFlags, geo->_detailId))
                ++g_counters.refreshes;

            count = scene._grid._nodes.GetSize();
            ++i;
            if (count <= i) break;
            while (i < count && scene._grid._nodes[i]._cell < 0) ++i;
        } while (i < count);
    }

    for (List::Iterator it = scene._globals.GetFirst(); it != scene._globals.GetEnd(); ++it) {
        Geometry* geo = *it;
        if (geo->_dirtyFlags != 0)
            geo->Clean();                                       // VIRTUAL, slot 8
    }
}

// The walk (cull.cpp's CullStack and helpers, copied record for record), Frame,
// BoxPasses, RetailTest and the parallel walk: gather_walk.h (CULLPAR moved them there
// unchanged so the unit tests run them).

// The extension's: the side record first.
struct SideTest {
    const Frame& f;
    SideArray&   side;
    bool         verify;
    bool operator()(int32_t cur, const Geometry* geo, uint32_t cullMask, uint32_t shadowMask) {
        ++g_counters.nodesTested;
        bool touched = false;
        if (!side.Valid(cur, geo)) {
            StoreFull(side, cur, geo);
            touched = true;
        } else if (verify) {
            // COMPPROF=4: a valid record must hold the live box, bit for bit.
            const bool same = std::memcmp(side.At(cur).box, &geo->_worldBounds, sizeof(float) * 6) == 0;
            StressHarness_Verify("DrawGatherSide", same, same ? 0 : cur);
        }
        const SideRecord& r = side.At(cur);
        Box wb;
        std::memcpy(&wb, r.box, sizeof(wb));
        const bool pass = NodeVisible(
            HintPasses(r, f.passMask, f.rendererDetail),
            [&] {
                touched = true;
                return (geo->_materialTypeFlags & f.passMask) != 0 &&
                       geo->_detailId <= f.rendererDetail;
            },
            [&] { return BoxPasses(f, wb, cullMask, shadowMask); });
        if (touched) ++g_counters.geoTouched;
        return pass;
    }
};

// kLiveAfterWalk: the box half from the record; the live half is LiveFilter's.
struct SideBoxTest {
    const Frame& f;
    SideArray&   side;
    bool         verify;
    bool operator()(int32_t cur, const Geometry* geo, uint32_t cullMask, uint32_t shadowMask) {
        ++g_counters.nodesTested;
        if (!side.Valid(cur, geo)) {
            StoreFull(side, cur, geo);
        } else if (verify) {
            // COMPPROF=4: a valid record must hold the live box, bit for bit.
            const bool same = std::memcmp(side.At(cur).box, &geo->_worldBounds, sizeof(float) * 6) == 0;
            StressHarness_Verify("DrawGatherSide", same, same ? 0 : cur);
        }
        Box wb;
        std::memcpy(&wb, side.At(cur).box, sizeof(wb));
        return BoxPasses(f, wb, cullMask, shadowMask);
    }
};

// kLiveAfterWalk: retail's flag / detail test over the box survivors, in order, compacting the list
// in place. `_detailId` (+0x28) and `_materialTypeFlags` (+0x30) can straddle a line: both named.
void LiveFilter(const Frame& f, std::vector<Geometry*>& list) {
    const size_t n = list.size();
    size_t kept = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i + kLiveAhead < n) {
            const Geometry* ahead = list[i + kLiveAhead];
            __builtin_prefetch(&ahead->_detailId);
            __builtin_prefetch(&ahead->_materialTypeFlags);
        }
        Geometry* const geo = list[i];
        if ((geo->_materialTypeFlags & f.passMask) != 0 && geo->_detailId <= f.rendererDetail) list[kept++] = geo;
    }
    g_counters.geoTouched += n;
    list.resize(kept);
}

// The visible lists, one per nesting depth: a dispatch can re-enter the gather
// (MaterialInstance::RenderTextures -> a generated image's own scene draw).
std::vector<std::vector<Geometry*>> g_lists;
int32_t                             g_depth = 0;

// ===========================================================================
// CULLPAR -- ext.gather_threads (class A, needs ext.draw_gather): the parallel
// walk (gather_walk.h) on persistent helpers (src/ext/gather_threads/gather_threads.h).
// ===========================================================================
// The pool: created by SyncThreads when the row turns on, deleted when it turns
// off; never freed at exit (a static destructor would join threads during process
// teardown). Main thread only.
Ext::GatherThreads::HelperPool* g_pool = nullptr;
ParWalk     g_par;
// > 0: the gather walks in parallel. Written by SyncThreads only (boot and every
// class A toggle: the only points where the row or the count can change), read once
// per gather pass, never inside the walk.
int32_t     g_parHelpers = 0;
bool        g_parZeroLogged = false;

// Out of line: the sequential path keeps its code (CULLPAR.md section 7).
__attribute__((noinline)) void ParallelGather(const Scene* self, const Frame& f, SideArray& side, bool verify,
                                              std::vector<Geometry*>& list) {
    // The frame's first gather pass wakes the helpers; they spin through the rest of
    // pass 1 and park at its end (ExtDrawPass1End).
    if (!g_pool->Awake()) {
        g_pool->Wake();
        ++g_counters.parWakes;
    }
    const ParPassResult r = ParGather(g_par, self, f, side, verify, list);
    g_counters.nodesTested  += r.stats.nodesTested;
    g_counters.geoTouched   += r.stats.geoTouched;
    g_counters.refreshes    += r.stats.refreshes;
    g_counters.parPasses    += 1;
    g_counters.parTasks     += static_cast<uint64_t>(r.tasks);
    g_counters.parHelper    += static_cast<uint64_t>(r.helperTasks);
    g_counters.parWaitTicks += static_cast<uint64_t>(r.waitTicks);
    if (r.sideGrew) ++g_counters.parSideGrew;
    if (verify) {
        // COMPPROF=4: the DrawGatherSide checks the tasks recorded, reported here on the
        // main thread, one call per check as the sequential walk makes them.
        for (uint64_t i = r.stats.sideMismatches; i < r.stats.sideChecks; ++i)
            StressHarness_Verify("DrawGatherSide", true, 0);
        for (uint64_t i = 0; i < r.stats.sideMismatches; ++i)
            StressHarness_Verify("DrawGatherSide", false, i < 4 ? r.stats.sideDetail[i] : 0);
    }
}

// Scene::_extDrawPass1End: Renderer::Draw's pass 1 is over (no gather follows in
// this frame). The spin ends here.
void ExtDrawPass1End() {
    if (g_pool != nullptr) g_pool->Park();
}

// Matches the pool to the rows. Main thread, outside any Renderer::Draw.
void SyncThreads() {
    const bool want = Ext::Ext().draw_gather && Ext::Ext().gather_threads;
    const int  physical = Cpu::Get().physical;
    // RENDERPIPE-1: the ACTIVE ext.render_pipe (a render thread takes a core). Sync runs on every
    // class A toggle (registry_boot.cpp SetAndRecord, after SetRequested), so a render_pipe toggle
    // re-runs this with the new value; that toggle comes from the UI pump inside a step, so the
    // render thread is idle and no pass 1 is running.
    const int  helpers = want ? Cpu::CullHelpers(physical, Ext::Ext().render_pipe) : 0;
    if (want && helpers == 0 && !g_parZeroLogged) {
        std::fprintf(stderr, "[GatherThreads] 0 helpers on %d physical cores: sequential walk\n", physical);
        std::fflush(stderr);
        g_parZeroLogged = true;
    }
    const int have = g_pool != nullptr ? g_pool->Helpers() : 0;
    if (helpers != have) {
        Scene::_extDrawPass1End = nullptr;
        delete g_pool;                                 // parks, stops and joins
        g_pool     = nullptr;
        g_par.pool = nullptr;
        if (helpers > 0) {
            g_pool = new Ext::GatherThreads::HelperPool(helpers);
            if (g_pool->Helpers() == 0) {
                delete g_pool;
                g_pool = nullptr;
            }
        }
        if (g_pool != nullptr) {
            g_par.pool              = g_pool;
            Scene::_extDrawPass1End = &ExtDrawPass1End;
        } else {
            g_par = ParWalk{};                         // the slots' memory released
        }
        std::fprintf(stderr, "[GatherThreads] %d helper thread(s) on %d physical cores%s\n",
                     g_pool != nullptr ? g_pool->Helpers() : 0, physical,
                     g_pool != nullptr ? " (NORMAL priority, parked between frames)" : ": sequential walk");
        std::fflush(stderr);
    }
    g_parHelpers = g_pool != nullptr ? g_pool->Helpers() : 0;
}

// ===========================================================================
// Gather -- the replaced Scene::AddCulledDrawItems.
// ===========================================================================
void ExtCulledGather(const Scene& scene, Renderer& rendererRef) {
    const Scene* self     = &scene;
    Renderer*    renderer = &rendererRef;
    ++g_counters.gathers;

    DrainLog();
    SideArray& side = g_sides.Acquire(self);

    const RendererDrawGroup* g = &renderer->_drawGroups[renderer->_currentDrawGroup];

    Frame f;
    f.passMask       = g->_materialTypeFlags;
    f.rendererDetail = renderer->_detailId;
    RtVolume_Frustum_MulTransform3(&g->_cullCamera._frustum, &f.cull, &g->_cullCamera._cameraToWorld);
    f.extX = f.cull.planes[0].x;
    f.extY = f.cull.planes[0].y;
    f.extZ = f.cull.planes[0].z;
    RtVolume_Frustum_MulTransform3(&g->_camera._frustum, &f.shadow, &g->_camera._cameraToWorld);
    f.notShadow = (g->_shader != 1);
    FrustumCull_Prepare(&f.cull, &f.cullPlanes);      // CULLROUTE: after both frusta's last writer
    FrustumCull_Prepare(&f.shadow, &f.shadowPlanes);
    f.cullVerify = g_frustumCullVerify != 0;
    f.boxes = 0;

    const int32_t depth = g_depth++;
    if (static_cast<int32_t>(g_lists.size()) <= depth) g_lists.resize(static_cast<size_t>(depth) + 1);
    std::vector<Geometry*>& list = g_lists[static_cast<size_t>(depth)];
    list.clear();

    const bool verify = g_verifyOn != 0;
    if (__builtin_expect(g_parHelpers > 0, 0)) {
        ParallelGather(self, f, side, verify, list);   // CULLPAR: ext.gather_threads
    } else if constexpr (kLiveAfterWalk) {
        SideBoxTest test{ f, side, verify };
        Walk(self, f, list, test);
        LiveFilter(f, list);
    } else {
        SideTest test{ f, side, verify };
        Walk(self, f, list, test);
    }
    g_counters.visible += list.size();

    if (verify) {
        // COMPPROF=4: retail's reads, the same walk, into a scratch list.
        std::vector<Geometry*> ref;
        ref.reserve(list.size());
        RetailTest rt{ f };
        Walk(self, f, ref, rt);
        size_t firstDiff = 0;
        while (firstDiff < ref.size() && firstDiff < list.size() && ref[firstDiff] == list[firstDiff])
            ++firstDiff;
        const bool same = ref.size() == list.size() && firstDiff == ref.size();
        StressHarness_Verify("DrawGather", same,
                             same ? 0 : (static_cast<long long>(ref.size()) << 32) |
                                            static_cast<long long>(firstDiff));
    }
    FrustumCull_CountSite(kFrustumCullSiteExtDrawGather, f.boxes);   // CULLROUTE: once per gather

    // The dispatch: retail's virtual slot 3 over the list, in order.
    const int32_t n = static_cast<int32_t>(list.size());
    [[maybe_unused]] const void* meshVt = nullptr;
    if constexpr (kDispatchMaterials) meshVt = Object::ClassVptr<Graphics::MeshInstance>::Get();
    for (int32_t i = 0; i < n; ++i) {
        if (i + kDispatchAhead < n) {
            const Geometry* ahead = g_lists[static_cast<size_t>(depth)][static_cast<size_t>(i + kDispatchAhead)];
            __builtin_prefetch(ahead);                         // vptr, _detailId
            __builtin_prefetch(&ahead->_worldBounds);          // the LOD box
            __builtin_prefetch(&ahead->_opacity);              // opacity pair, transform
            __builtin_prefetch(&ahead->_transform._previous);
            __builtin_prefetch(ahead + 1);                     // a MeshInstance's mesh / materials / animation
        }
        if constexpr (kDispatchMaterials) {
            // i+3: the vptr and `_materials` lines are the ones named at i+4.
            if (i + 3 < n) {
                const Geometry* g3 = g_lists[static_cast<size_t>(depth)][static_cast<size_t>(i + 3)];
                if (*reinterpret_cast<const void* const*>(g3) == meshVt) {  // CAST-OK: vslot (the vptr picks the stage)
                    const auto* mi = static_cast<const Graphics::MeshInstance*>(g3);
                    if (mi->_materials._data != nullptr) __builtin_prefetch(mi->_materials._data);
                }
            }
            // i+2: the array line named at i+3; the MaterialInstances AddDrawItem reads.
            if (i + 2 < n) {
                const Geometry* g2 = g_lists[static_cast<size_t>(depth)][static_cast<size_t>(i + 2)];
                if (*reinterpret_cast<const void* const*>(g2) == meshVt) {  // CAST-OK: vslot (the vptr picks the stage)
                    const auto* mi = static_cast<const Graphics::MeshInstance*>(g2);
                    const int32_t count = mi->_materials._size < kDispatchMaterialCount ? mi->_materials._size
                                                                                        : kDispatchMaterialCount;
                    for (int32_t k = 0; k < count; ++k) {
                        const Graphics::MaterialInstance* m = mi->_materials._data[k].Get();
                        if (m != nullptr) __builtin_prefetch(&m->_shaderClass);
                    }
                }
            }
        }
        // `list` may move if a nested gather grows g_lists: index it afresh.
        Geometry* geo = g_lists[static_cast<size_t>(depth)][static_cast<size_t>(i)];
        geo->AddDrawItems(*self, *renderer);                   // VIRTUAL, slot 3
    }
    --g_depth;
}

void SyncSeams(bool want) {
    if (want) {
        DrainLog();                                // stale ids from an earlier spell
        g_sides.ForgetAll();                       // every scene starts with no valid record
        ++g_counters.rebuilds;
        Scene::_extAddLog       = &g_addLog;
        Scene::_extClean        = &ExtClean;
        Scene::_extCulledGather = &ExtCulledGather;
    } else {
        Scene::_extCulledGather = nullptr;
        Scene::_extClean        = nullptr;
        Scene::_extAddLog       = nullptr;
        g_sides.ForgetAll();
        std::vector<std::vector<Geometry*>>().swap(g_lists);
    }
    g_installed = want;
}

}  // namespace

void Sync() {
    const bool want = Ext::Ext().draw_gather;
    if (want != g_installed) SyncSeams(want);
    SyncThreads();   // CULLPAR: ext.gather_threads follows both rows
}

bool Installed() { return g_installed; }

int ParallelHelpers() { return g_parHelpers; }

Counters TakeWindow() {
    const Counters c = g_counters;
    g_counters = Counters{};
    return c;
}

}  // namespace DrawGather
}  // namespace Ext
