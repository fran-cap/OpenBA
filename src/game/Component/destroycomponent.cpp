// src/game/Component/destroycomponent.cpp
// Clean reimplementation of DestroyComponent lifecycle methods from Application-x64.exe.
//
// DestroyComponent (0x68 bytes) -- ControllerComponent subclass.
// Manages building demolition/destruction process.
// Fields (DestroyComponent.h): burnTime (Time +0x48), burning (+0x50),
//         hasBuildComponent (+0x51), progress / progressTarget (int +0x54 / +0x58),
//         upgradeTarget (ExternalPtr<ComponentDescription> +0x60).
//
// Vtable at 0x140163120. Non-trivial overrides:
//   Persist   (0x14011db50) -- serialize all destroy state fields
//   Startup   (0x14011db00) -- copy the DestroyDescription's _replaceEntity into upgradeTarget
//   OnMessage (0x14011daf0) -- message 0 advances progress
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/DestroyComponent.h"
#include "Component/DestroyDescription.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch

// ============================================================================
// ENDGAME B.3.8 MI-A — DestroyComponent as a REAL class (DestroyComponent.h).
//
// Before: a 48-slot hand-built `DestroyComponent_LiveVtable` filled at every
// create (FillTypedVtable + FinishLiveVtable), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<DestroyCtrlTag> table stamped into raw bytes at +0x28.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/DestroyComponent.h"
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// The upgrade/demolish controller: the entity's StateMachineComponent links the
// member and the 0x14011dfa0 pump (UpgradeUI click) drives states 3->4->5
// (demolish -> haul-away -> refund + stone-house spawn), destroycomponent_states.cpp.
// Retail slot 16 returns 4 (0x140033c00 `MOV EAX,4`). That is only safe under
// retail's first-controller consult order, where the build controller precedes
// destroy in every building template; StateMachineComponent::Startup
// (statemachinecomponent.cpp) takes the first controller's default state. The state
// table is built ONCE at static-init time, as retail's static initializer does (B38 D6/D7).
extern "C" void DestroyCtrl_PopulateTable();
static const bool s_destroyStateTableBuilt = (DestroyCtrl_PopulateTable(), true);

// 0x14011dc80. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48 = 0, +0x50 = 0 (word:
// the two bools, H7), +0x54 = 0 (qword: +0x54 and +0x58), +0x60 = 0. No flag OR.
// The tree also zeroes +0x52 and +0x5c (kept).
DestroyComponent::DestroyComponent()
    : _controller(this), burnTime{0}, burning(false), hasBuildComponent(false), _pad52{},
      progress(0), progressTarget(0), _pad5C{}, upgradeTarget(nullptr) {}

// 0x14011dc20: restamp + ~Internal, no own work.
DestroyComponent::~DestroyComponent() = default;

// create 0x140033bc0: MemAlloc(0x68, 8), then the ctor.
::Object::Base* DestroyComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DestroyComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DestroyComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* DestroyComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DestroyComponent());
}

// [3] 0x14011db50 — save serialization: Component::Persist (a direct CALL
// 0x1401189b0), an empty controller scope, then one scope carrying field54 (int),
// +0x58 (int), +0x51 (bool), the +0x60 ExternalPtr (0x140002340), destroyFlags
// (+0x50, bool) and the Time at +0x48.
void DestroyComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    { IO::Serializer scope(stream, 0); }
    IO::Serializer ser(stream, 0);
    ser.Serialize(progress, nullptr);
    ser.Serialize(progressTarget, nullptr);
    ser.Serialize(hasBuildComponent, nullptr);   // +0x51
    ::Object::SerializeExternal<ComponentDescription>(ser, upgradeTarget);   // 0x140002340
    ser.Serialize(burning, nullptr);             // +0x50
    burnTime.Serialize(ser);
}

// [4] 0x14011db00 — copies the DestroyDescription's default target (desc+0x20)
// into +0x60; dispatched at spawn by ComponentEntity::StartupComponents.
void DestroyComponent::Startup() {
    Description* desc = _description;
    if (::Object::IsDerivedFromNullSafe(desc, DestroyDescription::TypeId()))
        upgradeTarget = static_cast<DestroyDescription*>(desc)->_replaceEntity;
}

// [15] 0x14011daf0: `if (msgCode == 0) ++*(int*)(self + 0x54);` — the demolition-
// progress credit, destroy-side twin of BuildComponent::OnMessage. The demolition
// action chain (BuildStructure_SetupBuildActions, altAnim=1) pushes a
// SendComponentMessage carrying DestroyComponent's type id; each completed
// PerformWork routes here and advances +0x54 toward the +0x58 target that gates the
// state-5 tick.
void DestroyComponent::OnMessage(int message, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    if (message == 0) ++progress;
}

DEFINE_EXE_TYPE(DestroyComponent, "ControllerComponent")
