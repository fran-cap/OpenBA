#pragma once
// ============================================================================
// src/engine/collection/Collection.h — binary layouts for Collection::*
// ============================================================================
//
// Wave 2, Lane E of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2,
// and §4.3 risk 1, which ranks these layouts the single biggest risk in the
// whole port: they are header-only templates, inlined into every consumer, and
// they define ZERO exported symbols. Getting one wrong silently corrupts every
// class that embeds it.)
//
// EVIDENCE: every layout below was read out of a decompiled Runtime-x64.dll
// function, and the specific function and field access is named next to it.
// The full write-up, including confidence levels and what is NOT pinned, is
// docs/history/runtime_port/WAVE2_COLLECTION.md. Nothing here is a guess; anything that
// could not be pinned is spelled out as such in a comment.
//
// THE REAL CONTAINER SET
// ----------------------
// The profile build's source-tree strings list exactly two headers in this
// directory (docs/history/runtime_port/original_source_tree.txt):
//
//     c:\projects\buildy\code\runtime\Collection/Array.h
//     c:\projects\buildy\code\runtime\Collection/Allocator.h
//
// and the mangled names in the DLL name exactly five Collection entities:
//
//     Collection::Array<T>                            (44 exports mention it)
//     Collection::Set<T, Allocator, Key, Compare>     (2)
//     Collection::LinkedList<T, Allocator>            (1, plus ::Iterator)
//     Collection::DefaultAllocator                    (a struct, U-prefixed)
//     Collection::AVLCompare                          (a struct, U-prefixed)
//
// *** THERE IS NO Collection::Map. ***  PLAN.md §4.3 risk 1 and the Wave 2
// brief both say "Map is the AVL tree behind Map_Insert/Map_Find". That is our
// own invented Ghidra name, not the engine's. `Map_Insert` (0x18002a090) is
// Set<Pair<String,String>>::Insert: it builds a 16-byte {key,value} element on
// the stack and hands it to the ordinary Set insert. A map in this engine is a
// Set whose ELEMENT embeds the key, which is exactly why Set takes a separate
// Key and Compare template parameter. Do not port a `Map`.
//
// Utility::HashGrid<T> and System::MemoryAllocator::PointerHash<T> are also
// templates, but they live in `Utility` and `System`, not `Collection`. They
// are out of scope for this header (HashGrid is Wave 5 territory).
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <new>           // placement new: Array's element ops, and Set building
                         //   its element inside the node
#include <type_traits>   // the traits rtcoll::SpecFor<T> reads
#ifndef BANISHED_UNIT_TEST
#include "../system/MemoryAllocator.h"   // B7 D4: the inline rtcoll::MemFree
#endif
#include "../io/Stream.h"   // B9 RT2: Array<T>::Serialize reads the stream's direction inline

// The erased node and container types the ported AVL cores take (B.4 R-COLL):
// `detail::AvlOps<Node, W>` hands a typed `Set<T>` / `Set<T>::Node` to them.
namespace rtsetavl   { struct Set; struct Node; }   // src/engine/collection/set_avl.h
namespace rtsetavl24 { struct Set; struct Node; }   // src/engine/collection/set_avl_e24.h
namespace RtObject   { struct TypeNode; }                  // src/engine/object/TypeInfo.h
namespace RtIO       { struct ModSetNode; }                 // src/engine/io/Modification.h

namespace Collection {

// ============================================================
// Collection::DefaultAllocator — STATELESS policy struct.
//
// Evidence that it carries no storage: every allocation and free reaches the
// global System::MemoryAllocator::_singleton by absolute address, and no
// allocating body ever reads a field of the container it is allocating for —
// FUN_18000f970 (Set node alloc), FUN_180034010 and 0x1800a76c0 (Array grow),
// and the free in CleanRemovedDialogs. A stateful allocator policy would have
// to be reached through the container. Size agrees: LinkedList is 0x18
// (head/tail/count) and Set is 0x10 (root/count), each with exactly 4 bytes of
// tail slack — room for a flag, not for a pointer.
//
// What it forwards to, from the node allocators inlined into FUN_18000f970
// (Set node alloc) and FUN_180034010 (Array grow):
//
//     EnterCriticalSection(MemoryAllocator::_singleton + 0x640);
//     p = System::MemoryAllocator::Allocate(_singleton, bytes, alignment);
//     LeaveCriticalSection(...);
//
// i.e. Allocate(n * sizeof(T), alignof(T)) under the allocator's own lock, and
// the symmetric MemoryAllocator::Free. The alignment argument is alignof(T),
// NOT sizeof(T): Array<int> passes 4 (0x1800a76c0), Array<ModInfo> — a 0x20
// struct — passes 8 (0x180034010), Set nodes pass 8 (0x18000f970).
// ============================================================
struct DefaultAllocator {};
static_assert(sizeof(DefaultAllocator) == 1, "policy struct, never stored");

// ============================================================
// Collection::AVLCompare — STATELESS comparison policy struct.
//
// Named by the mangled type of IO::PackageFile::BuildPackageFile's parameter:
//   ?$Set@VFilename@System@@UDefaultAllocator@Collection@@V12@UAVLCompare@4@
//                                                            ^^^^^^^^^^^^^^
// (`@4@` back-references `Collection`.) Same carries-no-storage argument as
// DefaultAllocator: the comparison is fully inlined at every call site and
// never reads a field of the Set.
//
// Semantics, from the inlined comparison in Set::Insert (FUN_18002a170) and
// Set::Find (0x18002ace0) for a String key: an ordinary lexicographic byte
// compare of the two key strings, with a NULL key ordering as "less than
// everything". Insert descends LEFT when newKey < nodeKey and RIGHT when
// nodeKey < newKey, and returns the EXISTING node when neither holds — Set
// never stores duplicate keys.
// ============================================================
struct AVLCompare {};
static_assert(sizeof(AVLCompare) == 1, "policy struct, never stored");

// ============================================================
// Collection::Array<T> — {data, size, capacity}, 16 bytes. CONFIRMED.
//
// The exe port has assumed this triple for months (src/game/Entity/EntityList.h
// `IBPArray`); this is the independent confirmation from Runtime-x64.dll that
// the Wave 2 brief asked for.
//
// sizeof == 16 is pinned by Video::ShaderProgram::GetByteCode (0x18004e100),
// a one-line `return _byteCode[i]` over an Array<Array<unsigned char>> that
// compiles to `*(this+0x40) + i*0x10`. Field offsets are pinned by four
// independent inlined bodies, all agreeing:
//   DynArrayPush              0x18007a0d0  (elements 8 bytes)
//   Input::KeyMap::GetIdList  0x1800a76c0  (Array<int>, elements 4 bytes)
//   Array<ModInfo>::SetSize   0x180034010  (elements 0x20 bytes, non-trivial T)
//   Array<T>::SetSize(0)      0x180079c90  (the constant-folded Clear)
// each reading `+0x00` as the buffer pointer, `+0x08` as an int32 element
// count, `+0x0c` as an int32 capacity.
//
// The field is named `_size`, not `_count`: the profile build's Array.h assert
// text is "index >= 0 && index < _size" (string at file offset 0x199e48 in
// ToolKit/bin/x64/Runtime-x64-profile.dll, immediately after the
// "Index out of range" message and near the Collection/Array.h path string).
//
// GROWTH POLICY (identical in all four bodies above), for SetSize(n):
//     if (n == _size) return;                       // exact-equal is a no-op
//     old = _data; oldSize = _size; _size = n;
//     if (_capacity < n) {
//         _capacity = n;
//         if (n == oldSize + 1 && oldSize != 0)      // appending to a NON-EMPTY
//             _capacity = n * 3;                     // array trebles
//         _data = Allocate(_capacity * sizeof(T), alignof(T));
//     }
//     // copy-construct [0, oldSize) from `old` if we reallocated,
//     // default-construct [oldSize, n),
//     // destroy the elements that fell off, then Free(old) if we reallocated
//
// Consequences worth stating because they are easy to get wrong:
//  * capacity NEVER shrinks and is NEVER reduced by SetSize; Clear()
//    (= SetSize(0), 0x180079c90) keeps the buffer and only zeroes _size.
//  * the FIRST append to an empty array allocates capacity 1, not 3 — the `*3`
//    is gated on oldSize != 0.
//  * a bulk SetSize(n) allocates EXACTLY n; only the +1 append over-allocates,
//    and it sets capacity to 3*newSize (not 3*oldCapacity), so appending one at
//    a time from empty walks capacities 1, 6, 21, 66, 201, ...
//  * T is destroyed properly on shrink and on realloc (0x180034010 calls the
//    element-destroy helper FUN_1800341e0 for both the truncated tail and the
//    whole old buffer), and copied by copy-CONSTRUCTION on realloc (the
//    Array<ModInfo> path bumps the String refcounts at `str-4`).
// ============================================================
// ============================================================================
// ENDGAME B.3.2 step 1 — `Collection::Array<T>`, the real template
// ============================================================================
//
// Everything above is the layout evidence and it is unchanged: the three fields
// below are still `_data` / `_size` / `_capacity` at +0x00 / +0x08 / +0x0c and
// every LayoutProbe static_assert at the bottom of this header still holds.
// What changed on 2026-09-13 is that the class now carries retail's METHODS
// instead of being a bare header the C bodies poke at.
//
// THE TEMPLATE WRAPS THE PORTED BODIES; IT DOES NOT RE-DERIVE THEM.
// `SetSize` calls `rtcoll::SetSizeCore` and `~Array` calls
// `rtcoll::ArrayDtorImpl` — the same two bodies, driven by the same
// `rtcoll::ArraySpec` rows, that array.cpp's per-address `RtColl_Array*`
// entries run. COLLECTION_INVENTORY.md §5.3 says why in one line: a body
// re-derived from Ghidra's C risks re-introducing an argument-count mismatch
// this port already solved.
//
// WHICH SPEC DRIVES AN INSTANTIATION — the third template parameter.
//
//   * `Array<T>` (the default, `DefaultSpec<T>`) derives its spec from T's type
//     traits: `byteCopy = is_trivially_copyable<T>`, `destroy == nullptr =
//     is_trivially_destructible<T>`, `init == nullptr =
//     is_trivially_default_constructible<T>` (retail's empty `else` arm, i.e.
//     the new tail is left UNINITIALISED), `assign` = placement copy-construct.
//     That mapping is not new design: it NAMES what the hand-derived rows
//     already encode.
//
//   * `Array<uint8_t, DefaultAllocator, ExplicitSpec<&kSpec_X>>` keeps a
//     HAND-DERIVED row. Every one of array.cpp's per-address SetSize entries uses this form,
//     because a trait cannot express what those rows say: a fill of -1
//     (kSpec_I32Neg1, 0x18007b960), an identity-Transform init
//     (kSpec_Transform, 0x180059140), a PARTIAL init that deliberately leaves
//     bytes of the new element untouched (kSpec_SpriteDraw 0x180078520,
//     kSpec_SpriteIndexZ 0x1800370c0, kSpec_TableCell10 0x1800799a0,
//     kSpec_Window 0x180024c10, kSpec_CaptureEntryZ 0x18004ba90,
//     kSpec_EffectSlot 0x180051390, kSpec_AudioChannel70 0x180051020,
//     kSpec_Rec14 0x180020e90, kSpec_IdExtPtr10 0x180059930,
//     kSpec_SfxSample18 0x180052e60, kSpec_MetaEntry20 0x180034010), or a
//     byteCopy=true row that ALSO carries a destroy — a combination C++ traits
//     cannot produce at all, since `is_trivially_copyable` implies a trivial
//     destructor (kSpec_PkgEntry 0x1800298f0, kSpec_StringExt 0x180061480).
//     Step 1 therefore replaces NO spec: the shims name their row and the
//     trait-derived form serves the typed instantiations (SoundEffect's
//     `Collection::Array<Sample>` among them).
//
// WHAT IS DELIBERATELY *NOT* ON THE CLASS
//   * `Serialize` is the EXE's, not the DLL's (hazard 6, corrected B9 RT2). None
//     of the DLL's Collection instantiations is one, and DLL callers serialize array
//     contents by iterating (COLLECTION_INVENTORY.md §4). The exe instantiates
//     one, always inlined: EntityList's serializer 0x1400c44a0 opens it for its
//     +0x08 array (0x1400c4542..0x1400c45af) and the template clone 0x140021ba0
//     runs it per UpdateBucket. See `Array<T>::Serialize` below.
//   * No `Sort`. The packfile introsort (0x1800264a0) is one hand-transcribed
//     algorithm whose tie behaviour is save-observable; it stays a free
//     function (hazard 7).
//   * No move constructor / move assignment. `SetSizeCore`'s realloc path is
//     copy-THEN-destroy-then-free in retail's element order and a move would
//     change the String/Filename/ExternalPtr refcount sequences the resave gate
//     observes (hazard 2).
//   * Copy construction is `= default`, i.e. the SHALLOW memberwise copy the
//     unconverted tree already relies on when it copies an overlay struct that
//     embeds an Array. It is DEFAULTED rather than left implicit so that
//     declaring `~Array` does not make every copy a -Wdeprecated-copy-dtor
//     warning.
//
//   * COPY ASSIGNMENT IS RETAIL'S DEEP ONE (closing pass, see `Assign` below).
//     0x18001cdf0 and its twins are `rtcoll::ArrayAssignCore` (array_destroy.cpp):
//     self-assign early-out, CLEAR (element destroy, then the buffer free),
//     `_size = src._size`, the same grow rule and `MemAlloc`, then a
//     per-element `assign` in retail's element order, and NO free after the
//     copy because the clear already did it. Copy-THEN-destroy, never a move
//     (COLLECTION_INVENTORY.md hazard 2: a move would change the
//     String/Filename/ExternalPtr refcount sequences the resave gate observes).
//   * The three fields stay PUBLIC. 419 sites across the tree name
//     `Collection::Array<...>` and read `_data`/`_size`/`_capacity` directly;
//     they become private with the last of them (B31_RULES step 2).
// ============================================================================

}  // namespace Collection  (reopened below; `rtcoll` is a global namespace)

