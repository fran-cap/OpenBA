// src/game/Player/tool_base_place.cpp
// The shared PlacementTool sub-helper PlacementTool::FlipVariant, a faithful 1:1
// reimplementation called by name from PlacementTool::OnKey (placementtool.cpp)
// and PlacementTool::CreateAt (tool_base_tile.cpp).

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen.  ENDGAME B.3.5 batch I2.
#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "object/External.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include "Player/exe_bodies.h"
#include "Player/PlacementTool.h"
#include "Component/ModelDescription.h"

// ============================================================
// PlacementTool::FlipVariant — mirror/flip + advance mesh variant (0x1400fd960)
// ============================================================
// Increments the mesh-variant index (this+0x64), wrapping to 0 when it reaches
// the variant count (ModelDescription +0x28, the description at this+0x38).
// If a scene object is currently displayed (this+0x60 >= 0), it removes it,
// re-selects the mesh for the new variant (last mesh of the variant's group),
// updates the mesh hierarchy, and re-adds it to the scene (storing the new id at
// this+0x60).
//
// Decompiled + asm-verified from 0x1400fd960. The original reaches the four
// graphics ops through IAT dispatch slots:
//   [0x140151360] Scene::RemoveObject     [0x140151498] MeshInstance::SetMesh
//   [0x140152888] MeshInstance::UpdateHierarchy   [0x140151370] Scene::AddObject
//
// Mesh selection (mirrors the disasm at 0x1400fd99b..0x1400fd9b9):
//   group = ModelDescription._meshGroups[variant]   (stride 0x18)
//   mesh  = group.meshes[group.meshCount - 1]       // last entry
// B4 PLAYER: a PlacementTool member (it reads only this tool's fields).
void PlacementTool::FlipVariant() {
    // Advance + wrap the variant index against the model description's count.
    int variant = meshVariant + 1;
    const ModelDescription* model = createComponent;
    int count = model->_meshGroupCount;
    if (variant >= count) variant = 0;
    meshVariant = variant;

    // Only update the live scene object if one is currently shown.
    int sceneObj = sceneObjectIdx;
    if (sceneObj < 0) return;

    Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;

    if (scene) {
        scene->RemoveObject(static_cast<uint32_t>(sceneObj));
    }

    // Re-select mesh for the new variant (last entry of its group).
    const ModelMeshGroup* entry = &model->_meshGroups[variant];
    int subCount = entry->meshCount;
    Graphics::GraphicsMesh* mesh = entry->meshes[subCount - 1];

    Graphics::MeshInstance* instance = meshInstance;
    if (instance) {
        instance->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
        instance->UpdateHierarchy();
    }
    if (scene && instance) {
        uint32_t newId = scene->AddObject(instance);
        sceneObjectIdx = static_cast<int32_t>(newId);
    }
}