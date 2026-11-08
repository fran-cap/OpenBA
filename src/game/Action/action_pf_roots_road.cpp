// src/game/Action/action_pf_roots_road.cpp
// Wave 3 (Action) — ROAD-edit ROOT callers. Faithful 1:1 ports of the four top-level
// road/zone mutation entry points that drive the pathfinding connectivity + terrain
// refresh. NONE is exercised on the autostart path, so a wrong transcription is
// undetectable silent corruption — every offset is taken verbatim from the Ghidra
// decompile. Do NOT "improve" or simplify.
#include "Action/exe_bodies.h"
//
// Registration point: RegisterPfRootsRoadBatch() in standalone/reg_pf_roots_road.cpp.
//
// B.4 ACT-a: the four bodies are Map's members (FillZones 0x14009c480, SetTerrainType
// 0x14009f2f0, ResetTerrainHeights 0x14009e0e0, FinishRoadCell 0x14009c640) and call:
//   0x14009c170 Map::FillZone               0x1400a8c90 RegionGraph::MarkCellDirty
//   0x1400a6fe0 PathfinderThread::SetCellTerrain
//   0x1400ab150 WorldGeometry::MarkChunksDirty   0x1400af370 WorldGeometry::RebuildRoadMesh
//   0x14009f3e0 Reimpl_MapGetTerrainPos     0x14001a6e0 WorkBST_BSTRemove_a6e0

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "Game/WorldGeometry.h"
#include "Action/PathSearch.h"
#include "Action/PathAvl.h"
#include "Game/terrain_sidecar.h"
#include "stress_harness.h"   // PFBENCH: PfMainTimer (ph_fillzones_ms; stress harness only)
#include "Work/exe_bodies.h"       // WorkBST_BSTRemove_a6e0
#include "Game/exe_bodies.h"       // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"    // kRdata_*
#include <cstring>
#include "math/Math.h"
#include <cstdint>
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "cell_trace.h"   // red square: BANISHED_CELL_TRACE (src/diag/cell_trace.h)
#include "ext/registry.h"   // Ext::Fix().terrain_resort
#include "ext/terrain_resort/terrain_resort.h"   // fix.terrain_resort (docs/extensions/handins/TERRAINRESORT.md)
#include "ext/winter_ice/walk_type.h"   // EXT SEAM (NOT RETAIL, WINTERICE.md 5.2): ext.walk_on_ice, a frozen lake cell reads as type 9

typedef unsigned int  uint;
typedef unsigned short ushort;

namespace {

// Float constants (Game/exe_constants.h) used by e0e0's tile-centre math:
// 0x140172230 (256.0f, a bit pattern) and 0x140172114 (0.5f cell-centre half).
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

} // namespace

// ============================================================
// 0x14009c480 — ZoneRefresh. (Ghidra: FUN_14009c480.) Map::FillZones.
//   If the zones are stale (zonesStale): clear the flag, zero every cell's zone, then walk
//   the cells and for each walkable (terrain type bit in 0xe1f) cell whose zone is still
//   0, flood-fill (0x14009c170) its connected run with the next zone id (starting at 1).
// ONE parameter (B3R Leftovers, read in retail): Ghidra's prototype had four, built from
// register use. The callers (0x14009f29e, 0x1400eb303, 0x1400e532a / 54ea / 556c, ...)
// load only RCX. The body WRITES EDX (0x14009c4bd; per cell 0x14009c524 = EDI, the cell
// index: uVar2 below) and R8D (0x14009c520 = R14W, the zone id: sVar6 below) before
// reading them -- they are its locals, initialised exactly there -- and never touches R9.
// ============================================================
void Map::FillZones() {
    PfMainTimer pfTimer(PF_MAIN_FILLZONES);   // [PFBENCH] one g_pfProfOn branch when the harness is off
    uint uVar1;
    uint64_t uVar2;
    int64_t lVar4;
    int64_t uVar5;
    short sVar6;

    if (static_cast<char>(zonesStale) != '\0') {
        uVar5 = 0;
        zonesStale = 0;
        uVar2 = 0;
        int64_t i = 0;
        if (0 < cells.GetSize()) {
            do {
                uVar1 = static_cast<int>(uVar2) + 1;
                cells.GetData()[i].zone = 0;
                uVar2 = static_cast<uint64_t>(uVar1);
                i = i + 1;
            } while (static_cast<int>(uVar1) < cells.GetSize());
        }
        lVar4 = static_cast<int64_t>(cells.GetSize());
        sVar6 = 1;
        uVar2 = 0;
        if (0 < lVar4) {
            do {
                if (((1 << (static_cast<uint8_t>(Ext::WalkOnIce::TypeOf(cells.GetData()[uVar5])) & 0x1f) &
                      Bits(PathBits::Walk)) != 0) &&
                    (static_cast<short>(cells.GetData()[uVar5].zone) == 0)) {
                    FillZone(static_cast<uint32_t>(static_cast<int>(uVar2)), static_cast<uint16_t>(sVar6));   // 0x14009c170
                    sVar6 = sVar6 + 1;
                }
                uVar5 = uVar5 + 1;
                lVar4 = lVar4 + -1;
                uVar2 = static_cast<uint64_t>(static_cast<int>(uVar2) + 1);
            } while (lVar4 != 0);
        }
    }
}



