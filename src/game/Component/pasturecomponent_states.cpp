// src/game/Component/pasturecomponent_states.cpp
// Animal-spawn wave / pasture lane — the PastureComponent CONTROLLER state machine.
//
// The pasture's behaviour is not in its own Update: PastureComponent is a
// ControllerComponent whose embedded state node (at comp+0x48, back-pointer at
// comp+0x60) is linked and ticked by the entity's StateMachineComponent. The state
// table is built once by PastureDescription_helper_81a0 (0x1401281a0):
//
//   sub_6940(2, helper_70c0, .., sub_67e0)   -> state 2  enter=70c0  perFrame=67e0
//   sub_6a60(3)                              ->   state 2 transitions -> {3}
//   sub_6940(3, helper_6e40, .., 0)          -> state 3  enter=6e40  (no perFrame)
//   sub_6a60(6) sub_6a60(4) sub_6a60(5)      ->   state 3 transitions -> {6,4,5}
//
// sub_6940 writes the record's +0x00 id, +0x08 fnA and +0x18 fnB; the node vtable's
// slot-3 dispatcher (0x140036bf0) tail-calls entry+0x08 and the slot-7 dispatcher
// (0x140036cb0) tail-calls entry+0x18 — i.e. fnA is ENTER and fnB is the PER-FRAME
// handler, exactly the enterHandler / trans38Handler fields of ControllerLive's
// StateTable. sub_6a60 appends {id,0} to the last record's transition list.
//
// The controller's GetDefaultState (primary vtable slot 16, 0x1400026c0) is
// `MOV EAX,2; RET` — the pasture's DEFAULT state is 2, the operational one, so a
// built pasture starts producing as soon as its state machine starts. State 3 is
// the teardown (release every animal back to the world's free-roaming pool and
// unregister the pasture).
//
// ⚠ Return-AL discipline: both ENTER handlers end `& 0xffffffffffffff00` (AL = 0,
// the "walk-stop" tail that lets sibling state nodes still run); the per-frame
// handler returns AL = 1. Ported verbatim — returning 1 from an enter here would
// starve every node behind the pasture.
//

#include "Component/controller_state_table.h"
#include "Component/PastureComponent.h"
#include "Component/AnimalComponent.h"
#include "Component/AutoPickupComponent.h"   // ENDGAME B.3.9 CB: 0x1401c3fb0 cell class
#include "Component/ComponentDescription.h"
#include "Component/InteractComponent.h"
#include "Component/LivestockComponent.h"
#include "Component/LivestockDescription.h"
#include "Component/MapComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/TrackerComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/WorkPlaceDescription.h"
#include "Game/PathfinderThread.h"   // SetCellTerrain (0x1400a6fe0)
#include "Action/PathSearch.h"       // RegionGraph::MarkCellDirty (0x1400a8c90)
#include "Entity/CitizenBehavior.h"   // the free-livestock list, the pasture registry, the town stock rows
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Game/exe_constants.h"
#include "Game/terrain_sidecar.h"     // [terrain sidecar] NoteWrite after the +0x29 store
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store

