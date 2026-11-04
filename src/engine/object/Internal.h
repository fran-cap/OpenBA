#pragma once
// ============================================================================
// src/engine/object/Internal.h — Object::Internal / InternalBasePtr /
//                                   InternalFactory  (Wave 3, lane B)
// ============================================================================
//
// The engine-wide handle table. EVERY Object::Internal-derived object — every
// UI::Element, every DialogInstance, every AchievementInstance, every
// GoalInstance — enters this table in its constructor and leaves it in its
// destructor, and `InternalPtr<T>` (the 103 template instantiations in the
// export table) is a bare pointer plus this table's index/generation pair,
// used ONLY by serialization.
//
// EVIDENCE. Every offset below was read out of a decompiled Runtime-x64.dll
// body and the body is named next to it. Where the retail build inlines a
// template, the PROFILE build (Runtime-x64-profile.dll, asserts on) is quoted
// as the independent witness — it calls the same helpers out of line and its
// assert strings name the fields.
//
// ORIGINAL SOURCE FILES (profile-build strings, docs/runtime_port/
// original_source_tree.txt): there is NO `Object\Internal.cpp`. The three
// classes live in
//     Object\InternalFactory.cpp
//     C:\Projects\Buildy\code\Runtime\Object/InternalFactory.h   (assert at :39)
//     c:\projects\buildy\code\runtime\Object/InternalPtr.h
// so `src/engine/object/internal.cpp` is our name, not theirs. Recorded here
// because the file-name mirror in src/engine/object/README.md is otherwise a
// lie for this lane.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"
#include "../system/Value.h"          // ::Random (0x9c4 bytes, index + mt[624])
// ENDGAME B.3.4 block 3: `RtObject::SerializerLite` — a fourth spelling of
// IO::Serializer's 16 bytes, declared here so that src/engine/object/ need not
// depend on src/engine/io/ — is GONE, and this is the dependency it was
// avoiding. GLOBAL SCOPE, above `namespace RtObject {`, never inside an `#if`
// (B31 hygiene).
#include "../io/Serializer.h"
// ENDGAME B.3.3 step 7: internal_class.h is merged back in below, so this
// header needs Object::Base / BaseTypeInfo / DECLARE_TYPE. GLOBAL SCOPE —
// the split existed only because External.h used to include TypeInfo.h from
// inside `namespace RtObject`, and that include is gone (External.h:43
// hoisted it in step 5).
#include "TypeInfo.h"


// ============================================================
// Object::Internal — 16 bytes.
//
//   +0x00  void*    vptr           (Object::Base's only member)
//   +0x08  int32    _factoryIndex  own slot in InternalFactory::_objects
//   +0x0c  uint32   _generation    InternalFactory::_random.Rand() at birth
//
// Pinned by ??0Internal@Object@@QEAA@XZ (retail 0x1800bfd10, profile
// 0x18008b370): after the two vtable stores it writes `-1` at +0x08 and `0` at
// +0x0c, then overwrites +0x08 with the factory's element count and +0x0c with
// a Rand() draw. ??1Internal (retail 0x1800bfdb0) reads +0x08 as the swap-
// remove index and zeroes +0x0c.
//
// There is no field past +0x0c: the profile ctor is literally
// `Base::Base(this); vptr = &Internal::_vftable_; _factoryIndex = -1;
//  _generation = 0; AddObject(_singleton, this);` and Base contributes only the
// vptr.
//
// VTABLE — 4 slots, and it is EXPORTED (??_7Internal@Object@@6B@,
// 0x180154720), so a port stamps the DLL's own vtable by name and never owns
// one of its own. Read out of .rdata at that address:
//     [0] 0x180006440  scalar deleting destructor  (flag 1 => operator delete)
//     [1] 0x1800064c0  TypeName()  -> const char*
//     [2] 0x1800064d0  Type()      -> unsigned int   (the type id)
//     [3] 0x1800bfda0  Persist(IO::Stream&)          (EMPTY in both builds)
// Object::Base's vtable (??_7Base@Object@@6B@, 0x180154788) has the first
// three. Slot indices are confirmed by two independent consumers:
// InternalBasePtr::Serialize calls `vtbl[0x10]` = slot 2 for the type id, and
// InternalFactory::PersistObject calls `vtbl[0x18]` = slot 3 to persist a
// freshly created object.
// ============================================================
// ENDGAME B.3.3 step 5. This POD is now an ALIAS for the real class
// `Object::Internal` below, and it must STAY TRIVIALLY DESTRUCTIBLE: the ported
// bodies in src/engine/object/ hold `RtObject::Internal` by pointer and the
// registered `RtObject_Internal_Dtor` shim is called BY HAND out of a vtable
// slot. Give the POD a destructor and the compiler runs a second one —
// B31_RULES' `RtSystem::String` double-free lesson, verbatim. The assertion
// itself is below, next to <type_traits>.

