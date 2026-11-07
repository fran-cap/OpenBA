// src/game/Component/keystone_a_map.cpp
// Keystone A — map overlay / map-init grid rasterizer cluster.
//
// Faithful 1:1 ports (Ghidra, ABI/offset verified against the disassembly) of the
// MapComponent overlay-update + CreateNaturalResource map-init family. These five
// functions share a "param rotation" switch (0x5a / 0xb4 / 0x10e — 90/180/270 deg)
// driving how a small char stamp ('#','_','.') is sampled into a destination cell
// grid, the same shape as ZoneComponent_sub_d360.
//
//   0x1401241b0  MapComponent::AddToMap            overlay update entry
//   0x14009dc20  MapComponent_overlay_dc20        overlay grid-update worker
//   0x140124750  CreateNaturalResource_impl_4750  map-init entry
//   0x14009dee0  CreateNaturalResource_impl_dee0  map-init grid-update worker
//   0x14009e2c0  CreateNaturalResource_helper_e2c0  per-cell terrain classification
//
// int*-arithmetic trap: `param_1` is `int*` (the Map). The grid pointer the original
// reads as `*(longlong*)(param_1 + 0x20)` is BYTE offset 0x80 (0x20 * 4) — confirmed
// by disasm `MOV ... ,[R13 + 0x80]`. `*param_1` is the map width (byte 0). Cell stride
// is 0x30 (LEA RBX,[RAX+RAX*2]; SHL RBX,4).
//
// Sub-callees (all already registered, or this batch):
//   fn_IsDerivedFrom         slot 0x140152a98 (engine_funcs)
//   fn_InternalBasePtr_CheckPtr slot 0x140152ab0 (engine_funcs)
//   fn_InternalBasePtr_Ctor  slot 0x140152a90 (engine_funcs)
//   0x1400a4d30  MapComponent_helper_4d30   (reg_component_visuals)
//   0x1400c3470  NotifyEntityChanged        (reg_action_hunt)
//   0x14009f2f0  SetTileRoadFlag/cost setter(reg_pf_roots_road)
//   0x1400c2ea0  MarkEntityRemoval          (reg_action_hunt)
//   0x140003110  DynArraySetCount push      (reg_action_hunt)
//   0x14009f3e0  GridToWorld terrain query  (mapquery, reg)
//   g_pGameWorldSingleton (+0xe8 / +0xd0 / +0xd8 / +0xf8 / +0x148)
//
// B.4 P2 COMP-45: dc20 / dee0 take the Map and read its MapCell records by name;
// e2c0 keeps the int* receiver its caller in Game/gamegrid_serialize.cpp passes (wave X).

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // Pf_SetTileRoadFlag_9f2f0, ActHunt_*_c2ea0
#include "Game/exe_bodies.h"     // Reimpl_MapGetTerrainPos
#include "Component/MapDescription.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "Object/factory_registration.h"   // GetTypeIdByName (dc20 eviction audit)
#include "Component/exe_bodies.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Game/Minimap.h"   // Minimap::ClearTerrain (0x1400a4d30)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "cell_trace.h"   // red square: BANISHED_CELL_TRACE (src/diag/cell_trace.h)

// dc20 stale-occupant EVICTION tally. Counts only the arm that actually destroys
// (MarkForRemoval + push onto the world removal queue), not the arm that skips an
// already-marked entity — a counter that also counted the skip would read as
// evidence of destruction when none happened.
int32_t g_dc20Evictions = 0;
// ...and the DANGEROUS subset. MarkForRemoval (0x1400c2ea0) only broadcasts the
// component +0x38 "removed from world" teardown — the pass where ModelComponent
// RemoveObjects its scene geometry — when the entity's aggregated flag bit 1 (0x2)
// is set; otherwise it just ORs in the 0x400 removal bit and the entity is queued
// for destruction with its model STILL LINKED into Graphics::Scene. So an eviction
// count alone is not the risk measure: this one is.
int32_t g_dc20EvictNoTeardown = 0;
// Entity gate passed but NO component carries +0x20 bit 1, so the +0x38 broadcast
// visits nobody and the scene geometry dangles anyway. Same end state as the
// counter above, reached by a different door.
int32_t g_dc20EvictNoParticipant = 0;
extern "C" void MapComponent_ReportDc20Evictions(const char* tag) {
    std::fprintf(stderr,
                 "[dc20Census:%s] evictions=%d skipSceneTeardown=%d noParticipatingComp=%d\n",
                 tag ? tag : "?", g_dc20Evictions, g_dc20EvictNoTeardown,
                 g_dc20EvictNoParticipant);
    std::fflush(stderr);
}

