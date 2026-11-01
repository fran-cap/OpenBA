// src/game/Component/destroycomponent_states.cpp
// Phase 3 / upgrade path — the DestroyComponent controller state machine, i.e.
// what demolishes a building and (when DestroyComponent+0x60 holds an upgrade
// target template) replaces it with the new building's construction site.
//
// Retail mechanism (Ghidra, DestroyDescription_helper_dd00 0x14011dd00): a
// global 0xf8-stride state table rooted at DAT_1401d7ae0, dispatched by the
// controller subobject at DestroyComponent+0x28 (vtable_DestroyComponent_
// Controller 0x1401630d0). States are entity-global ids shared with the Build
// controller's table:
//
//   state 3 (begin demolition): enter 0x14011d7b0 — stop production, strip
//           status icons/mesh, branch -> 4 (normal, flag +0x50==0) or 6.
//   state 4 (haul-away):        enter 0x14011cfd0 — queue a ClearStorage work
//           (type 1) unless storage already empty / materials never delivered;
//           tick 0x14011cec0 — when storage drains and no reclaim job remains,
//           -> 5. transitions {5, 2}.
//   state 5 (build/replace):    enter 0x14011cc70 — half-materials refund into
//           +0x58 + queue a BuildStructure work (type 0x2001), or (nothing to
//           build from) scatter contents + mark entity for removal;
//           tick 0x14011c120 — ★ THE REPLACEMENT SPAWN ★: GATED on demolition
//           progress (+0x54, credited per builder work unit via vtable slot 15
//           0x14011daf0) reaching the +0x58 target; then capture the old
//           footprint/transform, release workers, mark old entity for removal,
//           clone the target template (ComponentEntity_CopyCtor 0x1400c3720)
//           onto the old footprint and register it into the world
//           (EntityList::RegisterEntity 0x1400c48a0, category from
//           template+0x20). transitions {0, 2, 8, 7}.
//   state 6 (collapse/rubble):  enter 0x14011d4d0, update 0x14011d400, tick
//           0x14011d490 — PORTED in destroycomponent_state6.cpp (with the rubble
//           step 0x14011d5d0 and the material re-point 0x140125020). The arm fire
//           and tornado take: the model steps DOWN its damage-stage mesh variants
//           to the DestroyDescription+0x30 rubble floor as the +0x48 burn
//           countdown runs out. transitions {7}.
//   state 7 (terminal):         no handlers. transitions {0, 8, 5}.
//
// sub_4020 registers per-state EVENT handlers (node vtable slot 2 = PostEvent), which
// is how the DestroyUI panel's buttons reach this machine — NOT transition actions.
// State 7's two, 0x14011d2a0 (rebuild/reclaim) and 0x14011d1a0 (remove), are now
// PORTED below; they were null, and a null handler is a graceful no-op in the
// ControllerLive dispatcher, so every click on a destroyed building's panel was
// swallowed. Still DEFERRED (null): 0x14011d390 (state 6 event 2) and 0x14011bcc0
// (states 4 and 5, event 0).
//
// The live DFA cursor lives on the entity's StateMachineComponent; the pump
// that the UpgradeUI click handler calls (0x14011dfa0, ported disasm-faithfully
// as RemovalCalc_StateMachine_dfa0 in keystone_a_removalcalcpath.cpp) drives
// these handlers through the node linked at DestroyComponent+0x28.
//
// Handler placement (Ghidra-verified record layout for sub_3e50(state,A,B,C,D),
// 0xf8-stride records at DAT_1401d7ae0): A → record+0x08 (dispatched via
// controller 0x1400341a0), B → +0x20 (via 0x1400341d0, save/serialize),
// D → +0x18 (via 0x140034260), C is a dead parameter (never stored).
// Semantics follow the handler BODIES: every A-position body (d7b0/cfd0/cc70)
// is a one-shot entry action, every D-position body (cec0/c120/d490) is a
// repeated poll — so A = enter-once, D = per-frame tick in our dispatcher,
// the same placement the in-game-proven ResidenceComponent heat tick uses.
//
// STANDALONE deviations (each logged once at runtime):
//   * state-3 mesh/scene ops (Scene::RemoveObject, rubble-mesh swap) skipped —
//     visual-only; mirrors residencecomponent.cpp's SetMeshFlag skip.
// NOW PORTED: sub_c530 contents-scatter (0x14011c530) — reclaimed goods spill as
// RawMaterial ground piles over the footprint (was: dropped). See below.
//
// Default-ON via the default-on table (graduated 2026-07-25, upgrade path verified
// end-to-end); BANISHED_LIVE_DESTROY is now a kill-switch (=0). See
// destroycomponent.cpp.

#include <new>
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Work/ClearStorage.h"   // B.3.8 CB1a (Vtable)
#include "Work/Work.h"           // B.3.x W0: Work::GetPriority by name
#include "Work/BuildStructure.h"   // B4 X4-WORK: BuildStructure_CreateInPlaceFull returns the typed work
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/DestroyDescription.h"       // ENDGAME B.3.9 C1: 0x1401c7628 cell class
#include "Component/ModelDescription.h"         // ENDGAME B.3.9 C1: 0x1401c7a88 cell class
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (type-id fallback)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Component/controller_state_table.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "system/MemoryAllocator.h"
#include "Component/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Player/exe_bodies.h"
#include "Dialog/dialog_exe_helpers.h"
#include "Game/exe_bodies.h"

#include "Component/DestroyComponent.h"
#include "Component/BuildComponent.h"
#include "Component/BuildDescription.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StorageComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode (the by-id tree, B6 row 390)
#include "Game/PlayerToolManager.h"

