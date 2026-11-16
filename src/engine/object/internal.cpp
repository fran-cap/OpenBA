// ============================================================================
// src/engine/object/internal.cpp — Object::Internal / InternalBasePtr /
//                                   InternalFactory   (Wave 3, lane B)
// ============================================================================
//
// Byte-faithful port of the decompilation at the retail address named above
// each function (Runtime-x64.dll, image base 0x180000000, read 2026-09-06).
// Layouts, evidence and the vtable dump are in
// src/engine/object/Internal.h — read that first.
// The full write-up, including the GO/NO-GO verdict, is
// docs/history/runtime_port/WAVE3_LANE_B.md.
//
// ORIGINAL FILE NAME. The profile build's source-tree strings name
// `Object\InternalFactory.cpp` and the headers `Object/InternalFactory.h` and
// `Object/InternalPtr.h`. There is no `Object\Internal.cpp`. This file's name
// is the wave scaffolding's, not the engine's.
//
// No C++ exceptions anywhere in this file (PLAN.md §4.3 risk 5).
// ============================================================================

#include <cstdio>
#include <cstring>

#include "object/Internal.h"

namespace RtObject {
namespace {

// ============================================================
// ENDGAME B.3.3 step 5 — the allocator, the PRNG and the live singleton MOVED
// ============================================================
// `MemAlloc`/`MemFree`/`RandomRand` and the `?_singleton@InternalFactory@...`
// door used to live here. They are now `Object::detail::InternalMemAlloc` /
// `InternalMemFree` / `InternalRand` and `Object::InternalFactory::Get()`
// (src/engine/object/internal_class.cpp), which the unit build links.
//
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z  0x1800c7a50
//   ?MemFree@MemoryAllocator@System@@SAXPEAX@Z     0x1800c7ad0
//   ?Rand@Random@System@@QEAAIXZ                   0x1800c7f90
//   ?_singleton@InternalFactory@Object@@0PEAV12@EA 0x1801fc6d8

// ============================================================
// The two exported vtables -- GONE (ENDGAME B.3.9 OBJ)
// ============================================================
// `VtblInternal()` / `VtblBase()` served the `??0Internal` / `??1Internal`
// doors' hand stamps of the minted tables. The doors went with the minted
// `??_7Internal@Object@@6B@` row and its RT_DELDTOR row; every Internal is
// built by `Object::Internal::Internal()` and wears the compiler's table.


// ============================================================
// Collection::Array<T> bodies, as retail instantiates them — MOVED
// ============================================================
// All four (FUN_18002ffa0, FUN_18002fca0, FUN_18002fd90, FUN_18002fea0) are now
// `Object::detail::ArrayPtrSetSizeFill` / `ArrayPtrSetSize` / `ArrayPtrReserve`
// / `ArrayIntSetSize` in src/engine/object/internal_class.cpp, moved VERBATIM
// with the element spelling changed from `RtObject::Internal*` to
// `Object::Internal*` — the same eight bytes. They moved because
// `InternalFactory::AddObject`/`RemoveObject`/`ResetPersistState` are the only
// callers and those are now the real class's members. WAVE2's warning stands
// where they live now: one ported body cannot serve two element widths.


}  // namespace

// ============================================================================
// TIER 1 — no shared state
// ============================================================================

// ?Persist@Internal@Object@@UEAAXAEAVStream@IO@@@Z (0x1800bfda0, slot 3):
// ENDGAME B.3.9 OBJ -- the slot door went with the minted table, its only
// reader; the body is `Object::Internal::Persist` (internal_class.cpp).

// ============================================================================
// Internal's constructor and destructor — the bodies are in internal_class.cpp.
// ============================================================================

// ??0Internal@Object@@QEAA@XZ          retail 0x1800bfd10, profile 0x18008b370
//
// Retail inlines Base::Base and InternalFactory::AddObject; the profile build
// calls both out of line and proves what the inline is:
//     Base::Base(this);                    // vptr = &Base::_vftable_
//     this->vptr = &Internal::_vftable_;
//     this->_factoryIndex = -1;
//     this->_generation   = 0;
//     AddObject(_singleton, this);         // 0x1800c00c0
// and AddObject is
//     obj->_factoryIndex = _objects._size;
//     obj->_generation   = _random.Rand();
//     _objects.SetSize(_objects._size + 1, obj);
//
// The `-1` / `0` pair is a dead store in both builds — AddObject overwrites
// both before anything can observe them — and is reproduced anyway.
// ??0Internal@Object@@QEAA@XZ (0x1800bfd10) / ??1Internal@Object@@UEAA@XZ
// (0x1800bfdb0): ENDGAME B.3.9 OBJ -- the two doors went with the minted
// `??_7Internal@Object@@6B@` row, its RT_DELDTOR row 0x180006440 (the ??1
// door's caller) and import slot 0x140152768 (the ??0 door's). The bodies are
// `Object::Internal::Internal()` / `~Internal()` (internal_class.cpp); the
// [RtInternalTable] / [RtInternalVtbl] instruments went with them (D9).

}  // namespace RtObject

