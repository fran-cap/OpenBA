// ============================================================================
// src/engine/graphics/cull.cpp — Graphics::Scene::AddCulledDrawItems and the
// two quadtree-walk helpers it owns.
// ============================================================================
//
//   ?AddCulledDrawItems@Scene@Graphics@@AEBAXAEAVRenderer@2@@Z
//                                   retail 0x1801017a0, ordinal 1543, 2950 B
//   Graphics_Scene_CullStack_Init          retail 0x18005f2f0,  201 B (was FUN_)
//   Graphics_Scene_CullStack_PushChildren  retail 0x18005ead0,  737 B (was FUN_)
//
// BATCH 4 (the Graphics render/cull/sun-camera cluster). The `Array<Geometry*>`
// reserve this function opens with (retail 0x18005f980) is a member of the
// Collection::Array family and lives in src/engine/collection/array.cpp with
// its siblings, not here.
//
// ---------------------------------------------------------------------------
// WHAT IT DOES
// ---------------------------------------------------------------------------
// Walk the Scene's implicit QUADTREE (it is a quadtree, not an octree: four
// children, and only X and Y are halved per level), frustum-test each node's
// box, and for every object in a surviving cell dispatch Geometry vtable slot 3
// ("AddDrawItems") with (scene, renderer). Two frusta are in play:
//
//   * the CULL frustum, from the current draw group's cullCamera (group+0xe0),
//     transformed into world space by that camera's cameraToWorld;
//   * the SHADOW frustum, from the draw group's OTHER camera at group+0x08,
//     same treatment. It is only consulted when the group's `shader` field
//     (+0x1b8) is exactly 1, and objects are tested against it with a box grown
//     by their own bounding radius and pushed 11 units along the cull frustum's
//     plane-0 normal — that is the shadow caster extension.
//
// The two 6-bit plane masks travel together in one uint32: cull mask in the low
// 16 bits, shadow mask in the high 16. `Volume::Frustum::Contains` returns 0x40
// for "entirely outside" and otherwise the reduced mask, so a child node
// inherits only the planes its parent was still straddling.
//
// ---------------------------------------------------------------------------
// THE CULL STACK, AND WHAT RETAIL LEAVES UNINITIALISED
// ---------------------------------------------------------------------------
// `CullStack` is a 0x2430-byte STACK object in AddCulledDrawItems' own frame:
// a small header, 256 fixed records of 0x24 bytes, and the Scene pointer. It is
// never heap-allocated and never escapes.
//
// Init (0x18005f2f0) zeroes only the first 0x18 bytes of each record — the
// position and half-extents. `node`, `depth` and `mask` are NOT written for
// records 1..255, so a freshly initialised stack holds uninitialised stack
// bytes there. That is not sloppiness to be tidied: nothing reads a record
// above `top`, and zeroing it would make a byte comparison against the DLL's
// own stack diverge on whatever the caller happened to leave behind
// (reference_oracle_must_not_compare_unwritten_bytes.md). The loop below
// reproduces the partial zero exactly.
//
// ---------------------------------------------------------------------------
// ORDER OF OPERATIONS
// ---------------------------------------------------------------------------
// The box arithmetic is transcribed in retail's order. It feeds a plane-vs-box
// test whose sign decides visibility, so a re-grouped sum can flip an object in
// or out of frame at the silhouette. Do not simplify it.
// ============================================================================

#include "rt_libm.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "volume/frustum_cull.h"   // FRUSTUMCULL: the prepared cull (NOT RETAIL; same answers)
#include "graphics/Camera.h"
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"             // GATHERPF: MeshInstance's fields, by name, for the dispatch arm
#include "graphics/Scene.h"
#include "graphics/gather_prefetch.h"  // GATHERPF: the prefetch-ahead switches (NOT RETAIL; hints only)
#include "system/MemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// ENDGAME B.3.5 block F: these five name the REAL `Graphics::` classes now — the
// whole of this file's object surface (`Scene`, `Geometry`, `Renderer`,
// `RendererDrawGroup`) is block E's or block F's, and the members below are
// written against them. `RtGraphics::Camera` is not needed at all any more: the
// two frusta come out of `RendererDrawGroup::_camera` / `_cullCamera`.
using Graphics::Geometry;
using Graphics::Renderer;
using Graphics::RendererDrawGroup;
using Graphics::Scene;
using Graphics::SceneNode;

