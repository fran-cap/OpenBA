// src/game/Component/highlightcomponent.cpp
// Clean reimplementation of HighlightComponent::Shutdown from Application-x64.exe.
//
// HighlightComponent (0x128 bytes) manages selection/highlight outlines around entities.
// Embeds Graphics::EdgeMeshInstance at +0x28 (0xD0 bytes) and Math::Transform at +0x100.
//
// Vtable at 0x140159990. Only non-trivial override:
//   Shutdown (0x14004c880) -- Component_Serialize + empty serializer (version 0)
//
// Ghidra decompilation:
//   Component_Serialize(param_1, param_2)
//   Serializer(local, param_2, 0); ~Serializer(local)
//
// Decompiled from Application-x64.exe using Ghidra.

// graphics/Scene.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
#include "Component/HighlightComponent.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4a closing: the stream-scope ctor/dtor pair
#include <new>
#include <cstdint>
#include <cstdio>

#include <cstdlib>

// ============================================================
// HighlightComponent removed-from-world (vtable slot 7, +0x38)
// ============================================================
// The selection outline is an EdgeMeshInstance embedded at +0x28; when it is
// live in the Graphics::Scene its object id is at +0x120 (set by the tool-side
// Entity_impl_c4a0 / 0x14004c4a0 in tool_base_render.cpp). Retail's own +0x38 is
// a no-op (0x140118830) — retail removes the outline through the EditTool
// DESELECT path (cb40 -> c4a0 -> Scene::RemoveObject) which runs BEFORE the
// selected entity is freed.
//
// STANDALONE DEVIATION (required): our EditTool detects a dead selection only
// AFTER the EntityList drain has freed the entity block, so its cb40 reads an
// already-dead self+0x30 and never reaches this (freed) component to remove the
// outline. The dangling EdgeMeshInstance then stays in the Scene draw list and
// the next GameUpdateLoop::Draw faults in EdgeMeshInstance::Draw ->
// MeshInstance::GetNodeToWorld -> Transform::Lerp on freed node memory (live
// crash 2026-07-20, RDI inside the demolished yard-entity block).
//
// Fix: participate in the MarkForRemoval (0x1400c2ea0) +0x38 broadcast, which
// runs at mark time — while the component is still valid and BEFORE the drain
// frees it (and before that frame's Draw). This requires capability bit 1 (0x2)
// in the +0x20 flag word (the broadcast's per-component gate); HighlightComponent
// was the ONLY scene-object-owning component missing it (Model 0x83 / Storage
// Display 0xCB / StatusIcon 0xC2 all have it). No-ops when no outline is
// registered (+0x120 < 0), so it is safe on every non-selected teardown.
void HighlightComponent::OnRemove() {
    int32_t sceneId = highlightId;
    if (sceneId < 0) return;
    if (GameUpdateLoop::_singleton == nullptr) return;
    Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;   // +0x60
    if (!scene) return;
    scene->RemoveObject(static_cast<uint32_t>(sceneId));
    highlightId = -1;
}

// ============================================================================
// ENDGAME B.3.8 VIS — HighlightComponent as a REAL class (HighlightComponent.h).
//
// Before: a 48-slot HighlightComponent_InertVtable filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal), +0x120 = -1 patched in after the
// zero-fill. After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/HighlightComponent.h"

// 0x14004c930: Component() is the CALL 0x1401188b0; the 0x140159990 stamp is
// the compiler's; then, in retail's order, ??0EdgeMeshInstance on +0x28
// (CALL [0x140151358]), +0xf8 = 0, ??0Transform on +0x100 (CALL [0x140151328]),
// +0x11c = 0 (byte), +0x120 = -1. TREE DIVERGENCE: flags get bit 1 (0x82).
// +0x120 = -1 ("not in scene") is load-bearing: slot 7 runs on EVERY entity
// teardown, and a 0 would make it Scene::RemoveObject(scene, 0) on whichever
// object genuinely owns id 0 (live freeze 2026-07-25).
HighlightComponent::HighlightComponent()
    : edgeMeshInstance(), outlineMesh(nullptr), transform(), highlightFlag(0),
      _pad11D{}, highlightId(-1), _pad124{} {
    OrFlagsInCtor(0x2);
}

// 0x14004c8c0 (slot 0 0x140007040 runs it): restamp (the compiler's), delete
// +0xf8 through its slot 0 with flag 1 when non-null, +0xf8 = 0; then
// ~EdgeMeshInstance on +0x28 (CALL [0x1401513b0]) is the member destruction,
// and the Component restamp + ~Internal (JMP [0x140152770]) the base's.
HighlightComponent::~HighlightComponent() {
    delete outlineMesh;
    outlineMesh = nullptr;
}

// [3] 0x14004c880 — Component::Persist (a direct CALL 0x1401189b0) PLUS its own
// (empty) Serializer scope. The scope is NOT decoration: the save encoding
// brackets every scope with sentinels, so an "empty" scope still occupies 16
// bytes (AAAAAAAA open, two dwords, BBBBBBBB close). Measured on the packaged
// tutorial save: retail's HighlightComponent record is 66 bytes, of which
// Component::Persist is 50 and this scope the remaining 16. Skipping it desyncs
// the stream for every object after this one.
void HighlightComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Serializer scope(stream, 0);
}

// [8] 0x14004c850 (23 B): while the outline is live in the Scene (+0x120 >= 0),
// forward the frame time to the embedded EdgeMeshInstance's slot 1 (Update):
//   CMP [RCX+0x120],0 ; JL ret ; RCX += 0x28 ; JMP [[RCX]+0x8]  (RDX = time, passed through)
// Repaint ORs flag bit 2 when it adds the outline and clears it when it removes it.
void HighlightComponent::Tick(const System::Time* time) {
    if (highlightId < 0) return;
    edgeMeshInstance.Update(*time);
}

// [12] 0x14004c870 (10 B): event 0 rebuilds the outline:
//   TEST EDX,EDX ; JZ 0x14004c4a0 (Repaint, reads RCX only) ; RET
void HighlightComponent::OnEvent(int eventId) {
    if (eventId == 0) Repaint();
}

// create 0x140006e40: MemAlloc(0x128, 8), then the ctor.
::Object::Base* HighlightComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HighlightComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HighlightComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* HighlightComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HighlightComponent());
}

DEFINE_EXE_TYPE(HighlightComponent, "Component")
