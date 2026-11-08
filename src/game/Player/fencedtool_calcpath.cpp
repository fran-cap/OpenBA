// src/game/Player/fencedtool_calcpath.cpp
// FencedTool::CalcPath (vtable +0x68, Slot13) — the drag-commit that creates a
// fenced area (pasture / crop field): the master entity, the animal-shelter
// sub-entity, and the perimeter fence pieces (corners, straights, gate),
// all linked through the master's MultiObjectComponent.
//
// Ports (all verified against disassembly, not just decompile):
//   0x1400dae00  FencedTool_Slot13 (CalcPath)          — this file
//   0x1400dbc80  FencedTool_Slot13_sub_bc80            — spawn ONE fence piece
//   0x1400db8c0  FencedTool_Slot13_sub_b8c0            — walk one edge, spawn run
//   0x1401290d0  FencedTool_helper_90d0                — seat pick owner on shelter
//
// Section template array (CreateFencedDescription +0x28, ExternalPtr[6]), order
// fixed by Pasture.rsc `_sections`:
//   [0] AnimalShelter  [1] FenceEnd  [2] FenceCorner  [3] FenceStraight
//   [4] FenceStraightLong (2 cells)  [5] FenceGate (2 cells)
//
// Flow (retail 0x1400dae00):
//   1. vtable[14] (CalcCost) validates the drag rect; non-zero => abort (ret 0).
//   2. Edit-mode guard: if the placement dialog obj (tool+0x10) has +0x80 set,
//      the rect must equal the stored +0x98/+0x9c cells (re-fence of an
//      existing area) or abort.
//   3. 0x1400dbf90 places the shelter rect on the rotation-selected edge and
//      returns its world-centre pos + the yaw (0/-90/180/90 as float degrees).
//   4. Master entity: CtorFromDescription(tool+0x20), MapComponent gets the
//      full rect + yaw; existing entities of template desc+0x70 (Clear.rsc
//      grass) inside the rect are removed via 0x1401018d0.
//   5. Shelter entity: CtorFromDescription(sections[0]), MapComponent gets the
//      shelter rect, ModelComponent transform = FromYawPitchOffset(yaw, 0, pos)
//      (the pos is LOAD-BEARING — see the bridge-at-origin lesson), picking
//      user-data re-owned to the MASTER entity (helper_90d0), appended to the
//      master's MultiObject list, registered.
//   6. MultiObject+0x58 = perimeter cell count = (w+h)*2-4.
//   7. 4 corners (sub_bc80 idx 2, yaw 90/0/-90/180), skipped where the shelter
//      covers the corner; 4 edge runs (sub_b8c0).
//   8. Register the master entity. Return 1.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Geometry.h"
#include <new>
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Player/exe_bodies.h"
#include "Component/ComponentDescription.h"
#include "Component/CreateFencedDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/PickingComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Action/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "FencedTool.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)

