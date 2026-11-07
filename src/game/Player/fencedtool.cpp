// src/game/Player/fencedtool.cpp
// Clean reimplementation of FencedTool vtable methods from Application-x64.exe.
//
// FencedTool (0x70 bytes) extends DragTool (0x38 bytes).
// Handles fenced area placement (pasture, crop fields).
//
// Vtable at 0x1401605d0 (15 slots). Four are reimplemented here:
//   Slot0  Dtor      (0x140026930) — scalar deleting destructor
//   Slot5  Shutdown2  (0x1400dccc0) — cleanup scene object and mesh
//   Slot6  OnKey      (0x1400dcbe0) — wraps DragTool::OnKey + rotation handling
//   Slot9  Shutdown   (0x1400dcd10) — remove scene object + call base shutdown
//
// Remaining slots (Startup, Serialize, Draw, CalcPath, CalcCost) are too complex
// (many unresolved helpers: MeshInstance, Transform, entity creation, fence segment
// calculation) and are not reimplemented here.
//
// Decompiled from Application-x64.exe.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "Player/FencedTool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "object/External.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Player/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include "Player/RotationSetting.h"   // ENDGAME B.3.9 CB: 0x1401c63c8 cell class
#include "Component/CreateFencedDescription.h"
#include "Component/MapDescription.h"
#include "Component/ModelDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)


// ============================================================
// FencedTool::Shutdown2 (vtable +0x28, Slot5)
// ============================================================
// Removes the fence preview from the scene and destroys the mesh instance.
// Original: 0x1400dccc0
void FencedTool::Shutdown() {
    // Remove scene object if added
    if ((int)sceneObjectId >= 0) {
        Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
        if (scene) {
            scene->RemoveObject(sceneObjectId);
        }
    }

    // Destroy mesh instance via vtable dtor if allocated (slot 0, free)
    if (meshInstance) {
        delete meshInstance;
    }
}

// ============================================================
// Helper: the KeyMap of the input configuration
// ============================================================
static inline const Input::KeyMap* GetKeyMap() {
    return Input::Configuration::_singleton->_keyMap;
}

// ============================================================
// FencedTool::OnKey (vtable +0x30, Slot6)
// ============================================================
//   - KeyMap::Pressed(0, 0x13) = rotate counter-clockwise: decrement rotation,
//     wrap from -1 to 3
//   - KeyMap::Pressed(0, 0x14) = rotate clockwise: increment rotation,
//     wrap from 4 to 0
//
// Rotation value stored at rotationSetting(+0x50)->rotation (+0x10), read with no null
// test (0x1400dcc1f MOV RAX,[RBX] / 0x1400dcc25 DEC dword ptr [RAX+0x10]; the CW arm at
// 0x1400dcc77 / 0x1400dcc7d): Startup always resolves it, since SelectTool creates the
// RotationSetting GetSettingType names (review F-217).
// Always returns 1 (event consumed).
// Original: 0x1400dcbe0
// DragTool::OnKey is our own port (dragtool.cpp) — called by symbol (a direct call
// to the base, as retail's CALL 0x1400fc6b0).
#include "Player/dragtool.h"

char FencedTool::OnKey(char key) {
    // Call base DragTool::OnKey
    DragTool::OnKey(key);

    const Input::KeyMap* keyMap = GetKeyMap();

    // Check rotate counter-clockwise (key 0x13)
    bool rotLeft = keyMap->Pressed(0, ToUnderlying(GameKey::StructureLeft));
    if (rotLeft) {
        RotationSetting* rotSetting = rotationSetting;
        rotSetting->rotation = rotSetting->rotation - 1;

        if (rotationSetting->rotation < 0) {
            rotationSetting->rotation = 3;
        }
    } else {
        // Check rotate clockwise (key 0x14)
        bool rotRight = keyMap->Pressed(0, ToUnderlying(GameKey::StructureRight));
        if (rotRight) {
            RotationSetting* rotSetting = rotationSetting;
                rotSetting->rotation = rotSetting->rotation + 1;

            if (rotationSetting->rotation > 3) {
                rotationSetting->rotation = 0;
            }
        }
    }

    return 1;
}