// ============================================================
// Object::InternalBasePtr — 8 bytes, one raw pointer. NOT refcounted, NOT
// weak, NOT validated.
//
// Pinned by all four of its trivial members, each a single instruction:
//   ??0InternalBasePtr@Object@@QEAA@XZ            0x1800062c0  `*this = 0`
//   ??0InternalBasePtr@Object@@QEAA@PEAVInternal@1@@Z 0x1800062a0 `*this = arg`
//   ?Copy@InternalBasePtr@Object@@IEAAXAEBV12@@Z  0x1800062b0  `*this = *arg`
//   ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ    0x1800c0150  reads only +0x00
//
// *** CheckPtr AND AssignId ARE BARE `RET`s IN BOTH BUILDS. ***
// ?CheckPtr@InternalBasePtr@Object@@IEBAXXZ (retail 0x1800c0290, profile
// 0x18008c5d0) and ?AssignId@InternalBasePtr@Object@@IEAAXXZ (retail
// 0x1800c0280, profile 0x18008c5e0) contain nothing but `ret` — in the PROFILE
// build too, which is the build with System::Debug::Assert calls compiled in
// and 5,182 exports. So this is NOT "a debug-build leftover that got optimised
// out of retail" (the claim in engine/funcs/engine_funcs.h:1983): the bodies
// were compiled out at the SOURCE level in every shipped configuration, and no
// build of this engine has ever validated an InternalBasePtr.
//
// The consequence, stated plainly because a lot of our code is written as if
// the opposite were true: a stale InternalBasePtr NEVER self-invalidates, in
// retail or here. The handle table exists for SERIALIZATION — Serialize writes
// {index, generation, typeId} and InternalFactory::PersistObject resolves them
// back — not for weak-reference safety. Retail clears references explicitly at
// removal time and relies on that.
// ============================================================

// ============================================================
// Object::InternalFactory — 0x9e4 (2532) bytes.
//
//   +0x00  Collection::Array<Internal*> _objects      {data, size, capacity}
//   +0x10  Collection::Array<int32_t>   _persistIds   {data, size, capacity}
//   +0x20  System::Random               _random       (0x9c4 bytes)
//
// _objects is pinned by ??0InternalFactory (0x1800c0030, zeroes +0x00/+0x08),
// AddObject (0x1800c00c0, `SetSize(_objects, _size + 1, &obj)`), RemoveObject
// (0x1800c0110, `table[i] = table[last]` off +0x00 with the count at +0x08),
// and the exe-side audit that has been walking it for months
// (src/game/Object/internal_factory_audit.h).
//
// _persistIds is pinned by ClearPersistState (0x1800bfe30 — retail inlines
// `Free(_data); _data = 0; *(u64*)(this+0x18) = 0`, and the PROFILE build,
// 0x18008c0b0, spells it out as `MemFree(*(void**)(this+0x10)); *(u64*)(this+
// 0x10) = 0; *(u64*)(this+0x18) = 0`) and by ResetPersistState (0x1800bfe40),
// which resizes it to the object count and fills it with -1 through a 4-byte
// stride — element size 4. The profile build's assert inside that fill loop is
// "index >= 0 && index < _size" from Collection/Array.h, bounding on the dword
// at +0x18, which is therefore the SIZE field of the array at +0x10.
//
// _random at +0x20 is pinned twice: the ctor seeds 624 words starting at +0x24
// with the standard MT init_genrand recurrence (so index is the dword at
// +0x20 and mt[] starts at +0x24, exactly ::Random's layout), and both the
// ctor and AddObject call `System::Random::Rand((Random*)(this + 0x20))`.
//
// SIZE is pinned exactly by the copy constructor
// ??0InternalFactory@Object@@QEAA@AEBV01@@Z (0x18002f890): after copy-assigning
// the two arrays it copies the tail from +0x20 with a 16-byte-aligned unrolled
// loop (19 x 0x80 bytes, then 4 x 0x10, then a trailing dword) whose scalar
// fallback is `memcpy(dst + 0x20, src + 0x20, 0x9c4)`. So the last BYTE the
// class occupies is 0x20 + 0x9c4 - 1 = 0x9e3, and because the type contains a
// pointer its alignment is 8, giving sizeof == 0x9e8 with 4 bytes of trailing
// pad. Nothing reads that pad.
//
// The singleton is `?_singleton@InternalFactory@Object@@0PEAV12@EA`, an
// exported DATA symbol at 0x1801fc6d8 holding an InternalFactory*. The factory
// is NOT embedded in Engine the way the allocator is: Engine::GetInternalFactory
// (0x18000c910) returns `*(InternalFactory**)(engine + 0x6a8)` — a pointer
// member, not an embedded object.
// ============================================================