// --- retail .rdata constants, lifted as raw bits ----------------------------
//   0x180161694 = 0x3f000000    0.5f
//   0x1801616d8 = 0x40000000    2.0f    half-extent -> full-extent scale
//   0x1801616ec = 0x41000000    8.0f    node box max-Z bias
//   0x1801616f4 = 0x41300000   11.0f    shadow caster extension length
//   0x18016175c = 0xbf800000   -1.0f
//   0x18016176c = 0xc0400000   -3.0f    node box min-Z bias
constexpr float kHalf     =  0.5f;
constexpr float kTwo      =  2.0f;
constexpr float kMaxZBias =  8.0f;
constexpr float kShadowExtend = 11.0f;
constexpr float kMinusOne = -1.0f;
constexpr float kMinZBias = -3.0f;

// One entry of the walk stack. Stride 0x24, pinned by the `* 0x24` in both
// helpers and by AddCulledDrawItems' own indexing.
struct CullRecord {
    float    x, y, z;                 // +0x00  node centre
    float    halfX, halfY, halfZ;     // +0x0c  HALF extents (doubled at use)
    int32_t  node;                    // +0x18  quadtree node index
    int32_t  depth;                   // +0x1c
    uint32_t mask;                    // +0x20  shadowMask<<16 | cullMask
};
static_assert(sizeof(CullRecord) == 0x24, "cull record stride is 0x24");
static_assert(offsetof(CullRecord, halfX) == 0x0c, "record::halfX @ +0x0c");
static_assert(offsetof(CullRecord, node)  == 0x18, "record::node  @ +0x18");
static_assert(offsetof(CullRecord, mask)  == 0x20, "record::mask  @ +0x20");

struct CullStack {
    uint8_t    _head00[8];        // +0x0000  never written by Init
    int32_t    top;               // +0x0008  index of the top record; -1 = empty
    float      minBias[3];        // +0x000c  {0, 0, -3.0f}  added to the node box min
    float      maxBias[3];        // +0x0018  {0, 0, +8.0f}  added to the node box max
    CullRecord rec[256];          // +0x0024
    uint8_t    _pad2424[4];       // +0x2424  alignment before the Scene pointer
    const Scene* scene;           // +0x2428
};
static_assert(offsetof(CullStack, top)     == 0x0008, "CullStack::top @ +0x08");
static_assert(offsetof(CullStack, minBias) == 0x000c, "CullStack::minBias @ +0x0c");
static_assert(offsetof(CullStack, maxBias) == 0x0018, "CullStack::maxBias @ +0x18");
static_assert(offsetof(CullStack, rec)     == 0x0024, "CullStack::rec @ +0x24");
static_assert(offsetof(CullStack, scene)   == 0x2428, "CullStack::scene @ +0x2428");

// The array header AddCulledDrawItems builds its visible-object list in. Same
// {data, size, capacity} triple as Collection::Array; spelled locally because
// the element type is `Geometry*` and the append below is INLINED by retail.
struct GeomList {
    Geometry** data;
    int32_t    size;
    int32_t    capacity;
};
static_assert(sizeof(GeomList) == 16, "Array<Geometry*> header is 16 bytes");

// The Volume::Frustum bodies (volume/frustum.cpp, declared in Volume.h), by name:
// `??DFrustum@Volume@@` 0x1800f9310. `?Contains@Frustum@Volume@@` 0x1800f8e80 is
// reached through FRUSTUMCULL's prepared table (frustum_cull.h; kFastCull = false
// in frustum_cull.h makes it RtVolume_Frustum_Contains again).
Frustum* FrustumTimes(const Frustum* self, Frustum* ret, const Transform3* t) {
    return RtVolume_Frustum_MulTransform3(self, ret, t);
}

}  // namespace

// Collection::Array<Geometry*>::Reserve(16384), retail 0x18005f980. Ported in
// src/engine/collection/array.cpp with the rest of that family (it is one of
// them, not a Graphics body) and CALLED DIRECTLY rather than through
// ResolveAddr: the registry door would only buy instrumentation, and reaching
// it by name keeps <windows.h> — whose <wingdi.h> declares a function called
// `Rectangle` and so breaks Volume.h — out of this translation unit
// (the reason rt_port.h does not pull in standalone/resolve.h either).
namespace rtcoll { void RtColl_ArrayReserve_18005f980(void* a); }