// ============================================================
// FencedTool::OnCancel (vtable +0x48, Slot9)
// ============================================================
// Removes scene object if added, resets ID to -1, then TAIL-JUMPS to the shared
// DragTool cancel-drag (0x1400fc640) — so its return value IS that function's:
// non-zero = "I cancelled my drag, keep me alive", zero = "OK to tear me down".
// Original: 0x1400dcd10
char FencedTool::OnCancel() {
    // Remove scene object if added
    if ((int)sceneObjectId >= 0) {
        Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
        if (scene) {
            scene->RemoveObject(sceneObjectId);
        }
        sceneObjectId = (uint32_t)-1;
    }

    // Tail-call the shared DragTool cancel-drag (retail `JMP 0x1400fc640`) and return
    // its result.
    return DragTool::OnCancel();
}

// ============================================================
// FencedTool::Update (vtable +0x38, Slot7) — Fence preview mesh management
// ============================================================
// When active (param3 != 0 and dragging):
//   1. Calculate fence placement via 0x1400dbf90
//   2. If valid: add mesh to scene (if not already), set transform, update hierarchy
//   3. If invalid: remove mesh from scene
// When inactive: remove mesh from scene if present.
// Original: 0x1400dcad0
void FencedTool::Update(const System::Time* /*param2*/, char param3) {
    // Initialize local offset vector to {0, 0, 0}
    float offset[3] = {0.0f, 0.0f, 0.0f};

    if (param3 != 0 && dragState != DragState::Idle) {
        // Placement parameters — outputs from the anchor calc
        uint32_t outParam2 = 0;
        uint32_t outParam3 = 0;
        uint32_t outParam4 = 0;
        uint32_t outParam5 = 0;
        uint32_t outYaw = 0;   // the yaw's bits (degrees) 0x1400dbf90 stores for rotations 0..3

        // 0x1400dbf90 returns uint64_t; the door read AL as the bool.
        bool valid = static_cast<uint8_t>(CalcAnchor(
            &outParam2, &outParam3, &outParam4, &outParam5,
            offset, &outYaw)) != 0;

        if (valid) {
            // Add mesh to scene if not already added
            if ((int)sceneObjectId < 0) {
                Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
                if (scene && meshInstance) {
                    sceneObjectId = (int32_t)scene->AddObject(meshInstance);
                }
            }

            // Build transform from yaw with zero pitch, offset = the world position the
            // anchor calc just wrote (retail 0x1400dcb55 loads R9 from the same buffer
            // it passed as arg 6 at 0x1400dcb25).
            float zeroPitch = 0.f;
            float yawDeg = 0.f;
            std::memcpy(&yawDeg, &outYaw, sizeof yawDeg);   // retail reads the dword as the Scalar
            const Math::Transform localTransform = Math::Transform::FromYawPitchOffset(
                Math::Scalar(yawDeg), zeroPitch,
                Math::Vector3(offset[0], offset[1], offset[2]));

            {
                static int n = 0;
                if (n < 12) { ++n;
                    fprintf(stderr, "[FencedGhost] preview transform: offset=(%.2f, %.2f, %.2f)\n",
                            offset[0], offset[1], offset[2]);
                    fflush(stderr);
                }
            }

            // Set transform on mesh
            if (meshInstance) {
                meshInstance->SetTransform(localTransform);
            }

            // Update mesh hierarchy
            if (meshInstance) {
                meshInstance->UpdateHierarchy();
            }

            return;
        }
    }

    // Remove mesh from scene if present
    if ((int32_t)sceneObjectId >= 0) {
        Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
        if (scene) {
            scene->RemoveObject((uint32_t)sceneObjectId);
        }
        sceneObjectId = -1;
    }
}