// ============================================================
// 0x14009f2f0 — set a cell's terrain type + dirty/enqueue. (Ghidra: FUN_14009f2f0.)
//   Old type = cells[cell].terrainType. If the change crosses between the walkable set
//   0xe1f and the road set 0x1e0 (either way), the zones go stale (zonesStale |= 1).
//   Writes the new type (the low byte of `type`). If the transition crosses the
//   road/path boundary, marks the cell's region block dirty (0x1400a8c90) and sets uVar6
//   (the road-bit flag) accordingly. Finally sends the type to the worker (0x1400a6fe0).
// ============================================================
void Map::SetTerrainType(int param_2, uint param_3) {
    uint8_t bVar1;
    uint8_t bVar2;
    MapCell* lVar3;
    uint8_t bVar4;
    uint uVar5;

    lVar3 = &cells.GetData()[param_2];
    bVar1 = static_cast<uint8_t>(lVar3->terrainType);
    uVar5 = 1u << (bVar1 & 0x1f);
    bVar4 = static_cast<uint8_t>(param_3);
    if ((((uVar5 & Bits(PathBits::Walk)) == 0) || ((1u << (bVar4 & 0x1f) & Bits(PathBits::Zoneless)) == 0)) &&
        (((uVar5 & Bits(PathBits::Zoneless)) == 0) || ((1u << (bVar4 & 0x1f) & Bits(PathBits::Walk)) == 0))) {
        bVar2 = 0;
    } else {
        bVar2 = 1;
    }
    zonesStale = static_cast<uint8_t>(zonesStale) | bVar2;
    lVar3->terrainType = static_cast<PathType>(bVar4);
    TerrainSidecar::NoteWrite(lVar3);   // [terrain sidecar] mirror
    CellStamp::NoteWrite(lVar3);   // [AMBMEMO] cell write clock
    uVar5 = 1u << (bVar1 & 0x1f);
    if ((((uVar5 & Bits(PathBits::Walk)) != 0) && ((1u << (bVar4 & 0x1f) & Bits(PathBits::Zoneless)) != 0)) ||
        (((uVar5 & Bits(PathBits::Zoneless)) != 0) &&
         (uVar5 = 1u << (bVar4 & 0x1f), (uVar5 & Bits(PathBits::Walk)) != 0))) {
        regionGraph->MarkCellDirty(param_2 % size, param_2 / size);   // 0x1400a8c90
    }
    // Retail's R9 carries the road-bit flag here; 0x1400a6fe0 reads EDX and R8D only.
    pathfinder->SetCellTerrain(param_2, static_cast<int>(param_3));   // 0x1400a6fe0
}

