// src/game/Component/pickingcomponent.cpp
// Clean reimplementation of PickingComponent lifecycle methods from Application-x64.exe.
//
// PickingComponent (0x38 bytes) — manages 3D picking (mouse -> entity selection).
// Fields: +0x28=pickingId (int32, -1=unregistered), +0x30=entity ref (IBP)
//
// Vtable at 0x140164040. Two hookable overrides:
//   Shutdown (0x140128bb0) — Serialize IBP + IsDerivedFrom validation
//   OnCreate (0x140128b50) — Remove picking mesh via Scene dispatch
//

#include "Component/pickingcomponent_funcs.h"
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Component/PickingComponent.h"
#include "Component/PickingDescription.h"
#include "math/Math.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Entity/ComponentEntity.h"   // ENDGAME B.3.9 CB: ComponentEntity::TypeId()
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)
#include "ext/render_culling/render_culling.h"   // EXT SEAM: ForgetPick (OnRemove)



// ============================================================
// PickingComponent::RefreshMesh (0x140128f50) -- (re)register the picking mesh
// ============================================================
// Slots [6] OnAdded and [11] are `JMP 0x140128f50`; BuildComponent calls it as a site
// walks its construction frames (0x140108b26), so the pick mesh follows the drawn frame.
// Retail, in order:
//   1. its own slot 7 (OnRemove): drop the current mesh, pickingId = -1;
//   2. FindComponent<ModelComponent> (0x140003f60), read unguarded: meshGroup +0x164,
//      meshIndex +0x166;
//   3. `meshIndex < 0 || pickingId >= 0` -> return;
//   4. the description through IsDerivedFrom PickingDescription (cell 0x1401c7c48;
//      CMOVNZ, null on a miss, then read unguarded);
//   5. Picking::Scene::AddMesh (import 0x140152430) on GameUpdateLoop +0x70 with
//      groups[meshGroup].meshes[meshIndex], the model's transform (+0x148, seven
//      dwords), userData (+0x30; Startup 0x140128c30 seated it, a tool may have
//      re-pointed it since) and three zeros; the id goes to pickingId.
// Nothing is null-tested and nothing is printed.
void PickingComponent::RefreshMesh() {
    OnRemove();   // [RAX+0x38]

    ModelComponent* model = _entity.Get()->FindComponent<ModelComponent>();   // 0x140003f60
    const int16_t meshGroup = model->meshGroup;
    const int16_t meshIndex = model->meshIndex;
    if (meshIndex < 0 || pickingId >= 0) return;

    Description* descRaw = _description;
    PickingDescription* pickDesc = descRaw->IsDerivedFrom(PickingDescription::TypeId())   // import 0x140152a98
                                       ? static_cast<PickingDescription*>(descRaw) : nullptr;
    const Math::Transform xform = model->transform;

    Picking::PickingMesh* mesh = pickDesc->_groups[meshGroup].meshes[meshIndex];
    Picking::Scene* scene = GameUpdateLoop::_singleton->m_pickingScene;
    pickingId = (int32_t)scene->AddMesh(
        Object::ExternalPtr<Picking::PickingMesh>(mesh), xform, userData.Get(), 0, 0, nullptr);
}

// ============================================================================
// ENDGAME B.3.8 VIS — PickingComponent as a REAL class (PickingComponent.h).
//
// Before: a 48-slot PickingComponent_InertVtable filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal), pickingId = -1 patched in after
// the zero-fill.
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"

// 0x140128c80: Component() is the CALL 0x1401188b0; the 0x140164040 stamp is
// the compiler's; pickingId = -1 (so OnRemove's `pickingId >= 0` guard skips the
// RemoveMesh for a component that never registered a mesh), userData = 0,
// flags |= 0xc3.
PickingComponent::PickingComponent() : pickingId(-1), _pad2C{}, userData(nullptr) {
    OrFlagsInCtor(0xc3);
}

// ============================================================
// PickingComponent::Persist (vtable slot [3], 0x140128bb0)
// ============================================================
void PickingComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    // Serialize the userData InternalPtr
    userData.InternalBasePtr::Serialize(ser);

    // Validate: if not ComponentEntity, null it out
    // ENDGAME B.3.9 CB: retail validates this IBP with IsDerivedFrom(ptr, [0x1401c5d70]) =
    // ComponentEntity::TypeId() (Persist 0x140128bb0, at 0x140128bf9). The tree read 0x1401c72e8 instead -- the NAME
    // field (+8) of Description's record, whose only retail reader is Description's
    // TypeName slot 0x140031c60 -- which is unregistered, so the check never ran.
    if (userData != nullptr) {
        // ?IsDerivedFrom@Base@Object@@ (import slot 0x140152a98)
        if (!Object::IsDerivedFromNullSafe(userData.Get(), ComponentEntity::TypeId())) {
            userData = nullptr;
        }
    }
}

// [4] 0x140128c30 (9 B): userData (+0x30) = the owning entity (+0x10), the
// pick userData OnAdded hands Picking::Scene::AddMesh and a click returns.
// Runs from StartupComponents at create (flag bit 7; the ctor ORs 0xc3), before
// RegisterEntity's OnAdded, so a tool that re-points userData in between (the
// bridge's master entity) keeps its value.
void PickingComponent::Startup() {
    userData = _entity;
}

// [6] 0x140128b90 = OnAdded, `JMP 0x140128f50` — dispatched by
// ComponentEntity_OnAddedPass at EntityList_RegisterEntity time, in COMPONENT
// ARRAY ORDER. That order is the whole point: on a WoodHouse, Picking is index
// [5] and Build is index [16], so retail registers the pick mesh while the model
// still carries the FOUNDATION decal (meshIndex 0) and only then does
// BuildComponent's own OnAdded Stage 3 un-frame it to -1.
void PickingComponent::OnAdded() { RefreshMesh(); }

// [7] 0x140128b50 — entity mark-removal FUN_1400c2ea0 broadcasts vtable+0x38 to
// every component whose flags have bit 1 BEFORE the entity is freed; the body
// removes the picking mesh from Picking::Scene (gw+0x70) and resets pickingId.
// Without it a removed building's mesh dangles with userData = the freed entity.
// ============================================================
// PickingComponent::OnRemove (vtable slot [7], 0x140128b50; Ghidra "OnCreate")
// ============================================================
// If pickingId >= 0, calls Picking::Scene::RemoveMesh and resets to -1.

void PickingComponent::OnRemove() {
    if ((int)pickingId >= 0) {
        if (GameUpdateLoop::_singleton != nullptr) {
            Picking::Scene* pickingScene = GameUpdateLoop::_singleton->m_pickingScene;
            // EXT SEAM (NOT RETAIL): ext.render_culling drops a deferred push to this id.
            Ext::RenderCulling::ForgetPick(pickingScene, (uint32_t)pickingId);
            // Picking::Scene::RemoveMesh (import slot 0x140152438, Runtime 0x180125290).
            pickingScene->RemoveMesh((uint32_t)pickingId);
        }
        pickingId = -1;
    }
}

// [11] 0x140128ba0 = `JMP 0x140128f50`, the same body as slot 6: the post-load
// pass (EntityList 0x1400c4730) re-registers the pick mesh, which a save does not
// carry (pickingId is not serialized; the loaded component holds the ctor's -1).
void PickingComponent::Slot11() { RefreshMesh(); }

// create 0x140036da0: MemAlloc(0x38, 8), then the ctor.
::Object::Base* PickingComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PickingComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PickingComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* PickingComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PickingComponent());
}

DEFINE_EXE_TYPE(PickingComponent, "Component")
