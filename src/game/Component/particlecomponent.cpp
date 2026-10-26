// src/game/Component/particlecomponent.cpp
// Clean reimplementation of ParticleComponent lifecycle methods from Application-x64.exe.
//
// ParticleComponent (0x48 bytes) manages particle effects attached to entities.
// Fields: +0x28 per-emitter "running" flags (Array<bool>), +0x38 the emitters'
// live state (Array<ParticleEmitterSlot>, stride 0x18).
//
// Vtable at 0x14015a290. Six non-trivial overrides — all deeply coupled to
// Graphics::Scene, MeshInstance, and particle emitter subsystems.
//

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
#include "Component/ParticleDescription.h"
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"
#include <new>
#include "Component/ParticleComponent.h"
#include <cstdint>
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "pathfinding/PointList.h"
#include "scene_watch.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::size (GameUpdateLoop::m_game)
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "math/Math.h"
#include "ext/render_culling/render_culling.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PERFXEXT.md): ext.render_culling (PC_Culled)
#include "stress_harness.h"    // g_verifyOn / StressHarness_Verify (the PerfXSim shadow)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <new>
#include "graphics/ParticleInstance.h"   // ENDGAME B.3.9 SS2: ParticleInstance::_frameIndex
#include "graphics/Particle.h"           // B.3.x W0: Particle::CreateSystem by name
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton

#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (SA type-id fallback)
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch

#include "Component/exe_bodies.h"

static float PC_UVScale();  // defined with the helper ports below

// Entity re-bucket — FAITHFUL retail behavior, now the SOLE per-frame tick driver.
// Retail's StartEmitter/StopEmitter set the component's draw flag (self+0x20 bit 0x4)
// and call RefreshEntityFlags (c3470), which re-aggregates the OWNING entity's flags
// and registers it into the per-frame component-update tree; the tree walk then
// dispatches this component's +0x40 slot (PC_TickVSlot) every frame. Earlier this was
// suppressed in SA because re-bucketing a house woke its broken workplace/status
// evaluation (spurious "no workers"/?/cold icons); that eval is now fixed (the building
// "no workers" warning is raised only by WorkPlaceComponent_SetAccepting on an unstaffed
// active workplace), so the re-bucket is safe AND required to drive the tick. Always on.
static bool PC_RebucketEntity() { return true; }

// The component's description as a ParticleDescription: the member
// Base::IsDerivedFrom (import slot 0x140152a98) on the +0x18 description against
// the cell 0x1401c4758, no null test, CMOVNZ -- every retail body that reads it:
// Place 0x140052560 (0x1400525b8-0x1400525d6), StartEmitterFade 0x140053660
// (0x140053674-0x1400536a2), Setup 0x140052480 (0x1400524bf-0x1400524da), OnAdded
// 0x140052f70 (0x140052f7d-0x140052f97 and 0x140052fdb-0x140052ff9), Tick 0x140052b30
// (0x140052bff-0x140052c1b), Slot11 0x1400531b0 (0x1400531c4-0x1400531e3), OnEvent
// 0x140053090 (0x1400530a3-0x1400530c1). (B8 RES, row 1041: was IsDerivedFromOrTrust.)
static ParticleDescription* GetParticleDesc(const ParticleComponent* self) {
    Description* baseObj = self->_description;
    return baseObj->IsDerivedFrom(ParticleDescription::TypeId())
               ? static_cast<ParticleDescription*>(baseObj) : nullptr;
}

// ============================================================
// Full ports of the exe helper closure.
// Decompiled from Application-x64.exe:
//   0x140052480  Setup        — (re)build geometry+flag arrays, create one
//                               Graphics::ParticleSystemInstance per emitter
//                               via the referenced ParticleSystem resource's
//                               vtable+0x40 (CreateInstance, Runtime-side)
//   0x140052560  Place        — position emitter i: model transform / map-cell
//                               terrain pos / identity, then attach-point table
//                               (desc+0x20) or mesh-node lookup by name
//   0x140052990  StopEmitter  — Scene::RemoveObject + flag bookkeeping
//   0x140052a50  StartEmitter — Place + Update(0) + Scene::AddObject + flags
//   0x140053660  StartFade    — fade-in rate = 1/t1, opacity=0, StartEmitter
//   0x140052b30  UpdateTick   — per-frame (vtable+0x40): node-follow transform
//                               + opacity fade integration + geometry Update
//   0x140053090  Event        — event 2 = fade-in start, 3 = fade-out (b2 set)
//
// LEFT (B.4 COMP-d): GameUpdateLoop::m_graphicsScene / m_game are void* (W0);
// Pathfinding::PointList::Point keeps its name and position raw (runtime header);
// Component's Persist / Tick take void* (COMP-c's header).
// ============================================================

// g_fUVScale is the retail .rdata constant 1.0f. In SA the ResolveAddr for
// 0x140172234 lands inside an unrelated registered data blob (observed value
// 300.0 → fade rate 300/s, opacity 15+ → permanently opaque smoke), so use
// the known retail value directly.
static float PC_UVScale() { return 1.0f; }

