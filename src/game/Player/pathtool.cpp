// src/game/Player/pathtool.cpp
// Clean reimplementation of PathTool vtable methods from Application-x64.exe.
//
// PathTool (0x60 bytes) extends Tool (0x28 bytes) directly (NOT DragTool).
// Handles path placement: multi-point entity selection with highlighting.
//
// Vtable at 0x140160680 (13 slots). Four are reimplemented here:
//   Slot0  Dtor      (0x140026b70) — scalar deleting destructor
//   Slot6  OnKey     (0x1400de6b0) — path creation input handler
//   Slot8  Draw      (0x1400dd3b0) — render path preview
//   Slot10 Serialize (0x1400de8d0) — save/load entity comparison
//
// Decompiled from Application-x64.exe.

#include "Player/PathTool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "math/Math.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Player/exe_bodies.h"
#include "Game/Map.h"
#include "Entity/ComponentEntity.h"
#include "Game/WorldGeometry.h"
#include "system/MemoryAllocator.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)

// ============================================================
// PathTool helpers, called by name (Player/exe_bodies.h):
//   0x1401005a0 PlayerRay_GetMouseRay_1005a0 -- ray from the mouse position
//               (this call site passes (keyMap, dir, origin))
//   0x1400aa530 PlayerRay_TerrainMarch_aa530 -- intersect the ray with the terrain
//   0x1400a0280 PlayerEdit_EntityAtCell_a0280 -- entity at a map cell
//   0x1400dd710 PathTool::SetSelected -- SetSelectedEntity (+ highlighting)
//   0x1401008c0 PlayerTileRender_DrawOverlay_08c0 -- the tile overlay submit
// NOTE: the former s_fn_SubmitDebugDraw helper was removed — it resolved a
// dropped-digit-typo address (0x1400c08c0, mid-function in FUN_1400c0690) and was
// never called. Dead code.
// ============================================================

// ============================================================
// Helper: get KeyMap from external reference
// ============================================================
static inline const Input::KeyMap* GetKeyMap() {
    return Input::Configuration::_singleton->_keyMap;
}

// 0x1400de950 — ~PathTool: the leaf restamp, then three inlined
// Collection::Array<T>::~Array bodies (+0x50, +0x40, +0x30: MemFree the buffer,
// zero data / size / capacity), then Tool's restamp and `~Internal`. B4 PLAYER:
// the three arrays are Collection::Array members (PathTool.h), so the compiler's
// member destructors are those bodies, in that order.

