// src/game/Work/work_leaf_gather.cpp
// Wave (Game-gap / Work-leaf) — gather/forester/storage leaf helpers.
//
// Faithful 1:1 ports of exe sub-helpers referenced via ResolveAddr(Module::Exe,
// 0xADDR) from src/game/Work/{gatherrawmaterials,foresterwork}.cpp. They resolve
#include "Work/exe_bodies.h"
// to 0 (silently skipped) unless registered. This file provides the closure-safe subset only.
//
// CLOSURE-SAFETY: each function below was confirmed (snapshot docs/history/notes/_reg_snapshot.txt)
// to call ONLY already-registered exe sub-callees, sibling ports, or engine/runtime
// dispatch slots that resolve at runtime (CheckPtr 0x140152ab0, IsDerivedFrom
// 0x140152a98). Registering a half-null helper is silent corruption, strictly worse
// than the safe no-op of leaving it unregistered.
//
// The intricate pathfinding/copy-ctor helpers in the same batch (0x1400bf790,
// 0x1400ba9f0, 0x1400881d0, 0x1400beae0) were DEFERRED for the same reason.
//
// ⚠️ STALE-LIST CORRECTION (2026-07-16). This header used to list 0x1400f2f00 as
// deferred behind an unregistered closure of "0x1400c29a0, 0x140042890, 0x140088730,
// 0x14008d3c0, 0x14002c070, 0x1400c0ad0, 0x140115e60, 0x1400f6ef0 ...". That list has
// been overtaken by events and was actively misleading:
//   • 0x1400c29a0, 0x140042890, 0x140115e60, 0x1400f6ef0 — since REGISTERED
//     (Phase 2 action child-ctor waves / work_base + work_walkpos batches).
//   • 0x1400f2f00 itself — since ported (src/game/Work/pickup.cpp, the real
//     Pickup::Execute; Ghidra mislabels it Pickup_Serialize).
//   • 0x14008d3c0 (+ 0x14012d990, 0x14012d560) — since ported in
//     src/game/Work/pickup_gate.cpp.
//   • 0x1400c0ad0 (+ 0x1400c9b70) — since ported in
//     src/game/Work/work_base_actionctor.cpp.
//   • 0x14002c070 (+ 0x14002c380) — deliberately NOT registered: it is MSVC's
//     inlined std::sort over Pickup's 32-byte candidate records (ascending on the
//     distance int32 at +0x08). Reimplemented locally as Pickup_SortByDistance in
//     src/game/Work/pickup.cpp.
// Still genuinely unregistered from that old list: 0x1400c7cd0 / 0x1400ca520.
// Verify with a fresh grep over standalone/ before trusting any address list in a
// comment — including this one.
//
// Bodies are unconditional so this compiles as-is in the standalone exe.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"      // B3R: ActCNR_FindComponent_c460
#include "Component/exe_bodies.h"   // B3R: (unused since B6 block D: the QueryCount callers call the member)
#include <cstdint>
#include "Component/GrowthDescription.h"   // ENDGAME B.3.9 D5: GrowthDescription::TypeId()
#include "Component/GrowthComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Component/CitizenComponent.h"   // CitizenComponent::FreeStorageCapacity (defined here)

// ============================================================
// 0x140116540 — gatherrawmaterials.cpp helper
//   Free-storage-capacity query: returns (storage capacity field +0x38) minus the
//   current queried count. ABI: int fn(longlong self).
//   self+0x10 = an InternalBasePtr to the owning entity.
// Sub-callees (all registered / engine):
//   0x140006270 Entity_FindComponent_Storage = WorkBase_FindComponent_6270 (by name, B3R)
//   0x14012cf10 StorageComponent_QueryCount  = StorageComponent::QueryCount (the member)
// ============================================================
// RCX is a Component (only its +0x10 entity is read; the callers hand citizens).
int CitizenComponent::FreeStorageCapacity() const {
    // CheckPtr(self+0x10); FindComponent_Storage(*(self+0x10), &ibpStorage)
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270

    // CheckPtr(&ibpStorage); iVar1 = storage->capacity (+0x38)
    if (!storage) return 0;   // the tree's guard; retail reads +0x38 unguarded
    int capacity = storage->capacity;

    // CheckPtr(&ibpStorage); iVar2 = StorageComponent_QueryCount(storage)
    int count = storage->QueryCount();

    return capacity - count;
}

