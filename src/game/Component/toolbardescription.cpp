// src/game/Component/toolbardescription.cpp
// Clean reimplementation of ToolbarDescription (entity sub-description).
//
// ToolbarDescription (0x68 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x140164980.  GetVersion = 4.
//
// NOTE: this is an exe-defined type parented to "Description", NOT the Runtime
// UI::ElementDescription. UIDescription/ToolbarDescription register their own
// factory in the exe; they are not Runtime-self-registered.
//
// Field layout (from Ctor 0x14012dc30 + Serialize 0x14012da30 + dtor 0x14012db70):
//   +0x20: SpriteSheet* _spriteSheet ExternalPtr<UI::SpriteSheet>  (default 0)
//   +0x28: String  _name                                           (default empty)
//   +0x30: StringTable* _stringTable ExternalPtr<UI::StringTable> (default 0)
//   +0x38: String  _str1                                           (default empty)
//   +0x40: String  _str2                                           (default empty)
//   +0x48: String  _str3                                           (default empty)
//   +0x50: String* _entries._data String array buffer              (default 0)
//   +0x58: int32   _entries._size                                   (default 0)
//   +0x5c: int32   _entries._capacity                               (default 0)
//   +0x60: int32   _val0          (default -1)
//   +0x64: int32   _val1          (default 0)
//
// Serialize (0x14012da30) stream order:
//   1. ExternalPtr (+0x20)               FUN_14000bd30
//   2. String (+0x28)
//   3. ExternalPtr (+0x30)               FUN_14000bf40 (StringTable variant)
//   4. String (+0x38)
//   5. String (+0x40)
//   6. String (+0x48)
//   7. int count (+0x58)
//   8. (load) resize String array (+0x50) to count   FUN_140028860
//   9. loop count: String::Serialize(entries[i])
//   10. int (+0x64)
//   11. int (+0x60)
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (ToolbarDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140164980) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ToolbarDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"   // Collection::Array (the typed view of the raw triple)
#include <cstdint>
#include <new>
#include "Component/exe_bodies.h"
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// ctor (inlined into create 0x140039490 / createInPlace 0x1400394d0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ToolbarDescription::ToolbarDescription()
    : Description(),
      _spriteSheet(nullptr),
      _stringTable(nullptr),
      _entries(),
      _val0(-1),
      _val1(0) {}

// dtor (slot 0 0x140039500 minus the stamps; body 0x14012db70): ~String over the
// +0x50 array's count (0x14012dba1 tests the count only), MemFree(+0x50), zero
// +0x50 / +0x58, then ~String +0x48 / +0x40 / +0x38 / +0x28. That is the members'
// own destructors in reverse declaration order (~Array first); the compiler then
// chains ~Description -> ~External.
ToolbarDescription::~ToolbarDescription() {}

// [5] 0x14012da30: stream order as retail.
bool ToolbarDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // +0x20 in the exe uses FUN_14000bd30 (ExternalPtr<UI::SpriteSheet>); +0x30 uses
    // FUN_14000bf40 (ExternalPtr<UI::StringTable>). Both have a byte-identical stream
    // layout (only the validated typeId differs). Each goes through
    // ::Object::SerializeExternal<T> with its own T::GetTypeInfo().
    ::Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);   // 1. _spriteSheet
    _name.Serialize(ser);                        // 2. _name
    ::Object::SerializeExternal<UI::StringTable>(ser, _stringTable);   // 3. _stringTable
    _str1.Serialize(ser);                        // 4.
    _str2.Serialize(ser);                        // 5.
    _str3.Serialize(ser);                        // 6.
    
    // 7. count — serialize through a LOCAL, exactly like the exe (local_res8), NOT
    //    straight into +0x58. +0x58 is the resize's own count field: if we wrote the
    //    new count there first, the resize below would see oldCount==newCount and
    //    skip the allocation (leaving +0x50 NULL — the "cost ???" bug). The resize
    //    itself writes +0x58 once it has grown the buffer.
    int count = _entries._size;
    ser.Serialize(count, nullptr);
    
    // 8. on load, resize the String array (+0x50) to count (Array<System::String>::SetSize,
    //    0x140028860; otherwise +0x50 stays NULL and the two cost/status strings are
    //    never deserialized — the "cost ???" bug).
    //
    // ENDGAME B.3.4a closing — the same one-field-off read as
    // ambientemitterdescription.cpp: `*(int*)(*(void**)(serializer + 8) + 8)`
    // is ReaderWriter::_allowCustomSerialize, always 0, so `loading` was stuck
    // TRUE and the String array was resized on the SAVE leg as well.
    // Retail reads the direction off serializer +0x00 (the Stream).
    // ToolbarDescription::Serialize, Application-x64.exe 0x14012da30, local
    // Serializer at [RSP+0x28]:
    //     14012dacd  MOV RAX,qword ptr [RSP + 0x28]   ; serializer+0x00 = Stream*
    //     14012dad2  CMP dword ptr [RAX + 0x8],0x0    ; stream+0x08 = direction
    //     14012dad6  JNZ 0x14012dae5                  ; nonzero -> SKIP the resize
    //     14012dad8  MOV EDX,dword ptr [RSP + 0x50]   ; the count
    //     14012dadc  LEA RCX,[RSI + 0x50]
    //     14012dae0  CALL 0x140028860                 ; Array<String>::SetSize
    // Byte-for-byte the same five instructions as 0x1400431f0.
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);
    if (loading) {
        _entries.SetSize(count);   // Array<System::String>::SetSize 0x140028860
    }
    
    // 9. per-element String::Serialize (stride 8)
    System::String* buf = _entries._data;
    if (buf) {
        for (int i = 0; i < count; ++i) {
            buf[i].Serialize(ser);
        }
    }
    
    // 10/11. two ints (note: +0x64 BEFORE +0x60 in the original stream)
    ser.Serialize(_val1, nullptr);
    ser.Serialize(_val0, nullptr);
    return true;
}

// [6] 0x1400394e0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ToolbarDescription::Reconstruct() {
    this->~ToolbarDescription();
    return new (static_cast<void*>(this)) ToolbarDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ToolbarDescription::Destruct() { this->~ToolbarDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* ToolbarDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x1400394d0: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* ToolbarDescription::Construct() {
    return new (static_cast<void*>(this)) ToolbarDescription();
}

// create 0x140039490: MemAlloc(0x68, 8), then the (inlined) ctor.
::Object::Base* ToolbarDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ToolbarDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ToolbarDescription());
}
// createInPlace 0x1400394d0.
::Object::Base* ToolbarDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ToolbarDescription());
}

DEFINE_EXE_TYPE(ToolbarDescription, "Description")
