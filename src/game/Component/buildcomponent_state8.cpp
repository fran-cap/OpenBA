// src/game/Component/buildcomponent_state8.cpp
// Brick 2a — the BuildComponent construction STATE MACHINE handlers past state-1 enter:
//   * State1Update  (sub_81e0 @ 0x1401081e0) — per-frame: when all raw materials are in
//                    the site storage (sub_8630) request transition 1 -> 8.
//   * State8Enter   (sub_7ae0 @ 0x140107ae0) — on entering state 8. STAGED branch: reset
//                    the construction meshes to frame 0 and spawn the BuildStructure labor
//                    work. INSTANT branch (gw+0xd0 bit 0): snap every mesh to its final
//                    frame, stamp delivered(+0x48) = target, request 8 -> 2.
//   * State8Update  (sub_76e0 @ 0x1401076e0) — per-frame: advance each mesh's build frame
//                    proportional to progress(+0x48)/target; when progress >= target snap
//                    meshes to final + request transition 8 -> 2.
//   * State8OneShot (sub_7600 @ 0x140107600) — fires as state 8 exits: model mode 4 +
//                    remove the site from the pending-build registry (townMgr+0xd0).
//   * State8Finalize(sub_7ff0 @ 0x140107ff0) via State8To2Action (sub_75e0 @ 0x1401075e0)
//                    — the 8->2 transition ACTION: refund leftover materials to town stock
//                    (townMgr+0x170) and reconfigure the site storage as OUTPUT storage.
//
// Node-slot mapping (recovered 2026-07-11 — see reference_construction_pipeline_map):
//   StateMachineComponent::Update -> StateMachine::Tick(sm, time, altDraw=1, doDraw=1)
//   EVERY frame. Tick skips its UPDATE phase (slot +0x20) unless a transition is pending,
//   and runs the DRAW phase (slot +0x38, altDraw=1) every frame. So the PER-FRAME handler
//   is the trans38Handler (slot +0x38); the exit-transition handler is the updateHandler
//   (slot +0x20). The controller_state_table.h dispatcher forwards `sm` to both.
//
// A ported handler requests a transition via ControllerLive::RequestStateTransition(sm,id)
// (replicates `node->slot08(node, sm->index, &desired); sm[+0xa/+0xb] = ...`).
//
// Faithful offset-by-offset port of the Ghidra decompilations. All exe-internal helpers
// are resolved guarded; if one is unavailable the step is skipped.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Mesh.h"
#include "Component/StorageDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ModelDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/MapComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/BuildDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/Component.h"          // B.3.x W0: Component slots by name
#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "Component/controller_state_table.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Object/factory_registration.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Work/BuildStructure.h"   // B4 X4-WORK: BuildStructure_CreateInPlaceFull returns the typed work
#include "Action/exe_bodies.h"
#include "Component/pickingcomponent_funcs.h"
#include "system/MemoryAllocator.h"

#include "Component/BuildComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"

extern "C" void* g_pConstructionDemoSite;                            // the seeded foundation
extern "C" void* g_pBuildStructureWork = nullptr;                    // 2c-B probe: last spawned build work
// BuildStructure_CreateInPlace (0x1400e5a90) and BuildStructure_CreateInPlaceFull
// (0x1400e59c0), buildstructure.cpp, are declared in Work/exe_bodies.h.
// workplacecomponent.cpp — roster-only mode: the WPC attach/detach (fa20/f7e0 reimpl) skips the
// Scene mesh-visual toggle, so seating a Builder does NOT swap the site's build mesh.
extern bool g_wpcRosterOnly;

