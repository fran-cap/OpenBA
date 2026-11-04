#pragma once
// ============================================================================
// src/game/Component/ModelDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ModelDescription", parent "Description", ti 0x1401c7a88
//   create 0x140035930: MemAlloc(0x40, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400359a0.
//   table 0x140163c20, 10 slots:
//     [0] 0x140035a70 scalar deleting dtor   [1] 0x140035920 TypeName   [2] 0x140035910 Type
//     [3] 0x140124ef0 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140124dc0 Serialize (`MOV AL,1`)
//     [6] 0x140035a00 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140035a60 GetComponentType: ModelComponent's TypeId
//     [9] 0x1400359a0 Construct (folded with createInPlace)
//   sizeof 0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

namespace Graphics { class GraphicsMesh; class AnimationGroup; }
namespace Object { class External; }

// One element of ModelDescription's mesh-group array (stride 0x18), from Serialize
// 0x140124dc0: an ExternalPtr<AnimationGroup> at +0x00 (0x140035b40, type cell import
// 0x140152980; B4 S4 RESOLVE: AnimalComponent's 0x1401250f0 hands this qword to
// ?PlayAnimation@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@VAnimationGroup@... by
// value), then an Array of ExternalPtr<GraphicsMesh> (0x14001bcc0, type cell
// import 0x140151f20 = GraphicsMesh) whose element is the mesh a ModelComponent's
// meshIndex selects (0x140107400, 0x140124ac0).
struct ModelMeshGroup {
    Graphics::AnimationGroup* field_0x00;  // +0x00 ExternalPtr<AnimationGroup> (the group's animations)
    Graphics::GraphicsMesh** meshes;       // +0x08 Array<ExternalPtr<GraphicsMesh>> data
    int32_t                  meshCount;    // +0x10
    int32_t                  meshCapacity; // +0x14
};
static_assert(sizeof(ModelMeshGroup) == 0x18, "ModelDescription mesh group stride 0x18");
static_assert(offsetof(ModelMeshGroup, field_0x00) == 0x0, "ModelMeshGroup::field_0x00");
static_assert(offsetof(ModelMeshGroup, meshes) == 0x08, "");
static_assert(offsetof(ModelMeshGroup, meshCount) == 0x10, "");
static_assert(offsetof(ModelMeshGroup, meshCapacity) == 0x14, "");

class ModelDescription final : public Description {
    DECLARE_EXE_TYPE(ModelDescription)

public:
    ModelDescription();

    ~ModelDescription() override;                                          // [0] 0x140035a70
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140124ef0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140124dc0
    Object::External* Reconstruct() override;                              // [6] 0x140035a00
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140035a60
    virtual Object::External* Construct();                                 // [9] 0x1400359a0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ModelMeshGroup* _meshGroups;                // +0x20 Array<ModelMeshGroup> data
    int32_t _meshGroupCount;                    // +0x28
    int32_t _meshGroupCap;                      // +0x2c
    int32_t _defaultMeshGroup;                              // +0x30
    int32_t _defaultMeshIndex;                              // +0x34
    bool _randomMeshGroup;                           // +0x38
    uint8_t _pad39[7];                          // +0x39 never written by the ctor
};

static_assert(sizeof(ModelDescription) == 0x40, "ModelDescription is 0x40 (create 0x140035930 MemAlloc(0x40))");
static_assert(offsetof(ModelDescription, _pad39) == 0x39, "ModelDescription::_pad39");
static_assert(alignof(ModelDescription) == 8, "ModelDescription alignment");
static_assert(offsetof(ModelDescription, _meshGroups) == 0x20, "ModelDescription::_meshGroups @ +0x20");
static_assert(offsetof(ModelDescription, _meshGroupCount) == 0x28, "ModelDescription::_meshGroupCount @ +0x28");
static_assert(offsetof(ModelDescription, _meshGroupCap) == 0x2c, "ModelDescription::_meshGroupCap @ +0x2c");
static_assert(offsetof(ModelDescription, _defaultMeshGroup) == 0x30, "ModelDescription::_defaultMeshGroup @ +0x30");
static_assert(offsetof(ModelDescription, _defaultMeshIndex) == 0x34, "ModelDescription::_defaultMeshIndex @ +0x34");
static_assert(offsetof(ModelDescription, _randomMeshGroup) == 0x38, "ModelDescription::_randomMeshGroup @ +0x38");

// The mesh a ModelComponent's (meshGroup, meshIndex) names in a description's mesh-group
// array: `groups[meshGroup].meshes[meshIndex]`, or null when there is none. Port-written (no
// retail address): ModelComponent::Persist (modelcomponent.cpp) restores the mesh with it, and
// tests/unit/review_object_index_bounds_test.cpp drives it directly.
inline Graphics::GraphicsMesh* ModelDescriptionMesh(const ModelMeshGroup* groups, int32_t groupCount,
                                                    int16_t meshGroup, int16_t meshIndex) {
    if (meshIndex < 0 || meshGroup < 0)
        return nullptr;
    if (!groups)
        return nullptr;   // description carries no meshes
    // NOT RETAIL (review F-025): the two int16 fields come from the save; one at or past the
    // description's group count or the group's mesh count reads past the array. A save that
    // names no such mesh restores none, as for a negative field. Valid saves are in range.
    if (meshGroup >= groupCount)
        return nullptr;
    if (meshIndex >= groups[meshGroup].meshCount)
        return nullptr;
    Graphics::GraphicsMesh** group = groups[meshGroup].meshes;
    if (!group)
        return nullptr;
    return group[meshIndex];
}