// ============================================================
// Helpers, called directly
// ============================================================
//   finders: ComponentEntity::FindComponent<T> (the classes read from the type-id cell
//   each retail instance loads):
//     0x140003050 WorkComponent (0x1401c8070)        0x14000d6c0 BuildComponent (0x1401c7008)
//     0x140002e10 WorkPlaceComponent (0x1401c80e0)   0x140004740 MultiObjectComponent (0x1401c7ac0)
//     0x140004680 MapComponent (0x1401c79e0)         0x140003f60 ModelComponent (0x1401c7a50)
//     0x140002f90 StatusIconComponent (0x1401c7e08)  0x140006270 StorageComponent (0x1401c7e78)
//   0x14012c3f0 mesh state flag  CitizenComp_FUN_14012c3f0
//   0x14012ead0 / 0x14012eb50    WorkComponent_CheckWorkSlot / _RegisterWorkSlot
//   0x14012ec60 CitizenAI_WorkScheduler_Post
//   0x14012f670 WorkPlaceComponent_LeaveBuilderProfession, 0x14012f520 WorkPlaceComponent::EnterBuilderProfession
//   0x140103480 trade notify     NomadComp_FUN_140103480
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0
//   0x1400c3720 ComponentEntity_CtorFromDescription, 0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x140003870 ToolTile_SetTransformFromYaw_3870, 0x1401253d0 ActCNR_NR_SetVariant_1253d0
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call site below.
// WARNING: THE THIRD ARG OF 0x140003870 IS A FLOAT. It reads it from XMM2 (`movaps xmm6,xmm2`
// at +0x14) — the Microsoft x64 float slot, NOT R8D. Declaring it `int` put the value in
// R8D and left XMM2 holding stale garbage, so a rebuilt building came out at an arbitrary
// yaw (user-reported on a storage barn, 2026-08-23; the spawn trace showed
// yaw=1119092736 = 0x42B40000 = 90.0f — an obvious float bit pattern read as an int).
// Ghidra types it `undefined4` and passes the raw 4 bytes, which hides the mistake.
// Inverse of [reference_int_arg_typed_as_float_hides_at_index_zero].

// starttown.cpp — world-registration (region-map cells + pick mesh) for a building
// entity created at runtime; retail gets both from RegisterEntity's add-broadcast.
extern "C" void StartTown_RegisterRuntimeBuilding(ComponentEntity* entity);

// buildcomponent_state1.cpp — the construction-demo site/owner (standalone test scaffold).
extern "C" void* g_pConstructionDemoSite;
extern "C" ComponentEntity* g_pConstructionDemoEntity;

// Drop the construction-demo pointers when the building being retired owns them; they
// would otherwise dangle at a freed BuildComponent, and a reused heap block landing on
// the same address would compare equal to it in the construction force harness. The
// spawn path retargets them to the replacement immediately afterwards.
static void Destroy_ReleaseDemoSite(BuildComponent* buildComp) {
    if (buildComp && g_pConstructionDemoSite == buildComp) {
        g_pConstructionDemoSite   = nullptr;
        g_pConstructionDemoEntity = nullptr;
    }
}

// keystone_a_removalcalcpath.cpp — RemovalCalc_helper_8d50, the disasm-verified
// work-manager AVL lookup (walks the tree at param_1+0x30 for payload key == param_3).
// buildstructure.cpp — BuildStructure_CreateInPlaceFull, the full retail BuildStructure
// ctor (0x1400e59c0 port).

// ============================================================
// Worker release / builder re-seat
// ============================================================
// The WorkPlaceComponent half of a demolition: state-4 enter re-seats the workplace onto
// the BUILDER profession (FUN_14012f520) and the state-5 tick takes it back off before the
// old entity dies (FUN_14012f670). Both are ported (see
// destroycomponent_workerrelease.cpp and workplacecomponent.cpp).

// Every finder is ComponentEntity::FindComponent<T>. The tree answered null for a null
// entity (its old FindOn helper tested it); retail has no such test. Kept (B9).
template <class T>
static T* FindOn(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<T>();
}

// ============================================================
// DestroyDescription_helper_sub_da80 (0x14011da80) — storage-empty test.
// !storage || StorageComponent_QueryCount(storage) == 0.
// ============================================================
bool DestroyComponent::StorageEmpty() {
    if (!_entity) return true;
    // Ghidra-verified: retail da80 calls FUN_140006270 (StorageComponent
    // finder, DAT_1401c7e78) — NOT the ConsumeProduce finder.
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();
    if (!storage) return true;
    return storage->QueryCount() == 0;
}

// ============================================================
// DestroyDescription_helper_ea20 (0x14012ea20) — reclaim-job-pending test.
// Read in B6 (rows 390 / 391), 0x14012ea20-0x14012ea8e: 0x14012ea29 `CALL 0x14012ead0`
// (WorkComponent::IsWorkPosted), 0 when it fails; then the scheduler's by-id tree
// (GameUpdateLoop +0x118 -> trees[0] at +0x30) walked by the yard's work id (+0x28)
// (0x14012ea50-0x14012ea67: key < node -> +0x18, node < key -> +0x20); on the hit, CheckPtr
// the node's +0x08 Work and tail-jump its slot 11 (`JMP [RAX+0x58]`, GetPriority; AL kept).
// No test of the yard, the singleton, the scheduler or the Work's vptr.
// ============================================================
static char Destroy_ReclaimPending_ea20(WorkComponent* yard) {
    if (!yard->IsWorkPosted()) return 0;   // 0x14012ead0
    int key = yard->workId;
    const WorkSchedulerIdNode* node = GameUpdateLoop::_singleton->m_tutorial->trees[0].root;
    while (node) {
        if (key < node->key) {
            node = node->left;
        } else if (node->key < key) {
            node = node->right;
        } else {
            return static_cast<char>(node->work->GetPriority());   // slot 11
        }
    }
    return 0;
}

// ============================================================
// DestroyUI_helper_e600 (0x14011e600) — target differs from both the entity's
// current description AND the DestroyDescription's default target.
// DestroyDescription_helper_e5b0 (0x14011e5b0) — target == entity's current.
// ============================================================
bool DestroyComponent::TargetDiffers() {
    Description* desc = _description;
    DestroyDescription* typed = (desc && ::Object::IsDerivedFromNullSafe(desc, DestroyDescription::TypeId()))
                                    ? static_cast<DestroyDescription*>(desc) : nullptr;
    ComponentDescription* target = upgradeTarget;
    if (_entity && target == _entity->_description) return false;
    if (typed && target == typed->_replaceEntity) return false;
    return true;
}
bool DestroyComponent::TargetIsCurrent() {
    ComponentDescription* target = upgradeTarget;
    if (!_entity) return false;
    return target == _entity->_description;
}

// ============================================================
// Scatter-path helpers (0x14011c530 dependencies), resolved once.
//   0x14009f3e0  grid->world terrain query (returns out3, [2]=height). Registered
//                mapquery, exercised in-game by ~100 sites.
//   0x140129760  CreateRawMaterial_helper_9760 (PositionEntity) — places the
//                cloned pile on a cell with a yaw. Registered (reg_action_rawmat).
//   0x1401c7cb8  RawMaterialDescription type-id; 0x1401c7040 BuildDescription.
// .rdata float constants (exe values verified via read_memory):
//   DAT_140172114 = 0.5f  (cell-centre offset), DAT_1401720fc = 0.2f (max tile
//   height for a valid drop), DAT_140172204 = 90.0f (RandTol yaw scale).
// ============================================================
// 0x14009f3e0 is Reimpl_MapGetTerrainPos and 0x140129760 ActRm_PositionEntity_9760,
// both called directly.