// ---- demo-force gate -------------------------------------------------------------
// Brick-2a plumbing harness: fake the citizen half so the state chain can be validated
// crash-free without live hauling/labor. When on, for the demo site only: State1Update
// treats materials as present (force 1->8), and State8Update ramps progress(+0x48) toward
// target each frame (drives the mesh progression + 8->2 completion). Brick 2b/2c replace
// these fakes with real GatherRawMaterials haul + BuildStructure labor.
static int s_bc8_force = -1;
static bool BC8_DemoForce() {
    if (s_bc8_force < 0) {
        const char* e = Opt().buildDemoForce;
        s_bc8_force = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return s_bc8_force == 1;
}
static bool BC8_IsDemo(BuildComponent* bc) { return g_pConstructionDemoSite && bc == g_pConstructionDemoSite; }

// ---- exe helpers, called directly ------------------------------------------------
//   FUN_140003050 FindComponent<WorkComponent>
//   FUN_14012eb50 WorkComponent_RegisterWorkSlot (stop/release the worker)
//   0x1400c2b70   ModelComponentNotify_Reimpl (ModelComponent_helper_2b70)
//   0x140128f50   PickingComponent_RegisterMesh (Entity_impl_8f50)
//   0x140006270   FindComponent<StorageComponent>
//   0x140002f90   FindComponent<StatusIconComponent>
//   FUN_14012c3f0 CitizenComp_FUN_14012c3f0 (mesh finalize)
//   FUN_140088f90 ActRm_GetResourceCategory_88f90
//   0x140017550   Collection::Array<InternalPtr<T>>::Find (BuildComponent_helper_7550)
//   0x14012ec60   CitizenAI_WorkScheduler_Post (CitizenAI_impl_ec60, scheduler post)
// 0x14000f090 was Collection::Array<Bytes<0x10,8>>::SetSize monomorphised into
// the exe (ENDGAME B.3.2 step 2: stride 0x10, align 8, zero tail) — converted
// at its call site below.

// Stop/release the site's build worker (FUN_140003050 -> FUN_14012eb50), as the exe does
// before requesting the 1->8 / 8->2 transition.
static void BC8_StopWorker(BuildComponent* bc) {
    WorkComponent* wc = bc->_entity->FindComponent<WorkComponent>();
    if (wc) wc->CancelWork();
}

// the ModelDescription of a model (its +0x18, gated to ModelDescription DAT_1401c7a88).
static ModelDescription* BC8_MeshModelDesc(ModelComponent* meshElem) {
    Description* d = meshElem->_description;
    uint32_t tid = ModelDescription::TypeId();
    if (d && tid && ::Object::IsDerivedFromNullSafe(d, tid)) return static_cast<ModelDescription*>(d);
    return nullptr;
}
// Frame count for a model's current mesh group (modelDesc+0x20 group array, stride
// 0x18, frameCount @ +0x10).
static int BC8_MeshFrameCount(ModelComponent* meshElem, ModelDescription* modelDesc) {
    if (!modelDesc) return 0;
    int16_t variant = meshElem->meshGroup;
    ModelMeshGroup* groupBase = modelDesc->_meshGroups;
    if (!groupBase || variant < 0) return 0;
    return groupBase[variant].meshCount;
}
// Frame mesh pointer: modelDesc+0x20 group[variant].framePtrs(+8)[frame].
static Graphics::GraphicsMesh* BC8_MeshFramePtr(ModelComponent* meshElem, ModelDescription* modelDesc, int frame) {
    if (!modelDesc || frame < 0) return nullptr;
    int16_t variant = meshElem->meshGroup;
    ModelMeshGroup* groupBase = modelDesc->_meshGroups;
    if (!groupBase || variant < 0) return nullptr;
    Graphics::GraphicsMesh** framePtrArr = groupBase[variant].meshes;
    if (!framePtrArr) return nullptr;
    return framePtrArr[frame];
}
// Re-add a model's draw children (its entity's components, each one's slot 12).
static void BC8_ReaddDrawChildren(ModelComponent* meshElem) {
    ComponentEntity* host = meshElem->_entity.Get();
    if (!host) return;
    int hc = host->_components._size;
    for (int k = 0; k < hc; ++k) {
        Component* h = host->_components[k].Get();
        if (!h) continue;
        h->OnEvent(0);   // slot 12
    }
}
// Set one model to `frame` (hide old via slot 7, SetMesh new frame, show via
// slot 6). Mirrors the frame-set block shared by sub_76e0 / sub_7ae0.
static void BC8_SetMeshFrame(ModelComponent* model, int frame) {
    int16_t cur = model->meshIndex;
    if ((int)cur == frame) return;
    if (cur >= 0)
        model->OnRemove();   // slot 7
    model->meshIndex = (int16_t)frame;
    if (frame >= 0) {
        ModelDescription* modelDesc = BC8_MeshModelDesc(model);
        Graphics::GraphicsMesh* framePtr = BC8_MeshFramePtr(model, modelDesc, frame);
        model->meshInstance
            .SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(framePtr));   // 0x1800ffea0
        model->OnAdded();    // slot 6
    }
}