// ============================================================
// Helpers
// ============================================================
// The exe helpers this file calls, by retail address:
//   0x140002cc0 Array<T>::Remove (DynArray8_EraseByValue)
//   0x140003110 ActHunt_DynArraySetCount_3110 (DynArray8_ResizeFill; count+1 appends)
//   0x140004680 FindComponent<MapComponent>       0x140005500 FindComponent<AnimalComponent>
//   0x140002ed0 FindComponent<InteractComponent>  0x140002e10 FindComponent<WorkPlaceComponent>
//   0x1400c3470 ComponentEntity::RefreshEntityFlags
//   0x140105ed0 AnimalComponent::ReleaseToWander  0x1401240a0 LivestockComponent::PenInPasture
//   0x140128570 PastureComponent::ComputeCapacity
//   0x14012fc00 WorkPlaceComponent_SetAccepting
//   0x14009f770 MapGrid_StampCellRefs_9f770 (below)
//   0x140123070 Pf_RecursiveChildSum_123070 (footprint cell count)
//   0x140122e20 Pf_EntityResolvePos_2e20     (footprint cell position)
//   0x140006330 WorkAcc_FindComponent_6330   (RawMaterialDescription on a template)
//   0x140129760 ActRm_PositionEntity_9760
//   0x140013320 FindComponent<TrackerComponent> (NOT a storage finder, the fifth
//               instance of the "misnamed component finders" trap)
//   0x140058fd0 ActRm_ArrayUpsert_58fd0 (the tracker's {template,count} upsert, which
//               alone feeds the production-statistics panel)
//   0x140088f90 ActRm_GetResourceCategory_88f90
//   0x140003050 FindComponent<WorkComponent>  0x14012eb50 WorkComponent_RegisterWorkSlot
//   0x14012ec60 CitizenAI_WorkScheduler_Post  0x140044810 ActRm_DynArrayPush4_44810
//   0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x1400c3720 ComponentEntity_CtorFromDescription
//   .rdata 0x140172204 = 90.0f, the quarter-turn the dropped pile is rotated by.
// LEFT (B.4 COMP-d): GameUpdateLoop m_citizenList / m_game / m_entityList are void* (W0
// header); the Map's cells and RawMaterialComponent's quantity word (COMP-e) are
// untyped; the leaves above take their
// objects as int64_t / uintptr_t (other blocks' bodies).
namespace {

// The town manager at GameUpdateLoop+0xf0: the free-livestock list (+0x80), the pasture
// registry (+0x90) and the town stock rows (+0x170).
inline CitizenBehavior* Town() {
    GameUpdateLoop* w = GameUpdateLoop::_singleton;
    return w ? w->m_citizenList : nullptr;
}

}  // namespace

// ============================================================
// LivestockComponent::SetPasture (0x140123cc0; Ghidra PastureComponent_Slot14_sub_3cc0)
// — bind this livestock into `*pastureRef` (or, with a null target, release it into
// the free-roaming pool).
//
//   if (pasture == target) return;                         // already there
//   erase it from its current owner list (the town's free list when unowned, else
//       the old pasture's animals) and, when it HAD a pasture, re-run
//       AnimalComponent::ReleaseToWander so its tile/bounds are recomputed
//   pasture = target
//   append it to the new owner list (target->animals, or the free list) and, when
//       it now HAS a pasture, PenInPasture to copy the pasture rect onto the animal
// ============================================================
void LivestockComponent::SetPasture(PastureComponent* const* pastureRef) {
    PastureComponent* target = *pastureRef;
    PastureComponent* cur = pasture.Get();
    if (cur == target) return;

    CitizenBehavior* town = Town();
    LivestockComponent* selfRef = this;
    if (cur == nullptr) {
        if (town) town->livestock.Remove(selfRef);
    } else {
        cur->animals.Remove(selfRef);
        AnimalComponent* animal = _entity->FindComponent<AnimalComponent>();   // 0x140005500
        if (animal) animal->ReleaseToWander();   // 0x140105ed0
    }

    pasture = target;
    LivestockComponent* againRef = this;
    if (target) {
        target->animals.SetSizeFill(target->animals._size + 1, &againRef);   // 0x140003110   // PastureComponent::animals (+0x30)
        PenInPasture();   // 0x1401240a0
    } else if (town) {
        town->livestock.SetSizeFill(town->livestock._size + 1, &againRef);   // 0x140003110
    }
}

// ============================================================
// PastureDescription_helper_sub_7140 (0x140127140) — ADOPT free-roaming livestock.
//
// Runs on the pasture's state-2 enter and every state-2 frame. If the pasture is
// under capacity, it scans the town's free-roaming livestock list for animals whose
// ENTITY was cloned from the same template as the pasture's livestock type and binds
// them in, stopping as soon as the pasture is full.
//
// Retail snapshots every match into a heap array before binding, because binding
// erases from the very list it is walking. We snapshot the same way into a fixed
// buffer, stopping the collection once we already hold more candidates than the
// pasture can take — behaviourally identical, no allocation.
//
// This is the path that turns the "easy start" free-roaming cows/sheep/chickens
// (spawned by the start-config simple pool) into a stocked pasture.
// ============================================================
void PastureComponent::AdoptWanderers() {
    CitizenBehavior* town = Town();
    if (!town) return;
    int freeCount = town->livestock._size;
    if (freeCount == 0) return;

    const int cap = ComputeCapacity();   // 0x140128570
    if (animals._size >= cap) return;

    ComponentDescription* wanted = livestockType;
    Object::InternalPtr<LivestockComponent>* freeList = town->livestock._data;
    if (!freeList) return;

    static const int kMaxCandidates = 64;
    LivestockComponent* candidates[kMaxCandidates];
    int nCand = 0;
    const int needed = cap - animals._size;
    for (int i = 0; i < freeCount && nCand < kMaxCandidates && nCand < needed; ++i) {
        LivestockComponent* comp = freeList[i].Get();
        if (!comp) continue;
        ComponentEntity* owner = comp->_entity.Get();
        if (!owner) continue;
        if (owner->_description != wanted) continue;
        candidates[nCand++] = comp;
    }
    if (nCand == 0) return;

    for (int i = 0; i < nCand; ++i) {
        PastureComponent* pastureRef = this;
        candidates[i]->SetPasture(&pastureRef);   // 0x140123cc0
        if (animals._size >= cap) break;
    }
}