// ============================================================
// CalcCost helpers, called by name:
//   0x14009f3e0 Reimpl_MapGetTerrainPos (Game/exe_bodies.h) -- terrain height query
//   0x1400dbf90 PlayerEdit_FencedAnchorCalc_dbf90 (Player/exe_bodies.h) --
//               anchor/rotation adjustment
// ============================================================

static constexpr float HALF_CELL = 0.5f;
static constexpr float HEIGHT_TOLERANCE = 100.0f;

// ============================================================
// FencedTool::CalcCost (vtable +0x70, Slot14)
// ============================================================
// Validates a rectangular fence region between startCell and endCell.
// Returns: 0=valid, 1=too small, 2=too large, 3=blocked
// Original: 0x1400db490
int FencedTool::CalcCost(int startCell, int endCell,
                         uint32_t* outWidth, int* outHeight, int* outDepth,
                         int* outFenceSegments) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    Map* mapGrid = gameWorld->m_game;
    int mapWidth = mapGrid->size;

    // Convert flat cell indices to (col, row) via divmod
    int startCol = startCell % mapWidth;
    int startRow = startCell / mapWidth;
    int endCol   = endCell % mapWidth;
    int endRow   = endCell / mapWidth;

    // Sort to get min/max coordinates
    uint32_t minCol = (uint32_t)startCol, maxCol = (uint32_t)endCol;
    if (endCol < startCol) { maxCol = (uint32_t)startCol; minCol = (uint32_t)endCol; }
    uint32_t effectiveMaxCol = maxCol;

    uint32_t minRow = (uint32_t)startRow, maxRow = (uint32_t)endRow;
    if (endRow < startRow) { maxRow = (uint32_t)startRow; minRow = (uint32_t)endRow; }
    uint32_t effectiveMaxRow = maxRow;

    int width  = (int)(maxCol - minCol) + 1;
    int height = (int)(maxRow - minRow) + 1;

    *outHeight = width;
    *outDepth  = height;
    *outWidth  = 0;
    *outFenceSegments = (height + width) * 2 - 4;

    // Read size constraints from description
    const CreateFencedDescription* desc = fencedDesc;
    int minW = desc->_i60;
    int minH = desc->_i64;
    if (width < minW || height < minH) return 1;  // Too small

    int maxW = desc->_i58;
    int maxH = desc->_i5c;
    if (maxW < width || maxH < height) return 2;  // Too large

    // Read terrain type mask and allowed entity type
    uint32_t terrainMask = desc->_u20;
    const ComponentDescription* allowedEntityType = desc->_ref70;

    // Cell data base pointer
    const MapCell* cellDataBase = mapGrid->cells.GetData();

    bool allValid = true;

    int endRow2 = (int)minRow + height;
    int endCol2 = (int)minCol + width;

    for (int row = (int)minRow; row < endRow2 && allValid; row++) {
        for (int col = (int)minCol; col < endCol2 && allValid; col++) {
            if (col < 0 || mapWidth <= col || row < 0 || mapWidth <= row) {
                allValid = false; break;
            }

            int cellIdx = mapWidth * row + col;
            const MapCell* cellPtr = &cellDataBase[cellIdx];

            // Check terrain type mask
            uint8_t terrainByte = static_cast<uint8_t>(cellPtr->terrainType);
            if ((~terrainMask & (1u << (terrainByte & 0x1f))) != 0) {
                allValid = false; break;
            }

            // Check entity occupancy
            const ComponentEntity* cellEntity = cellPtr->blocker.Get();
            if (allowedEntityType != nullptr) {
                if (cellEntity != nullptr) {
                    if (cellPtr->blocker->_description != allowedEntityType) { allValid = false; break; }
                }
            } else {
                if (cellEntity != nullptr) { allValid = false; break; }
            }

            // Check occupied flag
            if (cellPtr->footprintCount != 0) { allValid = false; break; }

            // Height validation
            {
                float halfOffset = (float)(mapWidth >> 1) - HALF_CELL;
                float inPos[4] = {(float)(cellIdx % mapWidth) - halfOffset,
                                  (float)(cellIdx / mapWidth) - halfOffset, 0.0f, 0.0f};
                float outPos[4] = {0};
                float extra[3] = {0};   // retail: a Vector3 at [RBP-0x70] (z = 0 stored at 0x1400db6c0)
                mapGrid->GetTerrainPos(outPos, inPos, '\0', extra);

                // The query's third out, z: blocked when the input z (XMM6, 0.0 for every
                // cell) exceeds it -- `COMISS XMM6,[RBP-0x68]; JA` (0x1400db71e).
                if (inPos[2] > extra[2]) { allValid = false; break; }
                // |terrain z - XMM6| against the tolerance (0x1400db724..0x1400db749); XMM6
                // is zeroed before the loop (0x1400db5b5) and never written in it.
                Math::Scalar heightDiff(outPos[2] - inPos[2]);
                if (HEIGHT_TOLERANCE < heightDiff.Abs().v) { allValid = false; break; }
            }
        }
    }

    if (!allValid) return 3;  // Blocked

    // Anchor/rotation adjustment
    uint32_t anchorMinCol = minCol, anchorMinRow = minRow;
    uint32_t anchorMaxCol = effectiveMaxCol, anchorMaxRow = effectiveMaxRow;
    float anchorPos[3] = {0};
    // Arg 7, the yaw out: its own stack slot, [RBP+0x58] (`LEA RAX,[RBP+0x58];
    // MOV [RSP+0x30],RAX` at 0x1400db7eb); CalcCost never reads it.
    uint32_t anchorYaw;   // written by 0x1400dbf90 before any read

    uint64_t anchorResult = CalcAnchor(
        &anchorMinCol, &anchorMinRow, &anchorMaxCol, &anchorMaxRow,
        anchorPos, &anchorYaw);
    if ((char)anchorResult == '\0') return 3;

    // Check the owner's constraint rectangle
    const PlayerToolManager* toolState = dialogPtr;
    if (toolState->field_0x80) {
        int mw = gameWorld->m_game->size;
        int storedStart = toolState->field_0x98;
        int storedEnd   = toolState->field_0x9C;
        if (minCol != (uint32_t)(storedStart % mw)) return 3;
        if (minRow != (uint32_t)(storedStart / mw)) return 3;
        if (effectiveMaxCol != (uint32_t)(storedEnd % mw)) return 3;
        if (effectiveMaxRow != (uint32_t)(storedEnd / mw)) return 3;
    }

    return 0;  // Valid
}