namespace {

void ReserveVisibleList(GeomList* list) { rtcoll::RtColl_ArrayReserve_18005f980(list); }

// The inlined append: `SetSize(size + 1)` with the new element as the fill.
// Transcribed from 0x180102100 / 0x1801021ec (retail emits it twice, once per
// arm of the shadow branch; the two are instruction-identical).
void PushVisible(GeomList* list, Geometry* g) {
    Geometry** const old     = list->data;
    const int32_t    oldSize = list->size;
    const int32_t    n       = oldSize + 1;
    list->size = n;
    if (list->capacity < n) {
        // 32-bit LEA arithmetic, wrap included; same fold as SetSizeCore's.
        list->capacity = (oldSize != 0) ? static_cast<int32_t>(n * 3) : n;
        list->data = static_cast<Geometry**>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(list->capacity)) * 8, 8));
    }
    const int32_t start = (old == list->data) ? oldSize : 0;
    for (int32_t i = start; i < n; ++i) {
        Geometry** dst = list->data + i;
        if (dst == nullptr) continue;          // retail tests the COMPUTED address
        *dst = (i < oldSize) ? old[i] : g;
    }
    if (old != list->data && old != nullptr) System::MemoryAllocator::MemFree(old);
}

// The node box, in world space, with the stack's per-axis biases applied.
// Half-extents are stored halved and doubled here, exactly as retail does
// (`* DAT_1801616d8`) — except on Z, which is NOT doubled.
//
// *** THERE ARE TWO OF THESE AND THEY ARE NOT THE SAME FUNCTION. ***
// Retail builds the box twice, once per frustum, and MSVC scheduled the two
// max-X and max-Y sums with DIFFERENT GROUPINGS:
//     cull   max.y = (bias + y) + halfY*2      shadow  max.y = (y + halfY*2) + bias
//     cull   max.x = (halfX*2 + x) + bias      shadow  max.x = (x + halfX*2) + bias
// Float addition is not associative, so folding them into one helper would be
// a silent last-bit change on the axis a plane test is most likely to be
// straddling (reference_moved_term_hides_where_terms_cancel.md). Every other
// component IS identical between the two and is written once, in NodeBoxCull,
// which NodeBoxShadow starts from.
Box NodeBoxCull(const CullStack* st, const CullRecord& r) {
    Box b;
    b.min.x = st->minBias[0] + (r.x - r.halfX * kTwo);
    b.min.y = st->minBias[1] + (r.y - r.halfY * kTwo);
    b.min.z = st->minBias[2] + (r.z - r.halfZ);
    b.max.x = r.halfX * kTwo + r.x + st->maxBias[0];
    b.max.y = st->maxBias[1] + r.y + r.halfY * kTwo;
    b.max.z = st->maxBias[2] + r.z + r.halfZ;
    return b;
}
Box NodeBoxShadow(const CullStack* st, const CullRecord& r) {
    Box b = NodeBoxCull(st, r);
    b.max.x = r.x + r.halfX * kTwo + st->maxBias[0];
    b.max.y = r.y + r.halfY * kTwo + st->maxBias[1];
    return b;
}

// ---------------------------------------------------------------------------
// GATHERPF (NOT RETAIL; gather_prefetch.h, docs/extensions/handins/GATHERPF.md)
// ---------------------------------------------------------------------------
// Hints only. Every helper is always_inline: GCC treats __builtin_prefetch as
// touching no memory, so an out-of-line helper whose only effect is a prefetch
// is classed pure and its call deleted (PREFETCH.md section 4).

// The Geometry lines the ring test reads: `_detailId` (+0x28), `_materialTypeFlags`
// (+0x30) and `_worldBounds` (+0x4c..+0x63). The span +0x28..+0x63 is 60 bytes, so
// it lies on one or two lines and the line of its first and of its last word
// cover it; `_worldBounds.max.z` is the word at +0x60..+0x63.
[[gnu::always_inline]] inline void PrefetchCullLines(const Geometry* geo) {
    __builtin_prefetch(&geo->_detailId);
    __builtin_prefetch(&geo->_worldBounds.max.z);
}

