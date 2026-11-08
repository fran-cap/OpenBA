// ============================================================================
// src/engine/io/modification.cpp — WAVE 9 LANE MOD
// IO::Modification's mods-menu cluster, plus the Mod-entry lifecycle triple
// ============================================================================
//
// WHY THIS FILE EXISTS AND WHY IT IS DIFFERENT FROM EVERY OTHER LANE
//
// Every other Wave 9 lane was handed a list of SYMBOL NAMES that
// unsatisfied_export_scan / the old DLL-era switch could see. This one came out of
// the address-door census (docs/history/runtime_port/wave9_unregistered_addr_doors.csv,
// ce1d8e9 corrected by 4ef55b6): every `ResolveAddr(Module::Runtime, 0x…)` in
// the tree minus every address anything registers. Ten of the 62 unregistered
// doors are the mods cluster, all resolved in engine/funcs/engine_funcs.cpp.
//
// A by-address door is the deepest form of the problem this wave keeps
// rediscovering: with the DLL absent it resolves to null, the stored function
// pointer is CALLED, and nothing in the run ever mentioned a name. That is
// exactly the failure that walled attempt 13 (the allocator) and attempt 14
// (IO::PackageFile's ctor).
//
// ---------------------------------------------------------------------------
// WHAT IS PORTED HERE
// ---------------------------------------------------------------------------
//
//   | retail      | symbol / role                                  |
//   |-------------|------------------------------------------------|
//   | 0x18000da90 | ?GetCount@Modification@IO@@QEBAHXZ             |
//   | 0x18000da80 | ?GetModification@Modification@IO@@QEBAAEBUMod… |
//   | 0x1800aeab0 | ?Enable@Modification@IO@@QEAAXH_N@Z            |
//   | 0x1800aeb20 | ?Swap@Modification@IO@@QEAAXHH@Z               |
//   | 0x1800b05f0 | ?SaveList@Modification@IO@@QEAAXXZ             |
//   | 0x1800101e0 | Mod::Mod(const Mod&)          (unexported)     |
//   | 0x180026ef0 | Mod::operator=(const Mod&)    (unexported)     |
//   | 0x180011460 | Mod::~Mod()                   (unexported)     |
//   | 0x180010110 | Array<Mod>::operator=         (unexported)     |
//
// plus the seven unexported container internals those five reach, which have
// no name in either shipped build and are therefore RegisterRuntimeAddr ONLY:
//
//   0x180010b80  Array<Mod>::~Array          (per-element ~Mod, then free)
//   0x180011110  Array<int32>::operator=     0x1800111a0  Array<int32>::~Array
//   0x180011220  Array<uint8>::operator=     0x1800112a0  Array<uint8>::~Array
//   0x1800091f0  Array<Filename>::operator=  0x1800098d0  Array<Filename>::~Array
//   0x1800104f0  Set<Filename> clone-subtree
//   0x180010470  Set<Filename> destroy-subtree
//   0x180010570  Set<Filename> node alloc+init
//   0x180009c30  Set<Filename> node free
//
// Every one of those addresses was grepped for an existing
// `RegisterRuntimeAddr(0x…` across runtime/ before being claimed here, exactly
// as lane FIND and lane PKG did, so no other lane's registration is shadowed.
//
// ---------------------------------------------------------------------------
// WHAT IS *NOT* PORTED, AND WHY  (the honest half of the deliverable)
// ---------------------------------------------------------------------------
//
// 0x1800af5f0  ?RefreshList@Modification@IO@@QEAAXXZ — HELD BACK.
//   ~450 instructions that stack-construct an IO::PackageFile (vtable
//   0x180155700), call its Open (0x1800b46b0) / GetName-family accessors
//   (0x180025310/50/90, 0x180025450/530/580) and Close (0x1800b4540), then
//   build a Filename-keyed set per mod and sort the array. IO::PackageFile is
//   the CURRENT cutover wall (WAVE9.md attempt 14: `[RtResolve] MISS
//   "??0PackageFile@IO@@QEAA@XZ" -> NULL`, VCALL-of-0 on the all-zero object).
//   Porting RefreshList before PackageFile would produce a body that cannot be
//   exercised and cannot be diffed. It is blocked on a NAMED dependency that
//   another lane already owns, which is the one blocker shape
//   reference_holdback_blockers_are_stale_check_them_first.md says to state
//   plainly rather than work around.
//
// 0x18002a090  the string-keyed map insert — HELD BACK, and NOT this lane's.
//   It is a two-instruction-deep wrapper: it addrefs the key and value into a
//   stack Pair<String,String>, calls 0x18002a170 (117 instructions: the real
//   insert, which allocates through 0x18000f970 and rebalances through
//   0x18002a310), then releases both. src/engine/system/configuration.cpp:315
//   already recorded the same disposition — "a red-black insert, a Collection
//   lane's work". This lane CORRECTS one word of that entry: configuration.cpp
//   calls it "unreachable on our boots", which is true of ITS call site (an
//   empty command line) but NOT of the address. persistence_registry_win.cpp:266
//   reaches the same address from ReadSettings, which every boot runs. So the
//   address is live even though the call site that documented it is not — see
//   WAVE9_LANE_MOD.md. It is left to the Collection lane rather than written
//   blind here: an AVL/red-black rebalance written from a paginated
//   disassembly, with no populated tree to test it against, is precisely the
//   silent-corruption case reference_uniform_population_is_not_a_verification.md
//   is about.
//
// ---------------------------------------------------------------------------
// ONE DELIBERATE DEVIATION FROM THE INSTRUCTION STREAM
// ---------------------------------------------------------------------------
// Retail INLINES the allocator lock at every allocation and free in this
// cluster: `MOV RBX,[0x1801fc718]; ADD RBX,0x640; EnterCriticalSection;
// Allocate/Free(_singleton, …); LeaveCriticalSection`. That is byte-for-byte
// what the composed statics ?MemAlloc@MemoryAllocator@System@@ (0x1800c7a50)
// and ?MemFree@ (0x1800c7ad0) do — allocator.cpp's RegisterAllocatorAddresses()
// (WAVE9.md attempt 14) registers both — so this file calls those instead of
// re-inlining the critical section eleven times. Same lock, same singleton,
// same order; only the inlining differs. Doing it the other way would mean
// eleven independent copies of the lock sequence, which is eleven chances to
// get the pairing wrong for zero behavioural gain.
//
// The null test on free is INSIDE 0x1800c7ad0 (`TEST RCX,RCX; JZ ret` is its
// first instruction), which is why the bodies below hand it possibly-null
// pointers exactly as retail does.
//
// ---------------------------------------------------------------------------
// DIFFABILITY, PER FUNCTION
// ---------------------------------------------------------------------------
//   GetCount / GetModification  full oracle; both are const reads of two
//                               fields and produce independent results.
//   Enable                      full BYTE oracle. Enable writes exactly two
//                               bytes and allocates nothing, so the diff can
//                               run ours, photograph the whole 0x80 record plus
//                               the 0x28 header, RESTORE the pre-image, run
//                               theirs, and memcmp. Restoring is only safe
//                               because nothing was allocated — see Swap for
//                               the case where it is not.
//   Swap                        SHADOW, by value. It cannot be replayed and its
//                               bytes cannot be restored: retail's swap deep
//                               copies the two Arrays and CLONES the Set (new
//                               heap nodes), so putting the old bytes back
//                               would leak them. The shadow instead asserts the
//                               post-condition against a pre-image: the shared
//                               (refcounted) members must have exchanged
//                               POINTERS exactly, and the deep-copied members
//                               must have exchanged VALUES. That is not
//                               tautological — it fails on a wrong offset, a
//                               skipped member, or a deep copy that lost
//                               elements.
//   SaveList                    the mutation it makes to the object is
//                               `mods[i].order = i`, which is IDEMPOTENT and
//                               allocates nothing, and its Persistence writes
//                               are idempotent too. So it IS replayable: run
//                               ours, photograph the order column, restore the
//                               pre-image, run theirs, compare the columns.
//                               Retail's run is the one that lands, so a diff
//                               run's live behaviour is retail's.
//   the lifecycle triple + the container internals
//                               NOT DIFFED, and not diffable: each one mutates
//                               a live object through the shared heap, so
//                               running the original as an oracle would double
//                               free at refcount 1. They are verified
//                               INDIRECTLY, through Swap's shadow — Swap is
//                               nothing but three calls into the triple, so a
//                               green Swap shadow is a statement about them.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstddef>    // offsetof — the Set<Filename> node-layout asserts below
#include <new>        // placement new into freshly allocated Filename storage

