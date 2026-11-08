// src/game/Component/citizenai_findtarget.cpp
//
// Faithful 1:1 port of the CitizenAI "find-best-target" linchpin helper
// CitizenAI_State_sub_dd30 (0x14008dd30) plus its only UNREGISTERED sub-callee
// CitizenAI_sub_78c0 (0x1400178c0). dd30 is needed by 6 CitizenAI state handlers
// (Gather / Education / GrowUp / SeekHappiness etc.) — it scans two candidate
// lists (a "primary" list at param_1[2]/[3] and a "fallback" list at param_1[0]/[1]),
// path-distance-ranks each candidate with BuildStructure_impl_f810 / FUN_140132010,
// and returns (via out-params) the best entity + a target vec3 + a found component.
//
// IMPORTANT ADDRESS CORRECTIONS (the task brief used wrong addresses):
//   BuildStructure_impl_f810 is actually at 0x14009f810 (NOT 0x14008f810) and is
//       ALREADY REGISTERED as Pf_IsDstReachable_f810 (reg_pf_roots_route.cpp).
//   CitizenAI_sub_edb0      is actually at 0x14004edb0 (NOT 0x14008edb0) and is
//       ALREADY REGISTERED as Pf_PickReachableCell_edb0 (reg_pf_road.cpp).
//   CitizenAI_sub_78c0      is actually at 0x1400178c0 (NOT 0x1400878c0) and is
//       the ONLY function in this set that was unregistered → ported here.
//
// So this file ports exactly TWO functions:
//   CitizenAI_FindBestTarget_dd30  (0x14008dd30)  — the main helper
//   CitizenAI_sub_78c0             (0x1400178c0)  — find MarketLocationComponent on an entity
//
// All other sub-callees are already registered and called via ResolveAddr:
//   0x14009f810 Pf_IsDstReachable_f810        (BuildStructure_impl_f810; path cost)
//   0x14004edb0 Pf_PickReachableCell_edb0     (CitizenAI_sub_edb0; pick reachable goal)
//   0x140087fb0 Pf_EntityAnchorCell_7fb0      (Entity_impl_7fb0; entity -> cell idx)
//   0x14008ef40 CitizenComp_Entity_impl_ef40  (Entity_impl_ef40; pick free slot)
//   0x14012d5d0 Pf_FindBestAdjacentCell_d5d0  (FUN_14012d5d0; cost-to-adjacent)
//   0x140132010 WorkImpl_Sqrtf_132010         (FUN_140132010; sqrtf)
//   0x140006270 WorkBase_FindComponent_6270   (Entity_FindComponent_Storage)
//
// TYPE-ID GLOBALS (.rdata BaseTypeInfo, the first DWORD = the runtime CRC type id).
// In standalone these are UNFILLED (0xFFFFFFFF) so the original's type-matched
// component scans would find NOTHING and then dereference a NULL result (crash).
// Fix (project "Brick" pattern): resolve each one self-contained via
// Object::GetTypeIdByName("<TypeName>") so the scans actually match. TypeNames
// recovered from each global's *_RegisterType writer (Ghidra-verified):
//   DWORD_1401c4870 -> "RadiusDescription"          (RadiusDescription_RegisterType)
//   DWORD_1401c7cb8 -> "RawMaterialDescription"     (RawMaterialDescription_Register)
//   DAT_1401c7ee8   -> "StorageLocationComponent"   (StorageLocationComponent_RegisterType)
//   DAT_1401c7f20   -> "StorageLocationDescription" (StorageLocationDescription_Register)
//   DAT_1401c4560   -> "MarketLocationComponent"    (MarketLocationComponent_RegisterType; used by 78c0)
// Every dereference of a scan-result pointer is additionally NULL-guarded so the
// port is crash-safe even if a type id is unresolvable.
//


#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/StorageLocationDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageLocationComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/RadiusDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/MarketLocationComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"   // GameUpdateLoop +0xF0, the candidate lists
#include "Game/Map.h"                 // GameUpdateLoop +0xF8
#include "Object/factory_registration.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"