// ---- exe deps, called by name ------------------------------------------------
//   0x1400c3720 ComponentEntity_CtorFromDescription, 0x1400c48a0
//   EntityList_RegisterEntity_Impl (Entity/componententity_funcs.h)
//   0x140003110 ActHunt_DynArraySetCount_3110 (Action/exe_bodies.h) -- IBP append
//   0x1401018d0 DragPlacementTool_ClearPathEntities_1018d0, 0x1400dbf90
//   PlayerEdit_FencedAnchorCalc_dbf90 (Player/exe_bodies.h)
//   the finder leaves: 0x140004680 FindComponent<MapComponent>, 0x140003f60
//   FindComponent<ModelComponent>, 0x140026a30 FindComponent<PickingComponent>,
//   0x140004740 FindComponent<MultiObjectComponent>
namespace {

// The section templates are CreateFencedDescription::_sections (+0x28).

// Stamp cell rect + rotation onto a MapComponent (+0x28 shorts, +0x30 degrees
// normalised into [0,360) by repeated +360 exactly like retail).
void StampMapRect(MapComponent* mapComp, int x, int y, int w, int h, int rotDeg) {
    if (!mapComp) return;
    mapComp->tileX0 = static_cast<int16_t>(x);
    mapComp->tileY0 = static_cast<int16_t>(y);
    mapComp->tileX1 = static_cast<int16_t>(w);
    mapComp->tileY1 = static_cast<int16_t>(h);
    while (rotDeg < 0) rotDeg += 0x168;
    mapComp->extraField = rotDeg;
}

// Model transform = FromYawPitchOffset(yawDeg, 0, offset) into model+0x148,
// yaw cached at +0x168 (UN-normalised float, matching retail), then
// Geometry::SetTransform. Same helper family as bridgetool_calcpath's ApplyYaw.
void ApplyModelYaw(ModelComponent* model, float yawDeg, const float offset[3]) {
    if (!model) return;
    Math::Vector3 off(offset[0], offset[1], offset[2]);
    Math::Transform xform = Math::Transform::FromYawPitchOffset(yawDeg, 0.f, off);
    model->transform = xform;   // the seven dwords at +0x148
    model->yaw = yawDeg;        // +0x168
    model->meshInstance.SetTransform(model->transform);
}

// 0x1401290d0 — seat the shelter's pick owner: pickComp+0x30 = master entity;
// if the pick mesh is already registered (+0x28 >= 0), re-point its user data.
void SeatPickOwner_1290d0(PickingComponent* pickComp, ComponentEntity* const* masterIBPSlot) {
    if (!pickComp) return;
    pickComp->userData = *masterIBPSlot;
    if (pickComp->pickingId < 0) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Picking::Scene* scene = gw ? gw->m_pickingScene : nullptr;
    if (scene)
        scene->SetMeshUserData(static_cast<uint32_t>(pickComp->pickingId),
                      pickComp->userData.Get(), 0, 0, nullptr);
}

// Grid cell -> world pos via the 0x14009f3e0 terrain query (bridge CellRectCentre
// idiom): map-space = (col - c, row - c), c = (gridDim >> 1) - 0.5.
void CellWorldPos(Map* mapGrid, int col, int row, float out[3]) {
    out[0] = out[1] = out[2] = 0.f;
    if (!mapGrid) return;
    const float c = static_cast<float>(mapGrid->size >> 1) - 0.5f;
    float in[3] = { static_cast<float>(col) - c, static_cast<float>(row) - c, 0.f };
    mapGrid->GetTerrainPos(out, in, '\0', nullptr);
}

// Append the entity to the master's MultiObject list (0x140003110 on +0x48).
void AppendChild(MultiObjectComponent* multi, ComponentEntity* entity) {
    ComponentEntity* val = entity;
    multi->children.SetSizeFill(multi->children._size + 1, &val);   // 0x140003110
}

// Register the entity with the EntityList under its template's category (+0x20).
void RegisterWithList(GameUpdateLoop* gw, ComponentEntity* entity, const ComponentDescription* tmpl) {
    Object::InternalPtr<ComponentEntity> val = entity;
    int prio = static_cast<int>(tmpl->_category);
    gw->m_entityList->RegisterEntity(&val, prio);
}

// ============================================================
// 0x1400dbc80 — spawn ONE fence piece.
//   sectionIdx indexes CreateFencedDescription+0x28 (see table in header).
//   (x,y,w,h) is the piece's cell rect; angleDeg the piece rotation (may be
//   negative — MapComponent stores it normalised, the model yaw keeps the raw
//   value). World pos = midpoint of the rect's two corner cell centres, z=0.
//   The new entity is appended to the master's MultiObject list + registered.
// ============================================================
void SpawnFencePiece_dbc80(FencedTool* self, int sectionIdx, int x, int y,
                           int w, int h, int angleDeg, MultiObjectComponent* const* multiObjIBPSlot) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return;
    Map* mapGrid = gw->m_game;

    float a[3], b[3], centre[3];
    CellWorldPos(mapGrid, x, y, a);
    CellWorldPos(mapGrid, x + w - 1, y + h - 1, b);
    centre[0] = (a[0] + b[0]) * 0.5f;
    centre[1] = (a[1] + b[1]) * 0.5f;
    centre[2] = 0.0f;                       // retail zeroes z (0x1400dbdfa)

    ComponentDescription* sectionTmpl = self->fencedDesc->_sections[sectionIdx];
    if (!sectionTmpl) return;

    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* ent = mem ? ::new (mem) ComponentEntity(sectionTmpl) : nullptr;
    if (!ent) return;

