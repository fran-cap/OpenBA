// bridgetool_calcpath.cpp — BridgeTool::CalcPath (0x1400f9cd0), the bridge COMMIT.
//
// Dispatched from vtable slot 13 when a bridge drag is released. Before this file the
// slot delegated to ResolveAddr(Module::Exe, 0x1400f9cd0) — a leftover from the removed
// proxy-DLL mode. In the standalone exe there is no original exe to delegate to, so it
// resolved to 0, logged "[BridgeTool] ERROR: CalcPath function pointer is NULL", and no
// bridge was ever built.
//
// Shape (823 decompiled lines, condensed):
//   1. Snap the end cell, then gate on ValidatePath (bridgetool_validate.cpp).
//   2. Build a doubly-linked list of segment descriptors covering the span. Two size-0
//      end nodes (the bank pieces, drawn as 1 cell each) bracket a middle filled
//      CENTRE-OUTWARD with the largest fitting power-of-2 segment (8/4/2/1).
//      Node = MemAlloc(0x18, 8): +0x00 size(int) · +0x08 prev · +0x10 next.
//      In Ghidra the nodes decompile as float* and the sizes as denormals —
//      1.4013e-45 = 1, 2.8026e-45 = 2, 5.60519e-45 = 4, 1.12104e-44 = 8.
//   3. Clone a master ComponentEntity from the tool's description; its MapComponent
//      covers the whole span (plus the description's clearance radius) and its
//      MultiObjectComponent collects the segment entities.
//   4. Per segment: clone from CreateBridgeDescription's segmentTemplates (+0x38[0..4],
//      indexed 0=end piece, 1/2/3/4 = 1/2/4/8-cell), stamp cell coords + rotation onto its
//      MapComponent, build the yaw transform onto its ModelComponent, append it to the
//      master's MultiObjectComponent, and register it with the world entity list.
//   5. Stamp the total cell count on the MultiObjectComponent, register the master, free
//      the list.
//
// Component field layout is shared with the working DragPlacementTool commit
// (dragplacementtool.cpp): +0x28 col · +0x2a row · +0x2c width · +0x2e height (all int16)
// · +0x30 rotation (int32, degrees, normalised into [0,360) by adding 0x168).
// ComponentEntity component array is at entity+0x30 with the count at entity+0x38 —
// NOT the +0x28/+0x30 pair used for *description* entities in BridgeTool::Startup.
//
// The f3e0 terrain-height/world-position math IS ported (CellRectCentre below). It was
// once dismissed as dead — "retail computes it and then passes a ZERO offset" — which was
// a misread of FromYawPitchOffset's argument order: the zero is the PITCH, the position is
// the offset. Dropping it parked every bridge entity at the world origin and silently made
// the bridge unbuildable; see the note on ApplyYaw.
// (Picking used to be listed here as an omission. It is DONE now: the per-segment
//  PickingComponent is re-owned to the master entity and its mesh is registered, so a
//  click anywhere on the span selects the bridge — see SeatSegmentPickOwner below.)

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Geometry.h"
#include "BridgeTool.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/PickingComponent.h"
#include <new>
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Game/exe_bodies.h"                // 0x14009f3e0 Reimpl_MapGetTerrainPos
#include "Player/exe_bodies.h"              // 0x1400fb710 / 0x1400fb2d0
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)
#include "Component/ComponentDescription.h"
#include "Component/CreateBridgeDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"