// ============================================================
// PastureDescription_helper_7370 (0x140127370) — REPRODUCTION.
//
// For each livestock in the pasture, find the AnimalComponent on its entity and test
// the breed bit (flags bit 21 / 0x200000, set by the animal's own behaviour update).
// When set: clear the bit (BTR 0x15), notify the entity of the flag change, and — if
// the pasture holds fewer than capacity+3 animals (retail's `LEA R13D,[RAX+3]`) —
// spawn a calf/lamb/chick with this animal as the parent.
// ============================================================
void PastureComponent::Reproduce() {
    const int limit = ComputeCapacity() + 3;

    for (int i = 0; i < animals._size; ++i) {
        Object::InternalPtr<LivestockComponent>* arr = animals._data;
        if (!arr) return;
        LivestockComponent* livestock = arr[i].Get();
        if (!livestock) continue;
        ComponentEntity* owner = livestock->_entity.Get();
        AnimalComponent* animal = owner ? owner->FindComponent<AnimalComponent>() : nullptr;
        if (!animal) continue;

        uint32_t oldFlags = animal->_flags;
        if ((oldFlags & 0x200000u) == 0) continue;          // not ready to breed

        uint32_t newFlags = oldFlags & ~0x200000u;
        animal->_flags = newFlags;
        if (((newFlags ^ oldFlags) & 0xffff) != 0 && animal->_entity != nullptr) {
            animal->_entity->RefreshEntityFlags();   // 0x1400c3470
        }
        if (animals._size < limit) {
            AnimalComponent* parent = animal;
            SpawnLivestock(&parent);   // 0x140128670
        }
    }
}

// ============================================================
// PastureComponent slot 15 (+0x78 "OnMessage", 0x140127720) — the BREED TAG.
//
// This is the missing link of livestock reproduction. The herder's work chain
// ends in a SendComponentMessage(msg=0) at the pasture (same dispatch the build
// labor credit uses, see buildcomponent.cpp Slot15). On each message the retail
// handler picks ONE RANDOM pastured animal and ORs flag bit 23 (0x800000) into
// its AnimalComponent — the "breed-eligible" mark. AnimalComponent::Update's
// offspring latch requires exactly that bit (plus the canGraze byte and adult
// age) before it will arm the offspring timer that sets bit 21, which
// Reproduce then consumes to spawn a calf/lamb/chick. NOTHING else in
// the exe ever sets bit 23, so without this slot no livestock can ever breed —
// the reproduction rate is deliberately driven by herdsman labor.
//
// ABI matches BuildComponent's slot 15: (self RCX, int msgCode RDX, rest unused).
// The `(new ^ old) & 0xffff` notify can never fire here (bit 23 is above the
// mask); kept for fidelity like the identical dead test in helper_7400.
// ============================================================
void PastureComponent::OnMessage(int msg, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    if (msg != 0) return;
    const int count = animals._size;
    if (count == 0) return;
    GameUpdateLoop* w = GameUpdateLoop::_singleton;
    Object::InternalPtr<LivestockComponent>* arr = animals._data;
    if (!w || !arr) return;

    uint32_t idx = w->m_random->Rand(static_cast<uint32_t>(count));
    LivestockComponent* livestock = arr[idx].Get();
    if (!livestock) return;

    AnimalComponent* animal = livestock->_entity->FindComponent<AnimalComponent>();   // 0x140005500
    if (!animal) return;

    uint32_t oldF = animal->_flags;
    uint32_t newF = oldF | 0x800000u;
    animal->_flags = newF;
    if (((newF ^ oldF) & 0xffffu) != 0 && animal->_entity != nullptr) {
        animal->_entity->RefreshEntityFlags();   // 0x1400c3470
    }
}


