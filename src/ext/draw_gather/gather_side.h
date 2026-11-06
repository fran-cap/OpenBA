// ============================================================================
// src/ext/draw_gather/gather_side.h -- ext.draw_gather's side array (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWGATHER.md. Standard C++ only: the unit
// tests (tests/unit/ext_draw_gather_test.cpp) link gather_side.cpp alone.
//
// THE SIDE ARRAY. One 32-byte record per Graphics::Scene grid node id, beside
// the scene (never inside a retail struct): the node geometry's world box
// (Geometry +0x4c, the six floats the per-object frustum tests read) and two
// HINTS, the geometry's material-type flags (+0x30) and detail id (+0x28). A
// parallel array holds the Geometry* each record was taken from (its owner).
//
//   box    EXACT while the record is valid. Geometry::Clean (0x1800a4420) is
//          the box's only writer on a live geometry; the extension's Scene::Clean
//          refreshes the record whenever Clean reports a change (a box recompute
//          always reports one), and Scene::AddObject -- the other Clean caller --
//          logs the id (Graphics::SceneAddLog), which invalidates the record.
//   hints  NOT trusted: refreshed on every Clean (they sit in the cache line
//          Clean already loads for _dirtyFlags), but MeshInstance::SetMesh can
//          OR new bits in without dirtying anything. They only choose the ORDER
//          of the node's tests (NodeVisible); the live flags decide.
//
// NodeVisible: retail's per-object test is `flagsOK && detailOK && boxOK`
// (cull.cpp, AddCulledDrawItems), all three pure. Any order gives the same
// answer; the side array lets the box tests run first, from the dense record,
// so a node rejected by the frustum never touches its Geometry.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "graphics/gather_prefetch.h"   // issue #253: kGatherShaderSlots (the per-shader split)

namespace Ext {
namespace DrawGather {

// One node's record. `box` is a ::Box's bits: min x, y, z, max x, y, z.
struct SideRecord {
    float    box[6];
    uint32_t flagsHint;    // Geometry::_materialTypeFlags when last seen
    int32_t  detailHint;   // Geometry::_detailId when last seen
};
static_assert(sizeof(SideRecord) == 32, "two records per cache line");

// The per-object test in either order. `hintPass` = the hints say the live
// flag/detail test passes; then the box tests (from the record) run first.
// Both callables must be pure: the result is liveOk() && boxOk() either way.
template <class LiveOk, class BoxOk>
inline bool NodeVisible(bool hintPass, LiveOk&& liveOk, BoxOk&& boxOk) {
    if (hintPass) return boxOk() && liveOk();
    return liveOk() && boxOk();
}

inline bool HintPasses(const SideRecord& r, uint32_t passMask, int32_t rendererDetail) {
    return (r.flagsHint & passMask) != 0 && r.detailHint <= rendererDetail;
}

class SideArray {
public:
    // The record of node `id` was taken from `owner` and nothing has invalidated it.
    bool Valid(int32_t id, const void* owner) const {
        return id >= 0 && id < static_cast<int32_t>(_owner.size()) && owner != nullptr &&
               _owner[static_cast<size_t>(id)] == owner;
    }
    const SideRecord& At(int32_t id) const { return _rec[static_cast<size_t>(id)]; }

