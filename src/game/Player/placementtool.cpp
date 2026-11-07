// src/game/Player/placementtool.cpp
// Clean reimplementation of PlacementTool vtable methods from Application-x64.exe.
//
// PlacementTool (0x70 bytes) extends Tool (0x28 bytes) directly (NOT DragTool).
// Handles building placement: rotation, cost preview, mesh rendering.
//
// Vtable at 0x140161d20 (13 slots). Currently reimplemented here:
//   Slot0  Dtor       (0x14002dc90) — scalar deleting destructor
//   Slot5  Shutdown   (0x1400fe4e0) — cleanup scene objects + mesh + dialog
//   Slot6  OnKey      (0x1400fe2a0) — rotation keys + placement input
//   Slot7  Update     (0x1400fdee0) — placement logic + cost UI update
//
// Decompiled from Application-x64.exe.
//
// B.4 PLAYER: every field is read by name. The tool's owner (Tool +0x10) is the
// PlayerToolManager: its +0x00 holds the open tool dialog, its +0x80 / +0x98 /
// +0x9C / +0xA0 the tutorial's placement constraint.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Scene.h"
#include "Player/edittool_funcs.h"   // PlacementTool_Factory_SA
#include "graphics/Mesh.h"
#include <new>
#include "Player/PlacementTool.h"
#include "Player/RotationSetting.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/MapDescription.h"
#include "Component/ModelDescription.h"
#include "Component/RadiusDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/ToolbarDescription.h"
#include "PlacementTool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "ui/DialogInstance.h"
#include "object/External.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Game/PlayerToolManager.h"
#include "math/Math.h"
#include "system/String.h"
#include "system/Value.h"   // ENDGAME B.3.4b System callers: System::Random
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "object/Internal.h"    // ENDGAME B.3.6 UI callers (K4)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K4)
#include "input/Input.h"        // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)
#include "Player/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"

// ============================================================
// Exe helpers, called by name:
//   0x1400b0d00 DecalComponent_TerrainDecalRemove (Component/exe_bodies.h) --
//               the highlight/overlay removal
//   0x1400fd960 PlacementTool::FlipVariant, 0x1400fda00 PlacementTool::GetPlacement,
//   0x1400fd650 PlacementTool::CreateAt (members);
//   0x14009d580 ToolTile_FootprintValidity_d580, 0x1400ffc60 UpdateToolUI_fc60,
//   0x140005160 DragTool_FindDescription_5160, 0x1400ffbf0 DragTool_PlayTickSound_ffbf0,
//   0x1401000f0 DragTool_PlayPlaceSound_1000f0, 0x140100140 DragTool_PlayErrorSound_100140,
//   0x1400ff9e0 ToolUI_OpenToolDialog_ff9e0, 0x140055430 Player_HighlightAdd_55430,
//   0x140101380 PlayerTileRender_BuildRecords_1380 (Player/exe_bodies.h)
//   0x14009f3e0 Reimpl_MapGetTerrainPos (Game/exe_bodies.h)
// The bodies' return types are wider than the bool / char the old door typedefs
// read: each call reads the low byte (AL), as the door did.
// ============================================================

// DAT_140172248 (.rdata): the float the tile-validity / tile-render calls pass as
// their second float. The cell holds 0x447a0000 = 1000.0f (read 2026-10-02).
static constexpr float kRdata_140172248_f = 1000.0f;

// ============================================================
// Helper: get KeyMap object from external reference (triple deref)
// ============================================================
static inline const Input::KeyMap* GetKeyMap() {
    return Input::Configuration::_singleton->_keyMap;
}

// ============================================================
// PlacementTool::Shutdown (vtable +0x28, Slot5)
// ============================================================
// Removes placement preview from the scene, destroys the mesh instance,
// and closes the associated dialog.
// Original: 0x1400fe4e0
void PlacementTool::Shutdown() {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // Remove scene object if added (sceneObjectIdx >= 0)
    if (sceneObjectIdx >= 0) {
        Graphics::Scene* scene = gameWorld->m_graphicsScene;
        if (scene) {
            scene->RemoveObject(sceneObjectIdx);
        }
    }

    // Remove radius display highlight if active (radiusDisplayIdx >= 0)
    if (radiusDisplayIdx >= 0) {
        WorldGeometry* highlightMgr = gameWorld->m_worldGeometry;
        if (highlightMgr) {
            highlightMgr->RemoveDecal(radiusDisplayIdx);
        }
    }

    // Destroy mesh instance via vtable dtor if allocated
    Graphics::MeshInstance* mesh = meshInstance;
    if (mesh) {
        delete mesh;   // slot 0, free
    }

    // Close dialog if open: the owner (+0x10) holds it at its +0x00
    PlayerToolManager* owner = dialogPtr;
    UI::DialogInstance* ibpVal = owner->field_0x00;
    if (ibpVal != nullptr) {
        // Close the dialog via UI::UserInterface
        UI::UserInterface* uiMgr = gameWorld->m_userInterface;
        if (uiMgr) {
            Object::InternalPtr<UI::DialogInstance> localIBP(ibpVal);
            uiMgr->CloseDialog(&localIBP);
        }

        // Clear the InternalBasePtr
        owner->field_0x00 = nullptr;
    }
}

