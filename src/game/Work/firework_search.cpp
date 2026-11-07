// src/game/Work/firework_search.cpp
// FireWork burning-entity spiral search (FireWork action / building-on-fire propagation).
//
//   FireWork_helper_81d0          @ 0x1400881d0  -> CitizenBehavior::FindWater
//   FireWork_helper_81d0_sub_8730 @ 0x140088730  -> CitizenBehavior::ScanWaterCell
//
// B4: both are CitizenBehavior (TownManager) members -- rcx is the TownManager in
// retail (0x1400881d9 spills it as `this`), whatever the Ghidra names say.
#include "Work/exe_bodies.h"
//
// FindWater scans the TownManager's coarse cell index in an expanding ring spiral
// around a centre cell. For every in-bounds cell it invokes the per-cell helper (8730),
// which walks that cell's water-id list, filters by cell type, validates reachability
// (a00f0 + f810 path queries) and tracks the closest source (best path cost in
// *bestCost, its water id in *bestId, the well in *bestWell). When the ring radius
// exceeds the search limit (extent) the scan stops.
//
// After the scan:
//   - if no well was found: sample two terrain positions for the best water pair
//     (local_b8) via the map terrain sampler and write them to outA/outB.
//   - if a well was found: take the InteractComponent (type DAT_1401c7900) of its
//     entity, random-index it (Random::Rand over the recursive child count), resolve its
//     world position (Entity_impl_2e20) and anchor cell (Entity_impl_7fb0), then
//     write both terrain positions.
//
// ⚠ `this` IS NOT THE GameWorld. Slot 4 passes `*(GameWorld+0xF0)` -- the
// TownManager -- and that is faithful: retail 0x1400e977c does `mov rsi,rcx` with rsi
// loaded from `0xf0(g_GameWorld)`. The three fields this pair walks are TownManager
// fields, and the GameWorld's same-numbered fields are unrelated (GW+0xE0 = camera
// holder, GW+0x130 = weather sim, GW+0x140 = save/load), so reading them off the wrong
// object yields garbage rather than a clean null:
//
//   TownManager+0xE0/+0xE8   water-source entity refs (registered by 0x140091cb0)
//   TownManager+0x130/+0x138 water cell-id pairs
//   TownManager+0x140/+0x148 COARSE CELL INDEX over both: (W>>3)^2 buckets of stride
//                            0x10 = {int* ids, int count}. Sized by the GameWorld
//                            world-init at 0x1400975c7 -- see gameupdateloop_load.cpp
//                            Step 31. It is NOT sized by the TownManager ctor
//                            (0x140090a30 only zeroes it), so a port that skips that
//                            call site leaves the base null and the first ring probe
//                            faults at coarseCell*0x10.
//
// Faithful, offset-by-offset port of the decompiled exe helpers; do not "improve" behavior.
// Every numeric constant, the spiral index arithmetic and the per-cell call argument order
// were confirmed against disassemble_function.

#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"      // Reimpl_MapGetTerrainPos (0x14009f3e0)
#include "Game/GameUpdateLoop.h"  // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Action/exe_bodies.h"    // B3R: the Pf_* helpers called by name
#include "Game/exe_constants.h"   // B3R: kRdata_140172114
#include <cstdint>
#include "system/Value.h"         // ENDGAME B.3.4b System callers
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/InteractComponent.h"
#include "Component/WellComponent.h"

// ---------------------------------------------------------------------------
// Sub-helpers, called by name (B3R): 0x140122e20 Pf_EntityResolvePos_2e20,
// 0x140123070 Pf_RecursiveChildSum_123070, 0x140087fb0 Pf_EntityAnchorCell_7fb0,
// 0x1400a00f0 WorkBase_ValidateCell_a00f0, 0x14009f810 Pf_IsDstReachable_f810,
// 0x140132010 WorkImpl_Sqrtf_132010; the InteractComponent lookups (0x140002ed0 and
// the one 0x1400881d0 inlines) are ComponentEntity::FindComponent<InteractComponent>;
// DAT_140172114 = kRdata_140172114 (0.5f).
// ---------------------------------------------------------------------------

static inline float HalfCell() { return kRdata_140172114; }   // DAT_140172114 = 0.5f

