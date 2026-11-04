// ============================================================================
// src/engine/object/internal_class.cpp — ENDGAME B.3.3 step 5
// ============================================================================
//
// `Object::Internal` and `Object::InternalFactory` as real classes, under the
// retail names. The measured vtable, the layouts and the naming evidence are in
// src/engine/object/Internal.h; the design rules are
// docs/history/endgame_b/B33_RULES.md §1, §5, §6, §7 and §9 step 5.
//
// WHY THIS FILE IS SEPARATE FROM internal.cpp
// -------------------------------------------
// The same split src/engine/object/type_reflection.cpp has against
// type_system.cpp: internal.cpp owns the REGISTRY — the `RtObject_*` ABI shims,
// the mangled-name doors, the attendance counters and the vtable-pin screen.
// None of that can link into `make unit` (tests/unit/rt_port_stub.cpp stubs
// `ResolveExport`/`ResolveAddr` but not `RtResolveCached`). The CLASSES have to
// be unit-testable — B33_RULES §9 step 5 asks for the slot order, the
// `Type()`/`TypeName()` chain, `Destroy(p)` and the factory table logic — so the
// bodies live here behind one `-DBANISHED_UNIT_TEST` seam and the doors stay
// there.
//
// WHAT DID **NOT** MOVE, AND WHY
// ------------------------------
//   * `??0InternalFactory@Object@@QEAA@XZ` (0x1800c0030) is registered from
//     src/engine/engine/singleton_ctors.cpp and `??1InternalFactory@Object@@`
//     (0x18002f860) from src/engine/object/factory_dtors.cpp. Neither file is
//     in this step's set, so the class declares neither: a second spelling of a
//     body that is already registered at those two addresses is exactly the
//     drift B31_RULES' two-step shim pattern exists to avoid.
//   * `?Clone@Internal@Object@@QEAAPEAV12@XZ` (0x1800bfb10) *** IS PORTED AS OF
//     ENDGAME B.3.4 STREAMS (6/6) *** and is declared on the class. This
//     paragraph used to read "is UNPORTED — nothing in the tree registers it by
//     name or by address", which was true and was the ONLY place in the tree
//     that said so. Its blocker was named here as `IO::MemoryStream`, i.e.
//     B.3 step 4; B34_RULES §6.5(e) re-measured that blocker as ALREADY
//     SATISFIED (all eight slots of ??_7MemoryStream@ have had real bodies
//     since Wave 9/19) and scheduled the port as its own small commit.
//     The body is at the bottom of this file and the registered door is
//     `RtObject_Internal_Clone` in internal.cpp — the symbol's FIRST
//     registration, by name AND by address.
//   * B.3.4 block 3 LANDED `IO::Serializer`, so every `Serialize`/persist body
//     here now goes through the real class instead of `RtObject::SerializerLite`
//     (which is deleted). What did NOT change is the numbers: the **retail**
//     ReaderWriter vtable byte offsets, now spelled `rt::io::rw_vtbl::k*`,
//     still DIFFER in the profile build, and the raw slot calls stay raw until
//     nothing hands a Serializer a minted-table ReaderWriter (B34_RULES §3.1,
//     §4.2). Nothing here is cross-checked against the profile disassembly.
//
// No C++ exceptions anywhere in this file (PLAN.md §4.3 risk 5).
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>   // B.3.4 STREAMS (6/6): Internal::Clone's memmove
#include <new>       // B.3.4 block 3: ResetPersistState placement-constructs
                     // its IO::Serializer (see the note there)

#include "object/Internal.h"
#include "io/Stream.h"   // IO::MemoryStream, Internal::Clone's two
                                     // stream locals.

#if !defined(BANISHED_UNIT_TEST)
#include "system/MemoryAllocator.h"
#include "system/Value.h"   // B3R: System::Random::Rand by name
#endif

namespace Object {

// ============================================================================
// The seam
// ============================================================================
//
// Retail's `Collection::Array` growth inside this class goes through the
// MemoryAllocator STATICS, not through the container's own allocator policy:
// every inlined copy is `EnterCS(_singleton+0x640); Allocate(...); LeaveCS`,
// which is precisely `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`
// (0x1800c7a50) with the align argument threaded through. This step does not
// move that door — it only gives the bodies a home on the class.
//
// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) is the same body as
// the class-scoped `operator delete` at 0x180006a10 that `Internal`'s slot 0
// tail-calls (proved for all 88 such deletes in
// src/engine/object/class_operator_delete.cpp's banner), so slot 0's free arm
// goes through this one door rather than pinning an 89th copy.
namespace detail {

#if defined(BANISHED_UNIT_TEST)

// The host has no MemoryAllocator singleton and no registry. `align` is ignored
// because malloc already returns storage aligned for any scalar.
void* InternalMemAlloc(uint64_t bytes, int /*align*/) { return std::malloc((size_t)bytes); }
void  InternalMemFree(void* p)                        { std::free(p); }

// `?Rand@Random@System@@QEAAIXZ` (0x1800c7f90) is ported in
// src/engine/system/value_types.cpp, which stands on the registry and is not
// linked into `make unit`. A test that wants to observe the generation draw
// installs this hook for its duration and clears it again — the same shape
// tests/unit/rt_port_stub.cpp's `g_unitResolveAddr` has, and the same shape
// tests/unit/array_test.cpp already drives. With no hook installed the draw is
// 0, which is what `_generation` is initialised to anyway.
uint32_t (*g_unitInternalRandHook)(System::Random*) = nullptr;

uint32_t InternalRand(System::Random* random) {
    return g_unitInternalRandHook ? g_unitInternalRandHook(random) : 0u;
}

#else

// B3R W0-A0: the allocator's statics, called directly (they were resolved by
// name through RT_EXPORT_ONCE, which was bound to these same two bodies).
void* InternalMemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);   // 0x1800c7a50
}
void InternalMemFree(void* p) {
    System::MemoryAllocator::MemFree(p);                      // 0x1800c7ad0
}
// Wave 2 ported this and proved it bit-exact over 16,035,064 calls.
uint32_t InternalRand(System::Random* random) {
    return random->Rand();   // 0x1800c7f90
}