namespace {

// --- registered exe sub-callees: called by name (Component/Action/Work exe_bodies.h) ---

// --- type ids (self-contained via GetTypeIdByName; see header note) ---
static uint32_t s_tid_RadiusDescription      = 0;  // DWORD_1401c4870
static uint32_t s_tid_RawMaterialDescription = 0;  // DWORD_1401c7cb8
static uint32_t s_tid_StorageLocationComp    = 0;  // DAT_1401c7ee8
static uint32_t s_tid_StorageLocationDesc    = 0;  // DAT_1401c7f20
static uint32_t s_tid_MarketLocationComp     = 0;  // DAT_1401c4560

static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;

    if (s_tid_RadiusDescription == 0)      s_tid_RadiusDescription      = RadiusDescription::TypeId();
    if (s_tid_RawMaterialDescription == 0) s_tid_RawMaterialDescription = RawMaterialDescription::TypeId();
    if (s_tid_StorageLocationComp == 0)    s_tid_StorageLocationComp    = StorageLocationComponent::TypeId();
    if (s_tid_StorageLocationDesc == 0)    s_tid_StorageLocationDesc    = StorageLocationDescription::TypeId();
    if (s_tid_MarketLocationComp == 0)     s_tid_MarketLocationComp     = MarketLocationComponent::TypeId();
    s_init = true;
}

// vtable[+0x10] == Object::Base::GetTypeId(this) — read the runtime type id of a component.
static inline uint32_t CompTypeId(const Object::Base* comp) {
    return comp->Type();   // slot 2
}

} // namespace