// ============================================================
// PlacementTool::OnKey (vtable +0x30, Slot6)
// ============================================================
// Main input handler for building placement tool.
//
// If param2 == 0: close dialog and clear dialog IBP
// If param2 != 0 and dialog not open: open dialog via helper
// Then check KeyMap for rotation/flip/placement keys:
//   Key 0x13: rotate CCW (decrement rotation, wrap <0 to 3)
//   Key 0x14: rotate CW (increment rotation, wrap >3 to 0)
//   Key 0x15: flip/mirror via 0x1400fd960
//   Key 0x05: placement confirmation (if param2 != 0)
// Always returns 1.
//
// Disassembly reference: 0x1400fe2a0 (the rotation compares are signed: JGE /
// JLE at 0x1400fe364 / 0x1400fe3b7)
char PlacementTool::OnKey(char param2) {
    PlayerToolManager* owner = dialogPtr;

    // --- Dialog open/close logic ---
    if (param2 == 0) {
        // Close dialog if open (check value twice, matching original)
        if (owner->field_0x00 != nullptr && owner->field_0x00 != nullptr) {
            Object::InternalPtr<UI::DialogInstance> localIBP(owner->field_0x00);
            UI::UserInterface* uiMgr = GameUpdateLoop::_singleton->m_userInterface;
            if (uiMgr) {
                uiMgr->CloseDialog(&localIBP);
            }
            // Clear IBP
            owner->field_0x00 = nullptr;
        }
    } else {
        // Open dialog if not already open
        if (owner->field_0x00 == nullptr) {
            UI::DialogInstance* localResult = nullptr;
            owner->OpenToolDialog(&localResult, descriptionPtr);
        }
    }

    // --- Key handling ---
    const Input::KeyMap* keyMap = GetKeyMap();

    // Key 0x13: Rotate counter-clockwise
    bool rotCCW = keyMap->Pressed(0, ToUnderlying(GameKey::StructureLeft));
    if (rotCCW) {
        rotationIBP->rotation = rotationIBP->rotation - 1;

        // Wrap: if < 0, set to 3
        if (rotationIBP->rotation < 0) {
            rotationIBP->rotation = 3;
        }
    } else {
        // Key 0x14: Rotate clockwise
        bool rotCW = keyMap->Pressed(0, ToUnderlying(GameKey::StructureRight));
        if (rotCW) {
            rotationIBP->rotation = rotationIBP->rotation + 1;

            // Wrap: if > 3, set to 0
            if (rotationIBP->rotation > 3) {
                rotationIBP->rotation = 0;
            }
        } else {
            // Key 0x15: Flip/Mirror
            bool flip = keyMap->Pressed(0, ToUnderlying(GameKey::StructureType));
            if (flip) {
                FlipVariant();
            }
        }
    }

    // --- Placement confirmation (key 5, only when active) ---
    if (param2 != 0) {
        bool placeKey = keyMap->Pressed(0, ToUnderlying(GameKey::MouseLeft));
        if (placeKey) {
            // Calculate placement position
            int startCol = 0, startRow = 0, endCol = 0, endRow = 0;
            Math::Vector3 pos(0.0f, 0.0f, 0.0f);
            float yaw[2] = {0.0f, 0.0f};

            {
                bool valid = GetPlacement(&startCol, &startRow, &endCol, &endRow, &pos, yaw) != 0;
                if (valid) {
                    // Try to place via 0x1400fd650
                    {
                        bool placed = static_cast<uint8_t>(CreateAt(startCol, startRow,
                                                                    endCol, endRow, &pos, yaw[0])) != 0;
                        if (placed) {
                            // Placement success — finalize
                            DragTool_PlayPlaceSound_1000f0(this);
                            return 1;
                        }
                    }
                    // Placement failed — run failure handler
                    DragTool_PlayErrorSound_100140(this);
                }
            }
        }
    }

    return 1;
}