// ============================================================
// Shared release body — PastureDescription_helper_6e40 (0x140126e40), byte-identical
// to the PastureComponent vtable-slot-7 body at 0x1401277e0.
//
// Walk the pasture's animal list from the BACK, unassigning each animal (pasture ->
// null) so it returns to the town's free-roaming pool, then erase the pasture itself
// from the town's pasture registry.
//
// ⚠ The loop terminates because the unassign path ERASES the animal from `animals`,
// shrinking `animalCount`. FUN_140002cc0 / FUN_140003110 are DynArray8_EraseByValue /
// DynArray8_ResizeFill — an earlier port of this function mislabelled them
// "AddToList"/"InsertInList", so nothing ever shrank the list and the
// `while (count >= 1)` spun forever.
// ============================================================
void PastureComponent::ReleaseAllAnimals() {
    CitizenBehavior* town = Town();

    int guard = 0;
    while (animals._size >= 1) {
        if (++guard > 4096) {                       // cannot happen in retail; see above
            std::fprintf(stderr, "[Pasture] release loop guard tripped on %p (count=%d)\n",
                         static_cast<void*>(this), animals._size);
            std::fflush(stderr);
            break;
        }
        Object::InternalPtr<LivestockComponent>* arr = animals._data;
        if (!arr) break;
        LivestockComponent* animal = arr[animals._size - 1].Get();
        if (!animal) break;
        PastureComponent* cur = animal->pasture.Get();
        if (cur == nullptr) break;                  // already free: retail would spin here

        LivestockComponent* ref = animal;
        cur->animals.Remove(ref);
        ComponentEntity* owner = animal->_entity.Get();
        AnimalComponent* animalComp = owner ? owner->FindComponent<AnimalComponent>() : nullptr;
        if (animalComp) animalComp->ReleaseToWander();   // 0x140105ed0

        animal->pasture = nullptr;
        LivestockComponent* backRef = animal;
        if (town)
            town->livestock.SetSizeFill(town->livestock._size + 1, &backRef);   // 0x140003110
    }

    PastureComponent* selfRef = this;
    if (town) town->pastures.Remove(selfRef);
}

// ============================================================
// State 2 ENTER — PastureDescription_helper_70c0 (0x1401270c0).
//   * adopt any matching free-roaming livestock (sub_7140)
//   * append this pasture to the town's pasture registry
//   * find the owner's WorkPlaceComponent and set its accepting flag to 0
// Returns AL = 0 (`& 0xff..00`) so sibling state nodes still run.
// ============================================================
char PastureComponent::EnterOperational() {
    AdoptWanderers();   // 0x140127140

    CitizenBehavior* town = Town();
    PastureComponent* selfRef = this;
    if (town)
        town->pastures.SetSizeFill(town->pastures._size + 1, &selfRef);   // 0x140003110

    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    if (wpc) wpc->SetAccepting(false);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)

    return 0;
}

// ============================================================
// State 3 ENTER — PastureDescription_helper_6e40 (0x140126e40): teardown.
// Returns AL = 0.
// ============================================================
char PastureComponent::EnterTeardown() {
    ReleaseAllAnimals();
    return 0;
}

