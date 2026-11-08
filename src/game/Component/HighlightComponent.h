#pragma once
// ============================================================================
// src/game/Component/HighlightComponent.h — ENDGAME B.3.8 block VIS
//
// `HighlightComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HighlightComponent", parent "Component", ti 0x1401c4410
//   create 0x140006e40: MemAlloc(0x128, 8) + ctor 0x14004c930
//   ctor   0x14004c930: Component ctor, stamp 0x140159990,
//                       Graphics::EdgeMeshInstance ctor on +0x28, +0xf8 = 0,
//                       Math::Transform ctor on +0x100, +0x11c = 0 (byte),
//                       +0x120 = -1; no flag OR (flags stay 0x80)
//   dtor   0x14004c8c0: deleting-destroy the object at +0xf8, +0xf8 = 0,
//                       ~EdgeMeshInstance(+0x28), restamp, ~Internal
//   table  0x140159990, 16 slots:
//     [0]  0x140007040 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14004c880 Persist               [8] 0x14004c850 Tick (23 B)
//     [12] 0x14004c870 OnEvent (10 B) -> Repaint on event 0
//     every other slot = Component's
//
// TREE DIVERGENCES kept (behaviour of the tree's table, not retail's):
//   * flags are 0x82, not 0x80: bit 1 enrols the component in the
//     MarkForRemoval (0x1400c2ea0) +0x38 broadcast, and
//   * slot 7 (Component's `RET` in retail) removes a live outline from the
//     Scene at mark time — required because the standalone EditTool notices a
//     dead selection only after the entity is freed (live crash 2026-07-20).
// The embedded EdgeMeshInstance is a real member since B.4 wave Y (behaviour:
// commit): the ctor constructs it as 0x14004c930 does, the dtor deletes +0xf8
// and member destruction runs ~EdgeMeshInstance as 0x14004c8c0 does.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "graphics/Mesh.h"   // Graphics::EdgeMeshInstance (embedded, +0x28), Graphics::EdgeMesh
#include "math/Math.h"   // Math::Transform (+0x100)

class HighlightComponent final : public Component {
    DECLARE_EXE_TYPE(HighlightComponent)

public:
    HighlightComponent();                                         // 0x14004c930

    // 0x14004c8c0: the body deletes +0xf8 through its slot 0 and zeroes it;
    // ~EdgeMeshInstance(+0x28) is then the compiler's member destruction.
    ~HighlightComponent() override;                               // [0]  0x140007040 (body 0x14004c8c0)
    void Persist(IO::Stream& stream) override;                          // [3]  0x14004c880
    void OnRemove() override;                                     // [7]  = Component's in retail; TREE DIVERGENCE
    void Tick(const System::Time* time) override;                               // [8]  0x14004c850 outline Update while live
    void OnEvent(int eventId) override;                           // [12] 0x14004c870 event 0 -> Repaint
    void Repaint();                                               // 0x14004c4a0 rebuild the outline EdgeMeshInstance and re-add it to the scene (body: Player/tool_base_render.cpp)

    // PUBLIC while the ported bodies read them (B38 decision 1).
    Graphics::EdgeMeshInstance edgeMeshInstance; // +0x28 (ctor 0x14004c930: CALL [0x140151358] ??0EdgeMeshInstance; dtor 0x14004c8c0: CALL [0x1401513b0] ??1)
    Graphics::EdgeMesh* outlineMesh;     // +0xF8 the built outline mesh (ctor 0; Impl_b930 0x14004b930 MemAlloc(0x180) + EdgeMesh ctor; dtor 0x14004c8c0 deletes it)
    Math::Transform transform;       // +0x100 (ctor 0x14004c930: ??0Transform at +0x100 = identity; trivially destroyed, 0x14004c8c0 never touches it)
    uint8_t  highlightFlag;          // +0x11C (ctor 0)
    uint8_t  _pad11D[3];             // +0x11D
    int32_t  highlightId;            // +0x120 Scene object id of the outline (ctor -1)
    uint8_t  _pad124[4];             // +0x124
};

static_assert(sizeof(HighlightComponent) == 0x128, "HighlightComponent is 0x128 (create 0x140006e40)");
static_assert(offsetof(HighlightComponent, _pad11D) == 0x11D, "HighlightComponent::_pad11D");
static_assert(offsetof(HighlightComponent, _pad124) == 0x124, "HighlightComponent::_pad124");
static_assert(alignof(HighlightComponent) == 8, "HighlightComponent alignment");
static_assert(offsetof(HighlightComponent, edgeMeshInstance) == 0x28, "");
static_assert(offsetof(HighlightComponent, outlineMesh) == 0xF8, "");
static_assert(offsetof(HighlightComponent, transform) == 0x100, "");
static_assert(offsetof(HighlightComponent, highlightFlag) == 0x11C, "");
static_assert(offsetof(HighlightComponent, highlightId) == 0x120, "");
