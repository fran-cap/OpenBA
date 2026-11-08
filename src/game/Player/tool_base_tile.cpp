// src/game/Player/tool_base_tile.cpp
// Player wave (round 2) — "tile" sub-batch. Shared PlacementTool tile/footprint
// helpers resolved via ResolveAddr(Module::Exe, 0xADDR) from src/game/Player/*.cpp.
//
// These resolve to 0 (silently skipped) unless registered. This file provides
// faithful 1:1 reimplementations and a single registration point
// (RegisterPlayerTileBatch in standalone/reg_player_tile.cpp) so the call sites
// fire real logic.
//
// Each function is a 1:1 port of the decompiled+disassembled exe helper; do not
// "improve" behavior.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Mesh.h"
#include <new>
#include "object/External.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Action/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include "Object/factory_registration.h"
#include "Component/ModelComponent.h"   // ModelComponent::TypeId (scoped find)
#include "Component/ModelDescription.h"
#include <cstdio>                            // BANISHED_TOOL_DIAG place-model trace
#include <cstdlib>                           // std::getenv
#include <cstring>                           // std::strcmp (BANISHED_CLEAR_PLACE_OVER)
#include <climits>                           // INT_MIN (reject-trace dedupe)
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Player/exe_bodies.h"
#include "Player/PlacementTool.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/MapDescription.h"
#include "Component/MapComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "ext/registry.h"                      // EXT SEAM (NOT RETAIL): Ext::Ext().touch_pointer
#include "ext/touch_pointer/touch_pointer.h"   // EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_POINTER.md): KeepsModel

// ============================================================
// 0x140003870 — ModelComponent::SetTransformFromYaw
// ============================================================
// Builds a Math::Transform from (yaw=param_3, pitch=0, offset=param_2) into
// this+0x148, stores yaw at this+0x168, then Graphics::Geometry::SetTransform on
// the MeshInstance at this+0x28.
//
// Disasm-verified ABI (RCX=self, RDX=param_2 (offset Vector3*), XMM2=param_3 (yaw float)):
//   MOV R9,RDX        ; R9 = param_2 -> the OFFSET (4th) arg of FromYawPitchOffset
//   yaw local  = param_3 (XMM6)
//   pitch local = 0.0
//   CALL FromYawPitchOffset(out=local_38, &yaw, &pitch, offset=param_2)
//   copy 7 dwords (0x1c bytes, the Transform) result -> self+0x148
//   MOVSS self+0x168 = yaw
//   JMP Geometry::SetTransform(self+0x28, self+0x148)
//
// NOTE: the Ghidra decompile dropped R9/param_2 and showed a zeroed local as the
// offset; the disasm passes param_2 through as the offset. Ported per the disasm.
// B4 PLAYER: a ModelComponent member (every field it touches is the component's).
void ModelComponent::SetTransformFromYaw(const Math::Vector3* offset, float yawDegrees) {
    transform = Math::Transform::FromYawPitchOffset(yawDegrees, 0.0f, *offset);
    yaw = yawDegrees;
    meshInstance.SetTransform(transform);
}

// B4 FORWARD: 4 outside callers (Component/citizencomponent_lifecycle.cpp,
// destroycomponent_states.cpp, herdcomponent.cpp, immigrationcomponent_states.cpp).