// ============================================================
// PlacementTool::Update (vtable +0x38, Slot7)
// ============================================================
// Manages the placement preview visualization, each frame.
//
// When active (param3 != 0):
//   1. GetPlacement hit-tests the current cursor position
//   2. If valid: manage scene object, set transform, update bounding box,
//      check tile validity, update cost dialog
//   3. If invalid: remove scene object and highlight
// When inactive (param3 == 0):
//   Remove scene object and highlight
//
// Disassembly reference: 0x1400fdee0
// Direct calls:
//   0x1400fda00 = GetPlacement, 0x1400b0d00 = HighlightRemove,
//   0x140055430 = HighlightAdd (radius display), 0x14009d580 = TileValidityCheck,
//   0x140005160 = description lookup, 0x1400ffc60 = dialog update,
//   0x1400ffbf0 = cell change notify (tick sound)
//
// Original: 0x1400fdee0
void PlacementTool::Update(const System::Time* /*param2*/, char param3) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    Math::Vector3 pos(0.0f, 0.0f, 0.0f);
    int startCol = 0, startRow = 0, endCol = 0, endRow = 0;
    float yaw[2] = {0.0f, 0.0f};

    bool valid = false;
    if (param3 != 0) {
        valid = GetPlacement(&startCol, &startRow, &endCol, &endRow, &pos, yaw) != 0;
    }


    if (!valid || param3 == 0) {
        // --- Invalid / inactive: remove scene object and highlight ---
        if (sceneObjectIdx >= 0) {
            Graphics::Scene* scene = gameWorld->m_graphicsScene;
            if (scene) {
                scene->RemoveObject(sceneObjectIdx);
            }
            sceneObjectIdx = -1;
        }
        if (radiusDisplayIdx >= 0) {
            WorldGeometry* highlightMgr = gameWorld->m_worldGeometry;
            if (highlightMgr) {
                highlightMgr->RemoveDecal(radiusDisplayIdx);
            }
            radiusDisplayIdx = -1;
        }
        return;
    }

    // --- Valid placement: update preview ---
    Map* mapSize = gameWorld->m_game;

    // Update radius display if center cell changed (only if radiusComponent exists)
    if (radiusComponent != nullptr) {
        // Calculate center cell from placement bounds
        int centerRow = ((endRow - startRow) >> 1) + startRow;
        int centerCol = ((endCol - startCol) >> 1) + startCol;
        int centerCell = mapSize->size * centerRow + centerCol;

        if (centerCell != lastCenterCell) {
            // Remove old highlight
            if (radiusDisplayIdx >= 0) {
                WorldGeometry* highlightMgr = gameWorld->m_worldGeometry;
                if (highlightMgr) {
                    highlightMgr->RemoveDecal(radiusDisplayIdx);
                }
            }
            // Add new highlight
            radiusDisplayIdx = radiusComponent->AddHighlight(centerCell);
            lastCenterCell = centerCell;
        }
    }

    // Add scene object if not yet added
    if (sceneObjectIdx < 0) {
        Graphics::Scene* scene = gameWorld->m_graphicsScene;
        if (scene && meshInstance) {
            sceneObjectIdx = (int)scene->AddObject(meshInstance);
        }
    }

    // Set transform: FromYawPitchOffset(retbuf, &yaw, &pitch=0, &offset).
    // Ghidra (0x1400fdff3): R9=offset = the cursor WORLD position from GetPlacement
    // (pos), NOT zero — that is what makes the ghost mesh follow the cursor. R8=pitch
    // is zeroed ([RBP-0x70]).
    float zeroPitch = 0.f;
    Math::Transform localTransform =
        Math::Transform::FromYawPitchOffset(yaw[0], zeroPitch, pos);

    // Where the ghost mesh is actually being put this frame. If the preview appears
    // somewhere the cursor is not, this line says whether `pos` (the cursor world
    // position) is wrong or whether something downstream moved the mesh.
    {
        static int n = 0;
        if (n < 24) { ++n;
            fprintf(stderr, "[Preview] ghost transform: pos=(%.2f, %.2f, %.2f) yaw=%.2f "
                            "cells=(%d,%d)..(%d,%d) mesh=%p\n",
                    pos.x, pos.y, pos.z, yaw[0], startCol, startRow, endCol, endRow,
                    static_cast<void*>(meshInstance));
            fflush(stderr);
        }
    }

    if (meshInstance) {
        meshInstance->SetTransform(localTransform);
    }

    if (meshInstance) {
        meshInstance->UpdateHierarchy();
    }

    // Tile validity check via 0x14009d580
    // Uses the CreatePlacedDescription (+0x28) fields
    const CreatePlacedDescription* descComp = descComponent;
    int width = (endCol - startCol) + 1;
    int height = (endRow - startRow) + 1;
    int yawInt = (int)yaw[0];  // CVTTSS2SI EDI, XMM6
    char tileValid = 1;

    if (descComp) {
        // Match the calling convention from disasm at 0x1400fe09e:
        // RCX=map, EDX=startCol, R8D=startRow, R9D=width
        // stack: height, category(+0x20), &desc+0x30, &desc+0x28, 0.0, DAT_140172248, 0.0,
        //        0, &mapDesc+0x28, yawInt, desc+0x50
        tileValid = (char)(static_cast<uint8_t>(mapSize->FootprintValidity(startCol,       // R14D (from disasm)
            startRow,       // ESI (from disasm)
            width,          // R9D
            height,         // [RSP+0x20]
            descComp->_category,               // [RSP+0x28]
            descComp,                          // [RSP+0x30] — LEA desc+0x30, the code array
            &descComp->_name,                  // [RSP+0x38] — LEA desc+0x28, the code map
            0.0f,                              // [RSP+0x40]
            kRdata_140172248_f,                // [RSP+0x48] DAT_140172248
            0.0f,                              // [RSP+0x50]
            0,                                 // [RSP+0x58]
            &mapComponent->_footprint,         // [RSP+0x60] — mapComponent+0x28
            yawInt,                            // [RSP+0x68]
            descComp->_created)) != 0);        // [RSP+0x70]
    }

    // Check the owner's constraint flag (+0x80)
    const PlayerToolManager* dialogCtrl = dialogPtr;
    if (dialogCtrl->field_0x80) {
        int mapSz = gameWorld->m_game->size;
        // Original checks: startCol vs (+0x98)%mapSz, startRow vs (+0x98)/mapSz,
        //                   endCol vs (+0x9c)%mapSz, endRow vs (+0x9c)/mapSz,
        //                   yawInt vs (+0xa0)
        if (startCol != dialogCtrl->field_0x98 % mapSz ||
            startRow != dialogCtrl->field_0x98 / mapSz ||
            dialogCtrl->field_0x9C % mapSz != endCol ||
            dialogCtrl->field_0x9C / mapSz != endRow ||
            dialogCtrl->field_0xA0 != yawInt) {
            tileValid = 0;
        }
    }

    // Get cost/status strings from the tool group's ToolbarDescription
    ToolbarDescription* costResult = nullptr;
    costResult = descriptionPtr->FindDescription<ToolbarDescription>();   // 0x140005160

    // Build cost string: index into the description's string array (+0x50).
    // Index is 0 (valid) or 1 (invalid), based on tileValid
    uint32_t strIndex = (tileValid == 0) ? 1 : 0;
    System::String costString;  // System::String (8-byte COW refcounted)

    if (costResult != nullptr && (int)strIndex < costResult->_entries._size) {
        // Retail copy-constructs a temporary, copies that into costString and
        // then releases the temporary — the scope below does both.
        System::String tempString(costResult->_entries._data[strIndex]);
        costString = tempString;
    } else {
        // Empty-string fallback: retail aliases the zeroed costResult local as an
        // empty String, copies it and releases it — both no-ops on a null String,
        // so costString simply stays empty.
        costResult = nullptr;
    }

    // Get bounding box from mesh instance (+0x34 to +0x48 = 6 floats)
    Graphics::MeshInstance* mesh = meshInstance;
    ::Box bbox;
    if (mesh) {
        bbox = mesh->_localBounds;
    } else {
        bbox = ::Box{};
    }

    // Get the mesh transform (by value → hidden retbuf; RCX=mesh, RDX=&retbuf).
    Math::Transform meshTransformVal;
    const Math::Transform* meshTransform = nullptr;
    if (mesh) {
        meshTransformVal = mesh->GetTransform();
        meshTransform = &meshTransformVal;
    }

    // Update dialog with placement info via 0x1400ffc60
    // Original calling convention at 0x1400fe1f9:
    //   RCX = the owner (+0x10), EDX = 0, R8D = 0, R9 = &costString,
    //   [RSP+0x20] = R13B (tileValid), [RSP+0x28] = 1, [RSP+0x30] = 0,
    //   [RSP+0x38] = 1, [RSP+0x40] = -1, [RSP+0x48] = -1,
    //   [RSP+0x50] = &bbox, [RSP+0x58] = transform*
    {
        dialogPtr->UpdateCreateBar(// RCX: the owner at +0x10
            0, 0,                   // EDX, R8D
            &costString,            // R9: pointer to cost string
            tileValid,              // [RSP+0x20]
            1,                      // [RSP+0x28]
            0,                      // [RSP+0x30]
            1,                      // [RSP+0x38]
            -1,                     // [RSP+0x40]
            -1,                     // [RSP+0x48]
            &bbox,                  // [RSP+0x50]
            meshTransform);         // [RSP+0x58]
    }

    // Check if cell position changed (triggers tick sound)
    if (lastStartCol != startCol || lastStartRow != startRow) {
        lastStartCol = startCol;
        lastStartRow = startRow;
        dialogPtr->PlayTickSound();
    }
}

