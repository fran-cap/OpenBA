// src/game/Component/citizen_reassign_workers.cpp
//
// Faithful port of the town-wide RESIDENCE RE-BALANCER pair:
//   Entity_impl_6c60   (0x1400d6c60)  — per-residence relocation cost
//   FUN_1400d62a0      (0x1400d62a0)  — the re-balancer itself
//
// Reached, once per town tick, via the ProcessPaths drain (before AssignCitizens):
//   CitizenBehavior_Update (0x14008c2b0)
//     -> CitizenBehavior_ProcessPaths (FUN_1400d6f00, citizenbehavior.cpp:402)
//        when pendingIdx < 0:
//          fn_PathGraph_ReassignWorkers(pg)  <-- 0x1400d62a0 (THIS)
//          fn_PathGraph_AssignCitizens(pg)   <-- 0x1400d43b0 (citizen_assign_citizens.cpp)
//          fn_PathPairs_Clear(pg)            <-- 0x140025b30
//
// ============================================================================
// WHAT IT DOES
//   The pass consolidates single-occupant residences: it collects every
//   capacity<2 residence in the town, splits them into
//     - EMPTY ones   (occupant count == 0)  -> "destination" set   (retail tree local_b8)
//     - OCCUPIED ones (occupant count != 0)  -> "candidate" set     (retail tree local_98),
//       keyed by a relocation cost = Entity_impl_6c60(residence -> its own cell),
//   then, walking the candidates from HIGHEST current cost down, tries to move a
//   candidate's occupants into the nearest cheaper empty residence. On a match it
//   re-homes every occupant (CitizenComponent_AssignWorkplace), migrates the
//   residence's stored goods into the destination (StorageComponent add/subtract),
//   and retires the now-occupied destination from the empty set.
//
//   The two retail scratch trees are AVL trees whose node memory nothing outside
//   this function observes, so — exactly as src/game/Component/citizen_assign_citizens.cpp
//   does for the AssignCitizens allocator — they are modeled here as clean std::
//   containers with the SAME visiting order. The pointer tie-breaks follow heap
//   addresses, in retail as here; fix.alloc_tiebreak puts the residence's entity id in
//   front of the pointer (docs/retail_bugs.md §14; 0 with the row off).
//   Every touch of real game state (residence/occupant/storage fields) and every
//   leaf helper is called wholesale and byte-faithful.
//
//     retail tree local_b8  -> std::set<ResKey>             (empty residences, by ptr asc)
//     retail tree local_98  -> std::set<CostRes, CostCmp>   (occupied residences; in-order
//                                                            = cost DESC, ptr ASC on ties)
//
// FIELD MAP (verified against Application-x64.exe in Ghidra)
//   gameWorld (*g_pGameWorldSingleton)
//     +0xb0  profession registry (reg)   +0xf0  townManager   +0xf8  pathData(int*)
//   townManager
//     +0x20  residence entity array (ptr)   +0x28  residence count(int)
//   residence entity (ComponentEntity)
//     +0x30  component IBP array (ptr)       +0x38  component count(int)
//   residenceComponent (type-id ResidenceComponent 0x1401c7cf0)
//     +0x10  owner entity IBP                +0x50  occupant list {data@+0,count@+8}
//     +0x58  capacity(int)
//   occupant (CitizenComponent)
//     +0x3c   age(float)              +0x1f0 profession
//     +0x208  school (AttendanceComponent*)   +0x248 workplace (ComponentEntity*)
//     +0x288  CitizenDescription (+0x44 adultAge)
//   (B4 P2 CIT-2: read by the headers' names; the "+0x58 capacity" above is
//    ResidenceComponent::residentSlotCount, the household count, and the occupant
//    list is the first household, residentSlots[0].)
//   storageComponent (type-id StorageComponent 0x1401c7e78)
//     +0x28  item-entry array (ptr)   +0x30  item count(int)
//     item entry (stride 0x10): +0x00 descriptor(ptr) +0x08 avail(int) +0x0c reserved(int)
//
// LEAF HELPERS (all reimplemented + registered elsewhere; resolved wholesale):
//   Entity_impl_6c60          0x1400d6c60  — reimplemented in THIS file (called direct)
//   CitizenComponent_AssignWorkplace 0x1401163d0  (reg_citizen_assignworkplace.cpp)
//   StorageComponent_impl_cd80 0x14012cd80  == fn_StorageCanAdd
//   FUN_14012d000              0x14012d000  == fn_StorageAddItem
//   FUN_14012d150              0x14012d150  — storage subtract (resolved here)
//   Entity_FindComponent_Position 0x140002ed0 == fn_Entity_FindComponent_Position
//   Entity_impl_2e20          0x140122e20  == fn_Entity_GetPosition
//   Entity_impl_7fb0          0x140087fb0  == fn_Entity_GetGridId
//   FUN_1400a00f0             0x1400a00f0  == fn_Map_CanPathTo
//   FUN_140008830             0x140008830  == fn_PathDistance
//   type-ids DWORD_1401c7cf0 / DWORD_1401c7e78 (reg_economy_typeids.cpp)
//
// The retail AVL insert/rebalance/remove/free leaves (a8c0/e360/a6e0/e320/14c30)
// only maintained the scratch-tree layout and are therefore intentionally NOT
// called — the std:: containers subsume them.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>
#include "Component/ResidenceComponent.h"   // ENDGAME B.3.9 C1: 0x1401c7cf0 cell class
#include "Component/StorageComponent.h"     // ENDGAME B.3.9 C1: 0x1401c7e78 cell class
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/InteractComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "Game/ProfessionList.h"
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "ext/registry.h"                     // Ext::Fix().alloc_tiebreak
#include "ext/alloc_tiebreak/alloc_tiebreak.h"   // EntityKey
#include "ext/alloc_tiebreak/tie_key.h"       // TieKey: the scratch trees' keys