// FindComponent (exe 0x140003f60 ModelComponent, 0x140004680 MapComponent) on the
// owning entity; the null-entity test is the tree's.
static ModelComponent* PC_FindModel(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<ModelComponent>();   // 0x140003f60
}
static MapComponent* PC_FindMap(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<MapComponent>();   // 0x140004680
}

// The scene the emitters are drawn in.
static Graphics::Scene* PC_Scene() {
    return GameUpdateLoop::_singleton->m_graphicsScene;
}

// The emitter array's row: exe 0x14000a640, Array<ParticleEmitterSlot>::SetSize (the
// shared grow policy: exact-equal no-op, capacity n or 3n on a +1 append, MemAlloc(cap *
// 0x18, 8), copy from the old buffer below the old count, init above it, MemFree the old
// buffer when it moved). Copy: three qword moves (0x14000a83a..0x14000a84f). Init: a qword
// 0 at +0x00, dword -1 at +0x08, dword 0 at +0x10 (0x14000a85a..0x14000a865); +0x0C and
// +0x14 are not written. No element destroy.
namespace {
void ZeroEmitterSlot(ParticleEmitterSlot* e) {
    e->instance = nullptr;   // +0x00 qword
    e->sceneId  = -1;        // +0x08
    e->fadeRate = 0.0f;      // +0x10 (retail stores the dword 0)
}
void AssignEmitterSlot(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<uint64_t, &rtcoll::CopyWords<uint64_t, 3>>(d, s);
}
void InitEmitterSlot(uint8_t* d) {
    rtcoll::InitAs<ParticleEmitterSlot, &ZeroEmitterSlot>(d);
}
}  // namespace

const rtcoll::ArraySpec ParticleComponentRows::kSpec_EmitterSlot = {   // exe 0x14000a640
    "Array<ParticleEmitterSlot>", 0x18, 8,
    &AssignEmitterSlot, &InitEmitterSlot, nullptr, true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0x0c, 0x18,   // +0x0C..+0x10 and +0x14..+0x18 (one window: +0x10 is written)
    /*seed zero       */ 0, 0 };

// ============================================================
// ParticleComponent_helper_2560 — place emitter i (0x140052560)
// ============================================================
void ParticleComponent::Place(int index) {
    ParticleDescription* desc = GetParticleDesc(this);
    if (!desc) return;
    const ParticleEmitterDescription& em = desc->_emitters[index];
    if (!emitters._data) return;
    ParticleEmitterSlot* ge = &emitters[index];
    if (!ge->instance) return;   // resource failed to load — leave emitter inert

    ModelComponent* model = PC_FindModel(_entity.Get());
    MapComponent* mapc  = PC_FindMap(_entity.Get());

    Math::Transform tf;   // identity
    if (model && !em.noModelTransform) {
        tf = model->transform;
    } else if (mapc && GameUpdateLoop::_singleton != nullptr) {
        // Centre cell of the entity's map footprint → terrain-height world pos
        Map* game = GameUpdateLoop::_singleton->m_game;
        int W = game ? game->size : 0;
        if (W > 0) {
            int idx = ((int)static_cast<int16_t>(mapc->tileY0) + ((int)static_cast<int16_t>(mapc->tileY1) >> 1)) * W
                    + ((int)static_cast<int16_t>(mapc->tileX1) >> 1) + (int)static_cast<int16_t>(mapc->tileX0);
            float half = (float)(W >> 1) - 0.5f;
            Math::Vector3 in((float)(idx % W) - half, (float)(idx / W) - half, 0.0f);
            Math::Vector3 out(0.0f, 0.0f, 0.0f);
            game->GetTerrainPos(&out.x, &in.x, '\0', nullptr);
            tf.translation = out;
        }
    }

    Math::Vector3 pos = tf.translation;
    int followIdx = -1;
    Pathfinding::PointList* attach = desc->_attachPoints;
    if (attach) {
        // Named attach-point table: entries {String name, Vector3 offset}, stride 0x18
        const Collection::Array<Pathfinding::PointList::Point>& points = attach->Points();
        int n = points.GetSize();
        const Pathfinding::PointList::Point* arr = points.GetData();
        for (int j = 0; j < n && arr; j++) {
            // System::String::operator==(const String&) — slot 0x140151438.
            if (arr[j].name == em.name) {
                const float zero = 0.0f;
                const Math::Transform ypo = Math::Transform::FromYawPitchOffset(
                    zero, zero, arr[j].pos);
                // Math::Transform::operator*(const Transform&) — slot 0x140151430.
                const Math::Transform prod = ypo * tf;
                pos = prod.translation;
                followIdx = j;
                break;
            }
        }
    } else if (model) {
        // Mesh-node lookup by emitter name (e.g. a "smoke" node in the building mesh)
        const Graphics::MeshInstance* mi =
            &model->meshInstance;
        const Graphics::GraphicsMesh* mesh = mi->GetMesh().Get();
        const char* name = em.name._data;
        if (mesh && name && em.name.GetLength() != 0) {
            int n = mesh->GetNodeIndexFromName(name);
            if (n >= 0) {
                // GetNodeToWorld is a hidden-retslot door in retail; the door body
                // moves to the call site as ONE construction into the same storage.
                const Math::Transform ntw = mi->GetNodeToWorld(n);
                pos = ntw.translation;
                followIdx = n;
            }
        }
    }

    // Final placement: identity rotation + computed position
    Math::Transform finalTf;
    finalTf.translation = pos;
    ge->instance->SetTransform(finalTf);

    // Configure the instance from its ParticleSystem resource (slot 9, +0x48).
    // ge->instance is the concrete ParticleSystemInstance CreateSystem built (Setup);
    // ParticleInstance itself is abstract, so no live object has a pure slot here.
    Graphics::ParticleInstance* inst = ge->instance;
    if (em.particle)
        inst->SetParticle(Object::ExternalPtr<Graphics::Particle>(em.particle));   // slot 9

    ge->sceneId = -1;
    ge->nodeIndex = em.followNode ? followIdx : -1;

    // Prewarm: tick the emitter forward by t0 (elem+0x18) in engine fixed steps
    int64_t t0 = em.prewarm._ticks;
    if (t0 > 0 && ::Engine::MainLoop::_singleton) {
        System::Time step;
        step._ticks = ::Engine::MainLoop::_singleton->_deltaTime._ticks;
        if (step._ticks > 0) {
            for (int64_t acc = step._ticks; ; acc += step._ticks) {
                static_cast<Graphics::Geometry*>(inst)->Update(step);   // slot 1
                if (acc >= t0) break;
            }
        }
    }
}

