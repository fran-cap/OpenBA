// src/game/Action/animatenaturalresource.cpp
// Clean C++ reimplementation of AnimateNaturalResource action methods.
// Source: Game/Action/AnimateNaturalResource.cpp (from profile build debug strings)
//
// AnimateNaturalResource — 0x30 bytes. Triggers and waits for animation on a natural resource.
// Inherits: Object::Internal -> Action (0x20) -> AnimateNaturalResource
// Ctor: 0x140021dc0 (factory), Vtable: 0x14015fb10
//
// Decompiled from Application-x64.exe using Ghidra.

#include "AnimateNaturalResource.h"
// The runtime type header comes FIRST: graphics/Mesh.h reaches
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden
// by <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "graphics/Mesh.h"   // ENDGAME B.3.5 batch K1
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Action/exe_bodies.h"   // NaturalResourceSetAnimation 0x140126440, the B4 FORWARD ctor door
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/ComponentEntity.h"

// ============================================================
// Helpers, called by name (B3R)
//   0x1400052c0 WorkBase_FindComponent_52c0 — get NaturalResourceComponent from entity
//   0x140126440 ActCNR_SetAnimIndex_126440  — set animation index on NaturalResourceComponent
//   0x140003f60 ActCNR_FindComponent_3f60   — get ModelComponent from entity
// ============================================================
// ============================================================
// Serialize (0x1400c5660, vtable slot 3)
// Serializes status, entity ref with type check, animIndex, waitForAnim.
// ============================================================
void AnimateNaturalResource::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity ref + extra fields (version 0)
    IO::Serializer ser(stream, 0);

    // Entity ref (+0x20) with type validation
    resourceEntity.InternalBasePtr::Serialize(ser);
    if (!resourceEntity.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(resourceEntity, ComponentEntity::TypeId())) {
            resourceEntity = nullptr;
        }
    }

    ser.Serialize(animIndex, nullptr);     // +0x28
    ser.Serialize(waitForAnim, nullptr);   // +0x2C
}

// ============================================================
// AbstractImpl (0x1400c5600, vtable slot 4)
// Gets NR component from entity at +0x20, calls SetAnimIndex with +0x28.
// Returns waitForAnim flag.
// ============================================================
int  AnimateNaturalResource::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    resourceEntity->FindComponent<NaturalResourceComponent>()->SetAnimation(   // 0x1400052c0
                                static_cast<uint32_t>(animIndex));                     // 0x140126440

    return (this->waitForAnim != 0) ? 1 : 0;
}

// ============================================================
// Execute (0x1400c55b0, vtable slot 5)
// Checks if entity's model is still animating. Returns true while animating.
// ============================================================
int  AnimateNaturalResource::Execute(ActionStack* actionStack, const System::Time* time) {
    (void)actionStack;
    (void)time;

    // B6 (14d G64): 0x1400c55b0 finds the ModelComponent (0x140003f60), CheckPtr, then
    // MeshInstance::IsAnimating on [model]+0x28 with no test.
    ModelComponent* model = resourceEntity->FindComponent<ModelComponent>();   // 0x140003f60
    return model->meshInstance.IsAnimating() ? 1 : 0;
}

// ============================================================
// GetTarget (0x1400c5560, vtable slot 9)
// Returns true if param matches entity at +0x20.
// ============================================================
bool AnimateNaturalResource::GetTarget(ComponentEntity** param) {
    return resourceEntity.Get() == *param;
}

// ============================================================
// CheckEntity (0x1400c55a0, vtable slot 8)
// 3 bytes total (XOR EAX,EAX; RET) — returns 0 (no-op).
// NOT HOOKED — too small. Standalone exe only.
// ============================================================
// int AnimateNaturalResource_CheckEntity_Impl(AnimateNaturalResource* self) {
//     return 0;
// }

// ============================================================
// class AnimateNaturalResource (AnimateNaturalResource.h) — ENDGAME B.3.8 ACT.
// The hand-built g_AnimateNaturalResource_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140021dc0 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
AnimateNaturalResource::AnimateNaturalResource()
    : resourceEntity(nullptr),
      animIndex(0),
      waitForAnim(false),
      _pad2D{} {}

// [3] 0x1400c5660
// [4] 0x1400c5600
// [5] 0x1400c55b0
// [8] 0x1400c55a0 — the leaf's own `RET 0` (same bytes as the base body)
void AnimateNaturalResource::CheckEntity() {}
// [9] 0x1400c5560

::Object::Base* AnimateNaturalResource::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimateNaturalResource), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AnimateNaturalResource());
}
::Object::Base* AnimateNaturalResource::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AnimateNaturalResource());
}


// ============================================================
// FUN_1400c57d0 — AnimateNaturalResource_Action_CopyCtor
//   RCX=self, RDX=param_2, R8D=param_3 (int @ +0x28), R9B=param_4 (byte @ +0x2c).
//   +0x20 = *param_2 ; +0x28 = param_3 ; +0x2c = param_4.
// ============================================================
// 0x1400c57d0: +0x20 = *param_2 ; +0x28 = param_3 ; +0x2c = param_4.
AnimateNaturalResource::AnimateNaturalResource(ComponentEntity* const* entity,
                                               uint32_t animIndexArg, bool wait)
    : resourceEntity(*entity),
      animIndex(static_cast<int32_t>(animIndexArg)),
      waitForAnim(wait) {}

DEFINE_EXE_TYPE(AnimateNaturalResource, "Action")