namespace rtcoll {

// The element operations of one instantiation. Retail's `ElemDestroy` really
// does take an ignored `this` in RCX (array.cpp's ArraySpec banner).
using ElemAssign  = void (*)(uint8_t* dst, const uint8_t* src);
using ElemInit    = void (*)(uint8_t* dst);
using ElemDestroy = void (*)(void* ignoredThis, uint8_t* base, int count);

// MOVED HERE FROM src/engine/collection/array_internal.h (ENDGAME B.3.2 step
// 1) so `Collection::Array`'s inline bodies can name it. The per-field
// commentary — why the written extents are per ARM and not per TYPE, and what
// the probe seeding range is for — is still in array_internal.h, next to the
// `kSpec_*` rows it describes.
struct ArraySpec {
    const char* name;
    uint32_t    stride;
    int         align;
    ElemAssign  assign;    // never null
    ElemInit    init;      // null => retail leaves the new tail untouched
    ElemDestroy destroy;   // null => trivially destructible T
    bool        byteCopy;  // true when `assign` is a pure byte copy

    uint32_t    assignUnwrittenLo, assignUnwrittenHi;
    uint32_t    initUnwrittenLo,   initUnwrittenHi;

    uint32_t    seedZeroLo, seedZeroHi;
};

// ---------------------------------------------------------------------------
// B.4 (R-CORE) — the element view of a hand-derived row
// ---------------------------------------------------------------------------
// SetSizeCore steps the buffer as BYTES, `stride` at a time: the row is a
// runtime parameter, so the core is not a template over the element. These
// three adapters are the one place a stepped byte address becomes the element
// it holds. Every hand-derived row's element operation is written against the
// element's own type (fields by name) and reaches the core through one of them.
template <class E, void (*F)(E*, const E*)>
void AssignAs(uint8_t* dst, const uint8_t* src) {
    F(reinterpret_cast<E*>(dst), reinterpret_cast<const E*>(src));  // CAST-OK: elem_stride
}
template <class E, void (*F)(E*)>
void InitAs(uint8_t* dst) {
    F(reinterpret_cast<E*>(dst));  // CAST-OK: elem_stride
}
template <class E, void (*F)(E*, int)>
void DestroyAs(void* /*ignoredThis*/, uint8_t* base, int count) {
    F(reinterpret_cast<E*>(base), count);  // CAST-OK: elem_stride
}

// The word-wise element copy several rows share (B13: one body serving several
// element types): N moves of W, in address order, at retail's store width.
template <class W, int N>
void CopyWords(W* dst, const W* src) {
    for (int k = 0; k < N; ++k) dst[k] = src[k];
}

// ---------------------------------------------------------------------------
// SpecFor<T> — the trait-derived row
// ---------------------------------------------------------------------------
// One `static const ArraySpec` per element type, built at first use. The three
// element operations are function templates rather than lambdas so that each
// has a readable name in a crash log.
template <class T>
void TAssign(uint8_t* dst, const uint8_t* src) {
    // Retail's realloc arm COPY-CONSTRUCTS into the new buffer and destroys the
    // old elements afterwards; placement copy-construct is that, exactly.
    new (static_cast<void*>(dst)) T(*reinterpret_cast<const T*>(src));  // CAST-OK: elem_stride
}
template <class T>
void TInit(uint8_t* dst) {
    new (static_cast<void*>(dst)) T();
}
template <class T>
void TDestroy(void* /*ignoredThis*/, uint8_t* base, int count) {
    T* elems = reinterpret_cast<T*>(base);  // CAST-OK: elem_stride
    for (int i = 0; i < count; ++i)
        elems[i].~T();
}

template <class T>
const ArraySpec* SpecFor() {
    static const ArraySpec spec = {
        "Array<T> (trait-derived)",
        static_cast<uint32_t>(sizeof(T)),
        static_cast<int>(alignof(T)),
        &TAssign<T>,
        // A trivially default-constructible T reproduces retail's EMPTY else
        // arm: the new tail is left holding whatever the allocation contained.
        // Writing `TInit<T>` for a trivial T instead would ZERO the tail — a
        // different array.
        std::is_trivially_default_constructible<T>::value ? nullptr : &TInit<T>,
        std::is_trivially_destructible<T>::value ? nullptr : &TDestroy<T>,
        std::is_trivially_copyable<T>::value,
        /*assign unwritten*/ 0, 0,
        /*init   unwritten*/ 0, 0,
        /*seed zero       */ 0, 0 };
    return &spec;
}

}  // namespace rtcoll

namespace Collection {

// The two spec policies. `DefaultSpec` is the third template argument's
// default; `ExplicitSpec` names a hand-derived row (see the banner).
template <class T>
struct DefaultSpec {
    static const rtcoll::ArraySpec* Get() { return rtcoll::SpecFor<T>(); }
};
template <const rtcoll::ArraySpec* SPEC>
struct ExplicitSpec {
    static const rtcoll::ArraySpec* Get() { return SPEC; }
};

template <class T,
          class Allocator  = DefaultAllocator,
          class SpecPolicy = DefaultSpec<T>>
class Array {
public:
    // Retail's ctor is the three zero stores every inlined copy begins with.
    Array() : _data(nullptr), _size(0), _capacity(0) {}
    // Shallow, and deliberately so — see the banner.
    Array(const Array&) = default;

    // Retail's deep `Array<T>::operator=` — see the banner. `Assign` carries
    // the body; `operator=` is the spelling every call site already uses.
    Array& Assign(const Array& src);
    Array& operator=(const Array& src) { return Assign(src); }

    // ~Array — 0x18001c750 and its ten twins for a trivial element; the J2
    // family (array_destroy.cpp) for one with a destroy step. Element destroy
    // first, then the buffer: that is the order both retail families use.
    ~Array();

    // The whole of ~Array's body (ClearCallback over this header) WITHOUT
    // ending the member's lifetime: for an owner's hand-written dtor that
    // frees the buffer before the implicit ~Array runs again on the zeroed
    // header (Geometry::~Geometry, Element's dtor).
    void DestroyAndFree();

    void SetSize(int newSize);                      // ?SetSize@..@QEAAXH@Z     (2-arg form)
    void SetSize(int newSize, const T& fill);       // ?SetSize@..@QEAAXHAEBT@Z (3-arg form)
    // The 3-argument form with an untyped fill, for the instantiations whose
    // element "type" is a hand-derived spec rather than a C++ type.
    void SetSizeFill(int newSize, const void* fill);

    // 0x180079c90 — the constant-folded SetSize(0). Buffer and capacity kept.
    void Clear() { SetSize(0); }

    // The exe's Array<T>::Serialize (B9 RT2; inlined at every retail site, no
    // address of its own). Read in 0x140021ba0's element step and at
    // 0x1400c4542..0x1400c45af:
    //     n = _size; Serialize(int&, 0)        0x140021bd0..0x140021be2
    //     if (stream direction == reading)     0x140021be8 CMP [RAX+0x8],0 (inline)
    //         SetSize(n)                       0x140021bf8 CALL 0x1400032e0
    //     Serializer::Serialize<T>(n, _data, 0), inlined: TEST RDI,RDI / JLE
    // The count is the LOCAL read back (MOVSXD RDI,[RSP+0x48]), the data
    // pointer is re-read after the SetSize.
    void Serialize(IO::Serializer& s);

    // The two by-value members retail's 8-byte element arrays (Array<InternalPtr<T>>,
    // Array<T*>) share, one COMDAT-folded exe body each (B9 RT, D5):
    //   Find   0x140017550 — the index of the first element equal to `value`, -1 if none
    //   Remove 0x140002cc0 — remove EVERY element equal to `value`, the tail shifted down
    // RCX is the array, RDX the key (`CMP dword ptr [RCX + 0x8]` / `MOV R15,RDX`); both
    // CALL the key's CheckPtr (0x140152ab0, a bare RET) before each compare. Typed
    // wrappers over the erased cores rtcoll::ArrayFind8 / ArrayRemove8 (array.cpp).
    int  Find(const T& value) const;
    void Remove(const T& value);

    int GetSize() const     { return _size; }
    int GetCapacity() const { return _capacity; }

    T&       operator[](int index)       { return _data[index]; }
    const T& operator[](int index) const { return _data[index]; }

    T*       GetData()       { return _data; }
    const T* GetData() const { return _data; }

    static const rtcoll::ArraySpec* Spec() { return SpecPolicy::Get(); }

    // The CLEAR half of retail's `operator=`, and the whole of `~Array`: run
    // the element destroy over the live range, then the J1 buffer free. It is
    // `ArrayAssignCore`'s fourth argument — array_destroy.cpp passes
    // `ArrayClearTrivial` (the J1 body alone) for a trivial element and
    // `ArrayClearMetaEntry20` (destroy-then-J1) for a refcounted one, and this
    // is both of those, chosen by the spec instead of by the call site.
    static void ClearCallback(Array<uint8_t>* hdr);

    // PUBLIC during the migration — see the banner.
    T*      _data;       // +0x00: element buffer, or null when capacity == 0
    int32_t _size;       // +0x08: live element count
    int32_t _capacity;   // +0x0c: allocated element count; never shrinks
};

}  // namespace Collection

// The two ported bodies and the allocator boundary, declared here rather than
// in array_internal.h so that the class above can call them. SetSizeCore and
// MemAlloc are defined in src/engine/collection/array.cpp; ArrayDtorImpl is
// header-inline below.
namespace rtcoll {

// The 16-byte container header. `uint8_t` is chosen so `_data` is already the
// byte cursor every body wants; sizeof(Array<T>) is T-independent.
using ArrayHdr = Collection::Array<uint8_t>;

// The container allocator pair (array.cpp). Every container allocation and free
// reaches `System::MemoryAllocator::MemAlloc` / `MemFree` by name, where the
// diagnostic instruments (BANISHED_REDZONE, BANISHED_ALLOC_STATS) sit on the
// allocator's instrument seat. (B3R W2-RES: `rtcoll::DoorAlloc` / `DoorFree`,
// the function-pointer door src/engine/collection/alloc_door.cpp that answered
// the same two statics, had no caller left and is deleted.)
void* MemAlloc(uint64_t bytes, int align);
// B7 D4: header-inline outside the unit test, so the inline ~Array's free is
// ONE direct call to System::MemoryAllocator::MemFree (0x1800c7ad0), as retail's
// open-coded teardown is (RefreshList 0x1800afb71..0x1800afbb6 inlines even
// MemFree's lock / Free / unlock), not a call to this forwarder. The unit test
// keeps array.cpp's std::free.
#ifdef BANISHED_UNIT_TEST
void  MemFree(void* p);
#else
[[gnu::always_inline]] inline void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}
#endif

// The J1 body (array.cpp's banner reads it at 0x18001c750 / 0x18001c100 and
// nine twins): null test first, MemFree (a call, 0x1800c7ad0), then the three
// header zero stores; RAX == 0 at the RET. Header-inline (B7 D4) because retail
// open-codes it at the measured callers (RefreshList 0x1800afb71..0x1800afbb6:
// the free and the zero stores inline), so a typed `Collection::Array<T>` local
// must not become an out-of-line call. FORCED inline, as String's release is
// (String.h): GCC's inliner otherwise keeps an out-of-line `.isra` copy at some
// callers, a call retail lacks.
[[gnu::always_inline]] inline void* ArrayDtorImpl(ArrayHdr* a) {
    if (a->_data) MemFree(a->_data);
    a->_data     = nullptr;
    a->_size     = 0;
    a->_capacity = 0;
    return nullptr;         // retail leaves RAX == 0
}
void  SetSizeCore(ArrayHdr* a, int newSize, const uint8_t* fill,
                  const ArraySpec* spec);

// retail 0x18001cdf0 and its ten twins — `Array<T>::operator=`.
// `clear` is the destroy-and-free half; `Collection::Array<T>::ClearCallback`
// is what the template hands it. array_destroy.cpp's "The generic J4" banner
// reads the body out of 0x180014c70 / 0x180005770 / 0x180033f30:
//     if (this == &src) return this;          CMP RCX,RDX / JZ
//     <clear>(this);                          CALL — leaves _size = 0
//     _size = src._size;
//     if (_size > _capacity) { grow; MemAlloc(cap * stride, align); }
//     for (i = 0; i < _size; ++i) assign(data + i*stride, src.data + i*stride);
//     return this;
// The `_capacity = n*3` branch is DEAD (it needs oldSize != 0 and the clear has
// just zeroed it) and is reproduced anyway. NO OLD-BUFFER FREE AFTER THE COPY:
// the clear already freed it.
// Header-inline (B9 RT2, the B7 D4 shape ~Array / ArrayDtorImpl took): retail
// open-codes the assign at its callers (Audio::Scene::Update 0x1800dfd10 ends
// its empty source with two bare zero stores, 0x1800dfd19 / 0x1800dfd1d, the
// free elided), which GCC can only do when it sees this body: out of line,
// `&src` escaped and the source's ~Array kept its null test and MemFree call.
// FORCED inline, as ArrayDtorImpl is. The same reads and stores as the
// array_destroy.cpp body it was; that body's two dead locals (a `realloced`
// flag and a `copied` count, read by nothing) are not carried.
using ArrayClearFn = void (*)(ArrayHdr*);
[[gnu::always_inline]] inline void* ArrayAssignCore(ArrayHdr* a, const ArrayHdr* s,
                                                    const ArraySpec* spec,
                                                    ArrayClearFn clear) {
    if (a == s) return a;

    clear(a);

    const int32_t newSize = s->_size;
    const int32_t oldSize = a->_size;          // always 0; clear zeroed it
    a->_size = newSize;

    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == static_cast<int32_t>(oldSize + 1) && oldSize != 0)
            a->_capacity = static_cast<int32_t>(newSize * 3);   // dead; faithful
        a->_data = static_cast<uint8_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity)) * spec->stride,
                     spec->align));
    }

    for (int32_t i = 0; i < a->_size; ++i) {
        uint8_t* p = a->_data + static_cast<int64_t>(i) * spec->stride;
        if (p == nullptr) continue;            // retail tests the COMPUTED address
        spec->assign(p, s->_data + static_cast<int64_t>(i) * spec->stride);
    }

    return a;
}