    MapComponent* mapComp = ent->FindComponent<MapComponent>();   // 0x140004680
    StampMapRect(mapComp, x, y, w, h, angleDeg);

    ModelComponent* model = ent->FindComponent<ModelComponent>();   // 0x140003f60
    if (model) ApplyModelYaw(model, static_cast<float>(angleDeg), centre);

    // Report WHERE every fence piece is put. A "gate model ended up in a lake" report is
    // unactionable without this: it cannot distinguish a bad cell rect (the caller walked
    // the wrong edge) from a bad world position (CellWorldPos / the terrain lookup) from a
    // piece that is simply drawn at z=0, which is lake level. Section 5 is the Gate.
    {
        static int n = 0;
        if (n < 24) { ++n;
            fprintf(stderr, "[FencePiece] idx=%d%s cell=(%d,%d) %dx%d angle=%d "
                            "world=(%.2f, %.2f, %.2f) mapWidth=%d\n",
                    sectionIdx, (sectionIdx == 5) ? " GATE" : "", x, y, w, h, angleDeg,
                    centre[0], centre[1], centre[2], mapGrid ? mapGrid->size : -1);
            fflush(stderr);
        }
    }

    // Append to the master's MultiObject list (arr at +0x48, count at +0x50).
    MultiObjectComponent* multi = *multiObjIBPSlot;
    if (multi) {
        AppendChild(multi, ent);
    }

    RegisterWithList(gw, ent, sectionTmpl);
}

// ============================================================
// 0x1400db8c0 — walk one edge of the rect from (x0,y0) to (x1,y1), spawning
// the fence run. Literal transcription of the retail control flow:
//   - cells covered by the shelter rect are a GAP; the first fence cell after
//     a gap gets an End piece at the base angle, the last fence cell before
//     the gap gets an End rotated to face it (180 for horizontal, -90 for
//     vertical edges);
//   - a 1-cell Straight (idx 3) is used when fewer than two cells remain or
//     the 2-cell piece would collide with the shelter;
//   - otherwise 2-cell pieces: StraightLong (idx 4), with exactly ONE Gate
//     (idx 5) after the first long piece on edges the shelter does not touch
//     (the shelter itself is the entrance on its own edge).
// ============================================================
void EdgeRun_db8c0(FencedTool* self, int x0, int y0, int x1, int y1,
                   int shMinX, int shMinY, int shMaxX, int shMaxY,
                   int angleDeg, MultiObjectComponent* const* multiObjIBPSlot) {
    bool prevFence  = false;   // bVar2: previous cell was a fence cell
    bool afterGap   = false;   // bVar5: a shelter gap has been seen on this run
    bool prevLong   = false;   // bVar3: previous spawn was a long piece
    bool gateLatch  = false;   // bVar6
    bool gateLatch2 = false;   // bVar7
    const bool stepXOn = (x1 - x0) > 0;
    const int  spanY   = y1 - y0;
    const int  stepY   = (spanY > 0) ? 1 : 0;

    int wLong, hLong;
    if (angleDeg == 0) { wLong = 2; hLong = 1; }
    else               { wLong = 1; hLong = 2; }

    // bVar4: TRUE when the shelter touches none of this run's cells.
    bool noShelterOnRun = true;
    if (self->fencedDesc->_centerBuilding != 0 && y0 <= y1) {
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx)
                if (shMinX <= xx && xx <= shMaxX && shMinY <= yy && yy <= shMaxY)
                    noShelterOnRun = false;
    }

    int y = y0;
    for (;;) {
        if (y1 < y) return;
        const int stepX = stepXOn ? 1 : 0;
        if (x0 <= x1) {
            int x     = x0;
            int lookX = x0 + stepX;
            do {
                const int lookY = y + stepY;
                const bool inShelter = (shMinX <= x && x <= shMaxX &&
                                        shMinY <= y && y <= shMaxY);
                if (!inShelter) {
                    int idx = -1, ang = angleDeg;
                    bool spawned1Cell = false;
                    if (afterGap && !prevFence) {
                        idx = 1; ang = angleDeg; spawned1Cell = true;      // End after the gap
                    } else if (prevFence &&
                               shMinX <= lookX && lookX <= shMaxX &&
                               shMinY <= lookY && lookY <= shMaxY) {
                        idx = 1;                                           // End before the gap
                        ang = (angleDeg == 0) ? 0xb4 : -0x5a;
                        spawned1Cell = true;
                    } else if ((x1 < x + 2 * stepX) || (y1 < y + 2 * stepY) ||
                               (shMinX <= lookX && lookX <= shMaxX &&
                                shMinY <= lookY && lookY <= shMaxY)) {
                        idx = 3; ang = angleDeg; spawned1Cell = true;      // 1-cell Straight
                    }
                    if (spawned1Cell) {
                        MultiObjectComponent* val = *multiObjIBPSlot;
                        SpawnFencePiece_dbc80(self, idx, x, y, 1, 1, ang, &val);
                        gateLatch = gateLatch2;
                    } else if (gateLatch || !prevLong || !noShelterOnRun) {
                        MultiObjectComponent* val = *multiObjIBPSlot;
                        SpawnFencePiece_dbc80(self, 4, x, y, wLong, hLong, angleDeg, &val); // StraightLong
                        x += stepX; lookX += stepX; y += stepY;
                        prevLong = true;
                        gateLatch = gateLatch2;
                    } else {
                        MultiObjectComponent* val = *multiObjIBPSlot;
                        SpawnFencePiece_dbc80(self, 5, x, y, wLong, hLong, angleDeg, &val); // Gate
                        x += stepX; lookX += stepX; y += stepY;
                        gateLatch = true;
                    }
                    prevFence = true;
                    gateLatch2 = gateLatch;
                } else {
                    prevFence = false;
                    afterGap = true;
                    prevLong = false;
                }
                x += 1;
                lookX += 1;
            } while (x <= x1);
        }
        y += 1;
    }
}

} // namespace