// Find the RawMaterialDescription in a resource TEMPLATE's component-description
// array via GetTypeId (vtable+0x10). Its +0x48 caps each dropped pile at
// 100/_weight (retail c530). Raw pointers, not IBPs — no CheckPtr (matches
// FUN_140088f90).
static RawMaterialDescription* FindRawMatDescInTemplate(ComponentDescription* tmpl) {
    uint32_t tid = RawMaterialDescription::TypeId();
    int n = tmpl->_subDescCount;
    Description** base = tmpl->_subDescs;
    for (int i = 0; i < n; i++) {
        Description* c = base[i];
        uint32_t t = c->Type();   // slot 2
        if (t == tid)
            return (::Object::IsDerivedFromNullSafe(c, tid)) ? static_cast<RawMaterialDescription*>(c) : nullptr;
    }
    return nullptr;
}

// ============================================================
// sub_c530 (0x14011c530) — scatter reclaimed contents as RawMaterial ground piles.
// Faithful port. Two-phase:
//   Phase 1 builds a {template, count} list. If the building was only PARTIALLY
//   built (BuildComponent delivered < required), spill the actual Storage stacks
//   ((count-committed)/(delivered?2:1)); otherwise refund the recipe materials
//   (recipe.count / (comp+0x50 ? 4 : 2)) — the "recover half your resources" spill.
//   Phase 2 walks the footprint tiles (width×depth from baseX,baseY), and on each
//   empty (cell+0==0) walkable (cell+0x29==2) tile whose queried terrain height is
//   in [0, 0.2), clones the resource template, positions it (9760) with a random
//   yaw, sets amount(+0x28)/clears +0x30, and registers it into the world. The
//   placement cursor (col,row) persists across resource types. Overflow beyond the
//   footprint (row reaches depth) is dropped, exactly as retail.
// The register category is read from recipe[pairIndex]->+0x20 (retail indexes the
// build recipe by the scatter-pair index; benign for the ordered partial-build case).
// ============================================================
void DestroyComponent::ScatterContents(int baseX, int baseY, int width, int depth) {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    // BuildComponent + its BuildDescription (+0x18). Scatter is only reached with
    // comp+0x51 set, i.e. the build comp existed — guard defensively regardless.
    BuildComponent* buildComp = FindOn<BuildComponent>(entity);
    if (!buildComp) return;
    Description* rawDesc = buildComp->_description;
    if (!rawDesc) return;
    // Retail narrows the desc to BuildDescription (0x1401c7040) but then derefs
    // it unconditionally (required@+0x30, recipe@+0x20/+0x28) — the invariant is that
    // a BuildComponent always carries a BuildDescription. We trust the pointer (the
    // derive check would only null it, which retail never survives).
    BuildDescription* desc = static_cast<BuildDescription*>(rawDesc);

    int delivered = buildComp->laborProgress;
    int required  = desc->_i30 * buildComp->costMultiplier;
    BuildCost* recipeBase = desc->_costs;

    // ---- Phase 1: scatter list ----
    struct Pair { ComponentDescription* tmpl; int count; };
    std::vector<Pair> list;

    if (delivered < required) {
        // Partially built: spill the real Storage contents (StorageComponent::Item,
        // 0x10 bytes: resource, count, reserve).
        StorageComponent* storage = entity->FindComponent<StorageComponent>();
        if (storage) {
            int nStacks = storage->storageSlots._size;
            const StorageComponent::Item* stacks = storage->storageSlots._data;
            int divisor = (delivered > 0) ? 2 : 1;
            for (int i = 0; i < nStacks; i++) {
                const StorageComponent::Item& s = stacks[i];
                int f8 = s._count;
                int fc = s._reserve;
                list.push_back({s._resource, (f8 - fc) / divisor});
            }
        }
    } else {
        // Fully built: refund half (or a quarter, when comp+0x50) of the recipe.
        int divisor = burning ? 4 : 2;
        int nRecipe = desc->_costCount;
        for (int i = 0; i < nRecipe; i++) {
            list.push_back({recipeBase[i].material, recipeBase[i].quantity / divisor});
        }
    }
    if (list.empty()) return;

    // ---- Phase 2: place piles on empty walkable footprint tiles ----
    Map* grid = gw->m_game;   // +0xf8
    if (!grid) return;
    int gw_width = grid->size;
    if (gw_width <= 0) return;
    MapCell* cellArr = grid->cells.GetData();
    System::Random* random = gw->m_random;
    EntityList* entityList = gw->m_entityList;

    const float kHalfCell  = 0.5f;   // DAT_140172114
    const float kMaxHeight = 0.2f;   // DAT_1401720fc
    const float kYawScale  = 90.0f;  // DAT_140172204

    int placed = 0;
    int col = 0;   // local_148 — persists across resource types
    int row = 0;   // local_128

    for (size_t pi = 0; pi < list.size(); pi++) {
        ComponentDescription* tmpl = list[pi].tmpl;
        int remaining = list[pi].count;
        if (remaining <= 0) continue;

        int rowWorld = baseY + row;
        do {
            int cell = rowWorld * gw_width + baseX + col;
            float centre = static_cast<float>(gw_width >> 1) - kHalfCell;
            float in2[3] = { static_cast<float>(cell % gw_width) - centre,
                             static_cast<float>(cell / gw_width) - centre, 0.0f };
            float w0[3] = {0, 0, 0}, w1[3] = {0, 0, 0};
            grid->GetTerrainPos(w0, in2, 0, nullptr);
            grid->GetTerrainPos(w1, w0, 0, nullptr);
            const MapCell* cp = &cellArr[static_cast<uintptr_t>(cell)];
            bool valid = (cp->blocker == nullptr) &&
                         (static_cast<int8_t>(cp->terrainType) == 2) &&
                         (0.0f <= w1[2]) && (w1[2] < kMaxHeight);
            if (valid) {
                RawMaterialDescription* rmDesc = FindRawMatDescInTemplate(tmpl);
                // Retail divides with no test of the desc or its weight: 0x14011ca59
                // `MOV EAX,0x64; CDQ; IDIV dword ptr [RCX+0x48]`, RCX the CMOVNZ result
                // of the template scan or 0. Then CMOVG clamps to what is left.
                int per = 100 / rmDesc->_weight;
                if (per > remaining) per = remaining;

                ComponentEntity* mem = (::System::MemoryAllocator::_singleton)
                    ? static_cast<ComponentEntity*>(::System::MemoryAllocator::MemAlloc(0x48, 8)) : nullptr;
                ComponentEntity* newEntity = mem
                    ? ::new (mem) ComponentEntity(tmpl)
                    : nullptr;
                if (newEntity) {
                    ::Object::InternalPtr<ComponentEntity> entIBP(newEntity);

                    // RawMaterialComponent on the new entity → position + amount.
                    RawMaterialComponent* rm = newEntity->FindComponent<RawMaterialComponent>();   // 0x1400223c0
                    if (rm) {
                        int yaw = random ? random->RandTol(2) : 0;
                        rm->PositionRawMaterial(cell, static_cast<float>(yaw) * kYawScale);   // 0x140129760
                        rm->quantity = per;
                        rm->profession = nullptr;
                    }

                    // Register into the world with the recipe category.
                    int category = 0;
                    if (recipeBase) {
                        ComponentDescription* catTmpl = recipeBase[pi].material;
                        if (catTmpl) category = catTmpl->_category;
                    }
                    ::Object::InternalPtr<ComponentEntity> regIBP = entIBP;
                    if (entityList) entityList->RegisterEntity(&regIBP, category);

                    remaining -= per;
                    placed++;
                }
            }

            if (++col >= width) {
                col = 0;
                row++;
                rowWorld++;
            }
        } while (row != depth && remaining > 0);
    }

}

