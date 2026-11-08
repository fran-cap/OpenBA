// src/game/Component/citizenai_findhelpers.cpp
//
// Brick 3b — the DEEP find-helpers that unblock the State_Work / State_AdultTransition /
// State_ElderTransition handler trio. Faithful 1:1 ports transcribed from the Ghidra
// decompile, offset-by-offset, of:
//
//   0x14012ac30  FUN_14012ac30            -> ResidenceCheck_2ac30      (residence has free
//                                            capacity / is below cur-time threshold; bool)
//   0x14010db20  CitizenAI_helper_db20    -> CitizenAI_helper_db20     (State_Work: workplace
//                                            still valid for this citizen)
//   0x1400170b0  CitizenAI_helper_70b0    -> CitizenAI_helper_70b0     (sum of an int field
//                                            over a stride-0x10 record array; pure leaf)
//   0x14008ec70  Entity_impl_ec70         -> Entity_impl_ec70          (ElderTransition:
//                                            pick nearest valid residence w/ an InteractComponent)
//   0x14008e370  CitizenAI_helper_e370    -> CitizenAI_helper_e370     (AdultTransition:
//                                            find best work/storage/residence target — 4 phases)
//
// ADDRESS RE-VERIFICATION (this session, via get_function_callees on the real handler parents):
//   State_Work (0x14010d790)           -> CitizenAI_helper_db20 @ 14010db20  ✓
//   State_AdultTransition (0x14010cd80)-> CitizenAI_helper_e370 @ 14008e370  ✓ + FUN_14012ac30 @ 14012ac30 ✓
//   State_ElderTransition (0x14010d2c0)-> Entity_impl_ec70 @ 14008ec70       ✓ + FUN_14012ac30 @ 14012ac30 ✓
//   e370's only otherwise-UNregistered exe sub-callee = CitizenAI_helper_70b0 @ 1400170b0 (leaf).
//
// CLOSURE (every exe sub-callee is now registered — verified by grep of standalone/*.cpp):
//   0x1401158d0 CitizenComp_CheckWorkplaceValid (reg_component_gap_A.cpp)   [db20]
//   0x14009f810 Pf_IsDstReachable_f810          (reg_pf_roots_route.cpp)    [ec70, e370]
//   0x140008090 CitizenComp_Entity_FindComponent_Storage (reg_component_people.cpp) [ec70]
//   0x140006270 WorkBase_FindComponent_6270     (reg_work_base.cpp)         [e370]
//   0x140122e20 Pf_EntityResolvePos_2e20        (reg_pf_road.cpp)           [ec70, e370]
//   0x1400a00f0 WorkBase_ValidateCell_a00f0     (reg_work_base.cpp)         [ec70, e370]
//   0x140132010 WorkImpl_Sqrtf_132010           (reg_work_impl2.cpp)        [ec70, e370]
//   0x140087fb0 Pf_EntityAnchorCell_7fb0        (reg_pf_road.cpp)           [e370]
//   0x14008ef40 CitizenComp_Entity_impl_ef40    (reg_component_gap_A.cpp)   [e370]
//   0x14004edb0 Pf_PickReachableCell_edb0       (reg_pf_road.cpp)           [e370]
//   0x140008830 WorkBase_CalcDistance_8830      (reg_work_base.cpp)         [e370]
//   0x14009c480 Pf_ZoneRefresh_9c480            (reg_pf_roots_road.cpp)     [e370]
//   0x14012d5d0 Pf_FindBestAdjacentCell_d5d0    (reg_pf_roots_route.cpp)    [e370]
//   0x1400178c0 CitizenAI_sub_78c0              (citizenai_findtarget.cpp)  [e370]
//   ec70 -> ResidenceCheck_2ac30 and e370 -> CitizenAI_helper_70b0 are in THIS TU (direct call).
//
// KEY ABI NOTE (Ghidra-verified): Entity_impl_ef40 (0x14008ef40) IGNORES its first argument.
//   Its real signature is (ignoredArg1, int64_t* out, void* entityIBP, int category) — it only
//   reads param_3 (the IBP, via Entity_FindComponent_Storage(*ibp)) and param_4 (the category).
//   So in e370 sections A/D where Ghidra shows arg1 = a candidate/cell value (not the mapGrid that
//   sections B/C pass), that's harmless register noise; the port mirrors the decompile but arg1 is
//   don't-care.
//
// Type ids: each class's TypeId() (the cells below). Row 250 (B7 RES): the scans are
//   retail's (no null-slot test; the entity scans are ComponentEntity::FindComponent<T>()),
//   and the scan results are read as retail reads them: only the ef40 out-slot, the
//   preferred candidate and e370 Phase C's StorageLocationComponent are tested
//   (0x14008e707 `local_80 != 0`). TypeNames recovered from each global's *_Register/_RegisterType [DATA] writer:
//     DAT_1401c7d28  -> ResidenceDescription_Register     -> "ResidenceDescription"      [2ac30]
//     DAT_1401c7900  -> InteractComponent_RegisterType    -> "InteractComponent"         [ec70, e370]
//     DWORD_1401c7cb8-> RawMaterialDescription_Register   -> "RawMaterialDescription"    [e370]
//     DAT_1401c7ee8  -> StorageLocationComponent_RegisterType   -> "StorageLocationComponent"   [e370]
//     DAT_1401c7f20  -> StorageLocationDescription_Register     -> "StorageLocationDescription" [e370]
//     DWORD_1401c7cf0-> ResidenceComponent_RegisterType   -> "ResidenceComponent"        [e370]
//