// ============================================================
// Draw helper: the 0x14-byte tile records 0x1401008c0 reads (ToolTileRecord,
// Player/Tool.h: col, row, variant, color, angle) in a real Collection::Array
// (stride 0x14, align 4, byteCopy). B4 PLAYER: the hand-written {data, count,
// capacity} array and its toEngineFormat pack are gone. Its grow went to n*3 even
// from empty (capacity 3 for the first record). Retail's inlined grow is
// SetSizeCore's rule: capacity n, trebled only when one record is appended to a
// non-empty array (`LEA EAX,[RCX+1]; CMP; JNZ; TEST ECX,ECX; JZ; LEA ESI,[RBX+RBX*2]`
// at 0x1400dc542-0x1400dc54d and 0x1400dc942-0x1400dc94e), so the first grow is
// capacity 1, as here (known_port_issues 14 row 75).
// ============================================================
using TileArray = Collection::Array<ToolTileRecord>;

// ============================================================
// Draw helpers: 0x140101270 PlayerRay_ScreenToCell_101270 (the cell under the
// cursor), 0x14009f3e0 Reimpl_MapGetTerrainPos (terrain height query),
// 0x1401008c0 PlayerTileRender_DrawOverlay_08c0 (the tile overlay submit).
// ============================================================

// ============================================================
// FencedTool::Draw (vtable +0x40, Slot8)
// ============================================================
// Two-phase rendering:
//   Phase 1 (edit mode): If self->dialog(+0x10)->editMode(+0x80), renders the
//     existing placed tiles from startCell/endCell stored in the dialog.
//   Phase 2 (preview): While dragging, renders the rectangular preview area with
//     per-tile terrain/height validation (green=valid, red=invalid).
//
// Both phases build arrays of 0x14-byte TileRecords and call fn_TileRender.
// Original: 0x1400dc440
void FencedTool::Draw(Video::Context* context, char param3) {
    const PlayerToolManager* dialog = dialogPtr;
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;
    int mapWidth = mapGrid->size;

    // ---- Phase 1: the owner's constraint rectangle ----
    if (dialog->field_0x80) {
        int storedStart = dialog->field_0x98;
        int storedEnd   = dialog->field_0x9C;

        int startCol = storedStart / mapWidth;
        int startRow = storedStart % mapWidth;
        int endCol   = storedEnd / mapWidth;
        int endRow   = storedEnd % mapWidth;

        TileArray editTiles;

        for (int r = startCol; r <= endCol; r++) {
            for (int c = startRow; c <= endRow; c++) {
                ToolTileRecord rec;
                rec.col     = c;
                rec.row     = r;
                rec.variant = 0;
                rec.color   = 0xffffff00;  // yellow-ish marker
                rec.angle   = 0;
                editTiles.SetSize(editTiles._size + 1, rec);
            }
        }

        // Render (the 0x20-byte segment array is always empty here)
        Collection::Array<ToolSegmentRecord> lineArray;
        PlayerTileRender_DrawOverlay_08c0(nullptr, context, &editTiles, &lineArray);
        // editTiles / lineArray are released by ~Array at scope exit.
    }

    // ---- Phase 2: Preview (only when not param3 and actively dragging) ----
    if (param3 != '\0') return;

    int startCell, endCell;
    if (dragState != DragState::Idle) {
        startCell = this->startCell;
        endCell   = this->endCell;
    } else {
        // Get cell under cursor
        startCell = PlayerRay_ScreenToCell_101270(Input::Configuration::_singleton->_keyMap);
        endCell = startCell;
    }

    const float HEIGHT_TOL = 100.0f;  // DAT_140172208
    const float HALF_CELL_OFF = 0.5f; // DAT_140172114

    // Convert to col/row
    int col1 = startCell % mapWidth;
    int row1 = startCell / mapWidth;
    int col2 = endCell % mapWidth;
    int row2 = endCell / mapWidth;

    // Sort min/max
    int minCol = col1, maxCol = col2;
    if (col2 < col1) { minCol = col2; maxCol = col1; }
    int minRow = row1, maxRow = row2;
    if (row2 < row1) { minRow = row2; maxRow = row1; }

    int width  = maxCol - minCol + 1;
    int height = maxRow - minRow + 1;

    // Check size constraints from description
    const CreateFencedDescription* desc = fencedDesc;
    bool tooLarge = false;
    if (width > desc->_i58 || height > desc->_i5c) {
        tooLarge = true;
    }

    // Read terrain info for per-cell validation
    const MapCell* cellDataBase = mapGrid->cells.GetData();
    uint32_t terrainMask = desc->_u20;
    const ComponentDescription* allowedEntityType = desc->_ref70;

    // Build a placement validity bitmap (using System::String pattern from decompilation)
    // The original builds a string-like structure for the grid. We simplify to direct checks.

    TileArray previewTiles;

    int iterHeight = height;  // decrements as rows advance

    for (int r = minRow; r <= maxRow; r++) {
        for (int c = minCol; c <= maxCol; c++) {
            // Check if this cell was already placed (edit mode overlap flag)
            // The original uses a string-based bitmap. We check directly.
            bool alreadyPlaced = false;
            // (The bitmap logic from the original is complex; for preview purposes,
            //  we skip it — placed tiles are drawn in Phase 1.)

            bool valid = false;
            if (c >= 0 && c < mapWidth && r >= 0 && r < mapWidth) {
                int cellIdx = mapWidth * r + c;
                const MapCell* cellPtr = &cellDataBase[cellIdx];

                // Terrain type check
                uint8_t terrainByte = static_cast<uint8_t>(cellPtr->terrainType);
                if ((~terrainMask & (1u << (terrainByte & 0x1f))) != 0) {
                    valid = false;
                } else {
                    // Entity occupancy check
                    const ComponentEntity* cellEntity = cellPtr->blocker.Get();
                    if (allowedEntityType != nullptr) {
                        if (cellEntity != nullptr) {
                            if (cellPtr->blocker->_description != allowedEntityType) {
                                valid = false;
                                goto after_check;
                            }
                        }
                    } else {
                        if (cellEntity != nullptr) {
                            valid = false;
                            goto after_check;
                        }
                    }

                    // Occupied flag check
                    if (!alreadyPlaced && cellPtr->footprintCount != 0) {
                        valid = false;
                        goto after_check;
                    }

                    // Height validation
                    {
                        float halfOffset = (float)(mapWidth >> 1) - HALF_CELL_OFF;
                        float inPos[4] = {(float)(cellIdx % mapWidth) - halfOffset,
                                          (float)(cellIdx / mapWidth) - halfOffset, 0.0f, 0.0f};
                        float outPos[4] = {0};
                        float extra[3] = {0};   // retail: a Vector3 (0x1400dc890), [2] read after the call
                        mapGrid->GetTerrainPos(outPos, inPos, '\0', extra);

                        // The query's third out, z: red when the input z (XMM6, 0.0) exceeds it
                        // -- `COMISS XMM6,[RBP-0x40]; JA` to the red arm (0x1400dc8ad / 0x1400dc8b1).
                        if (!(inPos[2] > extra[2])) {
                            Math::Scalar heightDiff(outPos[2] - 0.0f);
                            if (heightDiff.Abs().v <= HEIGHT_TOL) {
                                valid = true;
                            }
                        }
                    }
                }
            }
after_check:

            ToolTileRecord rec;
            rec.col   = c;
            rec.row   = r;
            rec.variant = alreadyPlaced ? 2 : 0;
            rec.color = (tooLarge || !valid) ? 0xffff0000u : 0xff00ff00u;
            rec.angle = 0;
            previewTiles.SetSize(previewTiles._size + 1, rec);
        }
        iterHeight--;
    }

    // Render preview tiles (the 0x20-byte segment array is always empty here)
    Collection::Array<ToolSegmentRecord> lineArray;
    PlayerTileRender_DrawOverlay_08c0(nullptr, context, &previewTiles, &lineArray);
    // previewTiles / lineArray are released by ~Array at scope exit.
}