// ============================================================
// `RtObject::SerializerLite` AND `kEncSerialize{Bool,Uint,Int}` LIVED HERE AND
// ARE GONE — ENDGAME B.3.4 block 3.
//
// `SerializerLite` was a FOURTH declaration of IO::Serializer's 16 bytes
// (beside `rt::io::Serializer`, `RtIo::Serializer` and the class itself), made
// so that src/engine/object/ need not include src/engine/io/. `IO::Serializer`
// is now a real, non-polymorphic class in src/engine/io/Serializer.h, this
// header includes it at global scope above, and the five files that used the
// alias take the real class. B34_RULES §1.5.4 / §3.3.
//
// The three constants were the RETAIL `ReaderWriter` vtable byte offsets under
// a second spelling. They are now `rt::io::rw_vtbl::kScalarBool` (0x78),
// `kScalarUInt` (0x98) and `kScalarInt` (0xa0) — ONE spelling, in the header
// that pins the whole 26-slot table. *** The warning they carried is still
// live and now belongs to `rw_vtbl`: these offsets DIFFER BETWEEN BUILDS. The
// PROFILE build's `InternalBasePtr::Serialize` (0x18008c460) uses +0x90 /
// +0xb0 / +0xb8 for the same three calls, because its ReaderWriter vtable
// carries extra virtuals. Transcribing the profile numbers writes the right
// values through the wrong slots and desyncs every save. ***
//
// `rw_vtbl` itself STAYS for the whole of B.3.4: 54 files across eleven
// src/engine/* directories spell those offsets, and their classes are B.3
// steps 5-8 (§3.3).
// ============================================================

// B3X W0: the `kInternalVtbl*` byte-offset enum (0x00 / 0x08 / 0x10 / 0x18 =
// DeletingDtor / TypeName / Type / Persist) lost its last reader when
// internal.cpp began calling `Type()` by name; it is retired with it.


// ============================================================================
// ENDGAME B.3.1 step 1 -- the REAL classes, under retail's names.
// ============================================================================
//
// `RtObject::InternalBasePtr` above stays declared (B31_RULES step 1: every
// existing vocabulary keeps working; the POD goes when its last user does --
// today src/engine/achievement/goal.cpp:411 and the C shims below).
//
// TRIVIALLY COPYABLE AND TRIVIALLY DESTRUCTIBLE, ON PURPOSE AND BY ASSERTION.
// `Copy` (0x1800062b0) is `mov rax,[rdx]; mov [rcx],rax` -- a bare 8-byte copy,
// after which the two pointers are ALIASES. There is no refcount anywhere in
// this family: `InternalFactory::_objects` plus an explicit `new`/`delete` owns
// every `Internal`, and the {index, generation, typeId} triple exists for SAVE
// FILES, not for weak-reference safety (SMARTPTR_INVENTORY.md sections 1, 5.2).
// The `Collection::Array`/`LinkedList` bulk paths move these eight bytes with a
// plain qword copy, so a user-provided copy ctor or dtor here would be silently
// bypassed (section 5.4).
//
// *** CheckPtr AND AssignId ARE BARE `RET`s IN EVERY BUILD, PROFILE INCLUDED. ***
// They are transcribed below as empty bodies and MUST STAY EMPTY: retail never
// validates a stale handle, it relies on explicit clearing at removal time, and
// adding validation on deref would diverge from every save file and every
// caller (memory reference_checkptr_is_a_ret_in_every_build; hazard 5.1).
//
// RETAIL ADDRESSES (retail / profile):
//   ??0InternalBasePtr@Object@@QEAA@XZ                 0x1800062c0 / 0x180023900
//   ??0InternalBasePtr@Object@@QEAA@PEAVInternal@1@@Z  0x1800062a0 / 0x1800238e0
//   ?Copy@InternalBasePtr@Object@@IEAAXAEBV12@@Z       0x1800062b0 / 0x1800257d0
//   ?CheckPtr@InternalBasePtr@Object@@IEBAXXZ          0x1800c0290 / 0x18008c5d0
//   ?AssignId@InternalBasePtr@Object@@IEAAXXZ          0x1800c0280 / 0x18008c5e0
//   ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ         0x1800c0150 / 0x18008c5a0
//   ?Serialize@InternalBasePtr@Object@@QEAAXAEAVSerializer@IO@@@Z
//                                                      0x1800c0180 / 0x18008c460
//   InternalPtr<T> array Serialize (ONE 180-byte body, two instantiations):
//     0x180009a20 Achievement::AchievementInstance
//     0x180009fa0 Achievement::GoalInstance
//   Re-verified 2026-09-13, function-size-bounded, with
//   tools/extptr_clone_check.py's diff: identical=55 position-only=2 REAL=1,
//   the one REAL instruction being the `_typeInfo` load. ONE BODY.
//
// There is no `Clone` for this family either -- copying IS `Copy`.
// ============================================================================

#include <new>
#include <type_traits>

