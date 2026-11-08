// src/game/Player/roadtool.cpp
// Clean reimplementation of RoadTool vtable methods from Application-x64.exe.
//
// RoadTool (0x48 bytes) extends DragTool (0x38 bytes).
// Handles road placement: path calculation, validation, and cost estimation.
//
// Vtable at 0x140161e70 (15 slots).
//
// Hookable slots reimplemented:
//   Slot0  Dtor    (0x14002e8c0) — scalar deleting destructor
//   Slot4  Startup (0x1401056e0) — find CreateRoadDescription component
//   Slot8  Draw    (0x140105190) — road path preview rendering
//
// Unhookable slots (clean code for standalone exe, NOT hooked):
//   Slot6  OnKey    (0x140105690) — road-specific drag logic
//   Slot13 CalcPath (0x140104d30) — road path calculation
//   Slot14 CalcCost (0x140104fe0) — road cost calculation
//
// Decompiled from Application-x64.exe.

#include "Player/roadtool_funcs.h"
#include "Player/dragtool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "object/Internal.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // RoadTool_sub_ce00 (0x14009ce00), called by name
#include "Player/exe_bodies.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include "collection/Collection.h"
#include "Component/CreateRoadDescription.h"
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)
#include "Component/ComponentDescription.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include <cstdint>
#include <cstdio>
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// RoadTool::Startup (vtable +0x20, Slot4) — Find description component
// ============================================================
// Searches the tool group's sub-descriptions for the CreateRoadDescription
// (cell 0x1401c74d8). Stores the found description (or nullptr) in
// this->roadDesc (+0x38). A type match that fails IsDerivedFrom ends the search.
//
// Original: 0x1401056e0
void RoadTool::Startup() {
    const ComponentDescription* entity = descriptionPtr;
    CreateRoadDescription* result = nullptr;

    if (!entity) {
        roadDesc = nullptr;
        return;
    }

    int count = entity->_subDescCount;
    Description** components = entity->_subDescs;
    uint32_t targetTypeId = CreateRoadDescription::TypeId();

    for (int i = 0; i < count; i++) {
        Description* comp = components[i];
        if (!comp) continue;

        // Virtual slot 2 (+0x10): Type()
        if (comp->Type() == targetTypeId) {
            // Verify with IsDerivedFrom before casting
            if (Object::IsDerivedFromNullSafe(comp, targetTypeId)) {
                result = static_cast<CreateRoadDescription*>(comp);
            }
            break;
        }
    }

    roadDesc = result;
}

// RoadTool::OnKey (vtable +0x30, Slot6)
// Calls DragTool::OnKey, then reads KeyMap::GetValue(0, 0x16) to set
// the modifierFlag at +0x40 (the "upgrade road" key).
// Original: 0x140105690
char RoadTool::OnKey(char key) {
    // Retail (0x140105690) opens with `CALL 0x1400fc6b0` = DragTool::OnKey — a direct
    // rel32 call to the base, NOT a virtual dispatch (dispatching through vtable[6]
    // would re-enter RoadTool::OnKey and recurse forever). This call IS the drag
    // state machine.
    DragTool::OnKey(key);

    // Read KeyMap::GetValue(0, 0x16) for the modifier flag
    const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
    short val = keyMap->GetValue(0, ToUnderlying(GameKey::PathMethod));
    modifierFlag = (val == 0) ? 1 : 0;

    return 1;
}

// RoadTool::CalcPath (vtable +0x68, Slot13): roadtool_calcpath.cpp.