#include "io/Modification.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 IO: the class static
#include "system/Filename.h"
#include "collection/Collection.h"   // ENDGAME B.3.2 step 2 — see below
#include "system/MemoryAllocator.h"
#include "system/string_internal.h"

// ENDGAME B.3.1 step 2: the Filename copy-ctor/AdjustDelimiters entry points
// this file used to forward-declare here (0x1800067d0 / 0x1800c6970) are gone
// from this translation unit. Every use was one of Filename.h's documented
// idioms (a copy ctor, a Filename<-Filename assign-then-normalise) and is
// spelled as the class itself (B.4 acceptance MOD: `Mod::file` and the path
// set's elements are real `System::Filename`s).

namespace RtIO {


// ---------------------------------------------------------------------------
// The allocator boundary. See the banner: the composed statics, by address,
// with the same never-cache-a-null contract array.cpp uses (a caller that runs
// before RegisterAllocatorAddresses() must not freeze a null in for the life of
// the process).
// ---------------------------------------------------------------------------
}  // namespace RtIO

namespace RtIO {

// B3R RT-IO: the per-file ModMemAlloc / ModMemFree wrappers (which went through
// `rtcoll::DoorAlloc`/`DoorFree`, i.e. these same two statics) are gone; every
// site calls System::MemoryAllocator::MemAlloc / MemFree, the allocator the
// `Collection::Set<System::Filename, …>` of modification_refresh.cpp also uses,
// and the one BANISHED_REDZONE's instrument seat sits on.

// The String header's refcount word (String.h's accessor).
static inline int32_t& RefCount(char* s) {
    return RtSystem::StringRefCount(s);
}

// (B.4 acceptance MOD: `AssignShared` / `AdoptShared`, the release-then-adopt
// and the adopt retail inlines at each String member of Mod, are gone with the
// char* members: they ARE `System::String::operator=` (0x1800c9810's shape:
// the pointer compare, the release that stores `d - 8` before the free or null
// on the sharing branch, the unconditional store of the source, the AddRef)
// and the String copy ctor (the null store, then the adopt), which Mod's
// String members now run by name.)

// ============================================================================
// Collection internals — the unexported helpers the triple reaches
// ============================================================================

// The four ~Array bodies (0x1800111a0, 0x1800112a0, and the buffer-free tail of
// 0x1800098d0 / 0x180010b80) are the same six instructions with different
// element cleanup in front. `[RDI+8]` is ONE eight-byte store covering size AND
// capacity, so capacity IS reset here.
template <class T>
static void* ArrayFreeBuffer(ModArrayOf<T>* a) {
    ::System::MemoryAllocator::MemFree(a->data);          // retail's `if (p)` lives inside 0x1800c7ad0
    a->data = nullptr;
    a->size = 0;
    a->capacity = 0;
    return nullptr;               // retail leaves RAX == 0
}

// 0x1800111a0 / 0x1800112a0 (Collection::Array<int32> / <uint8>::~Array): the
// two erased entries that stood here had no caller left once PackageFile's icon
// and preview arrays and VertexLayout::_signature became typed members (B4
// X7-RT): those call `Collection::Array<T>::~Array`, the same free-and-zero.

// 0x180009af0, already ported by lane FIND (filesystem_find.cpp) and
// registered there. Declared, not re-implemented: two copies of a release loop
// is how a refcount idiom drifts.
void DestroyFilenameArray(void* ignoredThis, uint8_t* base, int count);

// 0x1800098d0 — Collection::Array<Filename>::~Array. Releases every element
// through 0x180009af0 first, then frees the buffer. (FileSystem's
// `_userPackFilenames` header; Mod::files is the class and runs the same body
// as its own ~Array / DestroyAndFree.)
void* RtColl_ArrayFilenameDtor_1800098d0(ModArrayOf<System::Filename>* a) {
    void* const elems = a->data;
    DestroyFilenameArray(a, static_cast<uint8_t*>(elems), a->size);   // CAST-OK: elem_stride
    return ArrayFreeBuffer(a);
}

// The growth rule shared by all four Array::operator= bodies, read at
// 0x18001013d..0x180010171 and identical (modulo stride/align) at 0x180011125,
// 0x180011235 and 0x18000921d:
//
//     int oldSize = dst->size;
//     dst->size = src->size;
//     if (dst->size > dst->capacity) {
//         dst->capacity = dst->size;
//         if (dst->size == oldSize + 1 && oldSize != 0)
//             dst->capacity = 3 * dst->size;          // LEA EAX,[RCX+RCX*2]
//         dst->data = MemAlloc((int64_t)dst->capacity * stride, align);
//     }
//
// The `oldSize` read happens BEFORE the store (`MOV ECX,[RDI+8]` then
// `MOV EDX,[RBX+8]` then `MOV [RBX+8],ECX`) — the push-one-element case is
// detected against the size the array had on entry.
// The copy loop's `ADD R9,RDX; JZ skip` is a test on `dst->data + i*stride`,
// not on `dst->data` — transcribed rather than simplified, because it is the
// only thing standing between a failed MemAlloc and a write through null, and
// it is also why the loop re-reads dst->size on every iteration.

// The free that opens both bodies is the element-free ~Array 0x1800111a0 /
// 0x1800112a0 (their own CALL in retail), on the typed member header: the
// unconditional MemFree (retail's `if (p)` lives inside 0x1800c7ad0), then the
// header zeroed (`[RDI+8]` one qword: size AND capacity).
template <class T>
static void* ArrayFreeBuffer(Collection::Array<T>* a) {
    ::System::MemoryAllocator::MemFree(a->_data);
    a->_data     = nullptr;
    a->_size     = 0;
    a->_capacity = 0;
    return nullptr;               // retail leaves RAX == 0
}

// ArrayAssignGrow's rule (above) on the typed header.
template <class T>
static void ArrayAssignGrow(Collection::Array<T>* dst, const Collection::Array<T>* src,
                            uint32_t stride, int align) {
    const int32_t oldSize = dst->_size;
    dst->_size = src->_size;
    if (dst->_size > dst->_capacity) {
        dst->_capacity = dst->_size;
        if (dst->_size == oldSize + 1 && oldSize != 0)
            dst->_capacity = dst->_size + dst->_size * 2;
        dst->_data = static_cast<T*>(::System::MemoryAllocator::MemAlloc(
            static_cast<uint64_t>(static_cast<int64_t>(dst->_capacity)) * stride, align));
    }
}

// 0x180011110 — Collection::Array<int32>::operator=. Every caller holds a
// `Collection::Array<uint32_t>` on both sides (Mod::ints, PackageFile::_iconData,
// GetIconData's by-value result).
void* RtColl_ArrayI32Assign_180011110(Collection::Array<uint32_t>* dst,
                                      const Collection::Array<uint32_t>* src) {
    ArrayFreeBuffer(dst);                       // CALL 0x1800111a0
    ArrayAssignGrow(dst, src, 4, 4);
    for (int32_t i = 0; i < dst->_size; ++i) {
        uint32_t* d = dst->_data + i;
        if (d == nullptr) continue;
        d[0] = src->_data[i];
    }
    return dst;
}

// 0x180011220 — Collection::Array<uint8>::operator=. Both sides are
// `Collection::Array<uint8_t>` (Mod::bytes, PackageFile::_previewData,
// GetPreviewData's by-value result, array.cpp's inner-array element op).
void* RtColl_ArrayU8Assign_180011220(Collection::Array<uint8_t>* dst,
                                     const Collection::Array<uint8_t>* src) {
    ArrayFreeBuffer(dst);                       // CALL 0x1800112a0
    ArrayAssignGrow(dst, src, 1, 1);
    for (int32_t i = 0; i < dst->_size; ++i) {
        uint8_t* d = dst->_data + i;
        if (d == nullptr) continue;
        *d = src->_data[i];
    }
    return dst;
}

// 0x1800091f0 — Collection::Array<Filename>::operator=: its own self-assignment
// early-out (`CMP RCX,RDX; JNZ`), ~Array 0x1800098d0, the growth rule above at
// stride 8, then the copy ctor 0x1800067d0 per element into the raw buffer. That
// is `Collection::Array<System::Filename>::Assign` (rtcoll::ArrayAssignCore with
// the trait row: self test, destroy-then-free, the same grow, placement copy per
// element behind the same computed-address test), so Mod::files assigns through
// it by name (B.4 acceptance MOD); the hand-written body that stood here is gone.

// The mod path set's node walk, typed on the set's own node
// (`ModFilenameSet::Node`, Modification.h).
using ModPathNode = ModFilenameSet::Node;

// 0x180010570 — Set<Filename> node alloc + init. RCX (the owning set) is
// spilled at 0x180010570 and NEVER READ AGAIN — the same shape lane FIND
// documented for DestroyFilenameArray, and the reason this signature carries an
// ignored first argument at all.
static ModPathNode* SetNodeNew(void* /*ignoredSet*/, const System::Filename* key,
                               ModPathNode* parent) {
    ModPathNode* n = static_cast<ModPathNode*>(::System::MemoryAllocator::MemAlloc(0x28, 8));
    if (n == nullptr) return nullptr;
    // CALL 0x1800067d0 — Filename copy ctor, into the freshly allocated node's
    // raw element bytes.
    new (&n->element) System::Filename(*key);
    n->parent = parent;
    n->left = nullptr;
    n->right = nullptr;
    n->balance = 0;                                       // MOV dword [RDI+0x20],EAX
    n->height = 0;
    return n;
}

// 0x1800104f0 — Set<Filename> clone-subtree. The `MOV word ptr [RAX+0x22]`
// immediately after the allocation is UNGUARDED in retail: a failed node
// allocation writes through null. Transcribed, not guarded — the project's rule
// is that a faithful crash names an upstream problem (an exhausted heap here),
// and a guard would silently produce a half-built tree instead.
ModPathNode* RtColl_SetCloneSubtree_1800104f0(void* set, const ModPathNode* src) {
    ModPathNode* dup = SetNodeNew(set, &src->element, nullptr);
    dup->height = src->height;       // copied in THIS order: +0x22 then +0x20
    dup->balance = src->balance;
    if (src->left != nullptr) {
        ModPathNode* c = RtColl_SetCloneSubtree_1800104f0(set, src->left);
        dup->left = c;
        c->parent = dup;
    }
    if (src->right != nullptr) {
        ModPathNode* c = RtColl_SetCloneSubtree_1800104f0(set, src->right);
        dup->right = c;
        c->parent = dup;
    }
    return dup;
}

// 0x180010470 — Set<Filename> destroy-subtree: post-order, then the key's
// release, then the node; the null test is at entry. It IS
// `Collection::Set<Filename>::DestroySubtree` (the same walk, the same
// `~Filename`, the same free), which Mod's operator= and the set's own ~Set
// (Clear: the root's two subtrees through it, then the root) call by name.

// ============================================================================
// The Mod-entry lifecycle (B.4 acceptance MOD: Mod's own members)
// ============================================================================

// RefreshList's stack record, 0x1800af790..0x1800af7d7: retail zeroes the
// members one by one in declaration order and leaves +0x24 / +0x28 / +0x2c (icon
// width, icon height, crc) and the pad at +0x51..+0x57 UNWRITTEN — every path
// that appends the record writes all three first, and the two early-out paths
// destroy it without reading them. So `iconWidth` / `iconHeight` / `crc` have no initialiser
// here. The set's `_pad0c` (+0x6c) is left unwritten too: `Collection::Set()`
// writes only the root qword and the count dword, as retail does (B6 row 640).
Mod::Mod()
    : name(), author(), description(),
      version(0), userVersion(0),                 // one qword store in retail
      order(0),
      ints(), bytes(),
      enabled(0),
      file(),
      set(),
      files() {
}

// 0x1800101e0 — Mod::Mod(const Mod&)
//
// Retail zeroes each member immediately before filling it, member by member, in
// declaration order. The member initialisers below are the zeroes and the
// copies that need no body: the three Strings are their copy ctor (the null
// store, then the adopt: 0x180010208..0x180010241, String's 0x18000c780 shape)
// and the six ints are copied, in order. The body is then retail's fill order
// from +0x30 on, statement for statement:
//   * ints / bytes: zeroed (Array()), then retail's `ADD RCX,0x30 ; CMP RCX,RDX`
//     self test on the sub-object and the CALL to its operator= (0x180011110 /
//     0x180011220), which begins by freeing dst->data — the zero is what makes
//     that free safe.
//   * enabled: copied.
//   * file: retail CALLs the Filename copy ctor 0x1800067d0 (null store, adopt,
//     AdjustDelimiters twice); here the member is default-constructed (the same
//     null store) and assigned with Filename::operator= 0x180006770 (adopt,
//     AdjustDelimiters once). On a null destination the two leave the same
//     buffer: AdjustDelimiters converges in one call (its last scan finds
//     nothing to splice, and a fresh scan with lastDelim = -1 splices a subset
//     of what that scan would), so retail's second call is a no-op on the first
//     call's result. One AdjustDelimiters call fewer than retail; no store,
//     refcount or free differs.
//   * set: zeroed (Set(): root and count, as retail; +0x6c unwritten), then
//     retail's self test, the
//     clone 0x1800104f0 when the source has a root, and the count.
//   * files: zeroed, then Array<Filename>::operator= (0x1800091f0).
Mod::Mod(const Mod& src)
    : name(src.name), author(src.author), description(src.description),
      version(src.version), userVersion(src.userVersion), order(src.order),
      iconWidth(src.iconWidth), iconHeight(src.iconHeight), crc(src.crc),
      ints(), bytes(),
      file(),
      set(),
      files() {
    if (&ints != &src.ints)
        RtColl_ArrayI32Assign_180011110(&ints, &src.ints);
    if (&bytes != &src.bytes)
        RtColl_ArrayU8Assign_180011220(&bytes, &src.bytes);

    enabled = src.enabled;

    file = src.file;

    if (&src.set != &set) {
        if (src.set._root != nullptr)
            set._root = RtColl_SetCloneSubtree_1800104f0(&set, src.set._root);
        set._count = src.set._count;
    }

    files = src.files;
}

// 0x180026ef0 — Mod::operator=(const Mod&). No self test on the record: retail
// tests each sub-object (the Strings and the Filename on their buffers, the
// Arrays and the Set on their addresses).
Mod& Mod::operator=(const Mod& src) {
    name = src.name;                  // 0x180026f0a..0x180026f46
    author = src.author;
    description = src.description;

    version = src.version;
    userVersion = src.userVersion;
    order = src.order;
    iconWidth = src.iconWidth;
    iconHeight = src.iconHeight;
    crc = src.crc;

    if (&ints != &src.ints)
        RtColl_ArrayI32Assign_180011110(&ints, &src.ints);
    if (&bytes != &src.bytes)
        RtColl_ArrayU8Assign_180011220(&bytes, &src.bytes);

    enabled = src.enabled;

    // The plain shared assign and THEN AdjustDelimiters (0x18002704a CALL
    // 0x1800c6970): `Filename::operator=(const Filename&)`, 0x180006770.
    file = src.file;

    if (&src.set != &set) {
        if (set._root != nullptr) {
            ModFilenameSet::DestroySubtree(set._root);    // CALL 0x180010470
            set._root = nullptr;
            set._count = 0;
        }
        if (src.set._root != nullptr)
            set._root = RtColl_SetCloneSubtree_1800104f0(&set, src.set._root);
        set._count = src.set._count;
    }

    files = src.files;                // CALL 0x1800091f0
    return *this;
}

// 0x180011460 — Mod::~Mod(). No body: retail's eight releases are the member
// destructors in reverse declaration order (Modification.h) — files'
// ~Array (0x1800098d0), the set's Clear (0x180010470 over the tree, then root
// and count zeroed), ~Filename on +0x58, bytes' and ints' ~Array (0x1800112a0 /
// 0x1800111a0), then the three Strings, description first.
Mod::~Mod() = default;
// 0x180010b80 / 0x180010110 / 0x180028610 — Array<Mod>'s ~Array, operator= and
// SetSize are `RtIO::ModList`'s members (DestroyAndFree / Assign / SetSize over
// the erased core); this row is what makes them Mod's.

}  // namespace RtIO