namespace Object {

// Defined in src/engine/object/internal.cpp, next to the ReaderWriter vtable
// slots and the `PersistObject` name door it needs -- `IO::Serializer`,
// `Object::Base` and `InternalFactory` are all still unconverted
// (SMARTPTR_INVENTORY.md section 4), so the body keeps reaching them exactly as
// the C body does.
class Internal;   // below; `_ptr` holds one

void InternalBasePtrSerializeBody(Internal** self,
                                  ::IO::Serializer& ser);
// Object::Base::IsDerivedFrom's ancestor walk, reached by name from
// src/engine/object/type_system.cpp -- the typed half of InternalPtr<T>.
bool InternalPtrIsDerivedFrom(const Internal* obj, uint32_t typeId);

class InternalBasePtr {
public:
    // 0x1800062c0 -- one instruction, `mov qword ptr [rcx], 0`.
    InternalBasePtr() : _ptr(nullptr) {}
    // 0x1800062a0 -- one instruction, `mov qword ptr [rcx], rdx`.
    InternalBasePtr(Internal* p) : _ptr(p) {}
    // No store at all: a member that retail never zeroes before it is built in
    // place (a callee's hidden return slot pointed at it, or an explicit store
    // later in the owner's ctor body). Retail's InternalPtr has no ctor of its
    // own to run there; this is that absence (B.4 S4: DialogInstance::_root).
    struct NoStore {};
    explicit InternalBasePtr(NoStore) {}

    // Trivial by construction and by assertion; see the banner.
    InternalBasePtr(const InternalBasePtr&) = default;
    InternalBasePtr& operator=(const InternalBasePtr&) = default;
    ~InternalBasePtr() = default;

    Internal* Get() const        { return _ptr; }
    Internal* operator->() const { return _ptr; }
    Internal& operator*()  const { return *_ptr; }
    explicit operator bool() const { return _ptr != nullptr; }
    // The null test on the held Internal*, with no downcast. NOT RETAIL (review
    // F-143): `InternalPtr<T>` hides `Get()` with a static_cast to T*, so
    // `field.Get() != nullptr` on a save-loaded field casts an object that may not
    // be a T before the type test that exists to catch it, which is undefined
    // behaviour. A field read from a save is null-tested with this, then type-tested
    // with `IsDerivedFromNullSafe(field, id)`. Retail compares the raw qword.
    bool IsNull() const { return _ptr == nullptr; }
    bool operator==(const InternalBasePtr& o) const { return _ptr == o._ptr; }
    bool operator!=(const InternalBasePtr& o) const { return _ptr != o._ptr; }

    // ?DeletePtr@... 0x1800c0150 --
    //     p = _ptr; if (p) { p->vtbl[0](p, 1); _ptr = 0; }
    // Slot 0 with flag 1 is MSVC's scalar deleting destructor: it runs
    // ~Internal (and therefore InternalFactory's swap-remove) and frees the
    // block.
    //
    // B3X W0: the call is `delete` (slot 0, flags 1) BY NAME on `Object::Internal`.
    // The body is defined below `class Internal` (the class must be
    // complete for the member call); the declaration and signature are
    // unchanged for every InternalPtr<T> instantiation.
    inline void DeletePtr();

    // ?Serialize@... 0x1800c0180. Writes/reads {present, index, generation,
    // type} through the ReaderWriter vtable and resolves the triple back
    // through InternalFactory::PersistObject on load.
    void Serialize(::IO::Serializer& ser) {
        InternalBasePtrSerializeBody(&_ptr, ser);
    }

    // ---- the transitional entry points the C shims call --------------------
    // `Copy`, `CheckPtr` and `AssignId` are PROTECTED in retail (the `IEAA` /
    // `IEBA` manglings). The registered C shims are free functions and cannot
    // reach them, so each gets a one-line static forwarder that disappears with
    // the shim in step 2. Nothing else may call these.
    static void Shim_Copy(InternalBasePtr* self, const InternalBasePtr* other) {
        self->Copy(*other);
    }
    static void Shim_CheckPtr(const InternalBasePtr* self) { self->CheckPtr(); }
    static void Shim_AssignId(InternalBasePtr* self)       { self->AssignId(); }

protected:
    // ?Copy@... 0x1800062b0 -- `mov rax,[rdx]; mov [rcx],rax`. NOT refcounted.
    void Copy(const InternalBasePtr& other) { _ptr = other._ptr; }

    // ?CheckPtr@... 0x1800c0290 -- a bare `ret` in retail AND in the profile
    // build. DO NOT ADD VALIDATION HERE. See the banner.
    void CheckPtr() const {}

    // ?AssignId@... 0x1800c0280 -- the same three bytes. Likewise empty.
    void AssignId() {}