// ============================================================================
// ENDGAME B.3.1 step 1 — the real class's out-of-line body.
//
// `?Serialize@InternalBasePtr@Object@@QEAAXAEAVSerializer@IO@@@Z`
//   retail 0x1800c0180, profile 0x18008c460.
//
// It takes the real `IO::Serializer` and calls the ReaderWriter's Serialize
// members, `Type()` and `InternalFactory::PersistObject` by name. No new casts.
//
//     bool present = (_ptr != 0);
//     rw->Serialize(present);                      // rw vtbl +0x78
//     if (!present) { _ptr = 0; return; }
//     index = _ptr ? _ptr->_factoryIndex : 0;
//     type  = _ptr ? _ptr->Type() : 0xffffffff;    // Internal vtbl slot 2
//     gen   = _ptr ? _ptr->_generation : 0;
//     rw->Serialize(index);                        // +0xa0, int
//     rw->Serialize(gen);                          // +0x98, uint
//     rw->Serialize(type);                         // +0x98, uint
//     _ptr = InternalFactory::PersistObject(_singleton, ser.stream, gen, index, type);
//
// The redundant `_ptr ? … : …` guards are retail's: `present` already proved
// the pointer non-null and the compiler kept the tests anyway.
//
// *** THE READERWRITER VTABLE OFFSETS ARE BUILD-SPECIFIC. *** These are the
// RETAIL numbers (0x78 / 0x98 / 0xa0, agreeing with the forwarder table in
// src/engine/io/serializer.cpp). The PROFILE build uses 0x90 / 0xb0 / 0xb8 for
// the same three calls; transcribing those writes the right values through the
// wrong slots and desyncs every save.
//
// NOT DIFFABLE and NOT SHADOWED by the byte oracle: the call advances a stream,
// so a second run would consume or emit the record twice. Wave 9 lane S
// VERIFIED it with the emission-sequence oracle — 4 emissions on the non-null
// branch and 1 on the null branch, identical to the shipped body.
// ============================================================================
namespace Object {

// B.3.4 block 3 — the parameter is the REAL `IO::Serializer`. B3X W0: the minted
// tables are retired (B.3.9), so the ReaderWriter and the object's Type() are
// called by name. The three retail offsets (0x78 / 0x98 / 0xa0) are spelled
// `rt::io::rw_vtbl::k*` in io/Serializer.h.
void InternalBasePtrSerializeBody(Internal** self,
                                  ::IO::Serializer& ser) {
    ::IO::ReaderWriter* const enc = ser._readerWriter;

    // A one-byte bool through the bool slot (+0x78), as retail passes it.
    bool present = (*self != nullptr);
    enc->SerializeBool(present, nullptr);
    if (!present) {
        *self = nullptr;
        return;
    }

    int      index;
    uint32_t type;
    Internal* p = *self;
    if (p == nullptr) {
        index = 0;
        type  = ::Object::kInvalidTypeId;
    } else {
        index = p->_factoryIndex;
        type = p->Type();   // slot 2 +0x10
    }
    uint32_t gen = (*self != nullptr) ? (*self)->_generation : 0u;   // written by SerializeUInt on a load

    enc->SerializeInt(index, nullptr);                          // +0xa0
    enc->SerializeUInt(gen, nullptr);                           // +0x98
    enc->SerializeUInt(type, nullptr);                          // +0x98

    // ?PersistObject@InternalFactory@Object@@ (0x1800bff10), the member by
    // name (B3R RT-Core) on `Object::InternalFactory::Get()`.
    *self = ::Object::InternalFactory::Get()->PersistObject(*ser._stream, gen, index, type);
}

}  // namespace Object