namespace {

// --- registered exe sub-callees, called by name (B3R) ------------------------
//   0x14009f2f0 Pf_SetTileRoadFlag_9f2f0        0x1400a4d30 MapTerrainClear_Reimpl
//   0x1400c3470 ComponentEntity::RefreshEntityFlags
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0 0x14009f3e0 Reimpl_MapGetTerrainPos

// e2c0 terrain classification thresholds (named DAT floats, exact bit values).
constexpr float kHalf     = 0.5f;    // DAT_140172114 = 0x3f000000
constexpr float kT_Ret8   = -1.4f;   // DAT_140172284 = 0xbfb33333
constexpr float kT_Ret7   = -0.25f;  // DAT_140172274 = 0xbe800000
constexpr float kT_Ret2Hi = 0.75f;   // DAT_140172124 = 0x3f400000
constexpr float kSlopeMin = 0.9f;    // DAT_140172134 = 0x3f666666
constexpr float kT_Ret6   = 2.0f;    // DAT_140172174 = 0x40000000

} // namespace

// ============================================================
// CreateNaturalResource_helper_e2c0 (0x14009e2c0)
// ============================================================
// Classifies the terrain at cell `param_2` of the map. Builds a centred world XY
// from the cell linear index (col = idx % width, row = idx / width, both minus
// width/2 - 0.5), queries terrain height/slope via 0x14009f3e0, and returns a
// small classification byte:
//   height < -1.4              -> 8 (deep water)
//   height < -0.25             -> 7 (shallow water / shore)
//   -0.25 <= h <= 0.75 & slope>=0.9 -> 2 (flat buildable)
//   else height < 2.0          -> 10, else 6
// Returned byte is consumed by callers as `1 << (b & 0x1f) & 0x1e0`.
int Map::ClassifyTerrain(int param_2) {
    // Stack layout mirrors the original (e2c0 disasm):
    //   local_18 (RSP+0x50) = out vec3   -> [0],[1] = world XY, local_10 (RSP+0x58)=height
    //   local_38 (RSP+0x30) = in XY      -> local_38=x, local_34=y
    //   local_28 (RSP+0x40) = out "mat"  -> f3e0 writes 3 dwords; local_20 (RSP+0x48)=elem[2]
    // The classifier reads height (local_10) and slope (local_20 == out-mat elem[2]).
    float outVec[3] = {0, 0, 0};       // local_18 / local_10
    float inXY[2];                     // local_38 / local_34
    float outMat[3] = {0, 0, 0};       // local_28[0..1] + local_20 (init 0.0)

    float half = (float)(size >> 1) - kHalf;            // local_34 = width/2 - 0.5
    inXY[0] = (float)(param_2 % size) - half;           // local_38 = col - half
    inXY[1] = (float)(param_2 / size) - half;           // local_34 = row - half

    GetTerrainPos(outVec, inXY, '\x01', outMat);   // 0x14009f3e0

    float height = outVec[2];                                       // local_10
    float slope  = outMat[2];                                       // local_20

    if (height < kT_Ret8) return static_cast<int>(PathType::DeepWater);
    if (height < kT_Ret7) return static_cast<int>(PathType::Water);
    if (kT_Ret7 <= height && height <= kT_Ret2Hi && kSlopeMin <= slope) return static_cast<int>(PathType::Normal);
    int ret = static_cast<int>(PathType::Unusable);
    if (height < kT_Ret6) ret = static_cast<int>(PathType::Walkable);
    return ret;
}

