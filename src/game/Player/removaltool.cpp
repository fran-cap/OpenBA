// src/game/Player/removaltool.cpp
// Clean reimplementation of RemovalTool vtable methods from Application-x64.exe.
//
// RemovalTool (0x40 bytes) extends DragTool (0x38 bytes).
// Handles building/item removal: selects area, marks entities for removal.
//
// Vtable at 0x140161de0 (15 slots). Three are reimplemented here:
//   Slot0  Dtor     (0x14002e7f0) — scalar deleting destructor
//   Slot4  Startup  (0x140104bd0) — find RemovalDescription component on entity
//   Slot14 CalcCost (0x140103ef0) — calculate removal area dimensions and validate
//
// Decompiled from Application-x64.exe.

#include "Player/RemovalTool.h"
#include "Player/edittool_funcs.h"   // RemovalTool_Factory_SA
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "Component/CreateRemovalDescription.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Player/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "object/TypeInfo.h"
#include "Component/ComponentDescription.h"
#include "Component/WorkComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Action/PathAvl.h"   // AvlNode28 (Map +0xE8)

// ============================================================
// RemovalTool::Startup (vtable +0x20, Slot4)
// ============================================================
// Searches the tool group description's sub-descriptions for the
// CreateRemovalDescription (cell 0x1401c74a0; the global name "RemovalDescription"
// lies — D5, Table 1/3). Stores the found description (or nullptr) in
// this->removalDesc (+0x38). A type match that fails IsDerivedFrom ends the search.
//
// Original: 0x140104bd0
void RemovalTool::Startup() {
    ComponentDescription* entity = descriptionPtr;
    CreateRemovalDescription* result = nullptr;

    if (!entity) {
        removalDesc = nullptr;
        return;
    }

    int count = entity->_subDescCount;
    Description** components = entity->_subDescs;
    uint32_t targetTypeId = CreateRemovalDescription::TypeId();

    for (int i = 0; i < count; i++) {
        Description* comp = components[i];
        if (!comp) continue;

        // Virtual slot 2 (+0x10): Type()
        if (comp->Type() == targetTypeId) {
            // Verify with IsDerivedFrom before casting
            if (Object::IsDerivedFromNullSafe(comp, targetTypeId)) {
                result = static_cast<CreateRemovalDescription*>(comp);
            }
            break;
        }
    }

    removalDesc = result;
}

// ============================================================
// RemovalTool::CalcCost (vtable +0x70, Slot14)
// ============================================================
// Calculates removal area dimensions between startCell and endCell.
//
// Steps:
//   1. Convert cell indices to grid (row, col) using map grid size
//   2. Compute min/max row/col for the bounding rectangle
//   3. Set output params: width = (maxCol-minCol+1), height = (maxRow-minRow+1)
//   4. If the owner (the PlayerToolManager, +0x10) has a constraint (+0x80 flag),
//      verify that the selection matches the constrained area exactly
//      (+0x98 constrained start cell, +0x9c constrained end cell)
//
// Return value semantics:
//   0 = success (removal can proceed)
//   3 = failure (selection doesn't match constrained area)
//
// Original: 0x140103ef0
int RemovalTool::CalcCost(int startCell, int endCell, uint32_t* outValid,
                          int* outCostB, int* outCostA, int* outLength) {
    // Get map grid size from GameWorld
    int gridSize = GameUpdateLoop::_singleton->m_game->size;

    // Convert cell indices to (row, col) grid coordinates
    int startRow = startCell / gridSize;
    int startCol = startCell % gridSize;
    int endRow   = endCell / gridSize;
    int endCol   = endCell % gridSize;

    // Compute min/max to get bounding rectangle
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

    // Set output params: dimensions of removal area
    *outLength = 1;
    *outCostB = (maxCol - minCol) + 1;   // width in columns
    *outCostA = (maxRow - minRow) + 1;   // height in rows
    *outValid = 0;

    // Check the owner's constraint
    const PlayerToolManager* descEntity = dialogPtr;
    if (descEntity->field_0x80) {
        // Constrained removal — verify selection matches the constrained area exactly
        int cGridSize = GameUpdateLoop::_singleton->m_game->size;

        int constrainStartCell = descEntity->field_0x98;
        int constrainEndCell   = descEntity->field_0x9C;

        int cStartCol = constrainStartCell % cGridSize;
        int cStartRow = constrainStartCell / cGridSize;
        int cEndCol   = constrainEndCell % cGridSize;
        int cEndRow   = constrainEndCell / cGridSize;

        if (minCol != cStartCol || minRow != cStartRow ||
            cEndCol != maxCol   || cEndRow != maxRow) {
            return 3;
        }
    }

    return 0;
}