// Mark the owner entity for removal + push onto the EntityList removal drain —
// the exact guard+push idiom of clearresourcecomponent_states.cpp (the 0x400
// bit keeps sibling controllers from double-pushing the same entity).
static void Destroy_MarkAndPushRemoval(ComponentEntity* entity) {
    if (!entity) return;
    if (GameUpdateLoop::_singleton == nullptr) return;
    // GameUpdateLoop::m_entityList (+0xe8): the removal array is its removalList (+0xd0).
    EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;
    if (!entityList) return;
    if (((entity->_entityFlags >> 10) & 1) != 0) return;
    entity->MarkForRemoval();
    entityList->removalList
        .SetSize(entityList->removalList._size + 1, entity);
}

// BuildComponent material math: delivered (+0x48), required = the BuildDescription's
// +0x30 * costMultiplier (+0x4c).
static int Yard_Delivered(BuildComponent* prod) {
    return prod ? prod->laborProgress : 0;
}
static int Yard_Required(BuildComponent* prod) {
    if (!prod) return 0;
    Description* desc = prod->_description;
    if (!desc) return 0;
    return static_cast<BuildDescription*>(desc)->_i30 * prod->costMultiplier;
}

// ============================================================
// State 3 enter — 0x14011d7b0 (begin demolition).
// ============================================================
char DestroyComponent::State3Enter(StateMachine* sm) {
    progress = 0;   // reset progress

    // Stop pending production work on the yard (FUN_14012ead0/FUN_14012eb50).
    WorkComponent* yard = FindOn<WorkComponent>(_entity.Get());
    if (yard && yard->IsWorkPosted())
        yard->CancelWork();

    // Strip every status icon of the building (0x14012c2a0; retail tests the component for null first).
    StatusIconComponent* icon = FindOn<StatusIconComponent>(_entity.Get());
    if (icon) icon->ClearAllFlags();

    int nextState = !burning ? 4 : 6;
    ::ControllerLive::RequestStateTransition(sm, nextState);

    // Not burning: the "being demolished or upgraded" icon (BuildingIconSpriteSheet BuildDestroy = flag 5) goes on after the state
    // request (0x14011d7b0 sets bit 0x20 and runs the SetFlag body inlined; a burning building goes to state 6 with no icon).
    // Cancelling the demolition clears it again (CancelDemolition, SetFlag(5, 0)).
    if (!burning && icon) icon->SetFlag(5, 1);

    // RETURN 0, NOT 1 — this is the state-machine ENTER WALK's stop bit, not a
    // success flag. StateMachine::Tick walks tail->parent through EVERY controller
    // node while handlers return 0 and STOPS at the first non-zero. Retail
    // 0x14011d7b0 zeroes AL on both exits (`return uVar3 & 0xffffffffffffff00`, and
    // a literal `return 0` on the +0x50 != 0 branch), so the walk continues past
    // DestroyComponent to the entity's other controllers.
    //
    // Returning 1 here stopped that walk on the destroy node, so
    // StorageLocationComponent's state-3 enter (StorageLoc_State3Enter ->
    // Array<T>::Remove on townMgr+0x00) never ran for a demolished storage
    // building. Measured on tutorial 1 step 66 ("remove the starting cart"):
    //   * the cart stayed in the town storage-location list forever, so
    //     TutorialGoalCount mode=4 read count=3 against target=2 and the step could
    //     never complete (the goal sets no _greaterEqual, so the compare is EQUALITY);
    //   * and once the cart's StorageComponent was torn down, the stale list entry
    //     made CitizenBehavior_CountResources deref a null storage IBP —
    //     "READ of 0x0000000000000030", crashing the next sim tick.
    // One cause, both symptoms.
    return 0;
}