// Arm 1. `node` is the ring node the walk is visiting. Loads ONLY ring nodes'
// `_next` / `_object`: the ring is circular (scene_grid.cpp GridThread), so every
// node reached by `_next` is one this cell's walk visits (or has visited, when the
// hop wraps to the head) and reads both words of.
[[gnu::always_inline]] inline void PrefetchChainAhead(const SceneNode* nodes, const SceneNode& node) {
    int32_t ahead = node._next;
    for (int k = 1; k < Graphics::GatherPf::kChainStride; ++k) ahead = nodes[ahead]._next;
    const SceneNode& an = nodes[ahead];
    PrefetchCullLines(an._object);                 // object i+S: its cull lines
    __builtin_prefetch(&nodes[an._next]);          // node i+S+1: its SceneNode line (address only)
}

}  // namespace

// The address point of the compiler's MeshInstance vtable (mesh_vtable_alias.cpp, the one
// graphics TU built without LTO: it names the table by its assembler symbol). Equality
// with an object's vptr means the dynamic type is exactly MeshInstance; only the dispatch
// arm's hints depend on it, so a mismatch only means those hints are not issued.
namespace Graphics { const void* GatherPf_MeshInstanceVtable(); }

namespace {

// Arm 2, stage i+2S: the vptr line and the line at +0xc0. `geo + 1` is one past
// the Geometry subobject (Geometry is 0xC0 bytes): a pointer the language lets us
// form, never dereferenced; on a MeshInstance it is `&_mesh` (+0xc0), the line of
// `_mesh`, `_materials` (+0xc8) and the head of `_animation` (+0xd8).
[[gnu::always_inline]] inline void PrefetchDispatchFar(const Geometry* geo) {
    __builtin_prefetch(geo);
    __builtin_prefetch(geo + 1);
}

// Arm 2, stage i+S. The vptr load is the word the virtual call on this entry
// loads S iterations later.
[[gnu::always_inline]] inline void PrefetchDispatchNear(const Geometry* geo, const void* meshVt) {
    const void* vp = *reinterpret_cast<const void* const*>(geo);  // CAST-OK: vslot (the vptr picks the stage)
    if (vp != meshVt) return;
    const Graphics::MeshInstance* mi = static_cast<const Graphics::MeshInstance*>(geo);
    __builtin_prefetch(&mi->_opacityPrev);         // +0x80: the opacity pair (+0x7c sits on it or on the box line)
    __builtin_prefetch(&mi->_animation);           // +0xd8: `_instanced`, `_layers`
    __builtin_prefetch(&mi->_sortBias);            // +0x11c
    if constexpr (Graphics::GatherPf::kDispatchMaterials) {
        // `_materials._data` is read by MeshInstance::AddDrawItems for every
        // triangle list; its line was prefetched at i+2S.
        __builtin_prefetch(mi->_materials._data);
    }
}

}  // namespace

// ============================================================================
// Graphics_Scene_CullStack_Init — retail 0x18005f2f0
// ============================================================================
// (CullStack* self, const Scene* scene, float minZBias, float maxZBias).
// AddCulledDrawItems is the only caller that passes the Scene it is walking;
// FUN_18005e750 is the other (unported) caller of this helper.
static CullStack* RtGraphics_Scene_CullStackInit(CullStack* self, const Scene* scene,
                                                 float minZBias, float maxZBias) {

    self->top        = -1;
    self->minBias[2] = minZBias;                 // +0x14, written before the pair
    self->minBias[0] = 0.0f;                     // +0x0c/+0x10 as one qword
    self->minBias[1] = 0.0f;
    self->maxBias[0] = 0.0f;                     // +0x18/+0x1c as one qword
    self->maxBias[1] = 0.0f;
    self->maxBias[2] = maxZBias;                 // +0x20

    // 256 records, first 0x18 bytes each. `node`, `depth` and `mask` are
    // deliberately NOT touched — see the banner.
    for (int i = 0; i < 256; ++i) {
        self->rec[i].x = 0.0f; self->rec[i].y = 0.0f; self->rec[i].z = 0.0f;
        self->rec[i].halfX = 0.0f; self->rec[i].halfY = 0.0f; self->rec[i].halfZ = 0.0f;
    }

    self->scene = scene;
    const float h = scene->_grid._cellSize * kHalf;
    self->top += 1;                              // 0

    CullRecord& r = self->rec[self->top];
    r.x = 0.0f; r.y = 0.0f; r.z = 0.0f;
    r.halfX = h; r.halfY = h; r.halfZ = 0.0f;
    r.node  = 0;
    r.depth = 0;
    r.mask  = 0x003f003fu;                       // both 6-plane masks, all set
    return self;
}