// Build-time roster-only guard, shared with citizen_assign_citizens.cpp: while set,
// the workplace/housing attach-detach helpers skip the Graphics::Scene mesh toggle
// down to the bare roster mutation (the scene churn that can deadlock the build). We
// hold it across each relocation's AssignWorkplace churn for the same reason the
// AssignCitizens pass (which runs immediately after us on the same drain) holds it.
extern bool g_wpcRosterOnly;

namespace {


// The consumer leaves are called by name: CitizenComponent_AssignWorkplace
// (0x1401163d0) and ActRm_StorageSubtract_d150 (0x14012d150).
// Row 330 (B7 RES): the component scans (cells 0x1401c7cf0 / 0x1401c7e78) are
// ComponentEntity::FindComponent<T>()'s shape -- `[[slot]+0x10]` with no null-slot test,
// IsDerivedFrom + CMOVNZ on the first match -- so the tree's FindComponentDerived (null
// entity / zero id / null slot tests) and its type-id cache are gone.

using ResKey = Ext::AllocTiebreak::TieKey<ResidenceComponent>;

// Candidate key: retail tree local_98 visits IN-ORDER as (cost DESC, residence ptr ASC).
// `tie` (fix.alloc_tiebreak, 0 with the row off) sits in front of the pointer.
struct CostRes { int cost; int32_t tie; ResidenceComponent* res; };
struct CostCmp {
    bool operator()(const CostRes& a, const CostRes& b) const {
        if (a.cost != b.cost) return a.cost > b.cost;   // higher cost visited first
        if (a.tie != b.tie) return a.tie < b.tie;
        return a.res < b.res;                           // ties: ascending pointer
    }
};

}  // namespace