// ============================================================
// State 4 enter — 0x14011cfd0 (queue the haul-away).
// ============================================================
char DestroyComponent::State4Enter(StateMachine* sm) {
    ComponentEntity* entity = _entity.Get();

    // Re-seat the workplace onto the BUILDER profession (retail FUN_14012f520 on the 2e10
    // comp) — ported in destroycomponent_workerrelease.cpp.
    WorkPlaceComponent* wp = FindOn<WorkPlaceComponent>(entity);
    if (wp) wp->EnterBuilderProfession();

    BuildComponent* prod = FindOn<BuildComponent>(entity);
    if (prod) hasBuildComponent = true;

    // Storage already empty, or build materials never fully delivered
    // (partially-built building has nothing to haul) -> straight to state 5.
    if (StorageEmpty() ||
        (prod && Yard_Delivered(prod) < Yard_Required(prod))) {
        ::ControllerLive::RequestStateTransition(sm, 5);
        // Retail 0x14011cfd0 reaches its single `return uVar3 & 0xffffffffffffff00`
        // from this branch too (LAB_14011d139 sets uVar3 = 0 first) — AL is zero, so
        // the enter walk continues to the sibling controllers. Same stop-bit rule as
        // state 3 above.
        return 0;
    }

    // Queue a ClearStorage work (type 1) on the production yard. Faithful ctor
    // path: Work_Ctor25 base bind (field_10 = entity) + ClearStorage vtable +
    // zeroed tail fields (ClearStorage_Ctor 0x1400e74b0 shape).
    WorkComponent* yard = FindOn<WorkComponent>(entity);
    if (yard && ::System::MemoryAllocator::_singleton) {
        void* mem = ::System::MemoryAllocator::MemAlloc(0x68, 8);
        if (mem) {
            // ENDGAME B.3.9 OBJ: the real class (Work_Ctor25 + the table +
            // four zero qwords), not the `??0Internal` door + hand stamp.
            ComponentEntity* entityTmp = entity;
            ClearStorage* work = ::new (mem) ClearStorage(&entityTmp);
            ::Object::InternalPtr<Work> workIBP(work);
            yard->PostWork(1, 0, &workIBP);
        }
    }
    // Retail 0x14011cfd0 ends `return uVar3 & 0xffffffffffffff00` — AL zeroed, walk
    // continues. See the state-3 note above for what a stray 1 costs.
    return 0;
}

// ============================================================
// State 4 tick — 0x14011cec0 (per-frame: storage drained -> state 5).
// ============================================================
char DestroyComponent::State4Tick(StateMachine* sm) {
    if (!StorageEmpty()) return 1;
    WorkComponent* yard = FindOn<WorkComponent>(_entity.Get());
    if (!Destroy_ReclaimPending_ea20(yard)) {
        ::ControllerLive::RequestStateTransition(sm, 5);
        if (yard) yard->CancelWork();
    }
    return 1;
}

// ============================================================
// State 5 enter — 0x14011cc70 (refund + queue BuildStructure, or finish up).
// ============================================================
char DestroyComponent::State5Enter() {
    ComponentEntity* entity = _entity.Get();
    BuildComponent* prod = FindOn<BuildComponent>(entity);

    // Diagnostic: the refund+BuildStructure branch (which sets the +0x58 progress
    // target that gates the replacement spawn) fires ONLY when the building has a
    // live BuildComponent (d6c0) with Delivered(+0x48) != 0. When it doesn't, we
    // fall to the ELSE branch: scatter + immediate mark-for-removal, NO spawn — the
    // "building just vanishes" symptom. Log the decision inputs so a live upgrade
    // shows exactly why (prod null vs Delivered==0).

    if (prod && Yard_Delivered(prod) != 0) {
        // Half-materials refund into +0x58.
        MultiObjectComponent* yard0 = FindOn<MultiObjectComponent>(entity);
        int mult = yard0 ? static_cast<int>(yard0->multiCount) : 1;
        Description* desc = prod->_description;
        int base = desc ? static_cast<BuildDescription*>(desc)->_i30 : 0;
        int recovered = (base * mult) / 2;
        if (recovered == 0) recovered = 1;
        progressTarget = recovered;

        // Queue the BuildStructure work (type 0x2001) via the full retail ctor.
        WorkComponent* yard = FindOn<WorkComponent>(entity);
        if (yard && ::System::MemoryAllocator::_singleton) {
            void* mem = ::System::MemoryAllocator::MemAlloc(0x70, 8);
            if (mem) {
                ComponentEntity* entityIBP = entity;
                Object::InternalPtr<Work> workIBP(new (mem) BuildStructure(&entityIBP, 1));   // 0x1400e59c0
                yard->PostWork(0x2001, 0, &workIBP);
            }
        }
    } else {
        // Nothing recoverable: scatter old contents (over the footprint) + notify
        // trade + mark the old entity for removal.
        if (hasBuildComponent) {
            MapComponent* fp = FindOn<MapComponent>(entity);
            // Retail loads the footprint with MOVSX: signed shorts.
            if (fp)
                ScatterContents(static_cast<int16_t>(fp->tileX0), static_cast<int16_t>(fp->tileY0),
                                static_cast<int16_t>(fp->tileX1), static_cast<int16_t>(fp->tileY1));
        }
        if (GameUpdateLoop::_singleton != nullptr) {
            // +0x120 (the tree's "trade manager"; GameUpdateLoop names it m_toolManager).
            PlayerToolManager* tradeMgr = GameUpdateLoop::_singleton->m_toolManager;
            ComponentEntity* entityIBP = entity;
            if (tradeMgr) tradeMgr->OnEntityRemoved(&entityIBP);   // 0x140103480
        }
        Destroy_ReleaseDemoSite(prod);   // nothing replaces it on this branch
        Destroy_MarkAndPushRemoval(entity);
    }
    return 1;
}

