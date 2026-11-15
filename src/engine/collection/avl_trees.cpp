// ============================================================================
// src/engine/collection/avl_trees.cpp — Wave 9 lane COLL
//   The `Collection::Set` / `Collection::Map` INSERT side: the three keyed
//   trees, plus the recursive subtree free.
// ============================================================================
//
// WHAT THIS CLOSES
// ----------------
// Lane VID2 ported the four ELEMENT-TYPE-AGNOSTIC AVL primitives (retrace,
// both rotations, erase) for the node header whose element is 0x10 bytes
// (src/engine/collection/set_avl.cpp). This file is the other half: the
// KEYED entry points — the descent, the node allocation, the pair
// construction and the wrappers — which are per-instantiation because the key
// comparison and the element copy are baked into each one.
//
// Four unregistered by-address doors close here:
//
//   0x180030140  Set<Pair<u32,TypeRecord>>::Insert  — the row that
//                `Object::Factory::Startup` could not close. Object::Factory's
//                type tree is a `Collection::Set` whose insert reaches
//                Retrace50 (GROUP A below).
//   0x18002a090  Map<String,String>::Insert — reached from `ReadSettings` on
//                EVERY boot (persistence_settings.cpp), from
//                `Persistence` (persistence.cpp) and from
//                `Configuration::SetCommandLine` (configuration.cpp).
//   0x180025a80  Set<Pair<Filename,u64>>::Insert — LoadPackages' path index
//                (filesystem_packages.cpp:824), one entry per file per
//                packfile.
//   0x180008340  the recursive subtree free (filesystem_packages.cpp:646),
//                which ClosePackages runs on every mods remount.
//
// TRANSITIVE CLOSURE, COUNTED BEFORE ANY BODY WAS WRITTEN
// -------------------------------------------------------
// Lane PICK's rule: porting the visible rows of a census can trade N doors for
// more than N. Counted from the disassembly, `filter_mnemonics="CALL"` on every
// body, transitively, before a line was written:
//
//   0x180030140 -> 0x180009350 (Array<T*>::operator=), 0x180030380, 0x1800093e0
//   0x180030380 -> 0x180008440 (node alloc), 0x180030540 (retrace)
//   0x180008440 -> 0x1800c6e60 (Allocate, REGISTERED), 0x1800087c0
//   0x1800087c0 -> 0x180009350
//   0x180030540 -> 0x1800307b0, 0x1800308e0        (both LEAVES, no CALL)
//   0x180009350 -> 0x1800093e0, 0x1800c7a50 (MemAlloc, reached by address)
//   0x1800093e0 -> 0x1800c6c00 (Free, REGISTERED)
//
//   0x18002a090 -> 0x18002a170, 0x1800c7ad0 (MemFree, REGISTERED)
//   0x18002a170 -> 0x18000f970 (node alloc), 0x18002a310 (retrace)
//   0x18000f970 -> 0x1800c6e60, 0x18000fb00 (pair ctor)
//   0x18000fb00 -> LEAF
//   0x18002a310 -> 0x18002a590, 0x18002a6c0     (both LEAVES)
//
//   0x180025a80 -> 0x1800067d0 (Filename copy ctor, REGISTERED by
//                  src/engine/system/filename.cpp:470), 0x180025c70,
//                  0x1800c7ad0
//   0x180025c70 -> 0x180008520 (node alloc), 0x18007eefc (_stricmp),
//                  0x180025e10 (retrace)
//   0x180008520 -> 0x1800c6e60, 0x180008840 (pair ctor)
//   0x180008840 -> 0x1800067d0
//   0x180025e10 -> 0x180026080, 0x1800261b0     (both LEAVES)
//
//   0x180008340 -> itself, 0x1800c7ad0, 0x180009c30 (SetNodeFree, REGISTERED
//                  by src/engine/system/allocator.cpp:1454)
//
// **Closure size: 20 bodies. 15 written here, 3 already ported by lane VID2
// and reused (see below), 2 already registered by other lanes.**
//
// ONE dependency is the CRT's: `0x18007eefc` (the `_stricmp` the packfile
// descent needs) is `rtcrt::StrICmp` (rt_crt.h), called by name from
// PathStrICmp below.
//
// SIX ADDRESSES RUN LANE VID2's BODIES, AND IT IS MEASURED
// ----------------------------------------------------------------------
// The Map<String,String> tree and the packfile Filename tree have the SAME
// node header as the message-callback Set lane VID2 ported: element 0x10,
// parent +0x10, left +0x18, right +0x20, balance +0x28, height +0x2a. Their
// retraces and rotations are therefore separate compilations of the same
// template — but "the same template" is a claim, so it was CHECKED rather than
// assumed, by reading the shipped bytes out of Runtime-x64.dll and comparing
// them (this file's report, §"byte comparison"):
//
//   rotate-left   0x18000b040, 0x18002a590, 0x180026080  BYTE-IDENTICAL over
//   rotate-right  0x18000b170, 0x18002a6c0, 0x1800261b0  the whole 0x12b-byte
//                                                        extent of each body
//   retrace       0x18000adc0 vs 0x18002a310 / 0x180025e10
//                     -> NOT byte-identical. First difference at +0x23
//                        (`MOV R8,RAX` vs `MOV RDX,RAX`): the two later
//                        bodies carry the node in RDX where 0x18000adc0
//                        carries it in R8, and 0x18000adc0 RE-READS `*slot`
//                        before the `balance > 1` test where the other two use
//                        the register they already hold. Nothing writes `*slot`
//                        between those two points, so the re-read is a
//                        no-difference and the three bodies are semantically
//                        one. 0x18002a310 and 0x180025e10 ARE byte-identical
//                        to each other.
//
// That is exactly the check this wave's rules demand: the Array/Set family has
// produced four near-misses that all passed green runs and differed only in an
// alignment immediate, a shift, or a trailing call. Writing "they look the
// same" would have been the fifth.
//
// ORACLE
// ------
// A destructive mutator cannot be replayed at a live call site: running ours
// and then theirs starts the second run from the state the first left.
//
// AVL defects CANCEL, which is why a comparison has to be per-step and structural
// (shape, balance, height, parent linkage, key order) rather than a final
// key-set check.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "system/String.h"   // B.4: RtSystem::StringRefCount
#include "collection/Collection.h"
#include "object/TypeInfo.h"
#include "avl_trees.h"
#include "set_avl.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name
#include "rt_libm.h"   // B3R: rtcrt::StrICmp (0x18007eefc)
#include "system/Filename.h"   // the Filename copy ctor (0x1800067d0)