// ============================================================
// 0x14009d580 — PlacementTool footprint tile-validity over a rotated rectangle
// ============================================================
// Iterates a (param_4 x param_5) rectangle anchored at (param_2,param_3). For each
// cell it computes, per the rotation code in param_14 (0x5a=90, 0xb4=180,
// 0x10e=270, else 0=identity), an index into two strings:
//   - footprint (param_13, MapDescription +0x28): '.' at the index sets the flag
//   - placeMap  (param_8,  CreatePlacedDescription +0x28): a char indexing the
//                placement description's per-cell code array (+0x30) to override
//                param_6 (the placement code), when that array is not empty.
// then calls 0x14009f5d0 per cell; returns 0 on the first false, else 1.
//
// Only PlacementTool calls it (0x1400fd71d, 0x1400fe09e). Retail's param_7 is
// the address of the description's embedded Array (+0x30); CreatePlacedDescription
// (block COMP-c) declares that Array as its raw triple, so the description itself
// is passed and the triple read by name (B4 PLAYER).
//
// 1:1 port of the Ghidra decompile (disasm-verified faithful). The index math and
// the rotation switch are reproduced exactly.
uint64_t Map::FootprintValidity(int param_2, int param_3, int param_4, int param_5,
        unsigned int param_6, const CreatePlacedDescription* param_7,
        const System::String* param_8,
        float param_9, float param_10, float param_11, char param_12,
        const System::String* param_13, int param_14, const ComponentDescription* param_15) {
    int iVar1 = param_3 + param_5;        // row end
    int iVar4 = iVar1;
    if (param_3 < iVar1) {
        iVar4 = param_2 + param_4;        // col end
        int iVar14 = param_3;             // current row
        int iVar3 = param_5;
        do {
            iVar3 = iVar3 + -1;
            if (param_2 < iVar4) {
                int iVar10 = param_2;     // current col
                int iVar11 = param_4;
                do {
                    iVar11 = iVar11 + -1;
                    bool bVar13 = false;
                    int iVar9 = iVar14 - param_3;
                    int iVar5 = iVar11;
                    int iVar7, iVar8, iVar6, iVar12;
                    if ((param_14 == 0x5a) ||
                        ((iVar7 = param_5, iVar8 = iVar11, iVar6 = iVar3, iVar12 = param_4,
                          param_14 != 0xb4 &&
                          (iVar8 = iVar10 - param_2, iVar9 = iVar3, iVar6 = iVar14 - param_3,
                           iVar5 = iVar10 - param_2, param_14 == 0x10e)))) {
                        iVar7 = param_4;
                        iVar8 = iVar9;
                        iVar6 = iVar5;
                        iVar12 = param_5;
                    }
                    iVar8 = ((iVar7 - iVar6) + -1) * (iVar12 + 1) + iVar8;
                    const char* footprint = param_13->_data;
                    if ((footprint != nullptr) && (param_13->GetLength() != 0)) {
                        bVar13 = footprint[iVar8] == '.';
                    }
                    const char* placeMap = param_8->_data;
                    if (((0 < param_7->_placeArray._size) && (placeMap != nullptr)) &&
                        (param_8->GetLength() != 0)) {
                        // data - 0xc0 + c*4: the code array indexed by the char - '0'.
                        param_6 = param_7->_placeArray._data[static_cast<long long>(placeMap[iVar8]) - 0x30];
                    }
                    bool ok = TestPlacement(iVar10, iVar14, param_6, param_9,
                                                       param_10, param_11, param_12,
                                                       bVar13 ? 1 : 0, param_15);
                    if (!ok) {
                        return 0;
                    }
                    iVar10 = iVar10 + 1;
                } while (iVar10 < iVar4);
            }
            iVar14 = iVar14 + 1;
        } while (iVar14 < iVar1);
    }
    return 1;
}

// ============================================================
// 0x1400fd650 — PlacementTool::CreateAt (validate + create the placed entity)
// ============================================================
// Validates the rectangle via ToolTile_FootprintValidity_d580; on success spawns a
// placed ComponentEntity (0x1400c3720), writes its MapComponent footprint rect
// (+0x28..+0x2E), orientation (+0x30 normalised into [0,0x168)), sets its
// ModelComponent transform (0x140003870), fixes up its model, registers it in the
// world entity list, and flips the tool's mesh variant (0x1400fd960).
//
// 1:1 port of the Ghidra decompile. B4 PLAYER: a PlacementTool member (it reads
// only this tool's fields and the world).

// Scoped ModelComponent finder (the tree's): the exact-id walk without
// IsDerivedFrom, kept beside FindComponent<ModelComponent>() (0x140003f60) as the
// fallback the port has always had.
static ModelComponent* ToolTile_FindModelComponentScoped(const ComponentEntity* entity) {
    const uint32_t s_tidModel = ModelComponent::TypeId();
    if (!entity || !s_tidModel) return nullptr;
    int n = entity->_components._size;
    Object::InternalPtr<Component>* arr = entity->_components._data;
    for (int k = 0; k < n && arr; ++k) {
        Component* c = arr[k].Get();
        if (!c) continue;
        if (c->Type() == s_tidModel) return static_cast<ModelComponent*>(c);
    }
    return nullptr;
}

