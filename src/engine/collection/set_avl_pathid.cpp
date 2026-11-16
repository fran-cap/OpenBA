// ============================================================================
// src/engine/collection/set_avl_pathid.cpp — BATCH 5 (2026-09-12)
//
//   `Collection::Set<Pair<System::Filename, uint32>, …>` — the compiled-path →
//   packfile-id map that `IO::Modification::SetupFileSystem` (0x1800aebb0)
//   builds on its own stack, and nothing else in the DLL uses.
//
//   0x180027380  Insert(set, out, &key /*Filename*/, &value /*uint32*/)  110 B
//   0x1800275e0  InsertCore(set, out, pair)                              351 B
//   0x1800278c0  AllocNode(ignored, pair, parent)  — alloc AND construct 169 B
//   0x180027970  SubtreeFree(ignored, node)        — recursive           120 B
//   0x1800279f0  Retrace(set, &slot)                                     237 B
//   0x180028120  RotateLeft(set, node)                                   299 B
//   0x180028250  RotateRight(set, node)                                  299 B
// ============================================================================
//
// WHY A SEVENTH COPY OF THIS ALGORITHM EXISTS IN THE TREE
// ---------------------------------------------------------------------------
// `Collection::Set` is a header-only template with ZERO exported symbols, so
// MSVC emitted one COMDAT per instantiation and the linker did not fold them.
// avl_trees.cpp already carries three keyed inserts (the type tree, the
// settings Map<String,String>, the packfile Set<Pair<Filename,uint64>>);
// external_set.cpp carries the 0x18-element resource tree; set_avl.cpp and
// set_avl_e24.cpp carry two more rebalance families. This is the seventh, and
// it is a DIFFERENT INSTANTIATION rather than a duplicate — measured, not
// assumed:
//
//   0x1800275e0 vs 0x180025c70 (the packfile insert core)   340 of 351 differ
//   0x180027380 vs 0x180025a80 (its public wrapper)          92 of 110 differ
//   0x180027970 vs 0x180008340 (the packfile subtree free)   57 of 120 differ
//
// THE TWO ROTATIONS ARE BYTE-IDENTICAL TO A BODY WE ALREADY HAVE, and that IS
// a duplicate, so the existing bodies serve them rather than a rewrite:
//
//   0x180028120 vs 0x18000b040 (rtsetavl::RotateLeft)    0 of 299 differ
//   0x180028250 vs 0x18000b170 (rtsetavl::RotateRight)   0 of 299 differ
//
// and the retrace is byte-identical to the packfile family's 0x180025e10
// EXCEPT for eight bytes, all of them inside the two rel32 CALL displacements
// that name the rotations above (offsets 142/143, 153/154, 194/195, 205/206).
// So it is `rtsetavl::Retrace` too, and that is the same reading avl_trees.cpp
// already makes when it binds 0x180025e10 / 0x180026080 / 0x1800261b0 to those
// three bodies. Six distinct addresses, three shared bodies.
//
// THE COMPARATOR IS CASE-SENSITIVE `strcmp`, NOT `_stricmp`
// ---------------------------------------------------------------------------
// *** This is the one field that separates this family from the packfile
// family it otherwise resembles, and getting it wrong silently changes which
// entries collide. *** 0x180025c70 CALLs 0x18007eefc (`__ascii_stricmp`);
// 0x1800275e0 has NO call in its descent at all — MSVC inlined a plain
// `strcmp`, and the final subtraction is MOVSX/MOVSX (0x180027661,
// 0x1800276c0 and the mirror pair at 0x1800276c0), i.e. SIGNED char
// arithmetic. Transcribed as `StrCmpSigned` below. The keys are
// `ExternalFactory::GetCompiledPath` results, which the caller has already put
// through `AdjustDelimiters`, so the case-sensitivity is not papered over by a
// normalisation somewhere else — a differently-cased path really is a
// different key here.
//
// THE TWO NULL ARMS ARE ASYMMETRIC, and that is retail's
// ---------------------------------------------------------------------------
//   node key null  -> skip the left test entirely and fall into the right test
//                     (0x18002762a `TEST R8,R8 / JZ 0x180027682`)
//   search key null-> take the LEFT branch (0x180027632 `TEST R9,R9 / JZ
//                     0x18002766f`) on the way down, and report EQUAL at the
//                     right test (0x180027688 `JZ 0x18002772b`)
// A "tidied" comparator that collapsed these into one sign test would build a
// differently-shaped tree on any input containing a null key.
//
// ALLOCATION AND THE STRING HEADER
// ---------------------------------------------------------------------------
// 0x1800278c0 inlines the allocator lock/Allocate/unlock triple; that is
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) with size 0x30
// and align 8, reached through the one door array.cpp and avl_trees.cpp
// already use — a heap cannot be half ours. `str-4` is `System::String`'s
// refcount and `str-8` its allocation base (that header belongs to String, not
// to the allocator).
//
// ORACLE
// ---------------------------------------------------------------------------
// NONE, and the refusal is structural, exactly as avl_trees.cpp records for
// its three inserts: every entry point here mutates one shared tree, so
// running ours and then theirs at a live call site inserts the same key twice
// and retail's second insert RETURNS EARLY — a comparison that is vacuously
// green. The tree this family builds is also a stack local of
// SetupFileSystem, destroyed before that function returns, so there is nothing
// left afterwards to compare either. What a boot DOES check is the end-to-end
// effect: the entries whose packfile id changed are the ones reloaded, and a
// wrong comparator or a wrong rotation shows up as the wrong resources being
// force-unloaded.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "system/String.h"   // B.4: RtSystem::StringRefCount
#include "collection/Collection.h"
#include "set_avl.h"
#include "set_avl_pathid.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name