// ===========================================================================
// CitizenBehavior::ScanWaterCell  (FUN_140088730)
//   Per-cell check: walk the water-id list of coarse cell `coarseCell`, and for each
//   reachable source update the closest-target trackers (bestCost / bestId / bestWell).
// map = the world Map, ringX / ringY = ring offsets (the radius-vs-limit early reject),
// srcCell = source cell.
// ===========================================================================
void CitizenBehavior::ScanWaterCell(Map* map, int coarseCell, int ringX, int ringY,
                                    uint32_t srcCell, int* bestCost, int* bestId,
                                    WellComponent** bestWell) {
    // The bucket {int* ids, int count, ...}; its count is re-read every pass.
    if (field_0x140._data[coarseCell].GetSize() <= 0)
        return;

    int idx = 0;
    do {
        // Ring radius (in fine units, *8) vs current best/limit squared distance.
        if (ringY * 8 * ringY * 8 + ringX * 8 * ringX * 8 <= (*bestCost) * (*bestCost)) {
            int id = field_0x140._data[coarseCell][idx];   // water id
            uint32_t uVar7;
            if (id < 0) {
                // Negative id -> the well at wells[-1-id].
                ComponentEntity* wellEntity = wells._data[-1 - id]->_entity.Get();
                InteractComponent* interact = wellEntity->FindComponent<InteractComponent>();

                Math::Vector3 pos;
                interact->NodePosition(&pos, 0, 0, nullptr);

                int iVar6 = map->size;                 // grid width W
                int iVar2 = static_cast<int>(pos.x + static_cast<float>(iVar6 >> 1));
                int iVar3 = static_cast<int>(pos.y + static_cast<float>(iVar6 >> 1));
                if (iVar2 < 0)       iVar2 = 0;
                if (iVar6 <= iVar2)  iVar2 = iVar6 - 1;
                if (iVar3 < 0)       iVar3 = 0;
                if (iVar6 <= iVar3)  iVar3 = iVar6 - 1;
                uVar7 = static_cast<uint32_t>(iVar6 * iVar3 + iVar2);
            } else {
                uVar7 = static_cast<uint32_t>(field_0x130._data[static_cast<int64_t>(id)].cellA);
                // Cell-type gate: bit (cellType & 0x1f) of mask 0xe07 must be set.
                // cellType = byte +0x29 of the 0x30-byte map cell (disasm:
                // MOV RAX,[R14 + 0x80]).
                uint8_t cellType = static_cast<uint8_t>(map->cells.GetData()[static_cast<int>(uVar7)].terrainType);
                if (((1u << (cellType & 0x1f)) & Bits(PathBits::Access)) == 0)
                    goto next;
            }

            // Reachability: path-type 0xe07 query then explicit length query.
            {
                uint64_t ok = map->ValidateCell(PathBits::Access, srcCell, static_cast<int>(uVar7));
                if (static_cast<char>(ok) != '\0') {
                    int iVar2;
                    iVar2 = map->PathDistance(srcCell, uVar7, '\x01');   // 0x14009f810 (was its Pf_IsDstReachable_f810 forwarder)
                    if (iVar2 < 0) {
                        // Unreachable by road -> fall back to euclidean (in cells) + bias.
                        int W2  = map->size;
                        int dr  = static_cast<int>(srcCell) / W2 - static_cast<int>(uVar7) / W2;
                        int dc  = static_cast<int>(srcCell) % W2 - static_cast<int>(uVar7) % W2;
                        float d = WorkImpl_Sqrtf_132010(static_cast<float>(dr * dr + dc * dc));
                        iVar2 = static_cast<int>(d) + W2;
                    }
                    if (iVar2 < *bestCost) {
                        *bestCost = iVar2;
                        *bestId = id;
                        // The well itself for a negative id; an empty ref for a water pair.
                        *bestWell = (id < 0) ? wells._data[-1 - id].Get() : nullptr;
                    }
                }
            }
        }
    next:
        idx = idx + 1;
    } while (idx < field_0x140._data[coarseCell].GetSize());
}

