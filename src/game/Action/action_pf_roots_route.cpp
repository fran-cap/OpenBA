// src/game/Action/action_pf_roots_route.cpp
// Wave 3 (Action) / Cluster A — routing/storage-selection ROOT callers.
//
// Faithful 1:1 ports of the four high-level routing roots that drive region routing and
// storage-drop selection. None is runtime-exercised on the autostart path, so a wrong port
// is undetectable silent corruption (strictly worse than leaving the addr unregistered).
// Transcribed literally from the Ghidra decompile; cross-checked against the disassembly.
#include "Action/exe_bodies.h"
//
// Sub-callees are called by name (B3R). Registration state of each sub-callee (confirmed from disassembly CALL targets):
//   0x1400a00f0  WorkBase_ValidateCell      — REG (reg_work_base)
//   0x14009d0a0  Pf_RegionSpiral            — REG (reg_pf_geom)  [Ghidra mislabels as d0a0]
//   0x1400a5ac0  Pf_RegionPairLookup        — REG (reg_pf_geom)
//   0x1400a8ba0  Pf_RegionPathLength        — REG (reg_pf_region; its dep 1dc00 now reg)
//   0x1400a5b30  Pf_RegionPairCacheInsert   — REG (reg_pf_region)
//   0x14012c560  Pf_ResourceLimitFlagTest   — REG (reg_pf_ctors)  [12cd80 dep]
//   0x140123070  Pf_RecursiveChildSum       — REG (reg_pf_ctors)
//   0x140122e20  Pf_EntityResolvePos        — REG (reg_pf_road)
//   0x14009c480  ZoneRefresh                — REG (reg_pf_roots_road; the pathfinding wave landed it)
//   0x140008830  WorkBase_CalcDistance      — REG (reg_work_base)
//   0x14009f3e0  Reimpl_MapGetTerrainPos    — REG (address_registry M5 5c)
//   0x140087fb0  Pf_EntityAnchorCell        — REG (reg_pf_road)
//   0x14004edb0  Pf_PickReachableCell       — REG (reg_pf_road)
//   0x140004680  FloorComponent_FindResData — REG (reg_component_nature)  [member-ABI FindComp]
//   0x140002ed0  WorkBase_FindComponent_2ed0 — REG (reg_work_base)  [Position component]
//   0x140132010  sqrtf                      — REG (reg_work_impl2)
//
// CLOSURE for the registrar (reg_pf_roots_route.cpp): ALL FOUR closure-satisfied → REGISTER.
//   * 0x14009f810  SATISFIED (a8ba0/a5ac0/a5b30/9d0a0/a00f0 all reg)
//   * 0x14012cd80  SATISFIED (12c560 + cf10 QueryCount + IsDerivedFrom)
//   * 0x14012d5d0  SATISFIED (9c480 ZoneRefresh now reg in reg_pf_roots_road)
//   * 0x14008d590  SATISFIED (its only code children are 12cd80/9f810/12d5d0 [this file] +
//                  87fb0/4edb0 [reg]; 12d5d0's closure now closed)
// NOTE: the "deferred" comment in reg_work_accessor.cpp re: 9c480 is STALE — it was landed
//       by the pathfinding wave (reg_pf_roots_road.cpp).
//
// ---- struct offsets reverse-engineered (for the lead's verification) ----
// g_pGameWorldSingleton + 0xf8 → the Map object (`piVar3`/`local_60`), an int*:
//   *(Map)      = mapStride (grid width in cells)
//   *(Map+0x20) = fine-cell array base (stride 0x30): +0x18 int regionId, +0x26 short zoneId/owner,
//                 +0x29 byte passability-flag (tested via `1<<(flag&0x1f) & maskE1f/E07`).
// StorageComponent (param_1 of 12cd80, param "self"): +0x38 int capacity-limit;
//   component-list at +0x28 (ptr array, stride 8) / +0x30 int count; matched storage's
//   ResourceLimit entry at +0x48 (int per-unit size) — DWORD_1401c7cb8 = RawMaterialDescription tid.
// ComponentEntity (8d590 local_c8): +0x30 component ptr-array, +0x38 int count.
//   Its components matched by GetTypeId (vtable+0x10) == DAT type-id, then IsDerivedFrom-narrowed.
//   Type ids: DAT_1401c7ee8 (route/path comp), DAT_1401c4560 (?), DWORD_1401c7e78 (StorageComponent),
//   DAT_1401c7f20 (BuildComponent-ish, +0x20 flags, +0x3c flags via residence +0x10 inner), and
//   pvVar19 (the drop-source comp on param_4 list, +0x5c flag mask vs storage +0x3c/+0x20).
//   Storage accept-count fast path (param_6==0): walk local_a8+0x28 pair array (stride 0x10,
//   key=param_4 at +0, value=+8 minus reserved +0xc) for headroom vs param_5.
// 8d590 caller args: param_1 = a pair of entity-list roots (param_1 and param_1+2, each
//   {ptr@0, int count@+8}); param_2 = out best-entity slot; param_3 = goal cell; param_4 =
//   source comp/list ptr; param_5 = qty; param_6 = use-capacity-check flag; param_7 = out vec3;
//   param_8 = exclude-entity IBP.

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Action/PathSearch.h"   // RegionGraph, RegionPairCache
#include "Work/exe_bodies.h"            // WorkBase_* (a00f0, 8830, 2ed0)
#include "Game/exe_bodies.h"            // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"         // kRdata_140172114
#include "Component/exe_bodies.h"       // FloorComponent_FindResData
#include "system/MemoryAllocator.h"     // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Object/factory_registration.h"   // Object::GetTypeIdByName (.rdata type-id fallback)
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/StorageLocationComponent.h"   // ENDGAME B.3.8 CB3: StorageLocationComponent::TypeId()
#include "Component/MarketLocationComponent.h"   // ENDGAME B.3.8 CB3: MarketLocationComponent::TypeId()
#include "Component/StorageComponent.h"   // ENDGAME B.3.8 CB3: StorageComponent::TypeId()
#include "Component/StorageLocationDescription.h"   // ENDGAME B.3.8 CB3: StorageLocationDescription::TypeId()
#include "Component/ComponentDescription.h"
#include "Component/InteractComponent.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"   // CitizenBehavior::field_0x00 / field_0x10 (PickBestDropTarget)
#include "stress_harness.h"   // PFBENCH: PfMainTimer (ph_pathdistance_ms; stress harness only)