// ============================================================
// The state-2 PRODUCTION tail of 0x1401267e0 (from 0x14012681d), in retail's order:
//
//   if (livestockType == null) return;                              0x14012681d
//   stamp the footprint cells with the livestock type (0x14009f770)
//   lsDesc   = the type's LivestockDescription (inlined scan, cell 0x1401c79a8;
//              CMOVNZ, null on a miss, read unguarded at 0x1401268f1)
//   produced = lsDesc->_byproduct; if (produced == null) return;    0x1401268f5
//   rawDesc  = FindDescription<RawMaterialDescription> (0x140006330), unguarded
//   interact = FindComponent<InteractComponent> (0x140002ed0), unguarded
//   full     = stock row lsDesc->_type: currentCount >= maxCount, ONCE (0x140126941)
//   while (production > rawDesc->_lowCreateCount) {                no cap
//       production -= rawDesc->_lowCreateCount;
//       if (full) continue;
//       for each interact node (kind 2; the count re-read per node): its cell;
//           an occupant with no RawMaterialComponent (0x1400223c0) is cleared off
//           the cell (terrain 2, region / pathfinder republish, occupant null) and
//           the cell is taken; an occupant pile skips the cell; a free cell is taken
//       no cell -> continue;
//       the pile: entity, position + quarter-turn, quantity, tracker upsert, the
//       town stock bump, RegisterEntity, the pasture's AutoPickup record
//   }
// Nothing in the loop prints.
// ============================================================
namespace {

void PastureProduce(PastureComponent* self) {
    // 0x14012681d: retail's one test before the stamp is the livestock type (+0x28).
    // The MapComponent (0x140126841), the map ([0x1401c3dd8]+0xf8, 0x14012686d) and the
    // town ([0x1401c3dd8]+0xf0, 0x140126927) are read with no test (row 1241).
    ComponentDescription* tmpl = self->livestockType;
    if (!tmpl) return;
    GameUpdateLoop* w = GameUpdateLoop::_singleton;
    Map* map = w->m_game;

    MapComponent* mc = self->_entity->FindComponent<MapComponent>();   // 0x140004680
    map->StampCellRefs(static_cast<int16_t>(mc->tileX0),
                       static_cast<int16_t>(mc->tileY0),
                       static_cast<int16_t>(mc->tileX1),
                       static_cast<int16_t>(mc->tileY1), tmpl);

    LivestockDescription* lsDesc = tmpl->FindDescription<LivestockDescription>();   // inlined, 0x140126889
    ComponentDescription* produced = lsDesc->_byproduct;   // +0x20
    if (produced == nullptr) return;

    RawMaterialDescription* rawDesc = produced->FindDescription<RawMaterialDescription>();   // 0x140006330
    InteractComponent* interact = self->_entity->FindComponent<InteractComponent>();         // 0x140002ed0

    const ProfessionResourceEntry& row =
        w->m_citizenList->professionResources._data[static_cast<int>(lsDesc->_type)];       // +0x170, +0x28
    const bool full = row.currentCount >= row.maxCount;                                       // SETGE, kept in [rbp+0x7f]

    while (self->production > rawDesc->_lowCreateCount) {
        self->production -= rawDesc->_lowCreateCount;
        if (full) continue;

        // A footprint cell for the pile.
        int cell = -1;
        bool found = false;
        for (int node = 0; node < interact->NodeCount(2); ++node) {   // 0x140123070, re-read per node
            Math::Vector3 xy;
            interact->NodePosition(&xy, 2, node, nullptr);              // 0x140122e20
            const int dim = map->size;
            const float half = static_cast<float>(dim >> 1);
            int gx = static_cast<int>(xy.x + half);
            int gy = static_cast<int>(xy.y + half);
            if (gx < 0) gx = 0;
            if (gx >= dim) gx = dim - 1;
            if (gy < 0) gy = 0;
            if (gy >= dim) gy = dim - 1;
            cell = dim * gy + gx;

            MapCell& rec = map->cells.GetData()[cell];
            ComponentEntity* occ = rec.occupant.Get();
            if (occ != nullptr && occ->FindComponent<RawMaterialComponent>() == nullptr) {   // 0x1400223c0
                // Not a pile: clear the occupant off the cell and take it
                // (0x140126a5c-0x140126ae2; the same retype as the cemetery's grave).
                const uint8_t old = rec.terrainType;                              // +0x29
                const bool wasWalk = ((1u << (old & 0x1f)) & 0x1e0u) != 0;       // old 5..8
                map->field_0xA2 |= static_cast<uint8_t>(wasWalk);
                rec.terrainType = 2;
                TerrainSidecar::NoteWrite(&rec);   // [terrain sidecar] mirror (missing until PERFXEXT)
                CellStamp::NoteWrite(&rec);   // [AMBMEMO] cell write clock
                if (wasWalk)
                    map->regionGraph->MarkCellDirty(cell % map->size, cell / map->size);   // +0x70, 0x1400a8c90
                map->pathfinder->SetCellTerrain(cell, 2);                                   // +0x68, 0x1400a6fe0
                rec.occupant = nullptr;
                CellStamp::NoteWrite(&rec);   // [AMBMEMO] cell write clock
                occ = nullptr;
            }
            if (occ == nullptr) { found = true; break; }
        }
        if (!found || cell < 0) continue;   // 0x140126b12 / 0x140126b1a

        // The pile entity (MemAlloc 0x48; the ctor 0x1400c3720 on a non-null block).
        ComponentEntity* ent = nullptr;
        if (void* mem = System::MemoryAllocator::MemAlloc(0x48, 8))
            ent = ::new (mem) ComponentEntity(produced);
        Object::InternalPtr<ComponentEntity> entRef = ent;

        // Position it on the cell with a random quarter-turn (0x140129760).
        RawMaterialComponent* rawComp = entRef->FindComponent<RawMaterialComponent>();   // inlined, cell 0x1401c7c80
        const int roll = w->m_random->RandTol(2);                                       // import 0x1401512f0
        rawComp->PositionRawMaterial(cell, static_cast<float>(roll) * kRdata_140172204);

        // 0x140126c1b-0x140126c74: a WorkPlaceDescription lookup on the pasture's
        // description whose result retail discards.
        (void)self->_entity->_description->FindDescription<WorkPlaceDescription>();      // cell 0x1401c8118

        rawComp->quantity = rawDesc->_lowCreateCount;   // +0x28
        rawComp->profession = nullptr;                  // +0x30

        // The pasture's TrackerComponent (0x140013320) records the production
        // (0x140058fd0), then the town stock row of the produce category (0x140088f90).
        ComponentEntity* pastureEntity = self->_entity.Get();
        CitizenBehavior* town = w->m_citizenList;
        const int perUnit = rawDesc->_lowCreateCount;
        if (pastureEntity != nullptr) {
            TrackerComponent* tracker = pastureEntity->FindComponent<TrackerComponent>();
            if (tracker != nullptr)
                tracker->Upsert(produced, perUnit);
        }
        const int idx = static_cast<int>(ResourceCategory(produced));
        if (idx >= 0)
            town->professionResources._data[idx]._field10 += perUnit;

        w->m_entityList->RegisterEntity(&entRef, produced->_category);   // 0x1400c48a0

        // The pasture's AutoPickupComponent (inlined scan, cell 0x1401c3fb0) records the
        // pile's work id (0x140003050 -> +0x28, 0x140044810).
        AutoPickupComponent* autoPickup = self->_entity->FindComponent<AutoPickupComponent>();
        if (autoPickup != nullptr) {
            WorkComponent* pileWork = entRef->FindComponent<WorkComponent>();
            autoPickup->Append(static_cast<uint32_t>(pileWork->workId));
        }
    }
}

}  // namespace

