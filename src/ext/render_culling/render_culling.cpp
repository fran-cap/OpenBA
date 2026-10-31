// src/ext/render_culling/render_culling.cpp -- see render_culling.h. NOT RETAIL: ext.render_culling (class A) and
// ext.anim_batch (class B), docs/extensions/handins/PERFXEXT.md.
#include "render_culling.h"
#include "graphics/Mesh.h"   // ENDGAME B.3.9 SS2: MeshInstance::_rootOnlyUpdates
// ENDGAME B.3.5: Graphics::Camera / Renderer / Geometry, ahead of the
// <windows.h> that "resolve.h" pulls in — <wingdi.h> declares a FUNCTION named
// Rectangle, which breaks Volume.h's Volume::Rectangle (the <windows.h> trap).
#include "graphics/Camera.h"
#include "graphics/Geometry.h"
#include <cstdint>
#include "system/Value.h"
#include <cstdio>
#include <cstring>
#include <algorithm>   // std::remove / remove_if (Forget*)
#include <new>       // placement new — the retslot door body at the call site
#include <vector>
#include "math/Math.h"       // -Iruntime/types
#include "volume/Volume.h"   // B3R: RtVolume_Frustum_MulTransform3
#include "Component/ModelComponent.h"   // B4: meshInstance / transform by name
#include "graphics/Animation.h"         // B4: Animation::_boneCount / _frameCount / _keys
#include "graphics/AnimationGroup.h"    // B4: AnimationGroup::_clips, GetAnimation
#include "graphics/AnimationState.h"    // B4: AnimationState::Layer
#include "Entity/ComponentEntity.h"     // PERFXTBL: model->_entity.Get() (the side table's key)
#include "Entity/EntityList.h"          // EntityList_MarkUpdateTreeDirty (the side table's first build)
#include "ext/registry.h"               // Ext::Ext(): the two toggles, read once per frame
#include "side_table.h"                // PERFXTBL: BatchHash + the per-entity side table
#include "stress_harness.h"             // g_verifyOn / StressHarness_Verify (COMPPROF=4 shadows)