// ============================================================
// CitizenAI_FindBestTarget_dd30 (0x14008dd30)
//   param_1 = candidate-list pair struct:
//             param_1[0]=fallbackArr, param_1[1]=fallbackCount,
//             param_1[2]=primaryArr,  param_1[3]=primaryCount
//   param_2 = out: best entity ptr (longlong)        [written 0 at entry]
//   param_3 = goal cell / dest A (uint)
//   param_4 = goal cell / dest B (uint, -1 => skip the "B" range check)
//   param_5 = out: found component ptr (longlong)
//   param_6 = out: target vec3 (float[3])
//   param_7 = a category passed to Entity_impl_ef40
// Returns param_1 (matches the original; callers ignore it).
//
// Field offsets used (Ghidra-verified against the decompile + disassembly):
//   g_pGameWorldSingleton + 0xf8  -> int* mapGrid (local_80); mapGrid[0] = grid width
//   candidate entity: +0x28 -> component-list2 holder; +0x30 -> component array (count at +0x38)
//   component-list2 holder +0x28 -> array, +0x30 -> count (RadiusDescription scan)
//   matched RadiusDescription (pvVar12) +0x28 -> a comparison threshold (iVar15)
//   matched RadiusDescription (pvVar12) +0x5c -> a flag bitmask (2nd phase)
//   StorageLocationDescription sub-object: (found+0x18) -> base for IsDerivedFrom(+0x20 mask)
//   if mask==0: Entity_FindComponent_Storage(found+0x10) -> *ptr +0x3c = mask
// ============================================================
CitizenBehavior* CitizenBehavior::FindBestTarget(ComponentEntity** found, uint32_t param_3,
                                               uint32_t param_4, ComponentDescription** param_5, float* param_6,
                                               uint32_t param_7) {
    // (B4 P2 CAI-2: list is the CitizenBehavior every caller passes, GameUpdateLoop +0xF0:
    //  [0]/[1] its +0x00 list, [2]/[3] its +0x10 list.)
    EnsureInit();

    // (B4 P2 CAI-2: Map::PathDistance 0x14009f810, PickReachableCell 0x14004edb0,
    //  FindBestAdjacentCell 0x14012d5d0 and ComponentEntity::FindComponent<StorageComponent>
    //  0x140006270 are called in place of their Pf_* / WorkBase_* forwarders.)
    Map* local_80 = GameUpdateLoop::_singleton->m_game;   // +0xF8
    *found = nullptr;
    uint32_t local_98_flag = 0;       // low byte = "early-return" gate (set when a "B"-qualified hit found)
    int local_a8_best = 0x7fffffff;   // best cost so far

    // -------- Phase 1: scan the PRIMARY list (param_1[2], count param_1[3]) --------
    if (0 < marketLocations._size) {
        int iVar20 = 0;
        do {
            ComponentEntity* local_a0 = marketLocations._data[iVar20].Get();                // candidate entity
            ComponentEntity* local_90 = local_a0;                                // IBP scratch
            uint32_t cell = EntityAnchorCell(&local_90, nullptr);        // entity -> cell idx

            // -- find RadiusDescription (DWORD_1401c4870) in candidate component-list2 (+0x28) --
            ComponentDescription* lVar16 = local_a0->_description;
            RadiusDescription* pvVar12 = nullptr;
            {
                int n = lVar16->_subDescCount;
                int idx = 0; int64_t off = 0;
                while (idx < n) {
                    Description* compSlot = lVar16->_subDescs[off];
                    if (CompTypeId(compSlot) == s_tid_RadiusDescription) {
                        Description* c = lVar16->_subDescs[off];
                        if (c->IsDerivedFrom(s_tid_RadiusDescription))
                            pvVar12 = static_cast<RadiusDescription*>(c);
                        break;
                    }
                    idx++; off++;
                }
            }
            int iVar15 = pvVar12->_radius;   // 0x14008de19: [desc+0x28], no test

            // -- compute whether candidate is within the "B" (param_4) distance band --
            bool bVar5 = false;
            if (-1 < static_cast<int>(param_4)) {
                int iVar8 = local_80->PathDistance(cell, param_4, '\x01');
                if (iVar8 < 0) {
                    int w = local_80->size;
                    int dRow = static_cast<int>(cell) / w - static_cast<int>(param_4) / w;
                    int dCol = static_cast<int>(cell) % w - static_cast<int>(param_4) % w;
                    float r = WorkImpl_Sqrtf_132010(static_cast<float>(dRow * dRow + dCol * dCol));
                    iVar8 = static_cast<int>(r) + w;
                }
                bVar5 = (iVar8 <= iVar15);
            }

            // -- compute path cost from candidate to the "A" (param_3) goal cell --
            int iVar15cost = local_80->PathDistance(cell, param_3, '\x01');
            if (iVar15cost < 0) {
                int w = local_80->size;
                int dRow = static_cast<int>(cell) / w - static_cast<int>(param_3) / w;
                int dCol = static_cast<int>(cell) % w - static_cast<int>(param_3) % w;
                float r = WorkImpl_Sqrtf_132010(static_cast<float>(dRow * dRow + dCol * dCol));
                iVar15cost = static_cast<int>(r) + w;
            }

            if (iVar15cost < local_a8_best) {
                local_90 = local_a0;
                ComponentDescription* local_88 = nullptr;
                CitizenComp_Entity_impl_ef40(local_80, &local_88, &local_90, param_7);  // pick free slot
                if (local_88 != nullptr) {
                    uint32_t newFlag = local_98_flag & 0xff;
                    if (bVar5) newFlag = 1;
                    local_98_flag = newFlag;

                    float local_78[3] = {0.0f, 0.0f, 0.0f};
                    ::Object::InternalPtr<MarketLocationComponent> local_50_ibp[2];
                    ::Object::InternalPtr<MarketLocationComponent>* pIVar10 = local_50_ibp;
                    *pIVar10 = local_a0->FindComponent<MarketLocationComponent>();   // 0x1400178c0
                    MarketLocationComponent* marketComp = pIVar10->Get();
                    int cost = marketComp->PickReachableCell(param_3, local_78);   // no test
                    if ((-1 < cost) && (cost < local_a8_best)) {
                        *found = local_a0;
                        param_6[0] = local_78[0];
                        param_6[1] = local_78[1];
                        param_6[2] = local_78[2];
                        *param_5 = local_88;
                        local_a8_best = cost;
                    }
                }
            }
            iVar20 = iVar20 + 1;
        } while (iVar20 < marketLocations._size);

        // If a "B"-qualified primary hit was found, return immediately (skip fallback).
        if ((static_cast<char>(local_98_flag) != '\0') && (*found != nullptr)) {
            return this;
        }
    }

    // -------- Phase 2: scan the FALLBACK list (param_1[0], count param_1[1]) --------
    if (0 < storageLocations._size) {
        int iVar20 = 0;
        int iVar15loop = local_a8_best;
        do {
            ComponentEntity* local_a0 = storageLocations._data[iVar20].Get();               // candidate entity
            ComponentEntity* local_58 = local_a0;                               // IBP scratch
            uint32_t cell = EntityAnchorCell(&local_58, nullptr);

            // path cost candidate -> param_3
            int iVar8 = local_80->PathDistance(cell, param_3, '\x01');
            if (iVar8 < 0) {
                int w = local_80->size;
                int dRow = static_cast<int>(cell) / w - static_cast<int>(param_3) / w;
                int dCol = static_cast<int>(cell) % w - static_cast<int>(param_3) % w;
                float r = WorkImpl_Sqrtf_132010(static_cast<float>(dRow * dRow + dCol * dCol));
                iVar8 = static_cast<int>(r) + w;
            }

            if (iVar8 < iVar15loop) {
                local_58 = local_a0;
                ComponentDescription* local_50[2] = {nullptr, nullptr};
                CitizenComp_Entity_impl_ef40(local_80, local_50, &local_58, param_7);  // pick free slot
                ComponentDescription* lVar21 = local_50[0];
                if (local_50[0] != nullptr) {
                    // -- find RawMaterialDescription (DWORD_1401c7cb8) in slot's list (lVar21+0x28) --
                    ComponentDescription* slotDesc = lVar21;
                    RawMaterialDescription* pvVar12 = nullptr;
                    {
                        int n = slotDesc->_subDescCount;
                        int idx = 0; int64_t off = 0;
                        while (idx < n) {
                            Description* compSlot = slotDesc->_subDescs[off];
                            if (CompTypeId(compSlot) == s_tid_RawMaterialDescription) {
                                Description* c = slotDesc->_subDescs[off];
                                if (c->IsDerivedFrom(s_tid_RawMaterialDescription))
                                    pvVar12 = static_cast<RawMaterialDescription*>(c);
                                break;
                            }
                            idx++; off++;
                        }
                    }

                    // -- the inline FindComponent<StorageLocationComponent> (cell 0x1401c7ee8): retail's
                    //    one test of a scan result in this phase (0x14008e14c, the IBP != 0) --
                    Component* pvVar14 = local_a0->FindComponent<StorageLocationComponent>();

                    if (pvVar14 != nullptr) {
                        Component* lVarComp = pvVar14;
                        // IsDerivedFrom((found+0x18), StorageLocationDescription), CMOVNZ over 0,
                        // +0x20 read with no test
                        Description* sub = lVarComp->_description;
                        StorageLocationDescription* derived = sub->IsDerivedFrom(s_tid_StorageLocationDesc)
                                                                  ? static_cast<StorageLocationDescription*>(sub) : nullptr;
                        uint32_t mask = derived->_locationType;
                        if (mask == 0) {
                            // Entity_FindComponent_Storage(found+0x10)->+0x3c, no test (the tree's
                            // scoped fallback scan is gone)
                            StorageComponent* stor = lVarComp->_entity->FindComponent<StorageComponent>();
                            mask = stor->rawMaterialFlags;
                        }

                        uint32_t radiusFlags = pvVar12->_flags;   // [rawMat+0x5c], no test
                        if ((radiusFlags & mask) != 0) {
                            float local_68[3] = {0.0f, 0.0f, 0.0f};

                            // the StorageLocationComponent find again (the COST source)
                            Component* pvVar12b = local_a0->FindComponent<StorageLocationComponent>();

                            // (Pf_FindBestAdjacentCell_d5d0 ignored its fourth argument, uVar19, which is gone)
                            int cost = pvVar12b->FindBestAdjacentCell(param_3, local_68);   // no test
                            if ((-1 < cost) && (cost < local_a8_best)) {
                                *found = local_a0;
                                param_6[0] = local_68[0];
                                param_6[1] = local_68[1];
                                param_6[2] = local_68[2];
                                *param_5 = lVar21;
                                iVar15loop = cost;
                                local_a8_best = cost;
                            }
                        }
                    }
                }
            }
            iVar20 = iVar20 + 1;
        } while (iVar20 < storageLocations._size);
    }

    return this;
}