namespace rtavl {

using RtObject::TypeNode;
using RtObject::TypeRecord;

// The effectiveness counters declared in avl_trees.h. See that banner for why
// they live inside the bodies rather than around them.
ProbeCounters g_pc = {};

// ---------------------------------------------------------------------------
// The allocator boundary.
//
// Retail INLINES the lock/Allocate/unlock sequence into each node allocator
// (`MOV RBX,[0x1801fc718] / ADD RBX,0x640 / EnterCriticalSection /
//  Allocate(_singleton, n, 8) / LeaveCriticalSection`), which is exactly
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) with align 8.
// The two wrappers below call System::MemoryAllocator::MemAlloc / MemFree by
// name instead of pinning 0x1800c6e60 and the +0x640 lock offset here.
// ---------------------------------------------------------------------------
static void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

// `if (p) { Enter; Free(_singleton,p); Leave; }` — the null test is INSIDE
// 0x1800c7ad0, which is why the callers below hand it a possibly-null pointer
// exactly as retail does.
static void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ---------------------------------------------------------------------------
// The System::String refcount idioms, INLINED at every site in this file
// exactly as retail inlines them.
//
// `str-4` is the refcount and `str-8` is the allocation base; that header
// belongs to System::String, NOT to the allocator
// (reference_never_written_needs_by_whom.md / PLAN §6's owner rule).
//
// NOTE THE ASYMMETRY IN Release, which is retail's and is reproduced: the
// FREEING branch leaves the slot holding `data - 8`, and only the
// still-referenced branch nulls it. Every one of the three sites below
// (0x18002a0e8, 0x18002a10c, 0x180025add, 0x18000838b) has the same shape.
// ---------------------------------------------------------------------------
static void StringAddRefInto(char** slot, char* src) {
    *slot = nullptr;                       // retail stores the zero FIRST
    if (src) {
        *slot = src;
        ++RtSystem::StringRefCount(src);
    }
}

static void StringReleaseInline(char** slot) {
    char* p = *slot;
    if (!p) return;
    if (--RtSystem::StringRefCount(p) == 0) {
        *slot = p - 8;
        MemFree(p - 8);
    } else {
        *slot = nullptr;
    }
}

// ============================================================================
// GROUP A — Set<Pair<uint32_t, TypeRecord>> : Object::Factory's type tree
// ============================================================================
//
// Node header, MEASURED (src/engine/object/TypeInfo.h carried +0x48 as a
// "GUESS, NOT MEASURED" int32 and a node of unknown size until this lane; both
// are corrected there in this commit):
//
//   +0x00 .. +0x2f  the element: key u32 at +0x00, TypeRecord value at +0x08
//   +0x30 parent    0x1800087c0 `MOV [RDI+0x30],RBX`
//   +0x38 left      0x180030380 `MOV [RDI+0x38],RAX` on the JC (less) arm
//   +0x40 right     0x180030380 `MOV [RDI+0x40],RAX` on the greater arm
//   +0x48 balance   int16 — 0x180030540 `CMP word [RDX+0x48],-1 / JGE`
//   +0x4a height    int16 — 0x180030540 `MOVZX ...,word [RAX+0x4a] / INC`
//   sizeof 0x50     0x180008440 `MOV EDX,0x50 / LEA R8D,[RDX-0x48]`
//                   -> Allocate(0x50, align 8)
//
// The +0x48 DWORD store in the pair constructor (0x180008825) zeroes BOTH
// 16-bit fields at once. It is written as one dword store here for the same
// reason ~Array's `[RDI+8]` is: it is one instruction covering two fields, and
// splitting it is a difference for free.
// ============================================================================