namespace Ext {
namespace RenderCulling {

Flags g_flags = {};
int64_t g_walkIndex = -1;   // PERFXTBL: the snapshot index of the entity being ticked

namespace {

// ---- per-frame frustum -------------------------------------------------------
// Volume::Frustum = 6 planes of {nx,ny,nz,d} at +0x00..+0x60 (Frustum::Contains,
// Runtime 0x1800f8e80: inside side is n.p - d > 0), plus one trailing dword; 0x64 bytes.
// Camera::GetFrustum copies 0x64 bytes; MulTransform3 with the camera-to-world Transform3
// at camera+0x64 (48 bytes) gives world-space planes — the same math as
// GeomDraw_CollectDraw (src/game/Game/geometry_draw.cpp).
float    s_planes[6][4];
bool     s_haveFrustum = false;
unsigned s_frameSerial = 0;
bool     s_simRanThisFrame = false;
bool     s_cullingOn = false;   // ext.render_culling as BeginFrame last saw it
std::vector<ModelComponent*> s_culledModels;   // models whose pose was left root-only this frame
struct DeferredPick { ModelComponent* model; Picking::Scene* scene; uint32_t id; PickXfCtor_t ctor; PickSetXf_t set; };
std::vector<DeferredPick> s_deferredPicks;   // citizens whose pick transform was not pushed
struct DeferredReplay { StatusIconComponent* obj; void (*fn)(StatusIconComponent*); };
std::vector<DeferredReplay> s_deferredReplays;  // icons etc. whose presentation tick was skipped
bool s_flushing = false;   // replays re-enter the tick bodies; no re-deferral while flushing
int  s_iterIndex = 0, s_iterCount = 1;   // SetIteration

// Graphics::MeshInstance::_rootOnlyUpdates (retail: import slot 0x140151f30), the class
// static (ENDGAME B.3.9 SS2).
bool* RootOnlyByte() { return &::Graphics::MeshInstance::_rootOnlyUpdates; }

void RefreshFrustum(::Graphics::Renderer* renderer) {
    s_haveFrustum = false;
    if (!renderer) return;
    // Renderer::GetCullCamera returns a reference into the camera object; before the first
    // Draw the pointer it adjusts is null, so the "reference" is a small negative address
    // (-0x110 seen live). Anything outside user space is "no camera yet". Taking the
    // address of the returned reference does not dereference it, so the range test below
    // still runs before any load.
    const ::Graphics::Camera* camera =
        &renderer->GetCullCamera();
    if (reinterpret_cast<intptr_t>(camera) <= 0x10000 ||  // CAST-OK: ptr_int
        reinterpret_cast<uintptr_t>(camera) >= 0x00007fffffff0000ull) return;  // CAST-OK: ptr_int
    alignas(16) uint8_t camFrustum[256] = {0};
    ::Frustum worldFrustum = {};   // as geometry_draw.cpp's: the body constructs into it
    // B.3.5: the retslot door's body moved to the call site — exactly one
    // construction, into the SAME caller storage the door used to be handed.
    const ::Frustum* camF = ::new (static_cast<void*>(camFrustum)) ::Frustum(camera->GetFrustum());
    // Graphics::Camera keeps its world transform at +0x64; the copy is
    // Math::Transform3's copy ctor, now through the real class's named field.
    static_assert(offsetof(::Graphics::Camera, _cameraToWorld) == 0x64,
                  "Camera::_cameraToWorld @ +0x64 — the offset this read used to spell out");
    Math::Transform3 camXf3 = camera->_cameraToWorld;
    // ??DFrustum@Volume@@QEBA?AV01@AEBVTransform3@Math@@@Z (0x1800f9310), explicit slot.
    RtVolume_Frustum_MulTransform3(camF, &worldFrustum, &camXf3);
    for (int i = 0; i < 6; ++i) {   // the six planes, {x, y, z, d} each
        s_planes[i][0] = worldFrustum.planes[i].x;
        s_planes[i][1] = worldFrustum.planes[i].y;
        s_planes[i][2] = worldFrustum.planes[i].z;
        s_planes[i][3] = worldFrustum.planes[i].w;
    }
    // Sanity: a zero frustum (renderer not yet drawn) must not cull everything.
    float mag = 0.0f;
    for (int i = 0; i < 6; ++i)
        mag += s_planes[i][0] * s_planes[i][0] + s_planes[i][1] * s_planes[i][1] +
               s_planes[i][2] * s_planes[i][2];
    s_haveFrustum = mag > 1e-6f;
}

// Bring every culled model's pose up to date: MeshInstance::Update with a ZERO time and
// the root-only flag clear. AnimationState::Update(dt=0) recomputes the node pose at the
// current animation time and snaps the interpolated state (prev = cur), so nothing moves.
void FlushCulled() {
    if (s_culledModels.empty()) return;
    bool* rootOnly = RootOnlyByte();
    const bool saved = *rootOnly;
    *rootOnly = 0;
    const System::Time zeroTime{};   // _ticks 0
    for (ModelComponent* model : s_culledModels) {
        ::Graphics::Geometry* geo = &model->meshInstance;   // +0x28
        // GEOM_CULL left the render-scene transform stale: push model+0x148 first.
        // The receiver is the Geometry the vtable dispatch below also drives: every
        // drawable component in this port embeds its Geometry at owner+0x28
        // (scene_watch.cpp:176-178 and engine/types/ModelComponent.h).
        if (g_flags.geomCull)
            geo->SetTransform(model->transform);   // +0x148
        // ENDGAME B.3.5 caller batch, COORDINATOR RULING. This was a hand-inlined
        // read of slot 1 (`vt + 0x8`) on the same proved `Graphics::Geometry` at
        // owner+0x28, guarded by `if (upd)` — a test of the OBJECT'S OWN VTABLE
        // SLOT, not of a door pointer. Slot 1 is `_purecall` only on the abstract
        // bases, and every live Geometry in this process wears a complete minted
        // table (`[RtMint] 152 of 152 vtables COMPLETE`), so that guard tested
        // nothing that can happen and it goes with the inlined dispatch. It is now
        // the virtual call it always was. The OBJECT-pointer reasoning above — the
        // receiver is proved from the code that produced it, not from its name —
        // is unchanged, and `g_flags.geomCull` still guards the SetTransform above.
        geo->Update(zeroTime);
    }
    s_culledModels.clear();
    *rootOnly = saved;
}

void FlushReplays() {
    if (s_deferredReplays.empty()) return;
    bool* rootOnly = RootOnlyByte();
    const bool saved = *rootOnly;
    *rootOnly = 0;
    std::vector<DeferredReplay> replays;
    replays.swap(s_deferredReplays);
    s_flushing = true;
    for (const DeferredReplay& d : replays) d.fn(d.obj);
    s_flushing = false;
    *rootOnly = saved;
}

void FlushPicks() {
    if (s_deferredPicks.empty()) return;
    for (const DeferredPick& d : s_deferredPicks) {
        alignas(16) uint8_t xf[136];
        const Math::Transform* built = d.ctor(xf, &d.model->transform);   // +0x148
        d.set(d.scene, d.id, built);
    }
    s_deferredPicks.clear();
}

// The first culled tick of a frame: the previous frame's lists are superseded (every
// culled model is re-decided this frame; a model that became visible gets its full
// update from the normal path).
void NoteSimRan() {
    if (s_simRanThisFrame) return;
    s_simRanThisFrame = true;
    s_culledModels.clear();
    s_deferredPicks.clear();
    s_deferredReplays.clear();
}

// ---- ext.anim_batch per-model bookkeeping --------------------------------------------
// Which models may skip their intermediate root-only updates is decided per model per
// iteration from the instance AnimationState (MeshInstance+0xd8):
//   layers at +0x08 (stride 0x38: group +0x00, animation index +0x08, pool id +0x0c,
//   time +0x18, blend total +0x20 / remaining +0x28), count at +0x10.
//   Animation = *(group+0x20) + index*0x70: node count +0x4c, frame count +0x50,
//   keyframes +0x60 (0x1c bytes each: quaternion, then position at +0x10).
// The move tick's walk speed is |MeshInstance::RootAnimationOffset()| per tick.
//  * count == 1 and the layer has a pool id: AnimationState::Update flags the state
//    "instanced" and RootAnimationOffset reads the AnimationManager pool pose, which
//    AnimationManager::Update refreshes with the real tick on EVERY iteration. The
//    instance update only advances the clock.
//  * otherwise (a blend between layers, or a non-instanced animation) the offset is the
//    instance's own +0x100, written by UpdateRootOnly from the layer times and the
//    per-tick blend weight, and Animation::GetRootPoseAtTime scales it by the dt it is
//    given. Such a model gets the plain per-tick update as long as any of its layers'
//    animations carries root motion (any non-zero node-0 keyframe position, cached per
//    Animation); with no root motion every dt yields the same zero offset.
// A model is re-decided every iteration, so a PlayAnimation between iterations switches
// it to per-tick from that iteration on; the last iteration then advances it by the
// ticks it actually skipped (+1), not a blind N.
// WHY CLASS B (PERFXEXT §1): the clock of a batched model lags by up to N-1 ticks inside
// the frame, and the sim compares it with the clip's duration (MeshInstance::IsAnimating:
// NaturalResourceComponent::Tick, AnimateNaturalResource::Execute, the tornado's
// knock-down); and the last iteration's (skipped+1) x tick lands on the NEW clip after a
// mid-frame PlayAnimation. The COMPPROF=4 shadow `AnimBatchSim` counts both.
// The per-frame skip count lives in the PERFXTBL side table (side_table.h,
// docs/extensions/handins/PERFXTBL.md), reached by the walk's current index; the hash
// below is the count's home only for models whose entity has no live slot.
// s_batchShadow runs the old per-call BatchEntry alone, for the COMPPROF=4 shadow.
using BatchSlot = SideTable::BatchSlot;
using BatchHash = SideTable::BatchHash<32768>;
BatchHash s_batchTbl;
BatchHash s_batchShadow;
SideTable::Side<BatchHash> s_side(s_batchTbl);
struct RootMotionSlot { const void* key; bool has; };
RootMotionSlot s_rootMotionTbl[4096];
unsigned s_batchSerial = 0;

bool AnimationHasRootMotion(const ::Graphics::Animation* anim) {
    RootMotionSlot& e = s_rootMotionTbl[(unsigned)(((reinterpret_cast<uintptr_t>(anim) >> 4) * 2654435761u) & 4095)];  // CAST-OK: hash
    if (e.key == anim) return e.has;
    const int nodes = anim->_boneCount;                  // +0x4c
    const int frames = anim->_frameCount;                // +0x50
    const Math::Transform* keys = anim->_keys.GetData(); // +0x60, stride 0x1c
    bool has = false;
    if (keys && nodes > 0) {
        for (int f = 0; f < frames && !has; ++f) {
            const Vector3& pos = keys[(size_t)nodes * f].translation;   // key +0x10
            has = pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f;
        }
    }
    e.key = anim; e.has = has;
    return has;
}

// True when this model's root-motion offset would differ between one update of N ticks
// and N updates of one tick (see above).
bool NeedsPerTickRootUpdate(const ModelComponent* model) {
    const ::Graphics::AnimationState& state = model->meshInstance._animation;   // +0x28 + 0xd8
    const int count = state._layers.GetSize();                     // +0x10
    if (count <= 0) return false;                                   // nothing playing
    const ::Graphics::AnimationState::Layer* layers = state._layers.GetData();   // +0x08
    if (!layers) return false;
    // Instanced: RootAnimationOffset reads the pool pose -- but only once the state's
    // "instanced" byte (+0x00) is set, which the next Update does. Right after a blend
    // finished the count is 1 with the byte still clear, and the offset would be the
    // last blended +0x100 held for the frame: give that model one more per-tick update.
    if (count == 1 && layers[0]._handle >= 0) return state._instanced == 0;   // +0x0c
    for (int i = 0; i < count; ++i) {
        const ::Graphics::AnimationState::Layer& layer = layers[i];   // stride 0x38
        const ::Graphics::AnimationGroup* group = layer._group;      // +0x00
        const int idx = layer._clipIndex;                            // +0x08
        if (!group || idx < 0) continue;
        if (!group->_clips._data) continue;                          // group +0x20
        if (AnimationHasRootMotion(&group->GetAnimation(idx))) return true;   // stride 0x70
    }
    return false;
}

// COMPPROF=4, ext.anim_batch (class B evidence, PERFXEXT §5): a model was just skipped on
// an intermediate iteration with `pending` ticks not applied. Retail's per-tick model
// would answer MeshInstance::IsAnimating at the clock + pending; the batched one answers
// at the clock. Detail 1 = the answers differ (a finishing clip seen late).
void AnimBatchSimCheck(const ModelComponent* model, int64_t pending) {
    const ::Graphics::AnimationState& state = model->meshInstance._animation;
    const ::Graphics::AnimationState::Layer* layers = state._layers.GetData();
    const bool has = state._layers.GetSize() > 0 && layers && layers[0]._group &&
                     layers[0]._group->_clips._data && layers[0]._clipIndex >= 0;
    bool looping = false;
    int64_t t = 0, duration = 0;
    if (has) {
        const ::Graphics::Animation& anim = layers[0]._group->GetAnimation(layers[0]._clipIndex);
        looping = anim._looping;
        t = layers[0]._time._ticks;
        duration = anim._duration;
    }
    const bool batched = IsAnimatingAt(has, looping, t, duration, 0);
    const bool retail  = IsAnimatingAt(has, looping, t, duration, pending);
    StressHarness_Verify("AnimBatchSim", batched == retail, batched == retail ? 0 : 1);
}

} // namespace

bool RootOnlyNow() { return *RootOnlyByte() != 0; }
bool* RootOnlyFlag() { return RootOnlyByte(); }

void BeginFrame(::Graphics::Renderer* renderer) {
    // The two toggles, once per frame. Ext() holds anim_batch's per-game value under the
    // class B lock; render_culling (class A) may change between any two frames.
    const ::Ext::Extensions& ext = ::Ext::Ext();
    if (ext.render_culling != s_cullingOn) {
        // Turned off: replay everything deferred (with the flags that deferred it), so
        // no pose, render transform, pick transform or icon stays stale. Turned on: the
        // planes from before are stale; BeginFrame / Draw refresh them.
        if (s_cullingOn) { FlushCulled(); FlushPicks(); FlushReplays(); }
        s_haveFrustum = false;
        s_simRanThisFrame = false;
        s_cullingOn = ext.render_culling;
    }
    const bool sideWas = SideTableNeeded(g_flags);
    g_flags = FlagsFor(ext.render_culling, ext.anim_batch);
    // The side table is built only while it has a user: from the next walk on.
    if (SideTableNeeded(g_flags) && !sideWas) EntityList_MarkUpdateTreeDirty();
    if (!g_flags.any) return;
    // The sim did not run last frame (pause / speed 0) but poses were left stale before
    // that: bring them up to date once so a paused camera pan shows the real pose.
    if (!s_simRanThisFrame) { FlushCulled(); FlushPicks(); FlushReplays(); }
    s_simRanThisFrame = false;
    ++s_frameSerial;
    if (g_flags.animCull && !s_haveFrustum) RefreshFrustum(renderer);   // fallback; Draw normally feeds SetWorldFrustum
}

// OFFSCREEN: the diagnostic copy of the planes (DiagPointVisible), filled only at
// COMPPROF level 2 -- one load and a not-taken branch per Draw otherwise.
static float s_diagPlanes[6][4];
static bool  s_diagHave = false;

void SetWorldFrustum(const void* frustum) {
    if (g_compProfOn >= 2 && frustum) {
        std::memcpy(s_diagPlanes, frustum, sizeof(s_diagPlanes));
        float mag = 0.0f;
        for (int i = 0; i < 6; ++i)
            mag += s_diagPlanes[i][0] * s_diagPlanes[i][0] + s_diagPlanes[i][1] * s_diagPlanes[i][1] +
                   s_diagPlanes[i][2] * s_diagPlanes[i][2];
        s_diagHave = mag > 1e-6f;
    }
    if (!g_flags.animCull || !frustum) return;
    std::memcpy(s_planes, frustum, sizeof(s_planes));
    float mag = 0.0f;
    for (int i = 0; i < 6; ++i)
        mag += s_planes[i][0] * s_planes[i][0] + s_planes[i][1] * s_planes[i][1] +
               s_planes[i][2] * s_planes[i][2];
    s_haveFrustum = mag > 1e-6f;
}

void Reset() {
    s_culledModels.clear();
    s_deferredPicks.clear();
    s_deferredReplays.clear();
    s_haveFrustum = false;
    s_simRanThisFrame = false;
}

void ForgetModel(ModelComponent* model) {
    if (!s_culledModels.empty())
        s_culledModels.erase(std::remove(s_culledModels.begin(), s_culledModels.end(), model),
                             s_culledModels.end());
    if (!s_deferredPicks.empty())
        s_deferredPicks.erase(std::remove_if(s_deferredPicks.begin(), s_deferredPicks.end(),
                                             [model](const DeferredPick& d) { return d.model == model; }),
                              s_deferredPicks.end());
}

void ForgetPick(Picking::Scene* scene, uint32_t id) {
    if (s_deferredPicks.empty()) return;
    s_deferredPicks.erase(std::remove_if(s_deferredPicks.begin(), s_deferredPicks.end(),
                                         [scene, id](const DeferredPick& d) {
                                             return d.scene == scene && d.id == id;
                                         }),
                          s_deferredPicks.end());
}

void ForgetIcon(StatusIconComponent* obj) {
    if (s_deferredReplays.empty()) return;
    s_deferredReplays.erase(std::remove_if(s_deferredReplays.begin(), s_deferredReplays.end(),
                                           [obj](const DeferredReplay& d) { return d.obj == obj; }),
                            s_deferredReplays.end());
}

static inline bool PlanesVisible(const float* p, float m) {
    for (int i = 0; i < 6; ++i) {
        const float* n = s_planes[i];
        if (n[0] * p[0] + n[1] * p[1] + n[2] * p[2] - n[3] < -m) return false;
    }
    return true;
}

// The same object is tested several times per frame (every speed iteration for
// particles; move tick + model tick + icon for a citizen). The frustum is fixed for the
// frame, so the answer is cached per (object, frame) in a direct-mapped table; a
// collision just recomputes. Keyed on the position's address, which is stable for the
// life of the component.
struct VisSlot { const void* key; unsigned serial; bool vis; };
static VisSlot s_visTbl[8192];

static bool CachedVisible(const float* p, float m) {
    if (!s_haveFrustum) return true;
    VisSlot& e = s_visTbl[((reinterpret_cast<uintptr_t>(p) >> 4) * 2654435761u) & 8191];  // CAST-OK: hash
    if (e.key == p && e.serial == s_frameSerial) return e.vis;
    const bool v = PlanesVisible(p, m);
    e.key = p; e.serial = s_frameSerial; e.vis = v;
    return v;
}

bool PointVisible(const float* p) {
    return CachedVisible(p, kCullMargin);
}

bool DiagPointVisible(const float* p) {
    if (!s_diagHave) return true;
    for (int i = 0; i < 6; ++i) {
        const float* n = s_diagPlanes[i];
        if (n[0] * p[0] + n[1] * p[1] + n[2] * p[2] - n[3] < -kCullMargin) return false;
    }
    return true;
}

bool AnimCullSkipSlow(ModelComponent* model) {
    NoteSimRan();
    if (*RootOnlyByte() != 0) return false;   // intermediate iteration: already root-only
    // Keyed on the model's own position (+0x158), as ModelCulled / PickCullSkip are. It
    // used to pass a STACK copy of it: CachedVisible keys on the pointer, so every model
    // shared one cache key (the same rsp-relative slot each tick) and got the frame's
    // first model's verdict (PERFXTBL hand-in, fix 1).
    if (PointVisible(&model->transform.translation.x)) return false;
    s_culledModels.push_back(model);
    return true;
}

bool PickCullSkip(ModelComponent* model, Picking::Scene* scene, uint32_t id, PickXfCtor_t ctor, PickSetXf_t set) {
    if (!g_flags.pickCull || !s_haveFrustum || !ctor || !set) return false;
    NoteSimRan();
    const float* pos = &model->transform.translation.x;   // +0x158
    if (PointVisible(pos)) return false;
    s_deferredPicks.push_back(DeferredPick{ model, scene, id, ctor, set });
    return true;
}

void SetIteration(int index, int count) {
    s_iterIndex = index; s_iterCount = count > 0 ? count : 1;
    if (index == 0) ++s_batchSerial;   // new sim frame: every per-model skip count is stale
}

// ---- PERFXTBL: the per-entity side table (side_table.h) -----------------------------
// The visibility verdict of `p` for this Update frame from the entity's slot; a miss is
// the old CachedVisible (probe, verdict, store), kept in the slot. COMPPROF=4: the old
// CachedVisible is asked on every call too and must agree (`kind` names the caller).
static bool SlotVisible(SideTable::Slot* slot, const float* p, float m, int kind) {
    const bool v = SideTable::Side<BatchHash>::Visible(slot, p, s_frameSerial,
                                                      [p, m] { return CachedVisible(p, m); });
    if (g_verifyOn) {
        const bool old = CachedVisible(p, m);
        StressHarness_Verify("PerfXVerdict", v == old, v == old ? 0 : kind);
    }
    return v;
}

void OnWalkSnapshot(ComponentEntity* const* snap, size_t n, bool sorted) {
    if (!SideTableNeeded(g_flags)) {
        // No user: nothing is built. One rebuild empties a table left from before (its
        // live counts spill to the hash, which nothing reads until a user returns).
        if (s_side.Usable() || s_side.Size() != 0)
            s_side.Rebuild(static_cast<ComponentEntity* const*>(nullptr), 0, false, s_batchSerial);
        return;
    }
    s_side.Rebuild(snap, n, sorted, s_batchSerial);
}

bool AnimBatchDecide(ModelComponent* model, const System::Time* time, int64_t* timeOut) {
    const int64_t tick = time->_ticks;
    *timeOut = tick;
    if (s_iterCount <= 1) return false;                       // speed 1: nothing to batch
    const bool last = (s_iterIndex >= s_iterCount - 1);
    // PERFXTBL: the entity's side-table slot (one compare inside the walk). The count
    // lives in the slot (side_table.h banner: one authoritative home per model and frame).
    SideTable::Slot* slot = s_side.SlotFor(model->_entity.Get(), g_walkIndex);
    unsigned* skipped = s_side.Batch(slot, model, s_batchSerial);
    BatchSlot* shadow = nullptr;
    if (g_verifyOn) {
        // COMPPROF=4: the old BatchEntry, alone in its own table, sees the same calls;
        // its count must equal the slot's before this call's increment.
        shadow = s_batchShadow.Entry(model, s_batchSerial);
        const bool ok = (shadow == nullptr) == (skipped == nullptr) &&
                        (!shadow || shadow->skipped == *skipped);
        StressHarness_Verify("PerfXVerdict", ok, ok ? 0 : 1);
    }
    if (!skipped) return false;
    if (!last) {
        if (NeedsPerTickRootUpdate(model)) return false;     // exact 1 x tick
        ++*skipped;
        if (shadow) ++shadow->skipped;
        if (g_verifyOn) AnimBatchSimCheck(model, tick * (int64_t)*skipped);
        return true;
    }
    // COMPPROF=4: a model skipped earlier in the frame that now needs per-tick root
    // updates (a PlayAnimation in between) gets the old clip's skipped ticks on the new
    // one. Detail 2.
    if (g_verifyOn && *skipped > 0 && NeedsPerTickRootUpdate(model))
        StressHarness_Verify("AnimBatchSim", false, 2);
    *timeOut = tick * (int64_t)(*skipped + 1);   // exactly the ticks this model skipped
    return false;
}

bool ModelCulled(ModelComponent* model) {
    if (s_flushing) return false;
    if (!g_flags.animCull || !s_haveFrustum || !model) return false;
    return !PointVisible(&model->transform.translation.x);   // +0x158
}

void NoteDeferred(StatusIconComponent* obj, void (*replay)(StatusIconComponent*)) {
    if (s_flushing) return;
    NoteSimRan();
    s_deferredReplays.push_back(DeferredReplay{ obj, replay });
}

// The verdict kept in the owner entity's side-table slot for the Update frame (PERFXTBL).
bool ParticleCullSkip(const float* pos, const ComponentEntity* entity) {
    if (!g_flags.particleCull || !s_haveFrustum) return false;
    SideTable::Slot* slot = s_side.SlotFor(entity, g_walkIndex);
    return !SlotVisible(slot, pos, kParticleMargin, 2);
}

} // namespace RenderCulling
}  // namespace Ext