// The tile records Draw hands to 0x1401008c0 (ToolTileRecord, Player/Tool.h).
// ENDGAME B.3.2 step 2: a `Collection::Array` (stride 0x14, align 4, byteCopy,
// init == nullptr, destroy == nullptr) whose SetSize is the reference grow rule.
using TileArray = Collection::Array<ToolTileRecord>;

// ============================================================
// Helper: push a tile record into a dynamic array
// ============================================================
static void PushTileEntry(TileArray* arr, const ToolTileRecord* entry) {
    arr->SetSize(arr->_size + 1, *entry);
}

// ============================================================
// Draw's exe helpers, called by name:
//   0x1401048a0 / 0x140104770 / 0x140104670 RemovalTool::CanRemoveField / Road /
//               Building (tool_base_path.cpp)
//   0x140101270 PlayerRay_ScreenToCell_101270, 0x1401008c0
//   PlayerTileRender_DrawOverlay_08c0 (Player/exe_bodies.h)
//   0x140003050 WorkBase_FindComponent_3050 (Work/exe_bodies.h) -- the parent/owner
//               entity lookup
//   0x14012ead0 WorkComponent_CheckWorkSlot (Component/exe_bodies.h) -- the
//               "marked for destruction" check
// The CanRemove* bodies return uint64_t; the caller reads AL as the bool.
// ============================================================

