// src/game/Player/dragplacementtool.cpp
// Clean reimplementation of DragPlacementTool vtable methods from Application-x64.exe.
//
// DragPlacementTool (0x48 bytes) extends DragTool (0x38 bytes).
// Handles rectangular area placement (e.g., stockpile, marketplace).
//
// Vtable at 0x140161b90 (15 slots). The whole vtable is reimplemented in this file:
//   Slot0  Dtor    (0x14002d900) — scalar deleting destructor
//   Slot4  Startup (0x1400fc3e0) — find description components on entity
//   Slot13 CalcPath(0x1400fb5c0) — commits the dragged area on drag-release
//   Slot14 CalcCost(0x1400fbae0) — validate area and calculate cost
//   plus Draw and the remaining slots, all wired in the vtable at the foot of the file.
//
// This banner used to end with "Remaining slots (Draw, CalcPath) are too complex
// (terrain validation, entity creation, scene rendering) and are not yet
// reimplemented". That has been FALSE since Draw and CalcPath landed further down this
// same file; the comment simply outlived the port that replaced it. Corrected
// 2026-09-06 — this tool is the ONLY route to a player-placed crop field
// (CropField.rsc declares `createdrag` -> CreateDragDescription -> DragPlacementTool),
// so a stale "not reimplemented" here reads as "crop fields cannot be placed", which
// is not true.
//
// Decompiled from Application-x64.exe.

#include "Player/DragPlacementTool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "Component/CreateDragDescription.h"
#include "Component/MapDescription.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Player/exe_bodies.h"
#include "Game/exe_constants.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include <cstring>
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"

// ============================================================
// Helper: search the tool group's sub-descriptions for T
// ============================================================
// Returns the first description whose Type() matches T's id and that IsDerivedFrom
// it, or nullptr; a type match that fails IsDerivedFrom ends the search.
template <class T>
static T* FindToolDescription(const ComponentDescription* entity) {
    if (!entity) return nullptr;
    const uint32_t targetTypeId = T::TypeId();

    int count = entity->_subDescCount;
    Description** components = entity->_subDescs;

    for (int i = 0; i < count; i++) {
        Description* comp = components[i];
        if (!comp) continue;

        // virtual slot 2 (+0x10): Type()
        if (comp->Type() == targetTypeId) {
            if (Object::IsDerivedFromNullSafe(comp, targetTypeId)) {
                return static_cast<T*>(comp);
            }
            break;
        }
    }
    return nullptr;
}

// ============================================================
// DragPlacementTool::Startup (vtable +0x20, Slot4)
// ============================================================
// Searches the tool group's sub-descriptions for two types:
//   1. CreateDragDescription (cell 0x1401c7430) -> this->placementDesc (+0x38)
//   2. MapDescription (cell 0x1401c7a18) -> this->areaDesc (+0x40)
//
// Original: 0x1400fc3e0
void DragPlacementTool::Startup() {
    // Find CreateDragDescription
    CreateDragDescription* foundPlacement = FindToolDescription<CreateDragDescription>(descriptionPtr);

    // Original re-reads the description pointer after the first search
    placementDesc = foundPlacement;

    // Find MapDescription
    areaDesc = FindToolDescription<MapDescription>(descriptionPtr);
}

// ============================================================
// Helpers, called by name (Player/exe_bodies.h)
// ============================================================

// PlayerPath_CharComp_f5d0 (0x14009f5d0): terrain validation for a single cell.
// Checks terrain mask, height constraints, overlap flags. Params: (map, col, row,
// terrainMask, 0.0f, heightTol, 0.0f, allowOverlap, hasExisting, entityTemplate).
// (Confirmed by the CALL at 0x1400fbcac in DragPlacementTool::CalcCost.)
static inline bool TerrainValidate(Map* mapPtr, uint32_t col, uint32_t row, uint32_t terrainMask,
                                   float p5, float heightTol, float p7, char allowOverlap,
                                   bool hasExisting, const ComponentDescription* entityTemplate) {
    return mapPtr->TestPlacement((int)col, (int)row, terrainMask, p5, heightTol, p7,
                                    allowOverlap, (char)hasExisting, entityTemplate);
}