// exe 0x140017550 / 0x140002cc0 — `Array<T>::Find` / `Remove` over an 8-byte
// element, defined in src/engine/collection/array.cpp. `key` is the element.
int  ArrayFind8(const ArrayHdr* a, const void* key);
void ArrayRemove8(ArrayHdr* a, const void* key);

}  // namespace rtcoll

namespace Collection {

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::ClearCallback(Array<uint8_t>* hdr) {
    const rtcoll::ArraySpec* spec = Spec();
    if (spec->destroy && hdr->_size > 0)
        spec->destroy(nullptr, hdr->_data, hdr->_size);
    rtcoll::ArrayDtorImpl(hdr);
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::DestroyAndFree() {
    ClearCallback(reinterpret_cast<rtcoll::ArrayHdr*>(this));   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
}

template <class T, class Allocator, class SpecPolicy>
inline Array<T, Allocator, SpecPolicy>::~Array() {
    DestroyAndFree();
}

// retail 0x18001cdf0 — the deep assign. Every arm of it is
// `rtcoll::ArrayAssignCore`; nothing is re-derived here, and the element order
// and the copy-THEN-destroy shape are that body's.
template <class T, class Allocator, class SpecPolicy>
inline Array<T, Allocator, SpecPolicy>&
Array<T, Allocator, SpecPolicy>::Assign(const Array& src) {
    rtcoll::ArrayAssignCore(reinterpret_cast<rtcoll::ArrayHdr*>(this),   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
                            reinterpret_cast<const rtcoll::ArrayHdr*>(&src),  // CAST-OK: elem_stride
                            Spec(), &ClearCallback);
    return *this;
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::SetSize(int newSize) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(this), newSize,   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
                        nullptr, Spec());
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::SetSize(int newSize, const T& fill) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(this), newSize,   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
                        reinterpret_cast<const uint8_t*>(&fill), Spec());  // CAST-OK: elem_stride
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::SetSizeFill(int newSize, const void* fill) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(this), newSize,   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
                        static_cast<const uint8_t*>(fill), Spec());  // CAST-OK: elem_stride
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::Serialize(IO::Serializer& s) {
    int n = _size;
    s.Serialize(n, nullptr);
    if (s._stream->GetDirection() == IO::kReading)
        SetSize(n);
    s.Serialize(n, _data, nullptr);
}

template <class T, class Allocator, class SpecPolicy>
inline int Array<T, Allocator, SpecPolicy>::Find(const T& value) const {
    static_assert(sizeof(T) == 8, "retail's Find (0x140017550) is the 8-byte element's");
    return rtcoll::ArrayFind8(reinterpret_cast<const rtcoll::ArrayHdr*>(this), &value);   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
}

template <class T, class Allocator, class SpecPolicy>
inline void Array<T, Allocator, SpecPolicy>::Remove(const T& value) {
    static_assert(sizeof(T) == 8, "retail's Remove (0x140002cc0) is the 8-byte element's");
    rtcoll::ArrayRemove8(reinterpret_cast<rtcoll::ArrayHdr*>(this), &value);   // Array<T> hands its own header to the erased core  // CAST-OK: elem_stride
}

// ============================================================
// Collection::detail — the LinkedList node allocation seam.
//
// The node allocator and the node free are the two halves of retail's
// 0x180032750 / 0x180009c30, which each open-code
// `Enter(_singleton+0x640); Allocate/Free(_singleton, …); Leave` inline. Both
// are defined ONCE in src/engine/collection/linkedlist.cpp, where the argument
// that they are the same instruction sequence as the ported
// `System::MemoryAllocator::MemAlloc`/`MemFree` (0x1800c7a50 / 0x1800c7ad0) is
// written out in full. They are declared here and defined there, so that under
// BANISHED_UNIT_TEST linkedlist.cpp defines a counting test allocator instead;
// this header stays free of <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================
namespace detail {
void* AllocateListNode(uint64_t bytes, int align);
void  ListNodeFree(void* p);
}  // namespace detail

// ============================================================
// Collection::LinkedList<T, Allocator> — doubly linked, head + tail + count.
//
// ENDGAME B.3.2 step 1 (LIST core): this WAS a plain layout struct; it is now
// the real class template, with the four ported bodies of
// src/engine/collection/linkedlist.cpp moved onto it as members. Nothing about the
// LAYOUT changed — every static_assert below is the one that was already here,
// and the class is still standard-layout with public data (see "STEP 1 KEEPS
// THE FIELDS PUBLIC" below), so every `offsetof` in the three headers that
// embed it still holds:
//     src/engine/ui/UserInterface.h   _dialogs  (LinkedList<DialogInstancePtr>)
//     src/engine/ui/DialogInstance.h  _children (LinkedList<InternalBasePtr>)
//     src/engine/graphics/Scene.h     _globals  (LinkedList<Geometry*>)
//
// LAYOUT EVIDENCE (unchanged, Wave 2):
// Pinned by UI::UserInterface::CleanRemovedDialogs (0x18011f790), which unlinks
// a node from the dialog list embedded at UserInterface+0x00. Every branch of
// that unlink names one field:
//   node == *(this+0x00)  -> *(this+0x00) = node[+0x10]   head, and next=+0x10
//   node == *(this+0x08)  -> *(this+0x08) = node[+0x08]   tail, and prev=+0x08
//   node[+0x08] != 0      -> *(node[+0x08] + 0x10) = node[+0x10]  prev->next
//   node[+0x10] != 0      -> *(node[+0x10] + 0x08) = node[+0x08]  next->prev
//   *(int*)(this+0x10) -= 1                                       count
// and the search loop compares `*node` against the dialog pointer (element at
// +0x00) while advancing through node[+0x10].
//
// The container is 0x18 (24) bytes: head +0x00, tail +0x08, count +0x10, and
// UserInterface's next member — an Array — starts at +0x18. The 4 bytes at
// +0x14 are never touched (see _pad14 below).
//
// Iterator is EIGHT BYTES and is just the node pointer: the whole body of
// UI::UserInterface::GetFirstDialog (0x180031150), which returns
// LinkedList<...>::Iterator by value, is `*(void**)retval = *(void**)this`.
//
// Node allocation/free goes through DefaultAllocator exactly like Set's does —
// CleanRemovedDialogs frees the unlinked node with MemoryAllocator::Free under
// the +0x640 critical section.
//
// ---------------------------------------------------------------------------
// WHAT THE MEMBERS ARE, AND WHERE EACH BODY CAME FROM
// ---------------------------------------------------------------------------
// Retail ships ONE instantiation of this template (T = an 8-byte pointer), and
// MSVC monomorphised it, so the "retail address" of each member below is the
// address of that one instantiation's copy. The template reproduces it
// byte-for-byte at T = 8; the generic spelling only replaces the baked-in
// `0x18` / `+0x8` / `+0x10` immediates with `sizeof(Node)` and the field names.
//
//   | member     | retail      | notes                                        |
//   |------------|-------------|----------------------------------------------|
//   | NewNode    | 0x180032750 | MemAlloc(sizeof(Node), 8) + init; first arg  |
//   |            |             | NEVER READ by retail (see linkedlist.cpp)    |
//   | FreeNode   | 0x180009c30 | MemFree with an ignored leading argument     |
//   | Remove     | 0x18007a010 | find by VALUE, unlink, free; a miss is SILENT|
//   | Clear      | 0x1800325f0 | free every node, zero head/tail/count        |
//   | Add        | inlined at  | append at the TAIL. Retail never emitted it  |
//   |            | 0x180120440 | out of line; the same sequence is inlined in |
//   |            |             | UI::UserInterface::BringDialogToFront and in |
//   |            |             | Graphics::Scene's global-geometry add.       |
//   | GetFirst   | 0x180031150 | `*(void**)retval = *(void**)this` — the head |
//   | GetCount   | inline      | the `_count` field; no out-of-line body      |
//
// THERE IS NO DESTRUCTOR, DELIBERATELY. Retail's callers call Clear() by hand
// (UI::UserInterface's dtor tail-JMPs to 0x1800325f0; ~DialogInstance calls it
// on `_children`), and every ported body that owns a list still does the same.
// Declaring `~LinkedList()` here would run a SECOND Clear on those same bytes
// after the manual one — the identical double-free trap B31_RULES records for
// System::String's release asymmetry. The destructor arrives when the last
// hand-written Clear() call site is converted, not before.
//
// LIKEWISE NO USER-PROVIDED DEFAULT CTOR (B31_RULES class rule 5): the three
// embedders above are zero-initialised by their own ported constructors, which
// write head/tail/count as explicit stores, and a `LinkedList() {}` here would
// silently turn `Foo f = {};` in every one of them into "links uninitialised".
//
// STEP 1 KEEPS THE FIELDS PUBLIC. Files outside this header still touch
// `_head`/`_tail`/`_count` directly (src/engine/ui/userinterface.cpp,
// src/engine/ui/combodialog_ctor.cpp, src/engine/ui/dialog_create.cpp,
// src/engine/graphics/scene.cpp's constructor and src/ext/phone_menu/ among
// them). Making them private now would break them, for no layout benefit —
// all-public and all-private are both standard-layout.
// ============================================================
template <class T, class Allocator = DefaultAllocator>
class LinkedList {
 public:
    struct Node {
        T     element;   // +0x00
        Node* prev;      // align8(sizeof(T)) + 0x00
        Node* next;      // align8(sizeof(T)) + 0x08
    };
    struct Iterator {
        Node* node;      // +0x00 — the entire iterator

        // Sugar only; retail's iterator is the bare pointer and every retail
        // walk is `for (n = list->_head; n; n = n->next)`. These add no state
        // and no member, which the sizeof assert below keeps honest.
        T&        operator*()  const { return node->element; }
        T*        operator->() const { return &node->element; }
        Iterator& operator++()       { node = node->next; return *this; }
        bool operator==(const Iterator& o) const { return node == o.node; }
        bool operator!=(const Iterator& o) const { return node != o.node; }
        explicit operator bool() const { return node != nullptr; }
    };

    // ---- 0x180032750 — allocate and initialise one node --------------------
    //
    // Retail, for the T=8 instantiation:
    //     MOV RSI,RDX                  ; RDX = &value.  RCX IS NEVER READ.
    //     ... Enter ; node = Allocate(_singleton, 0x18, 8) ; Leave ...
    //     TEST RDI,RDI / JZ -> return 0
    //     XOR ECX,ECX
    //     MOV [RDI],RCX                ; node->element = 0   <- DEAD, and kept
    //     MOV RAX,[RSI] / MOV [RDI],RAX; node->element = *value
    //     MOV [RDI+0x8],RCX            ; node->prev = 0
    //     MOV [RDI+0x10],RCX           ; node->next = 0
    //
    // `0x18` is `sizeof(Node)`; the alignment argument is the literal 8 retail
    // passes (this header's DefaultAllocator block: Set nodes pass 8 too, and
    // the argument is alignof(T), not sizeof(T)). Both evaluate to retail's
    // immediates at T=8. The dead `element = T()` store before the real one is
    // retail's and is kept.
    //
    // STATIC because retail's first argument is never read: the body is not a
    // member call at all, and BringDialogToFront calls it without setting RCX.
    static Node* NewNode(const T& value) {
        // The raw MemAlloc plus field stores below construct no object, and
        // Clear/Remove free a node without destroying one. That is retail's own
        // shape and it is sound only for a T whose copy is a byte copy and
        // whose destruction is a no-op — which retail's single instantiation
        // (a pointer) is, and which COLLECTION_INVENTORY.md §5.1 requires
        // Object::ExternalPtr/InternalBasePtr to stay. Asserted inside the
        // member rather than in the class body so an instantiation that never
        // allocates a node is not constrained by it.
        static_assert(__is_trivially_copyable(T),
                      "LinkedList nodes are raw-allocated: T must be trivially copyable");
        static_assert(__has_trivial_destructor(T),
                      "LinkedList nodes are raw-freed: T must be trivially destructible");

        Node* node = static_cast<Node*>(detail::AllocateListNode(sizeof(Node), 8));
        if (!node) { return nullptr; }

        node->element = T();     // retail's dead store, kept — see above
        node->element = value;
        node->prev    = nullptr;
        node->next    = nullptr;
        return node;
    }