static_assert(sizeof(TypeNode) == 0x50, "0x180008440 allocates 0x50 bytes");
static_assert(offsetof(TypeNode, parent) == 0x30, "0x1800087c0 MOV [RDI+0x30],RBX");
static_assert(offsetof(TypeNode, left)   == 0x38, "0x180030380 less arm");
static_assert(offsetof(TypeNode, right)  == 0x40, "0x180030380 greater arm");

// 0x1800093e0 -- `Collection::Array<T*>::~Array` (the J1 body). B7: its one
// owner, `TypeRecord::children`, is the real `Collection::Array<TypeRecord*>`
// now, so ~Factory's walk (factory_dtors.cpp) calls the member's ~Array and the
// free body that stood here over a raw {_data, _size, _capacity} view is gone.

// ---------------------------------------------------------------------------
// The height/balance recomputation of the 0x50-node family. Appears seven
// times across 0x180030540 / 0x1800307b0 / 0x1800308e0 and is identical in all
// seven; the 0x30-node family's copy is in set_avl.cpp and differs only in the
// four offsets.
//
//   MOVZX ..,word [child+0x4a] / INC   ; child ? child->height + 1 : 0
//   CMP / MOVZX EAX,CX / CMOVG AX,DX   ; height = max(l, r), DEFAULTING TO r
//   SUB CX,DX                          ; balance = r - l
//
// 16-BIT THROUGHOUT — `INC DX` on a word, `SUB CX,DX` on words, `CMOVG` on
// words. A tree deep enough to overflow a 16-bit height is not reachable, but
// the truncation is written as retail wrote it rather than widened, because
// "it cannot overflow" is a claim about call sites this lane does not own.
// ---------------------------------------------------------------------------
// The two 16-bit words are TypeInfo.h's `int16_t balance` (+0x48) and
// `int16_t height` (+0x4a), both asserted there.
static inline int16_t& Balance50(TypeNode* n) {
    return n->balance;
}
static inline int16_t& Height50(TypeNode* n) {
    return n->height;
}

static inline void Recompute(TypeNode* n) {
    const int16_t l = n->left  ? static_cast<int16_t>(Height50(n->left)  + 1) : 0;
    const int16_t r = n->right ? static_cast<int16_t>(Height50(n->right) + 1) : 0;
    Balance50(n) = static_cast<int16_t>(r - l);
    Height50(n)  = (l > r) ? l : r;
}

// ---------------------------------------------------------------------------
// 0x1800307b0 — rotate LEFT about `n`. Returns the new subtree root (RAX = R8).
//
// Retail's store order, preserved:
//
//   if (n == set->root) set->root = n->right;         0x1800307b0
//   p = n->parent; if (p) (p->left == n ? p->left : p->right) = n->right;
//   r = n->right;
//   r->parent = n->parent;                            0x1800307e1
//   n->right  = r->left;                              0x1800307e9
//   if (n->right) n->right->parent = n;
//   r->left   = n;                                    0x1800307f6
//   n->parent = r;
//   if (r->left)  Recompute(r->left);
//   if (r->right) Recompute(r->right);
//   Recompute(r);                                     unguarded — r cannot be
//                                                     null on this path
//
// The root test reads `[RCX]` — RCX is the SET, not the node — and RCX is then
// REUSED for the parent at 0x1800307bc, destroying it. Faithful; the set
// pointer is not needed again.
// ---------------------------------------------------------------------------
TypeNode* RotateLeft50(TypeNode** root, TypeNode* n) {
    ++g_pc.rot50L;
    if (n == *root) *root = n->right;

    TypeNode* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->right;
        else              p->right = n->right;
    }

    TypeNode* r = n->right;
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

// ---------------------------------------------------------------------------
// 0x1800308e0 — rotate RIGHT about `n`. Transcribed independently from
// 0x1800308e0 and then diffed against RotateLeft50: the two disassemblies
// differ in exactly the six link offsets (+0x38 <-> +0x40) and nowhere else.
// ---------------------------------------------------------------------------
TypeNode* RotateRight50(TypeNode** root, TypeNode* n) {
    ++g_pc.rot50R;
    if (n == *root) *root = n->left;

    TypeNode* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->left;
        else              p->right = n->left;
    }

    TypeNode* l = n->left;
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