// ============================================================
// Local function pointers for Startup
// ============================================================


// ============================================================
// Helper: search a description's sub-descriptions for T
// ============================================================
// Returns the first sub-description whose Type() matches T's id and that
// IsDerivedFrom it, or nullptr; a type match that fails IsDerivedFrom ends the
// search. Same pattern as DragPlacementTool's FindToolDescription, local to this TU.
template <class T>
static T* FencedFindComponent(const ComponentDescription* entity) {
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

// The section-0 template of a CreateFencedDescription (+0x28, ExternalPtr[6]).
static const ComponentDescription* FencedSection0(const CreateFencedDescription* desc) {
    return desc->_refs[0];
}

// ============================================================
// FencedTool::Startup (vtable +0x20, Slot4)
// ============================================================
// Initializes the fenced tool by finding description components on the entity,
// resolving RotationSetting, allocating a MeshInstance for fence preview, and
// setting its mesh from the FenceDescription's model.
//
// Component lookups:
//   1. CreateFencedDescription (cell 0x1401c4100) on the tool group -> this->fencedDesc (+0x38)
//   2. MapDescription (cell 0x1401c7a18) on the tool group -> this->areaDesc (+0x40)
//   3. ModelDescription (cell 0x1401c7a88) on fencedDesc's section 0 -> this->fenceDesc (+0x48)
//   4. CreatePlacedDescription (cell 0x1401c7468) on fencedDesc's section 0 ->
//      this->_field5C, _field60
//
// Also resolves RotationSetting IBP from toolSetting and allocates MeshInstance.
// Original: 0x1400dcd50
void FencedTool::Startup() {

    // 1. Find CreateFencedDescription on the tool group (+0x20)
    CreateFencedDescription* fencedDesc = FencedFindComponent<CreateFencedDescription>(descriptionPtr);
    this->fencedDesc = fencedDesc;
    // No test of the result: 0x1400dcd50 stores it at +0x38 and goes on.

    // 2. Find MapDescription on the tool group (+0x20)
    MapDescription* areaDesc = FencedFindComponent<MapDescription>(descriptionPtr);
    this->areaDesc = areaDesc;

    // 3. Find ModelDescription on fencedDesc's section 0 (+0x28)
    ModelDescription* fenceDesc = FencedFindComponent<ModelDescription>(FencedSection0(this->fencedDesc));
    this->fenceDesc = fenceDesc;

    // 4. Resolve RotationSetting from toolSetting IBP (+0x18)
    //    CheckPtr on the IBP first, then IsDerivedFrom, then construct new IBP at +0x50
    ToolSetting* toolSettingVal = toolSetting;
    RotationSetting* rotSetting = nullptr;
    if (toolSettingVal) {
        bool isDerived = Object::IsDerivedFromNullSafe(toolSettingVal, RotationSetting::TypeId());   // cell 0x1401c63c8
        if (isDerived) {
            rotSetting = static_cast<RotationSetting*>(toolSettingVal);
        }
    }
    // The InternalBasePtr at +0x50 holds the resolved RotationSetting
    rotationSetting = rotSetting;

    // 5. Find CreatePlacedDescription on fencedDesc's section 0 -> read +0x44 and +0x48
    const CreatePlacedDescription* sizeDesc1 =
        FencedFindComponent<CreatePlacedDescription>(FencedSection0(this->fencedDesc));
    // Read field +0x44 from CreatePlacedDescription -> this->_field5C
    _field5C = static_cast<uint32_t>(sizeDesc1->_i44);

    // Find CreatePlacedDescription again for +0x48 (original code does two separate lookups)
    const CreatePlacedDescription* sizeDesc2 =
        FencedFindComponent<CreatePlacedDescription>(FencedSection0(this->fencedDesc));
    // Read field +0x48 from CreatePlacedDescription -> this->_field60
    _field60 = static_cast<uint32_t>(sizeDesc2->_i48);

    // 6. Allocate MeshInstance (0x120 bytes, alignment 8)
    void* meshMem = System::MemoryAllocator::MemAlloc(0x120, 8);
    Graphics::MeshInstance* meshInst = nullptr;
    if (meshMem) {
        // Retail: `??0MeshInstance` then the exe's copy 0x14015e240 of the class's
        // own table (not a subclass) = `new MeshInstance()`.
        meshInst = ::new (meshMem) Graphics::MeshInstance();   // B.3.9 EXA: the real class (exe table 0x14015e240 is its own)
    }
    meshInstance = meshInst;

    // 7. Set mesh from the ModelDescription's first mesh group, last mesh
    //    (+0x20 Array<ModelMeshGroup> data; group +0x08 meshes, +0x10 count)
    const ModelMeshGroup* modelArray = this->fenceDesc->_meshGroups;
    int modelCount = modelArray->meshCount;
    Graphics::GraphicsMesh** modelData = modelArray->meshes;
    Graphics::GraphicsMesh* lastMesh = modelData[modelCount - 1];  // last entry

    if (meshInst) {
        meshInst->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(lastMesh));
    }

    // 8. UpdateHierarchy — tail call in original (JMP to dispatch slot)
    if (meshInstance) {
        meshInstance->UpdateHierarchy();
    }
}