namespace {

// The inline sub-description scan retail repeats (0x14008d5fd-0x14008d65b for the
// RawMaterialDescription, cell 0x1401c7cb8): each entry's Type() through [RCX] with no
// null test, the first match narrowed with IsDerivedFrom, a miss yields 0 (B6 14d G52:
// the old null-slot skip is gone; the component scans are ComponentEntity::FindComponent<T>).
template <class T>
const T* FindSubDescription(const ComponentDescription* material) {
    for (int32_t i = 0; i < material->_subDescCount; ++i) {
        Description* d = material->_subDescs[i];
        if (d->Type() == T::TypeId())
            return d->IsDerivedFrom(T::TypeId()) ? static_cast<const T*>(d) : nullptr;
    }
    return nullptr;
}

} // namespace

// ============================================================
// 0x14009f810 — Map::PathDistance(srcCell, dstCell, useCache) -> length
// ============================================================
// Region-routing reachability/length between two map cells. First ValidateCell (a00f0,
// mask 0xe1f) gates both cells; resolve each cell's region id (MapCell::regionId); if either
// is unset, lazily compute it via the region spiral (9d0a0). If both resolved and equal →
// trivially reachable (return 1). Else look up the cached region-pair length (a5ac0); on a
// miss (-1) run the full region A* (a8ba0) and, if useCache, store the result back (a5b30).
// Returns the path length (or -1 unreachable).
//
// Faithfulness: retail passes the original dstCell to a8ba0 as its 4th arg, which a8ba0 only
// hands on to 0x14001dc00 (never read).
int Map::PathDistance(uint32_t param_2, uint32_t param_3, char param_4) {
    PfMainTimer pfTimer(PF_MAIN_PATHDISTANCE);   // [PFBENCH] one g_pfProfOn branch when the harness is off
    // LEFT: 0x1400a00f0's C declaration (Work/exe_bodies.h, WORK-a's header) takes the
    // Map as int*.
    uint64_t uVar4 = ValidateCell(0xe1f, param_2, (int)param_3);   // 0x1400a00f0
    if ((char)uVar4 != '\0') {
        uint32_t uVar1 = static_cast<uint32_t>(cells.GetData()[(int)param_2].regionId);
        uint32_t uVar2 = static_cast<uint32_t>(cells.GetData()[(int)param_3].regionId);
        if ((int)uVar1 < 0) {
            uVar1 = (uint32_t)NearestRegionId((int)param_2);   // 0x14009d0a0
        }
        if ((int)uVar2 < 0) {
            uVar2 = (uint32_t)NearestRegionId((int)param_3);   // 0x14009d0a0
        }
        int iVar3;
        if (((int)uVar1 < 0) || ((int)uVar2 < 0)) {
            iVar3 = -1;
        } else if (uVar1 == uVar2) {
            iVar3 = 1;
        } else {
            RegionPairCache* cache = regionPairCache;
            iVar3 = (int)cache->Find((int)uVar1, (int)uVar2);   // 0x1400a5ac0
            if (iVar3 < 0) {
                iVar3 = regionGraph->PathLength((int)uVar1, uVar2);   // 0x1400a8ba0
                if (param_4 != '\0') {
                    cache->Store((int)uVar1, (int)uVar2, (uint32_t)iVar3);   // 0x1400a5b30
                }
            }
        }
        return iVar3;
    }
    return -1;
}