    // ---- 0x180009c30 — free one node ---------------------------------------
    //
    //     TEST RDX,RDX / JZ ret        ; the null test is on the SECOND argument
    //     ... Enter / Free(_singleton, RDX) / Leave ...
    //
    // The first argument is the container: retail homes it at [RSP+0x8] and
    // never reads it back, which is why this is a member whose `this` goes
    // unused. The null test is retail's own (MemFree's own first instruction),
    // not a guard added to paper over a crash.
    void FreeNode(Node* node) const { detail::ListNodeFree(node); }

    // ---- append at the TAIL ------------------------------------------------
    //
    // Inlined by retail at UI::UserInterface::BringDialogToFront 0x180120440,
    // transcribed arm for arm including which store increments `_count` first:
    //
    //     180120469  MOV RCX,[RBX+0x8]     ; tail
    //     180120472  MOV [RCX+0x10],RAX    ; tail->next = node
    //     18012047a  MOV [RAX+0x8],RCX     ; node->prev = tail
    //     18012047e  INC [RBX+0x10] / MOV [RBX+0x8],RAX
    //   (tail was null:)
    //     180120490  INC [RBX+0x10] / MOV [RBX],RAX / MOV [RBX+0x8],RAX
    //
    // `_head` is the BACKMOST dialog and `_tail` the frontmost, per
    // src/engine/ui/UserInterface.h — so "add" is "bring to the front" for
    // that consumer and "append" for Graphics::Scene's global list, which
    // inlines the same three stores (Scene::AddGlobalObject, src/engine/graphics/scene.cpp).
    void Add(const T& value) {
        Node* node = NewNode(value);
        if (Node* t = _tail) {
            t->next    = node;
            node->prev = t;
            ++_count;
            _tail = node;
        } else {
            ++_count;
            _head = node;
            _tail = node;
        }
    }

    // ---- 0x18007a010 — Remove(&value) --------------------------------------
    //
    // Find the node whose ELEMENT equals `value`, unlink it, zero its links,
    // decrement the count, and tail-jump into 0x180009c30 to free it.
    //
    // Three details that a tidier rewrite would get wrong:
    //
    //  * THE HEAD TEST COMPARES AGAINST THE ORIGINAL HEAD, held in R8 from
    //    before the search (`MOV R8,[RCX]` at 0x18007a010, `CMP RDX,R8` at
    //    0x18007a031), not against a re-read of `_head`. Nothing in this body
    //    can change the head in between, so the two agree — but writing
    //    `if (n == _head)` would make that an assumption instead of a
    //    transcription.
    //  * THE SEARCH IS BY VALUE, not by node, AND ONLY THE FIRST MATCH GOES:
    //    `value` is loaded ONCE, before the walk (0x18007a01e), the walk stops
    //    on the first equal element, and the body returns after the single free
    //    without resuming. A list holding the same value twice keeps the second.
    //  * A MISS IS SILENT. If the head is null, or the walk runs off the end,
    //    the body simply returns: no free, no count change.
    //    BringDialogToFront relies on that — it calls Remove unconditionally on
    //    a dialog that may not be in the list yet.
    //
    // The unlink itself matches, branch for branch, the one this header pins
    // from UI::UserInterface::CleanRemovedDialogs (0x18011f790).
    void Remove(const T& value) {
        Node* head = _head;
        if (!head) { return; }

        const T want = value;          // loaded ONCE, before the walk
        Node*   n    = head;
        while (!(n->element == want)) {
            n = n->next;
            if (!n) { return; }
        }

        if (n == head)  _head = n->next;      // the ORIGINAL head — see above
        if (n == _tail) _tail = n->prev;
        if (Node* prev = n->prev) prev->next = n->next;
        if (Node* next = n->next) next->prev = n->prev;
        n->prev = nullptr;
        n->next = nullptr;
        --_count;

        FreeNode(n);                          // retail's tail JMP 0x180009c30
    }

    // ---- 0x1800325f0 — Clear() ---------------------------------------------
    //
    // Retail, in full:
    //
    //     MOV R14,RCX / MOV RSI,[RCX]        ; n = list->head
    //     TEST RSI,RSI / JZ done
    //   loop:
    //     MOV RDI,[RSI+0x10]                 ; next = n->next  -- READ BEFORE THE FREE
    //     ... Enter ; Free(_singleton, RSI) ; Leave ...
    //     MOV RSI,RDI / TEST RDI,RDI / JNZ loop
    //   done:
    //     XOR EAX,EAX
    //     MOV [R14+0x8],RAX                  ; tail  = 0
    //     MOV [R14],RAX                      ; head  = 0
    //     MOV dword [R14+0x10],EAX           ; count = 0
    //
    // Three things a tidier rewrite would get wrong, all load-bearing:
    //
    //  * `next` IS READ BEFORE THE NODE IS FREED. Reading it after would be a
    //    use-after-free that works for exactly as long as the allocator does
    //    not reuse the block.
    //  * IT FREES NODES, NOT ELEMENTS. No refcount and no destructor is
    //    touched. Adding one would be a second release of a handle somebody
    //    else still owns — for `_children` the elements are child dialogs that
    //    UI::UserInterface owns, not the parent.
    //  * THE THREE STORES ARE UNCONDITIONAL — they happen on an already-empty
    //    list too — `_count` is a DWORD store, and `_pad14` is NOT written.
    void Clear() {
        Node* n = _head;
        while (n) {
            Node* next = n->next;   // read BEFORE the free — see above
            FreeNode(n);
            n = next;
        }
        _tail  = nullptr;
        _head  = nullptr;
        _count = 0;
    }

    // ---- 0x180031150 — the head, as an Iterator ----------------------------
    // `UI::UserInterface::GetFirstDialog` returns LinkedList<...>::Iterator by
    // value and its entire body is `*(void**)retval = *(void**)this`.
    Iterator GetFirst() const { return Iterator{_head}; }
    Iterator GetLast()  const { return Iterator{_tail}; }
    Iterator GetEnd()   const { return Iterator{nullptr}; }

    // The `_count` field. Retail emitted no out-of-line body for it; every
    // reader loads +0x10 directly.
    int32_t GetCount() const { return _count; }

    // ---- the fields, still public in step 1 (see the block comment) --------
    Node*   _head;       // +0x00
    Node*   _tail;       // +0x08
    int32_t _count;      // +0x10
    int32_t _pad14;      // +0x14: NOT PINNED — never read or written by any
                         //        decompiled body; present so sizeof == 0x18.
                         //        (Read "0x10" until Wave 7 lane C: a typo, and
                         //        one the same comment block contradicts 26
                         //        lines up, which states 0x18 correctly. The
                         //        arithmetic settles it — head 8 + tail 8 +
                         //        count 4 + pad 4 = 0x18 — and it is now pinned
                         //        by a static_assert in
                         //        src/engine/ui/UserInterface.h.)
};

// ============================================================
// Collection::Set<T, Allocator, Key, Compare> — an AVL tree. THE "Map".
//
// Four template parameters, from the two mangled names that spell it out:
//   ?$Set@VFilename@System@@UDefaultAllocator@Collection@@V12@UAVLCompare@4@
//     = Set<System::Filename, Collection::DefaultAllocator,
//           System::Filename, Collection::AVLCompare>
//   ?$Set@UCachedParseData@TextReader@IO@@UDefaultAllocator@Collection@@
//        U123@UAVLCompare@5@
//     = Set<IO::TextReader::CachedParseData, DefaultAllocator,
//           IO::TextReader::CachedParseData, AVLCompare>
// In both, Key == T: the key is the element (or its leading sub-object).
//
// CONTAINER, 0x10 bytes:
//   +0x00 root node pointer   — read as `*param_1` by Insert (FUN_18002a170)
//                               and Find (0x18002ace0)
//   +0x08 int32 node count    — Insert does `*(int*)(param_1+8) += 1` on every
//                               successful insert
//   +0x0c NOT PINNED (padding)
// Independently corroborated on the exe side by src/game/Entity/EntityList.h,
// which has TWO adjacent Sets: root +0x68 / count +0x70, root +0x78 / count
// +0x80 — a 0x10 stride, live in-game for months.
//
// NODE = { T element; Node* parent; Node* left; Node* right;
//          int16 balance; int16 height; uint32 pad; }
//      = align8(sizeof(T)) + 0x20 bytes.
//
// Element FIRST, so the pointer tail floats with sizeof(T). Three independent
// instantiations, three different element sizes, all agreeing:
//   T = 8    (exe EntityList tree)              node 0x28; parent +0x08,
//                                               left +0x10, right +0x18,
//                                               balance +0x20, height +0x22
//   T = 0x10 (Pair<String,String>, Map_Insert)  node 0x30; parent +0x10,
//                                               left +0x18, right +0x20,
//                                               balance +0x28, height +0x2a
//   T = 0x18 (Input::KeyMap key set, 1800a76c0) node 0x38; parent +0x18,
//                                               left +0x20, right +0x28
//
// The 0x10 case is exact: FUN_18000f970 allocates 0x30 bytes with alignment 8
// and hands them to the node constructor FUN_18000fb00, which writes
//   [0]=key [1]=value [2]=parent [3]=left(0) [4]=right(0)
// and zeroes the DWORD at +0x28, i.e. balance and height together.
//
// BALANCE AND HEIGHT, from the retrace/rebalance walk FUN_18002a310:
//   height(node)  = max(left  ? left->height  + 1 : 0,
//                       right ? right->height + 1 : 0)
//   balance(node) = rightHeight - leftHeight        // NOTE THE SIGN
//   balance > +1 -> rotate left  (right-heavy), preceded by a right rotation
//                   of the right child when that child's balance < 0
//   balance < -1 -> rotate right (left-heavy),  preceded by a left rotation
//                   of the left child when that child's balance > 0
// A leaf has height 0. The walk climbs via `parent` to the root. The two
// rotation helpers for the 0x10-element instantiation are FUN_18002a590 (left)
// and FUN_18002a6c0 (right).
//
// LOOKUP (0x18002ace0) descends left while searchKey < nodeKey and right while
// nodeKey < searchKey, returning the node when neither holds and null on
// falling off the tree. IN-ORDER TRAVERSAL (inlined in 0x1800a76c0) is the
// textbook parent-pointer successor walk, no threading.
//
// Because the pointer tail's offsets are a function of sizeof(T), MSVC emits a
// SEPARATE copy of insert/find/rebalance per instantiation with the offsets
// baked in. Do not assume one ported Set body serves two element types.
//
// ============================================================================
// ENDGAME B.3.2 step 1 (2026-09-13): `Set` IS NOW THE REAL CLASS.
// ----------------------------------------------------------------------------
// What changed: the POD above became a class with retail's member names
// (`Insert`, `Find`, `Remove`, `Clear`, `GetCount`, `Iterator`). What did NOT
// change: one byte of the layout, and not one of the AVL bodies. The class is a
// WRAPPER around the four already-ported AVL cores, selected by
// `align8(sizeof(T))`:
//
//   | element width | node  | ported core                       | file |
//   |---------------|-------|-----------------------------------|------|
//   | 0x8           | 0x28  | 0x1800274c0/27e20/27f50           | src/engine/io/modification_avl8.cpp |
//   | 0x10          | 0x30  | 0x18000adc0/b040/b170/aba0        | src/engine/collection/set_avl.cpp |
//   | 0x18          | 0x38  | 0x180023170/23570/236a0           | src/engine/collection/set_avl_e24.cpp |
//   | 0x30          | 0x50  | 0x180030540/307b0/308e0          | src/engine/collection/avl_trees.cpp |
//
// The rotations are NOT re-derived here (COLLECTION_INVENTORY.md §5 hazard 3:
// re-decompiling a body that is already ported is how a dropped argument gets
// re-introduced). `detail::AvlOps<Node, W>` forwards to those bodies through
// `src/engine/collection/set_core.cpp`.
//
// `detail::AvlGeneric<Node>` is the SAME algorithm written once over a node
// type instead of over baked-in offsets, and it is used for two things only:
//   * a width retail never instantiated (there is no ported body to call), and
//   * the host unit test (`BANISHED_UNIT_TEST`), except
//     tests/unit/set_ported_test.cpp (BANISHED_UNIT_PORTED_AVL).
// It is a generalisation OF the ported bodies — set_avl.cpp's Recompute,
// RotateLeft (0x18000b040), RotateRight (0x18000b170), Retrace (0x18000adc0)
// and Erase (0x18000aba0), transcribed with `Node*` in place of the offsets —
// not an independent re-derivation, and the retail addresses are on each body.
//
// THE THREE NODE LAYOUTS STAY DISTINCT. They are distinct here for the same
// reason set_avl.h and set_avl_e24.h are two files:
// one walk over two layouts
// returns the right answer for a key that happens to be the root and is
// permanently wrong for the next one. The layout is computed from `sizeof(T)`
// by the `Node` struct below, exactly as before, and the static_asserts at the
// bottom of this header still check all three widths field by field.
//
// DUPLICATE KEYS: `Insert` returns the EXISTING node and does NOT insert again
// and does NOT bump `_count` — retail's equal arm (0x180030418 -> 0x180030443,
// and the same arm in every other insert core). That rule is a unit test.
// ============================================================
template <class T,
          class Compare   = AVLCompare,
          class Allocator = DefaultAllocator,
          class Key       = T>
class Set;

