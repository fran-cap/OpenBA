// src/game/Component/clearresourcecomponent.cpp
// Clean reimplementation of ClearResourceComponent lifecycle methods.
//
// ClearResourceComponent (0x48 bytes) -- ControllerComponent subclass with no extra fields.
// Handles clearing resources from tiles during construction.
//
// Vtable at 0x140162840. Non-trivial override:
//   Persist (0x1401185E0) -- Component::Persist + two empty serializer blocks
//
// Decompiled from Application-x64.exe using Ghidra.

#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
// ============================================================================
// ENDGAME B.3.8 MI-A — ClearResourceComponent as a REAL class (ClearResourceComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES + CONTROLLER_INSTALL_SERIALIZE (a 48-slot
// table re-filled at every create), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<ClearResourceCtrlTag> table stamped into raw bytes at +0x28.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/ClearResourceComponent.h"

// The clear-job's self-destruct controller — this is what clears the clearing ICON
// once the resource is harvested. State table + the state-2 enter handler (a port of
// 0x140118560) live in clearresourcecomponent_states.cpp.
//
// ⚠️ The default state of 2 is faithful but must never be the one the SM adopts —
// state 2 is the destroy. It is safe only because StateMachineComponent_Startup
// (0x14012b6a0) reads GetDefaultState from the FIRST ControllerComponent only, and
// `cleararea` (default 0) precedes `clearresource` in every template that uses this
// component (Template/Clear.rsc, Template/ClearR.rsc — both verified).
//
// The state table is built ONCE at static-init time, as retail's static
// initializer does (B38 D6/D7); it used to be filled, unsynchronised, on every
// create from both the loader and the main thread.
extern "C" void ClearResourceCtrl_PopulateTable();
static const bool s_clearResourceStateTableBuilt = (ClearResourceCtrl_PopulateTable(), true);

// 0x1401186a0. Component() is the CALL 0x1401188b0; the table stamps and the
// member's 0x14015f070 / 0x1401627f0 stamps are the compiler's; `_controller(this)`
// is the +0x40 back-pointer store and the +0x30 / +0x38 zeroes. No flag OR.
ClearResourceComponent::ClearResourceComponent() : _controller(this) {}

// 0x140118640: restamp + ~Internal, no own work.
ClearResourceComponent::~ClearResourceComponent() = default;

// create 0x1400315a0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* ClearResourceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ClearResourceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ClearResourceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ClearResourceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ClearResourceComponent());
}

// [3] 0x1401185e0: Component::Persist (a direct CALL 0x1401189b0) and two empty
// Serializer scopes (each still occupies bytes in the save encoding).
void ClearResourceComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    { IO::Serializer scope(stream, 0); }
    { IO::Serializer scope(stream, 0); }
}

DEFINE_EXE_TYPE(ClearResourceComponent, "ControllerComponent")