namespace {

// ---- segment list -------------------------------------------------------------
struct SegNode {
    int32_t  size;      // +0x00  0 = bank end piece (drawn 1 cell wide)
    int32_t  _pad;      // +0x04
    SegNode* prev;      // +0x08
    SegNode* next;      // +0x10
};
static_assert(sizeof(SegNode) == 0x18, "SegNode must match MemAlloc(0x18,8)");

SegNode* SegAlloc(int size) {
    SegNode* n = static_cast<SegNode*>(System::MemoryAllocator::MemAlloc(0x18, 8));
    if (!n) return nullptr;
    n->size = size; n->_pad = 0; n->prev = nullptr; n->next = nullptr;
    return n;
}

// Largest power of two (capped at 8) that fits in n — retail's nested <8/<4/<2 ladder.
inline int LargestFit(int n) {
    if (n >= 8) return 8;
    if (n >= 4) return 4;
    if (n >= 2) return 2;
    return (n > 0) ? 1 : 0;
}

void SegInsertAfter(SegNode* at, SegNode* n) {
    n->prev = at; n->next = at->next;
    if (at->next) at->next->prev = n;
    at->next = n;
}
void SegInsertBefore(SegNode* at, SegNode* n) {
    n->next = at; n->prev = at->prev;
    if (at->prev) at->prev->next = n;
    at->prev = n;
}

// ---- component lookup ---------------------------------------------------------
// ComponentEntity::FindComponent<T> (ComponentEntity.h): the walk over the +0x30
// component array (+0x38 count), Type() then IsDerivedFrom. A null entity has no
// components here (the tree's own guard, kept).
template <class T>
T* FindComponentOn(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<T>();
}

// Stamp cell rect + rotation onto the entity's MapComponent.
void StampCells(MapComponent* comp, int col, int row, int w, int h, int rotation) {
    if (!comp) return;
    comp->tileX0 = static_cast<int16_t>(col);
    comp->tileY0 = static_cast<int16_t>(row);
    comp->tileX1 = static_cast<int16_t>(w);
    comp->tileY1 = static_cast<int16_t>(h);
    while (rotation < 0) rotation += 0x168;      // retail normalises by +360 until >= 0
    comp->extraField = rotation;
}

// World-space centre of a cell rect: sample the terrain position of the two opposite
// corner cells and average them. Retail does exactly this twice — once for the master
// (0x1400fa218..0x1400fa363) and once per segment (0x1400fa6f1..0x1400fa7e4) — via
// FUN_14009f3e0 (Reimpl_MapGetTerrainPos). Cell -> map-space is (col - c, row - c) with
// c = (gridSize >> 1) - 0.5, and the final z is forced to 0 (0x1400fa35d).
bool CellRectCentre(Map* mapGrid, int col0, int row0, int col1, int row1, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!mapGrid) return false;
    const int   W = mapGrid->size;
    const float c = static_cast<float>(W >> 1) - 0.5f;
    float inA[3] = { static_cast<float>(col0) - c, static_cast<float>(row0) - c, 0.0f };
    float inB[3] = { static_cast<float>(col1) - c, static_cast<float>(row1) - c, 0.0f };
    float a[3] = {0.f, 0.f, 0.f}, b[3] = {0.f, 0.f, 0.f};
    mapGrid->GetTerrainPos(a, inA, '\0', nullptr);
    mapGrid->GetTerrainPos(b, inB, '\0', nullptr);
    out[0] = (a[0] + b[0]) * 0.5f;
    out[1] = (a[1] + b[1]) * 0.5f;
    out[2] = 0.0f;
    return true;
}

// Build the yaw+offset transform and apply it to a ModelComponent: the Transform at
// +0x148 (0x1c bytes), the cached yaw at +0x168, and the MeshInstance's transform.
//
// The offset is NOT decorative and was the reason a placed bridge was never built.
// Retail passes the cell-rect centre computed above as FromYawPitchOffset's 4th
// argument (0x1400fa57c `LEA R9,[RBP-0x38]` for the master, 0x1400faa5d
// `LEA R9,[RSP+0x50]` per segment); the ZERO argument one slot over is the PITCH
// (`LEA R8,[RSP+0x30]` / `[RBP-0x70]`, both explicitly stored 0). The transform is what
// FUN_140122be0 (GatherRawMaterials' CalcBestTargetPos) walks to find a tile a
// builder can stand on.
void ApplyYaw(ModelComponent* model, float yawDeg, const float offset[3]) {
    if (!model) return;
    Math::Vector3 off(offset ? offset[0] : 0.f,
                      offset ? offset[1] : 0.f,
                      offset ? offset[2] : 0.f);
    Math::Transform xform = Math::Transform::FromYawPitchOffset(yawDeg, 0.f, off);

    model->transform = xform;   // the seven dwords at +0x148
    model->yaw = yawDeg;        // +0x168
    model->meshInstance.SetTransform(model->transform);
}