// Standalone-only: show the placed building's model frame. CreateAt faithfully
// leaves the placed building's ModelComponent at "no finished model" (+0x166 = -1)
// — the retail construction-site start state, which retail advances when the
// BuildComponent finishes. `foundation`:
//   false -> LAST mesh of the group (finished building)
//   true  -> FIRST mesh (the construction-start foundation): the BuildComponent
//            state machine then drives the model up through the build stages.
// Keeps the mesh group CreateAt chose (+0x164), clamped to a valid group.
static void ToolTile_SetPlacedModel(ModelComponent* model, bool foundation) {
    if (!model) return;
    const Description* description = model->_description;   // Component::_description (a ModelComponent's is its ModelDescription)
    const ModelDescription* modelDesc = static_cast<const ModelDescription*>(description);
    if (!modelDesc) return;
    const ModelMeshGroup* groupArray = modelDesc->_meshGroups;
    uint32_t groupCount = static_cast<uint32_t>(modelDesc->_meshGroupCount);
    if (!groupArray || groupCount == 0) return;

    int variantIdx = model->meshGroup;
    if (variantIdx < 0 || static_cast<uint32_t>(variantIdx) >= groupCount) variantIdx = 0;
    const ModelMeshGroup* group = &groupArray[variantIdx];
    int subCount = group->meshCount;
    // foundation -> first mesh (0); else last mesh (complete). A single-mesh group
    // has no construction stages, so 0 == complete either way (harmless).
    int modelVariant = foundation ? 0 : ((subCount > 0) ? subCount - 1 : 0);

    model->meshGroup = static_cast<int16_t>(variantIdx);
    model->meshIndex = static_cast<int16_t>(modelVariant);

    Graphics::GraphicsMesh* mesh = nullptr;
    Graphics::GraphicsMesh** subArr = group->meshes;
    if (subArr) mesh = subArr[modelVariant];
    if (mesh) {
        model->meshInstance.SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
        // Re-apply the placement yaw after the mesh swap: ModelComponent::Draw
        // re-applies +0x148 only when the model is not yet in the scene, and the
        // copy-constructed entity's +0x16c can be a stale scene id. +0x148 already
        // holds the yaw (SetTransformFromYaw wrote it), so push it directly.
        model->meshInstance.SetTransform(model->transform);
    }
}

// (The placement path deliberately performs no occupancy write — see the note at the tail
// of CreateAt. That belongs to MapComponent slot 12, driven by ClearArea's
// state-2 event-1 broadcast.)