// ============================================================
// ParticleComponent_helper_2990 — stop emitter i (0x140052990)
// ============================================================
void ParticleComponent::StopEmitter(int index) {
    if (!emitters._data) return;
    ParticleEmitterSlot* ge = &emitters[index];
    if (ge->sceneId >= 0) {
        if (GameUpdateLoop::_singleton != nullptr) {
            Graphics::Scene* scene = PC_Scene();
            if (scene)
                scene->RemoveObject((uint32_t)ge->sceneId);
        }
        ge->sceneId = -1;
    }
    if (active._data) active[index] = false;

    // Any emitter still live? Keep the per-frame flag.
    int total = emitters._size;
    for (int j = 0; j < total; j++)
        if (emitters[j].sceneId >= 0) return;

    if (!PC_RebucketEntity()) return;
    uint32_t oldFlags = _flags;
    uint32_t newFlags = oldFlags & ~0x4u;
    _flags = newFlags;
    if (((newFlags ^ oldFlags) & 0xFFFF) != 0 && _entity != nullptr) {
        _entity->RefreshEntityFlags();   // 0x1400c3470
    }
}

// ============================================================
// ParticleComponent_helper_2a50 — start emitter i (0x140052a50)
// ============================================================
void ParticleComponent::StartEmitter(int index) {
    if (!emitters._data) return;
    ParticleEmitterSlot* ge = &emitters[index];

    Graphics::Scene* scene = (GameUpdateLoop::_singleton != nullptr) ? PC_Scene() : nullptr;
    if (ge->sceneId >= 0 && scene)
        scene->RemoveObject((uint32_t)ge->sceneId);

    Place(index);   // 0x140052560
    if (!ge->instance || !scene) return;

    // Initial Update(Time(0)) then add to the scene
    System::Time zeroTime;
    zeroTime._ticks = 0;
    ge->instance->Update(zeroTime);   // slot 1
    {
        ge->sceneId = (int32_t)scene->AddObject(ge->instance);
        SceneWatch_Note((uint32_t)ge->sceneId, ge->instance, "ParticleComponent");
    }

    if (PC_RebucketEntity()) {
        uint32_t oldFlags = _flags;
        uint32_t newFlags = oldFlags | 0x4u;
        _flags = newFlags;
        if (((newFlags ^ oldFlags) & 0xFFFF) != 0 && _entity != nullptr) {
            _entity->RefreshEntityFlags();   // 0x1400c3470
        }
    }
    if (active._data) active[index] = true;
}

// ============================================================
// FUN_140053660 — start emitter i with fade-in (0x140053660)
// ============================================================
void ParticleComponent::StartEmitterFade(int index) {
    ParticleDescription* desc = GetParticleDesc(this);
    if (!desc) return;
    if (!emitters._data) return;
    ParticleEmitterSlot* ge = &emitters[index];

    float t1 = desc->_emitters[index].fadeTime.AsSeconds();
    ge->fadeRate = PC_UVScale() / t1;

    if (ge->instance) {
        Graphics::ParticleInstance* g = ge->instance;
        if (g->_opacity != 0.0f) {
            g->_dirtyFlags |= 4;      // dirty
            g->_opacity = 0.0f;       // opacity (both floats) = 0
            g->_opacityPrev = 0.0f;
        }
    }
    StartEmitter(index);   // 0x140052a50
}