// ============================================================
// State 5 tick — 0x14011c120 ★ the replacement spawn ★.
// ============================================================
char DestroyComponent::State5Tick() {
    // 0) RETAIL GATE (0x14011c120 first branch): wait for the demolition work.
    // +0x54 = progress (credited one unit per completed builder PerformWork via
    // vtable slot 15 / DestroyComponent_MsgReceive), +0x58 = the target the
    // state-5 enter computed. Until progress reaches the target the tick is a
    // pure no-op — no worker release, no trade notify, no mark-for-removal, no
    // spawn. Missing this gate raced the whole teardown on the first tick
    // (observed live 2026-07-20: instant mark-for-removal froze the open panel
    // via the 0x400 bit and the upgrade spawn logic misfired).
    //
    if (progress < progressTarget) {   // 0x14011c14f CMP / JL: both int32
        return 1;
    }

    ComponentEntity* entity = _entity.Get();
    if (!entity) return 1;

    // 0b) Retail: stop the yard's pending work now that demolition is complete
    // (FUN_140003050 -> FUN_14012eb50) before capturing the footprint.
    {
        WorkComponent* yard = FindOn<WorkComponent>(entity);
        if (yard) yard->CancelWork();
    }

    // 1) Capture the OLD footprint (grid cell + rotation) and world transform.
    // FUN_140004680 is the MAPCOMPONENT finder — the same component
    // DecalComponent_Draw reads its decal rect from (mapComp +0x28 minX, +0x2a minY,
    // +0x2c width, +0x2e height, +0x30 rotation).
    //
    // ⚠ `rot` is 32-BIT. Retail c120 reads `*(int*)(fp + 0x30)` and writes
    // `*(int*)(new + 0x30)`, and normalises with `+= 0x168` (360) — it is a rotation
    // in DEGREES, the ONLY one of these five fields that is not a short. This port
    // had it as int16_t on both sides: the low half landed correctly (0/90/180/270
    // all fit), so the rebuilt building's MESH looked right, but the high half was
    // left as whatever ComponentEntity_CopyCtor put there. DecalComponent_Draw reads
    // the field as a full uint32_t, so the decal got `garbage<<16 | rot` and painted
    // its foundation at the wrong orientation — the "rebuilt house's decal footprint
    // doesn't match" report (2026-08-23). Same class as the narrowed-store trap in
    // [reference_narrowed_8byte_store_desyncs_count_and_cap].
    // Retail loads the four shorts with MOVSX (0x14011c1a8..bb).
    MapComponent* fp = FindOn<MapComponent>(entity);
    int16_t sx = 0, sy = 0, sc = 0, sd = 0;
    int32_t rot = 0;
    if (fp) {
        sx = static_cast<int16_t>(fp->tileX0);
        sy = static_cast<int16_t>(fp->tileY0);
        sc = static_cast<int16_t>(fp->tileX1);
        sd = static_cast<int16_t>(fp->tileY1);
        rot = fp->extraField;
    }
    ModelComponent* pq = FindOn<ModelComponent>(entity);
    Math::Vector3 tf;   // (0, 0, 0)
    float yaw = 0.0f;
    uint16_t orient = 0;
    if (pq) {
        // ModelComponent's transform (+0x148): its translation (+0x10 / +0x14 / +0x18).
        tf.x = pq->transform.translation.x;
        tf.y = pq->transform.translation.y;
        tf.z = pq->transform.translation.z;
        orient = static_cast<uint16_t>(pq->meshGroup);
        yaw = pq->yaw;
    }

    // 2) Release workers from the old workplace, i.e. leave the BUILDER profession
    //    (FUN_14012f670 = workplacecomponent.cpp WorkPlaceComponent_LeaveBuilderProfession,
    //    called directly). Skipping it is what would leave the roster half-torn-down
    //    on the entity we are about to remove.
    WorkPlaceComponent* wp = FindOn<WorkPlaceComponent>(entity);
    if (wp) {
        wp->LeaveBuilderProfession();
    }

    // 3) Trade notify + mark the old entity for removal.
    if (GameUpdateLoop::_singleton != nullptr) {
        PlayerToolManager* tradeMgr = GameUpdateLoop::_singleton->m_toolManager;
        ComponentEntity* entityIBP = entity;
        if (tradeMgr) tradeMgr->OnEntityRemoved(&entityIBP);   // 0x140103480
    }
    Destroy_MarkAndPushRemoval(entity);

    BuildComponent* prod = FindOn<BuildComponent>(entity);
    ComponentDescription* target = upgradeTarget;
    const bool wasDemoSite =
        prod != nullptr && g_pConstructionDemoSite == prod;
    Destroy_ReleaseDemoSite(prod);

    if (target != nullptr) {
        bool ready = (prod && Yard_Delivered(prod) >= Yard_Required(prod)) || burning;
        if (ready) {
            // Retail 0x14011c3xx: scatter when e600 != 0 OR e5b0 != 0 (the
            // previous port had the e600 sense inverted).
            if (hasBuildComponent) {
                if (TargetDiffers() || TargetIsCurrent())
                    ScatterContents(sx, sy, sc, sd);
            }

            // >>> CREATE THE NEW BUILDING at the old footprint <<<
            if (::System::MemoryAllocator::_singleton &&
                GameUpdateLoop::_singleton != nullptr) {
                ComponentEntity* mem = static_cast<ComponentEntity*>(::System::MemoryAllocator::MemAlloc(0x48, 8));
                ComponentEntity* newEntity = mem ? ::new (mem) ComponentEntity(upgradeTarget) : nullptr;
                if (newEntity) {
                    ::Object::InternalPtr<ComponentEntity> hold(newEntity);

                    MapComponent* nfp = FindOn<MapComponent>(newEntity);
                    if (nfp) {
                        nfp->tileX0 = static_cast<uint16_t>(sx);
                        nfp->tileY0 = static_cast<uint16_t>(sy);
                        nfp->tileX1 = static_cast<uint16_t>(sc);
                        nfp->tileY1 = static_cast<uint16_t>(sd);
                        // 32-bit, like retail — see the capture-side note above.
                        int32_t r = rot;
                        while (r < 0) r += 360;   // retail: += 0x168 until >= 0
                        nfp->extraField = r;
                    }
                    ModelComponent* npq = FindOn<ModelComponent>(newEntity);
                    if (npq) {
                        npq->SetTransformFromYaw(&tf, yaw);   // 0x140003870
                        npq->SelectMesh(orient, 0);       // 0x1401253d0
                    }

                    EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;
                    int category = upgradeTarget->_category;
                    if (entityList) entityList->RegisterEntity(&hold, category);

                    // Standalone-only: give the replacement the two world registrations
                    // retail derives from RegisterEntity's add-broadcast — the region-map
                    // cell occupancy (click-pick + nav blocking + the armed removal clear)
                    // and the pick mesh. Our MapComponent slot 6 only paints the minimap
                    // and PickingComponent registers no mesh, so without this the new
                    // building is unclickable and leaves stale cells when it is removed.
                    StartTown_RegisterRuntimeBuilding(newEntity);

                    // Hand the construction-demo baton to the replacement when the
                    // building we just retired owned it (Destroy_ReleaseDemoSite cleared
                    // the pointers a moment ago), so BUILD_TRACE's demo logging follows
                    // the building that is now actually constructing.
                    if (wasDemoSite) {
                        BuildComponent* newProd = FindOn<BuildComponent>(newEntity);
                        g_pConstructionDemoSite   = newProd;
                        g_pConstructionDemoEntity = newEntity;
                    }

                    std::fprintf(stderr, "[DestroySM] UPGRADE SPAWN: new entity=%p from "
                                         "template=%p at cell(%d,%d) %dx%d rot=%d "
                                         "orient=%u yaw=%.1f cat=%d\n",
                                 (void*)newEntity, (void*)target,
                                 sx, sy, sc, sd, rot,
                                 static_cast<unsigned>(orient), static_cast<double>(yaw), category);
                    std::fflush(stderr);
                }
            }
            return 1;
        }
    }

    if (hasBuildComponent)
        ScatterContents(sx, sy, sc, sd);
    return 1;
}