namespace detail {

// ---------------------------------------------------------------------------
// The node-allocation seam. Retail's Set nodes come from
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) and go back
// through 0x180009c30, the allocator-lock Free wrapper WHOSE FIRST ARGUMENT IS
// IGNORED (set_avl.cpp's "THE FIFTH HELPER" banner). Both seams are defined in
// src/engine/collection/set_core.cpp, which calls the allocator by name.
// ---------------------------------------------------------------------------
void* SetNodeAlloc(uint64_t bytes, int align);
void  SetNodeFree(void* node);

// A non-default `Allocator` policy (the counting allocator the unit tests use)
// supplies its own statics; `DefaultAllocator` means retail's two doors.
template <class Allocator>
struct AllocTraits {
    static void* Allocate(uint64_t bytes, int align) { return Allocator::Allocate(bytes, align); }
    static void  Free(void* p)                    { Allocator::Free(p); }
    // The free as a POINTER. The erase core takes it by pointer, and the
    // `AvlOps<…,0x10>` forwarder recognises retail's own door by comparing
    // against `&SetNodeFree` — so this must be the door itself for
    // `DefaultAllocator`, not a wrapper around it.
    static void (*FreeFn())(void*) { return &AllocTraits::Free; }
};
template <>
struct AllocTraits<DefaultAllocator> {
    static void* Allocate(uint64_t bytes, int align) { return SetNodeAlloc(bytes, align); }
    static void  Free(void* p)                    { SetNodeFree(p); }
    static void (*FreeFn())(void*) { return &SetNodeFree; }
};

// ---------------------------------------------------------------------------
// Key traits. Only a POINTER key has a null state, and retail's descent treats
// it asymmetrically (a null SEARCH key goes LEFT, a null NODE key goes RIGHT) —
// see avl_trees.cpp's group-B banner. Every other key type has no null arm at
// all, which is why `IsNull` is a compile-time false for them and the arms fold
// away.
// ---------------------------------------------------------------------------
template <class K> struct KeyTraits {
    static bool IsNull(const K&) { return false; }
};
template <> struct KeyTraits<char*> {
    static bool IsNull(char* const& k) { return k == nullptr; }
};
template <> struct KeyTraits<const char*> {
    static bool IsNull(const char* const& k) { return k == nullptr; }
};

// `Collection::AVLCompare`, retail's inlined comparison (0x18002a1c4): a
// lexicographic byte scan finished with a SIGNED char subtraction. Defined in
// set_core.cpp so the one transcription serves every instantiation; it is the
// same body avl_trees.cpp's `StrCmpSigned` carries for the string map.
int AvlStrCmpSigned(const char* a, const char* b);

template <class Compare, class K>
struct CompareTraits {
    static bool Less(const K& a, const K& b) { return Compare::Less(a, b); }
};
template <class K>
struct CompareTraits<AVLCompare, K> {
    static bool Less(const K& a, const K& b) { return a < b; }
};
template <>
struct CompareTraits<AVLCompare, char*> {
    static bool Less(char* const& a, char* const& b) { return AvlStrCmpSigned(a, b) < 0; }
};
template <>
struct CompareTraits<AVLCompare, const char*> {
    static bool Less(const char* const& a, const char* const& b) {
        return AvlStrCmpSigned(a, b) < 0;
    }
};

// ---------------------------------------------------------------------------
// AvlGeneric<NodeT> — set_avl.cpp's four bodies with `NodeT*` in place of the
// baked-in offsets. Read set_avl.cpp's banners for the evidence behind every
// line here; the transcription notes that matter to a reader of THIS file are
// repeated inline.
//
// The container is the `Set` itself (`SetT`, its `_root` / `_count`): the
// {root, count} header every Set instantiation and every ported core agrees on
// (set_avl.h, set_avl_e24.h, and Set's own layout below).
// ---------------------------------------------------------------------------
template <class NodeT>
struct AvlGeneric {

    // 0x18000adee..0x18000ae1f. 16-BIT arithmetic throughout, and the max
    // DEFAULTS TO THE RIGHT height (`MOVZX EAX,CX / CMOVG AX,DX`).
    static void Recompute(NodeT* n) {
        const int16_t l = n->left  ? static_cast<int16_t>(n->left->height  + 1) : 0;
        const int16_t r = n->right ? static_cast<int16_t>(n->right->height + 1) : 0;
        n->balance = static_cast<int16_t>(r - l);
        n->height  = (l > r) ? l : r;
    }

    // retail 0x18000b040 — rotate LEFT about `n`.
    template <class SetT>
    static NodeT* RotateLeft(SetT* set, NodeT* n) {
        if (n == set->_root) set->_root = n->right;

        NodeT* p = n->parent;
        if (p) {
            if (p->left == n) p->left  = n->right;
            else              p->right = n->right;
        }

        NodeT* r  = n->right;
        r->parent = n->parent;
        n->right  = r->left;
        if (n->right) n->right->parent = n;
        r->left   = n;
        n->parent = r;

        if (r->left)  Recompute(r->left);
        if (r->right) Recompute(r->right);
        Recompute(r);
        return r;
    }

    // retail 0x18000b170 — the exact mirror.
    template <class SetT>
    static NodeT* RotateRight(SetT* set, NodeT* n) {
        if (n == set->_root) set->_root = n->left;

        NodeT* p = n->parent;
        if (p) {
            if (p->left == n) p->left  = n->left;
            else              p->right = n->left;
        }

        NodeT* l  = n->left;
        l->parent = n->parent;
        n->left   = l->right;
        if (n->left) n->left->parent = n;
        l->right  = n;
        n->parent = l;

        if (l->left)  Recompute(l->left);
        if (l->right) Recompute(l->right);
        Recompute(l);
        return l;
    }

    // retail 0x18000adc0 — the retrace. THE SECOND PARAMETER IS A SLOT, NOT A
    // NODE, and retail re-reads it after every rotation because a rotation can
    // move a different node into the position being examined. A null child
    // skips the rotation ENTIRELY, not just the pre-rotation.
    template <class SetT>
    static void Retrace(SetT* setRaw, NodeT** slot) {
        if (*slot == nullptr) return;

        for (;;) {
            NodeT* n      = *slot;
            NodeT* parent = n->parent;      // captured BEFORE any rotation

            Recompute(n);

            NodeT* cur = *slot;
            if (cur->balance > 1) {
                NodeT* r = cur->right;
                if (r) {
                    if (r->balance < 0) {
                        RotateRight(setRaw, r);
                        cur = *slot;
                    }
                    RotateLeft(setRaw, cur);
                }
            }

            cur = *slot;
            if (cur->balance < -1) {
                NodeT* l = cur->left;
                if (l) {
                    if (l->balance > 0) {
                        RotateLeft(setRaw, l);
                        cur = *slot;
                    }
                    RotateRight(setRaw, cur);
                }
            }

            *slot = parent;
            if (!parent) return;
        }
    }

    // retail 0x18000aba0 — erase `z`, free its node, retrace. Case 3 is a
    // PREDECESSOR splice (the rightmost node of z->left), not the textbook
    // successor: reading it the other way produces a valid AVL tree with the
    // same keys and a DIFFERENT SHAPE.
    //
    // `freeNode` is the node deallocation. For `DefaultAllocator` the caller
    // passes retail's own 0x180009c30 door, so this is byte-for-byte retail's
    // free; a test allocator passes its own.
    template <class SetT>
    static void Erase(SetT* set, NodeT* z, void (*freeNode)(void*)) {
        NodeT* retraceFromParent = z->parent;
        set->_count -= 1;                        // 0x18000abae, before anything else

        NodeT* retraceFromChild = nullptr;
        NodeT* replacement      = nullptr;

        if (z->right == nullptr || z->left == nullptr) {
            // ---- cases 1 and 2 (retail JMPs case 2 into case 1's tail) -----
            NodeT* p     = z->parent;
            NodeT* child = z->right == nullptr ? z->left : z->right;
            if (p) { if (p->left == z) p->left = child; else p->right = child; }
            else   { set->_root = child; }

            replacement      = child;
            retraceFromChild = replacement;
            if (replacement) replacement->parent = z->parent;

            freeNode(z);
            Retrace(set, replacement ? &retraceFromChild : &retraceFromParent);
            return;
        }

        // ---- case 3: both children -------------------------------------
        NodeT* s = z->left;
        while (s->right) s = s->right;           // the PREDECESSOR

        replacement       = nullptr;
        retraceFromParent = nullptr;

        if (s != z->left) {
            NodeT* sp = s->parent;
            sp->right = s->left;
            if (s->left) s->left->parent = sp;
            retraceFromParent = sp;
            replacement       = sp;
            s->left = z->left;
            Recompute(sp);
        }

        s->right = z->right;
        if (s->left)  s->left->parent  = s;
        if (s->right) s->right->parent = s;
        s->parent = z->parent;

        if (z->parent == nullptr) {
            set->_root = s;
            s->parent = nullptr;                 // 0x18000ad28, a SECOND store
            retraceFromChild = set->_root;
        } else {
            retraceFromChild = s;
            NodeT* p = z->parent;
            if (p->left == z) p->left = s; else p->right = s;
        }

        freeNode(z);
        Retrace(set, replacement ? &retraceFromParent : &retraceFromChild);
    }
};

