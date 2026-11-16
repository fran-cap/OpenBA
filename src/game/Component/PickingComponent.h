#pragma once
// ============================================================================
// src/game/Component/PickingComponent.h — ENDGAME B.3.8 block VIS
//
// `PickingComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "PickingComponent", parent "Component", ti 0x1401c7c10
//   create 0x140036da0: MemAlloc(0x38, 8) + ctor 0x140128c80
//   ctor   0x140128c80: Component ctor, stamp 0x140164040, +0x28 = -1,
//                       +0x30 = 0, flags |= 0xc3 (0xc3)
//   dtor   0x140128c40: stamp 0x140164040, stamp 0x140162920, ~Internal (no own work)
//   table  0x140164040, 16 slots:
//     [0]  0x140036fa0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x140128bb0 Persist               [4] 0x140128c30 Startup (9 B, userData = owner)
//     [6]  0x140128b90 OnAdded (`JMP 0x140128f50`, register the pick mesh)
//     [7]  0x140128b50 OnRemove (Ghidra "OnCreate", remove the pick mesh)
//     [11] 0x140128ba0 Slot11 (`JMP 0x140128f50`, the post-load re-register)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class ComponentEntity;   // Entity/ComponentEntity.h

class PickingComponent final : public Component {
    DECLARE_EXE_TYPE(PickingComponent)

public:
    PickingComponent();                                           // 0x140128c80

    // 0x140128c40
    ~PickingComponent() override = default;                       // [0]  0x140036fa0
    void Persist(IO::Stream& stream) override;                          // [3]  0x140128bb0
    void Startup() override;                                      // [4]  0x140128c30
    void OnAdded() override;                                      // [6]  0x140128b90
    void OnRemove() override;                                     // [7]  0x140128b50
    void Slot11() override;                                       // [11] 0x140128ba0 = RefreshMesh

    // 0x140128f50 (slots 6 / 11 JMP here): drop the mesh, then add the model's pick mesh.
    void RefreshMesh();

    // PUBLIC while the ported bodies read them (B38 decision 1).
    int32_t  pickingId;    // +0x28 Picking::Scene mesh id (ctor -1 = none)
    uint8_t  _pad2C[4];    // +0x2C
    Object::InternalPtr<ComponentEntity> userData; // +0x30 the entity a pick answers (Startup: the owner)
};

static_assert(sizeof(PickingComponent) == 0x38, "PickingComponent is 0x38 (create 0x140036da0)");
static_assert(offsetof(PickingComponent, _pad2C) == 0x2C, "PickingComponent::_pad2C");
static_assert(alignof(PickingComponent) == 8, "PickingComponent alignment");
static_assert(offsetof(PickingComponent, pickingId) == 0x28, "");
static_assert(offsetof(PickingComponent, userData) == 0x30, "");