#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/StorageLocationDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageLocationComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/ResidenceDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ResidenceComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/InteractComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/MarketLocationComponent.h"   // CitizenAI_sub_78c0's out pointer
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"              // GameUpdateLoop +0xF0, the candidate lists
#include "Game/Map.h"                            // GameUpdateLoop +0xF8
#include "Game/WeatherSimulation.h"              // GameUpdateLoop +0x130
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"

namespace {

// --- type ids (self-contained via GetTypeIdByName; see header note) ---
static uint32_t s_tid_ResidenceDescription = 0;  // DAT_1401c7d28
static uint32_t s_tid_InteractComponent    = 0;  // DAT_1401c7900
static uint32_t s_tid_RawMaterialDesc      = 0;  // DWORD_1401c7cb8
static uint32_t s_tid_StorageLocationComp  = 0;  // DAT_1401c7ee8
static uint32_t s_tid_StorageLocationDesc  = 0;  // DAT_1401c7f20
static uint32_t s_tid_ResidenceComponent   = 0;  // DWORD_1401c7cf0

// Resolves no door: it only caches the component type ids. The exe sub-callees are called
// by name (Component/Action/Work exe_bodies.h).
static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;

    if (s_tid_ResidenceDescription == 0) s_tid_ResidenceDescription = ResidenceDescription::TypeId();
    if (s_tid_InteractComponent == 0)    s_tid_InteractComponent    = InteractComponent::TypeId();
    if (s_tid_RawMaterialDesc == 0)      s_tid_RawMaterialDesc      = RawMaterialDescription::TypeId();
    if (s_tid_StorageLocationComp == 0)  s_tid_StorageLocationComp  = StorageLocationComponent::TypeId();
    if (s_tid_StorageLocationDesc == 0)  s_tid_StorageLocationDesc  = StorageLocationDescription::TypeId();
    if (s_tid_ResidenceComponent == 0)   s_tid_ResidenceComponent   = ResidenceComponent::TypeId();
    s_init = true;
}

// vtable[+0x10] == Object::Base::GetTypeId(this) — read the runtime type id of a component.
static inline uint32_t CompTypeId(const Object::Base* comp) {
    return comp->Type();   // slot 2
}