// ============================================================
// ParticleComponent_sub_2480 — setup (0x140052480)
// ============================================================
void ParticleComponent::Setup() {
    ParticleDescription* desc = GetParticleDesc(this);
    if (!desc) {
        return;
    }
    int n = desc->_emitterCount;
    emitters.SetSize(n);   // 0x14000a640
    active.SetSize(n);     // 0x14000a590 Collection::Array<bool>::SetSize(n)

    ParticleEmitterDescription* emArr = desc->_emitters;
    ParticleEmitterSlot* geoArr = emitters._data;
    int total = emitters._size;
    if (!emArr || !geoArr) return;
    int nullRes = 0, nullInst = 0;
    for (int i = 0; i < total; i++) {
        Graphics::Particle* res = emArr[i].particle;
        Graphics::ParticleInstance* inst = nullptr;
        if (res) {
            // Graphics::Particle slot 8 (+0x40) CreateSystem: pure in Particle,
            // filled by the concrete ParticleSystem every resource is.
            inst = res->CreateSystem();   // slot 8
        }
        // A null here is FATAL LATER, not here: ParticleComponent::OnAdded (slot 6)
        // reads emitters[i].instance->_opacity with no test, exactly as retail
        // 0x140053029 does, because in retail this slot is always populated. Say so
        // at the site that produced the null, or the only evidence is an AV reading
        // address 0x7c.
        if (!res)  ++nullRes;
        if (!inst) ++nullInst;
        geoArr[i].instance = inst;
        Place(i);   // 0x140052560
    }
    if (nullRes || nullInst) {
        static int s_pcNullBudget = 0;
        if (s_pcNullBudget < 16) {
            s_pcNullBudget++;
            fprintf(stderr, "[Particle] EMPTY GEOMETRY self=%p emitters=%d "
                    "nullResource=%d nullInstance=%d -- slot 6 OnAdded (Draw) WILL fault "
                    "reading 0x7c on the first such emitter. nullResource!=0 means the "
                    "ParticleAttachment _particle external never resolved; nullInstance "
                    "with nullResource==0 means the resource resolved but its vtable+0x40 "
                    "CreateInstance returned null.\n",
                    static_cast<void*>(this), total, nullRes, nullInst);
            fflush(stderr);
        }
    }
}



// ===== Standalone factory (Foundation D — component instantiation) =====
// Building particle effects (chimney smoke etc). Gated on BANISHED_LIVE_PARTICLE
// (or global the global live-components switch); default (env off) stays inert.
//
// Live slot map (retail vtable 0x14015a290):
//   [4]  +0x20 Startup   (0x140053230) — Setup: create ParticleSystemInstances
//   [6]  +0x30 OnAdded   (0x140052f70) — fade-in start of autoStart(b1) emitters
//   [7]  +0x38 OnRemoved (0x140052e80) — remove all emitters from the scene
//   [8]  +0x40 per-frame (0x140052b30) — node-follow + fade + geometry Update
//   [11] +0x58 OnUpdate2 (0x1400531b0) — setup + restart flagged emitters
//   [12] +0x60 Event     (0x140053090) — event 2/3 fade-in/out (b2 emitters)
// Slot [3] (+0x18 save/load serialize) IS installed -- see
// ParticleComponent_FinishLiveVtable below, which sets it unconditionally,
// outside the live gate, because serializer wiring is not a feature flag.
// (This note previously claimed the slot stayed inert "because the bool array
// is rebuilt by Startup anyway". That was stale -- it outlived the a11d083
// fix 120 lines below -- and it was wrong in principle as well: a loader must
// CONSUME the bytes the writer emitted regardless of whether the data can be
// reconstructed. "Rebuilt by Startup" would justify DISCARDING a value after
// reading it, never SKIPPING the read.
// See [[reference_null_guard_on_a_serializer_call_is_a_desync]] and
// [[reference_stub_comment_outlived_the_port_that_replaced_it]].)
//
// ENDGAME B.3.8 CIT: ParticleComponent is a REAL class now (ParticleComponent.h) —
// the 48-slot ParticleComponent_LiveVtable, its per-create fills and
// Inert_ScalarDtor slot 0 are gone; the compiler's 16-slot vtable carries the
// slots above, with a real ctor/dtor chain (B38_RULES D1, D2).
#include "Object/factory_registration.h"
#include "object/class_vptr.h"   // Object::ClassVptr (the ctor records the vptr hint)