// ---- picking ------------------------------------------------------------------
// Retail tail of the per-segment block (0x1400faab3 .. 0x1400fab7b): find the SEGMENT
// entity's PickingComponent (type id DAT_1401c7c10), then
//
//     pick->userData = masterEntity;                            // always
//     if (pick->pickingId >= 0)                                 // already has a mesh
//         Picking::Scene::SetMeshUserData(gw->pickScene, pickingId, userData, 0, 0, nullptr);
//
// i.e. every deck piece is re-OWNED by the master bridge entity, so a click anywhere
// on the span selects the bridge as one object. +0x30 is exactly what
// PickingComponent::OnAdded later hands to Picking::Scene::AddMesh as the pick
// userData, so seating it here is the load-bearing half.
void SeatSegmentPickOwner(ComponentEntity* seg, ComponentEntity* masterEntity,
                          GameUpdateLoop* gameWorld) {
    PickingComponent* pc = FindComponentOn<PickingComponent>(seg);
    if (!pc) return;
    pc->userData = masterEntity;

    const int meshId = pc->pickingId;
    if (meshId < 0) return;              // no mesh yet — OnAdded/RegisterMesh reads +0x30

    Picking::Scene* scene = gameWorld ? gameWorld->m_pickingScene : nullptr;
    if (scene)
        scene->SetMeshUserData(static_cast<uint32_t>(meshId), pc->userData.Get(), 0, 0, nullptr);
}

// Construct a ComponentEntity (0x48 bytes) from a template/description.
ComponentEntity* CloneEntity(ComponentDescription* templateEntity) {
    if (!templateEntity) return nullptr;
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    if (!mem) return nullptr;
    return ::new (mem) ComponentEntity(templateEntity);   // 0x1400c3720
}

}  // namespace

// Last-commit record, for the standalone bridge harness (BANISHED_TOOL_HARNESS=bridge).
// Same role as g_pConstructionDemoEntity: gives a headless test something to poll
// without having to re-derive the entities from the map grid. Diagnostic only —
// nothing in the game path reads these.
extern "C" {
ComponentEntity* g_BridgeLastMaster = nullptr;
ComponentEntity* g_BridgeLastSegments[32] = {};
int   g_BridgeLastSegmentCount = 0;
}

