// ============================================================================
// src/ext/draw_gather/gather_walk.h -- ext.draw_gather's quadtree walk, and
// ext.gather_threads' parallel form of it (CULLPAR)
// ============================================================================
// NOT RETAIL. Included by draw_gather.cpp and by tests/unit/ext_gather_threads_test.cpp
// only (everything here has internal linkage). docs/extensions/handins/DRAWGATHER.md
// (the walk) and CULLPAR.md (the parallel walk).
//
// The walk below was draw_gather.cpp's, moved here unchanged so the unit tests run
// the real thing; the one edit is the split of Walk into WalkStack (the loop) and
// Walk (the root record + the loop), which the object proof shows inlines back.
//
// THE PARALLEL WALK (THREADRESEARCH §2.3). The DFS order is fixed by
// CullStackPushChildren: a cell's own chain, then its subtrees 4, 3, 2, 1. The
// coordinator (the main thread) runs that same loop over the top kTopDepth levels,
// but it stops at a record of depth kTopDepth: such a record (with the masks it
// inherited) becomes a TASK. The plan is the pre-order sequence of SEGMENTS: an
// interior cell (its chain, with the masks its own tests gave) or a task. Each task
// is the subtree walk from its root record (WalkStack on a stack holding just that
// record), a function of that record and of data nobody writes during the walk, so
// concatenating the segments in plan order gives exactly the sequential list, for
// any helper count and any timing. Tasks run on the helpers and on the main thread
// (HelperPool); each has a private stack, output vector and counters, summed after
// the join.
//
// THE FUSED WALK (issue #253, at the end of this file): two groups' walks (the sun's
// shadow group and the main group) in one traversal, sequential and parallel; each
// group's list is its own walk's (DRAWGATHER.md section 10).
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "rt_libm.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "volume/frustum_cull.h"   // CULLROUTE: the prepared tables
#include "graphics/Geometry.h"
#include "graphics/Scene.h"
#include "ext/draw_gather/gather_side.h"
#include "ext/gather_threads/gather_threads.h"