// ============================================================
// PlacementTool::Draw (vtable +0x40, Slot8)
// ============================================================
// Renders the placement overlay tiles on the terrain.
//
// When param3 == 0 (active placement preview):
//   1. GetPlacement gives the placement bounds (startCol/startRow/endCol/endRow)
//   2. Read rotation from the RotationSetting at +0x48
//   3. Read description fields (category, code array, yaw angle, entity template)
//   4. Delegate to 0x140101380 which validates each cell and renders colored tiles
//
// Always checks the owner (+0x10) for an existing placement constraint at +0x80:
//   if set, renders that existing placement in yellow via 0x140101380.
//
// Original: 0x1400fdcc0
void PlacementTool::Draw(Video::Context* context, char param3) {
    float halfCell = kRdata_140172248_f;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // --- Section 1: Active placement preview (param3 == 0) ---
    if (param3 == 0) {
        int startCol = 0, startRow = 0;
        int endCol = 0;
        int endRow = 0;
        Math::Vector3 pos(0.0f, 0.0f, 0.0f);
        float yaw = 0.0f;

        bool valid = GetPlacement(&startCol, &startRow, &endCol, &endRow, &pos, &yaw) != 0;

        if (valid) {
            // Read descComponent before CheckPtr (used for yawAngle and entityTemplate)
            const CreatePlacedDescription* descComp1 = descComponent;  // +0x28

            // Re-read descComponent after CheckPtr (used for the category, code maps)
            const CreatePlacedDescription* descComp2 = descComponent;  // +0x28
            const MapDescription* mapComp = mapComponent;              // +0x30

            // The rotation * 90 degrees
            int yawInt = rotationIBP->rotation * 0x5a;

            // yawAngle from first descComponent read (+0x40)
            int yawAngle = descComp1->_i40;

            PlayerTileRender_BuildRecords_1380(
                &mapComp->_footprint,            // param1: mapComponent+0x28 (never read)
                context,                         // param2: render context
                startCol,                        // param3: startCol
                startRow,                        // param4: startRow
                static_cast<uint32_t>(endCol),   // param5: endCol
                endRow,                          // param6: endRow
                descComp2->_category,            // param7: terrainMask
                descComp2,                       // param8: desc+0x30 (LEA)
                &descComp2->_name,               // param9: desc+0x28 (LEA)
                0.0f,                            // param10
                halfCell,                        // param11: DAT_140172248
                0.0f,                            // param12
                0,                               // param13: forceRed = false
                0,                               // param14: allowOverlap = false
                &mapComp->_footprint,            // param15: mapComp+0x28 (same as param1)
                yawInt,                          // param16: rotation*90
                yawAngle,                        // param17: desc+0x40
                0,                               // param18: isExisting = false
                descComp1->_created              // param19: entityTemplate from desc
            );
        }
    }

    // --- Section 2: Existing placement marker (if the owner's constraint is set) ---
    const PlayerToolManager* dialogCtrl = dialogPtr;
    if (dialogCtrl->field_0x80) {
        const CreatePlacedDescription* descComp = descComponent;  // +0x28
        int mapSz1 = gameWorld->m_game->size;
        int mapSz2 = gameWorld->m_game->size;  // re-read (matches original)

        int existStart = dialogCtrl->field_0x98;
        int existEnd = dialogCtrl->field_0x9C;
        uint32_t existYaw = static_cast<uint32_t>(dialogCtrl->field_0xA0);

        PlayerTileRender_BuildRecords_1380(
            nullptr,                             // param1: (retail: the yaw in RCX; never read)
            context,                             // param2: render context
            existStart % mapSz2,                // param3: startCol
            existStart / mapSz2,                // param4: startRow
            (uint32_t)(existEnd % mapSz1),      // param5: endCol
            existEnd / mapSz1,                  // param6: endRow
            descComp->_category,                // param7: terrainMask
            descComp,                           // param8: desc+0x30
            &descComp->_name,                   // param9: desc+0x28
            0.0f,                                // param10
            halfCell,                            // param11
            0.0f,                                // param12
            0,                                   // param13: forceRed = false
            0,                                   // param14: allowOverlap = false
            &mapComponent->_footprint,          // param15: mapComp+0x28
            (int)existYaw,                       // param16: yawInt
            descComp->_i40,                     // param17: yaw angle from desc
            1,                                   // param18: isExisting = true
            descComp->_created                  // param19: entityTemplate
        );
    }
}