// ===========================================================================
// Entity_impl_6c60 (0x1400d6c60) — relocation cost for a residence's occupants
// ===========================================================================
// param_2 -> residence whose mis-professioned, employed, healthy, idle occupants
//            are summed; param_3 -> residence supplying the TARGET cell (its owner
//            entity's position). Returns the summed path distance (0 when none
//            qualify / unreachable). param_1 is dead (retail keeps it for ABI).
extern "C" uint64_t Entity_ReassignCost_6c60(const void* /*param_1*/,   // LEFT: param_1 is dead in retail (never read) and its two callers hand different things: a ResidenceComponent::Family* and PathGraph_ReassignWorkers' own void* ctx (exe_bodies.h)
                                             ResidenceComponent* const* param_2,
                                             ResidenceComponent* const* param_3) {
    // Row 330 (B7 RES): 0x1400d6c60 reads [gw+0xf8] / [gw+0xb0] and their fields with no test.
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* map = gw->m_game;
    ProfessionList* reg = gw->m_professionList;
    Map* pathData = map;

    // --- target cell from param_3's owner-entity position ---
    // The InteractComponent find (0x140002ed0) and the node position (0x140122e20) are
    // called by their members; the find's InternalPtr slot was always non-null.
    ComponentEntity* ownerEntity = (*param_3)->_entity.Get();
    InteractComponent* posComp = ownerEntity->FindComponent<InteractComponent>();
    Vector3Slot pos;   // pos[0]=x (local_48), pos[1]=y (local_44)
    posComp->NodePosition(&pos.v, 0, 0, nullptr);

    int width = map->size;
    int cellX = static_cast<int>(pos.v.x + static_cast<float>(width >> 1));
    int cellY = static_cast<int>(pos.v.y + static_cast<float>(width >> 1));
    if (cellX < 0) cellX = 0;
    if (width <= cellX) cellX = width - 1;
    if (cellY < 0) cellY = 0;
    if (width <= cellY) cellY = width - 1;
    uint32_t targetCell = static_cast<uint32_t>(width * cellY + cellX);

    // --- sum path cost of each qualifying occupant of param_2 to targetCell ---
    uint32_t total = 0;
    int i = 0;
    for (;;) {
        // The first household of param_2's residence, re-read every pass.
        const ResidenceComponent::Family* family = (*param_2)->residentSlots._data;
        if (!(i < family->_size)) break;
        CitizenComponent* worker = family->_data[i].Get();

        // An adult (desc->adultAge <= age); the null-description test is the tree's.
        CitizenDescription* desc = worker->description;
        bool adult = desc->adultAge <= worker->age;   // [[c+0x288]+0x44] <= [c+0x3c], no test
        if (adult &&
            worker->school == nullptr &&                                      // not at school
            worker->workplace != nullptr &&                                   // has a workplace
            worker->profession != reg->field_0x30) {                          // profession != default
            ComponentEntity* workplaceIBP = worker->workplace.Get();
            uint32_t wpCell = static_cast<uint32_t>(EntityAnchorCell(&workplaceIBP, nullptr));
            // WorkBase_ValidateCell_a00f0 returns its bool in the low byte of a uint64.
            if (static_cast<uint8_t>(pathData->ValidateCell(0xe1f, targetCell,
                                                                  static_cast<int>(wpCell)))) {
                total += static_cast<uint32_t>(pathData->CalcDistance(targetCell, wpCell));
            }
        }
        ++i;
    }
    return total;
}