// the BuildDescription (+0x18 gated to BuildDescription DWORD_1401c7040).
static BuildDescription* BC8_MatComp(BuildComponent* bc) {
    Description* d = bc->_description;
    uint32_t tid = BuildDescription::TypeId();
    if (d && tid && ::Object::IsDerivedFromNullSafe(d, tid)) return static_cast<BuildDescription*>(d);
    return nullptr;
}
static int BC8_Target(BuildComponent* bc, BuildDescription* matComp) {
    if (!matComp) return 0;
    // target = base build effort (matComp+0x30) * multiplier (bc+0x4c). The multiplier is
    // MultiObjectComponent+0x58 (0 when that component is inert in the standalone exe);
    // clamp to the retail default of 1 so the target never collapses to 0 (which would
    // instant-complete the build with no visible labor). See buildcomponent_state1.cpp for
    // the source clamp.
    int mult = bc->costMultiplier;
    if (mult <= 0) mult = 1;
    return matComp->_i30 * mult;
}

// Ensure the construction mesh list (+0x58 / count +0x60) is populated.
//
// SLOT CORRECTED 2026-07-27 (this comment previously said "draw-tick, vtable +0x40" and
// was the source of two sessions' worth of wrong premise): 0x140108d40 is vtable INDEX 6
// (+0x30), the ONE-SHOT OnAdded broadcast that ComponentEntity_OnAddedPass dispatches at
// EntityList_RegisterEntity time — NOT a per-frame draw. Verified: the function's only
// xref is 0x140162290 == vtable_BuildComponent(0x140162260) + 0x30, and +0x40 (index 8,
// 0x140118a70) is a shared empty `return;` stub. Ghidra's "BuildComponent_Draw" label is
// the same slot-6 mislabel as InteractComponent's and CitizenComponent's.
//
// So retail fills this list ONCE, when the site entity is registered. That slot is
// wired, so this lazy call normally finds the list already filled and returns; it stays
// because any site registered with the instant-build bit set leaves it empty (count=0),
// and then the state-8 mesh loops would iterate zero times (no visible rising). The
// appended IBPs persist for the whole state-8 duration, so one call suffices. This lazy
// call is idempotent with the wired slot: whichever runs first leaves count>0 and the
// other no-ops.
static void BC8_EnsureMeshList(BuildComponent* bc) {
    if (bc->modelComponents._size > 0) return;
    bc->FillModelLists();
}

// ==================================================================================
// State1Update — sub_81e0 (per-frame while in state 1). trans38Handler for state 1.
// ==================================================================================
char BuildComponent::State1Update(StateMachine* sm) {
    bool haveAll = HaveAllMaterials();
    if (!haveAll && BC8_DemoForce() && BC8_IsDemo(this)) {
        haveAll = true;   // brick-2a harness: pretend materials arrived (real haul = 2b)
    }
    if (haveAll) {
        BC8_StopWorker(this);
        ::ControllerLive::RequestStateTransition(sm, 8);
    }
    return 1;
}

