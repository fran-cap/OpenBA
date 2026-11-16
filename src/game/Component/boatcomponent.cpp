// src/game/Component/boatcomponent.cpp
// Clean reimplementation of BoatComponent lifecycle methods.
//
// BoatComponent (0x98 bytes) manages fishing boat state:
// entity refs, position vectors, route waypoints, dock ID.
// Secondary vtable sub-object at +0x78 (ControllerComponent pattern).
//
// Vtable at 0x140158f40. Non-trivial override:
//   Shutdown (0x1400465d0) -- 95-instruction complex serialization
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/BoatComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"
#include "object/TypeInfo.h"
#include "math/Math.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G

// FUN_14000d030 -- resize Vector3 array. ENDGAME B.3.2 step 2: this IS
// Collection::Array<Vector3>::SetSize — Persist calls route.SetSize(count)
// directly. (The free BoatComponent_ResizeVec3Array copy and its
// reader-less 0x14000d030 row went at the B.3.8 close.)

// ============================================================================
// ENDGAME B.3.8 MI-A — BoatComponent as a REAL class (BoatComponent.h).
//
// Before: a 48-slot hand-built `BoatComponent_LiveVtable` filled at every
// create (FillTypedVtable + InstallController), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<BoatCtrlTag> table stamped into raw bytes at +0x78.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/BoatComponent.h"

// ── Controller state machine (boatcomponent_states.cpp) ──────────────────────────
// BoatComponent is a state-machine component in exactly TradeComponent's shape, and the
// geometry is read off its retail vtable 0x140158f40 rather than guessed:
//   slot 16 = 0x140003730 (`XOR EAX,EAX; RET`)  => the machine starts in state 0
//   slot 17 = 0x140003720 (`LEA RAX,[RCX+0x78]`) => member at +0x78, back-pointer at +0x90
// The state table is built ONCE at static-init time, as retail's static initializer
// does (B38 D6/D7).
extern "C" void BoatCtrl_PopulateTable();
static const bool s_boatStateTableBuilt = (BoatCtrl_PopulateTable(), true);

// 0x140046800. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28..+0x68 = 0, +0x70 = -1 (0x14004685c: the route index, "no waypoint yet" —
// the route builder resets it to 0 with the first point, boatcomponent_route.cpp);
// `_controller(this)` is the member's back-pointer at +0x90. No flag OR (0x80).
BoatComponent::BoatComponent()
    : destination(), model(), character(), dockPosition(), dockHeading(0.0f), waypoint(),
      _pad5C{}, routeIndex(-1), _pad74{},
      _controller(this) {}

// 0x140046770: the body is empty. ~_controller restamps +0x78 (0x1400467a5), THEN
// ~route frees and zeroes the waypoint array (+0x60 data, +0x68 count/capacity,
// 0x1400467b1..0x1400467c1), then the bases' restamps and ~Internal: reverse
// declaration order, the compiler's chain.
BoatComponent::~BoatComponent() {
}

// create 0x1400036e0: MemAlloc(0x98, 8), then the ctor.
::Object::Base* BoatComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BoatComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) BoatComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* BoatComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) BoatComponent());
}

// [3] 0x1400465d0: Component::Persist (a direct CALL 0x1401189b0), an empty
// Serializer scope (format compat), then one scope carrying: the +0x28 pointer
// (validated against ti 0x1401c5d70 = ComponentEntity at 0x140046635; the tree had
// read it as ResidenceComponent 0x1401c7cf0), the +0x40 Vector3, the +0x4c float, the +0x30 pointer
// (ModelComponent, ti 0x1401c7a50), the +0x38 pointer (CharacterComponent, ti
// 0x1401c7078), the +0x50 Vector3, the +0x70 route index and the +0x60 Vector3
// route array (count at +0x68, resized on a load). A pointer whose object fails
// its IsDerivedFrom check is cleared.
void BoatComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Stream& io = stream;
    { IO::Serializer scope(io, 0); }
    IO::Serializer ser(io, 0);
    destination.Serialize(ser, ComponentEntity::TypeId());   // 0x140046635: MOV EDX,[0x1401c5d70]
    dockPosition.Serialize(ser);
    ser.Serialize(dockHeading, nullptr);                                               // +0x4c float
    model.Serialize(ser, ModelComponent::TypeId());
    character.Serialize(ser, CharacterComponent::TypeId());
    waypoint.Serialize(ser);
    ser.Serialize(routeIndex, nullptr);
    int count = route._size;
    ser.Serialize(count, nullptr);
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // exe 0x14000d030 IS Collection::Array<Vector3>::SetSize(n).
        route.SetSize(count);
    }
    Math::Vector3* points = route._data;
    if (count > 0 && points) {
        for (int i = 0; i < count; i++)
            points[i].Serialize(ser);
    }
}

DEFINE_EXE_TYPE(BoatComponent, "ControllerComponent")