// ============================================================
// State 6 (collapse/rubble) — PORTED in destroycomponent_state6.cpp.
// ============================================================
// enter 0x14011d4d0 / update 0x14011d400 / trans38-tick 0x14011d490, plus the
// rubble step 0x14011d5d0 and the material re-point 0x140125020. Gated at runtime
// by BANISHED_LIVE_DESTROY_COLLAPSE; see that file's header for the mechanism.

// ============================================================
// State-7 EVENT handlers — the destroyed-building panel's buttons.
// ============================================================
// DestroyUI's three buttons (destroyui.cpp Slot9: buttonReclaim 0x1771,
// buttonRebuild 0x1772, buttonDestroy 0x1773) all end in the same retail idiom
// (0x14011e400 / 0x14011e470 / 0x14011e4d0):
//
//   smc  = FindStateMachineComponent(entity)     // FUN_140003ea0
//   node = *(smc + 0x48)                          // the CURRENT node, re-pointed by Tick
//   node->vtable[2](node, smc + 0x48, (int)(int8)smc[0x51], eventId)
//
// i.e. they POST AN EVENT (node vtable slot 2 = PostEvent) on whatever node the
// state machine is currently sitting on — for a collapsed building that is the
// DestroyComponent's node, in state 7. eventId is 0 for reclaim/rebuild and 1 for
// destroy, which is exactly what DestroyDescription_helper_dd00 registers on state 7:
//   sub_4020(0, 0x14011d2a0)   <- rebuild / reclaim
//   sub_4020(1, 0x14011d1a0)   <- destroy / remove
//
// Both handlers were registered as NULL here, and ControllerLive::PostEvent treats a
// null handler as a graceful no-op — so every click on a destroyed building's Remove
// or Rebuild button was swallowed silently. That is the whole bug.
//
// Both bodies are near-identical: re-seat the workplace onto the Builder profession,
// then request state 5 (the rebuild/demolish worker state). They differ only in how
// they set the DestroyComponent's +0x51 "has a build yard" flag, and in whether they
// clear the queued upgrade target at +0x60.

// Shared prologue: retail's FUN_14012f520 on the 2e10 (WorkPlaceComponent) match.
static void Destroy_ReseatWorkplaceOnBuilder(ComponentEntity* entity) {
    WorkPlaceComponent* wp = FindOn<WorkPlaceComponent>(entity);
    if (!wp) return;
    wp->EnterBuilderProfession();
}

// State 7 event 0 — 0x14011d2a0. "Rebuild" (buttonRebuild) and "Reclaim"
// (buttonReclaim; 0x14011e400 posts the same event 0, then additionally calls the
// component's own vtable slot 4).
char DestroyComponent::State7Rebuild(StateMachine* sm) {
    ComponentEntity* entity = _entity.Get();

    Destroy_ReseatWorkplaceOnBuilder(entity);

    // Retail sets the flag unconditionally here (unlike state-4 enter / the destroy
    // event, which both gate it on a BuildComponent being present).
    hasBuildComponent = true;

    // +0x60 = the queued upgrade/replacement description. Re-pointing it at the
    // entity's OWN description (entity+0x28) is what makes this "rebuild what was
    // here" rather than "build the upgrade that was queued" — DestroyUI's
    // CanRebuild (0x14011e600) tests exactly this equality to decide whether the
    // panel shows the upgrade text.
    if (entity) {
        upgradeTarget = entity->_description;
    }

    // Retail runs a nested Tick(Time(0)) first when smc[0x51] < 0; ControllerLive's
    // RequestStateTransition bounds-checks the index instead, so a not-yet-settled
    // machine simply defers the request by one tick. See the note on that helper.
    ::ControllerLive::RequestStateTransition(sm, 5);
    return 1;
}

// State 7 event 1 — 0x14011d1a0. "Remove" (buttonDestroy).
char DestroyComponent::State7Remove(StateMachine* sm) {
    ComponentEntity* entity = _entity.Get();

    Destroy_ReseatWorkplaceOnBuilder(entity);

    // Same BuildComponent (d6c0) gate as state-4 enter: only a building with a build
    // yard gets the flag. Retail does NOT touch +0x60 here — the queued upgrade, if
    // any, survives the removal.
    BuildComponent* prod = FindOn<BuildComponent>(entity);
    if (prod) hasBuildComponent = true;

    ::ControllerLive::RequestStateTransition(sm, 5);
    return 1;
}

// ============================================================
// State 6 event 2 — 0x14011d390.
// ============================================================
// The whole body is the request-transition idiom (identical to
// ImmigrationDescription_helper_9ac0, 0x1400a9ac0) asking for state 7. State 6 is
// the burning/collapsing stage and 7 is the destroyed-building rest state, so this
// is "the fire is out — show the rubble panel". Nothing in the shipped UI posts
// event 2 today (the Remove/Rebuild buttons are state 7 / events 0-1), but the
// registration exists in dd00, so the handler does too. It reads no field of the
// component (RCX unused): it stays a free function (B1).
extern "C" char Destroy_State6Event2_ToRubble_d390(DestroyComponent* /*data*/, StateMachine* sm) {
    ::ControllerLive::RequestStateTransition(sm, 7);
    return 1;
}