// ==================================================================================
// State8Enter — sub_7ae0 (on entering state 8). enterHandler for state 8.
// BOTH branches are now ported: STAGED (reset construction meshes to frame 0 + spawn the
// BuildStructure work) and INSTANT (gw+0xd0 bit 0 — snap every mesh to its final frame,
// stamp +0x48 = target, request 8->2).
//
// sub_8720 IDENTIFIED 2026-07-27: 0x140108720 is the BODY of BuildComponent's OnAdded —
// 0x140108d40 is only a 4-instruction thunk that tests the instant-build bit and either
// falls through to it or returns. The body has exactly two callers: that thunk, and this
// handler at 0x140107bb0. So the instant branch reuses the same list-fill + model-reset,
// entered directly (bypassing the gate, which is set precisely when this path runs).
// See docs/history/notes/BRIDGE_HIGHLIGHT_DIALOG_BRIEF.md.
// ==================================================================================
char BuildComponent::State8Enter(StateMachine* sm) {
    uint8_t instant = 0;
    if (GameUpdateLoop::_singleton != nullptr)
        instant = GameUpdateLoop::_singleton->m_gameFlags & 1;

    if (instant == 0) {
        // STAGED: reset every construction mesh to frame 0.
        BC8_EnsureMeshList(this);
        int meshCount = modelComponents._size;
        for (int i = 0; i < meshCount; ++i) {
            ModelComponent* me = modelComponents[i].Get();
            if (!me) continue;
            int16_t cur = me->meshIndex;
            if (cur == 0) {
                me->_entity->SendEvent(0);
            } else {
                BC8_SetMeshFrame(me, 0);
                BC8_ReaddDrawChildren(me);
            }
            if (pickingComponents[i].Get() != nullptr) {
                pickingComponents[i].Get()->RefreshMesh();  // lane D: deref the IBP -- retail takes the PickingComponent, not its slot
            }
        }
        // Brick 2c: alloc BuildStructure (0x70) + find the site WorkComponent (c8070)
        // + CitizenAI_impl_ec60(worker, 0x2001, 1, &bsIBP) to enqueue the construct work,
        // so citizens claim it and do timed BuildStructure labor (BuildStructure::Execute
        // assigns each to a build position; PerformWork accumulates progress into bc+0x48).
        // Faithful to sub_7ae0's STAGED tail. Needs the live-citizen action stack +
        // task manager (same as the gather post).
        {
            void* mem = (::System::MemoryAllocator::_singleton)
                            ? ::System::MemoryAllocator::MemAlloc(0x70, 8) : nullptr;
            // Construct via the FULL retail ctor (0x1400e59c0): it routes base init through
            // Work_Ctor25 (sets field_10=site entity, +0x18 WorkComponent, +0x28 target cell)
            // AND allocates the build-position byte array field_50/field_58 from the site's
            // PositionComponent footprint. sub_7ae0 passes &(*(BuildComponent+0x10)) as the
            // entity IBP. Without the array, BuildStructure::Execute sees 0 unbuilt positions
            // and returns 1 (never assigns labor) — this is the 2c-B fix.
            BuildStructure* bsWork = nullptr;
            if (mem) {
                ComponentEntity* siteEnt = _entity.Get();   // *(BuildComponent+0x10)
                bsWork = new (mem) BuildStructure(&siteEnt, 0);   // 0x1400e59c0
            }
            if (bsWork) {
                g_pBuildStructureWork = bsWork;   // 2c-B probe: correlate with 7b60 claim walk
                Object::InternalPtr<Work> bsIBP(bsWork);
                // find the site WorkComponent (c8070) the work posts under
                WorkComponent* workComp = _entity->FindComponent<WorkComponent>();
                if (workComp != nullptr) {
                    workComp->PostWork(0x2001, 1, &bsIBP);

                    // Builders are seated by the tick-driven Builder pool
                    // (BuilderPool_Tick) through the townMgr+0x100 registration made at
                    // State1Enter — State8Enter must not also direct-seat, or the site
                    // would be double-staffed.
                }
            }
        }
    } else {
        // ---------------------------------------------------------------------------
        // INSTANT branch (gw+0xd0 bit 0) — retail's whole insta-build completion, ported
        // 2026-07-27. This is the path the start-town spawner drives: it brackets each
        // building's SetModel(variant, frame 0) + RegisterEntity with the bit set, and
        // StateMachineComponent's OnAdded (slot 6, 0x14012b660) ticks the SM inside that
        // same OnAddedPass. StateMachine::Tick loops while a pending state is set, so
        // 1 (State1Enter -> req 8) -> 8 (here -> req 2) -> 2 all runs before the spawner
        // clears the bit. Nothing else in the game sets it except the debug menu's
        // insta-build setting.
        //
        // Retail order (0x140107bb0..): sub_8720 first (it writes +0x4c), THEN the target,
        // then snap every collected mesh to its FINAL frame (frameCount-1 — the opposite
        // end of the ramp from the staged branch's frame 0), then request state 2.
        BuildDescription* matComp = BC8_MatComp(this);
        FillModelLists();           // sub_8720 (mesh-list fill + model reset)
        laborProgress = BC8_Target(this, matComp);
        int meshCount = modelComponents._size;
        for (int i = 0; i < meshCount; ++i) {
            ModelComponent* me = modelComponents[i].Get();
            if (!me) continue;
            int finalFrame = BC8_MeshFrameCount(me, BC8_MeshModelDesc(me)) - 1;
            if (finalFrame == (int)me->meshIndex) {
                me->_entity->SendEvent(0);
            } else {
                BC8_SetMeshFrame(me, finalFrame);
                BC8_ReaddDrawChildren(me);
            }
            if (pickingComponents[i].Get() != nullptr) {
                pickingComponents[i].Get()->RefreshMesh();  // lane D: deref the IBP -- retail takes the PickingComponent, not its slot
            }
        }
        ::ControllerLive::RequestStateTransition(sm, 2);
    }
    return 1;
}