// ============================================================================
// Graphics_Scene_CullStack_PushChildren — retail 0x18005ead0
// ============================================================================
// Pop the top node and, unless it is already at the deepest level, push its
// four quadtree children with the (already reduced) mask the caller supplies.
// Child n gets node index `parent*4 + n`, n in 1..4.
static void RtGraphics_Scene_CullStackPushChildren(CullStack* self, uint32_t mask) {

    const int32_t t = self->top;
    self->top = t - 1;                                   // pop first, always

    const int32_t depth = self->rec[t].depth + 1;
    if (depth >= self->scene->_grid._levelCount) return;       // leaf: stays popped

    const float   x    = self->rec[t].x;
    const float   y    = self->rec[t].y;
    const float   z    = self->rec[t].z;
    const int32_t node = self->rec[t].node;
    float         hx   = self->rec[t].halfX;
    float         hy   = self->rec[t].halfY;
    const float   hz   = self->rec[t].halfZ;

    self->top = t;                                       // child 1 reuses the slot

    hx = hx * kHalf;
    hy = hy * kHalf;
    const float nhx = hx * kMinusOne;
    const float nhy = hy * kMinusOne;

    struct { float dx, dy; int32_t n; } kChild[4] = {
        { nhx, nhy, 1 }, { hx, nhy, 2 }, { nhx, hy, 3 }, { hx, hy, 4 },
    };
    for (int i = 0; i < 4; ++i) {
        CullRecord& c = self->rec[self->top];
        c.x = x + kChild[i].dx;
        c.y = y + kChild[i].dy;
        c.z = z + hz;
        c.halfX = hx;
        c.halfY = hy;
        c.halfZ = hz;
        c.node  = node * 4 + kChild[i].n;
        c.depth = depth;
        c.mask  = mask;
        // Retail increments after the first three children only; `top` ends on
        // the LAST child, which is what a top-of-stack index means.
        if (i != 3) self->top += 1;
    }
}

// ============================================================================
// ?AddCulledDrawItems@Scene@Graphics@@AEBAXAEAVRenderer@2@@Z — retail 0x1801017a0
// ============================================================================

// ############################################################################
// ENDGAME B.3 step 5 (block F) — THE BODY, MOVED ONTO THE MEMBER
// ############################################################################
//
// `?AddCulledDrawItems@Scene@Graphics@@AEBAXAEAVRenderer@2@@Z` is `AEBA` — a
// PRIVATE const member that this DLL exports anyway
// (reference_aeaa_is_private_not_unexported.md). `Scene.h` declares it PUBLIC and
// records retail's access in a comment, because its registered door is a free
// `extern "C"` function that has to call it — the same choice block E made for
// `SunLight::CreateTargets` (`AEAA`). Access control is not ABI; the mangled name
// the door registers is.
//
// FIVE THINGS THE CONVERSION MADE NAMEABLE, each previously a raw byte offset and
// each now `static_assert`ed in Camera.h:
//   group + 0x1bc  ->  `RendererDrawGroup::_materialTypeFlags`
//                      (`?GetMaterialTypeFlags@Renderer@` 0x180017ff0 returns it)
//   group + 0x0e0  ->  `_cullCamera`, and +0x144 is its `_cameraToWorld`
//   group + 0x008  ->  `_camera`, and +0x6c is ITS `_cameraToWorld`
//                      — the "OTHER camera" this file's banner describes, now a
//                      named field with four independent witnesses
//   group + 0x1b8  ->  `_shader`
//   renderer+0x48  ->  `Renderer::_detailId` (`?GetDetailId@` 0x180018070)
//
// The two frusta are still copied through `::Frustum*` cursors rather than by
// member name, because `Volume::Frustum::operator*` is reached through an export
// door that takes pointers; the SOURCE of each copy is now a named member.
// ############################################################################

// GATHERPF: the COMPPROF `gather` row's counters (gather_prefetch.h). Main thread;
// the harness reads and resets them once per window.
uint64_t g_gatherVisible      = 0;
uint64_t g_gatherDrawItems    = 0;   // incremented by Renderer::AddDrawItem (wave9_lane_gfx.cpp)

namespace Graphics {

void Scene::AddCulledDrawItems(Renderer& rendererRef) const {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWGATHER.md): scene_ext.h --
    // ext.draw_gather replaces the whole gather (the same walk over a dense side
    // array, the same visible list in the same order). Null in the faithful
    // build: retail's body below runs unchanged.
    if (_extCulledGather != nullptr) { _extCulledGather(*this, rendererRef); return; }