#endif

// ============================================================================
// Collection::Array<T> bodies, as retail instantiates them
// ============================================================================
// Moved here VERBATIM from src/engine/object/internal.cpp (Wave 3, lane B);
// the only edit is the element type's spelling, `RtObject::Internal*` ->
// `Object::Internal*`, which is the same eight bytes.
//
// WAVE2's warning is live: node/element offsets are a function of sizeof(T), so
// MSVC emits a SEPARATE body per instantiation and one ported body cannot serve
// two element widths. Retail has four distinct ones in this class alone and all
// four are transcribed separately below with their own addresses. They are NOT
// re-expressed as `Collection::Array<T>::SetSize` (B32_RULES' "the template
// WRAPS the ported core" cuts the other way here): the container template binds
// to `Collection::DefaultAllocator`, and retail's inlined growth here binds to
// the MemoryAllocator statics above. Changing which heap these buffers come
// from is not this step's business, and the resave gate would see it.
//
// The `if (slot != nullptr)` tests are RETAIL's, not ours — the template's
// per-element address check surviving into codegen. They can never fire for a
// non-null buffer. Kept because this family is byte-faithful first.

// FUN_18002ffa0 — Array<Internal*>::SetSize(int newSize, const Internal*& fill)
void ArrayPtrSetSizeFill(Collection::Array<Internal*>* a, int newSize, Internal* const* fill) {
    const int oldSize = a->_size;
    if (newSize == oldSize)
        return;
    Internal** old = a->_data;
    a->_size = newSize;
    if (a->_capacity < newSize) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0)
            a->_capacity = newSize * 3;
        a->_data = static_cast<Internal**>(  // LEFT: FLOOR? allocation: InternalMemAlloc returns void* (the tier-I MemAlloc shape does not match this allocator)
            InternalMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity) << 3), 8));
    }
    int i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        Internal** slot = a->_data + i;
        if (slot != nullptr)
            *slot = (i < oldSize) ? old[i] : *fill;
    }
    if (old != a->_data && old != nullptr)
        InternalMemFree(old);
}

// FUN_18002fca0 — Array<Internal*>::SetSize(int newSize), no fill value.
// Only the surviving prefix is copied; a grown tail is left UNINITIALISED,
// which is why retail only ever calls this one to SHRINK.
void ArrayPtrSetSize(Collection::Array<Internal*>* a, int newSize) {
    const int oldSize = a->_size;
    if (newSize == oldSize)
        return;
    Internal** old = a->_data;
    a->_size = newSize;
    if (a->_capacity < newSize) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0)
            a->_capacity = newSize * 3;
        a->_data = static_cast<Internal**>(  // LEFT: FLOOR? allocation: InternalMemAlloc returns void* (the tier-I MemAlloc shape does not match this allocator)
            InternalMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity) << 3), 8));
    }
    int i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        if (i < oldSize) {
            Internal** slot = a->_data + i;
            if (slot != nullptr)
                *slot = old[i];
        }
    }
    if (old != a->_data && old != nullptr)
        InternalMemFree(old);
}

// FUN_18002fd90 — Array<Internal*>::Reserve(int n). Grows CAPACITY only: it sets
// _size to n while it reallocates and restores the old _size on the way out, and
// it returns immediately unless n is strictly greater than _size.
void ArrayPtrReserve(Collection::Array<Internal*>* a, int n) {
    const int oldSize = a->_size;
    if (oldSize >= n)
        return;
    Internal** old = a->_data;
    a->_size = n;
    if (a->_capacity < n) {
        a->_capacity = n;
        if (n == oldSize + 1 && oldSize != 0)
            a->_capacity = n * 3;
        a->_data = static_cast<Internal**>(  // LEFT: FLOOR? allocation: InternalMemAlloc returns void* (the tier-I MemAlloc shape does not match this allocator)
            InternalMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity) << 3), 8));
    }
    if (old != a->_data) {
        for (int i = 0; i < oldSize; ++i) {
            Internal** slot = a->_data + i;
            if (slot != nullptr)
                *slot = old[i];
        }
        if (old != nullptr)
            InternalMemFree(old);
    }
    a->_size = oldSize;
}