// ---- Per-frame frame-index un-stamp (Runtime-semantics fix, NOT a tick driver) ----
// The tick itself is now driven faithfully by the EntityList tree walk: StartEmitter's
// c3470 re-bucket registers the owning entity into the update tree, and the walk
// (EntityList_TreeNodeHandler) dispatches this component's +0x40 slot (PC_TickVSlot)
// every frame. The one thing SA still lacks is the per-render-frame advance of the
// shared Graphics::ParticleInstance::_frameIndex (Runtime static @0x1801fe77c) that
// retail's Renderer::Render performs; without it ParticleSystemInstance::Update dedups
// forever (emitState+0x2c == fi) and the sim freezes. Advancing the GLOBAL counter is
// not an option — the status-icon billboards share the mechanism and were built around
// the frozen counter (advancing it makes icons re-simulate as drifting particles).
// Instead, right before each tick, un-stamp THIS component's own live emitters so
// exactly one instance per state simulates. Blast radius = this component's smoke.
// (UNSTAMP, 2026-10-13: this banner predates the B.3.9 SS2 port of gameupdateloop.cpp step 3h,
// which now advances _frameIndex once per SIM ITERATION; with it the lowering below seems
// never to fire in steady state. docs/extensions/handins/UNSTAMP.md section 8.)
extern "C" int GameUpdateLoop_CurrentFrame();  // per-render-frame token (read-only)

// [UNSTAMP -- NON-RETAIL scaffolding, exact; docs/extensions/handins/UNSTAMP.md] "has this
// component already run its un-stamp this render frame" is answered by a frame stamp in
// the component's own Component::_pad24 (+0x24: a retail pad the ctor zeroes and nothing
// else reads or writes; on the line the walk already loads for _flags) instead of the
// s_compTbl probe below (one random line of a 128 KB table per particle tick). Frame token
// 0 is reserved: at token 0 the table never answers "seen" (its zeroed slots are all
// claimed by generation 0), and a fresh component's stamp is 0, so the stamp test is
// skipped there too. Kill switch: false = the table probe, exactly as before.
static constexpr bool kUnstampStamp = true;

// Pointer hash over the address bits (std::hash of a pointer is its address).
static inline uint32_t PC_SlotOf(const void* p, int cap) {
    return (uint32_t)((std::hash<const void*>{}(p) >> 4) * 2654435761u) & (cap - 1);
}

// The port-only per-component seen table (the kUnstampStamp = false path, and the
// COMPPROF=4 `UnstampStamp` shadow's reference). True = this component already ran its
// un-stamp in render frame `frame`; otherwise claims a slot for it (when one is free).
// At game speed N the tree walk ticks this component N times per RENDER frame, and
// every iteration after the first is a no-op here by construction (each emit is
// un-stamped at most once per frame, and the first tick re-stamps it). Skip the
// geo-entry walk for a component that already ran this frame — the same table
// keyed by the component pointer (~400 particle components tick per iteration at
// the 800-citizen stress town; this was 2.6% of the EntityList phase at 10x).
static bool PC_CompSeenTable(const void* k, int frame) {
    struct SeenSlot { const void* key; int gen; };
    constexpr int kCompCap = 8192;
    static SeenSlot s_compTbl[kCompCap];
    uint32_t h = PC_SlotOf(k, kCompCap);
    for (int probe = 0; probe < kCompCap; ++probe) {
        SeenSlot& sl = s_compTbl[(h + probe) & (kCompCap - 1)];
        if (sl.gen != frame) { sl.gen = frame; sl.key = k; return false; }
        if (sl.key == k) return true;   // already un-stamped this frame
    }
    return false;   // table full: not seen, nothing claimed
}