    Internal* _ptr;   // +0x00
};

static_assert(sizeof(InternalBasePtr) == 0x08, "Object::InternalBasePtr is 8 bytes");
static_assert(std::is_trivially_copyable<InternalBasePtr>::value,
              "Copy (0x1800062b0) is a bare qword move and the array/list bulk "
              "paths memcpy these; a real copy ctor would be silently skipped");
static_assert(std::is_trivially_destructible<InternalBasePtr>::value,
              "no destructor exists in retail; DeletePtr is explicit and manual");

// ============================================================
// Object::InternalPtr<T> -- retail's TYPED wrapper over the same eight bytes.
//
// Single inheritance, no virtuals, so the layout is unchanged (asserted). The
// only typed operation retail emits is the array Serialize's per-element type
// check, which is `InternalBasePtr::Serialize` plus one `IsDerivedFrom` against
// `T::_typeInfo`'s first dword -- ONE body, both instantiations
// (SMARTPTR_INVENTORY.md section 3). Until `T` is a real C++ class with a
// `_typeInfo` static (ENDGAME B.3 step 3, decision 4) the type id is supplied
// by the caller, exactly as the C thunks read it through the name door.
// ============================================================

// The type test on a pointer field's held Internal* (review F-143); defined below
// `class Internal`, where the class is complete.
inline bool IsDerivedFromNullSafe(const InternalBasePtr& p, uint32_t typeId);

template <class T>
class InternalPtr : public InternalBasePtr {
public:
    InternalPtr() : InternalBasePtr() {}
    explicit InternalPtr(NoStore n) : InternalBasePtr(n) {}   // see InternalBasePtr(NoStore)
    InternalPtr(T* p) : InternalBasePtr(static_cast<Internal*>(p)) {}

    InternalPtr(const InternalPtr&) = default;
    InternalPtr& operator=(const InternalPtr&) = default;
    ~InternalPtr() = default;

    // The downcast from `Object::Internal*` (B10). Every T derives from
    // `Object::Internal` at offset 0; T must be COMPLETE where Get() / the T*
    // ctor is instantiated (members of a class template are instantiated only
    // when used), not where `InternalPtr<T> x;` is declared.
    T* Get() const        { return static_cast<T*>(InternalBasePtr::Get()); }
    T* operator->() const { return Get(); }
    T& operator*()  const { return *Get(); }

    // 0x180009a20 / 0x180009fa0 -- the one array body, per element.
    void Serialize(::IO::Serializer& ser, uint32_t typeId) {
        InternalBasePtr::Serialize(ser);
        const Internal* obj = InternalBasePtr::Get();
        if (obj != nullptr && !InternalPtrIsDerivedFrom(obj, typeId))
            _ptr = nullptr;   // retail nulls the element on total mismatch
    }

    // The same element body with T's OWN cell (B9 RT2): what
    // `IO::Serializer::Serialize<T>(int, T*, const char*)` runs per element, and
    // what a struct holding an InternalPtr<T> runs for that field. Retail, per
    // instantiation, loads T's `_typeInfo` cell and CALLs Base::IsDerivedFrom
    // (IAT 0x140152a98) directly:
    //   InternalPtr<ComponentEntity>          cell 0x1401c5d70 (0x140021c24, 0x140033724)
    //   InternalPtr<NaturalResourceComponent> cell 0x1401c7b30 (0x14003374d)
    // A caller whose retail cell is NOT T's spells it with the two-argument form.
    void Serialize(::IO::Serializer& ser) {
        InternalBasePtr::Serialize(ser);
        // NOT RETAIL (review F-143): the test runs on the held Internal*, before any
        // downcast. Retail has no C++ cast; the port's `Get()` here was a
        // static_cast to T* of an object that is not a T whenever the test is about
        // to fail (a save naming another Internal type), which is undefined
        // behaviour. The answer is the same.
        if (!IsNull() && !IsDerivedFromNullSafe(*this, T::TypeId()))
            _ptr = nullptr;   // retail nulls the element on a type mismatch
    }