// FUN_18002fea0 — Array<int32_t>::SetSize(int newSize). Element width 4,
// alignment 4 (Collection.h: the alignment argument is alignof(T), not
// sizeof(T)); otherwise identical to ArrayPtrSetSize.
void ArrayIntSetSize(Collection::Array<int32_t>* a, int newSize) {
    const int oldSize = a->_size;
    if (newSize == oldSize)
        return;
    int32_t* old = a->_data;
    a->_size = newSize;
    if (a->_capacity < newSize) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0)
            a->_capacity = newSize * 3;
        a->_data = static_cast<int32_t*>(  // LEFT: InternalMemAlloc is the inlined Array<int32_t> growth allocator (void*); an allocation result by meaning, not by the tier-I name shape
            InternalMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity) << 2), 4));
    }
    int i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        if (i < oldSize) {
            int32_t* slot = a->_data + i;
            if (slot != nullptr)
                *slot = old[i];
        }
    }
    if (old != a->_data && old != nullptr)
        InternalMemFree(old);
}

}  // namespace detail

// ============================================================================
// Object::Internal — the reflection block
// ============================================================================
//
// Retail's own macro expansion, legible in the export table as one contiguous
// per-class block:
//
//   ?_TypeName@Internal@Object@@CAPEBDXZ          0x18002fc60  LEA RAX,[0x180155b10]  -> "Internal"
//   ?_ParentTypeName@Internal@Object@@CAPEBDXZ    0x18002fc70  LEA RAX,[0x180155ad0]  -> "Base"
//   ?Type@Internal@Object@@UEBAIXZ                0x1800064d0  MOV EAX,[0x1801f9c18]  (slot 2)
//   ?TypeId@Internal@Object@@SAIXZ                0x18002fc50  MOV EAX,[0x1801f9c18]
//   ?TypeName@Internal@Object@@UEBAPEBDXZ         0x1800064c0  MOV RAX,[0x1801f9c20]  (slot 1)
//   ?CreateNewObject@Internal@Object@@SAPEAVBase@2@XZ      0x18002fc80  XOR EAX,EAX; RET
//   ?CreateInPlaceObject@Internal@Object@@SAPEAVBase@2@PEAX@Z 0x1800070c0 XOR EAX,EAX; RET
//   ?_typeInfo@Internal@Object@@0VBaseTypeInfo@2@A         0x1801f9c18
//
// 0x1801f9c18 is `_typeInfo` + 0x00 (typeId) and 0x1801f9c20 is `_typeInfo` +
// 0x08 (name) — which is what identifies those two two-instruction bodies as the
// macro's, and is why `DECLARE_TYPE` reproduces them over the class's own
// record rather than over a pair of hand-named globals.
//
// *** BOTH FACTORY FUNCTIONS RETURN NULL IN RETAIL. *** `Object::Internal` is
// never created through `Factory::CreateObject`; only its subclasses are. The
// two `XOR EAX,EAX` bodies are transcribed as `return nullptr`, NOT as an
// abort: `InternalFactory::PersistObject`'s read path calls `factoryFn` and then
// dereferences the result unguarded, so a save naming "Internal" itself faults
// exactly where retail faults.
//
// THE ONE RECORD (ENDGAME B.3.9 OBJ, B39_RULES D6 "the duplicate"). Until this
// block the mint ALSO minted `?_typeInfo@Internal@Object@@0VBaseTypeInfo@2@A`
// (0x1801f9c18) and spliced it in front of this one, so `Factory::Startup` saw
// the name twice and its `Insert` silently kept the minted node. That row is
// retired (tools/rt_mint_retired/obj.txt): this CRT-init record is the only
// `Internal` in `_firstTypeInfo`, `Factory::Startup` inserts it, and every
// reader of the type id -- `Internal::TypeId()`/`Type()`, the minted
// `??_7Internal@Object@@6B@` slot doors (object_typeinfo_accessors.cpp),
// `PersistObject` and `Clone` below -- reads it. Its factory pointers are the
// real `return nullptr` bodies, which is what retail has.
// tests/unit/b39_obj_typeinfo_unique_test.cpp keeps it the only one.
DEFINE_TYPE(Object::Internal, "Base", nullptr)

// 0x18002fc80 / 0x1800070c0 — `XOR EAX,EAX; RET`, both of them.
Base* Internal::CreateNewObject()              { return nullptr; }
Base* Internal::CreateInPlaceObject(void*)     { return nullptr; }

// ============================================================================
// Object::Internal — lifetime
// ============================================================================

// ??0Internal@Object@@QEAA@XZ   retail 0x1800bfd10 / profile 0x18008b370
//
// The two vptr stores retail makes at 0x1800bfd2b and 0x1800bfd35 are the
// constructor chain (Base's, then Internal's own) and GCC emits both for us.
// They are NOT written by hand: a hand-written store would name the MINTED
// table address, and an object built by this constructor carries the COMPILER's.
// tests/unit/object_vtable_abi_test.cpp is why that is safe — the two tables are
// slot-for-slot interchangeable in both directions.
Internal::Internal() {
    _factoryIndex = -1;   // 0x1800bfd3f — dead: AddObject overwrites it
    _generation   = 0;    // 0x1800bfd46 — dead likewise. Reproduced anyway.
    InternalFactory::Get()->AddObject(this);
}