uint64_t PlacementTool::CreateAt(int param_2, int param_3, int param_4, int param_5,
                                 const Math::Vector3* param_6, float param_7) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    // DAT_140172248 is 0x447a0000 = 1000.0f (read again 2026-10-09):
    // `MOVSS XMM0,[0x140172248]; MOVSS [RSP+0x48],XMM0` at 0x1400fd6e6 / 0x1400fd6ee.
    const float DAT_140172248 = 1000.0f;

    Map* piVar10 = gameWorld->m_game;
    int iVar12 = (param_4 - param_2) + 1;   // width
    int iVar13 = (param_5 - param_3) + 1;   // height
    const CreatePlacedDescription* lVar3 = descComponent;
    int iVar11 = static_cast<int>(param_7); // orientation

    uint64_t uVar7 = piVar10->FootprintValidity(param_2, param_3, iVar12, iVar13,
        lVar3->_category,
        lVar3,
        &lVar3->_name,
        0.0f, DAT_140172248, 0.0f, '\0',
        &mapComponent->_footprint,
        iVar11,
        lVar3->_created);

    if (static_cast<char>(uVar7) == '\0') {
        return uVar7 & 0xffffffffffffff00ULL;
    }

    const PlayerToolManager* lvCfg = dialogPtr;
    if (lvCfg->tutorialRestricted) {
        int iVar1 = gameWorld->m_game->size;
        long long c98 = lvCfg->tutorialStartCell;
        long long c9c = lvCfg->tutorialEndCell;
        int col0 = static_cast<int>(c98 % iVar1);
        int row0 = static_cast<int>(c98 / iVar1);
        int col1 = static_cast<int>(c9c % iVar1);
        int row1 = static_cast<int>(c9c / iVar1);
        if ((param_2 != col0) || (param_3 != row0) ||
            (col1 != param_4) || (row1 != param_5) ||
            (lvCfg->tutorialRotation != iVar11)) {
            return uVar7 & 0xffffffffffffff00ULL;
        }
        // Retail reuses piVar10's register for col0 here; it is only ever the
        // first (never read) argument of 0x1401018d0.
    }

    DragPlacementTool_ClearPathEntities_1018d0(piVar10, param_2, param_3, param_4, param_5,
                                               descComponent->_created);

    // Allocate + copy-construct the placed ComponentEntity.
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* entity = nullptr;
    if (mem != nullptr) {
        entity = ::new (mem) ComponentEntity(descriptionPtr);
    }

    // Resolve the placed entity's MapComponent (0x140004680) and write the footprint rect.
    MapComponent* mapComp = entity->FindComponent<MapComponent>();
    mapComp->tileX0 = static_cast<short>(param_2);
    mapComp->tileY0 = static_cast<short>(param_3);
    mapComp->tileX1 = static_cast<short>(iVar12);
    mapComp->tileY1 = static_cast<short>(iVar13);
    mapComp->extraField = iVar11;
    if (iVar11 < 0) {
        do { iVar11 = iVar11 + 0x168; } while (iVar11 < 0);
        mapComp->extraField = iVar11;
    }

    // Resolve the ModelComponent (0x140003f60) and set its transform.
    ModelComponent* modelComp = entity->FindComponent<ModelComponent>();
    if (modelComp == nullptr) modelComp = ToolTile_FindModelComponentScoped(entity);
    if (modelComp != nullptr) modelComp->SetTransformFromYaw(param_6, param_7);

    // Resolve again and fix up the model component.
    ModelComponent* plVar4 = entity->FindComponent<ModelComponent>();
    if (plVar4 == nullptr) plVar4 = ToolTile_FindModelComponentScoped(entity);
    if (plVar4 != nullptr) {
        int uVar2 = meshVariant;
        (void)Object::IsDerivedFromNullSafe(plVar4->_description, ModelDescription::TypeId());
        plVar4->meshGroup = static_cast<int16_t>(uVar2);
        if (-1 < plVar4->meshIndex) {
            plVar4->OnRemove();   // slot 7
        }
        plVar4->meshIndex = -1;   // retail stores the word 0xffff
        // With the live construction pipeline the placed building is left at its
        // FOUNDATION frame so the BuildComponent state machine drives it through
        // organic gather -> build -> complete.
        ToolTile_SetPlacedModel(plVar4, true);
        plVar4->_entity->SendEvent(0);   // 0x1400c2b70
    }

    // Register the new entity in the world entity list.
    Object::InternalPtr<ComponentEntity> local_48 = entity;
    gameWorld->m_entityList->RegisterEntity(&local_48,
                                   static_cast<int>(descriptionPtr->_category));

    // The pick mesh: RegisterEntity's OnAdded pass runs PickingComponent::OnAdded
    // (0x140128b90 -> 0x140128f50) for the new entity; retail calls nothing more here
    // (row 1261). For a foundation (meshIndex -1, above) it registers nothing.

    // NB there is deliberately NO map-cell occupancy write here: retail's chain is
    // MapComponent::Draw (slot 6) -> gate `MapDescription+0x30 != 0` -> 41b0 -> dc20,
    // and +0x30 is 0 for a WoodHouse. A building claims its cells LATER, when its
    // ClearArea reaches state 2 (MapComponent_Slot12VSlot, mapcomponent.cpp).

    // 0x1400fd960's body is void: every caller of this function reads only AL (= 1).
    // EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_POINTER.md section 2): ext.touch_pointer's Shift toggle keeps
    // the model for the next placement (a row of identical houses); retail always advances it here.
    if (!(Ext::Ext().touch_pointer && Ext::TouchPointer::KeepsModel())) FlipVariant();
    uint64_t uVar9 = 0;
    return (uVar9 & ~0xffULL) | 1ULL;
}