// ============================================================
// State 2 PER-FRAME — PastureDescription_helper_81a0_sub_67e0 (0x1401267e0).
//   sub_7140 (adopt) -> helper_7370 (reproduce) -> production/slaughter tail.
// Returns AL = 1 (retail's `CONCAT71(..,1)`).
// ============================================================
char PastureComponent::TickOperational() {
    AdoptWanderers();
    Reproduce();
    PastureProduce(this);
    return 1;
}

// ============================================================
// Dispatcher + state table.
// ============================================================
// PastureCtrlTag lives in PastureComponent.h (the member type needs it).
using PastureDispatch = ::ControllerLive::Dispatcher<PastureCtrlTag>;

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm[, time]): [node+0x18] is the controller's back-pointer, this
// component, so each entry is a one-line adapter that calls the member. The tree's
// null-component guards stay in the adapters.
namespace {
char PastEnterOperational(PastureComponent* c, StateMachine*)   { return c ? c->EnterOperational() : 0; }  // 0x1401270c0
char PastTickOperational(PastureComponent* c, StateMachine*, const System::Time*) { return c ? c->TickOperational() : 1; } // 0x1401267e0
char PastEnterTeardown(PastureComponent* c, StateMachine*)      { return c ? c->EnterTeardown() : 0; }     // 0x140126e40
}  // namespace