// ============================================================
// RemovalTool::Draw (vtable +0x40, Slot8)
// ============================================================
// Renders colored tile overlay for removal preview.
// Two phases:
//   1. Constrained tiles from the owner's constraint (+0x80 flag)
//   2. Drag selection rectangle with per-tile removability check
//
// Original: 0x140104050
void RemovalTool::Draw(Video::Context* ctx, char param3) {
    const PlayerToolManager* descEntity = dialogPtr;
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;

    // ---- Phase 1: Constrained entity tiles ----
    if (descEntity->field_0x80) {
        int gridSize = mapGrid->size;
        int constrainStart = descEntity->field_0x98;
        int constrainEnd   = descEntity->field_0x9C;

        int startCol = constrainStart % gridSize;
        int startRow = constrainStart / gridSize;
        int endCol   = constrainEnd % gridSize;
        int endRow   = constrainEnd / gridSize;

        // Build entity tile array
        TileArray entityTiles;

        for (int row = startRow; row <= endRow; row++) {
            for (int col = startCol; col <= endCol; col++) {
                ToolTileRecord entry;
                entry.col     = col;
                entry.row     = row;
                entry.variant = 0;
                entry.color   = 0xffffff00; // retail constrained-tile color (0x140104050)
                entry.angle   = 0;
                PushTileEntry(&entityTiles, &entry);
            }
        }

        // Render: entity tiles in R8, empty array in R9
        Collection::Array<ToolSegmentRecord> emptyTiles;
        PlayerTileRender_DrawOverlay_08c0(nullptr, ctx, &entityTiles, &emptyTiles);

        // entityTiles / emptyTiles are released by ~Array at scope exit (no early
        // return inside this block).
    }

    // ---- Phase 2: Drag selection rectangle ----
    if (param3 == '\0') {
        int startCell, endCell;

        if (dragState == DragState::Idle) {
            // Not dragging — pick cell under cursor
            int cell = PlayerRay_ScreenToCell_101270(nullptr);
            startCell = cell;
            endCell   = cell;
        } else {
            startCell = this->startCell;
            endCell   = this->endCell;
        }

        if (startCell < 0) return;
        if (endCell < 0) endCell = startCell;

        int gridSize = mapGrid->size;

        // Convert to (row, col) grid coordinates
        uint32_t startRow2 = (uint32_t)(startCell / gridSize);
        uint32_t startCol2 = (uint32_t)(startCell % gridSize);
        uint32_t endRow2   = (uint32_t)(endCell / gridSize);
        uint32_t endCol2   = (uint32_t)(endCell % gridSize);

        // Compute min/max for bounding rectangle
        uint32_t minCol = startCol2, maxCol = endCol2;
        if ((int)endCol2 < (int)startCol2) {
            minCol = endCol2;
            maxCol = startCol2;
        }
        uint32_t minRow = startRow2, maxRow = endRow2;
        if ((int)endRow2 < (int)startRow2) {
            minRow = endRow2;
            maxRow = startRow2;
        }

        // Build selection tile array with per-tile removability check
        TileArray selTiles;
        mapGrid = GameUpdateLoop::_singleton->m_game;

        const CreateRemovalDescription* removalDesc = this->removalDesc;

        for (uint32_t r = minRow; r <= maxRow; r++) {
            for (uint32_t c = minCol; c <= maxCol; c++) {
                // Get entity at this tile from map grid
                int tileIndex = mapGrid->size * (int)r + (int)c;
                const MapCell* cellBase = &mapGrid->cells.GetData()[tileIndex];

                // Get entity occupying this tile: the blocker, else the occupant
                ComponentEntity* entityPtr = cellBase->blocker.Get();
                ComponentEntity* entityPtr2 = cellBase->occupant.Get();
                ComponentEntity* entity;
                if (entityPtr == nullptr) {
                    if (entityPtr2 == nullptr) {
                        // No entity — the null IBP
                        entity = nullptr;
                    } else {
                        entity = entityPtr2;
                    }
                } else {
                    entity = entityPtr;
                }

                // Check removability
                bool canRemove = false;

                canRemove = static_cast<uint8_t>(CanRemoveField(&entity, (int)c, (int)r)) != 0;

                if (!canRemove) {
                    // Additional checks from description flags
                    mapGrid = GameUpdateLoop::_singleton->m_game;

                    // Check road removal flag (+0x40 on removalDesc)
                    if (removalDesc && removalDesc->_b40) {
                        int tileIdx2 = mapGrid->size * (int)r + (int)c;
                        const MapCell* cellBase2 = &mapGrid->cells.GetData()[tileIdx2];
                        uint32_t terrainFlag = Bits(PathBitOf(cellBase2->terrainType));

                        // Check if cell has road-type terrain (bits 0 or 1)
                        if ((terrainFlag & Bits(PathBits::Fast)) != 0 || (terrainFlag & Bits(PathBits::Faster)) != 0 ||
                            cellBase2->field_0x2A != MapCell::kNoRoadBatch) {
                            canRemove = true;
                        }

                        if (!canRemove) {
                            // The map's road-cell BST (+0xE8): key at +0, children +0x10 / +0x18
                            const AvlNode28* bstNode = mapGrid->field_0xE8.root;
                            while (bstNode != nullptr) {
                                if (tileIdx2 < bstNode->key) {
                                    bstNode = bstNode->left;
                                } else if (tileIdx2 > bstNode->key) {
                                    bstNode = bstNode->right;
                                } else {
                                    canRemove = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!canRemove) {
                        // Check road removal 0x140104770
                        ComponentEntity* entityCopy1 = entity;
                        canRemove = static_cast<uint8_t>(CanRemoveRoad(&entityCopy1)) != 0;
                    }

                    if (!canRemove) {
                        // Check building removal 0x140104670
                        ComponentEntity* entityCopy2 = entity;
                        canRemove = static_cast<uint8_t>(CanRemoveBuilding(&entityCopy2)) != 0;
                    }

                    if (!canRemove) {
                        // Check removeBuildings flag (+0x43) and parent entity destruction
                        if (removalDesc && removalDesc->_b43 && entity != nullptr) {
                            ComponentEntity* entityForCheck = entity;

                            // The entity's WorkComponent (0x140003050 = FindComponent<WorkComponent>)
                            WorkComponent* work = entityForCheck->FindComponent<WorkComponent>();
                            if (work != nullptr) {
                                canRemove = work->IsWorkPosted();   // 0x14012ead0
                            }
                        }
                    }
                }


                ToolTileRecord entry;
                entry.col     = (int32_t)c;
                entry.row     = (int32_t)r;
                entry.variant = canRemove ? 3 : 0;
                // Retail (0x140104050): removable tile -> RED (0xffff0000), non-removable
                // -> GREEN (0x8000ff00). NOT the intuitive green=valid — matches the game
                // (red marks "will be cleared"). Our reimpl had these swapped.
                entry.color = canRemove ? 0xffff0000 : 0x8000ff00;
                entry.angle = 0;
                PushTileEntry(&selTiles, &entry);
            }
        }

        // Render: selection tiles in R8, empty array in R9
        Collection::Array<ToolSegmentRecord> emptyTiles2;
        PlayerTileRender_DrawOverlay_08c0(nullptr, ctx, &selTiles, &emptyTiles2);

        // selTiles / emptyTiles2 are released by ~Array at scope exit (the block's
        // only early return, `startCell < 0`, precedes their declarations).
    }
}

// ============================================================
// RemovalTool::CalcPath (vtable +0x68, Slot13)
// ============================================================
// This function is extremely complex (~300 lines of decompiled output):
//   - Iterates a rectangular grid of cells
//   - For each cell, checks removability via RemovalTool_sub_48a0
//   - Creates ComponentEntity copies from template (0x48 bytes each)
//   - Finds MapComponent (DWORD_1401c79e0) and DestroyComponent (DWORD_1401c75f0)
//     on entities via typed FindComponent loops
//   - Handles road removal: checks terrain flags, BST traversal for road tracking,
//     calls RemovalTool_helper_c700 for road validation
//   - Handles building removal: checks sub_4770/sub_4670, finds parent entities,
//     checks destruction status via FUN_14012ead0
//   - Handles crop removal: checks DWORD_1401c8070 component, BST lookup in
//     GameWorld+0x118 crop tracker, calls CitizenAI_impl_9ad0
//   - Calls FUN_1400c48a0 (EntityList_RegisterEntity) to register removal markers
//   - Calls TradeWork_impl_f370 for road count updates
//
// Original: 0x1401035e0
// ENDGAME B.3.8 callers: PORTED as RemovalTool::CalcPath in
// keystone_a_removalcalcpath.cpp; the own-address read of 0x1401035e0 that used to
// reach it through the registry is now the member itself.

// ============================================================
// RemovalTool members (RemovalTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_RemovalTool_Vtable[15], its slot stubs, the in-place _SA
// factory and RegisterRemovalToolVtable are gone: the compiler builds the
// table, and nothing reads 0x140161de0 back by address (the old dtor was the
// only reader). Inherited from the bases, as in retail: [6] DragTool::OnKey,
// [9] DragTool::OnCancel (0x1400fc640 — the right-click cancel that closes the
// CreateBar cost dialog), [10] false, [11] no setting (0x140004950 `*ret = 0`;
// SetEditMode dereferences the slot), [12] cursor 5 (0x1400fc9d0).
// ============================================================
#include "Object/factory_registration.h"
#include <new>

// [4] Startup, [8] Draw and [14] CalcCost are defined above, [13] CalcPath in
// keystone_a_removalcalcpath.cpp.

// create 0x14002e7a0: MemAlloc(0x40, 8), then the ctor 0x140104cc0.
::Object::Base* RemovalTool::CreateNewObject() {
    return static_cast<Object::Base*>(RemovalTool_Factory_SA());
}
// createInPlace 0x14002e7e0: `if (storage) ctor(storage)`.
::Object::Base* RemovalTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RemovalTool());
}

// Kept for src/diag/tool_harness.cpp (outside this block's files): the
// reflected create, by its old name.
extern "C" RemovalTool* RemovalTool_Factory_SA(void) {
    void* p = Object::detail::InternalMemAlloc(sizeof(RemovalTool), 8);
    if (!p) return nullptr;
    return new (p) RemovalTool();
}

DEFINE_EXE_TYPE(RemovalTool, "DragTool")