namespace rtpathid {

using rtsetavl::Node;
using rtsetavl::Set;

// PathId, PathIdNode and PathIdSet: set_avl_pathid.h (B.4 wave X: the one
// declaration this file and io/modification_setupfilesystem.cpp share).

namespace {

// The two allocator calls, same shape as avl_trees.cpp's MemAlloc / MemFree:
// System::MemoryAllocator::MemAlloc / MemFree (retail 0x1800c7a50 / 0x1800c7ad0)
// by name.
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}
void MemFree(void* p) {                     // the null test is INSIDE ?MemFree@
    System::MemoryAllocator::MemFree(p);
}

// Retail's inlined String release. NOTE THE ASYMMETRY, which is reproduced:
// the FREEING branch leaves the slot holding `data - 8` and only the
// still-referenced branch nulls it (0x1800279c1 and 0x1800279e6).
void StringReleaseInline(char** slot) {
    char* p = *slot;
    if (!p) return;
    if (--RtSystem::StringRefCount(p) == 0) {
        *slot = p - 8;
        MemFree(p - 8);
    } else {
        *slot = nullptr;
    }
}

// MSVC's inlined `strcmp`: compare as unsigned bytes while scanning, then
// subtract as SIGNED chars (`MOVSX` at 0x180027661 / 0x180027666). The two
// halves disagree only on bytes >= 0x80, which is exactly why the scan and
// the subtraction are transcribed separately instead of collapsed.
int StrCmpSigned(const char* a, const char* b) {
    size_t i = 0;
    if (static_cast<unsigned char>(*a) == static_cast<unsigned char>(*b)) {
        while (a[i] != '\0' && b[i] != '\0') {
            ++i;
            if (static_cast<unsigned char>(a[i]) != static_cast<unsigned char>(b[i])) break;
        }
    }
    return static_cast<int>(static_cast<signed char>(a[i])) -
           static_cast<int>(static_cast<signed char>(b[i]));
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x1800278c0 — allocate a 0x30 node and construct the element in place.
//
// FIRST PARAMETER IGNORED: retail takes the Set in RCX and never reads it
// (0x1800278c0..0x1800278d0 overwrite RCX with the allocator singleton). The
// signature keeps it because the two call sites pass it.
//
// A NULL ALLOCATION RETURNS NULL AND CONSTRUCTS NOTHING (0x1800278e0
// `TEST RAX,RAX / JZ`), and the callers below store that null into the tree
// exactly as retail does. No guard is added: retail relies on the allocation
// succeeding and a guard here would hide the failure rather than handle it.
// ---------------------------------------------------------------------------
PathIdNode* AllocNode(void* /*ignoredSet*/, const PathId* pair, PathIdNode* parent) {
    PathIdNode* n = static_cast<PathIdNode*>(MemAlloc(0x30u, 8));
    if (n == nullptr) return nullptr;

    char** keySlot = &n->element.key;
    *keySlot = nullptr;                       // retail stores the zero FIRST
    if (pair->key) {
        *keySlot = pair->key;
        ++RtSystem::StringRefCount(pair->key);
    }
    n->element.value = pair->value;           // a DWORD store
    n->parent  = parent;
    n->left    = nullptr;
    n->right   = nullptr;
    n->balance = 0;   // retail: ONE dword store at +0x28 over balance and
    n->height  = 0;   //   height (H7: two named stores, address order)
    return n;
}

// ---------------------------------------------------------------------------
// 0x1800275e0 — the insert core.
//
// Returns `outSlot`; `*outSlot` is the FRESH node on an insert, the EXISTING
// node on an equal hit, and the new root on an empty tree.
// ---------------------------------------------------------------------------
namespace {
// The policy `Collection::detail::InsertCorePattern` runs on for this tree
// (ENDGAME B.3.2 step 1). The descent is the one shared pattern in
// src/engine/collection/Collection.h — the same body 0x180030380,
// 0x18002a170, 0x180025c70 and 0x1800275e0 are four copies of — and every arm
// of it is unchanged. What is here is what made THIS instantiation different:
// the case-SENSITIVE comparator, the null-arm asymmetry, its own node
// allocator, and the retrace reached at its own address.
struct PathIdPolicy : Collection::detail::NoCounters {
    using Node    = PathIdNode;
    using SetT    = PathIdSet;
    using Element = PathId;

    static Node*&   Root(SetT* s)  { return s->root; }
    static int32_t& Count(SetT* s) { return s->count; }
    static const char* NodeKey(const Node* n) { return n->element.key; }
    static const char* SearchKey(const Element* p) { return p->key; }  // retail re-reads
    static bool IsNull(const char* k) { return k == nullptr; }
    // *** CASE-SENSITIVE `strcmp`, NOT `_stricmp`. *** This is the one field
    // that separates this family from the packfile family it otherwise
    // resembles, and getting it wrong silently changes which entries collide
    // (see the file banner).
    static bool Less(const char* a, const char* b) { return StrCmpSigned(a, b) < 0; }

    static Node* Alloc(SetT* set, const Element* p, Node* parent) {
        return AllocNode(set, p, parent);
    }
    static Node* Alloc(decltype(nullptr), const Element* p, Node* parent) {
        return AllocNode(nullptr, p, parent);
    }
    static void Retrace(SetT* set, Node** slot) {   // 0x1800279f0: the erased core every 16-byte element shares
        rtsetavl::Retrace(reinterpret_cast<Set*>(set), reinterpret_cast<rtsetavl::Node**>(slot));  // CAST-OK: elem_stride
    }
};
}  // namespace

PathIdNode** InsertCore(PathIdSet* set, PathIdNode** outSlot, const PathId* pair) {
    return Collection::detail::InsertCorePattern<PathIdPolicy>(set, outSlot, pair);
}

// ---------------------------------------------------------------------------
// 0x180027380 — the public insert.
//
//   (set, outSlot, &key /*Filename*/, &value /*uint32*/)
//
// Builds a refcounted copy of the key plus the value on the stack, inserts,
// releases the copy. THE VALUE IS COPIED AS A DWORD (`MOV EAX,[R9] /
// MOV [RSP+0x38],EAX` at 0x1800273c1), so +0x0c of the temporary is never
// written and neither is the node's — the caller passes a POINTER to the
// uint32, not the uint32.
// ---------------------------------------------------------------------------
PathIdNode** Insert(PathIdSet* set, PathIdNode** outSlot, char* const* key, const uint32_t* value) {
    PathId temp;
    temp.key = nullptr;                       // retail stores the zero FIRST
    if (*key) {
        temp.key = *key;
        ++RtSystem::StringRefCount(*key);
    }
    temp.value = *value;
    // temp._pad0c deliberately left uninitialised: retail's store is a DWORD.

    InsertCore(set, outSlot, &temp);

    StringReleaseInline(&temp.key);
    return outSlot;
}

// ---------------------------------------------------------------------------
// 0x180027970 — the recursive subtree free.
//
// (ignoredThis, node). Frees left, then right, then releases the key, then
// hands the node to `SetNodeFree` (0x180009c30, the allocator-lock Free
// wrapper WHOSE FIRST PARAMETER IS IGNORED — a one-argument MemFree in its
// place would free the ignored `this` and corrupt the free list; its port is
// rtalloc::FreeLockedIgnoringSelf in src/engine/system/allocator.cpp, and this
// file calls `rtsetavl::SetNodeFree`, which keeps the same two-argument contract).
//
// THE SECOND ELEMENT SLOT (+0x08) IS NOT RELEASED, because it is a uint32 and
// there is nothing to release. Retail releases exactly one slot and so does
// this.
//
// RECURSION DEPTH is the tree's height. The tree holds one entry per COMPILED
// EXTERNAL — a few thousand on a stock install — so the depth is under 20.
// Retail recurses and so does this; an explicit stack would be a different
// program with a different overflow point.
// ---------------------------------------------------------------------------
void SubtreeFree(void* ignoredThis, PathIdNode* node) {
    if (node == nullptr) return;

    SubtreeFree(ignoredThis, node->left);
    SubtreeFree(ignoredThis, node->right);

    StringReleaseInline(&node->element.key);

    rtsetavl::SetNodeFree(ignoredThis, node);
}

}  // namespace rtpathid
