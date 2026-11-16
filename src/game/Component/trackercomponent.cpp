// src/game/Component/trackercomponent.cpp
// Clean reimplementation of TrackerComponent lifecycle methods from Application-x64.exe.
//
// TrackerComponent (0x50 bytes) — tracks entities for path/resource tracking.
// Fields: +0x28=array1 ptr, +0x30=array1 count, +0x38=array2 ptr,
//         +0x40=array2 count, +0x48=trackerId (int, init -1)
//
// Vtable at 0x14015ab80. One non-trivial override:
//   Persist (0x140059130) — Serialize two paired (IBP+int) arrays + trackerId
//

#include "Component/TrackerComponent.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <cstdint>
#include <cstdio>
#include <new>
#include "Action/exe_bodies.h"   // ActRm_ArrayResize16_da70 (0x14000da70)
#include "Game/exe_bodies.h"

// Helpers: FUN_14000da70 ActRm_ArrayResize16_da70 — resize a paired (IBP+int) array;
// FUN_140002340 ::Object::SerializeExternal<ComponentDescription> — serialize an IBP entry.

// ============================================================================
// ENDGAME B.3.8 VIS — TrackerComponent as a REAL class (TrackerComponent.h).
//
// Before: a 48-slot TrackerComponent_InertVtable filled per create, Inert_ScalarDtor
// in slot 0 (only ~Internal), +0x48 = -1 patched in after the zero-fill.
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// The retail ctor (inlined in TrackerComponent_Factory 0x14000d7a0) writes
// +0x48 = -1. +0x48 is the "game-world item-count generation" stamp, and 0 is a
// LEGAL generation value: with 0 ProductionUI's per-frame update (0x140075a50)
// would compare gameWorld item count 0 == stamp 0 on the very first frame of a
// fresh world and SKIP the one-shot array rebuild the -1 sentinel forces.
TrackerComponent::TrackerComponent()
    : displayed(),
      produced(),
      trackerId(-1), _pad4C{} {}

// [3] ============================================================
// TrackerComponent::Persist (vtable slot [3], 0x140059130)
// ============================================================
// Serialize the two Entry arrays (displayed, produced), plus trackerId at +0x48.

void TrackerComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    // === the displayed list ===
    int localCount1 = displayed._size;
    ser.Serialize(localCount1, nullptr);

    // If loading, resize the array
    if (static_cast<int>(stream.GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        MaterialArraySetSize(&displayed, localCount1);   // 0x14000da70
    }

    // Serialize each (ExternalPtr + int) entry
    if (localCount1 > 0) {
        Entry* entry = displayed._data;
        for (int i = 0; i < localCount1; i++) {
            ::Object::SerializeExternal<ComponentDescription>(ser, entry->_resource);   // 0x140002340
            ser.Serialize(entry->_count, nullptr);
            ++entry;
        }
    }

    // === the produced list ===
    int localCount2 = produced._size;
    ser.Serialize(localCount2, nullptr);

    if (static_cast<int>(stream.GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        MaterialArraySetSize(&produced, localCount2);   // 0x14000da70
    }

    if (localCount2 > 0) {
        Entry* entry = produced._data;
        for (int i = 0; i < localCount2; i++) {
            ::Object::SerializeExternal<ComponentDescription>(ser, entry->_resource);   // 0x140002340
            ser.Serialize(entry->_count, nullptr);
            ++entry;
        }
    }

    // Serialize trackerId at +0x48
    ser.Serialize(trackerId, nullptr);
}

// create 0x14000d7a0: MemAlloc(0x50, 8), then the ctor.
::Object::Base* TrackerComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TrackerComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TrackerComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* TrackerComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TrackerComponent());
}

DEFINE_EXE_TYPE(TrackerComponent, "Component")
