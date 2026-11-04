// src/game/Component/immigrationcomponent.cpp
// Clean reimplementation of ImmigrationComponent::Shutdown from Application-x64.exe.
//
// ImmigrationComponent (0x68 bytes) -- ControllerComponent subclass.
// Manages immigration/nomad arrival events.
//
// Vtable at 0x140159b30:
//   [3] Shutdown = 0x14004e4b0 -- complex serialization with entity array loop
//
// Ghidra decompilation:
//   Component_Serialize(param_1, param_2)
//   Serializer(local_20, param_2, 0); ~Serializer(local_20)  // empty controller block
//   Serializer(local_30, param_2, 0)
//   Serialize(local_30, param_1+0x50, nullptr)  // field50 (int)
//   local_res8 = *(param_1+0x60)                // array count
//   Serialize(local_30, local_res8, nullptr)
//   if loading: DynArray_Resize(param_1+0x58, count)
//   for each element in array:
//     InternalBasePtr::Serialize(element, serializer)
//     if ptr != null && !IsDerivedFrom(ptr, ComponentEntity): clear ptr
//   Time::Serialize(param_1+0x48, serializer)
//   ~Serializer(local_30)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ImmigrationComponent.h"
#include "Entity/ComponentEntity.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch

// ============================================================================
// ENDGAME B.3.8 MI-A — ImmigrationComponent as a REAL class (ImmigrationComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES + CONTROLLER_INSTALL_SERIALIZE (a 48-slot
// table re-filled at every create), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<ImmigrationCtrlTag> table stamped into raw bytes at +0x28.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/ImmigrationComponent.h"

// The nomad-arrival state machine (states 2/0xf/0x10/0x11,
// immigrationcomponent_states.cpp). Node at +0x28 (slot 17 0x140004080), default
// state 2 (slot 16 0x1400026c0). The state table is built ONCE at static-init time,
// as retail's static initializer does (B38 D6/D7).
extern "C" void ImmigrationCtrl_PopulateTable();
static const bool s_immigrationStateTableBuilt = (ImmigrationCtrl_PopulateTable(), true);

// 0x14004e640. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48..+0x60 = 0. No flag OR.
ImmigrationComponent::ImmigrationComponent()
    : _controller(this), waitTimer{0}, arrivedCount(0), _pad54{} {}

// 0x14004e5d0: the group array (+0x58 data, +0x60 count/capacity) is freed and
// zeroed by ~group (kSpec_PtrZero: no element destroy); the controller's and the
// bases' dtors (restamps only) and ~Internal are the compiler's chain, reverse
// declaration order. The body is empty.
ImmigrationComponent::~ImmigrationComponent() {
}

// create 0x140007a20: MemAlloc(0x68, 8), then the ctor.
::Object::Base* ImmigrationComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ImmigrationComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ImmigrationComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ImmigrationComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ImmigrationComponent());
}

// [3] 0x14004e4b0: Component::Persist (a direct CALL 0x1401189b0), an empty
// Serializer scope (the controller block), then one scope carrying field50, the
// +0x58 entity array (count, resize on a load, each InternalBasePtr — cleared when
// it is not a ComponentEntity) and the Time at +0x48.
void ImmigrationComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Stream* st = &stream;
    { IO::Serializer scope(*st, 0); }
    IO::Serializer ser(*st, 0);
    ser.Serialize(arrivedCount, nullptr);
    int arrayCount = group._size;
    ser.Serialize(arrayCount, nullptr);
    // "Am I loading?" is the STREAM's direction (Serializer -> stream +8), not the
    // Serializer's own +8 (m_encoding): see
    // reference_serializer_plus8_is_encoding_not_loading_flag.
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // exe 0x1400032e0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
        group.SetSize(arrayCount);
    }
    Object::InternalPtr<ComponentEntity>* element = group._data;
    for (int64_t remaining = arrayCount; remaining > 0; --remaining, ++element) {
        element->InternalBasePtr::Serialize(ser);
        // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
        // downcast to ComponentEntity*; the answer is the same.
        if (!element->IsNull() && !::Object::IsDerivedFromNullSafe(*element, ComponentEntity::TypeId()))
            *element = nullptr;
    }
    waitTimer.Serialize(ser);
}

DEFINE_EXE_TYPE(ImmigrationComponent, "ControllerComponent")