// ---------------------------------------------------------------------------
// 0x180030540 — the retrace. Walks from `*slot` to the root, recomputing
// balances and rotating where the invariant broke.
//
// *** THE SECOND PARAMETER IS A SLOT, NOT A NODE, AND THAT IS LOAD-BEARING. ***
// Retail re-reads `*RDI` at the top of every iteration (0x180030560) and again
// after each rotation (0x1800305d2, 0x1800305dd, 0x180030606), because a
// rotation can move a different node into the position being examined. Passing
// the node by value and caching it would be a different algorithm that happens
// to agree on balanced inputs. The one call site hands it the address of a real
// stack local for exactly that reason (`LEA RDX,[RSP+0x30]` at 0x18003042c).
//
// The rebalance predicates are transcribed from the FLAGS, not from the obvious
// form:
//
//   0x1800305ad  CMP CX,1 / JLE skip           -> enter when balance > 1
//   0x1800305c0  TEST CX,CX / JG take / JNS take
//                                              -> pre-rotate ONLY when the
//                                                 right child's balance < 0
//   0x1800305e0  CMP word [RDX+0x48],-1 / JGE  -> enter when balance < -1
//   0x1800305f4  TEST CX,CX / JS take / JLE take
//                                              -> pre-rotate ONLY when the
//                                                 left child's balance > 0
//
// A NULL CHILD SKIPS THE ROTATION ENTIRELY, not just the pre-rotation
// (`TEST RAX,RAX / JZ` at 0x1800305ba and 0x1800305ee jumps past BOTH calls).
// That is the difference between "unbalanced with a null child" being a no-op
// and being a crash, and it is retail's.
//
// `parent` is captured from the node BEFORE any rotation moves it
// (`MOV RSI,[RAX+0x30]` at 0x180030566, before the first Recompute), and the
// loop advances by storing it back into the slot. That ordering is why a
// rotation at the current level cannot redirect the walk.
// ---------------------------------------------------------------------------
void Retrace50(TypeNode** root, TypeNode** slot) {
    if (*slot == nullptr) return;

    for (;;) {
        ++g_pc.retrace50Iters;
        TypeNode* n      = *slot;
        TypeNode* parent = n->parent;   // captured BEFORE any rotation moves n

        Recompute(n);

        // Retail does NOT re-read here (it still holds the node in RDX from
        // 0x180030563); the 0x30-node sibling 0x18000adc0 DOES re-read at the
        // same point. Nothing writes *slot between, so the two agree. Written
        // as the re-read, matching set_avl.cpp, so the three bodies read the
        // same in source as they behave in the binary.
        TypeNode* cur = *slot;
        if (Balance50(cur) > 1) {
            TypeNode* r = cur->right;
            if (r) {
                if (Balance50(r) < 0) {
                    ++g_pc.preRot50R;
                    RotateRight50(root, r);
                    cur = *slot;        // 0x1800305d2
                }
                RotateLeft50(root, cur);
            }
        }

        cur = *slot;                    // 0x1800305dd
        if (Balance50(cur) < -1) {
            TypeNode* l = cur->left;
            if (l) {
                if (Balance50(l) > 0) {
                    ++g_pc.preRot50L;
                    RotateLeft50(root, l);
                    cur = *slot;        // 0x180030606
                }
                RotateRight50(root, cur);
            }
        }

        *slot = parent;
        if (!parent) return;
    }
}

// ---------------------------------------------------------------------------
// 0x1800087c0 (pair ctor), 0x180008440 (node alloc), 0x180030380 (insert core)
// and 0x180009350 (`Array<T*>::operator=`, the pair ctor's only caller) —
// DELETED in B.4 (R-COLL, rule B8). `Object::Factory` is a `Collection::Set`
// (TypeInfo.h) and inserts through the template's `InsertCorePattern`, which
// reaches this group only through `Retrace50`; the four bodies had no caller
// by name, by pointer or by table in `code runtime standalone dx11 engine
// tests` (swept 2026-10-03).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 0x180030140 — the public insert (builds the 0x30-byte Pair on the stack,
// copy-assigns its Array through 0x180009350, calls the core, destroys the
// temporary through 0x1800093e0, returns the out-slot).
//
// DELETED in ENDGAME B.3.3 step 7, with its declaration in avl_trees.h and its
// `RegisterRuntimeAddr(0x180030140, …)` row. Step 3 moved `Object::Factory`
// onto `Collection::Set<TypeEntry,…>`, whose `Insert` reaches the SAME ported
// core (`AvlOps<Node,0x30>::Retrace` = `PortedRetrace48`, set_core.cpp, which
// calls Retrace50 here). That left `TypeSetInsert` with zero
// callers by NAME and zero readers of 0x180030140 by ADDRESS — no `ResolveAddr`,
// no per-file wrapper, no `rt_data_mint_tables.inc` `RT_MINT_UNNAMED` slot and
// no `rt_export_addrs.inc` row cite it (swept 2026-09-18).
// ---------------------------------------------------------------------------