    const Scene* self     = this;
    Renderer*    renderer = &rendererRef;

    const RendererDrawGroup* g = &renderer->_drawGroups[renderer->_currentDrawGroup];

    const uint32_t passMask = g->_materialTypeFlags;             // group +0x1bc

    // The cull frustum, from `_cullCamera` (group +0xe0) into world space.
    ::Frustum cullFrustum;
    FrustumTimes(&g->_cullCamera._frustum, &cullFrustum, &g->_cullCamera._cameraToWorld);

    // plane[0]'s normal, the direction the shadow-caster extension runs along.
    const float extX = cullFrustum.planes[0].x;
    const float extY = cullFrustum.planes[0].y;
    const float extZ = cullFrustum.planes[0].z;

    // The shadow frustum, from the draw group's OTHER camera, `_camera` at
    // group +0x08. `BeginDrawGroup` fills it from its third argument and
    // `?GetCamera@Renderer@` returns it; see Camera.h.
    ::Frustum shadowFrustum;
    FrustumTimes(&g->_camera._frustum, &shadowFrustum, &g->_camera._cameraToWorld);

    // FRUSTUMCULL (NOT RETAIL, faithful perf work; frustum_cull.h): the two
    // frusta's prepared tables. Built here, after their only writer (the
    // FrustumTimes just above); nothing below writes either frustum (they are
    // read through `const Frustum*` and the three plane-0 loads only), so every
    // test in the walk gets RtVolume_Frustum_Contains's answer bit for bit.
    // `verify` (BANISHED_STRESS_COMPPROF=4 only) is read once, here.
    FrustumCullPlanes cullPlanes, shadowPlanes;
    FrustumCull_Prepare(&cullFrustum, &cullPlanes);
    FrustumCull_Prepare(&shadowFrustum, &shadowPlanes);
    const bool verify = g_frustumCullVerify != 0;
    uint64_t   boxesTested = 0;
    auto contains = [verify, &boxesTested](const FrustumCullPlanes* p, const Box* box, unsigned int m) {
        ++boxesTested;
        return verify ? FrustumCull_ContainsVerified(p, box, m) : FrustumCull_Contains(p, box, m);
    };

    GeomList list = { nullptr, 0, 0 };
    ReserveVisibleList(&list);

    uint32_t shadowMask = 0x3f;

    CullStack st;
    RtGraphics_Scene_CullStackInit(&st, self, kMinZBias, kMaxZBias);

    // `shader != 1` means "no shadow pass": objects go straight into the list
    // and the second frustum is never consulted.
    const bool notShadow = (g->_shader != 1);