namespace rtcoll {

// stride 0x80, align 8. byteCopy=false: the assign refcounts and clones.
// assign = the copy ctor into the raw slot (0x1800101e0); no init arm (SetSize
// 0x180028610 has none: its fill loop copies from the old buffer or the fill,
// nothing else); destroy = ~Mod per element in address order (0x180010b80's
// `for (i = size; i > 0; --i, ++p)`). seedZero is the whole element: every
// member is a pointer the element ops dereference.
const ArraySpec kSpec_Mod = {                 // 0x180010b80 / 0x180010110 / 0x180028610
    "Array<IO::Modification::Mod:0x80>", 0x80, 8,
    &TAssign<RtIO::Mod>, nullptr, &TDestroy<RtIO::Mod>,
    /*byteCopy        */ false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0x00, 0x80 };

}  // namespace rtcoll

namespace RtIO {

// ============================================================================
// The five Modification entry points
// ============================================================================

// ?GetCount@Modification@IO@@QEBAHXZ   retail 0x18000da90
//     MOV EAX,[RCX+0x10] ; RET

// ?GetModification@Modification@IO@@QEBAAEBUMod@12@H@Z   retail 0x18000da80
//     MOVSXD RAX,EDX ; SHL RAX,7 ; ADD RAX,[RCX+8] ; RET
//
// MOVSXD, not a zero extension: a NEGATIVE index strides BACKWARDS off the
// array. Ghidra's C prints this as a zero extension
// (reference_ghidra_c_output_vs_disassembly.md), which would silently turn
// index -1 into an 8-GB forward offset. `static_cast<int64_t>` is the sign
// extension, and it is load-bearing.

// ?Enable@Modification@IO@@QEAAXH_N@Z   retail 0x1800aeab0
//
//     MOV RAX,[RCX+8] ; MOVSXD RDX,EDX ; MOV byte [RCX],1
//     SHL RDX,7 ; MOV byte [RDX+RAX+0x50],R8B ; RET
//
// Note that the dirty flag is set BEFORE the bounds-free indexed store, and
// unconditionally — Enable(i, x) marks the list dirty even when x is what the
// mod already held. Nothing in our tree depends on that, but a "no-op returns
// early" tidy-up would be a real behaviour change to IsDirty.

// ?Swap@Modification@IO@@QEAAXHH@Z   retail 0x1800aeb20
//
//     if (a == b) return;                      // CMP EDX,EBX ; JZ epilogue
//     this->dirty = 1;
//     Mod tmp;  Mod::Mod(&tmp, &mods[a]);      // 0x1800101e0
//     mods[a] = mods[b];                       // 0x180026ef0
//     mods[b] = tmp;                           // 0x180026ef0
//     tmp.~Mod();                              // 0x180011460
//
// The equality test is on the two INDICES and happens before the dirty flag is
// touched, so Swap(i, i) is a complete no-op. `b` is sign-extended (MOVSXD
// RBX,R8D) before the shift and `a` separately (MOVSXD RDI,EDX); both strides
// are `<< 7`.

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::Modification`'s member bodies
// ============================================================================
//
// A member of `IO::X` cannot be DEFINED inside `namespace RtIO` — `IO` and
// `RtIO` are siblings, so the definition has to appear in a namespace enclosing
// the class (B34_BRIEFS §A.2). This block therefore sits AFTER
// `}  // namespace RtIO`, and reaches that namespace's helpers as `RtIO::X` —
// which works for its anonymous-namespace and `static` members too, in this
// same translation unit.
//
// Inside this block a bare `Modification`, `Mod` or `Filename` would bind to
// whatever `namespace IO` sees first, so every POD is spelled `RtIO::` and every
// real class `System::` (B34_RULES §7.2, THE GREP HAZARD).
//
// NO QUALIFIED `IO::Modification::` CALL IS NEEDED IN THE DOORS ABOVE, and that
// is worth saying because a reader coming from blocks 1-2 will look for it:
// §A.1's recursion hazard is about a POLYMORPHIC receiver whose live instances
// wear a MINTED table whose slots resolve back to the doors. `IO::Modification`
// has no vtable at all (Modification.h's banner), so a member call on it is
// already a direct call.
// ============================================================================

