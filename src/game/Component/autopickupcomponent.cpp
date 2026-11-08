// src/game/Component/autopickupcomponent.cpp
// Clean reimplementation of AutoPickupComponent lifecycle methods.
//
// AutoPickupComponent (0x40 bytes) auto-collects dropped resources near buildings.
// Fields: +0x28=isEnabled (byte), +0x30=array ptr, +0x38=array count.
//
// Vtable at 0x140158df0. Non-trivial overrides:
//   Shutdown  (0x1400449d0) -- Serializes isEnabled bool + int array
//   Startup   (0x140044990) -- isEnabled from the AutoPickupDescription
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/AutoPickupComponent.h"
#include "Component/AutoPickupDescription.h"   // ENDGAME B.3.9 CB: 0x1401c3fe8 cell class
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>

// The +0x30 int array. ENDGAME B.3.2 step 2: FUN_1400021d0 IS
// `Collection::Array<int32>::SetSize`, MSVC-monomorphised into the exe, so the
// resolved pointer is gone and the member call takes its place.
// The IsDerivedFrom import slot 0x140152a98 is Object::Base::IsDerivedFrom.

// ============================================================================
// ENDGAME B.3.8 VIS — AutoPickupComponent as a REAL class (AutoPickupComponent.h).
//
// Before: COMPONENT_INERT_FACTORY_SER — a 48-slot table filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal).
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/AutoPickupComponent.h"

// 0x140044ae0: Component() is the CALL 0x1401188b0; the 0x140158df0 stamp is
// the compiler's; isEnabled (+0x28) = 1; +0x30 = +0x38 = 0 (pileWorkIds's Array ctor);
// flags |= 0x80.
AutoPickupComponent::AutoPickupComponent()
    : isEnabled(true), _pad29{} {
    OrFlagsInCtor(0x80);
}

// [3] 0x1400449d0 — Component::Persist (a direct CALL 0x1401189b0), then ONE
// Serializer scope: isEnabled (+0x28), the int array's count (+0x38; on a load the
// +0x30 array is resized to it) and the array elements in bulk.
void AutoPickupComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Serializer ser(stream, 0);
    ser.Serialize(isEnabled, nullptr);
    int count = pileWorkIds._size;
    ser.Serialize(count, nullptr);
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // +0x30 pileWorkIds: exe 0x1400021d0 (called at 140044a40) copies only the
        // old prefix and leaves a grown tail as-found. Safe: this arm runs only when
        // loading, and the bulk read below (140044a55) writes all `count` elements
        // from the stream before any of them is read.
        pileWorkIds.SetSize(count);
    }
    ser.Serialize(count, pileWorkIds._data, nullptr);
}

// [4] 0x140044990: isEnabled (+0x28) = the description's _enabled (+0x20), the
// description taken through IsDerivedFrom AutoPickupDescription (cell 0x1401c3fe8;
// CMOVNZ, null on a miss) and read unguarded, as retail reads it.
void AutoPickupComponent::Startup() {
    Description* obj = _description;
    AutoPickupDescription* config = obj->IsDerivedFrom(AutoPickupDescription::TypeId())   // import 0x140152a98
                                        ? static_cast<AutoPickupDescription*>(obj) : nullptr;
    isEnabled = config->_enabled;
}
// [13] 0x140044970 — `isEnabled ? 3 : 1` (NEG / SBB / AND 2 / OR 1); the command
// id is ignored, as in retail. Bit 0: the command is offered; bit 1: it is on.
uint32_t AutoPickupComponent::CommandMask(int /*command*/) {
    return isEnabled ? 3u : 1u;
}

// [14] 0x140044930 — flip isEnabled; on the OFF edge drop the collected list:
// MemFree(+0x30) (import 0x140152b68), then +0x30 = +0x38 = 0 (data, size and
// capacity), which is the whole of a trivial-element ~Array. The id is ignored.
void AutoPickupComponent::Command(int /*command*/) {
    isEnabled = !isEnabled;
    if (!isEnabled) {
        pileWorkIds.DestroyAndFree();
    }
}

// create 0x1400033c0: MemAlloc(0x40, 8), then the ctor.
::Object::Base* AutoPickupComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AutoPickupComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AutoPickupComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AutoPickupComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AutoPickupComponent());
}

DEFINE_EXE_TYPE(AutoPickupComponent, "Component")