// ===========================================================================
// CitizenBehavior::FindWater  (FUN_1400881d0)
//   Expanding-ring spiral over the coarse cell grid around centreCell, bounded by
//   half-search-extent `extent`. Returns true (low byte) when it wrote the two terrain
//   positions to outA / outB.
// ===========================================================================
int64_t CitizenBehavior::FindWater(uint32_t centreCell, int extent, float* outA, float* outB) {

    // The world Map at GameUpdateLoop+0xF8.
    Map* map = GameUpdateLoop::_singleton->m_game;

    WellComponent* found = nullptr;   // found well (null = none)
    int W      = map->size;
    int cellW  = W >> 3;            // coarse grid dimension
    int local_b8 = -1;             // best water id
    int iVar14 = static_cast<int>(centreCell) % W >> 3;   // coarse col
    int local_b4 = static_cast<int>(centreCell) / W >> 3; // coarse row

    int neg_cellW = -cellW;
    int local_a8  = cellW * local_b4;
    int local_54  = 1 - iVar14;
    int local_58  = iVar14 + 1;
    int local_80  = -iVar14;
    int local_68row = local_b4 + 1;

    int iVar6 = -1;                 // ring radius (grows each outer iteration)
    int local_b0 = local_a8;
    int local_70 = iVar14;          // iVar14 captured at top of each ring (decompile local_70)

    while (true) {
        int iVar3 = (iVar6 < 0) ? 0 : iVar6;
        local_70 = iVar14;          // capture entry iVar14 (restored at 2nd-for body end)
        if (extent <= iVar3 * 8) break;   // search-extent exit

        iVar3 = iVar6 + 1;
        int iVar8 = local_80 + iVar14;
        int local_ac = iVar8;
        int iVar10 = local_a8;
        int iVar4  = local_b0;
        // --- vertical edges of the ring (columns iVar14 and local_58+iVar6) ---
        for (; iVar8 <= iVar3; iVar8 = iVar8 + 1) {
            local_b0 = iVar4;
            if ((-1 < iVar8 + local_b4) && (iVar8 + local_b4 < cellW)) {
                if ((-1 < iVar14) && (iVar14 < cellW)) {
                    ScanWaterCell(map, iVar14 + iVar10, local_ac, iVar8,
                                  centreCell, &extent, &local_b8, &found);
                }
                iVar4 = local_58 + iVar6;
                if ((-1 < iVar4) && (iVar4 < cellW)) {
                    ScanWaterCell(map, iVar4 + iVar10, iVar3, iVar8,
                                  centreCell, &extent, &local_b8, &found);
                }
            }
            iVar10 = iVar10 + cellW;
            iVar4 = local_b0;
        }
        // --- horizontal edges of the ring (rows local_ac and local_68row) ---
        iVar10 = iVar14;
        for (iVar8 = local_54 + iVar14; iVar8 <= iVar6; iVar8 = iVar8 + 1) {
            iVar14 = iVar14 + 1;
            if ((-1 < iVar14) && (iVar14 < cellW)) {
                int row = local_ac + local_b4;
                if ((-1 < row) && (row < cellW)) {
                    ScanWaterCell(map, row * cellW + iVar14, iVar8, local_ac,
                                  centreCell, &extent, &local_b8, &found);
                }
                if ((-1 < local_68row + iVar6) && (local_68row + iVar6 < cellW)) {
                    ScanWaterCell(map, iVar4 + iVar14, iVar8, iVar3,
                                  centreCell, &extent, &local_b8, &found);
                }
            }
            iVar10 = local_70;   // decompile restores iVar10 = local_70 (entry iVar14), NOT the incremented iVar14
        }
        local_a8 = local_a8 + neg_cellW;
        local_b0 = local_b0 + cellW;
        iVar14 = iVar10 - 1;
        iVar6 = iVar3;
    }

    if (found == nullptr) {
        // No well found: emit terrain positions for the best water pair, if any.
        if (local_b8 < 0)
            return 0;   // low byte 0 (CONCAT garbage upper bytes; callers test the bool)

        int64_t lVar7 = static_cast<int64_t>(local_b8);
        float half = static_cast<float>(map->size >> 1) - HalfCell();

        // First terrain position: the pair's first cell id at +0x130[best].
        int cellA = field_0x130._data[lVar7].cellA;
        float inXY_a[3];
        inXY_a[0] = static_cast<float>(cellA % map->size) - half;
        inXY_a[1] = static_cast<float>(cellA / map->size) - half;
        inXY_a[2] = 0.0f;
        float outPosA[3];
        map->GetTerrainPos(outPosA, inXY_a, '\0', nullptr);
        outA[0] = outPosA[0];
        outA[1] = outPosA[1];
        outA[2] = outPosA[2];

        // Second terrain position: the pair's second cell id.
        int cellB = field_0x130._data[lVar7].cellB;
        float half2 = static_cast<float>(map->size >> 1) - HalfCell();
        float inXY_b[3];
        inXY_b[0] = static_cast<float>(cellB % map->size) - half2;
        inXY_b[1] = static_cast<float>(cellB / map->size) - half2;
        inXY_b[2] = 0.0f;
        float outPosB[3];
        map->GetTerrainPos(outPosB, inXY_b, '\0', nullptr);
        outB[0] = outPosB[0];
        outB[1] = outPosB[1];
        outB[2] = outPosB[2];
        return 1;
    }

    // A well was found: its entity's InteractComponent (the inlined FindComponent).
    ComponentEntity* wellEntity = found->_entity.Get();
    InteractComponent* chosen = wellEntity->FindComponent<InteractComponent>();

    // Random-index a sub-entity of it, resolve its position.
    uint32_t count = static_cast<uint32_t>(
        chosen->NodeCount(0));   // 0x140123070
    uint32_t randIdx = GameUpdateLoop::_singleton->m_random->Rand(count);

    Math::Vector3 resolved;
    chosen->NodePosition(&resolved, 0, static_cast<int>(randIdx), nullptr);

    // Anchor cell of the well's entity, then its terrain position into outB.
    ComponentEntity* anchorEntity = wellEntity;
    int anchorCell = EntityAnchorCell(&anchorEntity, nullptr);
    float half = static_cast<float>(map->size >> 1) - HalfCell();
    float inXY[3];
    inXY[0] = static_cast<float>(anchorCell % map->size) - half;
    inXY[1] = static_cast<float>(anchorCell / map->size) - half;
    inXY[2] = 0.0f;
    float outP[3];
    map->GetTerrainPos(outP, inXY, '\0', nullptr);
    outB[0] = outP[0];
    outB[1] = outP[1];
    outB[2] = outP[2];

    // The random-resolved sub-entity position goes to outA.
    outA[0] = resolved.x;
    outA[1] = resolved.y;
    outA[2] = resolved.z;
    return 1;
}
