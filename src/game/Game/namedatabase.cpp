// src/game/Game/namedatabase.cpp
// Clean reimplementation of the NameDatabase resource type.
//
// NameDatabase (0x60 bytes) — the ".ndb" name-generation resource
// (Game/Names/Cities.ndb, Female.ndb, etc). Extends Object::External (0x20 base).
// Registered by MenuScene_Setup (exe 0x14014cc40) with factory
// MenuScene_Setup_sub_c660 / cleanup MenuScene_Setup_sub_c6d0; vtable @ 0x14015eea0.
//
// It is loaded as part of the menu/scene resource closure during
// DialogLoaderLoopT::Load. In standalone it had only a NullFactory stub, so
// ExternalFactory::FindOrLoad got NULL back and crashed in IsDerivedFrom(NULL)
// (Runtime+0xC039A). This file provides the real factory + faithful Serialize.
//
// Layout (from factory 0xc660, dtor 0x1400a4790, Serialize 0x1400a45e0):
//   +0x00 vtable (0x14015eea0)
//   +0x08..+0x1F  Object::External base
//   +0x20 array1.data   (stride 0xc = 3 ints/elem)   +0x28 count  +0x2c cap
//   +0x30 array2.data   (stride 0x2 = uint16)        +0x38 count  +0x3c cap
//   +0x40 array3.data   (stride 0x1 = byte buffer)   +0x48 count  +0x4c cap
//   +0x50 array4.data   (stride 0x4 = int)           +0x58 count  +0x5c cap
//
// Serialize (vtable +0x28, exe 0x1400a45e0) — per array: serialize the count int;
// if loading (serializer stream mode == 0) resize the array; then serialize the
// elements. array1 is a per-element loop (SerializeBytes(4)+Serialize(byte)+
// Serialize(uint)); arrays 2/3/4 are bulk serializes. GetVersion (vtable +0x18,
// exe 0x1400a4780) = 0x401 (verified: `mov eax,0x401; ret`).
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/NameDatabase.h, field
// layout and retail slot map there) with a compiler-generated vtable. Gone: the
// hand-built 9-slot `g_NDB_Vtable`, its stubs, the lazily-resolved resize
// pointers (a non-atomic first-call cache, B6-5) and the C entry points.

#include "Game/NameDatabase.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "collection/Collection.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <cstdint>
#include <cstdio>
#include <new>

// Array resize helpers — the bodies registered at the four exe addresses
// (address_registry.cpp), called directly. The old lazily-resolved pointers
// (a non-atomic first-call cache, B6-5) resolved to exactly these.

// ============================================================
// NameDatabase::NameDatabase — factory sub_c660 body
// ============================================================
// External() and the 0x14015eea0 stamp are the compiler's; the four
// {data,count,cap} array headers at +0x20..+0x5F are zeroed.
// The four Collection::Array members' ctors are the zero stores.
NameDatabase::NameDatabase() {}

// ============================================================
// NameDatabase::Serialize (vtable +0x28) — faithful port of 0x1400a45e0
// ============================================================
bool NameDatabase::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // Loading when the serializer's stream direction is 0 (IO::Serializer::
    // GetDirection, retail 0x180006210). (The old test of the serializer's
    // stream pointer for null is gone: it holds the stream reference above.)
    auto isLoading = [&]() -> bool {
        return static_cast<int>(ser.GetDirection()) == 0;
    };

    // --- array1: stride 0xc (3 fields/elem) ---
    int c1 = _array1.GetSize();
    ser.Serialize(c1, nullptr);
    if (isLoading()) _array1.SetSize(c1);   // 0x14001c7c0
    if (c1 > 0) {
        NameDatabaseEntry* p = _array1.GetData();   // element0
        for (int i = 0; i < c1; ++i) {
            ser.SerializeBytes(4, &p[i].context, nullptr);   // field0: 4 raw bytes
            ser.Serialize(p[i].isWordEnd, nullptr);           // field1: uchar
            ser.Serialize(p[i].nextCharMask, nullptr);           // field2: uint
        }
    }

    // --- array2: stride 0x2 (uint16) — bulk ---
    int c2 = _array2.GetSize();
    ser.Serialize(c2, nullptr);
    if (isLoading()) _array2.SetSize(c2);   // 0x14001c890
    ser.Serialize(c2, _array2.GetData(), nullptr);

    // --- array3: stride 0x1 (byte buffer) — bulk ---
    int c3 = _array3.GetSize();
    ser.Serialize(c3, nullptr);
    if (isLoading()) _array3.SetSize(c3);   // 0x14000a590
    ser.Serialize(c3, _array3.GetData(), nullptr);

    // --- array4: stride 0x4 (int) — bulk ---
    int c4 = _array4.GetSize();
    ser.Serialize(c4, nullptr);
    if (isLoading()) _array4.SetSize(c4);   // 0x1400021d0
    ser.Serialize(c4, _array4.GetData(), nullptr);

    return true;
}

// ============================================================
// NameDatabase::~NameDatabase / DeletingDtor (vtable +0x00)
// ============================================================
// Mirrors 0x14001c790 -> FUN_1400a4790: four trivial-element
// `Collection::Array::~Array`s, back to front (+0x50 int32, +0x40 byte, +0x30,
// +0x20) — the compiler's member destruction. External::~External is the
// compiler's chain.
NameDatabase::~NameDatabase() {}

// NameDatabase::Reconstruct (vtable +0x30, 0x14001c730): dtor(self,0) then re-init.
Object::External* NameDatabase::Reconstruct() {
    this->~NameDatabase();
    return ::new (static_cast<void*>(this)) NameDatabase();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void NameDatabase::Destruct() { this->~NameDatabase(); }

// [8] 0x14001c6d0 — placement-new of the class on `this`.
Object::External* NameDatabase::Construct() {
    return ::new (static_cast<void*>(this)) NameDatabase();
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
::Object::Base* NameDatabase::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NameDatabase), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) NameDatabase());
}
::Object::Base* NameDatabase::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) NameDatabase());
}


DEFINE_EXE_TYPE_EXT(NameDatabase, "Object::External", "ndb")