    static void SerializeArray(::IO::Serializer& ser, int count,
                               InternalPtr* elems, uint32_t typeId) {
        for (int i = 0; i < count; ++i)
            elems[i].Serialize(ser, typeId);
    }
};


// The POD alias's trivial destructibility, asserted where <type_traits> is in
// scope. See the note beside `RtObject::Internal` itself.

// ############################################################################
// ENDGAME B.3.3 step 5, merged back here from src/engine/object/internal_class.h
// by step 7 — `Object::Internal` and `Object::InternalFactory` as REAL classes.
// ############################################################################

// ############################################################################
// ENDGAME B.3.3 step 5 — `Object::Internal` and `Object::InternalFactory` as
// REAL classes, under their retail names.
//
// MEASURED VTABLE. `??_7Internal@Object@@6B@` @ 0x180154720, read out of .rdata
// on 2026-09-18 (Runtime-x64.dll, image base 0x180000000) — FOUR slots, bounded
// by `??_7Achievement@0@6B@` at 0x180154740:
//
//   | slot | +off | address     | body                                   | name |
//   |------|------|-------------|----------------------------------------|------|
//   | 0    | 0x00 | 0x180006440 | VECTOR deleting dtor: `TEST DL,0x2`,   | NO RETAIL NAME in the |
//   |      |      |             | elem size 0x10, `LEA R9,[0x1800bfdb0]` | export table — it is  |
//   |      |      |             | (= ~Internal), free via 0x180006a10    | Base's `DeletingDtor` |
//   |      |      |             |                                        | slot, overridden      |
//   | 1    | 0x08 | 0x1800064c0 | `MOV RAX,[0x1801f9c20]; RET`           | `?TypeName@Internal@Object@@UEBAPEBDXZ` |
//   | 2    | 0x10 | 0x1800064d0 | `MOV EAX,[0x1801f9c18]; RET`           | `?Type@Internal@Object@@UEBAIXZ` |
//   | 3    | 0x18 | 0x1800bfda0 | `RET` — EMPTY in retail AND profile    | `?Persist@Internal@Object@@UEAAXAEAVStream@IO@@@Z` |
//
// Slots 1 and 2 are exactly what `DECLARE_TYPE` emits over the class's own
// `_typeInfo`, and the rest of retail's reflection block for this class is in
// the export table at the addresses named on each member below. So `Internal`
// takes `DECLARE_TYPE(Internal)`; `DEFINE_TYPE` lives in
// src/engine/object/internal_class.cpp.
//
// THE DESTRUCTOR IS VIRTUAL (the normal-virtual cutover, B3X W1): GCC spends
// two slots on it, so `TypeName`, `Type` and `Persist` sit one compiler slot
// after their retail slot; nothing reads the table by index (no minted table
// is left). Deletion is `delete p`.
//
// THE MINTED TABLE IS GONE (ENDGAME B.3.9 OBJ): the `??_7Internal@Object@@6B@`
// row, its RT_DELDTOR row 0x180006440 and the ??0 / ??1 doors that stamped it
// are retired; every Internal-derived object wears a compiler table.
// ############################################################################

class Internal;
class InternalFactory;

// ------------------------------------------------------------
// The seam this family needs from outside itself. Defined in
// src/engine/object/internal_class.cpp, which has a malloc/no-registry branch
// under -DBANISHED_UNIT_TEST exactly as src/engine/object/type_reflection.cpp
// and src/engine/collection/linkedlist.cpp do.
//
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z  0x1800c7a50
//   ?MemFree@MemoryAllocator@System@@SAXPEAX@Z     0x1800c7ad0  (== 0x180006a10)
//   ?Rand@Random@System@@QEAAIXZ                   0x1800c7f90
// ------------------------------------------------------------
namespace detail {
void*    InternalMemAlloc(uint64_t bytes, int align);
void     InternalMemFree(void* p);
uint32_t InternalRand(System::Random* random);

// The four `Collection::Array` instantiations retail emits for this class, one
// body per element width, each at its own retail address. They are the Wave 3
// transcriptions moved here from internal.cpp unchanged — they are NOT
// `Collection::Array<T>::SetSize`, because retail's inlined growth here goes
// through the MemoryAllocator statics above rather than the container's own
// allocator policy, and this step does not move that door.
//
//   FUN_18002ffa0  Array<Internal*>::SetSize(int, const Internal*&)
//   FUN_18002fca0  Array<Internal*>::SetSize(int)          — shrink-only in retail
//   FUN_18002fd90  Array<Internal*>::Reserve(int)
//   FUN_18002fea0  Array<int32>::SetSize(int)
void ArrayPtrSetSizeFill(Collection::Array<Internal*>* a, int newSize, Internal* const* fill);
void ArrayPtrSetSize(Collection::Array<Internal*>* a, int newSize);
void ArrayPtrReserve(Collection::Array<Internal*>* a, int n);
void ArrayIntSetSize(Collection::Array<int32_t>* a, int newSize);
}  // namespace detail

// ============================================================
// Object::Internal — 16 bytes: Base's vptr plus two dwords.
// ============================================================
class Internal : public Base {
    DECLARE_TYPE(Internal)

public:
    // ??0Internal@Object@@QEAA@XZ          retail 0x1800bfd10 / profile 0x18008b370
    //     Base::Base(this); vptr = &Internal::_vftable_;
    //     _factoryIndex = -1; _generation = 0;
    //     InternalFactory::_singleton->AddObject(this);
    // The `-1` / `0` pair is a dead store in BOTH builds — AddObject overwrites
    // both before anything can observe them — and is reproduced anyway.
    Internal();

    // ??0Internal@Object@@QEAA@AEBV01@@Z   retail 0x1800064e0
    //     LEA RAX,[0x180154788]; MOV [RCX],RAX      (Base's vptr)
    //     LEA RAX,[0x180154720]; MOV [RCX],RAX      (Internal's vptr)
    //     +0x08 and +0x0c copied verbatim.
    // *** IT DOES NOT CALL AddObject. *** The copy inherits the ORIGINAL's
    // `_factoryIndex`, so two live objects share one table slot and the second
    // `~Internal` swap-removes a slot that no longer names it. That is retail's
    // behaviour, it is the shape
    // reference_internalfactory_swapremove_wild_write.md describes, and it is
    // transcribed, not fixed.
    Internal(const Internal& other);