// ??0Internal@Object@@QEAA@AEBV01@@Z   retail 0x1800064e0
// It copies the two dwords and NOTHING ELSE — no AddObject. See the header.
Internal::Internal(const Internal& other)
    : Base(),
      _factoryIndex(other._factoryIndex),
      _generation(other._generation) {}

// ??4Internal@Object@@QEAAAEAV01@AEBV01@@Z   retail 0x180006430
Internal& Internal::operator=(const Internal& other) {
    _factoryIndex = other._factoryIndex;   // 0x180006430
    _generation   = other._generation;     // 0x180006436
    return *this;                          // MOV RAX,RCX
}

// ??1Internal@Object@@UEAA@XZ   retail 0x1800bfdb0 / profile 0x18008b400
//
// The `vptr = &Internal::_vftable_` at 0x1800bfdc6 and the
// `vptr = &Base::_vftable_` at 0x1800bfe13 are the destructor chain, emitted by
// the compiler. The only body is the two lines below.
Internal::~Internal() {
    _generation = 0;                                 // 0x1800bfdd0
    InternalFactory::Get()->RemoveObject(this);      // 0x1800bfdd7..0x1800bfe0d
}

// ---- vtable slot 3 — 0x1800bfda0, a bare RET in retail AND in the profile
// build. It exists so a subclass has something to override. MUST STAY EMPTY.
void Internal::Persist(IO::Stream&/*stream*/) {
}

