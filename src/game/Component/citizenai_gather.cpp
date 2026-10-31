// src/game/Component/citizenai_gather.cpp
//
// Brick 3b handler wave 4 — faithful 1:1 ports of the two near-identical CitizenAIComponent
// resource-gathering state handlers + one private helper they share-by-shape:
//
//   0x14010bc90  CitizenAI_State_GatherResources   (state id 6)  -> dd30 + CheckWorkplaceValid
//                                                    + Storage/QueryCount/GetResourceCount
//                                                    + Position/2e20 + FindPath + ef40 transfer-loop
//                                                    + MeshInstance hide-render branch + 12c3f0
//   0x14010b3e0  CitizenAI_State_GatherResources2  (state id 7)  -> same, with the private
//                                                    Storage_GetResourceCount2 + a resource-slot
//                                                    selection loop (count matching RawMaterial
//                                                    slots, Random pick, transfer)
//   0x14012ab00  Storage_GetResourceCount2  (private to Gather2; closure = Entity_FindComponent_Storage
//                                            @0x140006270 (REG) + IsDerivedFrom only -> closure-safe)
//
// Both handlers make their state ENTERABLE by validating/walking-to the citizen workplace and
// (when a carry target is found) building the pickup path + transferring resources onto the carrier.
// ValidateAndEnterState (citizenai_candidatelist.cpp) dispatches them by ResolveAddr; returning 1
// commits the state. Transcribed from the Ghidra decompile + disassembly (FUN 0x14010bc90 /
// 0x14010b3e0), offset-by-offset.
//
// SELF CHAIN: param_1 = CitizenAIComponent. self = param_1+0x38 (sibling CitizenComponent IBP).
//   citizen = *self. citizen+0x238 = workplace entity, +0x10 = own entity IBP, +0x18 = description
//   (CitizenDescription, +0xd8 = gather-rate scalar), +0x278 = CharacterComponent (+0x68 =
//   ModelComponent transform, +0x158/+0x15c/+0x160 = world pos). Success epilogue writes the
//   action-state flags to *self (+0x54 |= 4, +0x30 = -1, +0xb0 = 0 then +0xb0 = stateNum) and
//   returns CONCAT71(cz>>8, 1). Cooldown-fail stamps Time(rate * mult) into the CitizenAIComponent
//   timer (Gather: param_1+0x70; Gather2: param_1+0x78) and returns ...&0xff00. The
//   found-but-empty branch (resource count 0) runs the MeshInstance "hide" render branch on the
//   workplace MeshInstance (mirrors citizenai_handlers2.cpp SeekHappiness hide-render).
//
// GameWorld singleton (g_pGameWorldSingleton @ DAT_1401c3dd8): +0x60 = Graphics::Scene,
//   +0xb0 = (sub @+0x30, +0x50 = town carry capacity), +0xd8 = Random, +0xf0 = entity-list pair,
//   +0xf8 = MapGrid (int* width at [0]).
//
// Registered exe sub-callees (resolved via ResolveAddr; bodies NOT duplicated):
//   0x14008dd30  CitizenAI_State_sub_dd30           (citizenai_findtarget.cpp)   [find carry target]
//   0x1401158d0  CitizenComponent_CheckWorkplaceValid (reg_component_gap_A.cpp)
//   0x140002f90  Entity_FindComponent_MeshInstance  (registered)
//   0x140002ed0  Entity_FindComponent_Position      (reg_work_base.cpp)
//   0x140006270  Entity_FindComponent_Storage       (registered)  [citizen+0x10 variant]
//   0x140008090  Entity_FindComponent_Storage       (reg_component_people.cpp)   [workplace variant]
//   0x140122e20  Entity_impl_2e20                    (reg_pf_road.cpp)  [resolve world pos]
//   0x140087fb0  Entity_impl_7fb0                    (reg_pf_road.cpp)  [workplace anchor cell]
//   0x14008ef40  Entity_impl_ef40                    (reg_component_gap_A.cpp)  [carrier slot iter]
//   0x1400c3470  FUN_1400c3470 NotifyEntityChanged   (reg_action_hunt)
//   0x140116540  FUN_140116540  town carry-capacity  (registered)
//   0x14012c3f0  FUN_14012c3f0  MeshInstance set-flag (registered)
//   0x14012cf10  StorageComponent_QueryCount         (registered)
//   0x14012a9d0  Storage_GetResourceCount            (reg_component_people.cpp)  [Gather]
//   0x14012ab00  Storage_GetResourceCount2           (THIS FILE)                 [Gather2]
//   0x1400bf790  Pathfinding_FindPath                (registered)
//   0x1400ca230  Resource_TransferToCarrier          (registered)
//   0x140115e60  CitizenAI_State_LongTermPlan        (another agent registers; null-guarded)
//
// KEY ABI (verified): Entity_impl_ef40 (0x14008ef40) IGNORES its first arg — real sig
//   (ignoredArg1, int64_t* out, void* entityIBP, int category). Mirror the decompile but arg1
//   is don't-care (the decompile passes the citizen there; harmless).
//
// SELF-CONTAINED-BRICK type-ids (UNFILLED 0xFFFFFFFF in standalone; resolved via
//   Object::GetTypeIdByName, every scan-result deref null-guarded):
//     DWORD_1401c7190  -> "CitizenDescription"     [cooldown-rate IsDerivedFrom on citizen+0x18]
//     DWORD_1401c7cb8  -> "RawMaterialDescription"  [resource-slot child-scan IsDerivedFrom]
//
// .rdata float consts (read from the exe):
//     DAT_140172234 = 300.0f  (found-but-empty cooldown multiplier)
//     DAT_1401721fc =  75.0f  (no-valid-workplace cooldown multiplier)
//
// The MeshInstance/AddObject/RemoveObject render sub-branch touches the still-INERT ModelComponent
// — every deref through it is null-guarded so it stays crash-safe (it is NOT exercised at
// world-start).