// ============================================================
// PlacementTool::Startup (vtable +0x20, Slot4)
// ============================================================
// Initializes the placement tool by finding the descriptions among the tool
// group's sub-descriptions (+0x20), resolving the RotationSetting IBP, allocating
// a MeshInstance, and selecting a random mesh variant.
//
// Description lookups (Type() slot 2 + IsDerivedFrom):
//   1. cell 0x1401c7468 CreatePlacedDescription -> descComponent   (+0x28)
//   2. cell 0x1401c7a18 MapDescription          -> mapComponent    (+0x30)
//   3. cell 0x1401c7a88 ModelDescription        -> createComponent (+0x38)
//   4. cell 0x1401c4870 RadiusDescription       -> radiusComponent (+0x40)
//
// Then:
//   5. CheckPtr on IBP at +0x18, IsDerivedFrom RotationSetting, store IBP at +0x48
//   6. Allocate 0x120-byte MeshInstance, construct
//   7. Random::Rand(meshCount) to pick variant
//   8. SetMesh from variant's mesh group (last entry)
//   9. UpdateHierarchy on mesh
//  10. Store variant index at +0x64
//
// Original: 0x1400fe580

static bool s_startupTypeIdsLogged = false;

static void LogStartupTypeIds() {
    if (s_startupTypeIdsLogged) return;

    // The four *Description type-ids Startup's findComp uses (D5, ENDGAME B.3.9 CG3):
    // read BY CELL, never by the (wrong) engine_funcs global names.
    //   +0x28 descComponent   = cell 0x1401c7468 = CreatePlacedDescription (NOT "SizeDescription")
    //   +0x30 mapComponent    = cell 0x1401c7a18 = MapDescription          (NOT "AreaDescription")
    //   +0x38 createComponent = cell 0x1401c7a88 = ModelDescription        (NOT "FenceDescription");
    //         Startup derefs createComponent+0x28 (mesh count) UNGUARDED — MUST be present.
    //   +0x40 radiusComponent = cell 0x1401c4870 = RadiusDescription       (NOT "RadiusComponent")
    fprintf(stderr, "[Placement] type-ids: CreatePlaced=%u Map=%u Model=%u Radius=%u\n",
            CreatePlacedDescription::TypeId(), MapDescription::TypeId(),
            ModelDescription::TypeId(), RadiusDescription::TypeId());
    fflush(stderr);

    s_startupTypeIdsLogged = true;
}

// One description lookup: the first sub-description whose Type() matches, if it
// IsDerivedFrom the type; a type match that fails stops the search (null).
template <class T>
static T* FindToolDescription(const ComponentDescription* entity) {
    if (!entity) return nullptr;
    const uint32_t targetTypeId = T::TypeId();
    int count = entity->_subDescCount;
    Description** components = entity->_subDescs;
    for (int i = 0; i < count; i++) {
        Description* comp = components[i];
        if (!comp) continue;
        if (comp->Type() == targetTypeId) {
            if (Object::IsDerivedFromNullSafe(comp, targetTypeId)) {
                return static_cast<T*>(comp);
            }
            return nullptr;
        }
    }
    return nullptr;
}