// ============================================================
// 0x14012cd80 — StorageComponent::CanAdd(material, count)
// ============================================================
// The storage takes the material (0x14012c560) and has room for `count` more units: the
// material's RawMaterialDescription weight times count plus the stored weight
// (StorageComponent::QueryCount 0x14012cf10) fits the capacity.
bool StorageComponent::CanAdd(const ComponentDescription* material, int count) {
    if (!AcceptsMaterial(material)) {   // 0x14012c560
        return false;
    }
    const int stored = QueryCount();   // 0x14012cf10
    const RawMaterialDescription* raw = FindSubDescription<RawMaterialDescription>(material);
    const int unitWeight = raw->_weight;   // 0x14012cd80 reads +0x48 through the narrowed (or 0) pointer with no test (B6 14d G54)
    const int need = unitWeight * count + stored;
    return need <= capacity;
}

// ============================================================
// 0x14012d5d0 — the best cell next to a target component's entity, and its cost
// ============================================================
// Pick the best build/move cell. Two modes by whether the entity has an InteractComponent:
//  (A) No InteractComponent (a placement target): expand the MapComponent footprint
//      (tileX0/tileY0 = x/y, tileX1/tileY1 = w/h) by one cell, and for each passable cell
//      (terrain mask 0xe07) refresh the zones (9c480), then validate zone-match against the
//      goal cell (mask 0xe1f, MapCell::zone), keep the min-distance (8830) cell. Convert the
//      winning cell to world coords (9f3e0) into outPos.
//  (B) An InteractComponent: iterate its nodes (123070 count, 122e20 each), snap each to a
//      cell, ValidateCell (a00f0), then PathDistance (9f810) for the cost (falling back to
//      euclidean if unreachable), keep the min, copy its world position.
// Returns the cost, or -1.
//
// Retail's fourth argument (R9) is never read: the body's first use of that register
// is a store (the cell array base).
int Component::FindBestAdjacentCell(uint32_t param_2, float* param_3) const {
    ComponentEntity* entity = _entity.Get();
    Map* map = GameUpdateLoop::_singleton->m_game;
    // Two CONTIGUOUS Vector3 stack slots (the engine writers fill all 3 via one base
    // pointer): posVec = the node position / GridToWorld input, worldVec = its output.
    Math::Vector3 posVec;
    float worldVec[3] = {0, 0, 0};
    InteractComponent* interact = entity->FindComponent<InteractComponent>();   // 0x140002ed0
    int iVar15;
    if (interact == nullptr) {
        MapComponent* footprint = entity->FindComponent<MapComponent>();   // 0x140004680
        uint64_t uVar9 = 0xffffffff;
        int iVar5 = footprint->tileY0 + -1;
        int iVar4 = footprint->tileX0 + -1;
        uint32_t uVar16 = (int)footprint->tileX1 + 2;
        iVar15 = 0x7fffffff;
        int iVar6 = footprint->tileY1 + 2 + iVar5;
        uint32_t local_res8 = 0xffffffff;
        if (iVar5 <= iVar6) {
            do {
                if ((-1 < iVar5) && (iVar5 < map->size)) {
                    for (int iVar13 = iVar4; iVar13 < (int)(iVar4 + uVar16); iVar13 = iVar13 + 1) {
                        if ((-1 < iVar13) && (iVar13 < map->size)) {
                            uint32_t uVar14 = map->size * iVar5 + iVar13;
                            int64_t lVar10b = (int64_t)(int)uVar14;
                            if ((1 << (map->cells.GetData()[lVar10b].terrainType & 0x1f) & 0xe07U) != 0) {
                                // 0x14009c480, RCX only (Ghidra's uVar18 carried its phantom
                                // 3rd argument and is gone with it)
                                map->FillZones();
                                const MapCell* cells = map->cells.GetData();
                                const MapCell& goal = cells[(int64_t)(int)param_2];
                                const MapCell& cell = cells[lVar10b];
                                uint32_t uVar17 = 1 << (goal.terrainType & 0x1f);
                                uint32_t uVar7 = 1 << (cell.terrainType & 0x1f);
                                uint32_t uVar11 = uVar7 | uVar17;
                                uint32_t uVar12 = uVar11 | 0xe1f;
                                if ((((uVar12 & uVar7) != 0) && ((uVar12 & uVar17) != 0)) &&
                                    (((uVar11 & 0x1e0) != 0 ||
                                      (((cell.zone != 0 && goal.zone != 0) &&
                                        (cell.zone == goal.zone)))))) {
                                    // LEFT: 0x140008830's C declaration (Work/exe_bodies.h,
                                    // WORK-a's header) takes the Map as int*.
                                    int iVar8 = map->CalcDistance(uVar14, param_2);   // 0x140008830
                                    if (iVar8 < iVar15) {
                                        uVar9 = (uint64_t)uVar14;
                                        iVar15 = iVar8;
                                        local_res8 = uVar14;
                                        goto LAB_14012d8ce;
                                    }
                                }
                                uVar9 = (uint64_t)local_res8;
                            }
                        }
LAB_14012d8ce:;
                    }
                }
                iVar5 = iVar5 + 1;
            } while (iVar5 <= iVar6);
            iVar6 = (int)uVar9;
            if (-1 < iVar6) {
                posVec.y = (float)(map->size >> 1) - kRdata_140172114;
                posVec.x = (float)(iVar6 % map->size) - posVec.y;
                posVec.y = (float)(iVar6 / map->size) - posVec.y;
                posVec.z = 0.0f;
                map->GetTerrainPos(worldVec, &posVec.x, '\0', nullptr);   // 0x14009f3e0
                *param_3 = worldVec[0];
                param_3[1] = worldVec[1];
                param_3[2] = worldVec[2];
                return iVar15;
            }
        }
        iVar15 = -1;
    } else {
        int iVar6 = 0x7fffffff;
        int iVar4 = interact->NodeCount(0);   // 0x140123070
        iVar15 = 0;
        if (0 < iVar4) {
            do {
                interact->NodePosition(&posVec, 0, iVar15, nullptr);   // 0x140122e20
                int iVar13 = map->size;
                int iVar4b = (int)(posVec.x + (float)(iVar13 >> 1));
                int iVar5 = (int)(posVec.y + (float)(iVar13 >> 1));
                if (iVar4b < 0) iVar4b = 0;
                if (iVar13 <= iVar4b) iVar4b = iVar13 + -1;
                if (iVar5 < 0) iVar5 = 0;
                if (iVar13 <= iVar5) iVar5 = iVar13 + -1;
                uint32_t uVar16 = iVar13 * iVar5 + iVar4b;
                // LEFT: 0x1400a00f0's C declaration (Work/exe_bodies.h) takes the Map as int*.
                uint64_t uVar9 = map->ValidateCell(0xe1f, param_2, (int)uVar16);   // 0x1400a00f0
                if ((char)uVar9 != '\0') {
                    int iVar4c = map->PathDistance(param_2, uVar16, '\x01');   // 0x14009f810
                    if (iVar4c < 0) {
                        int iVar4d = map->size;
                        // uVar16 = size * iVar5 + iVar4b with both clamped to [0, size), so
                        // retail's `uVar16 / size` and `% size` are iVar5 and iVar4b (the map
                        // size does not change in between): carried, not divided.
                        int iVar5b = (int)param_2 / iVar4d - iVar5;
                        int iVar13b = (int)param_2 % iVar4d - iVar4b;
                        float fVar19 = __builtin_sqrtf((float)(iVar5b * iVar5b + iVar13b * iVar13b));   // PERF: inline, bit-identical
                        iVar4c = (int)fVar19 + iVar4d;
                        if (iVar4c < 0) goto LAB_14012d744;
                    }
                    if (iVar4c < iVar6) {
                        *param_3 = posVec.x;
                        param_3[1] = posVec.y;
                        param_3[2] = posVec.z;
                        iVar6 = iVar4c;
                    }
                }
LAB_14012d744:
                iVar15 = iVar15 + 1;
                iVar4 = interact->NodeCount(0);   // 0x140123070
            } while (iVar15 < iVar4);
        }
        iVar15 = -1;
        if (iVar6 < 0x7fffffff) {
            iVar15 = iVar6;
        }
    }
    return iVar15;
}