    while (st.top >= 0) {
        const int32_t t    = st.top;
        const int32_t cell = st.rec[t].node;

        if (self->_grid._cellCounts._data[cell] < 1) {           // empty cell: just pop
            st.top -= 1;
            continue;
        }

        const uint32_t packed = st.rec[t].mask;
        uint32_t cullMask = packed & 0xffffu;
        if (cullMask != 0) {
            const Box b = NodeBoxCull(&st, st.rec[t]);
            cullMask = contains(&cullPlanes, &b, cullMask);
            if (cullMask == 0x40) {                        // entirely outside
                st.top -= 1;
                continue;
            }
        }
        if (!notShadow) {
            shadowMask = packed >> 16;
            if (shadowMask != 0) {
                const Box b = NodeBoxShadow(&st, st.rec[t]);
                shadowMask = contains(&shadowPlanes, &b, shadowMask);
            }
        }

        const int32_t head = self->_grid._cellHeads._data[cell];
        if (head >= 0) {
            int32_t cur = head;
            do {
                const SceneNode& node = self->_grid._nodes._data[cur];
                Geometry* const  geo  = node._object;

                // GATHERPF arm 1: object i+S's cull lines and node i+S+1's line,
                // issued before this object's (mispredict-prone) flag / box branches
                // so a flush does not cancel them.
                if constexpr (GatherPf::kChain) PrefetchChainAhead(self->_grid._nodes._data, node);

                if ((geo->_materialTypeFlags & passMask) != 0 &&
                    geo->_detailId <= renderer->_detailId) {

                    unsigned int r = 0;
                    if (cullMask != 0) {
                        Box b = geo->_worldBounds;
                        r = contains(&cullPlanes, &b, cullMask);
                    }
                    if (r != 0x40) {
                        if (notShadow) {
                            PushVisible(&list, geo);
                        } else {
                            // Grow the object's box by its own bounding-sphere
                            // radius and by the caster extension, then test it
                            // against the shadow frustum.
                            const Box& wb = geo->_worldBounds;
                            const float cz = (wb.min.z + wb.max.z) * kHalf;
                            const float cy = (wb.min.y + wb.max.y) * kHalf;
                            const float cx = (wb.min.x + wb.max.x) * kHalf;
                            const float hz = (wb.max.z - wb.min.z) * kHalf;
                            const float hy = (wb.max.y - wb.min.y) * kHalf;
                            const float hx = (wb.max.x - wb.min.x) * kHalf;
                            const float rad = RtLibm::Sqrt((hy * hy + hx * hx) + hz * hz);

                            const float lz = cz - rad;
                            const float ly = cy - rad;
                            const float lx = cx - rad;

                            Box b;
                            b.max.z = (wb.max.z <= lz) ? lz : wb.max.z;
                            b.max.y = (wb.max.y <= ly) ? ly : wb.max.y;
                            b.max.x = (wb.max.x <= lx) ? lx : wb.max.x;
                            b.min.z = (lz <= wb.min.z) ? lz : wb.min.z;
                            b.min.y = (ly <= wb.min.y) ? ly : wb.min.y;
                            b.min.x = (lx <= wb.min.x) ? lx : wb.min.x;

                            const float ez = rad + cz + extZ * kShadowExtend;
                            const float ey = rad + cy + extY * kShadowExtend;
                            const float ex = extX * kShadowExtend + cx + rad;

                            if (b.max.z <= ez) b.max.z = ez;
                            if (b.max.y <= ey) b.max.y = ey;
                            if (b.max.x <= ex) b.max.x = ex;
                            if (ez <= b.min.z) b.min.z = ez;
                            if (ey <= b.min.y) b.min.y = ey;
                            if (ex <= b.min.x) b.min.x = ex;

                            if (shadowMask == 0 ||
                                contains(&shadowPlanes, &b, shadowMask) != 0x40) {
                                PushVisible(&list, geo);
                            }
                        }
                    }
                }
                cur = node._next;
            } while (cur != head);
        }

        RtGraphics_Scene_CullStackPushChildren(&st, (shadowMask << 16) | cullMask);
    }

    // ENDGAME B.3.5 block F: this was a hand-inlined `(*(void***)geo)[3]` call
    // with its own `AddFn` typedef and `RtGraphics::kVSlotAddDrawItems`. It is now
    // an ORDINARY VIRTUAL CALL on a `Graphics::Geometry*` — slot 3, pure on the
    // base, and `?AddDrawItems@SunLight@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z`
    // (0x180106f50) is one of the overrides that confirmed both the index and the
    // argument tuple (Geometry.h's enum comment).
    //
    // GATHERPF arm 2: the list is an array, so the entries S and 2S ahead cost
    // no chase; both are entries this loop reads (and calls through) later.
    [[maybe_unused]] const void* meshVt = nullptr;
    if constexpr (GatherPf::kDispatch) meshVt = Graphics::GatherPf_MeshInstanceVtable();
    for (int32_t i = 0; i < list.size; ++i) {
        if constexpr (GatherPf::kDispatch) {
            constexpr int32_t S = GatherPf::kDispatchStride;
            if (i + 2 * S < list.size) PrefetchDispatchFar(list.data[i + 2 * S]);
            if (i + S < list.size) PrefetchDispatchNear(list.data[i + S], meshVt);
        }
        Geometry* geo = list.data[i];
        geo->AddDrawItems(*self, *renderer);                     // VIRTUAL, slot 3
    }

    g_frustumCullBoxes += boxesTested;   // FRUSTUMCULL: the stress report's counter (once per call)
    ++g_frustumCullPasses;
    FrustumCull_CountSite(kFrustumCullSiteScene, boxesTested);   // CULLROUTE: the per-site breakdown
    g_gatherVisible += static_cast<uint64_t>(list.size);   // GATHERPF: the COMPPROF `gather` row

    if (list.data != nullptr) System::MemoryAllocator::MemFree(list.data);
}

}  // namespace Graphics