// ============================================================
// FencedTool::CalcPath (vtable +0x68, Slot13)
// ============================================================
// Creates fence entities along the drag rectangle perimeter.
//
// This function is extremely complex (~150 lines decompiled) and depends on
// ~15 unresolved helper functions:
//   - FUN_1400dbf90: anchor/rotation calculation (already resolved as s_fn_FencedTool_AnchorCalc)
//   - ComponentEntity_CopyCtor: entity deep-copy
//   - FUN_140004680: entity component accessor
//   - FUN_140004740: entity component accessor (different type)
//   - FUN_1401018d0: set cell range on entity
//   - Entity_ClearPathQueue: clear entity path queue
//   - Math::Transform::FromYawPitchOffset: build rotation transform
//   - Math::Transform::operator=: copy transform
//   - Graphics::Geometry::SetTransform: apply transform to geometry
//   - FencedTool_helper_90d0: link fence entity to main entity
//   - FUN_140003110: append entity to internal list
//   - FUN_1400c48a0: register entity with world entity list
//   - FencedTool_Slot13_sub_bc80: place corner fence posts (4 calls)
//   - FencedTool_Slot13_sub_b8c0: place side fence segments (4 calls)
//
// PORTED 2026-07-30: full reimplementation in fencedtool_calcpath.cpp (master
// entity + shelter + corner/edge/gate fence pieces via MultiObject). The old
// body here delegated to ResolveAddr(0x1400dae00) which was never registered,
// so committing a fenced drag was a silent no-op.
//
// Original: 0x1400dae00
// ENDGAME B.3.8 callers: the body is FencedTool::CalcPath (fencedtool_calcpath.cpp).