// ============================================================
// 0x14008d590 — the best storage entity to take `count` of `material`
// ============================================================
// Scan two storage-entity lists (lists[0] and lists[1]) and pick the best entity to
// drop/deliver to. Each list is first copied into a MemAlloc'd snapshot (Collection::Array's
// copy) so the scan survives a callee changing the list. For each candidate entity:
//   1. skip the exclude entity,
//   2. find its StorageLocationComponent, MarketLocationComponent and StorageComponent. With
//      a StorageLocationComponent, its StorageLocationDescription's _locationType (or, when
//      0, the location's owner entity's StorageComponent::rawMaterialFlags) is AND-tested
//      against the material's RawMaterialDescription::_flags; 0 -> skip.
//   3. capacity gate: useCapacity != 0 → StorageCanAdd (12cd80); else the headroom
//      (_count - _reserve) of the storage's item for this material vs count.
//   4. the routing cost: anchor cell (87fb0) → PathDistance (9f810) (euclidean fallback).
//      If it improves the best, compute the drop world position via FindBestAdjacentCell
//      (12d5d0, with a StorageLocationComponent) or PickReachableCell (4edb0), and on
//      success record the entity (*best), the position and the cost.
// Finally writes the best world position into outPos and returns `best`.
ComponentEntity** CitizenBehavior::PickBestDropTarget(ComponentEntity** best,
                                     uint32_t param_3, const ComponentDescription* material,
                                     int param_5, char param_6, float* outPos,
                                     const Object::InternalPtr<ComponentEntity>* exclude) const {
    Map* map = GameUpdateLoop::_singleton->m_game;
    int iVar21 = 0x7fffffff;
    *best = nullptr;
    float bestPos[3] = {0, 0, 0};

    // The drop-source material's RawMaterialDescription.
    const RawMaterialDescription* source = FindSubDescription<RawMaterialDescription>(material);

    int pass = 0;
    do {
        // Retail steps the two adjacent lists +0x00 / +0x10 by their 0x10 stride.
        const CitizenIbpList<ComponentEntity>* list = &(&field_0x00)[pass];
        // Collection::Array's copy into the empty local: MemFree of its (null) buffer,
        // MemAlloc(count * 8), then each element constructed in place.
        ComponentEntity** snapshot = nullptr;
        System::MemoryAllocator::MemFree(nullptr);
        const int n = list->_size;
        if (0 < n) {
            snapshot = static_cast<ComponentEntity**>(
                System::MemoryAllocator::MemAlloc((int64_t)n << 3, 8));
            ComponentEntity** slot = snapshot;
            for (int i = 0; i < n; ++i, ++slot) {
                if (slot != nullptr) {
                    *slot = nullptr;
                    *slot = list->_data[i].Get();
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            ComponentEntity* entity = snapshot[i];
            if (entity == exclude->Get()) continue;
            // B6 (14d G52 / G53): the three inline component scans (cells 0x1401c7ee8,
            // 0x1401c4560, 0x1401c7e78) have no null-slot skip; the location's description
            // is narrowed (IsDerivedFrom, cell 0x1401c7f20) and its +0x20 read with no test;
            // the source's +0x5c is read with no test (the decompile's `pvVar19 + 0x5c`).
            StorageLocationComponent* location = entity->FindComponent<StorageLocationComponent>();
            MarketLocationComponent* market = entity->FindComponent<MarketLocationComponent>();
            StorageComponent* storage = entity->FindComponent<StorageComponent>();
            if (location != nullptr) {
                Description* desc = location->_description;
                const StorageLocationDescription* locDesc =
                    desc->IsDerivedFrom(StorageLocationDescription::TypeId())
                        ? static_cast<const StorageLocationDescription*>(desc) : nullptr;
                uint32_t accepts = locDesc->_locationType;
                if (accepts == 0) {
                    StorageComponent* ownerStorage =
                        location->_entity->FindComponent<StorageComponent>();
                    accepts = ownerStorage->rawMaterialFlags;
                }
                if ((source->_flags & accepts) == 0) continue;
            }
            bool accepted = false;
            if (param_6 == '\0') {
                int headroom = 0;
                const int slots = storage->storageSlots._size;
                const StorageComponent::Item* items = storage->storageSlots._data;
                for (int k = 0; k < slots; ++k) {
                    if (items[k]._resource == material) {
                        headroom = items[k]._count - items[k]._reserve;
                        break;
                    }
                }
                if (-1 < headroom - param_5) accepted = true;
            } else {
                accepted = storage->CanAdd(material, param_5);   // 0x14012cd80
            }
            if (accepted) {
                ComponentEntity* anchor = entity;
                uint32_t uVar10 = (uint32_t)EntityAnchorCell(&anchor, nullptr);   // 0x140087fb0
                int iVar17d = map->PathDistance(uVar10, param_3, '\x01');   // 0x14009f810
                if (iVar17d < 0) {
                    iVar17d = map->size;
                    int iVar11 = (int)uVar10 % iVar17d - (int)param_3 % iVar17d;
                    int iVar9 = (int)uVar10 / iVar17d - (int)param_3 / iVar17d;
                    float fVar24 = __builtin_sqrtf((float)(iVar9 * iVar9 + iVar11 * iVar11));   // PERF: inline, bit-identical
                    iVar17d = (int)fVar24 + iVar17d;
                }
                if (iVar17d < iVar21) {
                    // CONTIGUOUS Vector3 out slot (the writer fills 3 floats via one base
                    // pointer).
                    float dropVec[3] = {0, 0, 0};
                    int iVar11;
                    if (location == nullptr) {
                        iVar11 = market->PickReachableCell(param_3, dropVec);   // 0x14004edb0
                    } else {
                        iVar11 = location->FindBestAdjacentCell(param_3, dropVec);   // 0x14012d5d0
                    }
                    if ((-1 < iVar11) && (iVar11 < iVar21)) {
                        bestPos[0] = dropVec[0];
                        bestPos[1] = dropVec[1];
                        bestPos[2] = dropVec[2];
                        *best = entity;
                        iVar21 = iVar17d;
                    }
                }
            }
        }
        System::MemoryAllocator::MemFree(snapshot);
        pass = pass + 1;
    } while (pass < 2);
    outPos[0] = bestPos[0];
    outPos[1] = bestPos[1];
    outPos[2] = bestPos[2];
    return best;
}