// ============================================================================
// GROUP B — Map<String,String> : Persistence / Configuration
// ============================================================================
//
// Node header IDENTICAL to lane VID2's (element 0x10, parent +0x10, left
// +0x18, right +0x20, balance +0x28, height +0x2a, sizeof 0x30), which is why
// its retrace and rotations are the shared rtsetavl bodies (set_avl.cpp) rather than
// re-transcribed. The element is two `char*` System::String slots.
//
// THE COMPARATOR IS AN INLINE strcmp AND IT IS THE HAZARD IN THIS SECTION.
// This project has already paid for one: a ported inline strcmp returned 0 for
// every distinct pair sharing a first byte, and four waves of oracles passed
// because 13 of the 14 values in the population were identical
// (reference_uniform_population_is_not_a_verification.md). Retail's, at
// 0x18002a1c4, transcribed from the flags:
//
//   c  = (unsigned char)a[0]
//   i  = 0
//   if (c != b[0]) goto out;
//   while (c != 0 && b[i] != 0) {         // TEST CL,CL / CMP byte [RAX],0
//       c = a[i+1]; ++i;                  // MOVZX ECX,[R10+RAX+1] / INC RAX
//       if (c != b[i]) break;             // CMP CL,[RAX] / JZ loop
//   }
// out:
//   return (signed char)a[i] - (signed char)b[i];
//
// SIGNED CHAR SUBTRACTION at the mismatch index — MOVSX, not MOVZX. It
// disagrees with `_stricmp`/`__ascii_stricmp` (which compares UNSIGNED) on any
// byte >= 0x80, which is why this comparator must NOT be shared with the
// packfile tree's below even though the two trees have the same node shape.
// filesystem_openfile.cpp:185 already recorded that distinction.
//
// The descent applies it TWICE with the operands swapped, which is the
// canonical `less`-twice idiom for {less, greater, equal}:
//
//   if (nodeKey  == null)                    goto cmp2;
//   else if (searchKey == null)              goto LEFT;
//   else if (Cmp(searchKey, nodeKey) < 0)    goto LEFT;
//  cmp2:
//   if (searchKey == null)                   return EQUAL(node);
//   else if (nodeKey == null)                goto RIGHT;
//   else if (Cmp(nodeKey, searchKey) < 0)    goto RIGHT;
//   return EQUAL(node);
//
// The null arms are asymmetric and they are retail's: a null SEARCH key with a
// non-null node key goes LEFT, while a null NODE key with a non-null search key
// goes RIGHT. Written as retail wrote them.
// ============================================================================

using rtsetavl::Node;
using rtsetavl::Set;

// The typed nodes (avl_trees.h) are rtsetavl::Node's layout, field for field:
// the one retrace / erase core serves them both.
static_assert(sizeof(StringPair) == 0x10, "the element of a 0x30-byte node");
static_assert(sizeof(StringPairNode) == 0x30, "0x18000f970 allocates 0x30");
static_assert(offsetof(StringPairNode, element) + offsetof(RtIo::SettingPair, key)   == 0x00, "StringPairNode::key @ +0x00");
static_assert(offsetof(StringPairNode, element) + offsetof(RtIo::SettingPair, value) == 0x08, "StringPairNode::value @ +0x08");
static_assert(offsetof(StringPairNode, parent)  == offsetof(Node, parent),  "StringPairNode::parent @ +0x10");
static_assert(offsetof(StringPairNode, left)    == offsetof(Node, left),    "StringPairNode::left @ +0x18");
static_assert(offsetof(StringPairNode, right)   == offsetof(Node, right),   "StringPairNode::right @ +0x20");
static_assert(offsetof(StringPairNode, balance) == offsetof(Node, balance), "StringPairNode::balance @ +0x28");
static_assert(offsetof(StringPairNode, height)  == offsetof(Node, height),  "StringPairNode::height @ +0x2a");
static_assert(offsetof(StringPairNode, _padTail) == 0x2c, "StringPairNode::pad2c @ +0x2c");
static_assert(sizeof(RtIo::SettingsSet) == sizeof(Set), "the {root, count} header");
static_assert(offsetof(RtIo::SettingsSet, _count) == offsetof(Set, count), "SettingsSet::_count @ +0x08");
static_assert(offsetof(FilenamePairNode, parent)  == offsetof(Node, parent),  "FilenamePairNode::parent @ +0x10");
static_assert(offsetof(FilenamePairNode, balance) == offsetof(Node, balance), "FilenamePairNode::balance @ +0x28");
static_assert(sizeof(RtIO::PackfileIndexTree) == sizeof(Set), "the {root, count} header");
static_assert(offsetof(RtIO::PackfileIndexTree, count) == offsetof(Set, count), "PackfileIndexTree::count @ +0x08");