// DAT_140172208 (.rdata): the height tolerance, 0x42C80000 = 100.0f.
static inline float HeightTolerance() {
    float f;
    std::memcpy(&f, &kRdata_140172208, sizeof f);
    return f;
}

// ============================================================
// DragPlacementTool::CalcCost (vtable +0x70, Slot14)
// ============================================================
// Calculates placement cost for a rectangular area.
//
// Converts start/end cells to grid coordinates, normalizes to min/max corners,
// checks description size constraints, then validates each cell's terrain.
// Finally checks the owner's single-rectangle constraint.
//
// Return: 0=success, 1=too small, 2=too big, 3=invalid terrain/overlap
// Original: 0x1400fbae0
int DragPlacementTool::CalcCost(int startCell, int endCell, uint32_t* outValid,
                                int* outCostA, int* outCostB, int* outLength) {
    // The map and its grid size
    Map* mapPtr = GameUpdateLoop::_singleton->m_game;
    int gridSize = mapPtr->size;

    // Convert cell indices to (col, row) grid coordinates
    int startRow = startCell / gridSize;
    int startCol = startCell % gridSize;
    int endRow = endCell / gridSize;
    int endCol = endCell % gridSize;

    // Normalize to min/max corners
    int minCol = startCol;
    int maxCol = endCol;
    if (endCol < startCol) {
        minCol = endCol;
        maxCol = startCol;
    }

    int minRow = startRow;
    int maxRow = endRow;
    if (endRow < startRow) {
        minRow = endRow;
        maxRow = startRow;
    }

    int width = (maxCol - minCol) + 1;
    int height = (maxRow - minRow) + 1;

    // Set output parameters
    *outCostA = width;
    *outCostB = height;
    *outValid = 0;
    *outLength = 1;

    float heightTol = HeightTolerance();

    // Read placement description fields
    const CreateDragDescription* desc = placementDesc;

    // Check min size (desc +0x30 = minWidth, +0x34 = minHeight)
    if (width < desc->_i30 || height < desc->_i34) {
        return 1;
    }

    // Check max size (desc +0x28 = maxWidth, +0x2C = maxHeight)
    if (desc->_i28 < width || desc->_i2c < height) {
        return 2;
    }

    // Validate terrain for each cell in the rectangle
    uint32_t terrainMask = desc->_shape;
    char allowOverlap = desc->_b24;
    const ComponentDescription* entityTemplate = desc->_created;

    bool allValid = true;
    for (int row = minRow; row <= maxRow && allValid; row++) {
        for (int col = minCol; col <= maxCol && allValid; col++) {
            // hasExisting = false (no occupancy grid in CalcCost, only in Draw)
            {
                bool cellValid = TerrainValidate(
                    mapPtr, (uint32_t)col, (uint32_t)row, terrainMask,
                    0.0f, heightTol, 0.0f,
                    allowOverlap, false, entityTemplate);
                if (!cellValid) {
                    allValid = false;
                }
            }
        }
    }

    if (allValid) {
        // The owner's single-rectangle constraint (PlayerToolManager +0x80 / +0x98 / +0x9C)
        const PlayerToolManager* dlg = dialogPtr;
        if (dlg && dlg->field_0x80) {
            int existGridSize = GameUpdateLoop::_singleton->m_game->size;
            int existStartCol = dlg->field_0x98 % existGridSize;
            int existStartRow = dlg->field_0x98 / existGridSize;
            int existEndCol = dlg->field_0x9C % existGridSize;
            int existEndRow = dlg->field_0x9C / existGridSize;
            if (minCol != existStartCol || minRow != existStartRow ||
                existEndCol != maxCol || existEndRow != maxRow) {
                return 3;
            }
        }
        return 0;
    }

    return 3;
}

// Draw also calls 0x140101270 PlayerRay_ScreenToCell_101270 (ray-picks the map cell
// under the cursor: row*gridSize + col, or -1 if no hit) and 0x1401008c0
// PlayerTileRender_DrawOverlay_08c0 (the tile renderer).