// Scan a comp-list2 (base+0x28 array, base+0x30 count) — the description/sub-component list of
// a slot. Entries are raw pointers, `[[entry]+0x10]` called with no test; the first type match
// is narrowed with IsDerivedFrom and the scan stops (retail's inline loop in 0x14008e370).
static Description* ScanCompList2(ComponentDescription* base, uint32_t tid) {
    int n = base->_subDescCount;
    Description** arr = base->_subDescs;
    for (int i = 0; i < n; ++i) {
        Description* comp = arr[i];
        if (CompTypeId(comp) == tid) {
            if (comp->IsDerivedFrom(tid))
                return comp;
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace

// ============================================================
// FUN_14012ac30 (0x14012ac30) — ResidenceCheck.
//   param_1 = a StorageComponent-ish object. obj = *(param_1+0x18) (its description holder).
//   Returns true if the residence's threshold field (desc+0x28) is <= the gameworld's current
//   value (gw+0x130 -> +0x18), OR there is still pending capacity (param_1+0x48 > 0).
// ============================================================
//   (B4 P2 CAI-2: the receiver is the ResidenceComponent every caller passes.)
bool ResidenceComponent::CheckHeating() {
    EnsureInit();
    Description* obj = _description;
    float cur = GameUpdateLoop::_singleton->m_weather->temperature;   // +0x130 -> +0x18
    // 0x14012ac5c-0x14012ac74: IsDerivedFrom, CMOVNZ over 0, [desc+0x28] with no test.
    ResidenceDescription* desc = obj->IsDerivedFrom(s_tid_ResidenceDescription)
                                     ? static_cast<ResidenceDescription*>(obj) : nullptr;
    float thresh = desc->_coldThreshold;
    if (thresh <= cur) return true;
    return 0 < heatRemaining._ticks;
}

// ============================================================
// CitizenAIComponent::CanReachHome (0x14010db20; tree: "CitizenAI_helper_db20") — the
//   State_Work check. No home -> false (retail's RAX keeps the citizen pointer above the
//   cleared low byte; the caller reads the low byte). Else citizen->CanReach(home)
//   (0x1401158d0).
// ============================================================
uint64_t CitizenAIComponent::CanReachHome() {
    EnsureInit();
    if (citizen->home == nullptr) {
        return 0;
    }
    // The caller reads the low byte (the body's char).
    return static_cast<uint8_t>(citizen->CanReach(citizen->home.Get()));   // 0x1401158d0
}

// ============================================================
// CitizenAI_helper_70b0 (0x1400170b0) — pure leaf: sum the int field at +8 of each stride-0x10
//   record in the array at (param_1+0x50), over (param_1+0x58) records. Returns the 32-bit sum.
// ============================================================
//   (B4 P2 CAI-2: the record array is ResidenceComponent::residentSlots, a Family each.)
uint64_t CitizenAI_helper_70b0(const ResidenceComponent* param_1) {
    uint32_t count = static_cast<uint32_t>(param_1->residentSlots._size);
    const ResidenceComponent::Family* arr = param_1->residentSlots._data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        sum += arr[i]._size;
    }
    return static_cast<uint64_t>(sum);
}

// Path cost from cell -> goal: f810 (real A* cost), else euclidean-on-grid fallback (sqrt+width).
// (B4 P2 CAI-2: Map::PathDistance 0x14009f810 called in place of its Pf_IsDstReachable_f810
//  forwarder.)
static int CellCost(Map* map, uint32_t cell, uint32_t goal) {
    int c = map->PathDistance(cell, goal, '\x01');
    if (c < 0) {
        int w = map->size;
        int dRow = static_cast<int>(cell) / w - static_cast<int>(goal) / w;
        int dCol = static_cast<int>(cell) % w - static_cast<int>(goal) % w;
        float r = WorkImpl_Sqrtf_132010(static_cast<float>(dRow * dRow + dCol * dCol));
        c = static_cast<int>(r) + w;
    }
    return c;
}

// World pos (vec3) -> grid cell index, clamped to [0, w-1] in each axis. (cell = row*w + col)
static uint32_t WorldToCell(const Map* map, const float* vec) {
    int w = map->size;
    int col = static_cast<int>(vec[0] + static_cast<float>(w >> 1));
    int row = static_cast<int>(vec[1] + static_cast<float>(w >> 1));
    if (col < 0) col = 0;
    if (w <= col) col = w - 1;
    if (row < 0) row = 0;
    if (w <= row) row = w - 1;
    return static_cast<uint32_t>(w * row + col);
}

// ============================================================
// Entity_impl_ec70 (0x14008ec70) — State_ElderTransition find-target.
//   Scan the entity list at (param_1+0x20 array, param_1+0x28 count); for each candidate find its
//   InteractComponent, resolve its world position -> cell, rank by path cost to goal (param_3),
//   keep the cheapest whose ValidateCell passes AND whose Storage passes ResidenceCheck_2ac30.
//   Out: *param_2 = best entity, param_4 = best target vec3. Returns param_2.
// ============================================================
//   (B4 P2 CAI-2: param_1 is the CitizenBehavior every caller passes, GameUpdateLoop +0xF0;
//    the list is its +0x20 entity array.)
ComponentEntity** CitizenBehavior::FindElderResidence(ComponentEntity** found, uint32_t param_3, float* param_4) {
    EnsureInit();
    Map* map = GameUpdateLoop::_singleton->m_game;   // +0xF8
    Map* mapGrid = map;
    int best = 0x7fffffff;
    *found = nullptr;
    if (residences._size < 1) return found;

    int idx = 0;
    do {
        ComponentEntity* cand = residences._data[idx].Get();

        // the inline FindComponent<InteractComponent> (cell 0x1401c7900), wrapped in an IBP
        InteractComponent* interact = cand->FindComponent<InteractComponent>();

        Vector3Slot vec;
        interact->NodePosition(&vec.v, 0, 0, nullptr);   // 0x140122e20
        uint32_t cell = WorldToCell(map, &vec.v.x);
        int cost = CellCost(map, cell, param_3);

        if (cost < best) {
            uint64_t valid = mapGrid->ValidateCell(PathBits::Walk, param_3, static_cast<int>(cell));
            if (static_cast<char>(valid) != '\0') {
                ComponentEntity* self_00 = residences._data[idx].Get();
                // 0x140008090 (was its CitizenComp_Entity_FindComponent_Storage forwarder)
                ResidenceComponent* storComp = self_00->FindComponent<ResidenceComponent>();
                bool ok = storComp->CheckHeating();   // CheckPtr'd, no test
                if (ok && (-1 < cost)) {
                    *found = cand;
                    param_4[0] = vec.v.x;
                    param_4[1] = vec.v.y;
                    param_4[2] = vec.v.z;
                    best = cost;
                }
            }
        }
        ++idx;
    } while (idx < residences._size);
    return found;
}

// ============================================================
// CitizenAI_helper_e370 (0x14008e370) — State_AdultTransition find-best-target. 4 phases:
//   A) a preferred candidate (*param_7): if its anchor cell validates, take it (cost = CalcDistance).
//   B) a MARKET candidate list (param_1[2] array / param_1[3] count): rank by path cost, then by
//      MarketLocationComponent reachability (sub_78c0 + edb0).
//   C) a RAW-MATERIAL/STORAGE candidate list (param_1[0]/[1]): match RawMaterialDescription on the
//      ef40-picked production slot, StorageLocationComponent on the entity, flag-mask compatibility
//      (RadiusDescription+0x5c & storage mask), then cost via FindBestAdjacentCell (d5d0).
//   D) a RESIDENCE candidate list (param_1[4]/[5]): skip residences whose occupancy sum (70b0) >= 1,
//      else use the InteractComponent position, refresh the zone (9c480) and check cell-flag
//      compatibility before taking the entity (ef40 pick).
//   Out: *param_2 = best entity, *param_4 = best found-component, param_5 = best vec3.
//   param_6 is an unused incoming reg the original repurposes as the running "best cost".
//   Returns param_2 (callers ignore the return).
// ============================================================
ComponentEntity** CitizenBehavior::FindAdultTarget(ComponentEntity** found, uint32_t param_3,
                                        ComponentDescription** param_4, float* param_5, uint64_t /*param_6*/,
                                        ComponentEntity** origP7) {
    // (B4 P2 CAI-2: list is the CitizenBehavior, GameUpdateLoop +0xF0: [0]/[1] its +0x00
    //  list, [2]/[3] +0x10, [4]/[5] +0x20.)
    EnsureInit();
    Map* map = GameUpdateLoop::_singleton->m_game;   // +0xF8
    Map* mapGrid = map;
    ComponentEntity** ret = found;
    *found = nullptr;
    int best = 0x7fffffff;

    // -------- Phase A: the preferred candidate (*param_7) --------
    ComponentEntity* preferred = *origP7;
    if (preferred != nullptr) {
        float vec[4] = {0, 0, 0, 0};
        ComponentEntity* candIBP = preferred;
        uint32_t cell = static_cast<uint32_t>(EntityAnchorCell(&candIBP, vec));
        uint64_t valid = mapGrid->ValidateCell(PathBits::Walk, param_3, static_cast<int>(cell));
        if (static_cast<char>(valid) != '\0') {
            int dist = mapGrid->CalcDistance(cell, param_3);
            ComponentEntity* candA = *origP7;
            ComponentDescription* outSlot = nullptr;
            CitizenComp_Entity_impl_ef40(candA, &outSlot, &candA, 0);
            if (outSlot != nullptr) {
                *found = *origP7;
                param_5[0] = vec[0];
                param_5[1] = vec[1];
                param_5[2] = vec[2];
                best = dist;
            }
        }
    }

    // -------- Phase B: market candidate list (param_1[2] / count param_1[3]) --------
    if (0 < marketLocations._size) {
        int i = 0;
        do {
            ComponentEntity* cand = marketLocations._data[i].Get();
            ComponentEntity* candIBP = cand;
            uint32_t cell = static_cast<uint32_t>(EntityAnchorCell(&candIBP, nullptr));
            int cost = CellCost(map, cell, param_3);
            if (cost < best) {
                ComponentEntity* candE = cand;
                ComponentDescription* outSlot = nullptr;
                CitizenComp_Entity_impl_ef40(map, &outSlot, &candE, 0);
                ComponentDescription* slot = outSlot;
                if (outSlot != nullptr) {
                    float vec[4] = {0, 0, 0, 0};
                    ::Object::InternalPtr<MarketLocationComponent> marketIBP[2];
                    ::Object::InternalPtr<MarketLocationComponent>* mp = marketIBP;
                    *mp = cand->FindComponent<MarketLocationComponent>();   // 0x1400178c0
                    MarketLocationComponent* marketComp = mp->Get();
                    // 0x14004edb0 (was its Pf_PickReachableCell_edb0 forwarder), no test
                    int mcost = marketComp->PickReachableCell(param_3, vec);
                    if ((-1 < mcost) && (mcost < best)) {
                        *found = cand;
                        param_5[0] = vec[0];
                        param_5[1] = vec[1];
                        param_5[2] = vec[2];
                        *param_4 = slot;
                        best = mcost;
                    }
                }
            }
            ++i;
        } while (i < marketLocations._size);
    }

    // -------- Phase C: raw-material / storage candidate list (param_1[0] / count param_1[1]) --------
    if (0 < storageLocations._size) {
        int i = 0;
        do {
            ComponentEntity* cand = storageLocations._data[i].Get();
            ComponentEntity* candIBP = cand;
            uint32_t cell = static_cast<uint32_t>(EntityAnchorCell(&candIBP, nullptr));
            int cost = CellCost(map, cell, param_3);
            if (cost < best) {
                ComponentEntity* candE = cand;
                ComponentDescription* slotOut[2] = {nullptr, nullptr};
                CitizenComp_Entity_impl_ef40(map, slotOut, &candE, 0);
                ComponentDescription* slot = slotOut[0];
                if (slot != nullptr) {
                    // RawMaterialDescription on the ef40-picked slot's comp-list2 (slot+0x28 / +0x30)
                    Description* rawMat = ScanCompList2(
                        slot, s_tid_RawMaterialDesc);

                    // StorageLocationComponent on the candidate entity: retail's ONE test of a
                    // scan result in this phase (0x14008e707, `local_80 != 0`).
                    Component* storComp = cand->FindComponent<StorageLocationComponent>();

                    if (storComp != nullptr) {
                        Component* storC = storComp;
                        // mask from StorageLocationDescription sub-object at storC+0x18, +0x20:
                        // IsDerivedFrom, CMOVNZ over 0, [derived+0x20] with no test.
                        Description* sub = storC->_description;
                        StorageLocationDescription* derived = sub->IsDerivedFrom(s_tid_StorageLocationDesc)
                                                                  ? static_cast<StorageLocationDescription*>(sub) : nullptr;
                        uint32_t mask = derived->_locationType;
                        if (mask == 0) {
                            // 0x140006270 (was its WorkBase_FindComponent_6270 forwarder); +0x3c
                            // read with no test. (The tree's scoped fallback scan is gone.)
                            StorageComponent* s = storC->_entity->FindComponent<StorageComponent>();
                            mask = s->rawMaterialFlags;
                        }
                        uint32_t radiusFlags = static_cast<RawMaterialDescription*>(rawMat)->_flags;   // [rawMat+0x5c], no test
                        if ((radiusFlags & mask) != 0) {
                            float vec[4] = {0, 0, 0, 0};
                            // 2nd StorageLocationComponent scan over the entity (cost source)
                            Component* costComp = cand->FindComponent<StorageLocationComponent>();
                            // 0x14012d5d0 (was its Pf_FindBestAdjacentCell_d5d0 forwarder), no test
                            int dcost = costComp->FindBestAdjacentCell(param_3, vec);
                            if ((-1 < dcost) && (dcost < best)) {
                                param_5[0] = vec[0];
                                param_5[1] = vec[1];
                                *found = cand;
                                param_5[2] = vec[2];
                                *param_4 = slot;
                                best = dcost;
                            }
                        }
                    }
                }
            }
            ++i;
        } while (i < storageLocations._size);
    }

    // -------- Phase D: residence candidate list (param_1[4] / count param_1[5]) --------
    if (0 < residences._size) {
        int i = 0;
        do {
            ComponentEntity* cand = residences._data[i].Get();

            // ResidenceComponent on the entity (comp-list1 at cand+0x30 / +0x38)
            ResidenceComponent* resComp = cand->FindComponent<ResidenceComponent>();
            uint64_t occ = CitizenAI_helper_70b0(resComp);

            if (static_cast<int>(occ) < 1) {
                // InteractComponent on the entity -> world position
                InteractComponent* interact = cand->FindComponent<InteractComponent>();

                Vector3Slot vec;
                interact->NodePosition(&vec.v, 0, 0, nullptr);   // 0x140122e20
                uint32_t cell = WorldToCell(map, &vec.v.x);

                // path cost (Ghidra's uVar30/uVar22, cell/goal or %w/dist², were the extra
                // ZoneRefresh "arguments"; 0x14009c480 takes RCX only and they are gone)
                int cost = map->PathDistance(cell, param_3, '\x01');   // 0x14009f810
                if (cost < 0) {
                    int w = map->size;
                    int dRow = static_cast<int>(cell) / w - static_cast<int>(param_3) / w;
                    int dCol = static_cast<int>(cell) % w - static_cast<int>(param_3) % w;
                    uint32_t d2 = static_cast<uint32_t>(dRow * dRow + dCol * dCol);
                    float r = WorkImpl_Sqrtf_132010(static_cast<float>(static_cast<int>(d2)));
                    cost = static_cast<int>(r) + w;
                }
                if (cost < best) {
                    map->FillZones();   // 0x14009c480 (was its Pf_ZoneRefresh_9c480 forwarder)
                    MapCell* cellArr = map->cells.GetData();   // +0x80
                    MapCell* cGoal = &cellArr[static_cast<int>(param_3)];
                    MapCell* cCand = &cellArr[static_cast<int>(cell)];
                    uint32_t fGoal = 1u << (static_cast<uint8_t>(cGoal->terrainType) & 0x1f);
                    uint32_t fCand = 1u << (static_cast<uint8_t>(cCand->terrainType) & 0x1f);
                    uint32_t both = fCand | fGoal;
                    uint32_t flags = both | Bits(PathBits::Walk);
                    bool proceed = true;
                    if (((flags & fGoal) != 0) && ((flags & fCand) != 0)) {
                        if ((both & Bits(PathBits::Zoneless)) == 0) {
                            uint16_t gz = cGoal->zone;
                            if (gz == 0) {
                                proceed = false;
                            } else {
                                uint16_t cz = cCand->zone;
                                if (cz == 0 || gz != cz) proceed = false;
                            }
                        }
                        if (proceed) {
                            ComponentEntity* entHolder[2] = {cand, nullptr};
                            ComponentDescription* outSlot = nullptr;
                            CitizenComp_Entity_impl_ef40(reinterpret_cast<void*>(static_cast<uintptr_t>(flags)), &outSlot, entHolder, 0);   // LEFT: B6 register retail leaves unset: at this site (0x14008eb7a in 0x14008e370) retail loads RDX / R8 / R9D and never RCX (its low word still holds the zone compared at 0x14008eb5e); ef40 (exe_bodies.h:144) ignores param_1 and its nine other callers pass pointers through the const void*, so the tree's integer here has no declared type that fits all ten without a cast or a changed value  // CAST-OK: ptr_int (S4 ruling)
                            if ((outSlot != nullptr) && (-1 < cost)) {
                                *found = cand;
                                param_5[0] = vec.v.x;
                                param_5[1] = vec.v.y;
                                param_5[2] = vec.v.z;
                                *param_4 = outSlot;
                                best = cost;
                            }
                        }
                    }
                }
            }
            ++i;
        } while (i < residences._size);
    }

    return ret;
}