// ---------------------------------------------------------------------------
// The inline strcmp of 0x18002a1c4 / 0x18002a223. See the banner.
//
// NOT `std::strcmp`: the standard says the comparison is on `unsigned char`,
// and this one is `MOVSX`. On a key containing a byte >= 0x80 the two disagree
// in SIGN, which is the difference between a subtree and its sibling.
// ---------------------------------------------------------------------------
static int StrCmpSigned(const char* a, const char* b) {
    unsigned char c = static_cast<unsigned char>(a[0]);
    int64_t i = 0;
    if (c == static_cast<unsigned char>(b[0])) {
        for (;;) {
            if (c == 0) break;              // TEST CL,CL / JZ out
            if (b[i] == 0) break;           // CMP byte ptr [RAX],0 / JZ out
            c = static_cast<unsigned char>(a[i + 1]);
            ++i;
            if (c != static_cast<unsigned char>(b[i])) break;   // CMP CL,[RAX]
        }
    }
    const int r = static_cast<int>(static_cast<signed char>(a[i])) -
                  static_cast<int>(static_cast<signed char>(b[i]));
    if (r < 0)      ++g_pc.strcmpLt;
    else if (r > 0) ++g_pc.strcmpGt;
    else            ++g_pc.strcmpEq;
    return r;
}

// ---------------------------------------------------------------------------
// 0x18000fb00 — the string-pair placement constructor.
//
//   node[+0x00] = 0; if (src->key)   { node[+0x00] = src->key;   ++key[-4]; }
//   node[+0x08] = 0; if (src->value) { node[+0x08] = src->value; ++val[-4]; }
//   node[+0x10] = parent; node[+0x18] = 0; node[+0x20] = 0;
//   node[+0x28] = 0 (DWORD, balance AND height)
//
// Both slots are zeroed BEFORE the conditional store — retail's `MOV [RCX],R9`
// with R9 == 0 at 0x18000fb19 — so a null source leaves a null slot rather
// than raw allocator bytes.
// ---------------------------------------------------------------------------
StringPairNode* StringPairCtor(StringPairNode* node, const StringPair* src,
                               StringPairNode* parent) {
    StringAddRefInto(&node->element.key._data,   src->key);
    StringAddRefInto(&node->element.value._data, src->value);

    node->parent  = parent;
    node->left    = nullptr;
    node->right   = nullptr;
    node->balance = 0;   // retail: ONE dword store at +0x28 over balance and
    node->height  = 0;   //   height (H7: two named stores, address order)
    return node;
}

// ---------------------------------------------------------------------------
// 0x18000f970 — allocate a 0x30-byte node (align 8) and construct its pair.
// FIRST PARAMETER IGNORED, exactly as 0x180008440's is; see that note.
// `MOV EDX,0x30 / LEA R8D,[RDX-0x28]` is Allocate(0x30, align 8).
// ---------------------------------------------------------------------------
StringPairNode* StringNodeAlloc(void* /*ignored*/, const StringPair* pair,
                                StringPairNode* parent) {
    StringPairNode* mem = static_cast<StringPairNode*>(MemAlloc(0x30u, 8));
    if (!mem) return nullptr;
    return StringPairCtor(mem, pair, parent);
}

// ---------------------------------------------------------------------------
// 0x18002a170 — the string-map insert core.
// ---------------------------------------------------------------------------
namespace {
// The policy for the string map. Same shared descent as group A's; what
// differs — and what is therefore here — is the comparator (retail's inlined
// SIGNED strcmp, NOT `_stricmp`), the null-key asymmetry a pointer key has,
// and the node allocator.
struct StringPolicy : Collection::detail::NoCounters {
    using Node    = StringPairNode;
    using SetT    = RtIo::SettingsSet;
    using Element = StringPair;

    static Node*&   Root(SetT* s)  { return s->_root; }
    static int32_t& Count(SetT* s) { return s->_count; }
    // Retail re-reads the node's key (`MOV R8,[RDI]`, 0x18002a1b4) and the
    // search key (0x18002a1bc, and AGAIN at 0x18002a212) every iteration; the
    // shared pattern calls these at exactly those points.
    static char* NodeKey(const Node* n) { return n->element.key._data; }
    static char* SearchKey(const Element* p) { return p->key; }
    static bool  IsNull(char* k) { return k == nullptr; }
    static bool  Less(char* a, char* b) { return StrCmpSigned(a, b) < 0; }

    static Node* Alloc(SetT* set, const Element* p, Node* parent) {
        return StringNodeAlloc(set, p, parent);
    }
    static Node* Alloc(decltype(nullptr), const Element* p, Node* parent) {
        return StringNodeAlloc(nullptr, p, parent);
    }
    // 0x18002a310 -- the shared rtsetavl body, by name (B3R RT-Core): the
    // one core every 16-byte element shares takes the erased node.
    static void Retrace(SetT* set, Node** slot) {
        rtsetavl::Retrace(reinterpret_cast<Set*>(set), reinterpret_cast<rtsetavl::Node**>(slot));  // CAST-OK: elem_stride
    }

    static void OnRootInsert()    { ++g_pc.rootInsert; }
    static void OnDescLeft()      { ++g_pc.descLeft; }
    static void OnDescRight()     { ++g_pc.descRight; }
    static void OnEqual()         { ++g_pc.equalHit; }
    static void OnNullSearchKey() { ++g_pc.nullSearchKey; }
    static void OnNullNodeKey()   { ++g_pc.nullNodeKey; }
};
}  // namespace

