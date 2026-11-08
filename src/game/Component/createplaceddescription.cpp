// src/game/Component/createplaceddescription.cpp
// Clean reimplementation of CreatePlacedDescription (entity description block).
//
// CreatePlacedDescription (0x58 bytes) — single-placement build tool parameters.
// Extends Object::External (0x20 base). Vtable at 0x140162cf0.
// Parent type "Description".
//
// Factory  : 0x140032a60  (MemAlloc 0x58 + ctor 0x140119a60)
// CreateInPlace: 0x140032aa0
// Ctor defaults (from CreatePlacedDescription_Ctor 0x140119a60):
//   int   @ +0x20 = 0   (enum: placement category)
//   String@ +0x28 = 0   (empty)
//   void* @ +0x30 = 0   (int[] array base — enum elements, stride 4)
//   int   @ +0x38 = 0   (array count, cap @+0x3c)
//   int   @ +0x40 = 0
//   int   @ +0x44 = 1
//   int   @ +0x48 = 1
//   void* @ +0x50 = 0   (ExternalRef: created description, weak)
// Serialize (vtable +0x28, 0x1401198d0), stream order:
//   enum         @ +0x20                 (encoding vtable +0x98 dispatch)
//   int count    @ +0x38                 (+ resize int[] @+0x30 on load)
//   for each element (stride 4 at base +0x30):
//     enum       @ elem                  (encoding vtable +0x98 dispatch)
//   String       @ +0x28
//   int          @ +0x44
//   int          @ +0x48
//   int          @ +0x40
//   ExternalRef  @ +0x50                 (FUN_140002340)
// GetVersion (vtable +0x18, 0x1401199f0): mov eax,4; ret -> file version 4.
// Dtor (vtable +0x00, 0x140032ad0): vtable reset + MemFree(array @+0x30) +
//   String::~String(+0x28) + External::~External + free.
//
// STANDALONE notes: fresh-load int[] alloc mirrors FUN_1400021d0 (stride 4);
// per-element + +0x20 enums use the serializer-encoding +0x98 dispatch;
// the +0x50 ExternalRef reuses the generic ExternalPtr serialize reimpl.
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreatePlacedDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162cf0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreatePlacedDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "system/MemoryAllocator.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)


// ctor (inlined into create 0x140032a60 / createInPlace 0x140032aa0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CreatePlacedDescription::CreatePlacedDescription()
    : Description(),
      _category(0),
      _footprintRotation(0),
      _width(1),
      _height(1),
      _created(nullptr) {}

// dtor (slot 0 0x140032ad0 minus the stamps): the body is empty. ~_placeArray frees the
// int[] array unconditionally and zeroes +0x30 / +0x38 (0x140032af7..0x140032b07), then
// ~_name (0x140032b0f): reverse declaration order. The compiler then chains
// ~Description -> ~External.
CreatePlacedDescription::~CreatePlacedDescription() {
}