// ---------------------------------------------------------------------------
// The ported cores, reached through one forwarder each. Defined in
// src/engine/collection/set_core.cpp, which is the only file that knows the
// three node types; the widths that have no ported body fall through to
// AvlGeneric.
//
// Under BANISHED_UNIT_TEST every width uses AvlGeneric: the generic core IS
// those bodies (see the banner above). One host test,
// tests/unit/set_ported_test.cpp, defines BANISHED_UNIT_PORTED_AVL before this
// header and links the four ported cores (set_avl.cpp, set_avl_e24.cpp,
// avl_trees.cpp, modification_avl8.cpp): its element types are its own, so the
// specialisations below exist for them alone and every other test keeps the
// generic core.
// ---------------------------------------------------------------------------
#if !defined(BANISHED_UNIT_TEST) || defined(BANISHED_UNIT_PORTED_AVL)
// element 8 / node 0x28 — the `Set<System::Filename>` path set every
// `IO::Modification::Mod` carries. ENDGAME B.3.2 closing pass; the three bodies
// are src/engine/io/modification_avl8.cpp's (its PART 1 banner carries the
// instruction-level evidence).
//
// Its retrace's first argument is `&set.root`, not the set — that is what
// `CMP RDX,[RCX]` at 0x180027f50's entry reads. `&set._root == (void*)&set`,
// since `_root` is at +0x00, so the `void* set` the other widths pass IS that
// pointer and the two spellings agree byte for byte. The retrace never touches
// `_count`, exactly like the other three widths.
//
// The retrace is R-IO's port in modification_avl8.cpp, typed on its node
// (`RtIO::ModSetNode`), called by name; the one-line forwarder `PortedRetrace8`
// was deleted in B.4 wave X. (Its two rotations, 0x180027e20 / 0x180027f50, had
// no body and no caller left; their declarations were deleted in B.4 wave X.)
}  // namespace detail
}  // namespace Collection
namespace RtIO {
extern "C" void RtColl_SetInsertFixup_1800274c0(ModSetNode** root, ModSetNode** cursor);   // 0x1800274c0
}  // namespace RtIO
namespace Collection {
namespace detail {

// The other three widths take the erased node and container types of their
// ported core (B.4 R-COLL: typed at the core, set_core.cpp forwards with no
// cast). The rotations 0x18000b040 / b170, 0x180023570 / 236a0 and
// 0x1800307b0 / 308e0 are reached only from inside their own retrace, so the
// template has no forwarder for them.
void  PortedRetrace16(rtsetavl::Set* set, rtsetavl::Node** slot);        // 0x18000adc0
void  PortedErase16(rtsetavl::Set* set, rtsetavl::Node* z);              // 0x18000aba0
void  PortedRetrace24(rtsetavl24::Set* set, rtsetavl24::Node** slot);    // 0x180023170
void  PortedRetrace48(RtObject::TypeNode** root, RtObject::TypeNode** slot);  // 0x180030540 (root = &set._root)
#endif

template <class NodeT, unsigned ElemWidth>
struct AvlOps : AvlGeneric<NodeT> {};

// The typed wrappers over the ONE ported core per width (B.4 policy: retail
// shares these bodies across every element type of the width). Each casts a
// typed `Set<T>` / `Set<T>::Node` to the core's erased node once, at the call:
// the layouts agree by the LayoutProbe asserts below (parent / left / right /
// balance / height at `align8(sizeof(T)) + 0x00 .. 0x1a`).
#if !defined(BANISHED_UNIT_TEST) || defined(BANISHED_UNIT_PORTED_AVL)
template <class NodeT>
struct AvlOps<NodeT, 0x8> : AvlGeneric<NodeT> {
    template <class SetT>
    static void Retrace(SetT* set, NodeT** slot) {
        RtIO::RtColl_SetInsertFixup_1800274c0(reinterpret_cast<RtIO::ModSetNode**>(set), reinterpret_cast<RtIO::ModSetNode**>(slot));  // CAST-OK: elem_stride
    }
    // No erase was ever emitted at this width either: `Mod::~Mod` tears the
    // whole tree down post-order (0x180010470) and nothing ever removes one
    // key. The generic body — the same algorithm — serves a caller that does.
};

template <class NodeT>
struct AvlOps<NodeT, 0x10> : AvlGeneric<NodeT> {
    template <class SetT>
    static void Retrace(SetT* set, NodeT** slot) {
        PortedRetrace16(reinterpret_cast<rtsetavl::Set*>(set), reinterpret_cast<rtsetavl::Node**>(slot));  // CAST-OK: elem_stride
    }
    // The ported erase frees through retail's own 0x180009c30 door, so the
    // `freeNode` a DefaultAllocator Set hands it is the same function; a Set
    // with any other allocator gets the generic body instead of a node freed
    // to the wrong heap.
    template <class SetT>
    static void Erase(SetT* set, NodeT* z, void (*freeNode)(void*)) {
        if (freeNode == &SetNodeFree) PortedErase16(reinterpret_cast<rtsetavl::Set*>(set), reinterpret_cast<rtsetavl::Node*>(z));  // CAST-OK: elem_stride
        else                          AvlGeneric<NodeT>::Erase(set, z, freeNode);
    }
};

template <class NodeT>
struct AvlOps<NodeT, 0x18> : AvlGeneric<NodeT> {
    template <class SetT>
    static void Retrace(SetT* set, NodeT** slot) {
        PortedRetrace24(reinterpret_cast<rtsetavl24::Set*>(set), reinterpret_cast<rtsetavl24::Node**>(slot));  // CAST-OK: elem_stride
    }
    // No erase was ever emitted for this width (set_avl_e24.h's table: "erase
    // (no door)") — the generic body, which is the same algorithm, serves it.
};

template <class NodeT>
struct AvlOps<NodeT, 0x30> : AvlGeneric<NodeT> {
    template <class SetT>
    static void Retrace(SetT* set, NodeT** slot) {
        PortedRetrace48(reinterpret_cast<RtObject::TypeNode**>(set), reinterpret_cast<RtObject::TypeNode**>(slot));  // CAST-OK: elem_stride
    }
};
#endif  // !BANISHED_UNIT_TEST || BANISHED_UNIT_PORTED_AVL

// ---------------------------------------------------------------------------
// InsertCorePattern — the keyed BST descent that EVERY retail
// `Set::InsertCore` is one copy of:
//
//   0x180030380  Set<Pair<u32,TypeRecord>>   (avl_trees.cpp, group A)
//   0x18002a170  Set<Pair<String,String>>    (avl_trees.cpp, group B)
//   0x180025c70  Set<Pair<Filename,u64>>     (avl_trees.cpp, group D)
//   0x1800275e0  Set<Pair<Filename,u32>>     (set_avl_pathid.cpp)
//
// and which `Set<T>::Insert` below also uses. The four ported cores are now
// one-line calls into this with their own policy; nothing about their
// comparators, their allocation doors, their probe counters or their re-reads
// moved. What the policy supplies is exactly what differed between them.
//
// The shape, arm for arm (avl_trees.cpp's group-B banner has the flags):
//
//   goLeft  = nodeKey   != null && (searchKey == null || Less(search, node))
//   goRight = searchKey != null && (nodeKey   == null || Less(node, search))
//   neither -> EQUAL: *outSlot = the existing node, RETURN WITHOUT counting.
//
// `searchKey` is RE-READ before the right test (0x18002a212), and the count is
// incremented on the insert arms only.
// ---------------------------------------------------------------------------
template <class P>
typename P::Node** InsertCorePattern(typename P::SetT* set,
                                     typename P::Node** outSlot,
                                     const typename P::Element* pair) {
    using Node = typename P::Node;

    Node* node = P::Root(set);
    if (node == nullptr) {
        P::OnRootInsert();
        Node* n = P::Alloc(set, pair, nullptr);
        P::Count(set) += 1;
        P::Root(set) = n;
        *outSlot     = n;
        return outSlot;
    }

    Node* fresh = nullptr;

    for (;;) {
        // Retail re-reads both keys every iteration; neither is hoisted.
        // Bound by reference, not copied: a `Key` can be the whole element.
        const auto& nodeKey = P::NodeKey(node);

        bool goLeft = false;
        {
            const auto& searchKey = P::SearchKey(pair);
            if (!P::IsNull(nodeKey)) {
                if (P::IsNull(searchKey))            { goLeft = true; P::OnNullSearchKey(); }
                else if (P::Less(searchKey, nodeKey))  goLeft = true;
            } else {
                P::OnNullNodeKey();
            }
        }
        if (goLeft) {
            P::OnDescLeft();
            Node* l = node->left;
            if (l == nullptr) {
                fresh      = P::Alloc(nullptr, pair, node);
                node->left = fresh;
                break;
            }
            node = l;
            continue;
        }

        const auto& searchKey = P::SearchKey(pair);   // 0x18002a212 — retail re-reads
        bool goRight = false;
        if (!P::IsNull(searchKey)) {
            if (P::IsNull(nodeKey))              goRight = true;
            else if (P::Less(nodeKey, searchKey)) goRight = true;
        }
        if (!goRight) {                          // EQUAL — a lookup, not an insert
            P::OnEqual();
            *outSlot = node;
            return outSlot;
        }
        P::OnDescRight();
        Node* r = node->right;
        if (r == nullptr) {
            fresh       = P::Alloc(nullptr, pair, node);
            node->right = fresh;
            break;
        }
        node = r;
    }

    // *** THE RETRACE TAKES A SLOT, NOT A NODE. *** Retail hands it the stack
    // cell the descent has been updating and re-reads it after every rotation.
    Node* slot = node;
    P::Retrace(set, &slot);
    P::Count(set) += 1;
    *outSlot = fresh;
    return outSlot;
}

// Counter hooks default to nothing; the ported cores' policies override the
// ones whose probe counters they keep (avl_trees.h's ProbeCounters).
struct NoCounters {
    static void OnRootInsert() {}
    static void OnDescLeft() {}
    static void OnDescRight() {}
    static void OnEqual() {}
    static void OnNullSearchKey() {}
    static void OnNullNodeKey() {}
};

}  // namespace detail

// ============================================================
// Collection::Set<T, Compare, Allocator, Key> — the class.
//
// Parameter ORDER note: retail's mangled name spells the parameters
// `Set<T, Allocator, Key, Compare>`. The class takes `Compare` second because
// that is the one a caller actually varies (B32_RULES step 1); `Key` and
// `Allocator` keep their retail defaults and `Key == T` remains the rule (the
// key is the element or its leading sub-object).
// ============================================================
template <class T, class Compare, class Allocator, class Key>
class Set {
public:
    struct Node {
        T        element;   // +0x00
        Node*    parent;    // align8(sizeof(T)) + 0x00
        Node*    left;      // align8(sizeof(T)) + 0x08
        Node*    right;     // align8(sizeof(T)) + 0x10
        int16_t  balance;   // align8(sizeof(T)) + 0x18  right height - left height
        int16_t  height;    // align8(sizeof(T)) + 0x1a  leaf == 0
        uint32_t _padTail;  // align8(sizeof(T)) + 0x1c  zeroed together with
                            //   balance and height as one dword by the node ctor
    };

    // +0x00 — NOT PINNED. No Set::Iterator is exported or returned by value
    // anywhere in the DLL; this is shaped after LinkedList::Iterator, which IS
    // pinned. Treat the LAYOUT as a guess; the WALK is retail's (the textbook
    // parent-pointer successor walk inlined into 0x1800a76c0, no threading).
    class Iterator {
    public:
        Iterator() : node(nullptr) {}
        explicit Iterator(Node* n) : node(n) {}
        T&   operator*() const { return node->element; }
        T*   operator->() const { return &node->element; }
        Node* GetNode() const { return node; }
        bool operator==(const Iterator& o) const { return node == o.node; }
        bool operator!=(const Iterator& o) const { return node != o.node; }
        Iterator& operator++() { node = Successor(node); return *this; }
    private:
        static Node* Successor(Node* n) {
            if (n->right) { n = n->right; while (n->left) n = n->left; return n; }
            Node* p = n->parent;
            while (p && p->right == n) { n = p; p = p->parent; }
            return p;
        }
        Node* node;         // +0x00 — the entire iterator
    };

    // Retail's ctor is the compiler's: two stores of zero, the root qword and
    // the count dword (FireWork 0x1400e9c19 / 0x1400e9c1c; EntityList
    // 0x1400c3fe5 / 0x1400c3fe8 and 0x1400c3ff4 / 0x1400c3ff7; CitizenBehavior
    // 0x140090b08 / 0x140090b0b and 0x140090b60 / 0x140090b63). `_pad0c` is
    // NOT written: it keeps whatever the storage held (rows 640 / 651 / 652).
    // `= default` is not used because the two fields must be zeroed, not left
    // indeterminate (B31_RULES class rule 5).
    Set() : _root(nullptr), _count(0) {}

    // The class OWNS its nodes, so the destructor is Clear. Retail's containers
    // are destroyed the same way (retail's 0x18000f7d0 is exactly this body).
    ~Set() { Clear(); }

    Set(const Set&)            = delete;   // retail never copies one
    Set& operator=(const Set&) = delete;

    // ---- Insert -------------------------------------------------------
    // Returns the node holding `element`. ON A DUPLICATE KEY IT RETURNS THE
    // EXISTING NODE, inserts nothing and does not bump the count.
    Node* Insert(const T& element) {
        Node* out = nullptr;
        detail::InsertCorePattern<Policy>(this, &out, &element);
        return out;
    }

    // ---- InsertInPlace / Emplace (ENDGAME B.3.2 closing pass) ----------
    //
    // The same descent, the same retrace, the same duplicate-key rule — but the
    // element is CONSTRUCTED IN THE NODE from `args` instead of copy-constructed
    // from a `T` the caller built first. `AllocNode` already placement-news into
    // the node, so what this removes is the caller's temporary, not a copy
    // inside the container.
    //
    // *** WHEN IT IS OBSERVABLY IDENTICAL TO `Insert`, AND WHEN IT IS NOT. ***
    // `Insert(tmp)` runs `T(const T&)` once and, at the caller, `~T()` on the
    // temporary. `Emplace(key, args…)` runs `T(args…)` once and nothing else.
    // For a trivially-copyable, trivially-destructible `T` those two are the
    // same bytes and the same (empty) side effects, so the swap is free.
    // For `System::String` / `System::Filename` / `Object::ExternalPtr<T>`
    // elements they are NOT the same: the temporary's AddRef and its matching
    // Release disappear, and COLLECTION_INVENTORY.md hazard 2 records that those
    // refcount sequences are observable through the resave gate. Retail's four
    // keyed inserts all build their `Pair` on the stack and let the insert copy
    // it, so every one of them keeps `Insert` — this is for a caller that has
    // no temporary to begin with.
    //
    // `key` is separate from the arguments because the descent needs the key
    // BEFORE anything is allocated: it is read on every comparison of the walk
    // and the node only exists at the end of it.
    template <class Ctor>
    Node* InsertInPlace(const Key& key, const Ctor& construct) {
        EmplaceReq<Ctor> req = { &key, &construct };
        Node* out = nullptr;
        detail::InsertCorePattern<EmplacePolicy<Ctor>>(this, &out, &req);
        return out;
    }

    template <class... Args>
    Node* Emplace(const Key& key, Args&&... args) {
        // Not <utility>'s forward: this header is a types header and the cast
        // is the whole of what `std::forward` does here.
        auto ctor = [&](void* raw) { new (raw) T(static_cast<Args&&>(args)...); };
        return InsertInPlace(key, ctor);
    }

    // ---- Find (retail 0x18002ace0) ------------------------------------
    // Descends left while searchKey < nodeKey and right while nodeKey <
    // searchKey, returns the node when neither holds and null on falling off
    // the tree — with the same null arms the insert descent uses.
    Node* Find(const Key& key) const {
        Node* node = _root;
        while (node) {
            const Key& nodeKey = KeyOf(node->element);
            bool goLeft = false;
            if (!detail::KeyTraits<Key>::IsNull(nodeKey)) {
                if (detail::KeyTraits<Key>::IsNull(key))    goLeft = true;
                else if (Cmp::Less(key, nodeKey))           goLeft = true;
            }
            if (goLeft) { node = node->left; continue; }

            bool goRight = false;
            if (!detail::KeyTraits<Key>::IsNull(key)) {
                if (detail::KeyTraits<Key>::IsNull(nodeKey)) goRight = true;
                else if (Cmp::Less(nodeKey, key))            goRight = true;
            }
            if (!goRight) return node;
            node = node->right;
        }
        return nullptr;
    }

    // ---- Remove -------------------------------------------------------
    // The element is destroyed, then the node is unlinked, freed and the tree
    // retraced by the erase core. For every retail instantiation `T` is
    // trivially destructible and the first step compiles away, which is why the
    // ported 0x18000aba0 has no element destructor in it.
    void Remove(Node* n) {
        if (!n) return;
        n->element.~T();
        Ops::Erase(this, n, Alloc::FreeFn());
    }
    bool Remove(const Key& key) {
        Node* n = Find(key);
        if (!n) return false;
        Remove(n);
        return true;
    }

    // ---- Clear (retail 0x18000f7d0) -----------------------------------
    // Post-order: both of the root's subtrees, then the root's element, then
    // the root's node, then the two zero stores — which retail performs even
    // when the tree was already empty (it branches to a second copy of them).
    void Clear() {
        Node* n = _root;
        if (n) {
            DestroySubtree(n->left);
            DestroySubtree(n->right);
            n->element.~T();
            Alloc::Free(n);
        }
        _root  = nullptr;
        _count = 0;
    }

    // retail 0x18000f890 / 0x180023130 / 0x180008340 — the recursive
    // post-order destroy Clear stands on, exposed so callers outside the class
    // run it too (io/modification.cpp, io/filesystem_packages.cpp). Both
    // children are loaded BEFORE the free:
    // the allocator writes a boundary tag into the block it takes back.
    static void DestroySubtree(Node* n) {
        if (!n) return;
        DestroySubtree(n->left);
        DestroySubtree(n->right);
        n->element.~T();
        Alloc::Free(n);
    }