// ============================================================
// Tile records: ToolTileRecord (Player/Tool.h), variant 0 = normal, 2 = has existing.
// ENDGAME B.3.2 step 2: a real `Collection::Array` (stride 0x14, align 4, byteCopy);
// every append is one element, so the `(count == 0) ? n : n*3` rule is the
// reference rule. Out-of-memory-only difference: the old push skipped the store
// when the block came back null; SetSizeCore skips the null element address.
// ============================================================
using TileArray = Collection::Array<ToolTileRecord>;

// ============================================================
// DragPlacementTool::Draw (vtable +0x40, Slot8)
// ============================================================
// Renders colored tile overlays for a drag-based area placement tool.
//
// Two main sections:
// 1. If the owner holds a constraint rectangle (+0x80), render its footprint as
//    yellow tiles (0xffffff00).
// 2. If param3==0, render the current drag rectangle:
//    - Get start/end cells from DragTool drag state or ScreenToCell
//    - Normalize to min/max corners
//    - Check area size against description constraints
//    - Validate each cell's terrain, color green/red accordingly
//    - Pass tile arrays to 0x1401008c0 for rendering
//
// Original: 0x1400fbe40
void DragPlacementTool::Draw(Video::Context* context, char param3) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // --- Section 1: the constraint rectangle overlay (yellow tiles) ---
    const PlayerToolManager* dialogCtrl = dialogPtr;
    if (dialogCtrl->field_0x80) {
        int gridSize = gameWorld->m_game->size;

        int existStart = dialogCtrl->field_0x98;
        int existEnd = dialogCtrl->field_0x9C;

        int existStartCol = existStart % gridSize;
        int existStartRow = existStart / gridSize;
        int existEndCol = existEnd % gridSize;
        int existEndRow = existEnd / gridSize;

        // Build tile array for the constraint footprint
        TileArray existArr;
        for (int row = existStartRow; row <= existEndRow; row++) {
            for (int col = existStartCol; col <= existEndCol; col++) {
                ToolTileRecord rec;
                rec.col     = col;
                rec.row     = row;
                rec.variant = 0;                // no special flags
                rec.color   = 0xffffff00;       // yellow
                rec.angle   = 0;
                existArr.SetSize(existArr._size + 1, rec);
            }
        }

        // Render: the 0x14-byte tile records, and an always-empty 0x20-byte
        // segment array.
        Collection::Array<ToolSegmentRecord> empty;
        PlayerTileRender_DrawOverlay_08c0(nullptr, context, &existArr, &empty);
        // existArr / empty are released by ~Array at scope exit (no early return in scope).
    }

    // --- Section 2: Current drag area overlay ---
    if (param3 == 0) {
        int startCell, endCell;

        // Get start/end cells based on drag state
        if (dragState == DragState::Idle) {
            // Not dragging: use ScreenToCell for both
            int cell = PlayerRay_ScreenToCell_101270(nullptr);
            startCell = cell;
            endCell = cell;
        } else {
            // Dragging: start from stored field, end from stored field
            startCell = this->startCell;
            endCell = this->endCell;
        }

        float heightTol = HeightTolerance();
        int gridSize = gameWorld->m_game->size;

        // Convert cell indices to (col, row)
        int startCol = startCell % gridSize;
        int startRow = startCell / gridSize;
        int64_t gridSz64 = (int64_t)gameWorld->m_game->size;
        int endCol = (int)((int64_t)endCell % gridSz64);
        int endRow = (int)((int64_t)endCell / gridSz64);

        // Normalize to min/max corners
        uint32_t minCol = (uint32_t)endCol;
        uint32_t maxCol = (uint32_t)startCol;
        if ((int)endCol > (int)startCol) {
            minCol = (uint32_t)startCol;
            maxCol = (uint32_t)endCol;
        }

        uint32_t minRow = (uint32_t)startRow;
        uint32_t maxRow = (uint32_t)endRow;
        if ((int)endRow < (int)startRow) {
            minRow = (uint32_t)endRow;
            maxRow = (uint32_t)startRow;
        }

        int width = (int)(maxCol - minCol) + 1;
        int height = (int)(maxRow - minRow) + 1;

        // Check area constraints from placement description
        const CreateDragDescription* desc = placementDesc;
        bool tooBig = false;
        if (desc) {
            if (desc->_i28 < width || desc->_i2c < height) {
                tooBig = true;
            }
        }

        // Read description fields for terrain validation
        uint32_t terrainMask = desc ? desc->_shape : 0;
        char allowOverlap = desc ? desc->_b24 : 0;
        const ComponentDescription* entityTemplate = desc ? desc->_created : nullptr;
        Map* mapPtr = gameWorld->m_game;

        // Build tile array for drag area
        TileArray dragArr;

        // The original also reads an occupancy grid string (local_98); in
        // DragPlacementTool::Draw it is always 0, so the occupancy check is false.

        uint32_t rowIdx = minRow;
        if ((int)minRow <= (int)maxRow) {
            do {
                uint32_t colIdx = minCol;
                if ((int)minCol <= (int)maxCol) {
                    do {
                        bool hasExisting = false;

                        // Validate terrain
                        bool cellValid = TerrainValidate(
                                mapPtr, colIdx, rowIdx, terrainMask,
                                0.0f, heightTol, 0.0f,
                                allowOverlap, hasExisting, entityTemplate);

                        // Determine color
                        uint32_t color;
                        if (tooBig || !cellValid) {
                            color = 0xffff0000;  // red
                        } else {
                            color = 0xff00ff00;  // green
                        }

                        ToolTileRecord rec;
                        rec.col     = (int32_t)colIdx;
                        rec.row     = (int32_t)rowIdx;
                        rec.variant = hasExisting ? 2 : 0;
                        rec.color   = color;
                        rec.angle   = 0;
                        dragArr.SetSize(dragArr._size + 1, rec);

                        colIdx++;
                    } while ((int)colIdx <= (int)maxCol);
                }
                rowIdx++;
            } while ((int)rowIdx <= (int)maxRow);
        }

        // Render drag area tiles
        Collection::Array<ToolSegmentRecord> empty;
        PlayerTileRender_DrawOverlay_08c0(nullptr, context, &dragArr, &empty);
        // dragArr / empty are released by ~Array at scope exit (no early return in scope).
    }
}