void PlacementTool::Startup() {
    LogStartupTypeIds();

    const ComponentDescription* entity = descriptionPtr;  // +0x20

    // 1. CreatePlacedDescription (cell 0x1401c7468) -> descComponent (+0x28)
    descComponent = FindToolDescription<CreatePlacedDescription>(entity);

    // 2. MapDescription (cell 0x1401c7a18) -> mapComponent (+0x30)
    mapComponent = FindToolDescription<MapDescription>(entity);

    // 3. ModelDescription (cell 0x1401c7a88) -> createComponent (+0x38)
    createComponent = FindToolDescription<ModelDescription>(entity);

    // 4. RadiusDescription (cell 0x1401c4870) -> radiusComponent (+0x40)
    radiusComponent = FindToolDescription<RadiusDescription>(entity);

    // --- Resolve RotationSetting IBP ---
    // Read the setting (+0x18), check IsDerivedFrom RotationSetting
    ToolSetting* ibpVal = toolSetting;
    RotationSetting* rotSetting = nullptr;
    if (ibpVal) {
        bool isDerived = Object::IsDerivedFromNullSafe(ibpVal, RotationSetting::TypeId());   // cell 0x1401c63c8
        if (isDerived) {
            rotSetting = static_cast<RotationSetting*>(ibpVal);
        }
    }

    // Store the resolved RotationSetting at +0x48
    rotationIBP = rotSetting;

    // --- Allocate and construct MeshInstance (0x120 bytes) ---
    void* meshMem = System::MemoryAllocator::MemAlloc(0x120, 8);

    Graphics::MeshInstance* meshInst = nullptr;
    if (meshMem) {
        // Retail: `??0MeshInstance` then the exe's copy 0x14015e240 of the class's
        // own table (not a subclass) = `new MeshInstance()`.
        meshInst = ::new (meshMem) Graphics::MeshInstance();   // B.3.9 EXA: the real class (exe table 0x14015e240 is its own)
    }
    meshInstance = meshInst;

    // --- Pick random mesh variant ---
    // The ModelDescription (+0x38) has the mesh-group count at +0x28
    const ModelDescription* modelComp = createComponent;
    uint32_t meshCount = static_cast<uint32_t>(modelComp->_meshGroupCount);

    // Game world's Random (+0xd8)
    System::Random* rng = GameUpdateLoop::_singleton->m_random;

    uint32_t variant = 0;
    if (rng) {
        variant = rng->Rand(static_cast<uint32_t>(meshCount));
    }

    // --- Set mesh from the variant's mesh group (last entry) ---
    // ModelDescription +0x20: the group array (stride 0x18): +0x08 meshes, +0x10 count.
    int64_t variantIdx = (int64_t)(int32_t)variant;
    const ModelMeshGroup* entryBase = &modelComp->_meshGroups[variantIdx];

    int subCount = entryBase->meshCount;
    Graphics::GraphicsMesh* mesh = entryBase->meshes[subCount - 1];

    if (meshInstance) {
        meshInstance->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
    }

    // --- Update hierarchy ---
    if (meshInstance) {
        meshInstance->UpdateHierarchy();
    }

    // Store variant index
    meshVariant = (int32_t)variant;

    // Diagnostic: dump the built preview mesh's state so we can see why it may not
    // render. Log the bounds (mesh+0x34, 6 floats) — if zero, the renderer
    // frustum-culls it → invisible.
    if (meshInstance) {
        const ::Box& b = meshInstance->_localBounds;
        fprintf(stderr, "[Placement] mesh built: mesh=%p meshPtr=%p "
                "count=%u variant=%u bounds=[%.2f %.2f %.2f .. %.2f %.2f %.2f]\n",
                static_cast<void*>(meshInstance),
                static_cast<void*>(mesh), meshCount, variant,
                b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
        fflush(stderr);
    }
}

// ============================================================
// PlacementTool members (PlacementTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_PlacementTool_Vtable[13], its slot stubs, the in-place _SA
// factory, the post-alloc -1 patch-up and RegisterPlacementToolVtable are
// gone: the compiler builds the table, the real ctor writes retail's -1
// sentinels (the zero-fill once made Serialize remove scene object 0), and
// nothing reads 0x140161d20 back by address (the old dtor was the only
// reader). Inherited from Tool, as in retail: [10] false.
// ============================================================
#include "Object/factory_registration.h"
#include "Player/RotationSetting.h"
#include <new>

// [4] Startup, [5] Shutdown, [6] OnKey, [7] Update and [8] Draw are defined above.

// [11] 0x140026920 (shared with FencedTool): `*ret = TypeInfo_RotationSetting`.
// SetEditMode builds the RotationSetting from this id on first activation and
// dereferences the returned slot. The class static replaces the old
// function-local lazy cache (B6-5).
Object::Type* PlacementTool::GetSettingType(Object::Type* ret) const {
    *ret = Object::Type(RotationSetting::TypeId());
    return ret;
}

// create 0x14002dc40: MemAlloc(0x70, 8), then the ctor 0x1400fe850.
::Object::Base* PlacementTool::CreateNewObject() {
    return static_cast<Object::Base*>(PlacementTool_Factory_SA());
}
// createInPlace 0x14002dc80: `if (storage) ctor(storage)`.
::Object::Base* PlacementTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PlacementTool());
}

// Kept for src/diag/tool_harness.cpp (outside this block's files): the
// reflected create, by its old name.
extern "C" PlacementTool* PlacementTool_Factory_SA(void) {
    void* p = Object::detail::InternalMemAlloc(sizeof(PlacementTool), 8);
    if (!p) return nullptr;
    return new (p) PlacementTool();
}

DEFINE_EXE_TYPE(PlacementTool, "Tool")

// ============================================================
// Standalone tool-harness probe (NOT a faithful reimpl — scaffolding for
// src/diag/tool_harness.cpp, Phase-1 of GUI_PLAN Keystone F).
// Logs descriptionPtr(+0x20)'s sub-descriptions + the 4 *Description type-ids
// Startup searches for, confirms the mesh-bearing ModelDescription (+0x38
// target) is present, THEN calls the faithful Startup. If Model is absent it
// logs and bails rather than letting Startup's unguarded createComponent+0x28
// deref hard-crash — so a wrong descriptionPtr source is diagnosed, not fatal.
// The harness hands the tool over as `void*` (src/diag/tool_harness.cpp, block
// STANDALONE): these three entries keep that C signature (B4 FORWARD).
// ============================================================
extern "C" int PlacementTool_HarnessStartupProbe(PlacementTool* self) {
    LogStartupTypeIds();  // logs the 4 type-ids

    const ComponentDescription* entity = self->descriptionPtr;  // +0x20
    if (!entity) {
        fprintf(stderr, "[Placement] PROBE: descriptionPtr(+0x20) is NULL\n");
        fflush(stderr);
        return 0;
    }

    int count = entity->_subDescCount;
    Description** comps = entity->_subDescs;
    fprintf(stderr, "[Placement] PROBE: descEntity=%p compArray=%p count=%d\n",
            static_cast<const void*>(entity), static_cast<void*>(comps), count);

    uint32_t tCreate = CreatePlacedDescription::TypeId();
    uint32_t tMap    = MapDescription::TypeId();
    uint32_t tModel  = ModelDescription::TypeId();
    uint32_t tRadius = RadiusDescription::TypeId();

    bool haveCreate = false, haveMap = false, haveModel = false, haveRadius = false;
    if (comps && count > 0 && count < 256) {
        for (int i = 0; i < count; i++) {
            Description* c = comps[i];
            if (!c) continue;
            uint32_t id = c->Type();
            if (id == tCreate) haveCreate = true;
            if (id == tMap)    haveMap = true;
            if (id == tModel)  haveModel = true;
            if (id == tRadius) haveRadius = true;
            fprintf(stderr, "[Placement] PROBE:   comp[%d]=%p typeId=%u\n", i,
                    static_cast<void*>(c), id);
        }
    }
    fprintf(stderr, "[Placement] PROBE: present Create=%d Map=%d Model=%d Radius=%d\n",
            haveCreate, haveMap, haveModel, haveRadius);
    fflush(stderr);

    if (!haveModel) {
        fprintf(stderr, "[Placement] PROBE: ModelDescription ABSENT — skipping Startup "
                        "(would crash at +0x38 deref). descriptionPtr is not the right "
                        "entity for this template.\n");
        fflush(stderr);
        return 0;
    }

    fprintf(stderr, "[Placement] PROBE: Model present — calling faithful Startup...\n");
    fflush(stderr);
    self->PlacementTool::Startup();
    fprintf(stderr, "[Placement] PROBE: Startup returned (meshInstance=%p variant=%d)\n",
            static_cast<void*>(self->meshInstance), self->meshVariant);
    fflush(stderr);
    return 1;
}