namespace IO {

// ?GetCount@Modification@IO@@QEBAHXZ                        retail 0x18000da90
//     MOV EAX,[RCX+0x10] ; RET
// ?_singleton@Modification@IO@@2PEAV12@EA (0x1801fc6b0) -- the class static
// (ENDGAME B.3.9 IO, D7); RtIoPortInit redirects the minted row onto it.
Modification* Modification::_singleton = nullptr;

// ext.menu_entry (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): src/ext/menu_entry/ sets them.
int (*Modification::_extModCached)(const System::Filename& file, RtIO::Mod& out) = nullptr;
void (*Modification::_extModStored)(const System::Filename& file, const RtIO::Mod* record) = nullptr;
bool (*Modification::_extOverridesRestore)(Modification& mod) = nullptr;
void (*Modification::_extOverridesBuilt)(const Modification& mod) = nullptr;
bool (*Modification::_extRemountSkip)(Modification& mod) = nullptr;
void (*Modification::_extRemountDone)(Modification& mod, int32_t reloads, int32_t unloads) = nullptr;

int Modification::GetCount() const {
    return _mods._size;
}

// ?GetModification@Modification@IO@@QEBAAEBUMod@12@H@Z      retail 0x18000da80
//     MOVSXD RAX,EDX ; SHL RAX,7 ; ADD RAX,[RCX+8] ; RET
//
// MOVSXD, not a zero extension: a NEGATIVE index strides BACKWARDS off the
// array. Ghidra's C prints this as a zero extension
// (reference_ghidra_c_output_vs_disassembly.md), which would silently turn
// index -1 into an 8-GB forward offset. `static_cast<int64_t>` is the sign
// extension, and it is load-bearing. Unbounded, exactly as retail is.
const RtIO::Mod& Modification::GetModification(int index) const {
    return _mods._data[index];   // a signed int index: MOVSXD, then the 0x80 stride
}

// ?Enable@Modification@IO@@QEAAXH_N@Z                       retail 0x1800aeab0
//
//     MOV RAX,[RCX+8] ; MOVSXD RDX,EDX ; MOV byte [RCX],1
//     SHL RDX,7 ; MOV byte [RDX+RAX+0x50],R8B ; RET
//
// The dirty flag is set BEFORE the bounds-free indexed store, and
// unconditionally — Enable(i, x) marks the list dirty even when x is what the
// mod already held. Nothing in our tree depends on that, but a "no-op returns
// early" tidy-up would be a real behaviour change to IsDirty.
void Modification::Enable(int index, bool value) {
    _dirty = 1;
    _mods._data[index].enabled = static_cast<uint8_t>(value);
}

// ?Swap@Modification@IO@@QEAAXHH@Z                          retail 0x1800aeb20
//
//     if (a == b) return;                      // CMP EDX,EBX ; JZ epilogue
//     this->dirty = 1;
//     Mod tmp;  Mod::Mod(&tmp, &mods[a]);      // 0x1800101e0
//     mods[a] = mods[b];                       // 0x180026ef0
//     mods[b] = tmp;                           // 0x180026ef0
//     tmp.~Mod();                              // 0x180011460
//
// The equality test is on the two INDICES and happens before the dirty flag is
// touched, so Swap(i, i) is a complete no-op. `b` is sign-extended (MOVSXD
// RBX,R8D) before the shift and `a` separately (MOVSXD RDI,EDX); both strides
// are `<< 7`, which is what `RtIO::Mod*` arithmetic gives (sizeof is 0x80).
void Modification::Swap(int a, int b) {
    if (a == b) return;

    _dirty = 1;

    RtIO::Mod tmp(_mods._data[a]);       // 0x1800101e0
    _mods._data[a] = _mods._data[b];     // 0x180026ef0
    _mods._data[b] = tmp;                // 0x180026ef0
}   // ~tmp: 0x180011460

// ?SaveList@Modification@IO@@QEAAXXZ                        retail 0x1800b05f0
//
//     if (!this->savingSettings) { Array<Mod>::operator=(&savedMods, &mods);
//                                  return; }
//     Persistence* p = *(0x1801fc6b8);
//     for (int i = 0; i < this->count; ++i) {
//         mods[i].order = i;
//         Filename tmp = mods[i].file.GetPathFilename(true);   // 0x1800c6740
//         char* nameRef = tmp.data;  addref;                   // then release tmp
//         p->Set("ModEnabled_", nameRef, mods[i].enabled);     // 0x1800b8380
//         p->Set("ModIndex_",   nameRef, mods[i].order);       // 0x1800b80c0
//         release nameRef;
//     }
//
// The two literals are at 0x1801557f8 ("ModEnabled_") and 0x180155808
// ("ModIndex_"), read out of .rdata; they are the same prefixes RefreshList
// formats back with "%s%s" (0x1801551f8) when it reads the settings in.
//
// `mods[i].order` is re-read from the record for the second Set rather than
// reusing the loop counter — retail does `MOV R9D,[RBP+R14+0x20]`. Identical in
// value; transcribed as written so a future change to the first store cannot
// silently desynchronise the two.
void Modification::SaveList() {
    if (!_savingSettings) {
        // 0x1800b0619: LEA RDX,[RCX+8] ; ADD RCX,0x18 ; CALL 0x180010110
        _savedMods = _mods;
        return;
    }

    ::IO::Persistence* p = ::IO::Persistence::_singleton;   // ENDGAME B.3.9 IO (D7): the class static

    for (int i = 0; i < _mods._size; ++i) {
        RtIO::Mod* m = _mods._data + i;
        m->order = i;

        // GetPathFilename's by-value return constructs `tmp` in place (the
        // hidden return slot).
        char* ref;
        {
            System::Filename tmp(m->file.GetPathFilename(true));

            // 0x1800b0676..0x1800b06be: take a REFERENCE to the returned
            // buffer, then release the temporary. `ref` outlives the scope
            // deliberately; the inner block's close is retail's release.
            ref = tmp._data;
            if (ref != nullptr) ++RtIO::RefCount(ref);
        }  // ~tmp releases here, exactly where retail's release sat

        // The door decides the overload: ?Set@..@QEAAXPEBD0_N@Z (bool), then
        // ?Set@..@QEAAXPEBD0H@Z (int).
        p->Set("ModEnabled_", ref, static_cast<bool>(m->enabled != 0));
        p->Set("ModIndex_", ref, static_cast<int>(m->order));

        if (ref != nullptr) {
            if (--RtIO::RefCount(ref) == 0) ::System::MemoryAllocator::MemFree(ref - 8);
        }
    }

}


}  // namespace IO
