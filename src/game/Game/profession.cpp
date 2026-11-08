// src/game/Game/profession.cpp
// Clean reimplementation of Profession game logic.
//
// Profession (0x60 bytes) -- citizen profession definition.
// Extends Object::External (0x20 base).
// Vtable at 0x14015efa0.
//
// Defines a profession with name, description, priority, and skill multipliers.
// Fields include ExternalPtrs at +0x20/+0x28, System::Strings at +0x30/+0x38/+0x40,
// and numeric fields (int, bools, floats) from +0x48 to +0x58.
//
// Profession is an Object::External type referenced by CitizenDescription (the
// citizen-template closure FindOrLoads Profession\*.rsc).
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/Profession.h, field
// layout and retail slot map there) with a compiler-generated vtable. Gone: the
// hand-built 9-slot `g_Prof_Vtable`, its stubs, the lazy vtable resolve in the
// old Init, the lazily-resolved ExternalPtr doors (a non-atomic first-call cache
// on the loader thread, B6-5) and the C entry points.
//
// Decompiled from Application-x64.exe.

#include "Game/Profession.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <cstdint>
#include <cstdio>
#include <new>
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// FUN_14000bf40 / FUN_14000bd30 -- the two ExternalPtr serialize variants.
// Both addresses are registered to this one body (address_registry.cpp); it is
// called directly (the old lazily-resolved pointers resolved to it).

// ============================================================
// Profession::Profession (0x1400a9030)
// ============================================================
// External() and the 0x14015efa0 stamp are the compiler's; then the defaults.
// The three System::String members (+0x30 / +0x38 / +0x40) are constructed
// empty (null data), retail's three zero stores.
Profession::Profession() {
    // ExternalPtr fields
    stringTable = nullptr;
    spriteSheet = nullptr;

    // Numeric fields
    sortPriority = 0;
    transient = 0;
    immuneToDisease = 0;
    isAvailable = 1;
    priority = 100;
    skillMult1 = 1.0f;
    skillMult2 = 1.0f;
}

// ============================================================
// Profession::~Profession / DeletingDtor (vtable +0x00)
// ============================================================
// Original: 0x14001e6f0
// Destroys 3 strings at +0x40, +0x38, +0x30 (in reverse order): the compiler's
// member destruction. External::~External is the compiler's chain; MemFree if
// flags & 1.
Profession::~Profession() {}

// ============================================================
// Profession::Reconstruct (vtable +0x30, Slot6) -- Reset to defaults
// ============================================================
// Original: 0x14001e580. Destroys (slot 0, flags 0) then re-constructs in place.
Object::External* Profession::Reconstruct() {
    this->~Profession();
    return ::new (static_cast<void*>(this)) Profession();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void Profession::Destruct() { this->~Profession(); }

// ============================================================
// Profession::Construct (vtable +0x40)
// ============================================================
// Original: 0x14001e570 — placement-new of the class on `this`.
Object::External* Profession::Construct() {
    return ::new (static_cast<void*>(this)) Profession();
}

// ============================================================
// Profession::Serialize (vtable +0x28, Slot5) -- Serialize all fields
// ============================================================
// Original: 0x1400a8f10
// Creates a Serializer(stream, version=0), then serializes each field.
bool Profession::Serialize(IO::Stream& stream) {
    IO::Serializer serializer(stream, 0);

    // Serialize ExternalPtr fields
    ::Object::SerializeExternal<UI::StringTable>(serializer, stringTable);  // +0x20 (0x14000bf40)
    ::Object::SerializeExternal<UI::SpriteSheet>(serializer, spriteSheet);  // +0x28 (0x14000bd30)

    // Serialize String fields
    stringName.Serialize(serializer);  // +0x30
    stringDesc.Serialize(serializer);  // +0x38
    stringTool.Serialize(serializer);  // +0x40

    // Serialize numeric fields
    serializer.Serialize(sortPriority, nullptr);      // +0x48 int
    serializer.Serialize(transient, nullptr);      // +0x4C bool
    serializer.Serialize(immuneToDisease, nullptr);      // +0x4D bool
    serializer.Serialize(skillMult1, nullptr);   // +0x54 float
    serializer.Serialize(skillMult2, nullptr);   // +0x58 float
    serializer.Serialize(priority, nullptr);     // +0x50 int
    serializer.Serialize(isAvailable, nullptr);  // +0x4E bool
    return true;
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
::Object::Base* Profession::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Profession), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) Profession());
}
::Object::Base* Profession::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) Profession());
}


DEFINE_EXE_TYPE(Profession, "Object::External")