// ============================================================
// Standalone tool-harness CREATE driver (GUI_PLAN Keystone F, Phase 2).
// ============================================================
// Drives the faithful CREATE path (PlacementTool::CreateAt, 0x1400fd650) for a
// HARNESS-CHOSEN centre cell, bypassing GetPlacement's cursor pick (which has no
// valid cursor at the settle frame). Mirrors GetPlacement's footprint-rectangle +
// world-centre math with orientation fixed at 0 — so it never derefs rotationIBP
// (+0x48), and yaw = 0.
//
// CreateAt reads this+0x10 (the owner) -> +0x80 as the "existing placement
// constraint" flag; the real path has the PlayerToolManager there. Here the tool
// gets a harness-owned manager whose constraint flag is clear (a fresh,
// unconstrained placement).
//
// Requires PlacementTool::Startup to have run first — the probe above does that.
//
// The per-cell footprint validity (0x14009d580 → 0x14009f5d0) rejects any cell that
// is occupied, wrong terrain type, or too steep, so the driver SCANS outward in
// expanding Chebyshev rings and places at the first buildable cell. CreateAt is
// side-effect-free when validity fails, so repeated attempts are safe.

// Compute the footprint rectangle + world-centre position for a chosen centre
// cell, mirroring GetPlacement exactly (orient 0 → no width/height swap, yaw 0).
// Returns false if the footprint falls off the map.
static bool HarnessCellPlacement(PlacementTool* self, int centerCell,
                                 int* outStartCol, int* outStartRow,
                                 int* outEndCol, int* outEndRow, Math::Vector3* pos) {
    const CreatePlacedDescription* descComp = self->descComponent;  // +0x28
    if (!descComp) return false;

    // Footprint dims live in the CreatePlacedDescription (+0x44 width, +0x48 height).
    uint32_t dimX = static_cast<uint32_t>(descComp->_i44);
    uint32_t dimY = static_cast<uint32_t>(descComp->_i48);

    Map* grid = GameUpdateLoop::_singleton->m_game;
    int width = grid->size;
    int col = centerCell % width;
    int row = centerCell / width;

    // Centre the footprint over (col,row) — mirrors GetPlacement exactly.
    int startCol, endCol, startRow, endRow, dimYHalf;
    if ((dimX & 1) == 0) {
        startCol = (col - ((int)dimX >> 1)) + 1;
        endCol   = ((int)dimX >> 1) + col;
    } else {
        int h = (int)(dimX - 1) >> 1;
        startCol = col - h;
        endCol   = h + col;
    }
    if ((dimY & 1) == 0) {
        dimYHalf = (int)dimY >> 1;
        startRow = (row - dimYHalf) + 1;
    } else {
        dimYHalf = (int)(dimY - 1) >> 1;
        startRow = row - dimYHalf;
    }
    endRow = dimYHalf + row;

    // Footprint fully on map, startCol >= 1 (mirrors GetPlacement's own gate).
    if (startCol < 0 || width <= startCol || startRow < 0 || width <= startRow ||
        startCol < 1 || width <= endCol || endRow < 0 || width <= endRow) {
        return false;
    }

    // World-centre position = average of the two diagonal corner cells' world
    // positions via GridToWorld (0x14009f3e0). 0.5.
    const float halfCell = 0.5f;
    *pos = Math::Vector3(0.0f, 0.0f, 0.0f);
    {
        int gw = grid->size;
        float centerOff = (float)(gw >> 1) - halfCell;
        int idxA = startRow * gw + startCol;
        float inA[3] = { (float)(idxA % gw) - centerOff, (float)(idxA / gw) - centerOff, 0.0f };
        float worldA[3] = {0.0f, 0.0f, 0.0f};
        grid->GetTerrainPos(worldA, inA, '\0', nullptr);

        int idxB = endRow * gw + endCol;
        float inB[3] = { (float)(idxB % gw) - centerOff, (float)(idxB / gw) - centerOff, 0.0f };
        float worldB[3] = {0.0f, 0.0f, 0.0f};
        grid->GetTerrainPos(worldB, inB, '\0', nullptr);

        pos->x = (worldB[0] + worldA[0]) * halfCell;
        pos->y = (worldB[1] + worldA[1]) * halfCell;
    }

    *outStartCol = startCol; *outStartRow = startRow;
    *outEndCol   = endCol;   *outEndRow   = endRow;
    return true;
}

static int HarnessTryPlaceAtCell(PlacementTool* self, int centerCell) {
    int startCol, startRow, endCol, endRow;
    Math::Vector3 pos(0.0f, 0.0f, 0.0f);
    if (!HarnessCellPlacement(self, centerCell, &startCol, &startRow, &endCol, &endRow, &pos))
        return 0;

    uint64_t placed = self->CreateAt(startCol, startRow, endCol, endRow, &pos, 0.0f /*yaw*/);
    return (int)(placed & 0xff);
}