// ===========================================================================
// FUN_1400d62a0 (0x1400d62a0) — town residence re-balancer
// ===========================================================================
void PathGraph::ReassignWorkers() {
    // Row 330 (B7 RES): 0x1400d62a0 reads [gw+0xf0] and its +0x20 / +0x28 with no test.
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    int               resCount = townMgr->field_0x20._size;
    Object::InternalPtr<ComponentEntity>* resArray = townMgr->field_0x20._data;

    // FIX alloc_tiebreak (NOT RETAIL, docs/extensions/handins/ALLOCTIEBREAK.md): both trees order
    // equal keys by the residence's entity id instead of by address. Off: every id is 0 and the
    // order is the pointer's.
    const bool stableTies = Ext::Fix().alloc_tiebreak;

    std::set<ResKey>              empties;   // retail local_b8: available (occupant==0) residences
    std::set<CostRes, CostCmp>    occupied;  // retail local_98: keyed by relocation cost

    // ---------- Phase 1: bucket every residence with fewer than two households ----------
    for (int r = 0; r < resCount; ++r) {
        ComponentEntity* resEntity = resArray[r].Get();
        ResidenceComponent* resComp = resEntity->FindComponent<ResidenceComponent>();   // +0x58 read, no test

        if (resComp->residentSlots._size >= 2)
            continue;                                 // only single-household residences

        ResidenceComponent::Family* occList = resComp->residentSlots._data;
        int occCount = occList->_size;
        const int32_t tie = stableTies ? Ext::AllocTiebreak::EntityKey(resComp) : 0;
        if (occCount == 0) {
            empties.insert(ResKey{ tie, resComp });
        } else {
            ResidenceComponent* rc2 = resComp;
            ResidenceComponent* rc3 = resComp;
            uint32_t cost = static_cast<uint32_t>(
                Entity_ReassignCost_6c60(occList, &rc2, &rc3));
            occupied.insert(CostRes{ static_cast<int>(cost), tie, resComp });
        }
    }

    int relocations = 0;

    // ---------- Phase 2: greedily consolidate ----------
    // Hold the roster-only guard across every AssignWorkplace churn below.
    if (!empties.empty() && !occupied.empty()) {
        bool prevRoster = g_wpcRosterOnly;
        g_wpcRosterOnly = true;

        for (auto it = occupied.begin(); it != occupied.end(); ++it) {
            ResidenceComponent* srcRes    = it->res;   // residence to (maybe) vacate (tree local_98 node)
            int                 threshold = it->cost;  // its own current cost (retail uVar13)
            ResidenceComponent* bestDest  = nullptr;   // retail local_res18
            ResKey              bestKey{ 0, nullptr };

            // nearest empty residence whose relocation cost beats `threshold`
            for (const ResKey& destKey : empties) {   // ascending key == retail in-order walk
                ResidenceComponent* destRes = destKey.ptr;
                ResidenceComponent* b = srcRes;
                ResidenceComponent* a = destRes;
                int c = static_cast<int>(Entity_ReassignCost_6c60(this, &b, &a));
                if (c < threshold) {
                    threshold = c;
                    bestDest = destRes;
                    bestKey = destKey;
                }
            }
            if (!bestDest) continue;

            // --- re-home every occupant of srcRes into bestDest's owner entity ---
            // Re-read the list header/count each step: SetHome may mutate the
            // occupant roster, exactly as the retail index-by-8 loop tolerates.
            int j = 0;
            for (;;) {
                const ResidenceComponent::Family* occList = srcRes->residentSlots._data;
                if (!(j < occList->_size)) break;
                CitizenComponent* worker = occList->_data[j].Get();
                ComponentEntity* targetOwner = bestDest->_entity.Get();
                worker->SetHome(targetOwner, 0);   // 0x1401163d0 (tree: "AssignWorkplace")
                ++j;
            }

            // --- migrate stored goods: srcRes owner storage -> bestDest owner storage ---
            ComponentEntity*  srcOwner   = srcRes->_entity.Get();
            StorageComponent* srcStorage = srcOwner->FindComponent<StorageComponent>();
            ComponentEntity*  dstOwner   = bestDest->_entity.Get();
            StorageComponent* dstStorage = dstOwner->FindComponent<StorageComponent>();

            {   // both finds read with no test (cd80 / d000 / d150 on them)
                std::vector<std::pair<ComponentDescription*, int>> toSubtract;
                int itemCount = srcStorage->storageSlots._size;
                const StorageComponent::Item* items = srcStorage->storageSlots._data;
                for (int k = 0; k < itemCount; ++k) {
                    const StorageComponent::Item& entry = items[k];
                    ComponentDescription* descriptor = entry._resource;
                    int   avail      = entry._count;
                    int   reserved   = entry._reserve;
                    int   amount     = avail - reserved;
                    if (dstStorage->CanAdd(descriptor, amount)) {   // 0x14012cd80
                        dstStorage->AddItem(descriptor, amount);         // 0x14012d000
                        toSubtract.emplace_back(descriptor, amount);
                    }
                }
                for (auto& s : toSubtract)
                    srcStorage->SubtractItem(s.first, s.second);         // 0x14012d150
            }

            // --- retire the now-occupied destination from the empty set ---
            empties.erase(bestKey);
            ++relocations;
            if (empties.empty()) break;           // retail: local_b0 == 0
        }

        g_wpcRosterOnly = prevRoster;
    }
}