// ============================================================================
// Object::Internal::Clone()                              retail 0x1800bfb10
//                                       ?Clone@Internal@Object@@QEAAPEAV12@XZ
// ============================================================================
// ENDGAME B.3.4 STREAMS (6/6), B34_BRIEFS §C.5. Transcribed from the 130
// shipped instructions at 0x1800bfb10..0x1800bfd02, not from the decompiled C
// — the decompiler prints the two stream locals as eighteen unrelated
// `local_NN`s and drops the arguments of all three indirect calls.
//
// A SERIALIZE ROUND-TRIP DEEP COPY, in six steps:
//
//   1. ask the `Object::Factory` for a fresh object of THIS object's own type;
//   2. keep it only if it derives from `Internal`, else keep NULL;
//   3. build a WRITE MemoryStream over a fresh 0x1000-byte block and call
//      slot 3 (`Persist`) on `this` — the object writes itself into it;
//   4. build a READ MemoryStream over a SECOND fresh 0x1000-byte block and
//      copy the first block into it;
//   5. call slot 3 on the NEW object — it reads itself back out;
//   6. destroy both streams (read one first) and return the new object.
//
// ---------------------------------------------------------------------------
// THE NULL ARM IS RETAIL'S AND IS NOT GUARDED
// ---------------------------------------------------------------------------
// `CMOVNZ R14,RBX` at 0x1800bfb88 leaves R14 at ZERO when `IsDerivedFrom` says
// no — and 0x1800bfc75 then does `MOV RAX,[R14]` on it, i.e. it loads a vptr
// from address 0 and calls slot 3. There is NO branch around it. So a `Clone`
// of a type whose factory produces a non-`Internal` FAULTS, and a `Clone` of a
// type the Factory does not know faults one step earlier inside
// `CreateObject`'s unguarded descent. Both are transcribed. The project rule
// applies in full: retail's unguarded derefs are load-bearing, and a null check
// here would not restore the behaviour, it would skip it and hand the caller a
// half-built object instead.
//
// ---------------------------------------------------------------------------
// THREE THINGS THE SHAPE WOULD GET WRONG
// ---------------------------------------------------------------------------
//  a) THE COPY IS SIZED BY THE STREAM'S `size` FIELD (+0x28), NOT BY ITS
//     CURSOR. 0x1800bfc65 is `MOV R8D,[RBP+0x7]`, which is the write stream's
//     `size`, so all 0x1000 bytes are copied whatever `Persist` actually wrote.
//     Copying `cursor` bytes would be the sensible thing and is not what the
//     machine does.
//  b) THE COPY IS `memmove`. 0x18008a290 is the DLL's statically linked
//     `memmove`, established from its prologue by src/engine/system/error.cpp
//     :152 and repeated by saveload_loadmeta.cpp:342 and by
//     `?Read@TextFileStream@IO@@` (B.3.4 4/6).
//  c) THE SECOND BLOCK IS A SEPARATE 0x1000 ALLOCATION, not the first one
//     handed over. Both streams have `ownsData = 1`, so BOTH blocks are freed
//     by the two destructors — the round trip leaks nothing and shares nothing.
//
// The two allocations are `EnterCriticalSection(_singleton+0x640);
// Allocate(_singleton, 0x1000, 4); LeaveCriticalSection(...)` in the shipped
// bytes, which this file's own banner already identifies as
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` with the align argument
// threaded through — so they go through `detail::InternalMemAlloc`, the door
// every other allocation in this file uses.
//
// FIELD-STORE ORDER IS RETAIL'S: data, size, cursor, direction, ownsData, for
// both streams, after the constructor has run and overwriting what it wrote.
//
// Not compiled into `make unit`: the host links no IO::MemoryStream bodies
// (its vtable's slots live in compressedstream.cpp) and nothing there calls
// `Clone`, which needs a live Object::Factory and a real Persist override.
#if !defined(BANISHED_UNIT_TEST)
Internal* Internal::Clone() {
    // 0x1800bfb35 loads the singleton BEFORE the virtual call at 0x1800bfb3f.
    // Order kept — the same order `Base::IsDerivedFrom` preserves and for the
    // same reason: it is what the disassembly says.
    const Factory* const factory = Factory::Get();   // [0x1801fc6e8]
    const uint32_t typeId = Type();                  // slot 2, +0x10

    // 0x1800bfb42..0x1800bfb6f is `Factory::CreateObject(typeId)` OPEN-CODED by
    // MSVC: the same AVL descent on `_types` (key at +0x00, left +0x38, right
    // +0x40), then `node+0x10` is `record.info` and `info+0x20` is the static
    // `factoryFn`, called with NO arguments. That is instruction for
    // instruction what type_reflection.cpp's `Factory::CreateObject` does, so
    // the member is called rather than the descent inlined a second time. (Its
    // port-only miss arm, review F-144, is unreachable here: a live object's own
    // type is registered.)
    Base* const created = factory->CreateObject(typeId);

    // 0x1800bfb75 reads `?_typeInfo@Internal@Object@@` + 0x00, which is the
    // type id; 0x1800bfb7e is `?IsDerivedFrom@Base@Object@@QEBA_NI@Z`;
    // 0x1800bfb88 is the CMOVNZ. See the banner on the null arm.
    Base* const clone = created->IsDerivedFrom(Internal::TypeId()) ? created
                                                                  : nullptr;

    // ---- the WRITE stream: 0x1800bfb8c .. 0x1800bfbfc ----------------------
    // Retail inlines ??0MemoryStream@IO@@QEAA@XZ (0x1800ae710) here: the local
    // is the real class, its constructor runs at 0x1800bfb90, and the five
    // stores below overwrite what it wrote.
    IO::MemoryStream writeStream;                         // 0x1800bfb90
    void* const writeBuf = detail::InternalMemAlloc(0x1000, 4);  // 0x1800bfbc4
    writeStream._data      = writeBuf;                    // 0x1800bfbd5  +0x18
    writeStream._size      = 0x1000;                      // 0x1800bfbd9  +0x28
    writeStream._cursor    = 0;                           // 0x1800bfbe0  +0x24
    writeStream._direction = 1;                           // 0x1800bfbe7  +0x08
    writeStream._ownsData  = 1;                           // 0x1800bfbee  +0x20
    Persist(writeStream);                                 // 0x1800bfbfc slot 3

    // ---- the READ stream: 0x1800bfbff .. 0x1800bfc7f -----------------------
    IO::MemoryStream readStream;                          // 0x1800bfc03
    void* const readBuf = detail::InternalMemAlloc(0x1000, 4);   // 0x1800bfc37
    readStream._data      = readBuf;                      // 0x1800bfc48  +0x18
    readStream._size      = 0x1000;                       // 0x1800bfc4c  +0x28
    readStream._cursor    = 0;                            // 0x1800bfc53  +0x24
    readStream._direction = 0;                            // 0x1800bfc5a  +0x08
    readStream._ownsData  = 1;                            // 0x1800bfc61  +0x20

    // `size`, not `cursor`; `memmove`, not `memcpy`. See (a) and (b).
    std::memmove(readBuf, writeBuf, writeStream._size);   // 0x1800bfc70

    // 0x1800bfc75 `MOV RAX,[R14]` on a possibly-NULL R14, then slot 3. The
    // downcast is well formed whatever the value: `Internal` derives from
    // `Base` non-virtually at offset 0, which every raw slot call in this tree
    // already assumes.
    Internal* const result = static_cast<Internal*>(clone);
    result->Persist(readStream);                          // 0x1800bfc7f slot 3

    // ---- the two inlined destructors, READ FIRST ---------------------------
    // 0x1800bfc83..0x1800bfcbc restamps 0x1801555b0 then 0x1801549a0 and frees
    // `data` (guarded by `ownsData`) and then `encoding`; 0x1800bfcbd..
    // 0x1800bfce7 is the same again for the write stream. That IS
    // `??1MemoryStream@IO@@UEAA@XZ` (0x1800ae890) on each local, in reverse
    // declaration order: the two locals' destructors at the closing brace.
    return result;                                        // 0x1800bfce8 (R14)
}
#endif  // !BANISHED_UNIT_TEST

// ============================================================================
// Object::InternalFactory
// ============================================================================

// ?_singleton@InternalFactory@Object@@0PEAV12@EA — retail 0x1801fc6d8. The
// class static (ENDGAME B.3.9 OBJ, B39_RULES D7); the minted row is redirected
// onto it in RtObjectPortInit, so the name and address doors answer these
// bytes. The mint never zeroes or re-seeds it; the language zero-initialises
// it and the ctor door publishes into it.
InternalFactory* InternalFactory::_singleton = nullptr;

// ?Get@InternalFactory@Object@@SAPEAV12@XZ   retail 0x18002f850 —
// `MOV RAX,[0x1801fc6d8]; RET`.
#if defined(BANISHED_UNIT_TEST)
namespace detail {
// A unit test builds its own factory and installs it for the duration of a
// case. Production takes the door below; this pointer does not exist there.
InternalFactory* g_unitInternalFactory = nullptr;
}  // namespace detail
InternalFactory* InternalFactory::Get() { return detail::g_unitInternalFactory; }
#else
InternalFactory* InternalFactory::Get() { return _singleton; }
#endif

// ?Startup@InternalFactory@Object@@QEAA_NXZ   retail 0x1800c0020 — `MOV AL,1; RET`.
bool InternalFactory::Startup() { return true; }

// ?Shutdown@InternalFactory@Object@@QEAAXXZ   retail 0x1800c0010 — bare `RET`.
// EMPTY IN RETAIL. The handle table is not drained at shutdown; every object
// removes itself in its own destructor. Do not "complete" this.
void InternalFactory::Shutdown() {}

// ?AddObject@InternalFactory@Object@@QEAAXPEAVInternal@2@@Z   retail 0x1800c00c0
//
//   0x1800c00cf  EAX  = _objects._size
//   0x1800c00d9  obj->_factoryIndex = EAX
//   0x1800c00df  CALL 0x1800c7f90  Random::Rand(&_random)     (RCX = this + 0x20)
//   0x1800c00e4  obj->_generation = EAX
//   0x1800c00f4  CALL 0x18002ffa0  _objects.SetSize(_size + 1, &obj)
//
// The fill argument is the ADDRESS OF THE SPILLED `obj` ARGUMENT (LEA R8,[RSP+0x38]),
// i.e. a `const Internal*&`, which is why the local below exists.
void InternalFactory::AddObject(Internal* obj) {
    obj->_factoryIndex = _objects._size;
    obj->_generation   = detail::InternalRand(&_random);
    Internal* elem = obj;
    detail::ArrayPtrSetSizeFill(&_objects, _objects._size + 1, &elem);
}

// ?RemoveObject@InternalFactory@Object@@QEAAXPEAVInternal@2@@Z  retail 0x1800c0110
//
// The swap-remove reference_internalfactory_swapremove_wild_write.md is about.
// THERE IS NO VALIDATION OF `idx` — not here and not in the profile build. If
// `idx` is stale the second store lands wherever the allocator has since put
// that block. That is retail's behaviour and this port keeps it; the fix for the
// family is upstream (unregister before freeing), never a guard here.
void InternalFactory::RemoveObject(Internal* obj) {
    const int last = _objects._size - 1;      // 0x1800c011b  DEC R10D
    const int idx  = obj->_factoryIndex;      // 0x1800c0114  MOVSXD RDX,[RDX+8]
    if (last != idx) {
        _objects._data[idx] = _objects._data[last];
        _objects._data[idx]->_factoryIndex = idx;
    }
    detail::ArrayPtrSetSize(&_objects, last); // 0x1800c0149  JMP 0x18002fca0
}

// ?ClearPersistState@InternalFactory@Object@@QEAAXXZ
//                                      retail 0x1800bfe30 / profile 0x18008c0b0
// Frees the persist-id array and zeroes all three of its fields. Retail inlines
// the free (FUN_18001c750); the profile build calls MemFree — and it calls it
// UNCONDITIONALLY, where retail's inline null-checks first. Same behaviour:
// MemoryAllocator::Free opens with `test rdx,rdx; jz`.
//
// It touches `_persistIds` and NEVER `_objects`, which is why it is not part of
// the handle-table hazard family: `_persistIds` has exactly two writers in the
// whole engine, this function and `ResetPersistState`.
void InternalFactory::ClearPersistState() {
    if (_persistIds._data != nullptr)
        detail::InternalMemFree(_persistIds._data);
    _persistIds._data     = nullptr;
    _persistIds._size     = 0;
    _persistIds._capacity = 0;
}

#if !defined(BANISHED_UNIT_TEST)

// ?ResetPersistState@InternalFactory@Object@@QEAAXAEAVStream@IO@@@Z
//                                      retail 0x1800bfe40 / profile 0x18008bfc0
//
//     Serializer ser(stream, 0);              // inlined; see below
//     int count = _objects._size;
//     ser.readerWriter->vtbl[0xa0](rw, &count, 0);   // WRITE on save, READ on load
//     _persistIds.SetSize(count);
//     _objects.Reserve(count);
//     for (i = 0; i < _persistIds._size; ++i) _persistIds[i] = -1;
//     ~Serializer(ser);                       // rw->vtbl[0](rw, 1)
//
// ARITY, closed from the bytes at 0x1800bfe68-0x1800bfe84 (Wave 9 lane Y):
//     MOV RCX,[RDX+0x10]   ; arg1 = stream->encoding (the factory)
//     XOR R8D,R8D          ; arg3 = 0                <-- SET BEFORE the test
//     CMP [RDX+0x8],EBX    ; direction == 0 ?
//     LEA RDX,[RSP+0x28]   ; arg2 = &serializer
//     CALL [RAX+0x18] / CALL [RAX+0x20]
// THREE arguments, identical on both branches, third a literal 0 — the
// serializer VERSION, which this call site does not use. Wave 4's IO::Serializer
// port pinned the same arity from the out-of-line 0x1800b9960, where
// BinaryWriter STORES that int and writes it to the stream.
//
// *** THE READERWRITER VTABLE OFFSETS ARE BUILD-SPECIFIC. *** `kScalarInt`
// below is 0xa0, the RETAIL number. The PROFILE build uses 0xb8 for the same
// call. Copying the profile number writes the right value through the wrong
// slot and desyncs every save.
//
// ENDGAME B.3.4 block 3 — THE INLINED SERIALIZER IS NOW A REAL `IO::Serializer`,
// AND ITS CTOR/DTOR POSITIONS ARE UNCHANGED. That was the precondition for
// converting it at all (block 3's brief: convert only if the positions are
// PROVABLY identical), and it holds here because the function is straight-line
// with no early return:
//
//   * CONSTRUCTION replaces the hand-inlined `{store stream, null the
//     readerWriter, read direction, read encoding, call factory slot 3 or 4
//     with (factory, &ser, 0)}` — which is instruction for instruction what
//     `IO::Serializer::Serializer(Stream&, int)` does. The literal `0` version
//     is retail's `XOR R8D,R8D` at 0x1800bfe6c and still reaches the factory as
//     the third argument.
//   * DESTRUCTION replaces the trailing `if (enc) encVtbl[0](enc, 1)` at the
//     SAME statement position — `~Serializer()` is that call, guard included
//     (the guard is retail's own `TEST RCX,RCX`, not one we added).
//
// The Serializer is a LOCAL: retail's inlined destructor (the slot-0 call at
// 0x1800bfef8) is the last call before the RET at 0x1800bff09, which is where
// the local's destructor runs. The body has no early return; keep it so --
// `~IO::Serializer` is NOT idempotent (it reads four more bytes on the read
// side and back-patches the placeholder on the write side).
void InternalFactory::ResetPersistState(IO::Stream& stream) {
    const int kSerializerVersion = 0;   // retail's XOR R8D,R8D at 0x1800bfe6c

    ::IO::Serializer ser(stream, kSerializerVersion);

    int count = _objects._size;
    ser._readerWriter->SerializeInt(count, nullptr);   // +0xa0 (B3X W0: by name)

    detail::ArrayIntSetSize(&_persistIds, count);
    detail::ArrayPtrReserve(&_objects, count);

    // The bound is re-read from _persistIds._size on every iteration, as retail
    // does; it cannot change, but this family is byte-faithful first.
    for (int i = 0; i < _persistIds._size; ++i)
        _persistIds._data[i] = -1;
}   // ~Serializer: 0x1800bfef8

#endif  // !BANISHED_UNIT_TEST

// PersistObject builds in the unit too (review_object_index_bounds_test.cpp): its read arm
// reaches only the stream's direction, the two arrays and, on a miss, the Factory type tree.

// ============================================================================
// ?PersistObject@InternalFactory@Object@@QEAAPEAVInternal@2@AEAVStream@IO@@IHI@Z
//                                                          retail 0x1800bff10
// ============================================================================
// Moved here UNCHANGED from src/engine/object/internalfactory_persist.cpp,
// whose banner carries the full read-out of both arms and is still the place to
// read it. The three things that must survive any future tidy-up are restated
// because each one is load-bearing:
//
//   * THE DESCENT'S MISS ARM PRODUCES A NULL NODE AND RETAIL DEREFERENCES IT.
//     `MOV [RSI+0xc],R15D` at address 0xc is how a save file naming an
//     unregistered type announces itself in retail. The port loads such a
//     record as a null pointer instead (review F-123 / F-145, NOT RETAIL notes
//     in the body): the dereference is undefined behaviour.
//   * `_persistIds._data` is RE-LOADED after the factory call (0x1800bffb4 /
//     0x1800bffbc) because the factory can have grown the table, and
//     `_objects._data` is RE-LOADED after the Serialize (0x1800bffea) because
//     Serialize can move the buffer. Caching either at the top returns a pointer
//     into a freed block.
//   * `_persistIds[i]` is a SENTINEL on the way in and an INDEX on the way out,
//     and the two arms store DIFFERENT things into it — the write path stores
//     `index` itself, the read path stores the NEW object's `_factoryIndex`.
//     That asymmetry is the function.
namespace {

// `?_singleton@Factory@Object@@0PEAV12@EA` (Runtime 0x1801fc6e8) is the class
// static `Object::Factory::_singleton` since ENDGAME B.3.9 OBJ (B39_RULES D7).
Factory* const* FactorySingletonSlot() {
    return &Factory::_singleton;
}

// (`Object::Internal`'s type id is `Internal::TypeId()`, the class record's
// +0x00, since ENDGAME B.3.9 OBJ retired the minted duplicate
// `?_typeInfo@Internal@Object@@` -- the one record. The coordinator measured
// that either record winning gives the same type tree and the same resave,
// b39_session/ORDER_MEASUREMENT.md variant C.)

// IO::Stream + 0x08 — `0 == reading`, non-zero == writing. The only field of
// Stream this body touches.
inline int32_t StreamMode(const IO::Stream* stream) {
    return stream->_direction;   // +0x08
}

// vtable slot 3 (+0x18) — `Object::Internal::Persist(IO::Stream&)`. B3X W0:
// called by name, `obj->Persist(stream)`; the minted `??_7Internal@Object@@6B@`
// that once made a raw slot read necessary was retired in B.3.9.

}  // namespace

Internal* InternalFactory::PersistObject(IO::Stream& stream, uint32_t generation,
                                         int index, uint32_t typeId) {
    int32_t* const persistIds = _persistIds._data;             // 0x1800bff27
    const int64_t i = index;                                   // MOVSXD RBX,R9D

    int32_t slot = index;

    if (StreamMode(&stream) != 0) {
        // ---- WRITE, 0x1800bffd4 --------------------------------------------
        if (persistIds[i] < 0) {
            persistIds[i] = static_cast<int32_t>(i);
            Internal* obj = _objects._data[i];
            obj->Persist(stream);                                     // slot 3 +0x18
        }
        // `slot` stays `index`: EBX is never reassigned on this arm.
    } else {
        // ---- READ, 0x1800bff3d ---------------------------------------------
        // NOT RETAIL (review F-022): `index` is the InternalPtr record's int from
        // the save, and retail indexes `_persistIds` with it unchecked. An index
        // outside the table ResetPersistState sized from the save's object count
        // is a corrupt record (an out-of-bounds read, and on a miss a write of the
        // new object's index past the buffer). It returns null without running
        // Persist, so the stream is NOT kept in step: on retail's miss path the
        // object's body follows inline, and the rest of the load parses those bytes
        // as later fields. The InternalPtr caller stores null where the record said
        // present. This only bounds the table access; it does not make the corrupt
        // save load. Every index a valid save holds is in range and takes retail's
        // path unchanged.
        if (index < 0 || index >= _persistIds._size)
            return nullptr;
        slot = persistIds[i];
        if (slot < 0) {
            // ---- the type-tree descent, 0x1800bff5f ------------------------
            using TypeNode = Factory::TypeSet::Node;
            Factory* const* const pSingleton = FactorySingletonSlot();
            TypeNode* node  = pSingleton ? (*pSingleton)->_types._root : nullptr;
            TypeNode* found = nullptr;
            while (node != nullptr) {
                if (typeId < node->element.typeId)  { node = node->left;  continue; }
                if (node->element.typeId >= typeId) { found = node; break; }   // equal
                node = node->right;
            }
            // A MISS LEAVES `found` NULL AND RETAIL DEREFERENCES IT (0x1800bff8c).
            // NOT RETAIL (review F-123): retail has no such check. The type id is
            // the save's; one that names no registered type is a read through
            // null (undefined behaviour). The record loads as a null pointer:
            // `_persistIds[i]` stays -1 and nothing is created.
            if (found == nullptr)
                return nullptr;
            ::Object::BaseTypeInfo* const info = found->element.record.info;   // node + 0x10
            // Zero-argument: retail leaves `info` in RCX at the call site but
            // the factory is a static that reads nothing.
            Base* const created = info->factoryFn();                  // CALL [RCX+0x20]
            // NOT RETAIL (review F-145): retail calls IsDerivedFrom on the result
            // unchecked (0x1800bff9f), a virtual call through null when the id names
            // an abstract type (its factory answers null). Loads as a null pointer.
            if (created == nullptr)
                return nullptr;

            const uint32_t internalTypeId = Internal::TypeId();       // [0x1801f9c18]
            Internal* obj =                                           // 0x1800c0390
                created->IsDerivedFrom(internalTypeId)
                                ? static_cast<Internal*>(created)     // B10: downcast after the type test
                                : nullptr;                            // CMOVNZ RSI,RBX
            // NOT RETAIL (review F-123): a registered type that is not an Internal
            // (an External resource type, say) leaves `obj` null, and retail reads
            // [RSI+0x8] through it (0x1800bffad). The object built for it is
            // destroyed (retail leaks it) and the record loads as a null pointer.
            if (obj == nullptr) {
                ::Object::Destroy(created);
                return nullptr;
            }

            // Retail reads `_factoryIndex` (+0x08) BEFORE
            // it writes `_generation` (+0x0c); two distinct fields, so the order
            // is unobservable, and it is kept because an unobservable order that
            // is transcribed costs nothing while one that is tidied is a
            // difference nobody can find.
            const int32_t newIndex = obj->_factoryIndex;              // 0x1800bffad
            obj->_generation       = generation;                      // 0x1800bffb0
            // `_persistIds` is RE-LOADED here (0x1800bffb4 / 0x1800bffbc).
            _persistIds._data[i] = newIndex;
            slot = _persistIds._data[i];

            obj->Persist(stream);                                     // 0x1800bffca  slot 3 +0x18
        }
    }

    // 0x1800bffea — `_objects._data` is re-read AFTER the Serialize.
    return _objects._data[static_cast<int64_t>(slot)];
}

}  // namespace Object