extern "C" int PlacementTool_HarnessPlaceAtCell(PlacementTool* self, int centerCell) {
    if (!self->descComponent) {
        fprintf(stderr, "[Placement] PLACE: descComponent(+0x28) NULL — run Startup first\n");
        fflush(stderr);
        return 0;
    }

    Map* grid = GameUpdateLoop::_singleton->m_game;
    int width = grid->size;
    int c0 = centerCell % width;
    int r0 = centerCell / width;
    const CreatePlacedDescription* dc = self->descComponent;
    fprintf(stderr, "[Placement] PLACE: scanning from (col=%d row=%d) foot=%ux%u for a buildable cell...\n",
            c0, r0, static_cast<uint32_t>(dc->_i44), static_cast<uint32_t>(dc->_i48));
    fflush(stderr);

    // A harness-owned manager whose +0x80 constraint flag is clear (the ctor
    // 0x1401025c0 zeroes it), so CreateAt skips the constraint block. Static so
    // it outlives this call; built on first use (the game is up by then). Never
    // destroyed (as before: its cameraViews Array is not freed at process exit).
    alignas(PlayerToolManager) static uint8_t s_harnessOwnerStorage[sizeof(PlayerToolManager)];
    static PlayerToolManager* const s_harnessOwner =
        ::new (static_cast<void*>(s_harnessOwnerStorage)) PlayerToolManager();
    self->dialogPtr = s_harnessOwner;

    // Expanding Chebyshev-ring scan around the centre; place at the first hit.
    const int kMaxRadius = 48;
    int attempts = 0;
    for (int rad = 0; rad <= kMaxRadius; rad++) {
        for (int dr = -rad; dr <= rad; dr++) {
            for (int dcx = -rad; dcx <= rad; dcx++) {
                // Only the perimeter of this ring (interior handled by smaller rad).
                if (rad != 0 && dr > -rad && dr < rad && dcx > -rad && dcx < rad) continue;
                int rr = r0 + dr, cc = c0 + dcx;
                if (rr < 1 || rr >= width - 1 || cc < 1 || cc >= width - 1) continue;
                attempts++;
                int cell = rr * width + cc;
                if (HarnessTryPlaceAtCell(self, cell)) {
                    fprintf(stderr, "[Placement] PLACE: SUCCESS at cell %d (col=%d row=%d), "
                            "ring=%d after %d attempts — building entity materialized\n",
                            cell, cc, rr, rad, attempts);
                    fflush(stderr);
                    DragTool_PlayPlaceSound_1000f0(self);  // finalize (place sound)
                    return 1;
                }
            }
        }
    }
    DragTool_PlayErrorSound_100140(self);  // fail (error sound)
    fprintf(stderr, "[Placement] PLACE: FAILED — no buildable cell within radius %d (%d attempts)\n",
            kMaxRadius, attempts);
    fflush(stderr);
    return 0;
}

// GUI_PLAN Keystone F, Phase 3B — cursor-free GHOST PREVIEW probe.
// Update's preview normally follows the OS cursor via GetPlacement, which the
// headless harness has none of. This drives the exact same ghost pipeline
// (Scene::AddObject → Transform::FromYawPitchOffset → Geometry::SetTransform →
// MeshInstance::UpdateHierarchy) for a fixed centre cell, leaving the preview mesh
// standing in the live render scene. Does NOT create a permanent entity.
extern "C" int PlacementTool_HarnessPreviewAtCell(PlacementTool* self, int centerCell) {
    if (!self->descComponent || !self->meshInstance) {
        fprintf(stderr, "[Preview] descComponent(+0x28)=%p meshInstance(+0x68)=%p — run Startup first\n",
                static_cast<const void*>(self->descComponent), static_cast<void*>(self->meshInstance));
        fflush(stderr);
        return 0;
    }


    int startCol, startRow, endCol, endRow;
    Math::Vector3 pos(0.0f, 0.0f, 0.0f);
    if (!HarnessCellPlacement(self, centerCell, &startCol, &startRow, &endCol, &endRow, &pos)) {
        fprintf(stderr, "[Preview] cell %d off-map — no preview\n", centerCell);
        fflush(stderr);
        return 0;
    }
    fprintf(stderr, "[Preview] cell=%d rect=(%d,%d)-(%d,%d) pos=(%.2f,%.2f,%.2f)\n",
            centerCell, startCol, startRow, endCol, endRow, pos.x, pos.y, pos.z);

    // 1) Submit the preview mesh to the live scene (gameWorld+0x60 — same scene the
    //    working ModelComponent render path uses).
    Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
    if (self->sceneObjectIdx < 0 && scene && self->meshInstance) {
        self->sceneObjectIdx = (int)scene->AddObject(self->meshInstance);
    }

    // 2) Build the placement transform from yaw + cursor world offset.
    float yawVal = 0.0f, zeroPitch = 0.0f;
    Math::Transform localTransform =
        Math::Transform::FromYawPitchOffset(yawVal, zeroPitch, pos);

    // Log the rotation rows and the translation so a degenerate (all-zero)
    // transform is obvious.
    const Math::Transform& xf = localTransform;
    fprintf(stderr, "[Preview] transform: rot=(%.2f %.2f %.2f %.2f) pos=(%.2f %.2f %.2f)\n",
            xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w,
            xf.translation.x, xf.translation.y, xf.translation.z);

    if (self->meshInstance) {
        self->meshInstance->SetTransform(localTransform);
        self->meshInstance->UpdateHierarchy();
    }

    bool degenerate = (xf.rotation.x == 0.0f && xf.rotation.y == 0.0f &&
                       xf.rotation.z == 0.0f && xf.rotation.w == 0.0f);
    fprintf(stderr, "[Preview] sceneObjectIdx=%d transform=%s — ghost mesh %s in the live scene\n",
            (int)self->sceneObjectIdx, degenerate ? "DEGENERATE(all-zero!)" : "valid",
            (self->sceneObjectIdx >= 0 && !degenerate) ? "SUBMITTED" : "NOT submitted");
    fflush(stderr);
    return (self->sceneObjectIdx >= 0 && !degenerate) ? 1 : 0;
}