// ============================================================
// 0x1400dae00 — FencedTool::CalcPath (Slot13). Returns 1 on commit, 0 on abort.
// ============================================================
bool FencedTool::CalcPath(int startCell, int endCell) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw || !fencedDesc) {
        return 0;
    }

    // 1. Validate through the virtual CalcCost (slot 14).
    uint32_t dummyW = 0;
    int rectW = 0, rectH = 0, segCount = 0;
    int rc = CalcCost(static_cast<int>(startCell), static_cast<int>(endCell),
                      &dummyW, &rectW, &rectH, &segCount);
    if (rc != 0) {
        return 0;
    }

    Map* mapGrid = gw->m_game;
    const int W = mapGrid->size;
    int minCol = static_cast<int>(startCell) % W, minRow = static_cast<int>(startCell) / W;
    int maxCol = static_cast<int>(endCell) % W,   maxRow = static_cast<int>(endCell) / W;
    if (maxCol < minCol) { int t = minCol; minCol = maxCol; maxCol = t; }
    if (maxRow < minRow) { int t = minRow; minRow = maxRow; maxRow = t; }

    // 2. Edit-mode guard (re-fence must match the stored rect). Retail reads
    //    tool+0x10 unconditionally; null-guarded here (SA: the placement dialog
    //    object may be absent in harness flows — treated as "not editing").
    PlayerToolManager* editObj = dialogPtr;
    if (editObj && editObj->tutorialRestricted != 0) {
        int sc = editObj->tutorialStartCell;
        int ec = editObj->tutorialEndCell;
        if (minCol != sc % W || minRow != sc / W || maxCol != ec % W || maxRow != ec / W) {
            return 0;
        }
    }

    // 3. Shelter placement + yaw.
    uint32_t shMinX = 0, shMinY = 0, shMaxX = 0, shMaxY = 0;
    float shelterPos[3] = { 0.f, 0.f, 0.f };
    uint32_t yawBits = 0;   // 0x1400dbf90 stores the yaw float's bits
    float yawF = 0.f;
    CalcAnchor(&shMinX, &shMinY, &shMaxX,
               &shMaxY, shelterPos, &yawBits);
    std::memcpy(&yawF, &yawBits, sizeof yawF);   // the yaw float from its bits
    const int yawI = static_cast<int>(yawF);

    // 4. Master entity from the tool's description template (tool+0x20).
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* master = mem ? ::new (mem) ComponentEntity(descriptionPtr) : nullptr;
    if (!master) return 0;

    MapComponent* masterMap = master->FindComponent<MapComponent>();   // 0x140004680
    StampMapRect(masterMap, minCol, minRow, rectW, rectH, yawI);

    MultiObjectComponent* multi = master->FindComponent<MultiObjectComponent>();   // 0x140004740

    {
        const ComponentDescription* clearTmpl = fencedDesc->_allowAndRemove;
        DragPlacementTool_ClearPathEntities_1018d0(master, minCol, minRow,
                                                   maxCol, maxRow, clearTmpl);
    }

    // 5. Shelter entity from sections[0].
    ComponentDescription* shelterTmpl = fencedDesc->_sections[0];
    ComponentEntity* smem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* shelter = (smem && shelterTmpl) ? ::new (smem) ComponentEntity(shelterTmpl) : nullptr;
    if (shelter) {
        MapComponent* shMap = shelter->FindComponent<MapComponent>();   // 0x140004680
        StampMapRect(shMap, static_cast<int>(shMinX), static_cast<int>(shMinY),
                     static_cast<int>(shMaxX - shMinX) + 1,
                     static_cast<int>(shMaxY - shMinY) + 1, yawI);

        ModelComponent* shModel = shelter->FindComponent<ModelComponent>();   // 0x140003f60
        if (shModel) ApplyModelYaw(shModel, yawF, shelterPos);

        PickingComponent* shPick = shelter->FindComponent<PickingComponent>();   // 0x140026a30
        ComponentEntity* masterVal = master;
        SeatPickOwner_1290d0(shPick, &masterVal);

        if (multi) {
            AppendChild(multi, shelter);
        }
        RegisterWithList(gw, shelter, shelterTmpl);
    }

    // 6. Perimeter cell count on the MultiObjectComponent.
    if (multi)
        multi->multiCount = (rectH + rectW) * 2 - 4;

    const int sMinX = static_cast<int>(shMinX), sMinY = static_cast<int>(shMinY);
    const int sMaxX = static_cast<int>(shMaxX), sMaxY = static_cast<int>(shMaxY);
    MultiObjectComponent* multiVal = multi;

    // 7. Corners (skip any the shelter covers) + 4 edge runs.
    if (minCol < sMinX || sMaxX < minCol || minRow < sMinY || sMaxY < minRow)
        SpawnFencePiece_dbc80(this, 2, minCol, minRow, 1, 1, 0x5a, &multiVal);
    if (minCol < sMinX || sMaxX < minCol || maxRow < sMinY || sMaxY < maxRow)
        SpawnFencePiece_dbc80(this, 2, minCol, maxRow, 1, 1, 0, &multiVal);
    if (maxCol < sMinX || sMaxX < maxCol || maxRow < sMinY || sMaxY < maxRow)
        SpawnFencePiece_dbc80(this, 2, maxCol, maxRow, 1, 1, -0x5a, &multiVal);
    if (maxCol < sMinX || sMaxX < maxCol || minRow < sMinY || sMaxY < minRow)
        SpawnFencePiece_dbc80(this, 2, maxCol, minRow, 1, 1, 0xb4, &multiVal);

    EdgeRun_db8c0(this, minCol + 1, minRow, maxCol - 1, minRow,
                  sMinX, sMinY, sMaxX, sMaxY, 0, &multiVal);
    EdgeRun_db8c0(this, minCol + 1, maxRow, maxCol - 1, maxRow,
                  sMinX, sMinY, sMaxX, sMaxY, 0, &multiVal);
    EdgeRun_db8c0(this, minCol, minRow + 1, minCol, maxRow - 1,
                  sMinX, sMinY, sMaxX, sMaxY, 0x5a, &multiVal);
    EdgeRun_db8c0(this, maxCol, minRow + 1, maxCol, maxRow - 1,
                  sMinX, sMinY, sMaxX, sMaxY, 0x5a, &multiVal);

    // 8. Register the master entity (priority from the tool's own template).
    RegisterWithList(gw, master, descriptionPtr);

    return 1;
}