// ============================================================
// FencedTool members (FencedTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_FencedTool_Vtable[15], its slot stubs, the _SA factories
// and RegisterFencedToolVtable are gone: the compiler builds the table, and
// nothing reads 0x1401605d0 back by address (the old dtor was the only
// reader). Inherited from the bases, as in retail: [10] false, [12] cursor 5.
// ============================================================
#include "Object/factory_registration.h"
#include "Player/RotationSetting.h"
#include <new>

// [4] Startup, [5] Shutdown, [6] OnKey, [7] Update, [8] Draw, [9] OnCancel and
// [14] CalcCost are defined above; [13] CalcPath is in fencedtool_calcpath.cpp.

// [11] 0x140026920 `MOV EAX,[TypeInfo_RotationSetting]; MOV [RDX],EAX; MOV
// RAX,RDX; RET`: FencedTool HAS a tool setting — the rotation the shelter edge
// follows; SetEditMode builds it from this id and Startup wires it to +0x50.
// The old stub resolved the id through a function-local lazy cache; the class
// static reads the registration record written once at boot (B6-5).
Object::Type* FencedTool::GetSettingType(Object::Type* ret) const {
    *ret = Object::Type(RotationSetting::TypeId());
    return ret;
}

// create 0x1400268d0: MemAlloc(0x70, 8), then the ctor 0x1400dd070. The ctor
// sets sceneObjectId = -1 ("preview not in the scene"): the tree's zero-fill
// once turned that into a live id 0 (the start-town barn's mesh) and
// Scene::RemoveObject(0) spun forever.
::Object::Base* FencedTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FencedTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FencedTool());
}
// createInPlace 0x140026910: `if (storage) ctor(storage)`.
::Object::Base* FencedTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FencedTool());
}

DEFINE_EXE_TYPE(FencedTool, "DragTool")