// ==================================================================================
// State8Update — sub_76e0 (per-frame while in state 8). trans38Handler for state 8.
// Advances mesh frames proportional to progress/target; on completion snaps to final +
// requests 8 -> 2.
// ==================================================================================
char BuildComponent::State8Update(StateMachine* sm) {
    BuildDescription* matComp = BC8_MatComp(this);
    int target = BC8_Target(this, matComp);

    if (BC8_DemoForce() && BC8_IsDemo(this)) {
        // brick-2a harness: ramp progress toward target (~60 frames) to drive the mesh
        // progression + completion. Real BuildStructure labor = brick 2c.
        //
        // The real target = matComp[0x30] (base build effort) * bc[0x4c] (build
        // multiplier). In standalone bc[0x4c] can be 0 — FillModelLists stage 4 copies
        // it from the MultiObjectComponent's +0x58 field — so the real target is 0.
        // Substitute a nominal positive target (base effort, or 120 frames) so the mesh
        // progression + completion demonstrate visibly. Real BuildStructure labor (2c)
        // computes and raises the true target.
        // Ramp budget in frames (~60fps): default 600 (~10s) so the rise is easy to watch.
        // Override with BANISHED_BUILD_DEMO_FRAMES.
        static int rampFrames = -1;
        if (rampFrames < 0) {
            const char* e = Opt().buildDemoFrames;
            rampFrames = (e && e[0]) ? std::atoi(e) : 600;
            if (rampFrames < 1) rampFrames = 600;
        }
        // Demo mode ALWAYS ramps over rampFrames. The real target (matComp[0x30] *
        // bc[0x4c]) is tiny in SA — when bc[0x4c] (build multiplier) is 0, BC8_Target
        // falls back to the bare base effort (~30), so the foundation would complete in
        // ~30 ticks, too fast to inspect or click. Stretching to rampFrames keeps the
        // building visibly under construction (default 600 ≈ 10s; raise BANISHED_BUILD_DEMO_FRAMES
        // to e.g. 30000 for a leisurely click test). Real BuildStructure labor (2c) uses
        // the true target and is unaffected (this block is demo-force + demo-site only).
        target = rampFrames;
        int inc = target / rampFrames; if (inc < 1) inc = 1;
        int p = laborProgress + inc;
        if (p > target) p = target;
        laborProgress = p;
    }

    BC8_EnsureMeshList(this);
    int progress = laborProgress;
    int meshCount = modelComponents._size;


    if (progress < target) {
        // still building — advance each mesh to floor((progress/target)*frameCount),
        // clamped to frameCount-2 so it never shows the final frame while incomplete.
        for (int i = 0; i < meshCount; ++i) {
            ModelComponent* me = modelComponents[i].Get();
            if (!me) continue;
            ModelDescription* modelDesc = BC8_MeshModelDesc(me);
            int frameCount = BC8_MeshFrameCount(me, modelDesc);
            if (frameCount <= 0) continue;
            int frame = (int)(((float)progress / (float)target) * (float)frameCount);
            if (frame == frameCount - 1) frame = frameCount - 2;
            int16_t cur = me->meshIndex;
            if (frame != (int)cur) {
                BC8_SetMeshFrame(me, frame);
                me->_entity->SendEvent(0);
                if (pickingComponents[i].Get() != nullptr) {
                    pickingComponents[i].Get()->RefreshMesh();  // lane D: deref the IBP -- retail takes the PickingComponent, not its slot
                }
            }
        }
    } else {
        // labor complete — stop worker, snap every mesh to final frame, request 8 -> 2.
        BC8_StopWorker(this);
        for (int i = 0; i < meshCount; ++i) {
            ModelComponent* me = modelComponents[i].Get();
            if (!me) continue;
            ModelDescription* modelDesc = BC8_MeshModelDesc(me);
            int frameCount = BC8_MeshFrameCount(me, modelDesc);
            int finalFrame = frameCount - 1;
            int16_t cur = me->meshIndex;
            if (finalFrame == (int)cur) {
                me->_entity->SendEvent(0);
            } else {
                BC8_SetMeshFrame(me, finalFrame);
                BC8_ReaddDrawChildren(me);
            }
            if (pickingComponents[i].Get() != nullptr) {
                pickingComponents[i].Get()->RefreshMesh();  // lane D: deref the IBP -- retail takes the PickingComponent, not its slot
            }
        }
        ::ControllerLive::RequestStateTransition(sm, 2);
    }
    return 1;
}