    int32_t GetCount() const { return _count; }
    bool    IsEmpty()  const { return _count == 0; }
    Node*   GetRoot()  const { return _root; }

    Iterator Begin() const {
        Node* n = _root;
        if (!n) return Iterator(nullptr);
        while (n->left) n = n->left;
        return Iterator(n);
    }
    Iterator End() const { return Iterator(nullptr); }

private:
    // The key is the element, or its leading sub-object for a `Pair` element —
    // retail's rule (Key == T in both mangled names that spell the parameters
    // out). The cast is the leading-subobject one, not a type pun.
    static const Key& KeyOf(const T& e) {
        return *reinterpret_cast<const Key*>(&e);  // CAST-OK: elem_stride
    }

    enum : unsigned { kElemWidth = (sizeof(T) + 7u) & ~7u };
    using Ops   = detail::AvlOps<Node, kElemWidth>;
    using Alloc = detail::AllocTraits<Allocator>;
    using Cmp   = detail::CompareTraits<Compare, Key>;

    // The node allocator: `align8(sizeof(T)) + 0x20` bytes at alignment 8, the
    // element copy-constructed in place, parent linked, children null, balance
    // and height zeroed as one dword. That is every retail node ctor
    // (0x18000fb00, 0x180023060, 0x1800087c0, 0x1800278c0) with the element
    // copy written as `T`'s copy constructor instead of the monomorphised
    // qword stores.
    //
    // A NULL ALLOCATION RETURNS NULL AND THE CALLER STORES IT AS THE CHILD —
    // retail does not check either (CLAUDE.md: do not "fix" a faithful crash).
    static Node* AllocNode(const T* src, Node* parent) {
        Node* n = static_cast<Node*>(Alloc::Allocate(sizeof(Node), 8));
        if (!n) return nullptr;
        new (static_cast<void*>(&n->element)) T(*src);
        n->parent   = parent;
        n->left     = nullptr;
        n->right    = nullptr;
        n->balance  = 0;
        n->height   = 0;
        // `_padTail` is deliberately NOT written: retail's node constructors
        // zero balance and height as ONE dword and leave the tail dword
        // holding allocator bytes (set_avl.h's pad2c, set_avl_e24.h's pad34).
        return n;
    }

    // The in-place request `InsertInPlace` walks the tree with: the key to
    // descend by, and the constructor to run in the node once the slot is
    // found. It plays the part `const T*` plays for `Insert`.
    template <class Ctor>
    struct EmplaceReq { const Key* key; const Ctor* ctor; };

    // `AllocNode` with the copy constructor replaced by `*req->ctor`. Every
    // other store — parent, the two null children, balance and height as one
    // dword, `_padTail` deliberately untouched — is byte-for-byte AllocNode's,
    // which is byte-for-byte retail's four node constructors'.
    template <class Ctor>
    static Node* EmplaceNode(const EmplaceReq<Ctor>* req, Node* parent) {
        Node* n = static_cast<Node*>(Alloc::Allocate(sizeof(Node), 8));
        if (!n) return nullptr;
        (*req->ctor)(static_cast<void*>(&n->element));
        n->parent  = parent;
        n->left    = nullptr;
        n->right   = nullptr;
        n->balance = 0;
        n->height  = 0;
        return n;
    }

    template <class Ctor>
    struct EmplacePolicy : detail::NoCounters {
        using Node    = typename Set::Node;
        using SetT    = Set;
        using Element = EmplaceReq<Ctor>;
        static Node*&   Root(Set* s)  { return s->_root; }
        static int32_t& Count(Set* s) { return s->_count; }
        static const Key& NodeKey(const Node* n)      { return KeyOf(n->element); }
        static const Key& SearchKey(const Element* e) { return *e->key; }
        static bool IsNull(const Key& k) { return detail::KeyTraits<Key>::IsNull(k); }
        static bool Less(const Key& a, const Key& b) { return Cmp::Less(a, b); }
        static Node* Alloc(Set* /*ignored*/, const Element* e, Node* parent) {
            return EmplaceNode(e, parent);
        }
        static Node* Alloc(decltype(nullptr), const Element* e, Node* parent) {
            return EmplaceNode(e, parent);
        }
        static void Retrace(Set* s, Node** slot) { Ops::Retrace(s, slot); }
    };

    // The policy `InsertCorePattern` runs on. It is the same shape the four
    // ported insert cores now hand it.
    struct Policy : detail::NoCounters {
        using Node    = typename Set::Node;
        using SetT    = Set;
        using Element = T;
        static Node*&   Root(Set* s)  { return s->_root; }
        static int32_t& Count(Set* s) { return s->_count; }
        static const Key& NodeKey(const Node* n)   { return KeyOf(n->element); }
        static const Key& SearchKey(const T* e)    { return KeyOf(*e); }
        static bool IsNull(const Key& k) { return detail::KeyTraits<Key>::IsNull(k); }
        static bool Less(const Key& a, const Key& b) { return Cmp::Less(a, b); }
        static Node* Alloc(Set* /*ignored*/, const T* e, Node* parent) {
            return AllocNode(e, parent);
        }
        static Node* Alloc(decltype(nullptr), const T* e, Node* parent) {
            return AllocNode(e, parent);
        }
        static void Retrace(Set* s, Node** slot) { Ops::Retrace(s, slot); }
    };
    friend struct Policy;

public:
    // PUBLIC, and deliberately so until the classes that EMBED a Set are
    // converted (§B.2): `Object::ExternalFactory`'s two trees, `Input::KeyMap`
    // and the exe's `EntityList` all reach these by name or by offset today,
    // and the layout probe at the bottom of this header takes `offsetof` of
    // them. All three members share one access specifier, so the type stays
    // standard-layout and that `offsetof` stays well defined.
    Node*   _root;          // +0x00
    int32_t _count;         // +0x08
    int32_t _pad0c;         // +0x0c: NOT PINNED — never read; the ctor leaves it as found
};

// ============================================================================
// Layout checks. Templates cannot be static_assert'ed in the abstract, so the
// three element widths the DLL evidence actually covers are instantiated here
// and checked field by field.
// ============================================================================
namespace LayoutProbe {

struct E8  { uint64_t a; };            // 8  — exe EntityList tree element
struct E16 { void* k; void* v; };      // 16 — Map_Insert's {key,value} pair
struct E24 { uint64_t a, b, c; };      // 24 — Input::KeyMap's key-set element

using A8 = Array<E8>;
static_assert(sizeof(A8) == 0x10,              "Collection::Array must be 0x10 bytes");
static_assert(offsetof(A8, _data)     == 0x00, "Array::_data @ +0x00");
static_assert(offsetof(A8, _size)     == 0x08, "Array::_size @ +0x08");
static_assert(offsetof(A8, _capacity) == 0x0c, "Array::_capacity @ +0x0c");
static_assert(sizeof(Array<uint8_t>)  == 0x10, "sizeof(Array<T>) is T-independent");
static_assert(sizeof(Array<E24>)      == 0x10, "sizeof(Array<T>) is T-independent");

using L8 = LinkedList<E8>;
static_assert(sizeof(L8) == 0x18,               "Collection::LinkedList must be 0x18 bytes");
static_assert(offsetof(L8, _head)  == 0x00,     "LinkedList::_head @ +0x00");
static_assert(offsetof(L8, _tail)  == 0x08,     "LinkedList::_tail @ +0x08");
static_assert(offsetof(L8, _count) == 0x10,     "LinkedList::_count @ +0x10");
static_assert(sizeof(L8::Iterator) == 0x08,     "LinkedList::Iterator is one pointer");
static_assert(sizeof(L8::Node) == 0x18,         "LinkedList::Node(T=8) is 0x18");
static_assert(offsetof(L8::Node, prev) == 0x08, "LinkedList::Node::prev @ +0x08 for T=8");
static_assert(offsetof(L8::Node, next) == 0x10, "LinkedList::Node::next @ +0x10 for T=8");

using S8  = Set<E8>;
using S16 = Set<E16>;
using S24 = Set<E24>;
static_assert(sizeof(S8) == 0x10,           "Collection::Set must be 0x10 bytes");
static_assert(offsetof(S8, _root)  == 0x00, "Set::_root @ +0x00");
static_assert(offsetof(S8, _count) == 0x08, "Set::_count @ +0x08");

// T = 8: matches src/game/Entity/EntityList.h EntityTreeNode exactly.
static_assert(sizeof(S8::Node) == 0x28,            "Set::Node(T=8) is 0x28");
static_assert(offsetof(S8::Node, parent)  == 0x08, "Set::Node::parent @ +0x08 for T=8");
static_assert(offsetof(S8::Node, left)    == 0x10, "Set::Node::left @ +0x10 for T=8");
static_assert(offsetof(S8::Node, right)   == 0x18, "Set::Node::right @ +0x18 for T=8");
static_assert(offsetof(S8::Node, balance) == 0x20, "Set::Node::balance @ +0x20 for T=8");
static_assert(offsetof(S8::Node, height)  == 0x22, "Set::Node::height @ +0x22 for T=8");

// T = 16: matches the 0x30-byte allocation in FUN_18000f970 and the offsets
// baked into Map_Find (0x18002ace0) and the rebalance FUN_18002a310.
static_assert(sizeof(S16::Node) == 0x30,            "Set::Node(T=0x10) is 0x30");
static_assert(offsetof(S16::Node, parent)  == 0x10, "Set::Node::parent @ +0x10 for T=0x10");
static_assert(offsetof(S16::Node, left)    == 0x18, "Set::Node::left @ +0x18 for T=0x10");
static_assert(offsetof(S16::Node, right)   == 0x20, "Set::Node::right @ +0x20 for T=0x10");
static_assert(offsetof(S16::Node, balance) == 0x28, "Set::Node::balance @ +0x28 for T=0x10");
static_assert(offsetof(S16::Node, height)  == 0x2a, "Set::Node::height @ +0x2a for T=0x10");

// T = 24: matches the traversal offsets inlined into Input::KeyMap::GetIdList.
static_assert(sizeof(S24::Node) == 0x38,           "Set::Node(T=0x18) is 0x38");
static_assert(offsetof(S24::Node, parent) == 0x18, "Set::Node::parent @ +0x18 for T=0x18");
static_assert(offsetof(S24::Node, left)   == 0x20, "Set::Node::left @ +0x20 for T=0x18");
static_assert(offsetof(S24::Node, right)  == 0x28, "Set::Node::right @ +0x28 for T=0x18");

}  // namespace LayoutProbe
}  // namespace Collection

// ==== B3R W0-P0c: Runtime bodies named by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below carries its retail address in
// its trailing comment. It is declared here, copied from its definition
// (linkage, namespace, types), so a caller can call it by name.
namespace rtsetavl { struct Set; struct Node; }   // src/engine/collection/set_avl.h

