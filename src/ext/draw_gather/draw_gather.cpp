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
//                its order, with a prefetch kDispatchAhead geometries on. The sun's
//                shadow gather also walks the main group (FUSED, issue #253) and
//                hands its list to the main gather that follows.
//
// The presentation is identical by construction (same list, same order, same
// virtual call per geometry, so the same draw items, sort keys, transforms and
// bone sets); the COMPPROF=4 shadows `DrawGather` (the list against retail's
// reads) and `DrawGatherSide` (each record used against the live box) check it.
// With ext.gather_threads on, the fused gather's two dispatches run as two pool tasks
// (DISPATCHPAR, dispatch_par.h); `DrawDispatchJoined` checks each group's whole pre-sort
// pass lists against a serial dispatch of the same list at its own point of the frame.
// Nothing here is read by the sim or the pick scene (Picking::Scene is a
// different class), nothing is serialized.
// ============================================================================
#include "ext/draw_gather/draw_gather.h"
#include "ext/draw_gather/gather_walk.h"
#include "ext/draw_gather/dispatch_par.h"   // DISPATCHPAR: the two dispatches' order-keeping half

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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
#include "graphics/gather_prefetch.h"   // DISPATCHPAR: g_gatherSlots (the verify puts them back)
#include "graphics/scene_ext.h"
#include "graphics/scene_grid.h"   // rtgfxgrid::GridReindex, Clean's re-index
#include "ext/registry.h"
#include "object/class_vptr.h"   // P13: Object::ClassVptr (the vptr picks the stage)
#include "platform/common/cpu/roster.h"      // CULLPAR: Cpu::CullHelpers
#include "platform/common/cpu/topology.h"    // CULLPAR: Cpu::Get().physical
#include "stress_harness.h"        // g_verifyOn / StressHarness_Verify (COMPPROF=4 shadows)
#include "system/MemoryAllocator.h"   // DISPATCHPAR: the pass lists' buffers (DrawItemArrayResize's allocator)