// ==================================================================================
// State8OneShot — sub_7600 (updateHandler for state 8; fires as state 8 exits).
// Model mode 4 + remove site from the pending-build registry (townMgr+0xd0) + finalize
// the site MeshInstance.
// ==================================================================================
char BuildComponent::State8OneShot(StateMachine* /*sm*/) {
    _entity->SendEvent(4);

    CitizenBehavior* townMgr = nullptr;
    if (GameUpdateLoop::_singleton != nullptr)
        townMgr = GameUpdateLoop::_singleton->m_citizenList;
    Object::InternalPtr<BuildComponent> selfIBP(this);
    if (townMgr) {
        int idx = townMgr->buildComponents.Find(selfIBP);   // 0x140017550
        if (idx >= 0) {
            int count = townMgr->buildComponents._size;
            Object::InternalPtr<BuildComponent>* base = townMgr->buildComponents._data;
            for (int j = idx; j + 1 < count; ++j)
                base[j] = base[j + 1];
            townMgr->buildComponents._size = count - 1;
        }
    }
    {
        StatusIconComponent* statusIcon = _entity->FindComponent<StatusIconComponent>();
        if (statusIcon) {
            statusIcon->SetFlag(0, 0);   // 0x14012c3f0 (CitizenComp_FUN_14012c3f0's member)
        }
    }
    return 1;
}