    // ??4Internal@Object@@QEAAAEAV01@AEBV01@@Z  retail 0x180006430
    // Four instructions: the two dwords, no vptr store, no table call.
    Internal& operator=(const Internal& other);

    // ---- vtable slot 0 — 0x180006440 ------------------------------------
    // ??1Internal@Object@@UEAA@XZ          retail 0x1800bfdb0 / profile 0x18008b400
    //     vptr = &Internal::_vftable_; _generation = 0;
    //     InternalFactory::_singleton->RemoveObject(this);
    //     Base::~Base(this);                    // vptr = &Base::_vftable_
    // The two vptr stores are what a real destructor chain emits for us.
    // Virtual (B3X W1): retail's slot 0 is the deleting destructor.
    ~Internal() override;

    // ---- vtable slot 3 — 0x1800bfda0, `RET` in retail AND in the profile
    // build. It exists so a subclass has something to override, and
    // `InternalFactory::PersistObject` calls it on every object it creates from
    // a stream. `?Persist@Internal@Object@@UEAAXAEAVStream@IO@@@Z`: retail's
    // `IO::Stream&` (B.4 P2 S1; it was spelled `void*` until then).
    virtual void Persist(IO::Stream& stream);

    // ?Clone@Internal@Object@@QEAAPEAV12@XZ   retail 0x1800bfb10, ord 1716
    //
    // NON-VIRTUAL (`QEAA`) and it appears in no vtable — not in
    // `??_7Internal@Object@@6B@`'s four slots and not anywhere else. It is a
    // SERIALIZE ROUND-TRIP DEEP COPY: ask the Factory for a fresh object of
    // this object's own type, write `this` into a 0x1000-byte MemoryStream,
    // copy those bytes into a second one, and read the new object back out of
    // it. Both streams are then destroyed and the new object is returned.
    //
    // It returns NULL when the freshly created object does not derive from
    // `Internal` — and it then calls slot 3 ON THAT NULL before returning it,
    // which is retail's and is transcribed rather than guarded. See the body.
    //
    // PORTED AT ENDGAME B.3.4 STREAMS (6/6). It had been genuinely unported —
    // no registration by name or by address, no stub, no TODO — and this
    // header's `Persist` neighbour plus internal_class.cpp's banner were the
    // only two places that said so. Its stated blocker was `IO::MemoryStream`,
    // which B34_RULES §6.5(e) already recorded as satisfied.
    Internal* Clone();

    int32_t  GetFactoryIndex() const { return _factoryIndex; }
    uint32_t GetGeneration()   const { return _generation; }

    // ---- the 16 bytes, in retail order ----
    // PUBLIC for the same reason `Object::BaseTypeInfo`'s are: every
    // unconverted body in the tree reads them at these offsets through a cast,
    // and `offsetof` on them has to be well formed for the pins below. Retail's
    // are private (`_factoryIndex`, `_generation` in the profile asserts).
    int32_t  _factoryIndex;   // +0x08  own slot in InternalFactory::_objects
    uint32_t _generation;     // +0x0c  InternalFactory::_random.Rand() at birth
};

static_assert(sizeof(Internal) == 0x10, "Object::Internal is 16 bytes");

// ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ 0x1800c0150 -- see the
// declaration. Slot 0, flags 1, by name (B3X W0).
inline void InternalBasePtr::DeletePtr() {
    Internal* p = _ptr;
    if (p != nullptr) {
        delete p;
        _ptr = nullptr;
    }
}
static_assert(offsetof(Internal, _factoryIndex) == 0x08, "Internal::_factoryIndex at +0x08");
static_assert(offsetof(Internal, _generation)   == 0x0c, "Internal::_generation at +0x0c");

// `IsDerivedFromNullSafe` over a pointer FIELD: the type test runs on the held
// Internal*, before any downcast. NOT RETAIL (review F-143): the game's type tests
// of an `InternalPtr<T>` read from a save were spelled `IsDerivedFromNullSafe(
// field.Get(), id)`, and `InternalPtr<T>::Get()` is a static_cast to T*; on an
// object that is not a T (the case the test exists to catch) that cast is
// undefined behaviour. Retail tests the Internal* with no C++ cast at all. The
// answer is the same.
inline bool IsDerivedFromNullSafe(const InternalBasePtr& p, uint32_t typeId) {
    return IsDerivedFromNullSafe(static_cast<const Base*>(p.Get()), typeId);
}

// ============================================================
// Object::InternalFactory — 0x9e8 bytes (0x9e4 of fields, alignof 8).
//
// The engine-wide handle table. Layout and evidence: the `RtObject` POD above,
// which this class is cross-asserted against field for field.
//
// NO DESTRUCTOR IS DECLARED, deliberately. `??1InternalFactory@Object@@QEAA@XZ`
// (retail 0x18002f860) is a NON-VIRTUAL member whose port lives in
// src/engine/object/factory_dtors.cpp — a file this step does not own — and
// `??0InternalFactory@Object@@QEAA@XZ` (0x1800c0030) is registered from
// src/engine/engine/singleton_ctors.cpp. Declaring either here would give the
// class a second, divergent spelling of a body that is already registered at
// those two addresses. The singleton is heap-allocated once and torn down
// through those doors; nothing constructs one of these by value.
// ============================================================
class InternalFactory {
public:
    // ?Get@InternalFactory@Object@@SAPEAV12@XZ   retail 0x18002f850 —
    // `MOV RAX,[0x1801fc6d8]; RET`, i.e. the exported data symbol
    // `?_singleton@InternalFactory@Object@@0PEAV12@EA` read straight out.
    static InternalFactory* Get();