static void PastureCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = PastureDispatch::table;
    if (t.count != 0) return;                       // idempotent
    // state 2 (the controller's DEFAULT state): operational.
    //   enter = helper_70c0, per-frame (trans38) = sub_67e0, transitions -> {3}
    TableRegisterState(t, 2, &PastEnterOperational,
                       nullptr, &PastTickOperational, nullptr);
    TableAddTransition(t, 3, nullptr);
    // state 3: teardown. enter = helper_6e40, no per-frame, transitions -> {6,4,5}
    TableRegisterState(t, 3, &PastEnterTeardown,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, 6, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddTransition(t, 5, nullptr);
    std::fprintf(stderr, "[Pasture] state table populated: %d states (2=operational, "
                 "3=teardown)\n", t.count);
    std::fflush(stderr);
}

// PastureWork factory-with-entity (src/game/Work/pasturework.cpp): MemAlloc(0x58,8)
// + the retail ctor 0x1400f2d80. `entityRefSlot` points at an 8-byte entity ref.
extern "C" Work* PastureWork_CreateForEntity(ComponentEntity* const* entityRefSlot);

// ============================================================
// PastureComponent slot 13 (0x140127dd0) — capability query for the three
// pasture commands. Shared preamble sums the FREE capacity across every OTHER
// pasture of the same livestock type (the town's pasture registry).
//
//   cmd 0 "empty the pasture":  our count fits elsewhere (0 < count <= free)
//   cmd 1 "split the herd":     count > 9 AND free >= 5
//   cmd 2 "toggle the herder":  bit 0 = has a livestock type,
//                               bit 1 = workplace currently accepting
//
// The cmd 0/1 results are additionally masked by hasType (uVar5 in retail).
// Slot 14 dispatches this through the vtable and tests only bit 0.
// ============================================================
uint32_t PastureComponent::CommandMask(int cmd) {
    int freeElsewhere = 0;
    CitizenBehavior* town = Town();
    const int n = town ? town->pastures._size : 0;
    Object::InternalPtr<PastureComponent>* list = town ? town->pastures._data : nullptr;
    for (int i = 0; i < n && list; ++i) {
        PastureComponent* p = list[i].Get();
        if (!p || p == this) continue;
        if (p->livestockType != livestockType) continue;
        freeElsewhere += p->ComputeCapacity() - p->animals._size;
    }

    const uint64_t hasType = (livestockType != nullptr) ? 1u : 0u;
    if (cmd == 0) {
        const int cnt = animals._size;
        return (cnt > 0 && cnt <= freeElsewhere) ? hasType : 0;
    }
    if (cmd == 1) {
        if (animals._size > 9)
            return (freeElsewhere < 5) ? 0 : hasType;
        return 0;
    }
    if (cmd == 2) {
        WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        uint64_t r = hasType;
        if (wpc && wpc->isAccepting) r |= 2;
        return r;
    }
    return 0;
}