// ============================================================
// States 4 AND 5, event 0 — 0x14011bcc0 (registered twice by dd00).
// ============================================================
// The CANCEL of an in-flight demolition/upgrade: states 4 (haul the storage away)
// and 5 (spawn the replacement) both accept it. It stops the site work and sends the
// machine back to whatever "not being destroyed" state fits the building's current
// model variant.
//
// Retail derefs the three finder results unguarded — a building sitting in state 4/5
// always carries a MeshInstance, a production yard and (for the variant math) a model
// component. Those derefs stay faithful.
char DestroyComponent::CancelDemolition(StateMachine* sm) {
    ComponentEntity* entity = _entity.Get();

    // Read first (retail order): does the building have a build yard at all?
    BuildComponent* build = FindOn<BuildComponent>(entity);

    // StatusIconComponent state flag 5 off (the "under construction/demolition"
    // overlay) and stop the production yard's worker.
    StatusIconComponent* mesh = FindOn<StatusIconComponent>(entity);
    mesh->SetFlag(5, 0);   // 0x14012c3f0
    WorkComponent* yard = FindOn<WorkComponent>(entity);
    yard->CancelWork();

    // comp+0x50 — the burning flag BeginBurning sets. Cancelling leaves nothing
    // standing, so the machine goes to the destroyed-building rest state instead.
    if (burning) {
        WorkPlaceComponent* wp = FindOn<WorkPlaceComponent>(entity);
        if (wp) wp->LeaveBuilderProfession();
        ::ControllerLive::RequestStateTransition(sm, 7);
        return 1;
    }

    int next;
    if (!build) {
        // No build yard: nothing was under construction, go straight back to state 2.
        next = 2;
    } else {
        // Pick the resume state from how far the model got. The variant source is the
        // MultiObjectComponent's (+0x48) first child entity when there is one — the
        // construction SITE — otherwise this entity itself.
        MultiObjectComponent* yard0 = FindOn<MultiObjectComponent>(entity);
        ComponentEntity* target = entity;
        if (yard0) target = yard0->children[0].Get();

        ModelComponent* model = FindOn<ModelComponent>(target);
        int16_t modelVariant = model->meshIndex;
        int16_t variantIdx   = model->meshGroup;
        Description* obj = model->_description;
        ModelDescription* modelDesc = (::Object::IsDerivedFromNullSafe(obj, ModelDescription::TypeId()))
                                          ? static_cast<ModelDescription*>(obj) : nullptr;

        if (modelVariant < 0) {
            next = 0;                       // no mesh chosen yet — back to the start
        } else {
            // group[variantIdx] is a ModelMeshGroup (0x18 bytes); +0x10 is its mesh
            // COUNT (+0x08 the mesh array destroycomponent_state6.cpp walks).
            const ModelMeshGroup* groups = modelDesc->_meshGroups;
            int meshCount = groups[variantIdx].meshCount;
            next = (modelVariant == meshCount - 1) ? 2 : 8;
        }
    }

    ::ControllerLive::RequestStateTransition(sm, next);

    // Release the workplace roster AFTER the request on this arm (retail order).
    WorkPlaceComponent* wp = FindOn<WorkPlaceComponent>(entity);
    if (wp) wp->LeaveBuilderProfession();

    return 1;
}

// ============================================================
// Dispatcher + state table for the DestroyComponent controller.
// Faithful graph from DestroyDescription_helper_dd00 (0x14011dd00). All four event
// handlers (0x14011bcc0/d390/d2a0/d1a0) are now ported and registered.
// ============================================================
// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<DestroyCtrlTag>, DestroyComponent.h).
using DestroyDispatch = ::ControllerLive::Dispatcher<DestroyCtrlTag>;

// The table holds one-line adapters: the dispatcher hands the handler
// `[node+0x18]` (the DestroyComponent), the state machine and, for the per-frame
// column, the elapsed Time.
static char DestroyState3Enter(DestroyComponent* c, StateMachine* sm)  { return c->State3Enter(sm); }   // 0x14011d7b0
static char DestroyState4Enter(DestroyComponent* c, StateMachine* sm)  { return c->State4Enter(sm); }   // 0x14011cfd0
static char DestroyState4Tick(DestroyComponent* c, StateMachine* sm, const System::Time* /*time*/)   { return c->State4Tick(sm); }    // 0x14011cec0
static char DestroyState5Enter(DestroyComponent* c, StateMachine* /*sm*/)            { return c->State5Enter(); }     // 0x14011cc70
static char DestroyState5Tick(DestroyComponent* c, StateMachine* /*sm*/, const System::Time* /*time*/)             { return c->State5Tick(); }      // 0x14011c120
static char DestroyState6Enter(DestroyComponent* c, StateMachine* /*sm*/)            { return c->State6Enter(); }     // 0x14011d4d0
static char DestroyState6Update(DestroyComponent* c, StateMachine* /*sm*/)           { return c->State6Update(); }    // 0x14011d400
static char DestroyState6Tick(DestroyComponent* c, StateMachine*, const System::Time* t) { return c->State6Tick(t); }   // 0x14011d490
static char DestroyState7Rebuild(DestroyComponent* c, StateMachine* sm) { return c->State7Rebuild(sm); }  // 0x14011d2a0
static char DestroyState7Remove(DestroyComponent* c, StateMachine* sm)  { return c->State7Remove(sm); }   // 0x14011d1a0
static char DestroyCancel(DestroyComponent* c, StateMachine* sm)        { return c->CancelDemolition(sm); }  // 0x14011bcc0

extern "C" void DestroyCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = DestroyDispatch::table;
    if (t.count != 0) return;   // idempotent

    TableRegisterState(t, 3, &DestroyState3Enter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddTransition(t, 6, nullptr);

    // sub_3e50(6, A=d4d0, B=d400, D=d490): A -> enter, B -> update, D -> trans38.
    TableRegisterState(t, 6, &DestroyState6Enter,
                       nullptr, &DestroyState6Tick,
                       &DestroyState6Update);
    TableAddTransition(t, 7, nullptr);
    // sub_4020(2, d390) — state 6's only event.
    TableAddEvent(t, 2, &Destroy_State6Event2_ToRubble_d390);

    TableRegisterState(t, 7, nullptr, nullptr, nullptr, nullptr);
    TableAddTransition(t, 0, nullptr);
    TableAddTransition(t, 8, nullptr);
    TableAddTransition(t, 5, nullptr);
    // sub_4020(0, d2a0) / sub_4020(1, d1a0) — the destroyed-building panel's
    // Rebuild/Reclaim and Remove buttons. Registration order matches dd00.
    TableAddEvent(t, 0, &DestroyState7Rebuild);
    TableAddEvent(t, 1, &DestroyState7Remove);

    TableRegisterState(t, 4, &DestroyState4Enter,
                       nullptr, &DestroyState4Tick,
                       nullptr);
    TableAddTransition(t, 5, nullptr);
    TableAddTransition(t, 2, nullptr);
    // sub_4020(0, bcc0) — cancel, registered on state 4 AND state 5.
    TableAddEvent(t, 0, &DestroyCancel);

    TableRegisterState(t, 5, &DestroyState5Enter,
                       nullptr, &DestroyState5Tick,
                       nullptr);
    TableAddTransition(t, 0, nullptr);
    TableAddTransition(t, 2, nullptr);
    TableAddTransition(t, 8, nullptr);
    TableAddTransition(t, 7, nullptr);
    TableAddEvent(t, 0, &DestroyCancel);
}