// [5] 0x1401198d0: stream order as retail.
bool CreatePlacedDescription::Serialize(IO::Stream& stream) {
    if (!::System::MemoryAllocator::_singleton) return true;   // standalone: the allocator is unwired
    IO::Serializer ser(stream, 0);
    ser.Serialize(_category, nullptr);
    // int[] count @ +0x38, base @ +0x30 (stride 4).
    //
    // Retail (0x1401198D0) serializes a LOCAL COPY of the count, resizes the array
    // ONLY on the load leg, and then walks the existing elements:
    //     local = *(uint*)(this+0x38);
    //     Serializer::Serialize(&ser, &local, 0);
    //     if (*(int*)(ser+8) == 0) FUN_1400021D0(this+0x30, local);   // load only
    //     p = *(longlong*)(this+0x30);
    //     for (n = local; n; --n) { enc->vt[0x98](enc, p); p += 4; }
    //
    // This copy used to (a) MemAlloc a brand-new array on EVERY pass, discarding the
    // one already parsed, and (b) zero each element immediately before the encoding
    // call. Both are additions, and together they are destructive: an already-loaded
    // resource gets its Serialize re-invoked through ExternalFactory::Add's
    // VisitorStream pass (NOT on every FindOrLoad — see the corrected mechanism note in
    // builddescription.cpp), and on any pass where the encoding does not write the
    // element back, the zero we just stored is what survives.
    //
    // Measured on Template/WoodHouse.rsc: the first parse produced the correct
    // `_placeBitArray` [0x14, 0x17] (Normal|Obstacle, Normal|Obstacle|Fast|Faster),
    // but by placement time the same description read [0x0, 0x0]. Those values are the
    // per-cell terrain-mask override in ToolTile_FootprintValidity_d580 (0x14009D580):
    // with them zero, `code` is 0 for every footprint cell, no terrain type is ever
    // allowed, and the building can never be placed anywhere.
    int count = _placeArray._size;
    ser.Serialize(count, nullptr);
    
    // Retail gates the resize on its load flag: `if (*(int*)(local_20 + 8) == 0)`, where
    // local_20 is the Serializer's first qword = the Stream, so the test is stream+8
    // ([[reference_serializer_plus8_is_encoding_not_loading_flag]]).
    //
    // [STANDALONE DEVIATION] We resize on the INVARIANT that gate exists to guarantee —
    // "the array is big enough before the element loop" — instead of on the flag itself.
    // Measured: our resource pipeline reaches this function with passes where stream+8 is
    // non-zero on an instance whose array is still null (the re-parse / reference-mode
    // behaviour documented in [[reference_reference_mode_corrupts_first_parse]] and
    // builddescription.cpp). Trusting the flag there left arr == null with count == 2, so
    // the element loop was skipped, the stream desynced, and the process crashed.
    // Resizing only when the array cannot already hold `count` keeps an already-parsed
    // array — values intact — which is the whole point of the fix above.
    uint32_t* arr = _placeArray._data;
    if (count > 0 && !arr) {
        // No buffer yet: allocate directly instead of going through the shared resize.
        // Reimpl_Array_Resize_Stride04 (our 0x1400021D0) reads the RECORDED count as
        // `oldCount` and copies that many elements from the old pointer, so handing it a
        // null data pointer with a non-zero count makes it read from address 0. Retail
        // never sees that state; ours could, so keep the shared helper off this path.
        arr = static_cast<uint32_t*>(::System::MemoryAllocator::MemAlloc((uint64_t)count * 4, 4));
        _placeArray._data     = arr;
        _placeArray._size     = count;
        _placeArray._capacity = count;
        // Zeroing a FRESH buffer is right — its elements are about to be read from the
        // stream. Zeroing an ALREADY-PARSED one is the bug this fix removes.
        if (arr) for (int i = 0; i < count; ++i) arr[i] = 0;
    } else if (count > 0 && _placeArray._capacity < count) {
        // exe 0x1400021d0 IS Collection::Array<int32>::SetSize(n), called here at
        // 14011993d: uint32_t's trait-derived row — the grown tail is left as the allocation found
        // it, like retail. Safe: this branch is only taken when the buffer cannot
        // already hold `count`, i.e. the array has not been parsed yet, and the element
        // loop right below reads all `count` slots from the stream into it.
        _placeArray.SetSize(count);
        arr = _placeArray._data;
    }
    for (int i = 0; i < count && arr; ++i) {
        ser.Serialize(arr[i], nullptr);   // enum element
    }
    _name.Serialize(ser);
    ser.Serialize(_width, nullptr);
    ser.Serialize(_height, nullptr);
    ser.Serialize(_footprintRotation, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _created);
    return true;
}

// [6] 0x140032ab0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreatePlacedDescription::Reconstruct() {
    this->~CreatePlacedDescription();
    return new (static_cast<void*>(this)) CreatePlacedDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreatePlacedDescription::Destruct() { this->~CreatePlacedDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreatePlacedDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x140032aa0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreatePlacedDescription::Construct() {
    return new (static_cast<void*>(this)) CreatePlacedDescription();
}

// create 0x140032a60: MemAlloc(0x58, 8), then the (inlined) ctor.
::Object::Base* CreatePlacedDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreatePlacedDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreatePlacedDescription());
}
// createInPlace 0x140032aa0.
::Object::Base* CreatePlacedDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreatePlacedDescription());
}

DEFINE_EXE_TYPE(CreatePlacedDescription, "Description")
