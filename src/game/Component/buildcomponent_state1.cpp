// src/game/Component/buildcomponent_state1.cpp
// Clean C++ port of BuildDescription_helper_8290 @ 0x140108290 — the BuildComponent
// state-1 ("gather materials") ENTER handler. This is the construction spawner: it
//   1. reads the build cost multiplier off the workplace component (+0x4c),
//   2. resets the site ModelComponent build variant,
//   3. registers the site into the construction registry (townMgr+0xd0),
//   4. if not all materials are delivered (sub_8630) and not instant-build:
//        - constructs a GatherRawMaterials work (GatherRawMaterials_Ctor),
//        - POSTS it to the work-reservation ledger via CitizenAI_impl_ec60 (0x14012ec60)
//          [GUARDED/deferred in standalone — the shared-AVL ledger insert belongs to the
//           later citizen-consumer brick; here we only trace the intent],
//        - configures the site StorageComponent to ACCEPT the required raw materials
//          (accept-mask +0x3c = OR of each material's accept bits; capacity +0x38 =
//           Σ costQty·carryWeight·multiplier),
//   5. else (materials present / instant): forces the state node to transition to state 8.
//
// ABI: this is a state-node ENTER handler. The exe dispatcher (0x140002b60) tail-calls
// it as `MOV RCX,[node+0x18]; JMP handler`, so it receives RCX = the BuildComponent
// (node+0x18 = *(BuildComponent+0x40 selfPtr) = the BuildComponent) AND RDX = the state
// machine node (preserved through the tail-JMP — bug-class #6 implicit forwarding). The
// controller_state_table.h Dispatcher<Tag>::Enter forwards `sm` as the 2nd arg.
//
// Faithful offset-by-offset port of the Ghidra decompilation of 0x140108290. All
// exe-internal helpers are resolved guarded; if one is unavailable the step is skipped.

#include <new>
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/BuildDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ModelComponent.h"   // B.3.8 CB1a (TypeId)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"   // ControllerLive::RequestStateTransition
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Work/GatherRawMaterials.h"   // B8: the (site) constructor 0x1400ec710
#include "system/MemoryAllocator.h"

#include "Component/BuildComponent.h"
#include "Component/ClearAreaComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelDescription.h"
#include "Component/MultiObjectComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/ProfessionList.h"

// town_workplace_registry.cpp — register a workplace into townMgr+0x100 (profession map).
extern "C" void  TownRegistry_RegisterWorkplace(CitizenBehavior* townMgr, const WorkPlaceComponent* workplaceComp);

// ---- Exe helpers, called directly --------------------------------------------------
// FUN_140004740 InteractComponent_FindWorkplaceComponent(entity, retIBP) -> retIBP
// BuildComponent_helper_7550 == 0x140017550 == Collection::Array<InternalPtr<T>>::Find(ibp) (B9 RT)
// FUN_140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised into
// the exe (ENDGAME B.3.2 step 2) — converted at its call site below.
// FUN_140003050 WorkBase_FindComponent_3050(entity, retIBP) -> WorkComponent (0x1401c8070)
// CitizenAI_impl_ec60 = CitizenAI_WorkScheduler_Post(workComp, code, flag, workIBP)
// Entity_FindComponent_Storage @ 0x140006270 = WorkBase_FindComponent_6270(entity, retIBP)
// FUN_140002e10 AttendanceComponent_GetJobHostComponent(entity, retIBP) -> WorkPlaceComponent
// FUN_14000f090 StorageComponent_DynArrayResize(slotArrayField, count) was
// Collection::Array<Bytes<0x10,8>>::SetSize monomorphised into the exe
// (ENDGAME B.3.2 step 2: stride 0x10, align 8, zero tail) — converted below.