// ============================================================
// PathTool::OnKey (vtable +0x30, Slot6) — Path creation input handler
// ============================================================
// On key-down with Pressed(0,5): picks map cell under cursor via terrain
// raycast and picking scene intersection, then selects/highlights the entity.
//
// Flow:
//   1. Check if primary action button is pressed (KeyMap::Pressed(0,5))
//   2. Compute mouse ray (GetMouseRay)
//   3. Raycast terrain to get world position -> convert to cell index
//   4. Get entity at cell via GetEntityAtCell
//   5. Also try Picking::Scene::Intersect for 3D object picking
//   6. If found entity differs from current selection, call SetSelectedEntity
//   7. Always returns 1
//
// Original: 0x1400de6b0
char PathTool::OnKey(char key) {
    if (key == '\0') {
        return 1;
    }

    // Check if primary action button is pressed
    const Input::KeyMap* keyMap = GetKeyMap();

    // KeyMap::Pressed(keyMap, 0, 5) — retail calls it through exe slot 0x140151758.
    bool pressed = keyMap->Pressed(0, ToUnderlying(GameKey::MouseLeft));
    if (!pressed) return 1;

    // The map
    Map* mapData = GameUpdateLoop::_singleton->m_game;

    // Compute mouse ray (the first out is what 0x1401005a0 writes as the origin;
    // the names below are the tree's, the order is retail's)
    float rayOrigin[3] = {0};  // 12 bytes (vec3)
    float rayDir[3] = {0};     // 12 bytes (vec3)
    float distance[2] = {0};   // raycast distance param
    PlayerRay_GetMouseRay_1005a0(keyMap, rayDir, rayOrigin);

    // Raycast terrain
    distance[0] = 400.0f;
    ComponentEntity* entityIBP = nullptr;
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;
    float outPos[4] = {0};

    // local_res18 is used for terrain height param: 400.0f
    float terrainParam[2] = {0};
    terrainParam[0] = 400.0f;  // 0x43c80000 as float

    WorldGeometry* mapSystem = GameUpdateLoop::_singleton->m_worldGeometry;
    if (mapSystem) {
        bool hit = mapSystem->TerrainMarch(rayDir, rayOrigin,
            terrainParam, outPos, distance) != 0;
        if (hit) {
            int gridSize = mapGrid->size;
            int cellX = (int)(outPos[0] + (float)(gridSize >> 1));
            int cellY = (int)(outPos[1] + (float)(gridSize >> 1));
            // Clamp to grid bounds
            if (cellX < 0) cellX = 0;
            if (cellX >= gridSize) cellX = gridSize - 1;
            if (cellY < 0) cellY = 0;
            if (cellY >= gridSize) cellY = gridSize - 1;
            int cellIndex = gridSize * cellY + cellX;
            if (cellIndex > 0) {
                ComponentEntity* retIBP = nullptr;
                mapData->EntityAtCell(&retIBP, cellIndex);
                entityIBP = retIBP;
            }
        }
    }

    // Try picking scene intersection as well
    Math::Vector3 hitNormal(0.0f, 0.0f, 0.0f);
    Math::Vector3 hitPos(0.0f, 0.0f, 0.0f);
    Picking::Scene* pickingScene = GameUpdateLoop::_singleton->m_pickingScene;
    if (pickingScene) {
        // Picking::Scene::Intersect / GetUserData (import slots 0x140152428 /
        // 0x140152890), arguments in the door's positional order.
        uint32_t hitIdx = static_cast<uint32_t>(pickingScene->Intersect(
            Math::Vector3(rayDir[0], rayDir[1], rayDir[2]),
            Math::Vector3(rayOrigin[0], rayOrigin[1], rayOrigin[2]),
            Math::Scalar(distance[0]),
            hitNormal, hitPos));
        if ((int)hitIdx >= 0) {
            // Picking::Scene's user data is `void*` (retail's API); the bridge and
            // the picking component store the owning ComponentEntity there.
            entityIBP = reinterpret_cast<ComponentEntity*>(pickingScene->GetUserData(hitIdx));   // Picking::Scene keeps its user data as void* (runtime header, not this set's); the owner stored there is the ComponentEntity  // CAST-OK: user_data
        }
    }

    // If we found an entity and it differs from current selection, update
    if (entityIBP != nullptr) {
        if (selectedEntity != entityIBP) {
            SetSelected(&entityIBP);
        }
    }

    return 1;
}

// ============================================================
// Draw records: ToolTileRecord (0x14) and ToolSegmentRecord (0x20), Player/Tool.h,
// in real Collection::Arrays (stride 0x14 / 0x20, align 4, byteCopy). B4 PLAYER:
// the two hand-written {data, count, capacity} arrays and their toEngineFormat
// packs are gone. Their grow went to n*3 even from empty (capacity 3 for the first
// record). Retail's inlined grow is SetSizeCore's rule: capacity n, trebled only
// when one record is appended to a non-empty array (0x1400dd47c-0x1400dd487 for the
// tiles, the same shape from 0x1400dd5d0 for the segments), so the first grow is
// capacity 1, as here (known_port_issues 14 row 75).
// ============================================================