    // ?_singleton@InternalFactory@Object@@0PEAV12@EA — retail 0x1801fc6d8. The
    // CLASS STATIC since ENDGAME B.3.9 OBJ (B39_RULES D7): defined in
    // internal_class.cpp, and RtObjectPortInit redirects the minted row onto
    // it, so the name door, the address door and this member are the same
    // eight bytes. Written by the ctor door (engine/singleton_ctors.cpp).
    // PUBLIC while foreign readers still reach the cell through the doors.
    static InternalFactory* _singleton;

    // ?Startup@InternalFactory@Object@@QEAA_NXZ  retail 0x1800c0020 —
    // `MOV AL,1; RET`. It really does nothing but succeed.
    bool Startup();
    // ?Shutdown@InternalFactory@Object@@QEAAXXZ  retail 0x1800c0010 — bare RET.
    void Shutdown();

    // ?AddObject@InternalFactory@Object@@QEAAXPEAVInternal@2@@Z   0x1800c00c0
    //     obj->_factoryIndex = _objects._size;
    //     obj->_generation   = _random.Rand();
    //     _objects.SetSize(_objects._size + 1, &obj);
    void AddObject(Internal* obj);

    // ?RemoveObject@InternalFactory@Object@@QEAAXPEAVInternal@2@@Z 0x1800c0110
    //     last = _objects._size - 1;  idx = obj->_factoryIndex;
    //     if (last != idx) { _objects[idx] = _objects[last];
    //                        _objects[idx]->_factoryIndex = idx; }
    //     _objects.SetSize(last);          // tail JMP to 0x18002fca0
    // NO VALIDATION OF `idx`, here or in the profile build. A stale index lands
    // the second store wherever the allocator has since put that block; the fix
    // for that family is upstream (unregister before freeing), never a guard
    // here.
    void RemoveObject(Internal* obj);

    // ?ClearPersistState@InternalFactory@Object@@QEAAXXZ   0x1800bfe30
    void ClearPersistState();

#if !defined(BANISHED_UNIT_TEST)
    // ?ResetPersistState@InternalFactory@Object@@QEAAXAEAVStream@IO@@@Z 0x1800bfe40
    void ResetPersistState(IO::Stream& stream);
#endif

    // ?PersistObject@InternalFactory@Object@@QEAAPEAVInternal@2@AEAVStream@IO@@IHI@Z
    //                                                      0x1800bff10
    Internal* PersistObject(IO::Stream& stream, uint32_t generation, int index,
                            uint32_t typeId);

    Collection::Array<Internal*>&       GetObjects()          { return _objects; }
    const Collection::Array<Internal*>& GetObjects() const    { return _objects; }
    Collection::Array<int32_t>&         GetPersistIds()       { return _persistIds; }
    System::Random&                     GetRandom()           { return _random; }

    // PUBLIC for the same reason `Object::Factory::_types` is: the unconverted
    // C bodies and the exe-side audit (src/game/Object/internal_factory_audit.h)
    // reach these directly, and `offsetof` on them has to be well formed.
    Collection::Array<Internal*> _objects;      // +0x00
    Collection::Array<int32_t>   _persistIds;   // +0x10
    System::Random               _random;       // +0x20
};

static_assert(sizeof(InternalFactory) == 0x9e8,
              "Object::InternalFactory: 0x9e4 bytes of fields, padded to 0x9e8 by alignof 8");
static_assert(offsetof(InternalFactory, _objects)    == 0x00, "_objects at +0x00");
static_assert(offsetof(InternalFactory, _persistIds) == 0x10, "_persistIds at +0x10");
static_assert(offsetof(InternalFactory, _random)     == 0x20, "_random at +0x20");

}  // namespace Object

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.
namespace IO { class Serializer; }   // io/Serializer.h

// ---- object/internal_ptr_array_serialize.cpp
// ==== end B3R W0-P0c ====