// Scan a material's ComponentDescription sub-description list (_subDescs / _subDescCount)
// for the first entry whose Type() (slot 2) == tid; verify with IsDerivedFrom. Mirrors
// the material-scan the exe does twice in 0x140108290 (and gather Execute's finder).
static RawMaterialDescription* BCS1_FindRawMatDesc(ComponentDescription* matEntity, uint32_t tid) {
    if (!matEntity || !tid) return nullptr;
    int count = matEntity->_subDescCount;
    Description** arr = matEntity->_subDescs;
    if (!arr) return nullptr;
    for (int i = 0; i < count; i++) {
        Description* comp = arr[i];
        if (!comp) continue;
        uint32_t t = comp->Type();
        if (t == tid) {
            if (::Object::IsDerivedFromNullSafe(comp, tid)) return static_cast<RawMaterialDescription*>(comp);
            return nullptr;
        }
    }
    return nullptr;
}

// Natural builder-flow: construction sites are registered into the townMgr+0x100
// profession BST under the Builder key at construction start (faithful to retail's WPC
// OnCreate FUN_14012f520), so the tick-driven Builder pool (BuilderPool_Tick) finds them
// via FindAvailableBuilding and seats idle laborers as Builders.

// ---- Construction-demo site (harness) -------------------------------------------
// `g_pConstructionDemoSite` is the BuildComponent of the one foundation StartTown leaves
// unbuilt when BANISHED_BUILD_DEMO_INDEX / BANISHED_BUILD_DEMO_HOUSE ask for it; the
// construction force harness (BANISHED_BUILD_DEMO_FORCE) drives only that site. It is not
// a gate: every BuildComponent runs the construction states as retail does.
extern "C" { void* g_pConstructionDemoSite = nullptr; }
// The demo foundation's OWNER entity (set by StartTown alongside g_pConstructionDemoSite).
// The construction force harness uses it as the GatherRawMaterials/BuildStructure work-area.
extern "C" { ComponentEntity* g_pConstructionDemoEntity = nullptr; }