namespace rtcoll {
// ---- collection/array_destroy.cpp
void* RtColl_ArrayAssign_180013640(void* self, const void* src);  // 0x180013640
void* RtColl_ArrayAssign_180014c70(void* self, const void* src);  // 0x180014c70
void* RtColl_ArrayAssign_18001b1a0(void* self, const void* src);  // 0x18001b1a0
void* RtColl_ArrayAssign_18001b270(void* self, const void* src);  // 0x18001b270
void* RtColl_ArrayAssign_18003ce30(void* self, const void* src);  // 0x18003ce30
void* RtColl_ArrayAssign_18003cf20(void* self, const void* src);  // 0x18003cf20
void* RtColl_ArrayAssign_180052700(void* self, const void* src);  // 0x180052700
void* RtColl_ArrayAssign_1800571b0(void* self, const void* src);  // 0x1800571b0
void* RtColl_ArrayCopyCtor_18001c620(void* self, const void* src);  // 0x18001c620
void* RtColl_ArrayDtorAnim_18001c1e0(void* self);  // 0x18001c1e0
void* RtColl_ArrayDtorElem_180004a90(void* self);  // 0x180004a90
void* RtColl_ArrayDtorElem_180004b70(void* self);  // 0x180004b70
void* RtColl_ArrayDtorElem_180004c80(void* self);  // 0x180004c80
void* RtColl_ArrayDtorElem_180004d60(void* self);  // 0x180004d60
void* RtColl_ArrayDtorElem_180004f50(void* self);  // 0x180004f50
void* RtColl_ArrayDtorElem_180010ab0(void* self);  // 0x180010ab0
void* RtColl_ArrayDtorElem_18001c2d0(void* self);  // 0x18001c2d0
void* RtColl_ArrayDtorElem_180033880(void* self);  // 0x180033880
void* RtColl_ArrayDtorElem_180039280(void* self);  // 0x180039280
void* RtColl_ArrayDtorElem_180039360(void* self);  // 0x180039360
void* RtColl_ArrayDtorElem_18003e310(void* self);  // 0x18003e310
void* RtColl_ArrayDtorElem_18003e4a0(void* self);  // 0x18003e4a0
void* RtColl_ArrayDtorElem_1800416d0(void* self);  // 0x1800416d0
void* RtColl_ArrayDtorElem_180043a00(void* self);  // 0x180043a00
void* RtColl_ArrayDtorElem_18004fa30(void* self);  // 0x18004fa30
void* RtColl_ArrayDtorElem_18004fb00(void* self);  // 0x18004fb00
void* RtColl_ArrayDtorElem_18004fbd0(void* self);  // 0x18004fbd0
void* RtColl_ArrayDtorElem_1800516c0(void* self);  // 0x1800516c0
void* RtColl_ArrayDtorElem_18005bf60(void* self);  // 0x18005bf60
void* RtColl_ArrayDtorElem_180065d40(void* self);  // 0x180065d40
void* RtColl_ArrayDtorElem_180073720(void* self);  // 0x180073720
// ---- collection/array.cpp
void* RtColl_ArrayDtor_180013da0(void* a);  // 0x180013da0
void* RtColl_ArrayDtor_180013e70(void* a);  // 0x180013e70
void* RtColl_ArrayDtor_180014d00(void* a);  // 0x180014d00
void* RtColl_ArrayDtor_18001c020(void* a);  // 0x18001c020
void* RtColl_ArrayDtor_18001c3b0(void* a);  // 0x18001c3b0
void* RtColl_ArrayDtor_18001c750(void* a);  // 0x18001c750
void* RtColl_ArrayDtor_180021490(void* a);  // 0x180021490
void* RtColl_ArrayDtor_1800248e0(void* a);  // 0x1800248e0
void* RtColl_ArrayDtor_1800249c0(void* a);  // 0x1800249c0
void* RtColl_ArrayDtor_180024aa0(void* a);  // 0x180024aa0
void* RtColl_ArrayDtor_180037310(void* a);  // 0x180037310
void* RtColl_ArrayDtor_180039440(void* a);  // 0x180039440
void* RtColl_ArrayDtor_18003d8c0(void* a);  // 0x18003d8c0
void* RtColl_ArrayDtor_18003da00(void* a);  // 0x18003da00
void* RtColl_ArrayDtor_18003db40(void* a);  // 0x18003db40
void* RtColl_ArrayDtor_18003e0c0(void* a);  // 0x18003e0c0
void* RtColl_ArrayDtor_180043cb0(void* a);  // 0x180043cb0
void* RtColl_ArrayDtor_18004cc90(void* a);  // 0x18004cc90
void* RtColl_ArrayDtor_180051d80(void* a);  // 0x180051d80
void* RtColl_ArrayDtor_180062320(void* a);  // 0x180062320
void* RtColl_ArrayDtor_180065bf0(void* a);  // 0x180065bf0
void* RtColl_ArrayDtor_180065e80(void* a);  // 0x180065e80
void* RtColl_ArrayDtor_180079870(void* a);  // 0x180079870
void RtColl_ArraySetSizeFill_180023830(void* a, int n, const void* fill);  // 0x180023830
void RtColl_ArraySetSizeFill_180024fe0(void* a, int n, const void* fill);  // 0x180024fe0
void RtColl_ArraySetSizeFill_180028cd0(void* a, int n, const void* fill);  // 0x180028cd0
void RtColl_ArraySetSizeFill_1800298f0(void* a, int n, const void* fill);  // 0x1800298f0
void RtColl_ArraySetSizeFill_180033690(void* a, int n, const void* fill);  // 0x180033690
void RtColl_ArraySetSizeFill_180034860(void* a, int n, const void* fill);  // 0x180034860
void RtColl_ArraySetSizeFill_18003df80(void* a, int n, const void* fill);  // 0x18003df80
void RtColl_ArraySetSizeFill_18003e1a0(void* a, int n, const void* fill);  // 0x18003e1a0
void RtColl_ArraySetSizeFill_180043b40(void* a, int n, const void* fill);  // 0x180043b40
void RtColl_ArraySetSizeFill_18004bfc0(void* a, int n, const void* fill);  // 0x18004bfc0
void RtColl_ArraySetSizeFill_180063a80(void* a, int n, const void* fill);  // 0x180063a80
void RtColl_ArraySetSizeFill_180076900(void* a, int n, const void* fill);  // 0x180076900
void RtColl_ArraySetSizeFill_180076d10(void* a, int n, const void* fill);  // 0x180076d10
void RtColl_ArraySetSizeFill_18007a0d0(void* a, int n, const void* fill);  // 0x18007a0d0
void RtColl_ArraySetSize_1800136f0(void* a, int n);  // 0x1800136f0
void RtColl_ArraySetSize_180020e90(void* a, int n);  // 0x180020e90
void RtColl_ArraySetSize_180021220(void* a, int n);  // 0x180021220
void RtColl_ArraySetSize_180024c10(void* a, int n);  // 0x180024c10
void RtColl_ArraySetSize_180024d70(void* a, int n);  // 0x180024d70
void RtColl_ArraySetSize_180029510(void* a, int n);  // 0x180029510
void RtColl_ArraySetSize_180029630(void* a, int n);  // 0x180029630
void RtColl_ArraySetSize_180029a80(void* a, int n);  // 0x180029a80
void RtColl_ArraySetSize_18002f070(void* a, int n);  // 0x18002f070
void RtColl_ArraySetSize_18002fea0(void* a, int n);  // 0x18002fea0
void RtColl_ArraySetSize_1800370c0(void* a, int n);  // 0x1800370c0
void RtColl_ArraySetSize_18003d1c0(void* a);  // 0x18003d1c0
void RtColl_ArraySetSize_18003d430(void* a);  // 0x18003d430
void RtColl_ArraySetSize_18003e600(void* a, int n);  // 0x18003e600
void RtColl_ArraySetSize_18003ea50(void* a, int n);  // 0x18003ea50
void RtColl_ArraySetSize_18003eea0(void* a, int n);  // 0x18003eea0
void RtColl_ArraySetSize_180043d80(void* a, int n);  // 0x180043d80
void RtColl_ArraySetSize_1800492d0(void* a, int n);  // 0x1800492d0
void RtColl_ArraySetSize_18004ba90(void* a, int n);  // 0x18004ba90
void RtColl_ArraySetSize_18004cdf0(void* a, int n);  // 0x18004cdf0
void RtColl_ArraySetSize_180052e60(void* a, int n);  // 0x180052e60
void RtColl_ArraySetSize_180053190(void* a, int n);  // 0x180053190
void RtColl_ArraySetSize_180053f00(void* a, int n);  // 0x180053f00
void RtColl_ArraySetSize_1800547e0(void* a, int n);  // 0x1800547e0
void RtColl_ArraySetSize_180054a20(void* a, int n);  // 0x180054a20
void RtColl_ArraySetSize_180055db0(void* a, int n);  // 0x180055db0
void RtColl_ArraySetSize_180056240(void* a, int n);  // 0x180056240
void RtColl_ArraySetSize_1800567d0(void* a, int n);  // 0x1800567d0
void RtColl_ArraySetSize_180056af0(void* a, int n);  // 0x180056af0
void RtColl_ArraySetSize_180059140(void* a, int n);  // 0x180059140
void RtColl_ArraySetSize_180059930(void* a, int n);  // 0x180059930
void RtColl_ArraySetSize_180059e70(void* a, int n);  // 0x180059e70
void RtColl_ArraySetSize_18005a220(void* a, int n);  // 0x18005a220
void RtColl_ArraySetSize_18005a410(void* a, int n);  // 0x18005a410
void RtColl_ArraySetSize_18005cc60(void* a, int n);  // 0x18005cc60
void RtColl_ArraySetSize_1800604e0(void* a, int n);  // 0x1800604e0
void RtColl_ArraySetSize_1800616c0(void* a, int n);  // 0x1800616c0
void RtColl_ArraySetSize_180061880(void* a, int n);  // 0x180061880
void RtColl_ArraySetSize_180061bb0(void* a, int n);  // 0x180061bb0
void RtColl_ArraySetSize_180062110(void* a, int n);  // 0x180062110
void RtColl_ArraySetSize_1800660f0(void* a, int n);  // 0x1800660f0
void RtColl_ArraySetSize_180066350(void* a, int n);  // 0x180066350
void RtColl_ArraySetSize_1800666e0(void* a, int n);  // 0x1800666e0
void RtColl_ArraySetSize_180066bc0(void* a, int n);  // 0x180066bc0
void RtColl_ArraySetSize_180066e80(void* a, int n);  // 0x180066e80
void RtColl_ArraySetSize_1800764c0(void* a, int n);  // 0x1800764c0
void RtColl_ArraySetSize_180076a40(void* a);  // 0x180076a40
void RtColl_ArraySetSize_180077330(void* a, int n);  // 0x180077330
void RtColl_ArraySetSize_1800774d0(void* a, int n);  // 0x1800774d0
void RtColl_ArraySetSize_1800783d0(void* a);  // 0x1800783d0
void RtColl_ArraySetSize_1800799a0(void* a, int n);  // 0x1800799a0
void RtColl_ArraySetSize_18007a4d0(void* a, int n);  // 0x18007a4d0
void RtColl_ArraySetSize_18007a790(void* a, int n);  // 0x18007a790
void RtColl_ArraySetSize_18007a900(void* a, int n);  // 0x18007a900
void RtColl_ArraySetSize_18007aa40(void* a, int n);  // 0x18007aa40
// ---- collection/array_destroy.cpp
void RtColl_DestroyElem_180044050(uint8_t* elem);  // 0x180044050
void RtColl_DestroyInner_1800419b0(void* ignoredThis, void* base, int count);  // 0x1800419b0
void RtColl_DestroyInner_18004ff80(void* ignoredThis, void* base, int count);  // 0x18004ff80
void RtColl_DestroyInner_180052110(void* ignoredThis, void* base, int count);  // 0x180052110
void RtColl_DestroyInner_180052220(void* ignoredThis, void* base, int count);  // 0x180052220
void RtColl_DestroyStrings_180005570(void* ignoredThis, void* base, int count);  // 0x180005570
void RtColl_DestroyStrings_180005880(void* ignoredThis, void* base, int count);  // 0x180005880
void RtColl_DestroyStrings_180005930(void* ignoredThis, void* base, int count);  // 0x180005930
void RtColl_DestroyStrings_1800059e0(void* ignoredThis, void* base, int count);  // 0x1800059e0
void RtColl_DestroyStrings_180006ac0(void* ignoredThis, void* base, int count);  // 0x180006ac0
void RtColl_DestroyStrings_180011010(void* ignoredThis, void* base, int count);  // 0x180011010
void RtColl_DestroyStrings_18001cbf0(void* ignoredThis, void* base, int count);  // 0x18001cbf0
void RtColl_DestroyStrings_180026b50(void* ignoredThis, void* base, int count);  // 0x180026b50
void RtColl_DestroyStrings_180029190(void* ignoredThis, void* base, int count);  // 0x180029190
void RtColl_DestroyStrings_18002afc0(void* ignoredThis, void* base, int count);  // 0x18002afc0
void RtColl_DestroyStrings_1800341e0(void* ignoredThis, void* base, int count);  // 0x1800341e0
void RtColl_DestroyStrings_180039580(void* ignoredThis, void* base, int count);  // 0x180039580
void RtColl_DestroyStrings_180039640(void* ignoredThis, void* base, int count);  // 0x180039640
void RtColl_DestroyStrings_18003e3e0(void* ignoredThis, void* base, int count);  // 0x18003e3e0
void RtColl_DestroyStrings_18003f070(void* ignoredThis, void* base, int count);  // 0x18003f070
void RtColl_DestroyStrings_18004fe20(void* ignoredThis, void* base, int count);  // 0x18004fe20
void RtColl_DestroyStrings_18004fed0(void* ignoredThis, void* base, int count);  // 0x18004fed0
void RtColl_DestroyStrings_180054920(void* ignoredThis, void* base, int count);  // 0x180054920
void RtColl_DestroyStrings_180054ba0(void* ignoredThis, void* base, int count);  // 0x180054ba0
void RtColl_DestroyStrings_180056960(void* ignoredThis, void* base, int count);  // 0x180056960
void RtColl_DestroyVtxStream_180056cb0(void* ignoredThis, void* basev, int count);  // 0x180056cb0
// ---- collection/array.cpp
void* RtColl_I32ArrayAssign_18001cdf0(Collection::Array<int32_t>* dst,
                                      const Collection::Array<int32_t>* src);  // 0x18001cdf0
// ---- collection/array_destroy.cpp
void* RtColl_LayoutEntryAssign_180043980(uint8_t* dst, const uint8_t* src);  // 0x180043980 (B.4: the element op)
void* RtColl_SpriteEntryAssign_180004220(uint8_t* dst, const uint8_t* src);  // 0x180004220 (B.4: the element op)
}  // namespace rtcoll

namespace IO { class PackageFile; }   // io/PackageFile.h
namespace rtsort {
// ---- collection/introsort.cpp
void SortByKey70(IO::PackageFile** first, int32_t count);  // 0x1800264a0
void  EffectSlotAssign(uint8_t* dst, const uint8_t* src);  // 0x180051650 (B.4: the element op, rtcoll::AssignAs)
}  // namespace rtsort

// rtpathid::Insert / SubtreeFree (0x180027380 / 0x180027970): src/engine/collection/set_avl_pathid.h

// Collection::Pair<K, V> -- the element of a keyed Set (B7 LX, D7a). Retail's
// mangled names spell it (`Set<Pair<String,String>>`, the settings map of
// IO::Persistence +0x00 and System::Configuration +0x10). The key is the
// leading sub-object, so `Set<Pair<K,V>, Compare, Allocator, K>` descends on
// it; the compiler's member-wise copy and destroy are retail's: the node ctor
// 0x18000fb00 copies +0x00 then +0x08, the element dtor 0x18000fa90 releases
// +0x08 then +0x00 (reverse declaration order).
namespace Collection {
template <class K, class V>
struct Pair {
    K key;     // +0x00
    V value;   // align(sizeof(K))
};
}  // namespace Collection
// ==== end B3R W0-P0c ====