// ============================================================
// 0x14009e0e0 — clear the terrain height words over a cell rect + refresh. (Ghidra:
//   CitizenAI_impl_e0e0.) For each cell of [x0..x1] x [y0..y1]: build a Vector3 at the
//   cell centre, map it to the terrain (0x14009f3e0), offset by half the heightmap edge,
//   zero that heightmap word and mark its chunk dirty (0x1400ab150). Then rebuild the
//   road mesh (0x1400af370). fVar4 = DAT_172230, fVar3 = DAT_172114.
// TRAP: 0x14009f3e0 writes its 2-float result into gw[0] / gw[1]; the heightmap offset
//   uses the SECOND Vector3 built after it.
// ============================================================
void Map::ResetTerrainHeights(int param_2, int param_3, int param_4, int param_5) {
    uint uVar1;
    WorldGeometry* lVar2;
    float fVar3;
    float fVar4;
    int iVar5;
    int iVar6;
    float fVar7;
    // Vector3 output slots must be CONTIGUOUS — GridToWorld writes 3 consecutive floats
    // through a single pointer. gw = GridToWorld out {98,94,90}; co = the second Vector3.
    float gw[3];   // gw[0]=local_98, gw[1]=local_94, gw[2]=local_90
    Math::Vector3 co;   // co.x=local_88, co.y=local_84, co.z=local_80
    Math::Vector3 local_78;  // Ghidra `Vector3 local_78 [80]` — only [0] was ever used (B.3.1: one Vector3)

    const int firstRow = param_3;   // FIX terrain_resort: the loop below advances param_3
    fVar4 = RdataF(kRdata_140172230);   // 0x140172230
    fVar3 = kRdata_140172114;           // 0x140172114
    lVar2 = GameUpdateLoop::_singleton->m_worldGeometry;
    for (; param_3 <= param_5; param_3 = param_3 + 1) {
        if (param_2 <= param_4) {
            iVar6 = param_2;
            do {
                uVar1 = static_cast<uint>(size);
                iVar5 = uVar1 * param_3 + iVar6;
                fVar7 = static_cast<float>(static_cast<int>(uVar1) >> 1) - fVar3;
                local_78 = Math::Vector3(
                                    static_cast<float>(iVar5 % static_cast<int>(uVar1)) - fVar7,
                                    static_cast<float>(iVar5 / static_cast<int>(uVar1)) - fVar7, 0.0f);
                GetTerrainPos(gw, &local_78.x, '\0', nullptr);   // 0x14009f3e0
                gw[2] = 0.0f;  // original: local_90 = 0 (a 4-byte zero into the z slot; the same bits)
                fVar7 = static_cast<float>(lVar2->_gridSize >> 1);
                co = Math::Vector3(gw[0] + fVar7, gw[1] + fVar7, 0.0f);
                lVar2->_heights.data[static_cast<int64_t>(lVar2->_heightRowStride) * static_cast<int64_t>(static_cast<int>(co.y)) +
                                static_cast<int64_t>(static_cast<int>(co.x))] = static_cast<short>(static_cast<int>(fVar4 * 0.0f));
                StillTerrain::Bump();   // [STILLTERRAIN] a heightmap word was written
                lVar2->MarkChunksDirty(static_cast<int>(co.x), static_cast<int>(co.y));   // 0x1400ab150
                iVar6 = iVar6 + 1;
            } while (iVar6 <= param_4);
        }
    }
    // FIX terrain_resort (NOT RETAIL, docs/extensions/handins/TERRAINRESORT.md): the cells
    // around the flattened ones keep the type they were sorted with; heal the stale "steep".
    if (Ext::Fix().terrain_resort) {
        Ext::TerrainResort::HealAround(this, param_2, firstRow, param_4, param_5);
    }
    GameUpdateLoop::_singleton->m_worldGeometry->RebuildRoadMesh(this);   // 0x1400af370
}

// ============================================================
// 0x14009c640 — finalize a road cell edit. (Ghidra: CompleteRoad_helper_c640.)
//   If type == 2 (removal): footprintCount -1, field_0x2A / roadAlpha = 0xff / 0 (one
//     word store), terrain type 2 via 0x14009f2f0.
//   Else: roadAlpha = orientation, terrain type (type & 0xff) via 0x14009f2f0, then
//     reset the terrain heights (0x14009e0e0) over [col..col+1] x [row..row+1].
//   Then walk the tree at pendingRoadCells (0x28 nodes keyed by cell) and if the cell is
//   present, delete it (0x14001a6e0). Finally rebuild the road mesh (0x1400af370).
// ============================================================
void Map::FinishRoadCell(int param_2, uint8_t param_3, uint param_4) {
    AvlNode28* piVar1;
    int iVar2;
    int iVar3;
    MapCell* lVar4;

    lVar4 = &cells.GetData()[param_2];
    if (param_4 == ToUnderlying(PathType::Normal)) {   // the road removed
        CellTrace::NoteCount(this, lVar4, lVar4->footprintCount, -1, "FinishRoadCell");
        lVar4->footprintCount = lVar4->footprintCount + -1;
        lVar4->roadType = MapCell::kNoRoadBatch;   // disasm: MOV word ptr,0xff over +0x2a / +0x2b (H7)
        lVar4->roadAlpha = 0;
        SetTerrainType(param_2, ToUnderlying(PathType::Normal));   // 0x14009f2f0
    } else {
        lVar4->roadAlpha = param_3;
        SetTerrainType(param_2, param_4 & 0xff);   // 0x14009f2f0
        iVar3 = param_2 / size;
        iVar2 = param_2 % size;
        ResetTerrainHeights(iVar2, iVar3, iVar2 + 1, iVar3 + 1);   // 0x14009e0e0
    }
    piVar1 = pendingRoadCells.root;
    do {
        if (piVar1 == nullptr) {
            goto LAB_14009c6d2;
        }
        if (piVar1->key <= param_2) {
            if (param_2 <= piVar1->key) {
                WorkBST_BSTRemove_a6e0(&pendingRoadCells, piVar1);   // 0x14001a6e0
                goto LAB_14009c6d2;
            }
            piVar1 = piVar1->right;
            continue;
        }
        piVar1 = piVar1->left;
    } while (true);
LAB_14009c6d2:
    GameUpdateLoop::_singleton->m_worldGeometry->RebuildRoadMesh(this);   // 0x1400af370
}