// RoadTool::CalcCost (vtable +0x70, Slot14)
// Similar validation to CalcPath, but only returns cost/length without
// actually placing road segments.
// Original: 0x140104fe0
//
// Flow:
//   1. Sort cells (ensure start <= end)
//   2. Initialize outputs: *outCostA=0, *outCostB=0, *outValid=2, param_7=1
//   3. Validate cells >= 0
//   4. Check the owner's path constraint (+0x80 / +0x98 / +0x9c)
//   5. Check terrain flags (mask 0x5e0), blocker occupancy
//   6. Run the road A* (0x14009ce00)
//   7. On success: set outputs to path length, *outValid=1, return 0
//   8. On failure: return 3
int RoadTool::CalcCost(int startCell, int endCell, uint32_t* outValid,
                       int* outCostB, int* outCostA, int* outLength) {
    // Sort cells: ensure start <= end
    uint32_t sc = startCell;
    uint32_t ec = endCell;
    if ((int)ec < (int)sc) {
        sc = endCell;
        ec = startCell;
    }

    // Initialize outputs
    *outCostA = 0;
    *outCostB = 0;
    *outValid = 2;

    // Validate cell indices
    if ((int)sc < 0 || (int)ec < 0) {
        return 3;
    }

    *outLength = 1;

    // Check the owner's path constraint
    const PlayerToolManager* descEntity = dialogPtr;
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;

    if (descEntity && descEntity->tutorialRestricted) {
        int gridSize = mapGrid->size;
        uint32_t constraintA = gridSize * (descEntity->tutorialStartCell / gridSize) +
            descEntity->tutorialStartCell % gridSize;
        uint32_t constraintB = gridSize * (descEntity->tutorialEndCell / gridSize) +
            descEntity->tutorialEndCell % gridSize;

        // Cells must match the description constraints (in either order)
        if ((constraintA != sc || constraintB != ec) &&
            (constraintA != ec || constraintB != sc)) {
            return 3;
        }
    }

    // Check terrain flags at both cells
    const MapCell* cells = mapGrid->cells.GetData();
    uint32_t flagA = Bits(PathBitOf(cells[sc].terrainType));
    uint32_t flagB = Bits(PathBitOf(cells[ec].terrainType));
    uint32_t combinedFlags = flagA | flagB;

    // Check if terrain allows road placement (mask 0x5e0 = bits 5-10 must be clear)
    if ((combinedFlags & Bits(PathBits::NoRoad)) != 0) {
        return 3;
    }

    // Check cells are unoccupied (the blocker at +0x00 of the cell must be null)
    if (cells[sc].blocker != nullptr || cells[ec].blocker != nullptr) {
        return 3;
    }

    // Run the road A* (0x14009ce00). Retail passes mask `combinedFlags | 0xa17` and the
    // modifier byte at +0x40, writing the cell path into a stack buffer.
    int pathBuf[2048];   // retail reserves 8184 bytes for the same buffer
    int pathLength = 0;
    pathLength = mapGrid->FindRoadPath(static_cast<PathBits>(combinedFlags) | PathBits::RoadRoute, sc, ec, nullptr,
                                       pathBuf, modifierFlag, 1);
    if (pathLength != 0) {
        *outCostA  = pathLength;
        *outCostB  = pathLength;
        *outLength = pathLength;
        *outValid  = 1;
        return 0;
    }

    return 3;
}

// The tile records Draw hands to 0x1401008c0 (ToolTileRecord, Player/Tool.h).
// ENDGAME B.3.2 step 2: a `Collection::Array` (stride 0x14, align 4, byteCopy).
using TileArray = Collection::Array<ToolTileRecord>;

// ============================================================
// Helper: push a tile record into a dynamic array
// ============================================================
static void PushTileEntry(TileArray* arr, const ToolTileRecord* entry) {
    arr->SetSize(arr->_size + 1, *entry);
}

// Draw calls 0x140101270 PlayerRay_ScreenToCell_101270 and 0x1401008c0
// PlayerTileRender_DrawOverlay_08c0 by name, and the road A* too.