// graphics/Scene.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h, which breaks Volume.h's own
// `sizeof(Rectangle)` assert.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
// EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
// ext.item_priority (class C), the fuel pick and the finder tiers
#include "ext/item_priority/item_priority.h"
#include "Component/CitizenDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/InteractComponent.h"
#include "Component/ResidenceComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"   // GameUpdateLoop +0xF0 (the dd30 candidate lists)
#include "Game/Map.h"                 // GameUpdateLoop +0xF8
#include "Game/ProfessionList.h"
#include "Game/Profession.h"
#include "graphics/Mesh.h"
#include "Component/Component.h"   // B.3.x W0: Component slots by name
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "scene_watch.h"
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Action/PickupStoredRawMaterial.h"   // AddMaterial 0x1400ca230 on the path result
#include "Work/exe_bodies.h"

namespace {

// --- registered exe sub-callees: called by name (Component/Action/Work exe_bodies.h) ---
// NB: WorkAct_GatherRawMatCtor_bf790's 8th param is a 64-bit RawMaterialDescription* (0 =
// "any material"), NOT a 32-bit `long`. On Win64 `long` is 32-bit: a `long` there made the
// caller write only the low dword of the 8-byte stack slot, leaving the high dword as stack
// garbage, which the callee (exe Pathfinding_FindPath, which reads the full 8 bytes via
// CONCAT44) passed to the PickupStored ctor (ca520) and crashed once LIVE_STORAGE made
// storage non-empty. The declaration's int64_t writes the full 0.

static uint32_t s_tid_CitizenDescription     = 0;  // DWORD_1401c7190
static uint32_t s_tid_RawMaterialDescription = 0;  // DWORD_1401c7cb8

static const float kMultEmpty   = 300.0f;  // DAT_140172234
static const float kMultInvalid = 75.0f;   // DAT_1401721fc

static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;

    if (s_tid_CitizenDescription == 0)     s_tid_CitizenDescription     = CitizenDescription::TypeId();
    if (s_tid_RawMaterialDescription == 0) s_tid_RawMaterialDescription = RawMaterialDescription::TypeId();
    s_init = true;
}

// the citizen's ModelComponent (CharacterComponent +0x68).
// Retail CheckPtrs +0x278 and +0x68 and reads through both with no test.
static ModelComponent* CitizenModel(CitizenComponent* citizen) {
    CharacterComponent* ch = citizen->character.Get();
    return ch->modelComponent.Get();
}

// model world pos (transform.translation +0x158/+0x15c) -> clamped grid cell (w*row + col).
static uint32_t ModelCell(const ModelComponent* model, const Map* map) {
    int w = map->size;
    int col = static_cast<int>(model->transform.translation.x + static_cast<float>(w >> 1));
    int row = static_cast<int>(model->transform.translation.y + static_cast<float>(w >> 1));
    if (col < 0) col = 0; if (w <= col) col = w - 1;
    if (row < 0) row = 0; if (w <= row) row = w - 1;
    return static_cast<uint32_t>(w * row + col);
}