// ============================================================
// MapComponent_overlay_dc20 (0x14009dc20)
// ============================================================
// Overlay grid-update worker. For each cell in the [param_4..+param_6) x
// [param_5..+param_7) window of the destination grid (param_1, base +0x80, stride
// 0x30): if the cell currently references an entity (cell+8) different from the
// overlay's owner (*owner) and that entity's flags bit 10 is clear, mark it
// for removal and push it to the gameworld removal queue. Then sample the rotated
// char stamp (footprint) at the rotated coordinate; '#'/'_' => set, '.' => leave,
// '_' => value 2. Write the resulting road/cost value via 0x14009f2f0 and store the
// owner / null IBP into the cell.
void MapComponent_overlay_dc20(Map* param_1, ComponentEntity* const* owner, uint8_t param_3,
                               int param_4, int param_5, int param_6, int param_7,
                               const System::String* footprint, int param_9) {
    int iVar18 = 0;
    int iVar4 = param_7;
    if (0 < param_7) {
        do {
            iVar4 = iVar4 + -1;             // descending outer (row)
            int iVar13 = 0;
            int iVar5 = param_6;
            if (0 < param_6) {
                do {
                    iVar5 = iVar5 + -1;     // descending inner (col)
                    int iVar15 = (param_5 + iVar18) * param_1->size + iVar13 + param_4;
                    MapCell* cell = &param_1->cells.GetData()[static_cast<int64_t>(iVar15)];   // +0x80, stride 0x30
                    Object::InternalPtr<ComponentEntity>* occupant = &cell->occupant;   // +0x08 InternalPtr<ComponentEntity>

                    // Evict any stale entity occupying this cell.
                    if ((*owner != nullptr) && (occupant->Get() != nullptr)) {
                        ComponentEntity* lVar2 = occupant->Get();
                        if (*owner != lVar2) {
                            EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;   // +0xE8
                            ComponentEntity* local_68 = lVar2;
                            if (lVar2 != nullptr) {
                                if ((local_68->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
                                    // Always-on tally (the trace above is opt-in AND capped at
                                    // 40 lines, so nothing countable survived a default run).
                                    // This branch DESTROYS an entity, so its rate is the single
                                    // most load-bearing number in this file: it is how a
                                    // spawned entity gets freed while its ModelComponent is
                                    // still linked into Graphics::Scene.
                                    ++g_dc20Evictions;
                                    if ((local_68->_entityFlags & Bits(ComponentFlags::OnRemove)) == 0) {
                                        ++g_dc20EvictNoTeardown;   // scene geometry will dangle
                                    } else {
                                        // Entity gate passed — but MarkForRemoval's broadcast
                                        // only reaches components whose OWN +0x20 has bit 1.
                                        // If none do, the broadcast is a no-op and the scene
                                        // geometry dangles just the same. Measure that arm too;
                                        // the entity-level counter above cannot see it.
                                        int n = local_68->_components._size;
                                        Object::InternalPtr<Component>* arr = local_68->_components._data;
                                        int participating = 0;
                                        for (int ci = 0; ci < n && arr; ++ci) {
                                            Component* c = arr[ci].Get();
                                            if (c && (c->_flags & Bits(ComponentFlags::OnRemove)))
                                                ++participating;
                                        }
                                        if (participating == 0) ++g_dc20EvictNoParticipant;
                                    }
                                    local_68->MarkForRemoval();   // 0x1400c2ea0
                                    entityList->removalList
                                        .SetSize(entityList->removalList._size + 1, local_68);
                                }
                            }
                        }
                    }

                    // Sample the rotated char stamp.
                    const char* lVar2 = footprint->_data;
                    bool bVar7;
                    if ((lVar2 == nullptr) || (footprint->GetLength() == 0)) {
                        bVar7 = true;
                    } else {
                        bVar7 = false;
                    }
                    bool bVar19 = true;
                    uint8_t bVar16 = param_3;
                    if (!bVar7) {
                        int iVar12 = iVar5;
                        int iVar6 = iVar18;
                        int iVar11, iVar17, iVar8;
                        if ((param_9 == 0x5a) ||
                            ((iVar11 = param_7, iVar17 = param_6, iVar8 = iVar4,
                              param_9 != 0xb4 &&
                              (iVar8 = iVar18, iVar12 = iVar13, iVar6 = iVar4,
                               param_9 == 0x10e)))) {
                            iVar11 = param_6;
                            iVar17 = param_7;
                            iVar8 = iVar12;
                            iVar12 = iVar6;
                        }
                        char cVar1 = lVar2[((iVar11 - iVar8) - 1) * (iVar17 + 1) + iVar12];
                        if ((cVar1 == '#') || (cVar1 == '_')) {
                            bVar7 = true;
                        } else {
                            bVar7 = false;
                        }
                        bVar19 = cVar1 != '.';
                        if (cVar1 == '_') {
                            bVar16 = ToUnderlying(PathType::Normal);
                        }
                        // ⚠ DELIBERATE DEVIATION — BANISHED_LIVE_WALKWAY_DROPCELLS (default
                        // on; set =0 to restore the byte-faithful claim). Retail claims '_'
                        // exactly like '#' (CMP CL,0x2e ; SETNZ BL). A '_' cell is the
                        // building's own walkable strip and is where its produce is dropped,
                        // and retail's FishingWork::Slot4 only works if those cells read
                        // cell+8 == 0. Class stamp and stale-occupant eviction are untouched;
                        // ownership for placement/picking lives in the ZONE raster (cell+0),
                        // which this does not affect. Full rationale on the flag.
                        if (cVar1 == '_') {   // walkway drop-cells: a deliberate deviation from the retail bytes
                            bVar19 = false;
                        }
                    }

                    uint8_t bVar10 = static_cast<uint8_t>(cell->terrainType);   // +0x29
                    if (bVar7) {
                        bVar10 = bVar16;
                    }
                    param_1->SetTerrainType(iVar15, (uint32_t)bVar10);   // 0x14009f2f0

                    iVar13 = iVar13 + 1;
                    *occupant = bVar19 ? *owner : nullptr;   // the owner / a null InternalBasePtr
                    CellStamp::NoteWrite(cell);   // [AMBMEMO] cell write clock
                } while (iVar13 < param_6);
            }
            iVar18 = iVar18 + 1;
        } while (iVar18 < param_7);
    }
}

// ============================================================
// CreateNaturalResource_impl_dee0 (0x14009dee0)
// ============================================================
// Map-init grid-update worker. For each cell in the window: sample the rotated char
// stamp (footprint) — '#'/'_' => set. On a set cell, classify the terrain via
// 0x14009e2c0; if the classification bit is in mask 0x1e0 AND the cell already holds
// an entity that differs from the owner, force class 2; write the class via
// 0x14009f2f0. Always (re)initialise the cell's +8 IBP slot to null.
void CreateNaturalResource_impl_dee0(Map* param_1, ComponentEntity* const* owner, int param_3,
                                     int param_4, int param_5, int param_6,
                                     const System::String* footprint, int param_8) {
    int iVar16 = 0;
    int iVar3 = param_6;
    if (0 < param_6) {
        do {
            iVar3 = iVar3 + -1;            // descending outer (row)
            int iVar12 = 0;
            int iVar4 = param_5;
            if (0 < param_5) {
                do {
                    iVar4 = iVar4 + -1;    // descending inner (col)
                    const char* lVar2 = footprint->_data;
                    int iVar14 = (iVar16 + param_4) * param_1->size + iVar12 + param_3;

                    bool bVar6;
                    if ((lVar2 == nullptr) || (footprint->GetLength() == 0)) {
                        bVar6 = true;
                    } else {
                        bVar6 = false;
                    }
                    if (!bVar6) {
                        int iVar7 = iVar4;
                        int iVar5 = iVar16;
                        int iVar15, iVar8, iVar10;
                        if ((param_8 == 0x5a) ||
                            ((iVar15 = param_5, iVar8 = iVar3, iVar10 = param_6,
                              param_8 != 0xb4 &&
                              (iVar8 = iVar16, iVar7 = iVar12, iVar5 = iVar3,
                               param_8 == 0x10e)))) {
                            iVar15 = param_6;
                            iVar8 = iVar7;
                            iVar10 = param_5;
                            iVar7 = iVar5;
                        }
                        char cVar1 = lVar2[((iVar10 - iVar8) - 1) * (iVar15 + 1) + iVar7];
                        if ((cVar1 == '#') || (cVar1 == '_')) {
                            bVar6 = true;
                        } else {
                            bVar6 = false;
                        }
                    }

                    MapCell* plVar11 = &param_1->cells.GetData()[static_cast<int64_t>(iVar14)];   // +0x80, stride 0x30
                    if (bVar6) {
                        int cls = param_1->ClassifyTerrain(iVar14);
                        uint8_t bVar13 = (uint8_t)cls;
                        if (((1 << (bVar13 & 0x1f) & Bits(PathBits::Zoneless)) != 0)
                            && (plVar11->blocker != nullptr)) {   // +0x00
                            if (plVar11->blocker != *owner) {
                                bVar13 = ToUnderlying(PathType::Normal);
                            }
                        }
                        CellTrace::NoteResort(param_1, iVar14, static_cast<uint8_t>(plVar11->terrainType), bVar13);
                        param_1->SetTerrainType(iVar14, (uint32_t)bVar13);   // 0x14009f2f0
                    }

                    iVar12 = iVar12 + 1;
                    plVar11->occupant = nullptr;   // +0x08, a null InternalBasePtr
                    CellStamp::NoteWrite(plVar11);   // [AMBMEMO] cell write clock
                } while (iVar12 < param_5);
            }
            iVar16 = iVar16 + 1;
        } while (iVar16 < param_6);
    }
}

// ============================================================
// MapComponent::AddToMap (0x1401241b0)
// ============================================================
// Overlay-update entry. Guarded by the +0x20 bit-20 flag (must be CLEAR).
// Resolves the MapDescription (IsDerivedFrom DAT_1401c7a18), optionally
// paints the footprint into the minimap overlay via 0x1400a4d30, then runs the
// grid occupancy update (dc20). Finally sets bit-20 and, if the low-16 flag bits
// changed and the owner entity at +0x10 is non-null, RefreshEntityFlags (0x1400c3470).
// Callers (retail): MapComponent::OnAdded 0x1401243a1 (under _claimOnAdd),
// MapComponent::OnEvent 0x1401243c3 (message 1), the MultiObject helper 0x14012560f.
// The cast is IsDerivedFrom on [RCX+0x18] with no test (0x1401241f4); the
// description is read through the CMOVNZ result unguarded (0x140124202).
void MapComponent::AddToMap() {
    if ((_flags >> 0x14 & 1) != 0) return;

    Description* obj = _description;
    Map* piVar2 = GameUpdateLoop::_singleton->m_game;   // +0xF8
    bool bVar3 = obj->IsDerivedFrom(MapDescription::TypeId());   // cell 0x1401c7a18
    MapDescription* pvVar5 = bVar3 ? static_cast<MapDescription*>(obj) : nullptr;

    if (pvVar5->_showOnMap) {
        GameUpdateLoop::_singleton->m_imageCapture->ClearTerrain(// 0x1400a4d30 (+0x148)
                               static_cast<int>(pvVar5->_color._value),
                               (int)static_cast<int16_t>(tileX0),
                               (int)static_cast<int16_t>(tileY0),
                               (int)static_cast<int16_t>(tileX1),
                               (int)static_cast<int16_t>(tileY1));
    }

    if (!pvVar5->_noOccupancy) {
        ComponentEntity* local_res8 = _entity.Get();
        MapComponent_overlay_dc20(
            piVar2, &local_res8,
            (uint8_t)pvVar5->_cellType,
            (int)static_cast<int16_t>(tileX0),
            (int)static_cast<int16_t>(tileY0),
            (int)static_cast<int16_t>(tileX1),
            (int)static_cast<int16_t>(tileY1),
            &pvVar5->_footprint,
            extraField);

        uint32_t uVar1 = _flags;
        uint32_t uVar4 = uVar1 | 0x100000;          // BTS 0x14
        _flags = uVar4;
        if ((((uVar4 ^ uVar1) & 0xffff) != 0) && (_entity != nullptr)) {
            _entity->RefreshEntityFlags();   // 0x1400c3470
        }
    }
}

// ============================================================
// CreateNaturalResource_impl_4750 (0x140124750)
// ============================================================
// Map-init entry. Guarded by the +0x20 bit-20 flag (must be SET). Resolves the
// MapDescription, runs the map-init grid-update (dee0), then CLEARS bit-20 and, if
// the low-16 flag bits changed and the owner entity is non-null, RefreshEntityFlags.
// LEFT: nothing calls this body; MapComponent's slot 7 reaches 0x140124750 through
// MapComponent_DetachOccupancy_4750 (mapcomponent.cpp), a different body of the same
// retail function (it trusts the linked description and unmarks through starttown).
void CreateNaturalResource_impl_4750(MapComponent* self) {
    if ((self->_flags >> 0x14 & 1) == 0) return;

    Description* obj = self->_description;
    Map* piVar2 = GameUpdateLoop::_singleton->m_game;   // +0xF8
    uint32_t typeId = MapDescription::TypeId();   // cell 0x1401c7a18; TypeId_AreaDescription is misnamed (D5)
    bool bVar3 = typeId ? ::Object::IsDerivedFromNullSafe(obj, typeId) : false;
    ComponentEntity* local_res8 = self->_entity.Get();
    MapDescription* pvVar5 = bVar3 ? static_cast<MapDescription*>(obj) : nullptr;

    CreateNaturalResource_impl_dee0(
        piVar2, &local_res8,
        (int)static_cast<int16_t>(self->tileX0),
        (int)static_cast<int16_t>(self->tileY0),
        (int)static_cast<int16_t>(self->tileX1),
        (int)static_cast<int16_t>(self->tileY1),
        &pvVar5->_footprint,
        self->extraField);

    uint32_t uVar1 = self->_flags;
    uint32_t uVar4 = uVar1 & 0xffefffff;            // BTR 0x14
    self->_flags = uVar4;
    if ((((uVar4 ^ uVar1) & 0xffff) != 0) && (self->_entity != nullptr)) {
        self->_entity->RefreshEntityFlags();   // 0x1400c3470
    }
}