static void PC_UnstampOwnEmitters(ParticleComponent* self) {
    if (!self) return;
    int32_t fi = ::Graphics::ParticleInstance::_frameIndex;

    // Per-frame emit dedup. The 6 founding houses share only ~3 POOLED emit-states
    // (verified: emit-diag showed one emit ref'd by 3 houses). Un-stamping a shared
    // emit once per referencing house would re-simulate it Nx/frame (smoke Nx too
    // fast). Instead un-stamp each DISTINCT emit at most once per render frame; the
    // first house's tick then simulates it and the rest dedup (emit+0x2c == fi). This
    // reproduces retail's once-per-render-frame advance without the deleted two-phase
    // driver. The seen-set resets when the GameUpdateLoop frame token changes.
    // Seen-set = a generation-stamped open-addressing table (2026-09-04). The old 64-entry
    // linear array was both a CAP (a town with more than 64 distinct emits per frame —
    // the 800-house stress town — silently stopped un-stamping the rest, freezing their
    // smoke) and a hot spot (2.2% of the EntityList phase at 800x10: ~400 calls per
    // iteration, each scanning up to 64 pointers per geo entry).
    struct SeenSlot { const void* key; int gen; };
    constexpr int kSeenCap = 4096;   // power of two; ~5 emits per component worst case
    static SeenSlot s_seenTbl[kSeenCap];
    const int frame = GameUpdateLoop_CurrentFrame();
    // Has this component already run its un-stamp this render frame? (see kUnstampStamp)
    if constexpr (kUnstampStamp) {
        const uint32_t token = static_cast<uint32_t>(frame);
        const bool seen = frame != 0 && self->_pad24 == token;
        if (g_verifyOn) {
            // COMPPROF=4 `UnstampStamp`: the table, probed exactly as the kill-switch arm
            // probes it (once per un-culled tick, so its state is the old path's), must
            // give the same answer. Only this shadow reads the table. Detail: bit 0 the
            // stamp says seen, bit 1 the table says seen (2 alone = an address reused by
            // a new component within one render frame, UNSTAMP.md section 1.3).
            const bool tbl = PC_CompSeenTable(self, frame);
            StressHarness_Verify("UnstampStamp", seen == tbl, (seen ? 1 : 0) | (tbl ? 2 : 0));
        }
        if (seen) return;   // already un-stamped this frame
        self->_pad24 = token;
    } else {
        if (PC_CompSeenTable(self, frame)) return;   // already un-stamped this frame
    }
    auto seenInsert = [&](const void* k) -> bool {   // true = already un-stamped this frame
        uint32_t h = PC_SlotOf(k, kSeenCap);
        for (int probe = 0; probe < kSeenCap; ++probe) {
            SeenSlot& sl = s_seenTbl[(h + probe) & (kSeenCap - 1)];
            if (sl.gen != frame) { sl.gen = frame; sl.key = k; return false; }
            if (sl.key == k) return true;
        }
        return false;   // table full (cannot happen below kSeenCap distinct emits)
    };

    ParticleEmitterSlot* arr = self->emitters._data;
    int total = self->emitters._size;
    for (int i = 0; arr && i < total; ++i) {
        if (!arr[i].instance || arr[i].sceneId < 0) continue;
        Graphics::ParticleInstance* g = arr[i].instance;
        Graphics::Particle* res = g->_resource;
        RtGraphics::ParticleBoundsRecord* emit = g->_boundsPtr;
        if (!res || !emit || res->_active == 0) continue;
        if (seenInsert(emit)) continue;              // already un-stamped this frame
        if (emit->frameTag == fi) {
            emit->frameTag = fi - 1;
            ++PartUnstamp::g_lowered;   // the /part row (UNSTAMP.md section 8, doubt 1)
        }
    }
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/PERFXEXT.md): ext.render_culling (class A, src/ext/render_culling/render_culling.h) -- a particle
// system nobody can see is FROZEN (not ticked) rather than simulated; when the camera
// reaches it the plume it had simply continues. Position = the first live emitter
// geometry's transform translation (+0x94).
// EXACT (PERFXEXT §3.1): only a component none of whose live emitters is fading is frozen.
// The tick's StopEmitter (opacity reaching 0) writes sceneId / active[] / flag bit 2, which
// TornadoComponent::Tick and ParticleComponent::Persist read; a quiet emitter's tick
// touches presentation only (Ext::RenderCulling::EmitterQuiet). A fade started while frozen unfreezes
// the component on its next tick, so every StopEmitter happens on retail's tick.
static bool PC_Culled(ParticleComponent* self, const System::Time* time) {
    if (!Ext::RenderCulling::ParticleCull()) return false;
    int total = self->emitters._size;
    ParticleEmitterSlot* geoArr = self->emitters._data;
    if (total <= 0 || !geoArr) return false;
    const float* pos = nullptr;
    for (int i = 0; i < total; i++) {
        const ParticleEmitterSlot& ge = geoArr[i];
        if (!ge.instance || ge.sceneId < 0) continue;   // not live: the tick does nothing to it
        if (!Ext::RenderCulling::EmitterQuiet(ge.fadeRate, ge.instance->_opacity)) return false;
        if (!pos) pos = &ge.instance->_transform._current.translation.x;
    }
    if (!pos) return false;
    // PERFXTBL: the verdict is kept per Update frame in the owner entity's side-table slot.
    if (!Ext::RenderCulling::ParticleCullSkip(pos, self->_entity.Get())) return false;
    if (g_verifyOn) {
        // COMPPROF=4 `PerfXSim`: retail's StopEmitter decision for every live emitter of
        // this skipped tick, from the tick's own arithmetic, must be "no".
        const float dt = time->AsSeconds();
        const float uvScale = PC_UVScale();
        bool stops = false;
        for (int i = 0; i < total; i++) {
            const ParticleEmitterSlot& ge = geoArr[i];
            if (ge.instance && ge.sceneId >= 0 &&
                Ext::RenderCulling::RetailStopsEmitter(ge.fadeRate, ge.instance->_opacity, dt, uvScale))
                stops = true;
        }
        StressHarness_Verify("PerfXSim", !stops, stops ? 1 : 0);
    }
    return true;
}

// 0x1400533d0. Component() is the CALL 0x1401188b0; the 0x14015a290 stamp is
// the compiler's; +0x28..+0x40 = 0 (the two Array ctors, 0x1400533fd..0x140053409);
// flags |= 0xc3.
ParticleComponent::ParticleComponent() {
    OrFlagsInCtor(0xc3);
    Object::ClassVptr<ParticleComponent>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
}

// 0x140053310 (the scalar 0x14000a560 adds the MemFree on &1). Restamp (the compiler's),
// then every non-null emitters[i].instance is destroyed through its slot 0 with flag 1
// (0x140053350..0x140053370: `delete`, the virtual deleting dtor; the count and the data
// pointer re-read each pass; the slot is NOT nulled). Nothing unregisters the instance
// from Graphics::Scene here: OnRemove 0x140052e80 (the MarkForRemoval broadcast) does.
// Then ~emitters frees +0x38 (0x140053372..0x140053380) and ~active frees +0x28
// (0x140053384..0x140053392): reverse declaration order, then Component's chain.
ParticleComponent::~ParticleComponent() {
    for (int i = 0; i < emitters._size; ++i) {
        Graphics::ParticleInstance* instance = emitters[i].instance;
        if (instance != nullptr)
            delete instance;
    }
}