    // A full refresh (grows the arrays to hold `id`).
    void Store(int32_t id, const void* owner, const float box[6], uint32_t flags, int32_t detail);
    // Hints only; the caller has checked Valid(id, owner).
    void StoreHints(int32_t id, uint32_t flags, int32_t detail) {
        SideRecord& r = _rec[static_cast<size_t>(id)];
        r.flagsHint  = flags;
        r.detailHint = detail;
    }
    void Invalidate(int32_t id) {
        if (id >= 0 && id < static_cast<int32_t>(_owner.size())) _owner[static_cast<size_t>(id)] = nullptr;
    }
    // CULLPAR: grows the arrays (as Store would) so every id < n has a slot; new slots
    // are invalid. The parallel walk calls it before any task runs, so no Store during
    // the walk grows them (a growth would move every record under the other threads).
    void Reserve(int32_t n);
    void Clear();                  // every record invalid, memory released
    int32_t Size() const { return static_cast<int32_t>(_owner.size()); }

private:
    std::vector<SideRecord>  _rec;
    std::vector<const void*> _owner;
};

// Scene::Clean's per-node upkeep (the extension's Clean, after retail's
// `dirty && Clean()` for live node `id`): a full refresh when Clean reported a
// change or the record is not this owner's -- `box()` is called only then, so
// an unchanged node's box line is never loaded -- the hints otherwise.
// Returns true for a full refresh.
template <class BoxFn>
inline bool UpkeepAfterClean(SideArray& side, int32_t id, const void* owner, bool cleanChanged,
                             BoxFn&& box, uint32_t flags, int32_t detail) {
    if (cleanChanged || !side.Valid(id, owner)) {
        float b[6];
        box(b);
        side.Store(id, owner, b, flags, detail);
        return true;
    }
    side.StoreHints(id, flags, detail);
    return false;
}

// The side arrays of the scenes the extension has served, keyed by the Scene's
// address (the game scene, the menu scene, the minimap's stack scene). A scene
// destroyed and another built at its address is handled by the add log (every
// node of the new scene is logged as it is added). Least recently used is
// evicted past kMaxScenes; an evicted scene starts empty (all invalid) next time.
class SceneSides {
public:
    static constexpr int kMaxScenes = 4;
    SideArray& Acquire(const void* scene);       // finds or makes (evicting LRU)
    SideArray* Find(const void* scene);          // null when not held
    void ForgetAll();                            // every record of every scene dropped
    int  Count() const { return _n; }

private:
    struct Slot { const void* scene = nullptr; uint64_t lastUse = 0; SideArray side; };
    Slot     _slots[kMaxScenes];
    int      _n = 0;
    uint64_t _clock = 0;
};

// The counters the stress report prints (draw_gather.cpp fills them; TakeWindow
// in draw_gather.h hands a window out and zeroes it).
struct Counters {
    uint64_t gathers      = 0;   // replaced AddCulledDrawItems calls
    uint64_t nodesTested  = 0;   // grid nodes visited in surviving cells (records read)
    uint64_t visible      = 0;   // pushed onto the visible list (= AddDrawItems dispatches)
    uint64_t visibleByShader[kGatherShaderSlots] = {};   // issue #253: `visible` by the group's shader
    uint64_t geoTouched   = 0;   // nodes whose Geometry the walk had to read (live flags / refresh)
    uint64_t refreshes    = 0;   // per-node full record refreshes (gather or Clean)
    uint64_t cleans       = 0;   // replaced Scene::Clean calls
    uint64_t rebuilds     = 0;   // whole-table forgets (toggle on, log overflow)
    uint64_t logged       = 0;   // AddObject ids drained from the log
    // CULLPAR (ext.gather_threads): summed once per parallel pass, never per object.
    uint64_t parPasses    = 0;   // gathers walked in parallel
    uint64_t parTasks     = 0;   // frontier subtrees (tasks) over those passes
    uint64_t parHelper    = 0;   // ... of which a helper ran
    uint64_t parWaitTicks = 0;   // the main thread's join wait, QPC ticks (sum over passes)
    uint64_t parWakes     = 0;   // pool wakes (one per frame that gathered)
    uint64_t parSideGrew  = 0;   // passes whose side array changed size during the walk (must stay 0)
    // FUSED (issue #253): the shadow group's walk that also walked the main group's.
    uint64_t fusedWalks   = 0;   // shadow gathers that walked both groups in one traversal
    uint64_t fusedServed  = 0;   // main gathers handed that walk's list (no walk of their own)
    uint64_t fusedUnused  = 0;   // fused lists dropped unused (pass 1 ended, or a newer fused walk)
    // DISPATCHPAR (issue #253, ext.gather_threads): the fused gather's two dispatches on the pool.
    uint64_t dispatchPar       = 0;   // fused gathers whose two dispatches ran as pool tasks
    uint64_t dispatchHelper    = 0;   // ... tasks a helper ran (of two per gather)
    uint64_t dispatchWaitTicks = 0;   // the main thread's join wait, QPC ticks
    uint64_t dispatchCuts      = 0;   // list entries the tasks left to the main thread (not ParSafe)
    uint64_t dispatchStale     = 0;   // main gathers that found the copied renderer state changed (must stay 0)
};

}  // namespace DrawGather
}  // namespace Ext