// ============================================================
// PastureComponent slot 14 (0x140127a60) — execute a pasture command.
// Guard: vtable slot 13 (above), bit 0. This is the slot the pasture dialog's
// SetLivestockType invokes with cmd 2 the first time a type is assigned — the
// ORGANIC start of the herder chain:
//
//   cmd 2: toggle the herder. Flip the owner entity's WorkPlaceComponent
//     accepting flag (Entity_impl_fc00). If it lands OFF: stop the entity's
//     work slot (FUN_14012eb50). If it lands ON: MemAlloc(0x58) + the retail
//     PastureWork ctor and post it to the work scheduler
//     (CitizenAI_impl_ec60, code=1) — a citizen then claims it and tends the
//     pasture, whose completed cycles send the slot-15 msg-0 breed tag.
//
//   cmd 0 / cmd 1: transfer animals to the other same-type pastures — all of
//     them (cmd 0) or floor(half) (cmd 1, keep = count - count/2). Fill the
//     pasture with the most free capacity first, then sweep the rest, moving
//     the FIRST animal of our array each step via LivestockComponent::SetPasture
//     (which erases it from our array, so the loop advances).
// ============================================================
void PastureComponent::Command(int cmd) {
    const uint64_t can = CommandMask(cmd);   // slot 13, a virtual dispatch in retail
    if ((can & 1) == 0) {
        return;
    }

    if (cmd == 2) {
        WorkComponent* wc = _entity->FindComponent<WorkComponent>();             // 0x140003050
        WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();  // 0x140002e10
        if (!wc || !wpc) {   // standalone guard; retail assumes both
            std::fprintf(stderr, "[Pasture] cmd 2: missing %s%s on %p — herder toggle skipped\n",
                         wc ? "" : "WorkComponent ", wpc ? "" : "WorkPlaceComponent",
                         static_cast<void*>(this));
            std::fflush(stderr);
            return;
        }

        const bool cur = wpc->isAccepting;
        wpc->SetAccepting(!cur);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)
        if (!wpc->isAccepting) {
            wc->CancelWork();   // 0x14012eb50
            wpc->SetAccepting(false);
        } else {
            ComponentEntity* entRef = _entity.Get();
            Work* work = PastureWork_CreateForEntity(&entRef);
            Object::InternalPtr<Work> workIBP = work;   // the InternalPtr ctor is a plain store
            wc->PostWork(1, 1, &workIBP);
            wpc->SetAccepting(true);
        }
        return;
    }

    // cmd 0 / cmd 1 — transfer animals out.
    CitizenBehavior* town = Town();
    const int n = town ? town->pastures._size : 0;
    Object::InternalPtr<PastureComponent>* list = town ? town->pastures._data : nullptr;
    const int keep = (cmd != 0) ? animals._size - (animals._size >> 1) : 0;

    PastureComponent* best = nullptr;
    int bestFree = 0;
    for (int i = 0; i < n && list; ++i) {
        PastureComponent* p = list[i].Get();
        if (!p || p == this) continue;
        if (p->livestockType != livestockType) continue;
        const int freeN = p->ComputeCapacity() - p->animals._size;
        if (freeN > bestFree) { bestFree = freeN; best = p; }
    }

    int moved = 0;
    auto transferInto = [&](PastureComponent* dst) {
        while (keep < animals._size) {
            if (dst->ComputeCapacity() <= dst->animals._size) break;
            Object::InternalPtr<LivestockComponent>* arr = animals._data;
            if (!arr) break;                        // standalone guard
            if (!arr[0]) break;                     // standalone guard
            PastureComponent* dstRef = dst;
            arr[0]->SetPasture(&dstRef);   // 0x140123cc0
            ++moved;
        }
    };
    if (best) transferInto(best);
    for (int i = 0; i < n && list; ++i) {
        PastureComponent* p = list[i].Get();
        if (!p || p == this || p == best) continue;
        if (p->livestockType != livestockType) continue;
        transferInto(p);
    }
}


// ============================================================
// 0x14009f770 — map-grid cell owner-ref stamp (shared leaf).
// ------------------------------------------------------------
// Writes an 8-byte ref (the owning entity/template value the caller passes)
// into cell+0x10 for every cell of the (x,y,w,h) footprint rect. Cell array at
// Map+0x80, stride 0x30. Retail callers: the pasture state-2 per-frame
// (sub_67e0 — stamps the pasture's cells so pathing/occupancy queries see it)
// and HarvestOrchard_impl_0800. Was resolved through the registry but never
// registered — the null guard made footprint stamping a silent no-op until
// 2026-07-30 (deferred item from the animal wave's Update 2).
// ============================================================
uint64_t Map::StampCellRefs(int x, int y, int w, int h, ComponentDescription* ref) {
    if (w <= 0) return 0;
    const Map* map = this;
    const int dim = map->size;
    // +0x10 of a cell is its owner ref.
    MapCell* cells = map->cells._data;   // [RCX+0x80], read with no test (0x14009f7cd)
    for (int64_t dx = 0; dx < w; ++dx) {
        for (int64_t dy = 0; dy < h; ++dy) {
            const int64_t cell = (dy + y) * dim + x + dx;
            cells[cell].field_0x10 = ref;
        }
    }
    return 0;
}

// Built ONCE at static-init time, as retail's PastureDescription setup does (B38 D6/D7).
// Before, every PastureComponent factory call re-ran it from both threads. The
// "[Pasture] state table populated" line now prints before standalone_error.log opens.
static const bool s_pastureStateTableBuilt = (PastureCtrl_PopulateTable(), true);