namespace Ext {
namespace DrawGather {

namespace {

using Graphics::Geometry;
using Graphics::Renderer;
using Graphics::RendererDrawGroup;
using Graphics::RendererDrawItem;
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
// FUSED (issue #253): bumped by every Clean and by every drain that invalidated a record
// (or overflowed); a fused list is served only while neither has moved.
uint64_t              g_cleanGen = 0;
uint64_t              g_logGen   = 0;

// Every id AddObject logged: that node's record is not to be trusted. An
// overflow forgets every record of every scene.
void DrainLog() {
    bool overflow = false;
    g_addLog.Drain(
        [](const Graphics::SceneAddLog::Entry& e) {
            ++g_counters.logged;
            ++g_logGen;
            if (SideArray* s = g_sides.Find(e.scene)) s->Invalidate(static_cast<int32_t>(e.id));
        },
        [&overflow](bool o) { overflow = o; });
    if (overflow) {
        g_sides.ForgetAll();
        ++g_counters.rebuilds;
        ++g_logGen;
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
    ++g_cleanGen;   // FUSED: no list walked before this Clean is served after it

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
FusedParWalk g_fpar;   // FUSED (issue #253): the fused walk's plan and slots, on the same pool
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

// ===========================================================================
// FUSED (issue #253) -- the shadow group's walk also walks the main group's.
// ===========================================================================
// SunLight::AddDrawItems opens the shadow group inside the main group's globals loop
// (draw camera = draw group 0's camera, cull camera = the sun's, shader 1) and gathers
// it; the main group's own gather follows once the globals are done. The shadow
// gather builds the main group's Frame the way that later gather will (MainFrameFor),
// walks both in one traversal (gather_walk.h FusedWalk), dispatches the shadow list
// and keeps the main list here. The main gather builds its Frame as always and takes
// the kept list only when it is that walk's (SameWalk on every input the walk reads)
// over the same scene, renderer, nesting depth and frame, with no Clean and no
// invalidating add-log drain since; otherwise it walks as before. The kept list is
// what its own walk would have produced: the grid, the boxes and the flags are not
// written between the two gathers (the dispatches in between are const AddDrawItems
// calls), and Clean / AddObject, the writers, move the generations. The dispatch of
// each list is untouched (same list, same point of the frame), so the draw items are.
// The COMPPROF=4 `DrawGatherFused` shadow checks each fused list against retail's
// reads (RetailTest) at its own gather. Main thread only.
struct MainStash {
    bool                   valid      = false;
    const Scene*           scene      = nullptr;
    const Renderer*        renderer   = nullptr;
    int32_t                depth      = 0;
    int32_t                frameIndex = 0;
    uint64_t               cleanGen   = 0;
    uint64_t               logGen     = 0;
    Frame                  f;
    std::vector<Geometry*> list;
    // DISPATCHPAR: the list was dispatched into g_dispatch[depth]'s main task, for draw group
    // `preGroup`; the main gather replays it instead of dispatching it.
    bool                   pre        = false;
    int32_t                preGroup   = -1;
};
MainStash             g_stash;
std::vector<FusedHit> g_hits;

// The Frame ExtCulledGather builds for group `g` (one place, so the two agree).
void FillFrame(Frame& f, const RendererDrawGroup& g, int32_t rendererDetail) {
    f.passMask       = g._materialTypeFlags;
    f.rendererDetail = rendererDetail;
    RtVolume_Frustum_MulTransform3(&g._cullCamera._frustum, &f.cull, &g._cullCamera._cameraToWorld);
    f.extX = f.cull.planes[0].x;
    f.extY = f.cull.planes[0].y;
    f.extZ = f.cull.planes[0].z;
    RtVolume_Frustum_MulTransform3(&g._camera._frustum, &f.shadow, &g._camera._cameraToWorld);
    f.notShadow = (g._shader != 1);
    FrustumCull_Prepare(&f.cull, &f.cullPlanes);      // CULLROUTE: after both frusta's last writer
    FrustumCull_Prepare(&f.shadow, &f.shadowPlanes);
    f.cullVerify = g_frustumCullVerify != 0;
    f.boxes = 0;
}

// For a shadow gather (`shadow.notShadow` false): the Frame of the group the current
// scene entry opened (Renderer::Draw writes its index into the entry before the
// globals loop), when that group has no shadow pass and its cull frustum is this
// group's draw-camera frustum -- the sun's case (draw camera = draw group 0's). A
// spot light's group (draw camera = its own) does not qualify.
bool MainFrameFor(const Renderer& r, const Frame& shadow, Frame& m, int32_t& group) {
    const int32_t si = r._currentSceneGroup;
    if (si < 0 || si >= r._sceneGroups._size) return false;
    const int32_t gi = r._sceneGroups._data[si]._drawGroupIndex;
    if (gi < 0 || gi >= r._drawGroups._size || gi == r._currentDrawGroup) return false;
    const RendererDrawGroup& g = r._drawGroups._data[gi];
    if (g._shader == 1) return false;
    FillFrame(m, g, r._detailId);
    group = gi;
    return SamePlanes(shadow.shadow, m.cull);
}

void DropStash() {
    if (g_stash.valid) ++g_counters.fusedUnused;
    g_stash.valid = false;
}

// ===========================================================================
// The dispatch's prefetches (one place: the serial loop and the pool tasks run the same).
// ===========================================================================
// `list` may move if a nested gather grows g_lists: the serial loop hands it afresh.
inline void DispatchPrefetch(Geometry* const* list, int32_t i, int32_t n, [[maybe_unused]] const void* meshVt) {
    if (i + kDispatchAhead < n) {
        const Geometry* ahead = list[i + kDispatchAhead];
        __builtin_prefetch(ahead);                         // vptr, _detailId
        __builtin_prefetch(&ahead->_worldBounds);          // the LOD box
        __builtin_prefetch(&ahead->_opacity);              // opacity pair, transform
        __builtin_prefetch(&ahead->_transform._previous);
        __builtin_prefetch(ahead + 1);                     // a MeshInstance's mesh / materials / animation
    }
    if constexpr (kDispatchMaterials) {
        // i+3: the vptr and `_materials` lines are the ones named at i+4.
        if (i + 3 < n) {
            const Geometry* g3 = list[i + 3];
            // The vptr picks the stage.
            if (*reinterpret_cast<const void* const*>(g3) == meshVt) {  // CAST-OK: vslot
                const auto* mi = static_cast<const Graphics::MeshInstance*>(g3);
                if (mi->_materials._data != nullptr) __builtin_prefetch(mi->_materials._data);
            }
        }
        // i+2: the array line named at i+3; the MaterialInstances AddDrawItem reads.
        if (i + 2 < n) {
            const Geometry* g2 = list[i + 2];
            // The vptr picks the stage.
            if (*reinterpret_cast<const void* const*>(g2) == meshVt) {  // CAST-OK: vslot
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
}

// ===========================================================================
// DISPATCHPAR (issue #253) -- ext.gather_threads: the fused gather's two dispatches,
// the shadow group's and the main group's, as two tasks on the helper pool.
// ===========================================================================
// dispatch_par.h has the order argument. Each task dispatches into a stand-in Renderer
// (its `proxy`): `_drawGroups` holds one group whose header (render buffer, both
// cameras, shader, pass mask) is a copy of the real group's, `_currentDrawGroup` is 0,
// and `_alphaT` / `_frameIndex` / `_shaderDetail` / `_detailId` are the real renderer's.
// `_sceneGroups` is empty and `_drawables` unused.
//
// What a task runs (ParSafe): exact MeshInstances with a mesh and no null material and no
// material with a rendered texture. MeshInstance::AddDrawItems (mesh.cpp, retail
// 0x180100f20) reads the instance, its mesh and materials, the renderer's `_alphaT` and the
// current group's camera position and `_shader`, bumps its group's g_gatherSlots slot
// (gather_prefetch.h: the shadow group's and the main group's slots differ, each on its
// own line) and calls the `_extLevelGathered` seam (Mesh.h: thread-safe, a locked queue),
// then Renderer::AddDrawItem (wave9_lane_gfx.cpp, 0x1801057b0) per triangle list, which
// reads the group header and the material, appends to the group's pass list
// (DrawItemArrayResize: the locked allocator) and bumps the same slot; its one other path,
// MaterialInstance::RenderTextures (a nested scene draw on the real renderer), runs only for
// a material with a rendered texture, which ParSafe leaves to the main thread. Every other
// Geometry (lights: AddLight writes the renderer's drawables; particles, edge meshes, the
// world geometry, status icons) is a cut, dispatched live on the main thread at its place.
//
// The main group's items are made at the shadow gather, before the rest of the main
// group's globals and before the shadow list's cuts, rather than at the main gather. The
// inputs they read are not written in between: the instances, meshes and materials as
// for the fused walk itself (FUSED above: the dispatches in between are const
// AddDrawItems calls), and the renderer state the task copied, which the main gather
// compares before it replays (PreStillValid; otherwise it dispatches as before). The
// COMPPROF=4 shadow `DrawDispatchJoined` (ReplayVerified, below) checks the joined pass
// lists against a serial dispatch.
struct alignas(128) DispatchTask {
    Renderer*                     proxy = nullptr;   // made at the first use; freed by ReleaseDispatch only
    const Scene*                  scene = nullptr;
    const std::vector<Geometry*>* list  = nullptr;   // valid during the pool pass only
    const void*                   meshVt = nullptr;
    std::vector<DispatchCut>      cuts;
    int32_t                       ends[kDispatchPasses] = {};
};
struct DispatchPair {
    DispatchTask task[2];   // 0 = the shadow group, 1 = the main group
};
// One pair per nesting depth (a cut's nested gather can be fused too); boxed so a pair
// stays put while a deeper one is added.
std::vector<std::unique_ptr<DispatchPair>> g_dispatch;

// Main thread, outside any Renderer::Draw (a row toggle): the pairs and their stand-ins freed.
void ReleaseDispatch() {
    for (std::unique_ptr<DispatchPair>& d : g_dispatch) {
        if (!d) continue;
        for (DispatchTask& t : d->task) {
            delete t.proxy;
            t.proxy = nullptr;
        }
    }
    g_dispatch.clear();
}

bool ParSafe(const Geometry* geo, const void* meshVt) {
    // An exact MeshInstance only.
    if (*reinterpret_cast<const void* const*>(geo) != meshVt) return false;  // CAST-OK: vslot
    const auto* mi = static_cast<const Graphics::MeshInstance*>(geo);
    if (mi->_mesh.Get() == nullptr) return false;
    for (int32_t k = 0; k < mi->_materials._size; ++k) {
        const Graphics::MaterialInstance* m = mi->_materials._data[k].Get();
        if (m == nullptr || m->_hasRenderedTexture != 0) return false;
    }
    return true;
}

// Main thread, before the pool pass: the stand-in for draw group `group` of `r`.
void ArmProxy(DispatchTask& t, const Renderer& r, int32_t group) {
    if (t.proxy == nullptr) t.proxy = new Renderer();
    Renderer& p = *t.proxy;
    const RendererDrawGroup& g = r._drawGroups._data[group];
    if (p._drawGroups._size == 0) {
        p.Graphics::Renderer::BeginDrawGroup(g._renderBuffer, g._camera, g._cullCamera, g._shader,
                                             g._materialTypeFlags);
    } else {
        RendererDrawGroup& pg = p._drawGroups._data[0];
        pg._renderBuffer      = g._renderBuffer;
        pg._camera            = g._camera;
        pg._cullCamera        = g._cullCamera;
        pg._shader            = g._shader;
        pg._materialTypeFlags = g._materialTypeFlags;
        for (int k = 0; k < kDispatchPasses; ++k) pg._items[k]._size = 0;   // the buffers are kept
    }
    p._currentDrawGroup = 0;
    p._alphaT           = r._alphaT;
    p._frameIndex       = r._frameIndex;
    p._shaderDetail     = r._shaderDetail;
    p._detailId         = r._detailId;
}

void RunTask(DispatchTask& t) {
    Geometry* const* const list = t.list->data();
    const int32_t n = static_cast<int32_t>(t.list->size());
    Renderer& p = *t.proxy;
    Collection::Array<RendererDrawItem>* const items = p._drawGroups._data[0]._items;
    const void* const meshVt = t.meshVt;
    RunDispatchTask(
        n,
        [&](int32_t i) {
            DispatchPrefetch(list, i, n, meshVt);
            return ParSafe(list[i], meshVt);
        },
        [&](int32_t i) { list[i]->AddDrawItems(*t.scene, p); },   // VIRTUAL, slot 3
        [&](int k) { return items[k]._size; }, t.cuts, t.ends);
}

void DispatchTaskFn(void* ctx, int32_t task, int32_t /*worker*/) {
    RunTask(static_cast<DispatchPair*>(ctx)->task[task]);
}

// Appends `count` items to a pass list. The list's buffer comes from the allocator
// DrawItemArrayResize uses (its free is the list's own); the capacity it leaves is not
// retail's growth rule, which no reader sees.
void AppendItems(Collection::Array<RendererDrawItem>& dst, const RendererDrawItem* src, int32_t count) {
    const int32_t old  = dst._size;
    const int32_t need = old + count;
    if (need > dst._capacity) {
        const int32_t cap = need + need / 2;
        auto* data = static_cast<RendererDrawItem*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(cap) * sizeof(RendererDrawItem), 8));
        if (old > 0) std::memcpy(data, dst._data, static_cast<size_t>(old) * sizeof(RendererDrawItem));
        if (dst._data != nullptr) System::MemoryAllocator::MemFree(dst._data);
        dst._data     = data;
        dst._capacity = cap;
    }
    std::memcpy(dst._data + old, src, static_cast<size_t>(count) * sizeof(RendererDrawItem));
    dst._size = need;
}

// The task's items into the renderer's current group, in the serial order; the cuts
// dispatched live (list entries read afresh from g_lists: a cut's nested gather can grow it).
void ReplayTask(const DispatchTask& t, const Scene& scene, Renderer& r, int32_t depth) {
    const RendererDrawGroup& pg = t.proxy->_drawGroups._data[0];
    ReplayDispatch(
        t.cuts, t.ends,
        [&](int k, int32_t first, int32_t count) {
            AppendItems(r._drawGroups._data[r._currentDrawGroup]._items[k], pg._items[k]._data + first, count);
        },
        [&](int32_t index) {
            Geometry* geo = g_lists[static_cast<size_t>(depth)][static_cast<size_t>(index)];
            geo->AddDrawItems(scene, r);                   // VIRTUAL, slot 3
        });
}

// The two pads AddDrawItem never writes are not compared: they are stack bytes in retail too.
bool SameItem(const RendererDrawItem& a, const RendererDrawItem& b) {
    return a._geometry == b._geometry && a._material == b._material && a._instance == b._instance &&
           a._flags == b._flags && a._node == b._node &&
           std::memcmp(&a._sortKey, &b._sortKey, sizeof(float)) == 0 && a._maxInstances == b._maxInstances &&
           a._userData == b._userData && a._animationInstanceId == b._animationInstanceId;
}

// ===========================================================================
// COMPPROF=4: `DrawDispatchJoined`, the replay against the serial dispatch.
// ===========================================================================
// A gather is the last thing that fills its draw group before the group's sort
// (Renderer::Draw's pass 1, render_pipe's Pass1, SunLight / SpotLight::AddDrawItems:
// AddCulledDrawItems, then the sort), so the group's three pass lists at the replay's end
// are the joined pre-sort lists: the items made before the gather, the buffered items and
// the cuts' items.
//
// The reference is the serial dispatch on the real renderer at this gather's point: every
// entry of the visible list, in list order, AddDrawItems on the current group. A cut runs
// there, once (a cut can add a light, open a nested draw group or draw a rendered texture,
// so it cannot run twice), and the span it appended to each pass is recorded. The safe
// entries run a second time (the task ran them once); their g_gatherSlots counts are put
// back. The lists are then copied whole, cut back to their size at the gather's start and
// refilled by the new path: ReplayDispatch appends the task's buffered items and, at each
// cut, that cut's recorded span. The copy and the refilled lists are compared item by item
// over their whole length, so the interleaving of buffered items and cuts is checked, not
// only the safe items. The lists keep the new path's items, as without the check.
//
// For the shadow group the reference is its full serial order at the shadow gather, each
// cut in its place; for the main group it is the serial dispatch at the main gather, where
// retail dispatches it. Detail: (pass << 60) | (count mismatch ? 1 << 59 : 0)
// | (main group ? 1 << 58 : 0) | first differing item.
struct CutSpan {
    int32_t first[kDispatchPasses] = {};
    int32_t end[kDispatchPasses]   = {};
};
std::vector<RendererDrawItem> g_joinedRef[kDispatchPasses];   // COMPPROF=4 scratch
std::vector<CutSpan>          g_joinedCuts;

void ReplayVerified(const DispatchTask& t, const Scene& scene, Renderer& r, int32_t depth, bool mainGroup) {
    const int slot = GatherShaderSlot(r._drawGroups._data[r._currentDrawGroup]._shader);
    int32_t base[kDispatchPasses];
    for (int k = 0; k < kDispatchPasses; ++k) {
        base[k] = r._drawGroups._data[r._currentDrawGroup]._items[k]._size;
    }

    // The serial dispatch. g_lists, its entry and the draw group are read afresh: a cut's
    // nested gather can grow g_lists, a cut's BeginDrawGroup can grow _drawGroups.
    g_joinedCuts.clear();
    size_t nextCut = 0;
    const size_t n = g_lists[static_cast<size_t>(depth)].size();
    for (size_t i = 0; i < n; ++i) {
        Geometry* geo = g_lists[static_cast<size_t>(depth)][i];
        const bool cut = nextCut < t.cuts.size() && static_cast<size_t>(t.cuts[nextCut].index) == i;
        if (!cut) {
            const GatherSlotCounters saved = g_gatherSlots[slot];
            geo->AddDrawItems(scene, r);                   // VIRTUAL, slot 3
            g_gatherSlots[slot] = saved;
            continue;
        }
        ++nextCut;
        CutSpan span;
        for (int k = 0; k < kDispatchPasses; ++k) {
            span.first[k] = r._drawGroups._data[r._currentDrawGroup]._items[k]._size;
        }
        geo->AddDrawItems(scene, r);                       // VIRTUAL, slot 3 (the cut's one run)
        for (int k = 0; k < kDispatchPasses; ++k) {
            span.end[k] = r._drawGroups._data[r._currentDrawGroup]._items[k]._size;
        }
        g_joinedCuts.push_back(span);
    }

    // The reference copied whole, the lists back to the gather's start, the new path's refill.
    RendererDrawGroup& g = r._drawGroups._data[r._currentDrawGroup];
    for (int k = 0; k < kDispatchPasses; ++k) {
        const Collection::Array<RendererDrawItem>& list = g._items[k];
        g_joinedRef[k].assign(list._data, list._data + list._size);
        g._items[k]._size = base[k];
    }
    const RendererDrawGroup& pg = t.proxy->_drawGroups._data[0];
    size_t cutOrdinal = 0;
    ReplayDispatch(
        t.cuts, t.ends,
        [&](int k, int32_t first, int32_t count) {
            AppendItems(g._items[k], pg._items[k]._data + first, count);
        },
        [&](int32_t /*index*/) {
            if (cutOrdinal >= g_joinedCuts.size()) return;   // counted as a mismatch below
            const CutSpan& span = g_joinedCuts[cutOrdinal];
            ++cutOrdinal;
            for (int k = 0; k < kDispatchPasses; ++k) {
                const int32_t count = span.end[k] - span.first[k];
                if (count > 0) AppendItems(g._items[k], g_joinedRef[k].data() + span.first[k], count);
            }
        });

    // A cut the serial pass did not meet in the task's order counts as a mismatch (detail 0).
    bool same = nextCut == t.cuts.size() && cutOrdinal == g_joinedCuts.size();
    long long detail = 0;
    for (int k = 0; k < kDispatchPasses && same; ++k) {
        const int32_t refCount = static_cast<int32_t>(g_joinedRef[k].size());
        const int32_t newCount = g._items[k]._size;
        const RendererDrawItem* const ref = g_joinedRef[k].data();
        int32_t i = 0;
        while (i < refCount && i < newCount && SameItem(ref[i], g._items[k]._data[i])) ++i;
        if (refCount != newCount || i != refCount) {
            same = false;
            detail = (static_cast<long long>(k) << 60) | (refCount != newCount ? (1ll << 59) : 0) |
                     (mainGroup ? (1ll << 58) : 0) | i;
        }
    }
    StressHarness_Verify("DrawDispatchJoined", same, detail);
}

// The fused shadow gather's dispatch: both lists on the pool, then the shadow list's items
// into the current (shadow) group. The main list's wait in g_dispatch[depth] for the main
// gather (g_stash.pre).
void ParDispatchFused(const Scene& scene, Renderer& r, int32_t depth, int32_t mainGroup, bool verify) {
    if (static_cast<int32_t>(g_dispatch.size()) <= depth) g_dispatch.resize(static_cast<size_t>(depth) + 1);
    std::unique_ptr<DispatchPair>& pair = g_dispatch[static_cast<size_t>(depth)];
    if (!pair) pair = std::make_unique<DispatchPair>();
    DispatchPair& d = *pair;
    const void* const meshVt = Object::ClassVptr<Graphics::MeshInstance>::Get();
    DispatchTask& shadow = d.task[0];
    DispatchTask& main   = d.task[1];
    ArmProxy(shadow, r, r._currentDrawGroup);
    shadow.scene  = &scene;
    shadow.list   = &g_lists[static_cast<size_t>(depth)];
    shadow.meshVt = meshVt;
    ArmProxy(main, r, mainGroup);
    main.scene  = &scene;
    main.list   = &g_stash.list;
    main.meshVt = meshVt;
    // Before anything that can nest: a fused gather nested in a cut below overwrites the stash.
    g_stash.pre      = true;
    g_stash.preGroup = mainGroup;

    g_pool->Publish(&DispatchTaskFn, &d, 2);
    const Ext::GatherThreads::HelperPool::PassResult pr = g_pool->Finish();
    shadow.list = nullptr;
    main.list   = nullptr;
    ++g_counters.dispatchPar;
    g_counters.dispatchHelper   += static_cast<uint64_t>(pr.tasks - pr.callerTasks);
    g_counters.dispatchWaitTicks += static_cast<uint64_t>(pr.waitTicks);
    g_counters.dispatchCuts     += shadow.cuts.size() + main.cuts.size();

    // COMPPROF=4: DrawDispatchJoined dispatches the shadow list serially, cuts in place, and
    // checks the shadow group's whole pre-sort lists against it (ReplayVerified).
    if (verify) {
        ReplayVerified(shadow, scene, r, depth, false);
    } else {
        ReplayTask(shadow, scene, r, depth);
    }
}

// The main gather, handed a pre-dispatched list: the renderer state the task copied is
// still the renderer's.
bool PreStillValid(const Renderer& r, int32_t depth) {
    if (r._currentDrawGroup != g_stash.preGroup) return false;
    if (static_cast<int32_t>(g_dispatch.size()) <= depth) return false;
    if (!g_dispatch[static_cast<size_t>(depth)]) return false;
    const Renderer& p = *g_dispatch[static_cast<size_t>(depth)]->task[1].proxy;
    const RendererDrawGroup& g  = r._drawGroups._data[r._currentDrawGroup];
    const RendererDrawGroup& pg = p._drawGroups._data[0];
    return g._shader == pg._shader && g._materialTypeFlags == pg._materialTypeFlags &&
           std::memcmp(&g._camera._cameraToWorld.translation, &pg._camera._cameraToWorld.translation,
                       sizeof(g._camera._cameraToWorld.translation)) == 0 &&
           std::memcmp(&r._alphaT, &p._alphaT, sizeof(float)) == 0;
}

// Scene::_extDrawPass1End: Renderer::Draw's pass 1 is over (no gather follows in
// this frame). The spin ends here, and a fused list nobody took is dropped.
void ExtDrawPass1End() {
    if (g_pool != nullptr) g_pool->Park();
    DropStash();
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
        g_pool      = nullptr;
        g_par.pool  = nullptr;
        g_fpar.pool = nullptr;
        if (helpers > 0) {
            g_pool = new Ext::GatherThreads::HelperPool(helpers);
            if (g_pool->Helpers() == 0) {
                delete g_pool;
                g_pool = nullptr;
            }
        }
        if (g_pool != nullptr) {
            g_par.pool  = g_pool;
            g_fpar.pool = g_pool;
        } else {
            g_par  = ParWalk{};                        // the slots' memory released
            g_fpar = FusedParWalk{};
            ReleaseDispatch();                         // DISPATCHPAR: the pairs and their stand-ins
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
    FillFrame(f, *g, renderer->_detailId);

    const int32_t depth = g_depth++;
    if (static_cast<int32_t>(g_lists.size()) <= depth) g_lists.resize(static_cast<size_t>(depth) + 1);
    std::vector<Geometry*>& list = g_lists[static_cast<size_t>(depth)];
    list.clear();

    const bool verify = g_verifyOn != 0;
    bool fused = false;   // FUSED: this list came out of a fused walk
    bool fusedShadow = false;   // DISPATCHPAR: ... and this is its shadow gather
    bool replay = false;        // DISPATCHPAR: ... and this is its main gather, the list already dispatched
    int32_t mainGroup = -1;
    Frame fm;
    if (g_stash.valid && g_stash.scene == self && g_stash.renderer == renderer && g_stash.depth == depth &&
        g_stash.frameIndex == renderer->_frameIndex && g_stash.cleanGen == g_cleanGen &&
        g_stash.logGen == g_logGen && SameWalk(g_stash.f, f)) {
        // FUSED: the main group, walked by the shadow gather just before.
        list.swap(g_stash.list);
        g_stash.valid = false;
        fused = true;
        ++g_counters.fusedServed;
        if (g_stash.pre) {
            replay = PreStillValid(*renderer, depth);
            if (!replay) ++g_counters.dispatchStale;
        }
    } else if (!f.notShadow && MainFrameFor(*renderer, f, fm, mainGroup)) {
        // FUSED: the shadow group, with the main group's walk in the same traversal.
        DropStash();
        g_hits.clear();
        TaskStats stats;
        if (__builtin_expect(g_parHelpers > 0, 0)) {
            if (!g_pool->Awake()) {
                g_pool->Wake();
                ++g_counters.parWakes;
            }
            const ParPassResult r = FusedParGather(g_fpar, self, f, fm, side, verify, g_hits);
            stats = r.stats;
            g_counters.parPasses    += 1;
            g_counters.parTasks     += static_cast<uint64_t>(r.tasks);
            g_counters.parHelper    += static_cast<uint64_t>(r.helperTasks);
            g_counters.parWaitTicks += static_cast<uint64_t>(r.waitTicks);
            if (r.sideGrew) ++g_counters.parSideGrew;
        } else {
            FusedBoxTest test{ f, fm, side, verify, stats };
            FusedWalk(self, f, fm, g_hits, test);
        }
        g_counters.nodesTested += stats.nodesTested;
        g_counters.refreshes   += stats.refreshes;
        g_counters.geoTouched  += g_hits.size();
        if (verify) {
            // COMPPROF=4: the DrawGatherSide checks the walk recorded, one call per check.
            for (uint64_t k = stats.sideMismatches; k < stats.sideChecks; ++k)
                StressHarness_Verify("DrawGatherSide", true, 0);
            for (uint64_t k = 0; k < stats.sideMismatches; ++k)
                StressHarness_Verify("DrawGatherSide", false, k < 4 ? stats.sideDetail[k] : 0);
        }
        g_stash.list.clear();
        FusedSplit(f, fm, g_hits, list, g_stash.list);
        g_stash.valid      = true;
        g_stash.scene      = self;
        g_stash.renderer   = renderer;
        g_stash.depth      = depth;
        g_stash.frameIndex = renderer->_frameIndex;
        g_stash.cleanGen   = g_cleanGen;
        g_stash.logGen     = g_logGen;
        g_stash.f          = fm;
        g_stash.pre        = false;   // DISPATCHPAR: ParDispatchFused sets it
        g_stash.preGroup   = -1;
        f.boxes += fm.boxes;   // the stress report's per-site count: both groups' tests, here
        fused = true;
        fusedShadow = true;
        ++g_counters.fusedWalks;
    } else if (__builtin_expect(g_parHelpers > 0, 0)) {
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
    g_counters.visibleByShader[GatherShaderSlot(g->_shader)] += list.size();   // issue #253

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
        StressHarness_Verify(fused ? "DrawGatherFused" : "DrawGather", same,
                             same ? 0 : (static_cast<long long>(ref.size()) << 32) |
                                            static_cast<long long>(firstDiff));
    }
    FrustumCull_CountSite(kFrustumCullSiteExtDrawGather, f.boxes);   // CULLROUTE: once per gather

    // DISPATCHPAR (ext.gather_threads): the fused gather's two dispatches on the pool; the
    // main gather that follows replays the main group's.
    if (fusedShadow && g_parHelpers > 0) {
        ParDispatchFused(*self, *renderer, depth, mainGroup, verify);
        --g_depth;
        return;
    }
    if (replay) {
        const DispatchTask& t = g_dispatch[static_cast<size_t>(depth)]->task[1];
        if (verify) {
            ReplayVerified(t, *self, *renderer, depth, true);   // COMPPROF=4: DrawDispatchJoined
        } else {
            ReplayTask(t, *self, *renderer, depth);
        }
        --g_depth;
        return;
    }

    // The dispatch: retail's virtual slot 3 over the list, in order.
    const int32_t n = static_cast<int32_t>(list.size());
    [[maybe_unused]] const void* meshVt = nullptr;
    if constexpr (kDispatchMaterials) meshVt = Object::ClassVptr<Graphics::MeshInstance>::Get();
    for (int32_t i = 0; i < n; ++i) {
        // `list` may move if a nested gather grows g_lists: index it afresh.
        DispatchPrefetch(g_lists[static_cast<size_t>(depth)].data(), i, n, meshVt);
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
        g_stash = MainStash{};                     // FUSED: no kept list, its memory released
        ReleaseDispatch();                         // DISPATCHPAR: the pairs and their stand-ins
        std::vector<FusedHit>().swap(g_hits);
        std::vector<std::vector<Geometry*>>().swap(g_lists);
    }
    g_installed = want;
}

}  // namespace

void Sync() {
    const bool want = Ext::Ext().draw_gather;
    if (want != g_installed) SyncSeams(want);
    SyncThreads();   // CULLPAR: ext.gather_threads follows both rows
    // The pass-1 end parks the helpers and drops an untaken fused list (FUSED): installed
    // whenever the extension is (Sync runs outside any Renderer::Draw).
    Scene::_extDrawPass1End = (g_installed || g_pool != nullptr) ? &ExtDrawPass1End : nullptr;
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