// ==================================================================================
// State8Finalize — sub_7ff0 (via State8To2Action sub_75e0, the 8->2 transition action).
// Refund leftover site-storage materials to town stock (townMgr+0x170) + reconfigure the
// site storage as OUTPUT storage.
// ==================================================================================
void BuildComponent::State8Finalize() {
    // Retail order: the 8->2 transition puts every controller into state 2, so the
    // workplace hand-off happens alongside this storage reconfigure. Do it first and
    // unconditionally — the storage half below early-returns on several data shapes
    // (no StorageComponent, no StorageDescription) and the profession must clear anyway.
    // The live WorkPlaceComponent controller links a real state node, so the SAME 8->2
    // transition enters state 2 on it and the SM dispatches edf0 itself (the faithful
    // path). BuildComponent_ClearBuilderWorkplace is NOT called here: running the hook
    // too would drive the enter twice (the register half dedups, but f670's worker
    // release does not want a second pass).

    // No decal event here: retail 0x140107ff0 (and the 8->2 action 0x1401075e0 that
    // calls it) sends none. The footprint decal went opaque at ClearArea state 2
    // (DecalComponent::OnEvent(1), 0x1401177f0); D.4 RESIDUE dropped the tree's call.

    StorageComponent* storage = _entity->FindComponent<StorageComponent>();
    if (storage == nullptr) return;

    CitizenBehavior* townMgr = nullptr;
    if (GameUpdateLoop::_singleton != nullptr)
        townMgr = GameUpdateLoop::_singleton->m_citizenList;

    // (a) refund leftover storage slots to town stock (townMgr+0x170; entry idx*0x20+0xc).
    int slotCount = storage->storageSlots._size;
    for (int k = 0; k < slotCount; ++k) {
        StorageComponent::Item* slotArr = storage->storageSlots._data;
        StorageComponent::Item* slot = slotArr + k;
        int count = slot->_count;
        ComponentDescription* matType = slot->_resource;
        // ActRm_GetResourceCategory_88f90 is a forwarder that never reads its first argument.
        int resIdx = ResourceCategory(matType);   // 0x140088f90
        if (resIdx >= 0 && townMgr) {
            ProfessionResourceEntry* stockBase = townMgr->professionResources._data;
            if (stockBase) {
                int* pStock = &stockBase[resIdx].field_0x0C;
                *pStock += count;
            }
        }
    }

    // (b) reconfigure storage as OUTPUT storage (StorageDescription-driven).
    StorageDescription* desc = nullptr;
    {
        Description* d = storage->_description;
        uint32_t tid = StorageDescription::TypeId();
        if (d && tid && ::Object::IsDerivedFromNullSafe(d, tid)) desc = static_cast<StorageDescription*>(d);
    }
    if (!desc) return;
    int mult = 1;
    if (desc->_flagA) {
        // sized output: scan storage entity for a MapComponent -> footprint.
        ComponentEntity* ent = storage->_entity.Get();
        uint32_t tidMap = MapComponent::TypeId();
        MapComponent* mapc = nullptr;
        if (ent && tidMap) {
            int cc = ent->_components._size;
            for (int i = 0; i < cc; ++i) {
                Component* c = ent->_components[i].Get();
                if (!c) continue;
                uint32_t t = c->Type();   // slot 2
                if (t == tidMap) { if (::Object::IsDerivedFromNullSafe(c, tidMap)) mapc = static_cast<MapComponent*>(c); break; }
            }
        }
        if (mapc)
            mult = (int)static_cast<int16_t>(mapc->tileY1) *
                   (int)static_cast<int16_t>(mapc->tileX1);
    }
    int perItem   = desc->_pickupValue;
    int acceptTyp = static_cast<int>(desc->_rawMaterialFlags);
    storage->storageSlots.SetSize(0);
    storage->rawMaterialFlags = static_cast<uint32_t>(acceptTyp);
    storage->capacity = perItem * mult;
}

// sub_75e0 — the 8->2 transition ACTION (node slot +0x28 / our OnState).
char BuildComponent::State8To2Action() {
    State8Finalize();
    return 0;   // return ignored by Tick (Phase A2 discards the slot-0x28 return)
}