// [3] 0x140053270. Slot 3 is the FIRST call into this class on a load-from-save
// (PersistObject creates the object and dispatches +0x18 before Startup or any
// tick has run). Retail 0x140053270 reads an emitter count and then that many bools.
// ============================================================
// ParticleComponent::Persist (0x140053270)
// ============================================================
void ParticleComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;
    IO::Serializer ser(*st, 0);

    int localCount = active._size;
    ser.Serialize(localCount, nullptr);

    // If loading, resize bool array
    // FUN_14000a590 is Collection::Array<bool>::SetSize(n).
    if (static_cast<int>(st->GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        active.SetSize(localCount);
    }

    // Serialize bool array
    ser.Serialize(localCount, active._data, nullptr);
}

// [4] 0x140053230 — setup: create the ParticleSystemInstances.
// ============================================================
// ParticleComponent::Startup (0x140053230) — actually setup/init
// ============================================================
void ParticleComponent::Startup() {
    Setup();   // 0x140052480

    // Zero all emitter flags
    int count = active._size;
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            active[i] = false;
        }
    }
}

// [6] 0x140052f70 — fade-in start of the autoStart (b1) emitters.
// ============================================================
// ParticleComponent::OnAdded (0x140052f70)
// ============================================================
void ParticleComponent::OnAdded() {
    ParticleDescription* desc = GetParticleDesc(this);
    if (!desc) return;

    float uvScale = PC_UVScale();

    int emitterTotal = desc->_emitterCount;
    if (emitterTotal <= 0) return;

    ParticleEmitterDescription* descArray = desc->_emitters;
    ParticleEmitterSlot* geoArray = emitters._data;

    for (int i = 0; i < emitterTotal; i++) {
        // Is the emitter started on add? (autoStart, +0x11)
        if (!descArray[i].autoStart) continue;

        // Re-check IsDerivedFrom for desc (original does this per iteration)
        ParticleDescription* descCheck = GetParticleDesc(this);
        if (!descCheck) continue;

        ParticleEmitterSlot* geoEntry = &geoArray[i];

        // The fade time of the emitter
        float timeSecs = descCheck->_emitters[i].fadeTime.AsSeconds();

        // Set emitter timing: uvScale / timeSecs
        geoEntry->fadeRate = uvScale / timeSecs;

        // Check/set geometry opacity
        Graphics::ParticleInstance* geoObj = geoEntry->instance;
        if (geoObj->_opacity != 0.0f) {
            geoObj->_dirtyFlags |= 4;
            geoObj->_opacity = 0.0f;
            geoObj->_opacityPrev = 0.0f;
        }

        StartEmitter(i);   // 0x140052a50
    }
}

// [7] 0x140052e80 — remove every emitter from the scene.
// ============================================================
// ParticleComponent::OnRemove (0x140052e80)
// ============================================================
void ParticleComponent::OnRemove() {
    int totalCount = emitters._size;
    if (totalCount <= 0) return;

    ParticleEmitterSlot* geoArray = emitters._data;
    bool* flagArray = active._data;

    for (int i = 0; i < totalCount; i++) {
        ParticleEmitterSlot* geoEntry = &geoArray[i];

        // Remove scene object if valid
        uint32_t objId = (uint32_t)geoEntry->sceneId;
        if ((int)objId >= 0) {
            if (GameUpdateLoop::_singleton != nullptr) {
                PC_Scene()->RemoveObject(objId);
            }
            geoEntry->sceneId = -1;
        }

        // Clear emitter flag
        flagArray[i] = false;

        // Check if any emitter still has a valid scene object
        bool anyValid = false;
        if (totalCount > 0) {
            for (int j = 0; j < totalCount; j++) {
                if (emitters[j].sceneId >= 0) {
                    anyValid = true;
                    break;
                }
            }
        }

        if (!anyValid) {
            // Clear bit 2 of component flags
            uint32_t oldFlags = _flags;
            uint32_t newFlags = oldFlags & ~0x4u;
            _flags = newFlags;

            if (((newFlags ^ oldFlags) & 0xFFFF) != 0 && _entity != nullptr) {
                _entity->RefreshEntityFlags();   // 0x1400c3470
            }
        }
    }
}