StringPairNode** StringInsertCore(RtIo::SettingsSet* set, StringPairNode** outSlot,
                                  const StringPair* pair) {
    return Collection::detail::InsertCorePattern<StringPolicy>(set, outSlot, pair);
}

// ---------------------------------------------------------------------------
// 0x18002a090 — the public Map insert.
//
// (map, outSlot, &key, &value). Builds a REFCOUNTED copy of both strings on the
// stack, inserts it, and releases both. The AddRef/Release pair is why the
// wrapper exists at all: the node's ctor AddRefs again, so the temp's two refs
// are the "+1 for the argument" the caller's strings need while the insert can
// still fail.
//
// Retail releases the VALUE first (0x18002a0de) and the KEY second
// (0x18002a102) — reverse construction order, as a C++ destructor sequence
// must be. Kept.
//
// RAX at the RET is RBX == the SECOND parameter.
// ---------------------------------------------------------------------------
StringPairNode** StringMapInsert(RtIo::SettingsSet* map, StringPairNode** outSlot,
                                 char* const* key, char* const* value) {
    StringPair temp;
    StringAddRefInto(&temp.key,   *key);
    StringAddRefInto(&temp.value, *value);

    StringInsertCore(map, outSlot, &temp);

    StringReleaseInline(&temp.value);
    StringReleaseInline(&temp.key);
    return outSlot;
}

// ============================================================================
// GROUP D — Set<Pair<Filename,uint64>> : the packfile path index
// ============================================================================
//
// Same 0x30 node as group B; the element is a `System::Filename` (one char*)
// and a packed {packIndex, fileIndex} qword. The comparator is the CRT
// `_stricmp` at 0x18007eefc, NOT group B's signed strcmp — the two disagree on
// any byte >= 0x80 and a package path can contain one
// (filesystem_openfile.cpp:185).
//
// The descent is the same less-twice idiom, and `IndexFind`
// (filesystem_packages.cpp:442) is its READ-side twin: an independent
// transcription by lane PKG of the same walk, which agrees with this one arm
// for arm. That is a free second reading of the comparator's null handling.
// ============================================================================

// 0x18007eefc — the CRT `_stricmp`, called by name as rtcrt::StrICmp (rt_crt.h).
static int PathStrICmp(const char* a, const char* b) {
    const int r = rtcrt::StrICmp(a, b);
    if (r < 0)      ++g_pc.icmpLt;
    else if (r > 0) ++g_pc.icmpGt;
    else            ++g_pc.icmpEq;
    return r;
}

// ---------------------------------------------------------------------------
// 0x180008840 — the {Filename, uint64} placement constructor.
//
//   Filename::Filename(node+0x00, src+0x00)     ; 0x1800067d0, PORTED
//   node[+0x08] = src[+0x08]                    ; the packed value qword
//   node[+0x10] = parent ; +0x18 = 0 ; +0x20 = 0 ; +0x28 = 0 (DWORD)
//
// The Filename copy ctor is called directly, as a placement new on the raw
// MemAlloc storage.
// ---------------------------------------------------------------------------
FilenamePairNode* FilenamePairCtor(FilenamePairNode* node, const FilenamePair* src,
                                   FilenamePairNode* parent) {
    new (&node->element.key) System::Filename(src->key);   // 0x1800067d0 (the node is raw MemAlloc storage)

    node->element.value = src->value;   // the {packIndex, fileIndex} pair: one qword move
    node->parent  = parent;
    node->left    = nullptr;
    node->right   = nullptr;
    node->balance = 0;   // retail: ONE dword store at +0x28 over balance and
    node->height  = 0;   //   height (H7: two named stores, address order)
    return node;
}

// 0x180008520 — allocate 0x30 (align 8) and construct. FIRST PARAM IGNORED.
FilenamePairNode* FilenameNodeAlloc(void* /*ignored*/, const FilenamePair* pair,
                                    FilenamePairNode* parent) {
    FilenamePairNode* mem = static_cast<FilenamePairNode*>(MemAlloc(0x30u, 8));
    if (!mem) return nullptr;
    return FilenamePairCtor(mem, pair, parent);
}

// ---------------------------------------------------------------------------
// 0x180025c70 — the packfile-index insert core.
// ---------------------------------------------------------------------------
namespace {
// The policy for the packfile path index. IDENTICAL to the string map's except
// for the comparator — `_stricmp` (0x18007eefc), not group B's signed strcmp;
// the two disagree on any byte >= 0x80 and a package path can contain one
// (filesystem_openfile.cpp:185) — and the node allocator.
struct FilenamePolicy : Collection::detail::NoCounters {
    using Node    = FilenamePairNode;
    using SetT    = RtIO::PackfileIndexTree;
    using Element = FilenamePair;         // {Filename key @+0x00, {packIndex, fileIndex} @+0x08}

