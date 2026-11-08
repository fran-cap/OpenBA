// src/game/Game/disease.cpp
// Disease (Object::External, 0x48 bytes) — disease resource definition.
//
// Loaded during the in-game world build (worldgen / climate setup) as an
// External resource referenced by an ExternalList. In standalone it was a
// NullFactory stub → factory returned NULL → Runtime's External::Load called
// IsDerivedFrom on the NULL result → AV @Runtime+0xC039A (the "BuildTerrain
// hang" was actually this crash, masked by the shutdown-deadlock in
// LdrShutdownProcess). This file provides the real factory + faithful Serialize.
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/Disease.h) with a
// compiler-generated vtable; the old 9-slot `g_Disease_Vtable` is gone.
//
// Decompiled READ-ONLY from Application-x64.exe:
//   RegisterType  0x14014cb00 (mislabeled ClimateGenerator_StaticInit):
//                 BaseTypeInfo("Disease","Object::External", abstract=NULL,
//                              factory=0x14008fd0, vslot=ClimateGenerator_vslot18)
//   Factory       0x14008fd0: MemAlloc(0x48,8); External::External; vtable 0x14015e190;
//                 +0x20=0 +0x28=0 +0x30=1.0f +0x34=0 +0x38=1.0f +0x3c=10.0f +0x40=0
//   Serialize     0x1400932d0 stream order:
//                 +0x20 ExternalPtr<StringTable> (FUN_14000bf40),
//                 +0x28 System::String,
//                 +0x30/+0x34/+0x38/+0x3c/+0x40 floats
//   Dtor          0x140018f60: String::~String(+0x28); External::~External; cond free.

#include "Game/Disease.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <cstdint>
#include <new>
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// 0x14000bf40 — ExternalPtr<StringTable>::Serialize, registered at that address
// (standalone/address_registry.cpp) to this body in serialize_externals.cpp.
// Called directly: the old lazily-resolved door (a non-atomic first-call cache,
// reached on the LOADER thread — B6-5) resolved to exactly this function.

// create 0x14008fd0 — the field defaults (External() and the stamp are the
// compiler's).
Disease::Disease()
    : _stringTable(nullptr), _name(),            // ExternalPtr (weak), empty String (qword 0)
      _spreadIntervalMonths(1.0f), _contagion(0.0f), _spreadRadius(1.0f), _catchWeight(10.0f), _deathOdds(0.0f) {}

// Dtor 0x140018f60: ~String(+0x28) (the member's), then ~External: the
// compiler's chain, in that order.
Disease::~Disease() {}

// [5] 0x1400932d0 — returns AL=1.
bool Disease::Serialize(IO::Stream& stream) {
    IO::Serializer s(stream, 0);
    ::Object::SerializeExternal<UI::StringTable>(s, _stringTable);
    _name.Serialize(s);
    s.Serialize(_spreadIntervalMonths, nullptr);
    s.Serialize(_contagion, nullptr);
    s.Serialize(_spreadRadius, nullptr);
    s.Serialize(_catchWeight, nullptr);
    s.Serialize(_deathOdds, nullptr);
    return true;   // ~Serializer runs here, after the last field, as before

}

// [6] 0x1400190a0 — slot 0 with flags 0 (destroy, no free), then the ctor
// again in place (retail inlines the ctor: External(), stamp, the defaults).
Object::External* Disease::Reconstruct() {
    this->~Disease();
    return ::new (static_cast<void*>(this)) Disease();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void Disease::Destruct() { this->~Disease(); }

// [8] 0x140019040 — placement-new of the class on `this` (the old table's
// CreateInPlace, the same behaviour).
Object::External* Disease::Construct() {
    return ::new (static_cast<void*>(this)) Disease();
}

// create 0x14008fd0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* Disease::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Disease), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) Disease());
}
::Object::Base* Disease::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) Disease());
}


DEFINE_EXE_TYPE(Disease, "Object::External")