namespace Ext {
namespace DrawGather {

namespace {

using Graphics::Geometry;
using Graphics::Scene;
using Graphics::SceneNode;

// ===========================================================================
// The walk -- cull.cpp's CullStack and helpers, copied record for record (the
// faithful file keeps its own; FRUSTUMCULL is changing that body and a shared
// header is the follow-up once both are merged, DRAWGATHER.md §8).
// ===========================================================================
constexpr float kHalf         =  0.5f;   // 0x180161694
constexpr float kTwo          =  2.0f;   // 0x1801616d8
constexpr float kMaxZBias     =  8.0f;   // 0x1801616ec
constexpr float kShadowExtend = 11.0f;   // 0x1801616f4
constexpr float kMinusOne     = -1.0f;   // 0x18016175c
constexpr float kMinZBias     = -3.0f;   // 0x18016176c

struct CullRecord {
    float    x, y, z;
    float    halfX, halfY, halfZ;
    int32_t  node;
    int32_t  depth;
    uint32_t mask;          // shadowMask<<16 | cullMask
};

struct CullStack {
    int32_t      top;
    float        minBias[3];
    float        maxBias[3];
    CullRecord   rec[256];
    const Scene* scene;
};

void CullStackInit(CullStack* self, const Scene* scene, float minZBias, float maxZBias) {
    self->top        = -1;
    self->minBias[0] = 0.0f; self->minBias[1] = 0.0f; self->minBias[2] = minZBias;
    self->maxBias[0] = 0.0f; self->maxBias[1] = 0.0f; self->maxBias[2] = maxZBias;
    self->scene = scene;
    const float h = scene->_grid._cellSize * kHalf;
    self->top += 1;
    CullRecord& r = self->rec[self->top];
    r.x = 0.0f; r.y = 0.0f; r.z = 0.0f;
    r.halfX = h; r.halfY = h; r.halfZ = 0.0f;
    r.node  = 0;
    r.depth = 0;
    r.mask  = 0x003f003fu;
}

void CullStackPushChildren(CullStack* self, uint32_t mask) {
    const int32_t t = self->top;
    self->top = t - 1;
    const int32_t depth = self->rec[t].depth + 1;
    if (depth >= self->scene->_grid._levelCount) return;

    const float   x    = self->rec[t].x;
    const float   y    = self->rec[t].y;
    const float   z    = self->rec[t].z;
    const int32_t node = self->rec[t].node;
    float         hx   = self->rec[t].halfX;
    float         hy   = self->rec[t].halfY;
    const float   hz   = self->rec[t].halfZ;

    self->top = t;
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
        if (i != 3) self->top += 1;
    }
}

// cull.cpp's two node boxes, with their two DIFFERENT max-X / max-Y groupings.
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

// The shadow-caster box of an object: cull.cpp's arithmetic, term for term.
Box ShadowCasterBox(const Box& wb, float extX, float extY, float extZ) {
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
    return b;
}

// What one walk needs about its frame.
struct Frame {
    ::Frustum cull;
    ::Frustum shadow;
    // CULLROUTE (volume/frustum_cull.h): the two frusta's prepared tables, built
    // in ExtCulledGather after the two MulTransform3 that fill `cull` / `shadow`
    // (their only writers; the walk reads the Frame through `const Frame&`).
    FrustumCullPlanes cullPlanes;
    FrustumCullPlanes shadowPlanes;
    float     extX, extY, extZ;
    uint32_t  passMask;
    int32_t   rendererDetail;
    bool      notShadow;
    bool      cullVerify;          // g_frustumCullVerify, read once per gather (COMPPROF=4)
    mutable uint64_t boxes;        // box tests this gather (the stress report's per-site count)
};

// The one frustum test (retail 0x1800f8e80), over a prepared table: the same
// answer as RtVolume_Frustum_Contains on the frustum the table came from.
inline unsigned int Contains(const Frame& f, const FrustumCullPlanes* p, const Box* b, unsigned int mask) {
    ++f.boxes;
    return FrustumCull_Test(p, b, mask, f.cullVerify);
}

// Does the object pass, given its box? Retail's box half of the per-object test.
bool BoxPasses(const Frame& f, const Box& wb, uint32_t cullMask, uint32_t shadowMask) {
    if (cullMask != 0) {
        Box b = wb;
        if (Contains(f, &f.cullPlanes, &b, cullMask) == 0x40) return false;
    }
    if (f.notShadow) return true;
    const Box b = ShadowCasterBox(wb, f.extX, f.extY, f.extZ);
    return shadowMask == 0 || Contains(f, &f.shadowPlanes, &b, shadowMask) != 0x40;
}

// The quadtree walk, retail's, with the per-object test as a policy:
// `test(cur, geo, cullMask, shadowMask)` -> push or not.
// CULLPAR: the loop over a prepared stack, so a frontier subtree (gather_walk.h's
// tasks below) runs the very same loop from its own root record. Each instantiation
// has one caller, so it inlines back into Walk as before.
template <class Test>
void WalkStack(const Scene* self, const Frame& f, CullStack& st, std::vector<Geometry*>& out, Test& test) {
    uint32_t shadowMask = 0x3f;

    while (st.top >= 0) {
        const int32_t t    = st.top;
        const int32_t cell = st.rec[t].node;

        if (self->_grid._cellCounts._data[cell] < 1) {
            st.top -= 1;
            continue;
        }

        const uint32_t packed = st.rec[t].mask;
        uint32_t cullMask = packed & 0xffffu;
        if (cullMask != 0) {
            const Box b = NodeBoxCull(&st, st.rec[t]);
            cullMask = Contains(f, &f.cullPlanes, &b, cullMask);
            if (cullMask == 0x40) {
                st.top -= 1;
                continue;
            }
        }
        if (!f.notShadow) {
            shadowMask = packed >> 16;
            if (shadowMask != 0) {
                const Box b = NodeBoxShadow(&st, st.rec[t]);
                shadowMask = Contains(f, &f.shadowPlanes, &b, shadowMask);
            }
        }

        const int32_t head = self->_grid._cellHeads._data[cell];
        if (head >= 0) {
            int32_t cur = head;
            do {
                const SceneNode& node = self->_grid._nodes._data[cur];
                Geometry* const  geo  = node._object;
                if (test(cur, geo, cullMask, shadowMask)) out.push_back(geo);
                cur = node._next;
            } while (cur != head);
        }

        CullStackPushChildren(&st, (shadowMask << 16) | cullMask);
    }
}

template <class Test>
void Walk(const Scene* self, const Frame& f, std::vector<Geometry*>& out, Test& test) {
    CullStack st;
    CullStackInit(&st, self, kMinZBias, kMaxZBias);
    WalkStack(self, f, st, out, test);
}

// Retail's per-object test, reading the Geometry in retail's order (the shadow's reference).
struct RetailTest {
    const Frame& f;
    bool operator()(int32_t, const Geometry* geo, uint32_t cullMask, uint32_t shadowMask) const {
        if ((geo->_materialTypeFlags & f.passMask) == 0 || geo->_detailId > f.rendererDetail)
            return false;
        return BoxPasses(f, geo->_worldBounds, cullMask, shadowMask);
    }
};



// ===========================================================================
// CULLPAR -- the parallel walk (ext.gather_threads).
// ===========================================================================

// The frontier depth. 2 = at most 16 subtrees under 5 interior cells: the heavy
// town's ~8,300 chain objects per pass (149,553 / 18 passes, GATHERPF) give ~520
// per subtree on average, over THREADRESEARCH §2.5's minimum chunk of ~200;
// depth 3 (64 subtrees) would give ~130, under it. The stress `gather` line's
// tasks per pass is the measurement (CULLPAR.md §2).
constexpr int32_t kTopDepth = 2;

// Per task (and the coordinator's interior work): plain counters, private to the
// thread running it, summed by the coordinator after the join. The DrawGatherSide
// shadow is recorded here and reported after the join, on the main thread
// (StressHarness_Verify is single-threaded).
struct TaskStats {
    uint64_t nodesTested    = 0;
    uint64_t geoTouched     = 0;
    uint64_t refreshes      = 0;
    uint64_t sideChecks     = 0;
    uint64_t sideMismatches = 0;
    int32_t  sideDetail[4]  = { 0, 0, 0, 0 };   // the first mismatching node ids
};

// draw_gather.cpp's SideTest with private counters (the side record first).
struct TaskSideTest {
    const Frame& f;
    SideArray&   side;
    bool         verify;
    TaskStats&   s;
    bool operator()(int32_t cur, const Geometry* geo, uint32_t cullMask, uint32_t shadowMask) {
        ++s.nodesTested;
        bool touched = false;
        if (!side.Valid(cur, geo)) {
            float box[6];
            std::memcpy(box, &geo->_worldBounds, sizeof(box));
            side.Store(cur, geo, box, geo->_materialTypeFlags, geo->_detailId);   // never grows: Reserve
            ++s.refreshes;
            touched = true;
        } else if (verify) {
            const bool same = std::memcmp(side.At(cur).box, &geo->_worldBounds, sizeof(float) * 6) == 0;
            ++s.sideChecks;
            if (!same) {
                if (s.sideMismatches < 4) s.sideDetail[s.sideMismatches] = cur;
                ++s.sideMismatches;
            }
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
        if (touched) ++s.geoTouched;
        return pass;
    }
};

// One plan entry, in pre-order: an interior cell's chain or a task.
struct Segment {
    int32_t  task;          // >= 0: the task; -1: the interior cell below
    int32_t  cell;
    uint32_t cullMask;
    uint32_t shadowMask;
    int32_t  begin, end;    // the interior cell's objects in ParWalk::interior
};

// A helper's own copy of the pass's Frame (the box counter is the only member a
// walk writes), on its own cache lines.
struct alignas(64) HelperFrame {
    Frame f;
};

// A task's output and counters, on their own cache lines.
struct alignas(64) TaskSlot {
    std::vector<Geometry*> out;
    TaskStats              s;
};

// What one parallel pass adds to the counters (draw_gather.cpp sums it into
// g_counters / f.boxes and replays the side shadow).
struct ParPassResult {
    TaskStats stats;                  // every task and the interior cells
    int32_t   tasks       = 0;
    int32_t   helperTasks = 0;
    int64_t   waitTicks   = 0;        // the join wait (QPC ticks)
    bool      sideGrew    = false;    // the side array changed size during the walk (never)
};

// The state the parallel walk keeps between passes (the vectors keep their
// capacity). Main thread only, except what a running task reads (the pass fields,
// its root) and writes (its slot, its helper's frame).
struct ParWalk {
    Ext::GatherThreads::HelperPool* pool = nullptr;
    std::vector<CullRecord>  roots;      // task -> its root record
    std::vector<Segment>     plan;
    std::vector<Geometry*>   interior;
    std::vector<TaskSlot>    slots;
    HelperFrame              frames[Cpu::kMaxCullHelpers];
    // The pass (written before Publish, read by the tasks).
    const Scene*             scene  = nullptr;
    const Frame*             frame  = nullptr;
    SideArray*               side   = nullptr;
    bool                     verify = false;
};

void ParRunTask(void* ctx, int32_t task, int32_t worker) {
    ParWalk& p = *static_cast<ParWalk*>(ctx);   // HelperPool::TaskFn's void* context  // CAST-OK: user_data
    const Frame& f = worker == 0 ? *p.frame : p.frames[worker - 1].f;
    TaskSlot& slot = p.slots[static_cast<size_t>(task)];
    slot.out.clear();
    slot.s = TaskStats{};
    TaskSideTest test{ f, *p.side, p.verify, slot.s };
    CullStack st;
    CullStackInit(&st, p.scene, kMinZBias, kMaxZBias);   // the biases and the scene ...
    st.rec[0] = p.roots[static_cast<size_t>(task)];      // ... and this subtree's root
    WalkStack(p.scene, f, st, slot.out, test);
}

void AddStats(TaskStats& to, const TaskStats& s) {
    to.nodesTested += s.nodesTested;
    to.geoTouched  += s.geoTouched;
    to.refreshes   += s.refreshes;
    for (uint64_t i = 0; i < s.sideMismatches && to.sideMismatches + i < 4; ++i)
        to.sideDetail[to.sideMismatches + i] = s.sideDetail[i < 4 ? i : 3];
    to.sideChecks     += s.sideChecks;
    to.sideMismatches += s.sideMismatches;
}

// The plan: Walk's loop over the top kTopDepth levels, in the same order and with
// the same box tests, stopping at the frontier. Box tests count into `f.boxes`.
void ParPlan(ParWalk& p, const Scene* self, const Frame& f) {
    p.roots.clear();
    p.plan.clear();
    uint32_t shadowMask = 0x3f;

    CullStack st;
    CullStackInit(&st, self, kMinZBias, kMaxZBias);

    while (st.top >= 0) {
        const int32_t t    = st.top;
        const int32_t cell = st.rec[t].node;

        if (self->_grid._cellCounts._data[cell] < 1) {   // the subtree walk's own first test
            st.top -= 1;
            continue;
        }
        if (st.rec[t].depth == kTopDepth) {              // the frontier: a task
            p.plan.push_back(Segment{ static_cast<int32_t>(p.roots.size()), cell, 0, 0, 0, 0 });
            p.roots.push_back(st.rec[t]);
            st.top -= 1;
            continue;
        }

        const uint32_t packed = st.rec[t].mask;
        uint32_t cullMask = packed & 0xffffu;
        if (cullMask != 0) {
            const Box b = NodeBoxCull(&st, st.rec[t]);
            cullMask = Contains(f, &f.cullPlanes, &b, cullMask);
            if (cullMask == 0x40) {
                st.top -= 1;
                continue;
            }
        }
        if (!f.notShadow) {
            shadowMask = packed >> 16;
            if (shadowMask != 0) {
                const Box b = NodeBoxShadow(&st, st.rec[t]);
                shadowMask = Contains(f, &f.shadowPlanes, &b, shadowMask);
            }
        }

        p.plan.push_back(Segment{ -1, cell, cullMask, shadowMask, 0, 0 });
        CullStackPushChildren(&st, (shadowMask << 16) | cullMask);
    }
}

// The parallel walk of one gather pass, appended to `out`: the same list as
// Walk(self, f, out, SideTest) for any helper count and any timing. `side` is the
// scene's side array; it is sized to the node count here, before any task runs.
// The pool must be awake (or have no helpers) for the helpers to take tasks; a
// parked pool only means the caller runs every task.
ParPassResult ParGather(ParWalk& p, const Scene* self, const Frame& f, SideArray& side, bool verify,
                        std::vector<Geometry*>& out) {
    ParPassResult r;
    // No Store during the walk may grow the arrays (a growth would move every record
    // under the other threads): every id the walk reaches is < the node count.
    side.Reserve(self->_grid._nodes.GetSize());
    const int32_t sideSize = side.Size();

    ParPlan(p, self, f);

    const int32_t nTasks = static_cast<int32_t>(p.roots.size());
    if (static_cast<int32_t>(p.slots.size()) < nTasks) p.slots.resize(static_cast<size_t>(nTasks));
    p.scene  = self;
    p.frame  = &f;
    p.side   = &side;
    p.verify = verify;
    const int helpers = p.pool != nullptr ? p.pool->Helpers() : 0;
    for (int w = 0; w < helpers; ++w) {
        p.frames[w].f            = f;
        p.frames[w].f.boxes      = 0;
        p.frames[w].f.cullVerify = false;   // the FrustumCull shadow reports on the main thread only
    }
    if (nTasks > 0 && p.pool != nullptr) p.pool->Publish(&ParRunTask, &p, nTasks);

    // The interior cells' chains, on the main thread, while the helpers start.
    p.interior.clear();
    {
        TaskSideTest test{ f, side, verify, r.stats };
        for (Segment& s : p.plan) {
            if (s.task >= 0) continue;
            s.begin = static_cast<int32_t>(p.interior.size());
            const int32_t head = self->_grid._cellHeads._data[s.cell];
            if (head >= 0) {
                int32_t cur = head;
                do {
                    const SceneNode& node = self->_grid._nodes._data[cur];
                    Geometry* const  geo  = node._object;
                    if (test(cur, geo, s.cullMask, s.shadowMask)) p.interior.push_back(geo);
                    cur = node._next;
                } while (cur != head);
            }
            s.end = static_cast<int32_t>(p.interior.size());
        }
    }

    if (nTasks > 0) {
        if (p.pool != nullptr) {
            const Ext::GatherThreads::HelperPool::PassResult pr = p.pool->Finish();
            r.helperTasks = pr.tasks - pr.callerTasks;
            r.waitTicks   = pr.waitTicks;
        } else {
            for (int32_t t = 0; t < nTasks; ++t) ParRunTask(&p, t, 0);
        }
    }
    r.tasks = nTasks;

    // The join is behind us: concatenate in plan order, sum the counters.
    for (const Segment& s : p.plan) {
        if (s.task < 0) {
            out.insert(out.end(), p.interior.begin() + s.begin, p.interior.begin() + s.end);
        } else {
            const TaskSlot& slot = p.slots[static_cast<size_t>(s.task)];
            out.insert(out.end(), slot.out.begin(), slot.out.end());
            AddStats(r.stats, slot.s);
        }
    }
    for (int w = 0; w < helpers; ++w) f.boxes += p.frames[w].f.boxes;
    r.sideGrew = side.Size() != sideSize;
    return r;
}

// ===========================================================================
// FUSED (issue #253) -- two groups' walks in one traversal.
// ===========================================================================
// The sun's shadow group (Frame A) and the main group (Frame B) walk the same quadtree
// in the same pre-order (CullStackPushChildren fixes it); each separate walk visits a
// subset of the cells, in that order, and within a cell the same chain in the same
// order. The fused walk visits the union: a record carries both groups' masks and a
// LIVE bit per group (clear below a cell that group's walk would have popped), each
// cell's tests are each group's own tests on that group's frame, in its order, and an
// object visited for a live group gets that group's per-object test. So the objects
// group G accepts, in walk order, are exactly G's separate walk's list. The per-object
// test is retail's `flagsOK && detailOK && boxOK` (pure; any order): the walk makes the
// box halves from the side record (one record load for both groups) and FusedSplit
// the live halves, reading each Geometry once for both groups.

// One fused survivor: bit 0 = group A's box test passed, bit 1 = group B's.
struct FusedHit {
    Geometry* geo;
    uint32_t  bits;
};

// Per stack slot, beside the CullStack record (which carries group A's packed mask):
// group B's packed mask and the live groups.
struct FusedExtra {
    uint32_t maskB;
    uint32_t live;
};

constexpr uint32_t kFusedRootLive = 3u;
constexpr size_t   kFusedLiveAhead = 16;

// One cell's frustum tests for one group, as WalkStack makes them; false = the group's
// walk pops this cell. `shadowMask` is WalkStack's carried value: 0x3f for a group with
// no shadow pass (it is never reassigned there), else the cell's own result.
inline bool CellMasks(const Frame& f, const CullStack* st, const CullRecord& r, uint32_t packed,
                      uint32_t& cullMask, uint32_t& shadowMask) {
    cullMask = packed & 0xffffu;
    if (cullMask != 0) {
        const Box b = NodeBoxCull(st, r);
        cullMask = Contains(f, &f.cullPlanes, &b, cullMask);
        if (cullMask == 0x40) return false;
    }
    shadowMask = 0x3f;
    if (!f.notShadow) {
        shadowMask = packed >> 16;
        if (shadowMask != 0) {
            const Box b = NodeBoxShadow(st, r);
            shadowMask = Contains(f, &f.shadowPlanes, &b, shadowMask);
        }
    }
    return true;
}

// The box halves of both groups' per-object tests, from the side record (SideBoxTest's
// upkeep and DrawGatherSide check, with private counters as TaskSideTest's).
struct FusedBoxTest {
    const Frame& fa;
    const Frame& fb;
    SideArray&   side;
    bool         verify;
    TaskStats&   s;
    uint32_t operator()(int32_t cur, const Geometry* geo, uint32_t live, uint32_t cmA, uint32_t smA,
                        uint32_t cmB, uint32_t smB) {
        ++s.nodesTested;
        if (!side.Valid(cur, geo)) {
            float box[6];
            std::memcpy(box, &geo->_worldBounds, sizeof(box));
            side.Store(cur, geo, box, geo->_materialTypeFlags, geo->_detailId);
            ++s.refreshes;
        } else if (verify) {
            const bool same = std::memcmp(side.At(cur).box, &geo->_worldBounds, sizeof(float) * 6) == 0;
            ++s.sideChecks;
            if (!same) {
                if (s.sideMismatches < 4) s.sideDetail[s.sideMismatches] = cur;
                ++s.sideMismatches;
            }
        }
        Box wb;
        std::memcpy(&wb, side.At(cur).box, sizeof(wb));
        uint32_t bits = 0;
        if ((live & 1u) != 0 && BoxPasses(fa, wb, cmA, smA)) bits |= 1u;
        if ((live & 2u) != 0 && BoxPasses(fb, wb, cmB, smB)) bits |= 2u;
        return bits;
    }
};

// The fused loop over a prepared stack; `extra[k]` belongs to `st.rec[k]`.
template <class Test>
void FusedWalkStack(const Scene* self, const Frame& fa, const Frame& fb, CullStack& st, FusedExtra* extra,
                    std::vector<FusedHit>& out, Test& test) {
    while (st.top >= 0) {
        const int32_t t    = st.top;
        const int32_t cell = st.rec[t].node;

        if (self->_grid._cellCounts._data[cell] < 1) {
            st.top -= 1;
            continue;
        }

        uint32_t live = extra[t].live;
        uint32_t cmA = 0;
        uint32_t smA = 0;
        uint32_t cmB = 0;
        uint32_t smB = 0;
        if ((live & 1u) != 0 && !CellMasks(fa, &st, st.rec[t], st.rec[t].mask, cmA, smA)) live &= ~1u;
        if ((live & 2u) != 0 && !CellMasks(fb, &st, st.rec[t], extra[t].maskB, cmB, smB)) live &= ~2u;
        if (live == 0) {
            st.top -= 1;
            continue;
        }

        const int32_t head = self->_grid._cellHeads._data[cell];
        if (head >= 0) {
            int32_t cur = head;
            do {
                const SceneNode& node = self->_grid._nodes._data[cur];
                Geometry* const  geo  = node._object;
                const uint32_t   bits = test(cur, geo, live, cmA, smA, cmB, smB);
                if (bits != 0) out.push_back(FusedHit{ geo, bits });
                cur = node._next;
            } while (cur != head);
        }

        // The children land in slots t..st.top (none at a leaf): theirs is this cell's B mask.
        CullStackPushChildren(&st, (smA << 16) | cmA);
        for (int32_t k = t; k <= st.top; ++k) extra[k] = FusedExtra{ (smB << 16) | cmB, live };
    }
}

template <class Test>
void FusedWalk(const Scene* self, const Frame& fa, const Frame& fb, std::vector<FusedHit>& out, Test& test) {
    CullStack  st;
    FusedExtra extra[256];
    CullStackInit(&st, self, kMinZBias, kMaxZBias);
    extra[0] = FusedExtra{ 0x003f003fu, kFusedRootLive };
    FusedWalkStack(self, fa, fb, st, extra, out, test);
}

// The live halves: each survivor's Geometry read once, its flags / detail tested against
// each group whose box test it passed; both lists keep the walk's order.
inline void FusedSplit(const Frame& fa, const Frame& fb, const std::vector<FusedHit>& hits,
                       std::vector<Geometry*>& outA, std::vector<Geometry*>& outB) {
    const size_t n = hits.size();
    for (size_t i = 0; i < n; ++i) {
        if (i + kFusedLiveAhead < n) {
            const Geometry* ahead = hits[i + kFusedLiveAhead].geo;
            __builtin_prefetch(&ahead->_detailId);
            __builtin_prefetch(&ahead->_materialTypeFlags);
        }
        Geometry* const geo    = hits[i].geo;
        const uint32_t  flags  = geo->_materialTypeFlags;
        const int32_t   detail = geo->_detailId;
        if ((hits[i].bits & 1u) != 0 && (flags & fa.passMask) != 0 && detail <= fa.rendererDetail)
            outA.push_back(geo);
        if ((hits[i].bits & 2u) != 0 && (flags & fb.passMask) != 0 && detail <= fb.rendererDetail)
            outB.push_back(geo);
    }
}

// Two walks are the same walk: the inputs Walk reads from its Frame (the cull frustum and,
// with a shadow pass, the shadow frustum and the caster extension, which comes from the
// cull frustum), bit for bit.
inline bool SamePlanes(const ::Frustum& a, const ::Frustum& b) {
    return std::memcmp(a.planes, b.planes, sizeof(a.planes)) == 0 && a.isOrthographic == b.isOrthographic;
}
inline bool SameWalk(const Frame& a, const Frame& b) {
    if (a.passMask != b.passMask || a.rendererDetail != b.rendererDetail || a.notShadow != b.notShadow)
        return false;
    if (!SamePlanes(a.cull, b.cull)) return false;
    return a.notShadow || SamePlanes(a.shadow, b.shadow);
}

// ---------------------------------------------------------------------------
// The fused walk on the helpers: ParGather's plan / tasks / plan-order concatenation,
// with FusedWalkStack's records. The same hits as FusedWalk for any helper count and
// any timing (a task is a function of its root record and extra, and of data nobody
// writes during the walk).
// ---------------------------------------------------------------------------
struct FusedSegment {
    int32_t  task;          // >= 0: the task; -1: the interior cell below
    int32_t  cell;
    uint32_t live;
    uint32_t cmA, smA, cmB, smB;
    int32_t  begin, end;    // the interior cell's hits in FusedParWalk::interior
};

struct FusedRoot {
    CullRecord rec;
    FusedExtra extra;
};

struct alignas(64) FusedHelperFrames {
    Frame a;
    Frame b;
};

struct alignas(64) FusedTaskSlot {
    std::vector<FusedHit> out;
    TaskStats             s;
};

// Main thread only, except what a running task reads (the pass fields, its root) and
// writes (its slot, its helper's frames), as ParWalk.
struct FusedParWalk {
    Ext::GatherThreads::HelperPool* pool = nullptr;
    std::vector<FusedRoot>     roots;
    std::vector<FusedSegment>  plan;
    std::vector<FusedHit>      interior;
    std::vector<FusedTaskSlot> slots;
    FusedHelperFrames          frames[Cpu::kMaxCullHelpers];
    const Scene*               scene  = nullptr;
    const Frame*               fa     = nullptr;
    const Frame*               fb     = nullptr;
    SideArray*                 side   = nullptr;
    bool                       verify = false;
};

void FusedParRunTask(void* ctx, int32_t task, int32_t worker) {
    FusedParWalk& p = *static_cast<FusedParWalk*>(ctx);   // HelperPool::TaskFn's void* context  // CAST-OK: user_data
    const Frame& fa = worker == 0 ? *p.fa : p.frames[worker - 1].a;
    const Frame& fb = worker == 0 ? *p.fb : p.frames[worker - 1].b;
    FusedTaskSlot& slot = p.slots[static_cast<size_t>(task)];
    slot.out.clear();
    slot.s = TaskStats{};
    FusedBoxTest test{ fa, fb, *p.side, p.verify, slot.s };
    CullStack  st;
    FusedExtra extra[256];
    CullStackInit(&st, p.scene, kMinZBias, kMaxZBias);
    const FusedRoot& root = p.roots[static_cast<size_t>(task)];
    st.rec[0] = root.rec;
    extra[0]  = root.extra;
    FusedWalkStack(p.scene, fa, fb, st, extra, slot.out, test);
}

// ParPlan's loop with FusedWalkStack's cell step.
void FusedParPlan(FusedParWalk& p, const Scene* self, const Frame& fa, const Frame& fb) {
    p.roots.clear();
    p.plan.clear();

    CullStack  st;
    FusedExtra extra[256];
    CullStackInit(&st, self, kMinZBias, kMaxZBias);
    extra[0] = FusedExtra{ 0x003f003fu, kFusedRootLive };

    while (st.top >= 0) {
        const int32_t t    = st.top;
        const int32_t cell = st.rec[t].node;

        if (self->_grid._cellCounts._data[cell] < 1) {
            st.top -= 1;
            continue;
        }
        if (st.rec[t].depth == kTopDepth) {
            p.plan.push_back(FusedSegment{ static_cast<int32_t>(p.roots.size()), cell, 0, 0, 0, 0, 0, 0, 0 });
            p.roots.push_back(FusedRoot{ st.rec[t], extra[t] });
            st.top -= 1;
            continue;
        }

        uint32_t live = extra[t].live;
        uint32_t cmA = 0;
        uint32_t smA = 0;
        uint32_t cmB = 0;
        uint32_t smB = 0;
        if ((live & 1u) != 0 && !CellMasks(fa, &st, st.rec[t], st.rec[t].mask, cmA, smA)) live &= ~1u;
        if ((live & 2u) != 0 && !CellMasks(fb, &st, st.rec[t], extra[t].maskB, cmB, smB)) live &= ~2u;
        if (live == 0) {
            st.top -= 1;
            continue;
        }

        p.plan.push_back(FusedSegment{ -1, cell, live, cmA, smA, cmB, smB, 0, 0 });
        CullStackPushChildren(&st, (smA << 16) | cmA);
        for (int32_t k = t; k <= st.top; ++k) extra[k] = FusedExtra{ (smB << 16) | cmB, live };
    }
}

// The fused walk of one gather pass on the pool, appended to `out`: the same hits as
// FusedWalk(self, fa, fb, out, FusedBoxTest).
ParPassResult FusedParGather(FusedParWalk& p, const Scene* self, const Frame& fa, const Frame& fb,
                             SideArray& side, bool verify, std::vector<FusedHit>& out) {
    ParPassResult r;
    side.Reserve(self->_grid._nodes.GetSize());
    const int32_t sideSize = side.Size();

    FusedParPlan(p, self, fa, fb);

    const int32_t nTasks = static_cast<int32_t>(p.roots.size());
    if (static_cast<int32_t>(p.slots.size()) < nTasks) p.slots.resize(static_cast<size_t>(nTasks));
    p.scene  = self;
    p.fa     = &fa;
    p.fb     = &fb;
    p.side   = &side;
    p.verify = verify;
    const int helpers = p.pool != nullptr ? p.pool->Helpers() : 0;
    for (int w = 0; w < helpers; ++w) {
        p.frames[w].a            = fa;
        p.frames[w].a.boxes      = 0;
        p.frames[w].a.cullVerify = false;
        p.frames[w].b            = fb;
        p.frames[w].b.boxes      = 0;
        p.frames[w].b.cullVerify = false;
    }
    if (nTasks > 0 && p.pool != nullptr) p.pool->Publish(&FusedParRunTask, &p, nTasks);

    p.interior.clear();
    {
        FusedBoxTest test{ fa, fb, side, verify, r.stats };
        for (FusedSegment& s : p.plan) {
            if (s.task >= 0) continue;
            s.begin = static_cast<int32_t>(p.interior.size());
            const int32_t head = self->_grid._cellHeads._data[s.cell];
            if (head >= 0) {
                int32_t cur = head;
                do {
                    const SceneNode& node = self->_grid._nodes._data[cur];
                    Geometry* const  geo  = node._object;
                    const uint32_t   bits = test(cur, geo, s.live, s.cmA, s.smA, s.cmB, s.smB);
                    if (bits != 0) p.interior.push_back(FusedHit{ geo, bits });
                    cur = node._next;
                } while (cur != head);
            }
            s.end = static_cast<int32_t>(p.interior.size());
        }
    }

    if (nTasks > 0) {
        if (p.pool != nullptr) {
            const Ext::GatherThreads::HelperPool::PassResult pr = p.pool->Finish();
            r.helperTasks = pr.tasks - pr.callerTasks;
            r.waitTicks   = pr.waitTicks;
        } else {
            for (int32_t t = 0; t < nTasks; ++t) FusedParRunTask(&p, t, 0);
        }
    }
    r.tasks = nTasks;

    for (const FusedSegment& s : p.plan) {
        if (s.task < 0) {
            out.insert(out.end(), p.interior.begin() + s.begin, p.interior.begin() + s.end);
        } else {
            const FusedTaskSlot& slot = p.slots[static_cast<size_t>(s.task)];
            out.insert(out.end(), slot.out.begin(), slot.out.end());
            AddStats(r.stats, slot.s);
        }
    }
    for (int w = 0; w < helpers; ++w) {
        fa.boxes += p.frames[w].a.boxes;
        fb.boxes += p.frames[w].b.boxes;
    }
    r.sideGrew = side.Size() != sideSize;
    return r;
}

}  // namespace
}  // namespace DrawGather
}  // namespace Ext