// 0x140108290, the state-1 ENTER handler (sm = the state machine).
int BuildComponent::State1Enter(StateMachine* smNode) {
    // 0x14000a170 (FindComponent<ClearAreaComponent>) and 0x140118270
    // (ModelComponent_impl_8270): ClearArea_Finalize8270 is the clean C++ port
    // of the second, called below.

    // +0x4c = 1 (default multiplier)
    costMultiplier = 1;

    // multiplier from the MultiObjectComponent (+0x58), 0x140004740
    MultiObjectComponent* multi = _entity->FindComponent<MultiObjectComponent>();
    if (multi != nullptr) {
        // Retail: bc+0x4c = MultiObjectComponent+0x58 (the build-effort multiplier). In the
        // standalone exe that component's lifecycle is inert so +0x58 can stay 0, which
        // would clobber the default 1 set just above and make the state-8 build target
        // (matComp[0x30] * mult) collapse to 0 (instant completion, no visible labor). Only
        // adopt the value when it is a valid positive multiplier.
        uint32_t wpMult = multi->multiCount;
        if ((int32_t)wpMult > 0)
            costMultiplier = static_cast<int32_t>(wpMult);
    }

    // reset the site ClearAreaComponent build variant (a170 -> 8270)
    ClearAreaComponent* clearArea = _entity->FindComponent<ClearAreaComponent>();
    if (clearArea != nullptr) {
        clearArea->FinishClear();
    }

    // register the site into the construction registry (townMgr+0xd0 / count +0xd8)
    CitizenBehavior* townMgr = nullptr;
    if (GameUpdateLoop::_singleton != nullptr)
        townMgr = GameUpdateLoop::_singleton->m_citizenList;
    Object::InternalPtr<BuildComponent> siteIbp(this);
    if (townMgr) {
        int idx = townMgr->buildComponents.Find(siteIbp);   // 0x140017550
        if (idx < 0) {
            // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill).
            int newCount = townMgr->buildComponents._size + 1;
            townMgr->buildComponents
                .SetSizeFill(newCount, &siteIbp);
        }
    }

    // instant-build flag: gw[0xd0] & 1
    uint8_t instant = 0;
    if (GameUpdateLoop::_singleton != nullptr)
        instant = GameUpdateLoop::_singleton->m_gameFlags & 1;

    bool haveAll = HaveAllMaterials();

    if (!haveAll && instant == 0) {
        // -------- SPAWN branch: build the gather work + configure site storage --------
        void* mem = (::System::MemoryAllocator::_singleton)
                        ? ::System::MemoryAllocator::MemAlloc(0x78, 8) : nullptr;
        Work* work = nullptr;
        if (mem) {
            ComponentEntity* entIbp = _entity.Get();
            work = new (mem) GatherRawMaterials(&entIbp);   // 0x1400ec710
        }
        ::Object::InternalPtr<Work> workIbp(work);

        // find the WorkComponent (0x1401c8070) the work posts under
        WorkComponent* workComp = _entity->FindComponent<WorkComponent>();

        // POST to the reservation ledger (brick 2b): CitizenAI_impl_ec60 inserts the
        // gather work into the gw+0x118 priority AVL (via 0x1400f88a0) keyed by the
        // WorkComponent, so an idle citizen claims it and runs GatherRawMaterials::Execute
        // (the town->site haul). Exe: CitizenAI_impl_ec60(*pWorkCompIBP, 0x2001, 1, &workIBP).
        // Needs the live-citizen action stack + pathfinder.
        if (workComp != nullptr) {
            workComp->PostWork(0x2001, 1, &workIbp);
        }

        // configure the site StorageComponent accept-mask + capacity
        Description* descObj = _description;
        uint32_t buildDescTid = BuildDescription::TypeId();
        BuildDescription* buildDesc = (descObj && buildDescTid &&
                           ::Object::IsDerivedFromNullSafe(descObj, buildDescTid))
                                          ? static_cast<BuildDescription*>(descObj) : nullptr;

        StorageComponent* storage = _entity->FindComponent<StorageComponent>();

        if (storage != nullptr) {
            storage->storageSlots.SetSize(0);

            uint32_t acceptMask = 0;
            int totalQty = 0;
            int multiplier = costMultiplier;
            uint32_t rawMatTid = RawMaterialDescription::TypeId();

            int matCount = buildDesc ? buildDesc->_costCount : 0;
            BuildCost* matArr = buildDesc ? buildDesc->_costs : nullptr;
            int found = 0;
            for (int i = 0; i < matCount; i++) {
                ComponentDescription* matEntity = matArr[i].material;   // element @0 = raw material
                int costQty = matArr[i].quantity;                       // element @8 = cost qty
                RawMaterialDescription* rawMat = BCS1_FindRawMatDesc(matEntity, rawMatTid);
                if (rawMat) {
                    ++found;
                    acceptMask |= rawMat->_flags;
                    int carryWeight = rawMat->_weight;
                    totalQty += costQty * carryWeight * multiplier;
                }
            }

            storage->rawMaterialFlags = acceptMask;
            storage->capacity = totalQty;

        }

    } else {
        // -------- Materials present / instant: force transition to state 8 --------
        // Retail (0x140108290 else-branch):
        //     req = 8;
        //     if ((int8_t)sm[9] < 0) { Time t(0.0f); StateMachine_Tick(sm,t,0,0); }
        //     i = node->slot08(node, sm[9], &req);
        //     if (i >= 0) { sm[0xb] = i; sm[0xa] = req; }
        // == ControllerLive::RequestStateTransition, which is the same idiom minus the
        // nested pre-tick (it bails when sm[9] < 0 instead). That pre-tick cannot fire on
        // the path this branch actually takes: StateMachine::Tick writes sm+9 from the
        // node Step BEFORE dispatching the Enter slot, so sm+9 is >= 0 whenever a state's
        // enter handler is running. The one caller that could reach it with sm+9 < 0 is a
        // hand-driven node outside Tick, which this project has none of.
        //
        // The instant-build leg of this branch (instant != 0) is retail's start-town
        // completion path: gw+0xd0 bit 0 is set across the spawner's SetModel +
        // RegisterEntity bracket, StateMachineComponent's own OnAdded (slot 6,
        // 0x14012b660) ticks the SM inside that same OnAddedPass, and StateMachine::Tick
        // loops while a pending state is set — so 1 -> 8 -> 2 all completes before the
        // spawner clears the bit. State8Enter's instant branch is what snaps the meshes to
        // their final frame and stamps +0x48 = target. See StartTown_SpawnStructures and
        // docs/history/notes/BRIDGE_HIGHLIGHT_DIALOG_BRIEF.md.
        ::ControllerLive::RequestStateTransition(smNode, 8);
    }

    // ===========================================================================
    // WORKPLACE HAND-OFF — runs on BOTH legs of the branch above (moved out of the
    // spawn leg 2026-08-15).
    //
    // This block used to live inside `if (!haveAll && instant == 0)`, i.e. only when
    // the site had to gather materials. That is not where retail puts it: retail's
    // WorkPlaceComponent controller runs FUN_14012f520 ("enter Builder profession")
    // at construction START, unconditionally — it has nothing to do with materials.
    //
    // The consequence of the misplacement was total, not partial. An instant-built or
    // fully-stocked site never got WPC+0x48 = Builder, so at completion
    // WorkPlaceComponent_LeaveBuilderProfession bailed on its `key == 0` guard and
    // never assigned the building's NATURAL profession from the description (+0x20)
    // either. +0x48 stayed 0 for the entity's whole life, and WorkplaceUI_Startup
    // hides the entire workplace panel when +0x48 == 0 (faithful to retail
    // 0x140087080) — so no worker up/down spinner and no work-stopped icon on ANY
    // workplace: orchard, crop field, fishing dock, hunter lodge, gatherer hut.
    // Measured 2026-08-15: 23642 `[WorkPlaceUI] HIDDEN` lines, 0 shown, across 6
    // distinct entities; `[BuildTrace] ... profession 0x0 -> 0x0` on every site.
    // ===========================================================================
    // *** TWO CONVENTIONS SET WPC+0x50 IN THIS CODEBASE. READ BOTH BEFORE GREPPING. ***
    //   (a) WorkPlaceComponent_SetAccepting()  — workplacecomponent.cpp:503, the
    //       faithful Entity_impl_fc00 (0x14012fc00) port. Sets +0x50 AND drives the
    //       status-icon side effects (clears closed/disabled bits; raises the
    //       "no workers" warning when the roster is empty).
    //   (b) the `isAccepting = 1` FIELD WRITE just below — sets +0x50 only, no icon.
    // A `grep SetAccepting` CANNOT SEE (b). That blind spot cost a wrong root-cause
    // call on 2026-07-30 ("the only true-setter is starttown.cpp:394"). If you are
    // auditing who makes a workplace accepting, grep for `isAccepting` / `0x50` too.
    //
    // Retail does NOT hand-roll this: FUN_14012f520 ("enter Builder profession",
    // ported at destroycomponent_workerrelease.cpp:222 and registered) ends with
    // WorkPlaceUI_helper_fb00(...) then Entity_impl_fc00(wpc, 1) — i.e. the full
    // SetAccepting, icon included. So converting (b) to a real SetAccepting call is
    // justifiable on faithfulness grounds; but the *properly* faithful change is to
    // call the already-ported f520 here instead of hand-rolling a subset of it, which
    // also brings ee60's worker release, the profession re-key, the BST register and
    // SetMaxWorkers. That is a real behaviour change and wants its own gated task —
    // do not do it as a drive-by while touching this comment.
    // ---------------------------------------------------------------------------
    // Enable the site's WorkPlaceComponent so citizens actually gather for it.
    // GatherRawMaterials::Execute (0x1400ebe40) gates the whole haul on the site's
    // WorkPlaceComponent (0x1401c80e0) "accepting" flag (+0x50): when 0 it returns
    // immediately ("workplace disabled") and never searches for source storage. In
    // retail that flag is set by the WorkPlaceComponent's own live lifecycle; in the
    // standalone exe that component is inert, so +0x50 stays 0 and no construction
    // haul ever runs. Set it here (construction-start) via the SAME finder GRM uses,
    // reproducing the accepting state a live WorkPlaceComponent would hold. (The full
    // WorkPlaceComponent_SetAccepting also drives a status-icon warning; only the
    // gate-relevant +0x50 is reproduced here — the visual is deferred to the live WPC.)
    {
        WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        if (wpc != nullptr) {
            wpc->isAccepting = true;
            // 2c-B: mark the site's WorkPlaceComponent as a "Builder workplace" —
            // WPC+0x48 = Builder marker = *(*(gw+0xb0)+0x38). Retail's construction-start
            // handler FUN_14012f520 does this (the inert WPC controller never runs it in
            // standalone). This keeps the seated builders consistent: the employment tick's
            // Phase-1 housing-sync would RESET a citizen whose +0x1f0 (Builder) != its
            // workplace's required profession (+0x48), so +0x48 must read Builder.
            //
            // NOTE (legacy off-path rationale): registering the site into the
            // townMgr+0x100 profession BST makes BuildPathGraph (0x1400d70c0) produce
            // pairs -> ProcessPaths drains -> the employment allocator (0x1400d43b0) runs
            // its Phase-5B AssignHousing(null) churn over every idle laborer. That churn
            // was ORIGINALLY believed to deadlock the WPC Scene mesh-visual toggle, which
            // is why the off-path defers the SEAT to State8Enter (direct CitizenEmploy_
            // SeatWorkersAt against the site entity) instead of registering. That deadlock
            // is now neutralized by g_wpcRosterOnly (held across the allocator body, the
            // builder pool, and the profession spinner), so the NATURAL register path below
            // is default-on and verified safe: a 30s autostart run with a registered site
            // stays hang-free and the town-hall Professions panel shows the site's real
            // Builder capacity ("X of 3" via the CapProbe in builder_pool_driver.cpp — the
            // same WorkPlaceComponent/WorkComponent/CalcMaxWorkers path ProfessionUI_Slot8
            // and FindAvailableBuilding use). Seating stays deferred to State8Enter either
            // way: seating at construction start would convert the idle Laborers that HAUL
            // the raw materials into Builders before any hauling. Laborers haul; Builders build.
            ProfessionList* profReg = GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_professionList : nullptr;
            Profession* builderMarker = profReg ? profReg->field_0x38 : nullptr;
            if (builderMarker != nullptr)
                wpc->profession = builderMarker;
            // NATURAL builder flow (BANISHED_BUILDER_NATURAL): register the site into
            // the townMgr+0x100 profession BST keyed Builder — the step retail does at
            // WPC OnCreate (FUN_14012f520 → AddWorkplaceToProfessionTree 0x1400895a0).
            // This is what makes FindAvailableBuilding (0x14008d100) able to locate the
            // site, so the tick-driven Builder pool seats idle laborers here. Capacity
            // (WorkComponent+0x30) is computed lazily by FindAvailableBuilding via
            // CalcMaxWorkers; accepting (WPC+0x50) was set just above. The employment
            // allocator's AssignHousing churn (which townMgr+0x100 population activates)
            // is freeze-guarded by g_wpcRosterOnly inside CitizenAssignCitizens.
            if (townMgr) {
                TownRegistry_RegisterWorkplace(townMgr, wpc);
            }
        }
    }


    return 1;
}