// [8] 0x140052b30 — the per-frame tick (node-follow + fade + geometry Update),
// dispatched by the EntityList tree walk (0x1400c2e5d: RDX = the update's time
// pointer, passed through).
// ============================================================
// ParticleComponent per-frame tick (vtable +0x40, 0x140052b30)
// ============================================================
void ParticleComponent::Tick(const System::Time* time) {
    if (PC_Culled(this, time)) return;   // frozen off-screen: neither the unstamp nor the tick
    PC_UnstampOwnEmitters(this);   // SA per-frame frame-index advance (see helper) before the tick
    int total = emitters._size;
    if (total <= 0) return;
    ParticleEmitterSlot* geoArr = emitters._data;
    if (!geoArr) return;
    float uvScale = PC_UVScale();
    const System::Time& elapsed = *time;

    for (int i = 0; i < total; i++) {
        ParticleEmitterSlot* ge = &geoArr[i];

        // Node/attach-point follow: recompute transform every frame
        if (ge->nodeIndex >= 0 && ge->instance) {
            ModelComponent* model = PC_FindModel(_entity.Get());
            ParticleDescription* desc = GetParticleDesc(this);
            if (desc && model) {
                Math::Transform tf;
                bool have = false;
                Pathfinding::PointList* attach = desc->_attachPoints;
                if (attach) {
                    const Pathfinding::PointList::Point* arr = attach->Points().GetData();
                    if (arr) {
                        // Transform copy-ctor (seven dwords) from the ModelComponent's +0x148.
                        const Math::Transform modelTf =
                            model->transform;
                        const float zero = 0.0f;
                        const Math::Transform ypo = Math::Transform::FromYawPitchOffset(
                            zero, zero, arr[ge->nodeIndex].pos);
                        // Math::Transform::operator*(const Transform&) — slot 0x140151430.
                        tf = ypo * modelTf;
                        have = true;
                    }
                } else {
                    // Retail retslot door, body moved to the call site: exactly one
                    // construction into `tf`, the same storage the door wrote.
                    tf = model->meshInstance
                             .GetNodeToWorld(ge->nodeIndex);
                    have = true;
                }
                if (have)
                    ge->instance->SetTransform(tf);
            }
        }

        // Opacity fade integration + geometry tick (only while in the scene)
        if (ge->sceneId >= 0 && ge->instance) {
            Graphics::ParticleInstance* g = ge->instance;
            float op = g->_opacity;
            if (ge->fadeRate != 0.0f) {
                float dt = elapsed.AsSeconds();
                op += dt * ge->fadeRate;
                if (op > uvScale) {
                    ge->fadeRate = 0.0f;
                    op = uvScale;
                } else if (op < 0.0f) {
                    ge->fadeRate = 0.0f;
                    StopEmitter(i);   // 0x140052990
                    continue;
                } else if (op <= 0.0f) {
                    StopEmitter(i);   // 0x140052990
                    continue;
                }
            } else if (op <= 0.0f) {
                StopEmitter(i);   // 0x140052990
                continue;
            }
            if (op != g->_opacity) {
                g->_dirtyFlags |= 4;   // dirty
                g->_opacity = op;
                g->_opacityPrev = op;
            }
            ge->instance->Update(elapsed);   // slot 1
        }
    }
}

// [11] 0x1400531b0 — setup + restart the flagged emitters.
// ============================================================
// ParticleComponent::Slot11 (0x1400531b0)
// ============================================================
void ParticleComponent::Slot11() {
    ParticleDescription* desc = GetParticleDesc(this);

    Setup();   // 0x140052480

    if (!desc) return;

    int emitterTotal = desc->_emitterCount;
    if (emitterTotal <= 0) return;

    bool* flagArray = active._data;

    for (int i = 0; i < emitterTotal; i++) {
        if (flagArray[i]) {
            StartEmitterFade(i);   // 0x140053660
        }
    }
}

// [12] 0x140053090 — event 2/3 fade-in/out of the b2 emitters.
// ============================================================
// ParticleComponent_Slot12 — event handler (vtable +0x60, 0x140053090)
// event 2 = start with fade-in, event 3 = begin fade-out (b2 emitters only)
// ============================================================
void ParticleComponent::OnEvent(int eventCode) {
    ParticleDescription* desc = GetParticleDesc(this);
    if (!desc) return;
    ParticleEmitterDescription* emArr = desc->_emitters;
    ParticleEmitterSlot* geoArr = emitters._data;
    int total = emitters._size;
    if (!emArr || !geoArr) return;

    for (int i = 0; i < total; i++) {
        const ParticleEmitterDescription& elem = emArr[i];
        if (!elem.eventToggled) continue;   // b2: event-toggled emitters only
        if (eventCode == 2) {
            StartEmitterFade(i);   // 0x140053660
        } else if (eventCode == 3) {
            float t1 = elem.fadeTime.AsSeconds();
            geoArr[i].fadeRate = -(PC_UVScale() / t1);
        }
    }
}

// create 0x14000a360: MemAlloc(0x48, 8), then the ctor.
::Object::Base* ParticleComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ParticleComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ParticleComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ParticleComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ParticleComponent());
}

// [PREFETCHCOV] the ctor records the vptr (Object::ClassVptr<ParticleComponent>): the staged walk
// prefetch (entitylist.cpp, kPrefetchCovParticle) tells this class by it.

DEFINE_EXE_TYPE(ParticleComponent, "Component")