// ============================================================
// 0x140120c30 — foresterwork.cpp helper (FUN_140120c30)
//   Footprint occupancy/ownership test over a 2x2 cell window of the map cell grid.
//   For each cell (col-1..col, row-1..row, excluding the (0,row==base) corner case
//   matching the original guard) it counts:
//     iVar11 = total non-null cells touched
//     iVar8  = cells whose resolved owner (FUN_14000c460 -> +0x18, IsDerivedFrom
//              DAT_1401c77e8 filter) equals param_1 (the queried work base)
//   Returns false (0) if iVar8 > *(int*)(self+0x5c) OR iVar11 > *(int*)(self+0x58),
//   else true (1).
//   ABI: longlong fn(Base* self, int col, int row).
//   Cell grid: grid = *(int**)((*g_pGameWorldSingleton) + 0xf8); mapW = *grid;
//     cellArrBase = *(void**)((uintptr_t)grid + 0x80);
//     cell = *(void**)(cellArrBase + (mapW*row + col)*0x30 + 0x8)
// Sub-callees:
//   0x14000c460 FUN_14000c460 (owner-resolve) = ActCNR_FindComponent_c460 (by name, B3R)
//   slot 0x140152a98 IsDerivedFrom                     [engine dispatch]
//   g_pGameWorldSingleton (DAT_1401c3dd8)              [runtime global]
//   DAT_1401c77e8 type-ID global                        [runtime-filled]
// ============================================================
// RCX is the GrowthDescription whose planting is tested (its +0x58 / +0x5c limits
// are read, and a neighbour's growth counts as "owned" when its description is it).
long long GrowthDescription::CheckFootprint(int col, int row) const {
    // The singleton / map null arms are the tree's guards (retail reads them unguarded).
    if (GameUpdateLoop::_singleton == nullptr) return 1;

    // grid = (*g_pGameWorldSingleton)->m_game (+0xF8); mapW = its size
    Map* grid = GameUpdateLoop::_singleton->m_game;
    if (!grid) return 1;
    int mapW = grid->size;

    int touched = 0;   // iVar11
    int owned   = 0;   // iVar8

    // Original loop window: rowIdx from (row-1) .. (row), colIdx from (col-1) .. (col),
    // skipping the cell where (colDelta==0 && rowIdx==row) — i.e. the (col, row) corner
    // when colDelta is 0. Faithful port of the nested do/while with bound guards.
    for (long long rowIdx = static_cast<long long>(row) - 1;
         rowIdx - row < 2; rowIdx++) {
        for (long long colDelta = -1; colDelta < 2; colDelta++) {
            // ((colDelta != 0) || (rowIdx != row)) && bounds-check
            if ((colDelta != 0) || (rowIdx != row)) {
                long long c = static_cast<long long>(col) + colDelta;
                if (c >= 0 && c < mapW && rowIdx >= 0 && rowIdx < mapW) {
                    ComponentEntity* cell = grid->cells.GetData()[static_cast<long long>(mapW) * rowIdx + c].occupant.Get();
                    if (cell != nullptr) {
                        touched++;
                        // 0x14000c460 = FindComponent<GrowthComponent>
                        GrowthComponent* growthComp = cell->FindComponent<GrowthComponent>();
                        if (growthComp != nullptr) {
                            Description* obj = growthComp->_description;
                            Description* owner = nullptr;
                            if (Object::IsDerivedFromNullSafe(obj, GrowthDescription::TypeId()))
                                owner = obj;
                            if (owner == this)
                                owned++;
                        }
                    }
                }
            }
        }
    }

    if (owned   > _maxNeighborsSelf ||
        touched > _maxNeighbors)
        return 0;
    return 1;
}