// ============================================================
// RoadTool::Draw (vtable +0x40, Slot8)
// ============================================================
// Renders colored tile overlay for road placement preview.
// Two phases:
//   1. Constrained path from the owner's constraint (+0x80 flag)
//   2. Drag selection with pathfinding (strict then relaxed mask)
//
// Original: 0x140105190
void RoadTool::Draw(Video::Context* context, char param3) {
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;
    const PlayerToolManager* descEntity = dialogPtr;
    int pathBuf[2048]; // large stack buffer for pathfinding output

    // ---- Phase 1: Constrained path from the owner's constraint ----
    if (descEntity->tutorialRestricted) {
        int gridSize = mapGrid->size;
        int constrainStart = descEntity->tutorialStartCell;
        int constrainEnd   = descEntity->tutorialEndCell;

        // Normalize to cell indices
        int startNorm = gridSize * (constrainStart / gridSize) + constrainStart % gridSize;
        int endNorm   = gridSize * (constrainEnd / gridSize) + constrainEnd % gridSize;

        int pathLen = 0;
        pathLen = mapGrid->FindRoadPath(PathBits::RoadRoute, (uint32_t)startNorm, (uint32_t)endNorm,
                            nullptr, pathBuf, modifierFlag, 1);

        // Build tile array from path results
        TileArray pathTiles;
        for (int i = 0; i < pathLen; i++) {
            ToolTileRecord entry;
            entry.col     = pathBuf[i] % mapGrid->size;
            entry.row     = pathBuf[i] / mapGrid->size;
            entry.variant = 0;
            entry.color   = 0xffffff00; // yellow default
            entry.angle   = 0;
            PushTileEntry(&pathTiles, &entry);
        }

        // Render: path tiles in R8, empty array in R9
        Collection::Array<ToolSegmentRecord> emptyTiles;
        PlayerTileRender_DrawOverlay_08c0(nullptr, context, &pathTiles, &emptyTiles);

        // pathTiles / emptyTiles are released by ~Array at scope exit (no early
        // return inside this block).
    }

    // ---- Phase 2: Drag selection with pathfinding ----
    if (param3 == '\0') {
        uint32_t startCell, endCell;

        if (dragState == DragState::Idle) {
            int cell = PlayerRay_ScreenToCell_101270(nullptr);
            startCell = (uint32_t)cell;
            endCell   = (uint32_t)cell;
        } else {
            startCell = (uint32_t)this->startCell;
            endCell   = (uint32_t)this->endCell;
        }

        if ((int)startCell < 0) return;

        // Sort cells: ensure start <= end
        uint32_t sc = startCell, ec = endCell;
        if ((int)ec >= 0 && (int)ec < (int)sc) {
            sc = endCell;
            ec = startCell;
        }

        if ((int)ec < 0) ec = sc;

        // Get terrain data
        const MapCell* terrainBase = mapGrid->cells.GetData();

        // Check terrain flags at both cells
        uint32_t flagA = Bits(PathBitOf(terrainBase[sc].terrainType));
        uint32_t flagB = Bits(PathBitOf(terrainBase[ec].terrainType));
        uint32_t combinedFlags = flagA | flagB;

        // Both cells must be valid terrain (mask 0x5e0 clear) and unoccupied
        bool valid = ((combinedFlags & Bits(PathBits::NoRoad)) == 0) &&
                     (terrainBase[sc].blocker == nullptr) &&
                     (terrainBase[ec].blocker == nullptr);

        int pathLen = 0;

        if (valid) {
            // Try strict pathfinding first (mask: combinedFlags | 0xa17)
            pathLen = mapGrid->FindRoadPath(static_cast<PathBits>(combinedFlags) | PathBits::RoadRoute, sc, ec,
                                            nullptr, pathBuf, modifierFlag, 1);
        }

        if (pathLen == 0) {
            // Fallback: relaxed pathfinding (mask: combinedFlags | 0xff7)
            pathLen = mapGrid->FindRoadPath(static_cast<PathBits>(combinedFlags) | PathBits::RoadRouteRelaxed, sc, ec,
                                            nullptr, pathBuf, modifierFlag, 0);
        }

        if (pathLen > 0) {
            // Build tile array with per-tile color coding
            TileArray pathTiles;

            for (int i = 0; i < pathLen; i++) {
                int cellIdx = pathBuf[i];
                const MapCell* cellData = &terrainBase[cellIdx];
                uint8_t terrainByte = static_cast<uint8_t>(cellData->terrainType);
                uint32_t terrainFlag = 1u << (terrainByte & 0x1f);

                // Color: green if valid road terrain (0xa17), red if invalid or occupied
                uint32_t color;
                if ((terrainFlag & Bits(PathBits::RoadRoute)) == 0 || cellData->blocker != nullptr) {
                    color = 0xffff0000; // red — invalid
                } else {
                    color = 0xff00ff00; // green — valid
                }

                ToolTileRecord entry;
                entry.col     = cellIdx % mapGrid->size;
                entry.row     = cellIdx / mapGrid->size;
                entry.variant = 1; // road tile
                entry.color   = color;
                entry.angle   = 0;
                PushTileEntry(&pathTiles, &entry);
            }

            // Render: path tiles in R8, empty array in R9
            Collection::Array<ToolSegmentRecord> emptyTiles;
            PlayerTileRender_DrawOverlay_08c0(nullptr, context, &pathTiles, &emptyTiles);

            // pathTiles / emptyTiles are released by ~Array at scope exit (the
            // enclosing block's only early return, `startCell < 0`, precedes them).
        }
    }
}

// ============================================================
// RoadTool members (RoadTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_RoadTool_Vtable[15], its slot stubs, the _SA factories and
// RegisterRoadToolVtable are gone: the compiler builds the table, and nothing
// reads 0x140161e70 back by address (the old dtor was the only reader).
// Inherited from the bases, as in retail: [9] DragTool::OnCancel (0x1400fc640),
// [10] false, [11] no setting (0x140004950 `*ret = 0`; SetEditMode
// dereferences the slot), [12] cursor 5 (0x1400fc9d0).
// ============================================================
#include <new>

// [4] Startup, [6] OnKey, [8] Draw and [14] CalcCost are defined above, [13]
// CalcPath in roadtool_calcpath.cpp.

// create 0x14002e870: MemAlloc(0x48, 8), then the ctor 0x1401057d0.
::Object::Base* RoadTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RoadTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RoadTool());
}
// createInPlace 0x14002e8b0: `if (storage) ctor(storage)`.
::Object::Base* RoadTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RoadTool());
}

DEFINE_EXE_TYPE(RoadTool, "DragTool")