    static Node*&   Root(SetT* s)  { return s->root; }
    static int32_t& Count(SetT* s) { return s->count; }
    static const char* NodeKey(const Node* n) { return n->element.key._data; }
    static const char* SearchKey(const Element* p) {
        return p->key._data;                               // 0x180025ce1 re-read
    }
    static bool IsNull(const char* k) { return k == nullptr; }
    static bool Less(const char* a, const char* b) { return PathStrICmp(a, b) < 0; }

    static Node* Alloc(SetT* set, const Element* p, Node* parent) {
        return FilenameNodeAlloc(set, p, parent);
    }
    static Node* Alloc(decltype(nullptr), const Element* p, Node* parent) {
        return FilenameNodeAlloc(nullptr, p, parent);
    }
    static void Retrace(SetT* set, Node** slot) {
        rtsetavl::Retrace(reinterpret_cast<Set*>(set), reinterpret_cast<rtsetavl::Node**>(slot));  // CAST-OK: elem_stride
    }

    static void OnRootInsert()    { ++g_pc.rootInsert; }
    static void OnDescLeft()      { ++g_pc.descLeft; }
    static void OnDescRight()     { ++g_pc.descRight; }
    static void OnEqual()         { ++g_pc.equalHit; }
    static void OnNullSearchKey() { ++g_pc.nullSearchKey; }
    static void OnNullNodeKey()   { ++g_pc.nullNodeKey; }
};
}  // namespace

FilenamePairNode** FilenameInsertCore(RtIO::PackfileIndexTree* set, FilenamePairNode** outSlot,
                                      const FilenamePair* pair) {
    return Collection::detail::InsertCorePattern<FilenamePolicy>(set, outSlot, pair);
}

// ---------------------------------------------------------------------------
// 0x180025a80 — the public packfile-index insert.
//
// (tree, outSlot, &key /*Filename*/, &value /*uint64*/). Builds a refcounted
// Filename copy plus the value qword on the stack, inserts, releases the copy.
// The value is copied RAW (`MOV RAX,[RBX] / MOV [RSP+0x30],RAX`) — the caller
// passes a POINTER to the qword, not the qword.
// ---------------------------------------------------------------------------
FilenamePairNode** FilenameSetInsert(RtIO::PackfileIndexTree* set, FilenamePairNode** outSlot,
                                     const System::Filename* key,
                                     const RtIO::PackfileIndexValue* value) {
    // B.4 Z MIRROR2: the stack pair is a real FilenamePair: the Filename copy
    // ctor 0x1800067d0 on +0x00, the raw value qword on +0x08, the insert,
    // then ~FilenamePair (the key's release) at scope exit, retail's order.
    FilenamePair temp{ *key, *value };

    FilenameInsertCore(set, outSlot, &temp);

    return outSlot;   // ~temp: the key's release
}

// ============================================================================
// 0x180008340 — the recursive subtree free
// ============================================================================
//
// (ignoredThis, node). Frees a whole subtree of the 0x30-node family:
//
//   if (!node) return;                       ; 0x180008340 TEST RDX,RDX / JZ
//   Free(this, node->left);                  ; +0x18
//   Free(this, node->right);                 ; +0x20
//   Release(node->key);                      ; the inlined String idiom
//   SetNodeFree(this, node);                 ; 0x180009c30
//
// THE SECOND ELEMENT SLOT (+0x08) IS NOT RELEASED. In the packfile index that
// slot is the packed {packIndex, fileIndex} qword, not a String, so there is
// nothing to release — but this body serves EVERY instantiation of the 0x30
// node whose second slot is trivially destructible, and a "tidy" symmetric
// release of +0x08 would corrupt the heap on the first call. Retail releases
// exactly one slot and so does this.
//
// `SetNodeFree` is 0x180009c30, the allocator-lock Free wrapper whose FIRST
// PARAMETER IS IGNORED — a one-argument MemFree there would free the ignored
// `this` and corrupt the free list. It is rtsetavl::SetNodeFree (set_avl.cpp),
// called here by name with the same contract (ignore arg 1, free arg 2).
//
// RECURSION DEPTH is the tree's height, which for an AVL tree of the ~9,000
// entries a stock install's four packfiles produce is under 20. Retail
// recurses and so does this; a hand-rolled explicit stack would be a different
// program with a different stack-overflow point.
//
// ORACLE: NONE, and it is not close. It frees everything it touches; running it
// twice is a double free.
// ============================================================================
void SubtreeFree(void* ignoredThis, FilenamePairNode* node) {
    if (node == nullptr) return;

    ++g_pc.freeNodes;
    SubtreeFree(ignoredThis, node->left);
    SubtreeFree(ignoredThis, node->right);

    if (node->element.key._data != nullptr) ++g_pc.freeStringsReleased;
    StringReleaseInline(&node->element.key._data);

    rtsetavl::SetNodeFree(ignoredThis, node);
}

}  // namespace rtavl