// ============================================================
// BridgeTool::CalcPath (vtable +0x68, Slot13) — 0x1400f9cd0
// ============================================================
bool BridgeTool::CalcPath(int startCell, int endCell) {
    if (startCell < 0 || endCell < 0) return 0;

    const int snapped = SnapEndCell(endCell);
    int bodyLen = 0;
    if (!ValidatePath(startCell, snapped, &bodyLen)) return 0;
    if (bodyLen <= 0) return 0;

    const CreateBridgeDescription* desc = bridgeDesc;
    if (!desc) return 0;
    const uint32_t maskBody = desc->_placeBits;
    const int      radius   = desc->_edgeWidth;

    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return 0;
    Map* mapGrid = gameWorld->m_game;
    if (!mapGrid) return 0;
    const int width = mapGrid->size;
    if (width <= 0) return 0;
    const MapCell* terrainBase = mapGrid->cells.GetData();
    if (!terrainBase) return 0;

    // ---- 1. Build the segment list ------------------------------------------------
    // Two size-0 end nodes, then the middle (bodyLen - 2 cells) filled centre-outward.
    SegNode* head = SegAlloc(0);
    SegNode* tail = SegAlloc(0);
    if (!head || !tail) return 0;
    head->next = tail; tail->prev = head;

    {
        int remaining = bodyLen - 2;
        if (remaining > 0) {
            const int first = LargestFit(remaining);
            SegNode* mid = SegAlloc(first);
            if (mid) SegInsertAfter(head, mid);

            // grow left from the centre
            int left = (remaining / 2) - (first / 2);
            SegNode* cursor = mid;
            while (left > 0 && cursor) {
                const int sz = LargestFit(left);
                if (sz <= 0) break;
                SegNode* n = SegAlloc(sz);
                if (!n) break;
                SegInsertBefore(cursor, n);
                cursor = n;
                left -= sz;
            }

            // grow right from the centre
            cursor = mid;
            for (int filled = (remaining / 2) + (first / 2); filled < remaining && cursor; ) {
                const int sz = LargestFit(remaining - filled);
                if (sz <= 0) break;
                SegNode* n = SegAlloc(sz);
                if (!n) break;
                SegInsertAfter(cursor, n);
                cursor = n;
                filled += sz;
            }
        }
    }

    // ---- 2. Span geometry + axis step ---------------------------------------------
    int startRow = startCell / width, startCol = startCell % width;
    int endRow   = snapped   / width, endCol   = snapped   % width;
    int minCol = startCol, maxCol = endCol;
    if (endCol < startCol) { minCol = endCol; maxCol = startCol; }
    int minRow = startRow, maxRow = endRow;
    if (endRow < startRow) { minRow = endRow; maxRow = startRow; }

    // First body cell in the span — where the deck actually starts.
    int bodyCol = minCol, bodyRow = minRow;
    {
        bool found = false;
        for (int row = minRow; row <= maxRow && !found; row++) {
            for (int col = minCol; col <= maxCol; col++) {
                const MapCell* cell = &terrainBase[width * row + col];
                const uint32_t bit = Bits(PathBitOf(cell->terrainType));
                if (maskBody & bit) { bodyCol = col; bodyRow = row; found = true; break; }
            }
        }
    }
    const int stepCol = (bodyCol != maxCol) ? 1 : 0;   // horizontal run
    const int stepRow = (bodyRow != maxRow) ? 1 : 0;   // vertical run

    // ---- 3. Master entity ----------------------------------------------------------
    ComponentEntity* master = CloneEntity(descriptionPtr);
    Object::InternalPtr<ComponentEntity> masterIBP = master;   // the InternalBasePtr's eight bytes
    if (!master) return 0;

    {
        int mw = stepCol * bodyLen, mh = stepRow * bodyLen;
        const int crossW = radius * stepRow * 2 + 1;
        const int crossH = radius * stepCol * 2 + 1;
        if (mw < crossW) mw = crossW;
        if (mh < crossH) mh = crossH;
        const int mCol = bodyCol - radius * stepRow;
        const int mRow = bodyRow - radius * stepCol;
        StampCells(FindComponentOn<MapComponent>(masterIBP.Get()), mCol, mRow, mw, mh, stepRow * 0x5a);
        float centre[3];
        CellRectCentre(mapGrid, mCol, mRow, mCol + mw - 1, mRow + mh - 1, centre);
        ApplyYaw(FindComponentOn<ModelComponent>(masterIBP.Get()),
                 static_cast<float>(stepRow) * 90.0f, centre);
    }
    MultiObjectComponent* multi = FindComponentOn<MultiObjectComponent>(masterIBP.Get());

    // ---- 4. Per-segment entities ---------------------------------------------------
    EntityList* entityList = gameWorld->m_entityList;
    int   cursorCol = bodyCol, cursorRow = bodyRow;
    int   placedCells = 0, placedSegs = 0;
    g_BridgeLastMaster = master;
    g_BridgeLastSegmentCount = 0;

    for (SegNode* n = head; n != nullptr; n = n->next) {
        const int rawSize = n->size;
        const int span    = (rawSize == 0) ? 1 : rawSize;   // end pieces occupy 1 cell

        int segW = span, segH = 1, rot = 0;
        if (stepCol == 0) { rot = 0x5a; segW = 1; segH = span; }   // vertical run
        // End pieces face outward.
        if (n == tail && stepCol == 0)      rot = 0x5a;
        else if (n == head && stepCol == 0) rot = -0x5a;
        else if (n == tail && stepRow == 0) rot = 0;
        else if (n == head && stepRow == 0) rot = 0xb4;

        // segmentTemplates[0..4] at desc+0x38: 0 = end piece, then 1/2/4/8 cells.
        int tplIdx = 0;
        if      (rawSize == 1) tplIdx = 1;
        else if (rawSize == 2) tplIdx = 2;
        else if (rawSize == 4) tplIdx = 3;
        else if (rawSize == 8) tplIdx = 4;

        ComponentDescription* tpl = desc->_sections[tplIdx];
        ComponentEntity* seg = CloneEntity(tpl);
        if (seg) {
            Object::InternalPtr<ComponentEntity> segIBP = seg;   // the InternalBasePtr's eight bytes

            StampCells(FindComponentOn<MapComponent>(segIBP.Get()), cursorCol, cursorRow, segW, segH, rot);
            float segCentre[3];
            CellRectCentre(mapGrid, cursorCol, cursorRow,
                           cursorCol + segW - 1, cursorRow + segH - 1, segCentre);
            ApplyYaw(FindComponentOn<ModelComponent>(segIBP.Get()), static_cast<float>(rot), segCentre);

            // Append to the master's MultiObjectComponent (array +0x48, count +0x50).
            if (multi) {
                const int newCount = multi->children._size + 1;
                // Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8
                // align 8, new tail zeroed (kSpec_PtrZero).
                multi->children.SetSize(newCount);
                if (multi->children._data) {
                    multi->children[newCount - 1] = Object::InternalPtr<ComponentEntity>(segIBP);
                    multi->children._size = newCount;
                }
            }

            // Re-own the segment's pick mesh to the master (retail 0x1400faab3..fab7b),
            // BEFORE the register — that is the window PickingComponent::OnAdded reads.
            SeatSegmentPickOwner(segIBP.Get(), master, gameWorld);

            if (entityList && tpl) {
                const int categoryId = static_cast<int>(tpl->_category);
                entityList->RegisterEntity(&segIBP, categoryId);
            }

            // RegisterEntity's OnAdded pass registers the segment's pick mesh
            // (PickingComponent::OnAdded 0x140128b90 -> 0x140128f50) under the +0x30
            // owner seated above, so a pick answers the master; retail calls nothing
            // more here (row 1261).
            if (g_BridgeLastSegmentCount < 32)
                g_BridgeLastSegments[g_BridgeLastSegmentCount++] = seg;
            placedSegs++;
        }

        placedCells += span;
        cursorCol   += segW * stepCol;
        cursorRow   += segH * stepRow;
    }

    // ---- 5. Finish: stamp the cell total, register the master, free the list --------
    if (multi)
        multi->multiCount = placedCells;

    if (entityList) {
        const int categoryId = static_cast<int>(descriptionPtr->_category);
        entityList->RegisterEntity(&masterIBP, categoryId);
    }

    for (SegNode* n = head; n != nullptr; ) {
        SegNode* next = n->next;
        System::MemoryAllocator::MemFree(n);
        n = next;
    }
    fprintf(stderr, "[Bridge] CalcPath: start=%d end=%d snapped=%d len=%d -> %d segments, "
            "%d cells, master=%p multi=%p (stepCol=%d stepRow=%d radius=%d)\n",
            startCell, endCell, snapped, bodyLen, placedSegs, placedCells,
            static_cast<void*>(master), static_cast<void*>(multi), stepCol, stepRow, radius);
    fflush(stderr);

    return 1;
}