// ============================================================
// CalcPath helpers, called by name: 0x1401018d0
// DragPlacementTool_ClearPathEntities_1018d0 (clears existing path entities and
// sets up the grid range), 0x1400c3720 ComponentEntity_CtorFromDescription (clone
// an entity from its description), 0x1400c48a0 EntityList_RegisterEntity_Impl.
// ============================================================

// ============================================================
// DragPlacementTool::CalcPath (vtable +0x68, Slot13)
// ============================================================
// Creates a placement entity for the drag area.
//
// Steps:
//   1. Call CalcCost (vtable+0x70) to validate the area — if non-zero, fail
//   2. Convert cell indices to grid coords, normalize min/max
//   3. If the owner has a constraint (+0x80), verify selection matches exactly
//   4. Clear existing path entities via 0x1401018d0
//   5. Create a ComponentEntity from the tool group's description
//   6. Find its MapComponent and set placement coords + rotation
//   7. Register entity with the entity list
//
// Rotation: 0x5a (90 degrees) if height >= width, else 0.
//
// Original: 0x1400fb900
bool DragPlacementTool::CalcPath(int startCell, int endCell) {
    // Virtual CalcCost (slot 14, +0x70) to validate, into local out-values

    uint32_t outValid = 0;
    int outCostA = 0;   // width
    int outCostB = 0;   // height
    int outLength = 0;

    int calcResult = CalcCost(startCell, endCell, &outValid, &outCostA, &outCostB, &outLength);


    if (calcResult != 0) {
        return false;
    }

    // Get map grid size
    int gridSize = GameUpdateLoop::_singleton->m_game->size;

    // Convert cell indices to grid coords
    int startRow = startCell / gridSize;
    int startCol = startCell % gridSize;
    int endRow = endCell / gridSize;
    int endCol = endCell % gridSize;

    // Normalize to min/max
    int minCol = startCol, maxCol = endCol;
    if (endCol < startCol) {
        minCol = endCol;
        maxCol = startCol;
    }
    int minRow = startRow, maxRow = endRow;
    if (endRow < startRow) {
        minRow = endRow;
        maxRow = startRow;
    }

    // Check the owner's constraint (+0x80 / +0x98 / +0x9C)
    const PlayerToolManager* dlg = dialogPtr;
    if (dlg->field_0x80) {
        int cGridSize = GameUpdateLoop::_singleton->m_game->size;
        int cStartCol = dlg->field_0x98 % cGridSize;
        int cStartRow = dlg->field_0x98 / cGridSize;
        int cEndCol = dlg->field_0x9C % cGridSize;
        int cEndRow = dlg->field_0x9C / cGridSize;

        if (minCol != cStartCol || minRow != cStartRow ||
            cEndCol != maxCol || cEndRow != maxRow) {
            // Faithful to retail (0x1400fb900): a restricted step demands the
            // EXACT rectangle, not one inside it.
            return false;
        }
    }

    // Clear existing path entities
    const ComponentDescription* entityTemplate = this->placementDesc->_created;

    DragPlacementTool_ClearPathEntities_1018d0(this, minCol, minRow, maxCol, maxRow, entityTemplate);

    // Allocate and construct a ComponentEntity from the tool group's description
    ComponentEntity* newEntityMem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* newEntity = nullptr;
    if (newEntityMem) {
        newEntity = ::new (newEntityMem) ComponentEntity(descriptionPtr);
    }

    // The entity's InternalBasePtr is its eight bytes
    Object::InternalPtr<ComponentEntity> entityIBP = newEntity;

    // Find its MapComponent (0x140004680, cell 0x1401c79e0)
    MapComponent* buildComp = entityIBP->FindComponent<MapComponent>();

    // Determine rotation: 0x5a (90 degrees) if height >= width, else 0
    int rotation = 0;
    if (outCostB >= outCostA) {
        rotation = 0x5a;  // 90 degrees
    }

    // Set placement coordinates on the MapComponent
    buildComp->tileX0 = (int16_t)minCol;
    buildComp->tileY0 = (int16_t)minRow;
    buildComp->tileX1 = (int16_t)outCostA;  // width
    buildComp->tileY1 = (int16_t)outCostB;  // height
    buildComp->extraField = rotation;

    // Register entity with entity list
    EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;
    int categoryId = static_cast<int>(descriptionPtr->_category);

    entityList->RegisterEntity(&entityIBP, categoryId);

    return true;
}

// ============================================================
// DragPlacementTool members (DragPlacementTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_DragPlacementTool_Vtable[15], its slot stubs, the _SA
// factories and RegisterDragPlacementToolVtable are gone: the compiler builds
// the table, and nothing reads 0x140161b90 back by address (the old dtor was
// the only reader). Inherited from the bases, as in retail: [6]
// DragTool::OnKey (0x1400fc6b0), [9] DragTool::OnCancel (0x1400fc640), [10]
// false, [11] no setting (0x140004950 `*ret = 0`; SetEditMode dereferences
// the slot), [12] cursor 5 (0x1400fc9d0).
// ============================================================
#include "Object/factory_registration.h"
#include <new>

// [4] Startup, [8] Draw, [13] CalcPath and [14] CalcCost are defined above.

// create 0x14002d8b0: MemAlloc(0x48, 8), then the ctor 0x1400fc530.
::Object::Base* DragPlacementTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DragPlacementTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DragPlacementTool());
}
// createInPlace 0x14002d8f0: `if (storage) ctor(storage)`.
::Object::Base* DragPlacementTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DragPlacementTool());
}

DEFINE_EXE_TYPE(DragPlacementTool, "DragTool")