// ============================================================
// PathTool::Draw (vtable +0x40, Slot8) — Render path preview overlay
// ============================================================
// Iterates two data arrays stored in the PathTool:
//   1. Point markers at +0x40 (int array) / +0x48 (count): single-cell highlights
//   2. Line segments at +0x50 (int pair array) / +0x58 (count): cell-to-cell lines
//
// For each point marker, converts flat cell index to (col, row) and builds a
// 0x14-byte tile record with variant=4, color=0xfffccc4c.
//
// For each line segment, reads two cell indices, converts both to (col, row),
// and builds a 0x20-byte segment record with variant=5, color=0xfffccc4c,
// width=0.15f.
//
// Both arrays are then passed to 0x1401008c0, which handles the actual GPU
// vertex submission (material setup, quad generation, etc.).
//
// Original: 0x1400dd3b0
void PathTool::Draw(Video::Context* context, char) {
    int mapWidth = GameUpdateLoop::_singleton->m_game->size;

    // ---- Build tile array from point markers (+0x40, +0x48) ----
    Collection::Array<ToolTileRecord> tiles;
    const int32_t* pointArray = points._data;
    int pointCount  = points._size;

    for (int i = 0; i < pointCount; i++) {
        int cellIdx = pointArray[i];
        ToolTileRecord rec;
        rec.col     = cellIdx % mapWidth;
        rec.row     = cellIdx / mapWidth;
        rec.variant = 4;
        rec.color   = 0xfffccc4cu;
        rec.angle   = 0;
        tiles.SetSize(tiles._size + 1, rec);
    }

    // ---- Build line array from segments (+0x50, +0x58) ----
    Collection::Array<ToolSegmentRecord> lines;
    const PathSegment* segArray = segments._data;
    int segCount = segments._size;

    for (int i = 0; i < segCount; i++) {
        int cellA = segArray[i].minCell;
        int cellB = segArray[i].maxCell;
        ToolSegmentRecord rec;
        rec.col1    = cellA % mapWidth;
        rec.row1    = cellA / mapWidth;
        rec.variant = 5;
        rec.color   = 0xfffccc4cu;
        rec.angle   = 0;
        rec.col2    = cellB % mapWidth;
        rec.row2    = cellB / mapWidth;
        rec.width   = 0.15f;  // 0x3e19999a
        lines.SetSize(lines._size + 1, rec);
    }

    // ---- Render via 0x1401008c0 ----
    PlayerTileRender_DrawOverlay_08c0(nullptr, context, &tiles, &lines);
    // lines / tiles are released by ~Array at scope exit.
}

// ============================================================
// PathTool::Serialize (vtable +0x50, Slot10) — Entity comparison
// ============================================================
// Calls CheckPtr on self->selectedEntity (+0x28), then compares *param2
// to the selected entity value. Returns true if they match.
//
// Original: 0x1400de8d0
bool PathTool::IsSelected(ComponentEntity* const* param2) {
    // Call CheckPtr on self->selectedEntity to resolve any lazy pointer

    // Compare the stream value to the selected entity
    return *param2 == selectedEntity;
}

// [13] 0x140026b60 `MOV RAX,[RCX+0x28]; MOV [RDX],RAX; MOV RAX,RDX; RET`.
ComponentEntity** PathTool::GetSelected(ComponentEntity** ret) const {
    *ret = selectedEntity;
    return ret;
}

// ============================================================
// PathTool members (PathTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_PathTool_Vtable[13], its slot stubs, the _SA factories and
// RegisterPathToolVtable are gone: the compiler builds the table, and nothing
// reads 0x140160680 back by address (the old dtor was the only reader).
// Inherited from Tool, as in retail: [11] no setting (0x140004950 `*ret = 0`;
// SetEditMode dereferences the returned slot).
// ============================================================
#include "Object/factory_registration.h"
#include <new>

// [6] OnKey (0x1400de6b0), [8] Draw (0x1400dd3b0) and [10] IsSelected (0x1400de8d0)
// are defined above; [5] Shutdown (0x1400de930, a bare JMP to 0x1400dd250) is in
// pathtool_setselected.cpp next to the helper it jumps to.

// create 0x140026b10: MemAlloc(0x60, 8), then the ctor 0x1400de9e0.
::Object::Base* PathTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PathTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PathTool());
}
// createInPlace 0x140026b50: `if (storage) ctor(storage)`.
::Object::Base* PathTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PathTool());
}

DEFINE_EXE_TYPE(PathTool, "Tool")