// cooldown rate scalar: ( (IsDerivedFrom(citizen->_description, CitizenDescription) ? desc : 0)->_fd8 ) * mult.
// Row 251 (B7 RES): retail reads [narrowed+0xd8] with no test (IsDerivedFrom, CMOVNZ over 0,
// in 0x14010bc90 / 0x14010b3e0's three cooldown tails); the tree's 0.0 fallback is gone.
static int64_t CooldownStamp(CitizenComponent* citizen, float mult) {
    Description* desc = citizen->_description;
    CitizenDescription* cd = desc->IsDerivedFrom(s_tid_CitizenDescription)
                                 ? static_cast<CitizenDescription*>(desc) : nullptr;
    float rate = cd->_fd8;
    System::Time stamp = {0};
    RtSystem_Time_Ctor(&stamp, rate * mult);
    return stamp._ticks;
}

// Scan an entity's child list (slots at entity+0x28, count at entity+0x30) for the first child
// whose vtable+0x10 type id == RawMaterialDescription, IsDerivedFrom-confirmed. Returns the child
// (or null). Mirrors the inner do/while in the decompile.
// (B4 P2 CAI-2: the "entity" is a material template, ComponentDescription +0x28 / +0x30.)
// Row 251 (B7 RES): no null template / null child test (retail calls [[child]+0x10] straight).
static RawMaterialDescription* FindRawMaterialChild(const ComponentDescription* entity) {
    int count = entity->_subDescCount;
    Description** slots = entity->_subDescs;
    for (int i = 0; i < count; ++i) {
        Description* child = slots[i];
        uint32_t tid = child->Type();   // slot 2
        if (tid == s_tid_RawMaterialDescription) {
            if (child->IsDerivedFrom(s_tid_RawMaterialDescription))
                return static_cast<RawMaterialDescription*>(child);
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace

// ============================================================
// 0x14012ab00 — ResidenceComponent::GetFuelCount (Gather2, HeatTick, HasSufficientResources2)
//   Walks the storage component's row entries (entity+0x10 -> Storage; rows at +0x28, count +0x30,
//   stride 0x10). For each row finds the RawMaterialDescription child; if its +0x5c bit6 set, adds
//   (row+8 - row+0xc) to the running count. Returns the total. Closure = Entity_FindComponent_Storage
//   + IsDerivedFrom only -> closure-safe.
// ============================================================
//   (B4 P2 CAI-2: RCX is the ResidenceComponent, only its +0x10 entity is read; the rows are
//    StorageComponent::Item.)
int ResidenceComponent::GetFuelCount() const {
    EnsureInit();
    // 0x140006270 (was its WorkBase_FindComponent_6270 forwarder)
    StorageComponent* stor = _entity->FindComponent<StorageComponent>();   // no test
    int total = 0;
    int count = stor->storageSlots._size;
    for (int i = 0; i < count; ++i) {
        StorageComponent::Item* rows = stor->storageSlots._data;
        ComponentDescription* row = rows[i]._resource;
        RawMaterialDescription* rm = FindRawMaterialChild(row);
        if ((rm->_flags >> 6 & 1) != 0) {   // 0x14012aba5: [rm+0x5c], no test
            total += rows[i]._count - rows[i]._reserve;
        }
    }
    return total;
}

// Gather (state 6) carrier transfer loop (decompile tail of the found!=0 branch). Walks the
// carrier-slot list produced by ef40, transferring while the citizen carry-capacity (capLeft)
// holds. ef40's entityIBP arg is &found (the carry-target, NOT the home); its first arg is
// IGNORED. Mirror the decompile's chaining: first call from the citizen, subsequent calls
// from the prior path-result entity (pathResult).
static void GatherTransferLoop(CitizenComponent* citizen, ::Object::InternalPtr<Action> pathResult,
                               ComponentEntity* found, int capLeft) {
    ComponentEntity* entIBP = found;         // &found is the ef40 entityIBP arg (RBP+0x6f)
    ComponentDescription* carrierSlot = nullptr;   // ef40 out (RBP+0x77 / local_res18)
    CitizenComp_Entity_impl_ef40(citizen, &carrierSlot, &entIBP, 0);
    while (carrierSlot != nullptr) {
        RawMaterialDescription* rm = FindRawMaterialChild(carrierSlot);
        int slotCost = rm->_weight;   // [rm+0x48], no test
        if (capLeft < slotCost) break;
        // 0x1400ca230 (was its ActChild_TransferToCarrier_ca230 forwarder): the path
        // result is the PickupStoredRawMaterial 0x1400bf790 built.
        static_cast<PickupStoredRawMaterial*>(pathResult.Get())->AddMaterial(carrierSlot, 1);
        capLeft -= slotCost;
        entIBP = found;
        CitizenComp_Entity_impl_ef40(pathResult.Get(), &carrierSlot, &entIBP, 0);   // arg 1 ignored; the tree passes pathResult
    }
}

// Gather2 (state 7) resource-slot SELECTION + transfer (decompile LAB_14010b92f .. LAB_14010baf1 +
// LAB_14010bb8a). `found` = the carry-target entity; pathResult = the FindPath result entity (the
// carrier handle for Resource_TransferToCarrier). Outer loop re-scans `found`'s Storage each pass:
//   pass 1 — count rows whose RawMaterialDescription child has +0x5c bit6 set AND (row+8 - row+0xc)
//            > 0; track a preferred index (last match whose rm+0x58 byte != 0), else stays -1.
//   if any match: if no preferred, Random-pick chosen = Rand(matchCount).
//   pass 2 — walk to the chosen-th match; transfer it if capLeft >= rm+0x48; deduct and re-loop.
// Returns when a pass yields no transfer. Every scan-deref null-guarded.
static void SelectAndTransfer(ComponentEntity* found, ::Object::InternalPtr<Action> pathResult, int capLeft) {
    int transfers = 0;

    for (;;) {
        // Storage(found) -> stor: 0x140006270 (was its WorkBase_FindComponent_6270 forwarder)
        StorageComponent* stor = found->FindComponent<StorageComponent>();   // no test

        int rowCount = stor->storageSlots._size;                         // local_60
        int chosenIdx = -1;                                     // local_res8 low
        int matchCount = 0;                                    // pvVar21

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
        // this fetch picks the fuel itself (the finder only named the storage), so item priority narrows it
        // here: only the rows of the best rank among the fuel the storage holds stay eligible. Tested once,
        // above the loops.
        const bool prio = Ext::ItemPriority::Active(Ext::ItemPriority::kFuel);
        int bestRank = 0x7fffffff;
        if (prio) {
            for (int cur = 0; cur < rowCount; ++cur) {
                StorageComponent::Item* rb = stor->storageSlots._data;
                RawMaterialDescription* rm = FindRawMaterialChild(rb[cur]._resource);
                if ((rm->_flags >> 6 & 1) == 0 || rb[cur]._count - rb[cur]._reserve <= 0) {
                    continue;
                }
                const int r = Ext::ItemPriority::RankOfMat(Ext::ItemPriority::kFuel, rm);
                if (r < bestRank) {
                    bestRank = r;
                }
            }
        }

        // pass 1: count matches + preferred index
        StorageComponent::Item* rowsBase = nullptr;  // local_res10 (= *(stor+0x28))
        for (int cur = 0; cur < rowCount; ++cur) {
            rowsBase = stor->storageSlots._data;
            ComponentDescription* child = rowsBase[cur]._resource;
            RawMaterialDescription* rm = FindRawMaterialChild(child);
            bool flagged = (rm->_flags >> 6 & 1) != 0;
            int avail = rowsBase[cur]._count - rowsBase[cur]._reserve;
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): only the best rank
            if (flagged && prio && !Ext::ItemPriority::Eligible(Ext::ItemPriority::kFuel, rm, bestRank)) {
                flagged = false;
            }
            if (flagged && avail > 0) {
                if (rm->_unknown58)
                    chosenIdx = matchCount;
                ++matchCount;
            }
        }

        if (matchCount == 0) return;
        if (chosenIdx < 0) {
            System::Random* rng = GameUpdateLoop::_singleton->m_random;   // [gw+0xd8], no test
            chosenIdx = (int)rng->Rand((uint32_t)matchCount);
        }

        // pass 2: find the chosenIdx-th match
        int matchCounter = 0;   // local_res10 low
        ComponentDescription* hitChild = nullptr;   // the row child to transfer (lVar5)
        int hitCursor = -1;
        for (int cur = 0; cur < rowCount; ++cur) {
            StorageComponent::Item* rb = stor->storageSlots._data;   // local_88
            ComponentDescription* child = rb[cur]._resource;
            RawMaterialDescription* rm = FindRawMaterialChild(child);
            bool flagged = (rm->_flags >> 6 & 1) != 0;
            int avail = rb[cur]._count - rb[cur]._reserve;
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): the same test as pass 1
            if (flagged && prio && !Ext::ItemPriority::Eligible(Ext::ItemPriority::kFuel, rm, bestRank)) {
                flagged = false;
            }
            if (flagged && avail > 0) {
                if (matchCounter == chosenIdx) { hitChild = child; hitCursor = cur; break; }
                ++matchCounter;
            }
        }

        if (hitChild == nullptr || hitCursor < 0) {
            return;   // LAB_14010bb8a: lVar5 == 0 -> done
        }

        // LAB_14010bb8a: resolve the chosen row child's RawMaterial cost, transfer if capLeft allows
        RawMaterialDescription* rm = FindRawMaterialChild(hitChild);
        int cost = rm->_weight;   // [rm+0x48], no test
        if (capLeft < cost) {
            return;
        }
        // 0x1400ca230 (was its ActChild_TransferToCarrier_ca230 forwarder)
        static_cast<PickupStoredRawMaterial*>(pathResult.Get())->AddMaterial(hitChild, 1);
        ++transfers;
        capLeft -= cost;
        // goto LAB_14010b92f: re-loop with reduced capacity
    }
}

// ============================================================
// 0x14010bc90 — CitizenAIComponent::State_GatherResources  (state id 6)
// ============================================================
uint64_t CitizenAIComponent::State_GatherResources() {
    EnsureInit();

    bool haveWorkplace = (citizen->home != nullptr);
    char valid = 0;
    if (haveWorkplace) {
        valid = citizen->CanReach(citizen->home.Get());
    }

    if (haveWorkplace && valid != 0) {
        // own-storage QueryCount: the citizen entity's StorageComponent -> QueryCount
        StorageComponent* own = citizen->_entity->FindComponent<StorageComponent>();
        int qc = own->QueryCount();   // CheckPtr'd, no test
        if (qc > 0) {
            // 0x140115e60 LongTermPlan returns nothing; its caller keeps only the low byte
            // (ValidateAndEnterState), so the upper bits of r carry no value.
            citizen->LongTermPlan();
            uint64_t r = 0;
            queuedState = 6;
            return (r & 0xffffffffffffff00ULL) | 1;
        }

        // compute the citizen grid cell
        ModelComponent* model = CitizenModel(citizen.Get());
        Map* mg = GameUpdateLoop::_singleton->m_game;   // +0xF8
        int w = mg->size;
        uint32_t cell = ModelCell(model, mg);

        // bGoal = home ? 7fb0(&home) : -1
        uint32_t bGoal;
        if (citizen->home == nullptr) {
            bGoal = 0xffffffff;
        } else {
            ComponentEntity* wp = citizen->home.Get();
            bGoal = EntityAnchorCell(&wp, nullptr);
        }

        // dd30 find carry target
        float dest[4] = {0, 0, 0, 0};   // local_70
        ComponentEntity* found = nullptr;          // local_res10
        ComponentDescription* outComp = nullptr;   // local_res18
        CitizenBehavior* listpair = GameUpdateLoop::_singleton->m_citizenList;
        listpair->FindBestTarget(&found, cell, bGoal, &outComp, dest, 0);
        (void)w;

        if (found == nullptr) {
            // ---- cooldown + status-icon branch ----
            int64_t stamp = CooldownStamp(citizen.Get(), kMultEmpty);
            timers[6]._ticks = stamp;
            citizen->happinessFlags |= 4;
            // 0x14010bef5..0x14010c05b: the home's residence food count (0x140008090 finder,
            // CALL 0x14012a9d0 at 0x14010bf33); at zero, StatusIconComponent::SetFlag(6, 1)
            // inlined (0x14010bf6e finder 0x140002f90, OR [icon+0xf0],0x40 at 0x14010bfa3).
            if (citizen->home != nullptr) {
                if (citizen->home->FindComponent<ResidenceComponent>()->GetFoodCount() == 0)
                    citizen->home->FindComponent<StatusIconComponent>()->SetFlag(6, 1);   // 0x14012c3f0
            }
            return static_cast<uint64_t>(stamp) & 0xffffffffffffff00ULL;
        }

        // ---- found != 0: build path + transfer ----
        // 1) StatusIcon set-flag(6) on the home
        if (citizen->home != nullptr) {
            StatusIconComponent* icon = citizen->home->FindComponent<StatusIconComponent>();
            icon->SetFlag(6, '\0');   // 0x14012c3f0 (was its CitizenComp_FUN_14012c3f0 forwarder)
        }

        // 2) InteractComponent(home) -> 2e20 -> destPos; FindPath
        InteractComponent* pp = citizen->home->FindComponent<InteractComponent>();
        Vector3Slot destPos;  // local_60
        pp->NodePosition(&destPos.v, 0, 0, nullptr);

        CitizenComponent* citizenVal = citizen.Get();                         // local_88
        ComponentEntity* destEntityIBP = citizenVal->home.Get();              // local_res18
        ::Object::InternalPtr<Action> pathResult;                        // local_res20
        WorkAct_GatherRawMatCtor_bf790(&pathResult, &citizenVal, -1,
                       &destEntityIBP, &destPos.v.x, &found,
                       dest, nullptr, -1, 0, 1, '\x01', 1, 0);

        // 3) capLeft = max( the profession list's field_0x30 carry amount, FUN_140116540(citizen) )
        int townCap = GameUpdateLoop::_singleton->m_professionList->field_0x30->priority;
        int citCap = citizen.Get()->FreeStorageCapacity();
        int capLeft = (townCap < citCap) ? citCap : townCap;

        // 4) transfer loop (ef40 walks carrier slots of `found`, the carry target)
        GatherTransferLoop(citizen.Get(), pathResult, found, capLeft);

        // 5) set citizen action-state flags + return 1
        citizen->statusFlags |= 4;
        citizen->field_0x30 = -1;
        citizen->field_0xB0 = 0;
        citizen->field_0xB0 = 1;
        return 1;  // CONCAT71(citizen >> 8, 1): the caller reads the low byte
    }

    // ---- no reachable home: cooldown(_fd8 * 75.0) -> timers[6] ----
    int64_t stamp = CooldownStamp(citizen.Get(), kMultInvalid);
    timers[6]._ticks = stamp;
    return static_cast<uint64_t>(stamp) & 0xffffffffffffff00ULL;
}

// ============================================================
// 0x14010b3e0 — CitizenAIComponent::State_GatherResources2  (state id 7)
// ============================================================
uint64_t CitizenAIComponent::State_GatherResources2() {
    EnsureInit();

    bool haveWorkplace = (citizen->home != nullptr);
    char valid = 0;
    if (haveWorkplace) {
        valid = citizen->CanReach(citizen->home.Get());
    }

    if (haveWorkplace && valid != 0) {
        // own-storage QueryCount
        StorageComponent* own = citizen->_entity->FindComponent<StorageComponent>();
        int qc = own->QueryCount();   // CheckPtr'd, no test
        if (qc > 0) {
            // 0x140115e60 LongTermPlan returns nothing; its caller keeps only the low byte
            // (ValidateAndEnterState), so the upper bits of r carry no value.
            citizen->LongTermPlan();
            uint64_t r = 0;
            queuedState = 7;
            return (r & 0xffffffffffffff00ULL) | 1;
        }

        // compute cell
        ModelComponent* model = CitizenModel(citizen.Get());
        Map* mg = GameUpdateLoop::_singleton->m_game;   // +0xF8
        int w = mg->size;
        uint32_t cell = ModelCell(model, mg);

        // bGoal = home ? 7fb0(&home) : -1
        uint32_t bGoal;
        if (citizen->home == nullptr) {
            bGoal = 0xffffffff;
        } else {
            ComponentEntity* wp = citizen->home.Get();
            bGoal = EntityAnchorCell(&wp, nullptr);
        }

        // dd30 find carry target (category arg = 1 here, vs 0 in Gather)
        float srcPos[4] = {0, 0, 0, 0};  // local_88
        ComponentEntity* found = nullptr;          // local_70
        ComponentDescription* outComp = nullptr;   // local_res10
        CitizenBehavior* listpair = GameUpdateLoop::_singleton->m_citizenList;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
        // with item priority active for Fuel (category 1) the finder runs once per rank, best first, so a
        // better-ranked fuel in a farther storage wins over a worse one in the nearest.
        Ext::ItemPriority::FindTiered(Ext::ItemPriority::kFuel, [&]() -> bool {
            found = nullptr;
            outComp = nullptr;
            listpair->FindBestTarget(&found, cell, bGoal, &outComp, srcPos, 1);
            return found != nullptr;
        });
        (void)w;

        if (found == nullptr) {
            // ---- cooldown + status-icon branch ----
            citizen->happinessFlags |= 4;
            int64_t stamp = CooldownStamp(citizen.Get(), kMultEmpty);
            timers[7]._ticks = stamp;
            // GatherResources2's twin of the block above: the fuel count (CALL 0x14012ab00 at
            // 0x14010b682); at zero, StatusIconComponent::SetFlag(7, 1) inlined (bit 0x80).
            if (citizen->home != nullptr) {
                if (citizen->home->FindComponent<ResidenceComponent>()->GetFuelCount() == 0)
                    citizen->home->FindComponent<StatusIconComponent>()->SetFlag(7, 1);   // 0x14012c3f0
            }
            return static_cast<uint64_t>(stamp) & 0xffffffffffffff00ULL;
        }

        // ---- found != 0: StatusIcon flag(7), InteractComponent+2e20, FindPath ----
        if (citizen->home != nullptr) {
            StatusIconComponent* icon = citizen->home->FindComponent<StatusIconComponent>();
            icon->SetFlag(7, '\0');   // 0x14012c3f0 (was its CitizenComp_FUN_14012c3f0 forwarder)
        }

        InteractComponent* pp = citizen->home->FindComponent<InteractComponent>();
        Vector3Slot destPos;  // local_98
        pp->NodePosition(&destPos.v, 0, 0, nullptr);

        CitizenComponent* citizenVal = citizen.Get();                         // local_res20
        ComponentEntity* destEntityIBP = citizenVal->home.Get();              // local_res18
        ::Object::InternalPtr<Action> pathResult;                        // local_78
        WorkAct_GatherRawMatCtor_bf790(&pathResult, &citizenVal, -1,
                       &destEntityIBP, &destPos.v.x, &found,
                       srcPos, nullptr, -1, 0, 1, '\x01', 1, 0);

        // capLeft = max( the profession list's field_0x30 carry amount, FUN_140116540(citizen) )
        int townCap = GameUpdateLoop::_singleton->m_professionList->field_0x30->priority;
        int citCap = citizen.Get()->FreeStorageCapacity();
        int capLeft = (townCap < citCap) ? citCap : townCap;
        // Distribution diagnostic (firewood-evenness): the per-trip take budget is
        // max(town default, citizen free carry) in WEIGHT units — degenerate values
        // here (garbage townCap from an unfilled gw+0xb0 chain, or a huge citizen
        // storage capacity) make single citizens drain the barn.
        { static int n = 0; if (n < 12) { ++n;
            fprintf(stderr, "[Gather2] budget: townCap=%d citCap=%d -> capLeft=%d\n",
                    townCap, citCap, capLeft);
            fflush(stderr); } }

        // Gather2's resource-slot SELECTION loop (decompile LAB_14010b92f .. LAB_14010baf1):
        //   pass 1 — count carrier rows whose RawMaterialDescription +0x5c bit6 set and have
        //            (row+8 - row+0xc) > 0; track a preferred index (rows whose +0x58 byte != 0).
        //   if any match: if the preferred index is unset, Random-pick one of the matches.
        //   pass 2 — walk to the chosen match and TransferToCarrier it.
        // Mirrors the decompile offset-by-offset; every scan-deref null-guarded. The carrier-slot
        // entity is found via ef40 (the loop's storage component is `found`'s Storage).
        SelectAndTransfer(found, pathResult, capLeft);

        // ---- set citizen action-state flags + return 1 ----
        citizen->statusFlags |= 4;
        citizen->field_0x30 = -1;
        citizen->field_0xB0 = 0;
        citizen->field_0xB0 = 2;
        return 1;  // CONCAT71(citizen >> 8, 1): the caller reads the low byte
    }

    // ---- no reachable home: cooldown(_fd8 * 75.0) -> timers[7] ----
    int64_t stamp = CooldownStamp(citizen.Get(), kMultInvalid);
    timers[7]._ticks = stamp;
    return static_cast<uint64_t>(stamp) & 0xffffffffffffff00ULL;
}

