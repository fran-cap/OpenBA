// ============================================================================
// src/engine/object/external_set.cpp — Wave 9 lane RES
//   `Collection::Set<ResourceEntry>` (the ExternalFactory resource tree) and
//   `Object::ExternalFactory::Clean`.
// ============================================================================
//
// Byte-faithful port of the disassembly at the retail address named above each
// function (Runtime-x64.dll, image base 0x180000000, read 2026-09-08 with
// `disassemble_function(address=)` — NOT the decompiled C, which drops
// dereferences and prints MOVSXD as a zero extension).
//
// Layout evidence and the file-level argument: external_set.h.
// Wave write-up, including what this lane DECLINED and why:
// docs/history/runtime_port/WAVE9_LANE_RES.md.
//
// ---------------------------------------------------------------------------
// THE BYTE COMPARISON AGAINST LANE INPUT'S E24 FAMILY, DONE NOT ASSUMED
// ---------------------------------------------------------------------------
// Three of the ten bodies here are the same algorithm lane INPUT ported at
// other addresses. That is a claim, so it was CHECKED by reading both
// disassemblies rather than asserted from the node layout:
//
//   rotate left   0x18002d860 vs 0x180023570   IDENTICAL instruction for
//   rotate right  0x18002d990 vs 0x1800236a0   instruction over the whole body
//   retrace       0x18002d370 vs 0x180023170   NOT identical. First difference
//                     at +0x23: 0x18002d370 carries the node in R8
//                     (`MOV R8,RAX`) where 0x180023170 carries it in RDX
//                     (`MOV RDX,RAX`). Nothing else differs and nothing writes
//                     `*slot` between the two points, so the two are
//                     semantically one — the SAME no-difference lane COLL
//                     recorded for 0x18000adc0 vs 0x18002a310.
//
// **They are still transcribed separately here.** Reusing
// `rtsetavl24::RotateLeft` for 0x18002d860 would work today and would be a
// latent trap: a body used for an address it was not read from carries no
// evidence of its own. Three ~30-line bodies is cheaper than the next lane
// having to re-derive which instantiation a shared body came from.
//
// ---------------------------------------------------------------------------
// WHAT IS NOT HERE
// ---------------------------------------------------------------------------
// `ExternalFactory::FindOrLoad` (0x1800bf320) is in external_compiled.cpp; its
// callees `ExpandRelativePath` (0x1800bd360) and `External::Load` (0x1800bc620)
// are in src/engine/io/external_path.cpp and src/engine/object/external_load.cpp.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <new>          // placement new into raw node/array element bytes

#include "external_set.h"
#include "collection/fold_index.h"   // block PATHINDEX
#include "collection/ptr_index.h"    // block VISITPASS
#include "object/resource_ext.h"     // NOT RETAIL: VISITREPLAY.md
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree / FreeLockedIgnoringSelf

// `??0Filename@System@@QEAA@AEBV01@@Z` (0x1800067d0) used to be forward-declared
// here as a C entry point. ENDGAME B.3.1 step 2 calls it as what it is — the
// `System::Filename` copy constructor — so the declaration is gone.

namespace rtresset {

using RtObject::ResourceEntry;

ProbeCounters g_rc = {};

// ---------------------------------------------------------------------------
// THE ALLOCATOR BOUNDARY.
//
// Retail INLINES `EnterCriticalSection(alloc+0x640) / Allocate / Leave` into
// every node allocator and every array grow in this family, rather than calling
// the static wrappers. This port goes through the two STATIC doors instead —
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) and
// `?MemFree@…@SAXPEAX@Z` (0x1800c7ad0) — for the reason avl_trees.cpp gives at
// length: the composed entry points take the same critical section, so the
// observable behaviour is identical, and going through ONE door instead of
// pinning `0x1800c6e60` plus the `+0x640` offset plus the two `[0x18014b39x]`
// import slots keeps four fragile facts out of this file. Lane E's allocator
// port owns all of them.
//
// B3R RT-Core: the two statics (0x1800c7a50 / 0x1800c7ad0) and the
// ignored-`this` free (0x180009c30) are called by name.
// ---------------------------------------------------------------------------

// The refcounted-String release retail inlines at every site in this family:
//     dec [p-4]; if (0) { p -= 8; MemFree(p) } else { slot = null }
// That release is `~Filename` (0x180006980) — the "else store a NULL into the
// slot" half included, since System::String's destructor carries retail's
// asymmetry verbatim (String.h). ResourceEntry::path is the real
// System::Filename; the helper keeps the two sites' spelling.
static void ReleaseInline(System::Filename* slot) {
    slot->~Filename();
}

// NOT RETAIL (block PATHINDEX): the resource tree's hash index. Never destroyed,
// for the reason expand_memo.cpp gives (a loader thread at exit).
namespace {
const char* ResourceNodeKey(const void* node) {
    return Elem(static_cast<const Node*>(node))->path._data;
}
Collection::FoldIndex& ResIndex() {
    static Collection::FoldIndex* const index = new Collection::FoldIndex(&ResourceNodeKey);
    return *index;
}
// NOT RETAIL (block VISITPASS): which node holds an External (ptr_index.h); the
// visit pass (external_visit.cpp) reads it. Kept in step at the same three
// points as the fold index, and never destroyed for the same reason.
Collection::PtrIndex& VisitIdx() {
    static Collection::PtrIndex* const index = new Collection::PtrIndex();
    return *index;
}
void IndexesInsert(const Set* set, Node* fresh) {
    ResIndex().Insert(set, fresh);
    VisitIdx().Insert(set, fresh, fresh != nullptr ? Elem(fresh)->external : nullptr);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): a new row.
    ::Object::NotifyResourceEvent(::Object::ResourceEvent::Insert, fresh, nullptr, nullptr);
}
}  // namespace

bool IndexedFind(const void* set, int32_t count, const char* key, Node** out) {
    const void* node = nullptr;
    if (!ResIndex().Find(set, count, key, &node))
        return false;
    *out = const_cast<Node*>(static_cast<const Node*>(node));
    return true;
}

void IndexClear(const void* set) {
    ResIndex().Clear(set);
    VisitIdx().Clear(set);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): every row freed.
    ::Object::NotifyResourceEvent(::Object::ResourceEvent::Clear, nullptr, nullptr, nullptr);
}

bool VisitIndexFind(const void* set, int32_t count, const ::Object::External* ext, Node** out, bool* verified) {
    const void* node = nullptr;
    if (!VisitIdx().Find(set, count, ext, &node, verified))
        return false;
    *out = const_cast<Node*>(static_cast<const Node*>(node));
    return true;
}

bool VisitIndexSetVerified(const void* set, const ::Object::External* ext) {
    return VisitIdx().SetFlag(set, ext, true);
}

void VisitIndexStats(uint64_t* size, bool* broken, uint64_t* duplicates) {
    *size = VisitIdx().Size();
    *broken = VisitIdx().Broken();
    *duplicates = VisitIdx().Duplicates();
}

// ---------------------------------------------------------------------------
// The comparator. Retail's tree comparisons all go through the CRT `_stricmp`
// at 0x18007eefc, which is locale-sensitive in MSVC — and the DLL never calls
// `setlocale`, so it runs in the "C" locale where `_stricmp` is plain ASCII
// case folding. Spelled out here for the reason filename.cpp and external.cpp
// both spell it out: src/engine must not depend on a CRT whose locale
// behaviour differs between toolchains (PLAN §4.1).
//
// This is the SAME function external.cpp already has as `StriCmpAscii` /
// `FilenameLess`. It is duplicated rather than exported because those are
// `static` in a file another lane owns, and because the counters below have to
// live INSIDE the comparator the bodies under test actually call.
// ---------------------------------------------------------------------------
static int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0)  return 0;
    }
}

// `a < b` exactly as the four descents inline it. The two null arms are
// ASYMMETRIC and that asymmetry is retail's, transcribed from the branch
// structure rather than from an idea of what a comparator should do:
//
//   0x18002ce a5  MOV RDX,[RDI]      ; node key   -> if NULL skip the LESS arm
//   0x18002ce ad  MOV RCX,[RBX]      ; search key -> if NULL TAKE the LESS arm
//   0x18002ce b5  CALL 0x18007eefc   ; _stricmp(search, node)
//   0x18002ce bc  JNS                ; less only when the result is < 0
//
// so: a null RIGHT-hand side orders below everything (return false), a null
// LEFT-hand side orders below any non-null (return true). Byte-identical to
// `System::Filename::operator<` (0x180026cb0).
static inline bool FilenameLess(const char* a, const char* b) {
    if (b == nullptr) { ++g_rc.nullNodeKey;   return false; }
    if (a == nullptr) { ++g_rc.nullSearchKey; return true;  }
    int r = StriCmpAscii(a, b);
    if (r < 0) { ++g_rc.cmpLt; return true; }
    if (r > 0) { ++g_rc.cmpGt; return false; }
    ++g_rc.cmpEq;
    return false;
}

// ---------------------------------------------------------------------------
// Recompute — the balance/height update inlined at seven points in this family.
//
//   balance = right_height - left_height
//   height  = max(left_height, right_height)
//
// counting a null child as -1, which retail expresses as `child->height + 1` or
// literal 0. BOTH FIELDS ARE 16-BIT AND RETAIL'S ARITHMETIC ON THEM IS 16-BIT
// THROUGHOUT (`INC R9W`, `SUB CX,R9W`, `CMOVG AX,R9W`), so the intermediates are
// int16_t here too. Widening them to int would be correct for every tree this
// engine builds and wrong for the one it does not.
// ---------------------------------------------------------------------------
static void Recompute(Node* n) {
    int16_t lh = n->left  ? static_cast<int16_t>(n->left->height  + 1) : 0;
    int16_t rh = n->right ? static_cast<int16_t>(n->right->height + 1) : 0;
    n->balance = static_cast<int16_t>(rh - lh);
    n->height  = (lh > rh) ? lh : rh;
}

// ---------------------------------------------------------------------------
// 0x18002d860 — rotate LEFT about `n`.
//
//   if (n == set->root) set->root = n->right;      18002d860 CMP RDX,[RCX]
//   p = n->parent;                                 18002d86c [RDX+0x18]
//   if (p) (p->left == n ? p->left : p->right) = n->right;
//   r = n->right;                                  18002d889 [RDX+0x28]
//   r->parent = n->parent;                         18002d891
//   n->right  = r->left;                           18002d899
//   if (n->right) n->right->parent = n;            18002d8a2
//   r->left   = n;                                 18002d8a6
//   n->parent = r;                                 18002d8aa
//   Recompute(r->left); Recompute(r->right); Recompute(r);
//
// The first two Recomputes are guarded on the child being non-null; the last is
// UNGUARDED because `r` cannot be null on this path. Both kept as written.
//
// NOTE the root test reads `[RCX]` — RCX is the SET, not the node — and RCX is
// then REUSED for the parent at 0x18002d86c, destroying it. Faithful; the set
// pointer is not needed again.
// ---------------------------------------------------------------------------
Node* RotateLeft(Set* set, Node* n) {
    ++g_rc.rotL;
    if (n == set->root) set->root = n->right;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->right;
        else              p->right = n->right;
    }

    Node* r = n->right;
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
// 0x18002d990 — rotate RIGHT about `n`. The exact mirror: every `+0x28` (right)
// becomes `+0x20` (left) and vice versa. Transcribed independently from
// 0x18002d990 and then diffed against RotateLeft above — the two disassemblies
// differ in exactly the six link offsets and nowhere else.
// ---------------------------------------------------------------------------
Node* RotateRight(Set* set, Node* n) {
    ++g_rc.rotR;
    if (n == set->root) set->root = n->left;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->left;
        else              p->right = n->left;
    }

    Node* l = n->left;
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
// 0x18002d370 — the retrace. Walks from `*slot` to the root, recomputing
// balances and rotating where the invariant broke.
//
// *** THE SECOND PARAMETER IS A SLOT, NOT A NODE, AND THAT IS LOAD-BEARING. ***
// Retail re-reads `*RDI` at the top of every iteration (0x18002d390) and again
// after each rotation (0x18002d3d9, 0x18002d402, 0x18002d40d, 0x18002d436),
// because a rotation can move a different node into the position being
// examined. Passing the node by value and caching it would be a different
// algorithm that happens to agree on balanced inputs. Every call site hands it
// the address of a real stack local for the same reason.
//
// The rebalance predicates are transcribed from the FLAGS, not from the obvious
// form:
//
//   0x18002d3dc  CMP word [RDX+0x30],1  / JLE skip   -> enter when balance > 1
//   0x18002d3f0  TEST CX,CX / JG take / JNS take     -> pre-rotate ONLY when the
//                                                       right child's balance
//                                                       is strictly < 0
//   0x18002d410  CMP word [RDX+0x30],-1 / JGE skip   -> enter when balance < -1
//   0x18002d424  TEST CX,CX / JS take / JLE take     -> pre-rotate ONLY when the
//                                                       left child's balance is
//                                                       strictly > 0
//
// A null child skips the rotation ENTIRELY, not just the pre-rotation
// (`TEST RAX,RAX / JZ` jumps past BOTH calls at 0x18002d3ea and 0x18002d41e).
// That is the difference between "unbalanced with a null child" being a no-op
// and being a crash, and it is retail's.
// ---------------------------------------------------------------------------
void Retrace(Set* set, Node** slot) {
    if (*slot == nullptr) return;

    for (;;) {
        ++g_rc.retraceIters;
        Node* n      = *slot;
        Node* parent = n->parent;      // captured BEFORE any rotation moves n

        Recompute(n);

        Node* cur = *slot;             // retail re-reads (0x18002d3d9)
        if (cur->balance > 1) {
            Node* r = cur->right;
            if (r) {
                if (r->balance < 0) {
                    ++g_rc.preRotR;
                    rtresset::RotateRight(set, r);
                    cur = *slot;       // 0x18002d402
                }
                rtresset::RotateLeft(set, cur);
            }
        }

        cur = *slot;                   // 0x18002d40d
        if (cur->balance < -1) {
            Node* l = cur->left;
            if (l) {
                if (l->balance > 0) {
                    ++g_rc.preRotL;
                    rtresset::RotateLeft(set, l);
                    cur = *slot;       // 0x18002d436
                }
                rtresset::RotateRight(set, cur);
            }
        }

        *slot = parent;
        if (!parent) return;
    }
}

// ---------------------------------------------------------------------------
// 0x18002e6e0 — the node's element constructor.
//
//   Filename::Filename(&node->element.path, &src->path);   0x1800067d0
//   node->element.external    = src->external;             +0x08 qword copy
//   node->element.usageMask/referenceId = src's            +0x10 qword copy
//   node->parent = parent;  node->left = node->right = 0;
//   *(uint32_t*)&node->balance = 0;
//
// THE LAST STORE IS ONE DWORD AT +0x30 AND IT ZEROES BOTH 16-BIT FIELDS
// (0x18002e72c `MOV dword ptr [RSI+0x30],EAX`). Written as two int16 stores
// here because that is what the fields are; the observable result is identical
// and the alternative — a `uint32_t` alias write — would need a cast that hides
// which fields it covers.
//
// The +0x08 and +0x10 copies are RAW QWORDS in retail: the `External*` and the
// {usageMask, referenceId} pair. Neither is refcounted, so a raw copy is the
// whole constructor for them.
// ---------------------------------------------------------------------------
Node* PairCtor(Node* node, const ResourceEntry* src, Node* parent) {
    ResourceEntry* e = Elem(node);
    new (&e->path) System::Filename(src->path);
    e->external    = src->external;
    e->usageMask   = src->usageMask;
    e->referenceId = src->referenceId;
    node->parent  = parent;
    node->left    = nullptr;
    node->right   = nullptr;
    node->balance = 0;
    node->height  = 0;
    return node;
}

// ---------------------------------------------------------------------------
// 0x18002e740 — allocate a node and construct its element.
//
// Retail: take the allocator critical section, `Allocate(0x38, align 8)`,
// release, and — ON A NULL RESULT RETURN NULL WITHOUT CONSTRUCTING. That early
// out is at 0x18002e7a2 and it is kept: it is the one place in this family
// where an allocation failure does not fault.
//
// `LEA R8D,[RDX-0x30]` with EDX=0x38 is the alignment argument, 8. Ghidra
// prints it as arithmetic on the size, which it is; the value is 8.
// ---------------------------------------------------------------------------
Node* AllocNode(void* /*ignored, never read*/, const ResourceEntry* src, Node* parent) {
    Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (n == nullptr) return nullptr;
    return PairCtor(n, src, parent);
}

// ---------------------------------------------------------------------------
// 0x18002ce60 — the insert core. Descends from the root, and on a miss
// allocates a node, links it, and retraces from ITS PARENT.
//
// THE RETRACE SLOT IS THE PARENT, NOT THE NEW NODE. `[RSP+0x30]` tracks the
// last node the descent stood on and is handed to the retrace by ADDRESS
// (0x18002cf0c `LEA RDX,[RSP+0x30]`), so a rotation that replaces that node is
// observed. Passing `&newNode` would retrace from a leaf whose balance is 0 and
// terminate immediately — a defect no balanced population reveals.
//
// THE COUNT INCREMENT MOVES. On the empty-tree path it happens BEFORE the root
// store (0x18002ce91); on the insert path it happens AFTER the retrace
// (0x18002cf1c). Both kept where they are: nothing between them reads the
// count, but a future reader comparing this to the shipped bytes should not
// have to wonder.
//
// AN EQUAL KEY RETURNS THE EXISTING NODE AND INSERTS NOTHING (0x18002cf36).
// ---------------------------------------------------------------------------
Node** InsertCore(Set* set, Node** out, const ResourceEntry* pair) {
    Node* n = set->root;

    if (n == nullptr) {
        ++g_rc.rootInsert;
        Node* fresh = AllocNode(set, pair, nullptr);
        set->count += 1;               // 0x18002ce91, BEFORE the root store
        set->root = fresh;
        IndexesInsert(set, fresh);   // NOT RETAIL (blocks PATHINDEX, VISITPASS)
        *out = fresh;
        return out;
    }

    Node* slot = n;                    // [RSP+0x30]
    for (;;) {
        if (FilenameLess(pair->path._data, Elem(n)->path._data)) {
            Node* l = n->left;
            if (l == nullptr) {
                ++g_rc.descLeft;
                Node* fresh = AllocNode(set, pair, n);
                n->left = fresh;
                rtresset::Retrace(set, &slot);   // 0x18002cf17, slot still holds `n`
                set->count += 1;       // 0x18002cf1c, AFTER the retrace
                IndexesInsert(set, fresh);   // NOT RETAIL (blocks PATHINDEX, VISITPASS)
                *out = fresh;
                return out;
            }
            n = l; slot = l;
            continue;
        }
        if (FilenameLess(Elem(n)->path._data, pair->path._data)) {
            Node* r = n->right;
            if (r == nullptr) {
                ++g_rc.descRight;
                Node* fresh = AllocNode(set, pair, n);
                n->right = fresh;
                rtresset::Retrace(set, &slot);
                set->count += 1;
                IndexesInsert(set, fresh);   // NOT RETAIL (blocks PATHINDEX, VISITPASS)
                *out = fresh;
                return out;
            }
            n = r; slot = r;
            continue;
        }
        ++g_rc.equalHit;
        *out = n;                      // 0x18002cf36 — found, nothing inserted
        return out;
    }
}

// ---------------------------------------------------------------------------
// 0x18002ca00 — the public insert. Builds the 0x18-byte element on ITS OWN
// STACK from the key and the value, hands that temporary to the core, and then
// RELEASES the temporary's Filename.
//
// The temporary is built as: `Filename::Filename(&tmp.path, key)` — a real copy
// constructor, so the key's refcount goes UP — then two raw qword copies out of
// `value`. The core's own `PairCtor` copies the temporary AGAIN into the node,
// taking a second reference, and the release at 0x18002ca7a drops the
// temporary's. Net: one reference per node, exactly as retail.
//
// **The release runs on EVERY path, including the equal-key path where nothing
// was inserted.** A "tidied" version that skipped it when the core reported a
// hit would leak one string reference per cache hit — and `FindOrLoad` reaches
// this ~22.6k times in one menu-to-gameplay run.
// ---------------------------------------------------------------------------
Node** Insert(Set* set, Node** out, const System::Filename* key, const EntryValue* value) {
    // The Filename copy ctor (it member-initialises `_data` to null before it
    // adopts: retail's explicit pre-null is inside it), then the value's fields.
    ResourceEntry tmp(*key, value->external, value->usageMask, value->referenceId);

    InsertCore(set, out, &tmp);

    return out;                        // ~ResourceEntry: the release at 0x18002ca70                        // 0x18002caa1 MOV RAX,RSI — the out slot
}

// ---------------------------------------------------------------------------
// 0x18002ee40 — release a node's element and free the node.
//
// The whole body is guarded on the node being non-null (0x18002ee40 `TEST
// RDX,RDX / JZ` to the RET), so a null node is a no-op rather than a fault.
// Note which parameter that is: the NODE IS THE SECOND ARGUMENT and the first
// is ignored, the same shape as 0x180009c30 underneath it.
// ---------------------------------------------------------------------------
void FreeEntryNode(void* ignored, Node* node) {
    if (node == nullptr) return;
    ReleaseInline(&Elem(node)->path);
    rtalloc::FreeLockedIgnoringSelf(ignored, node);   // 0x180009c30
}

// ---------------------------------------------------------------------------
// 0x18002cf60 — erase `z` from `set`, release its element, free its node, and
// retrace.
//
// `set->count` is decremented FIRST, at 0x18002cf85, before anything can fail.
// Kept there.
//
// THREE CASES, and retail's third is a PREDECESSOR splice, not the successor
// splice the textbook uses:
//
//   1. z->right == null            : splice z->left into z's place
//   2. z->right != null, left null : splice z->right into z's place
//   3. both children               : `s` = the RIGHTMOST node of z->LEFT subtree
//                                    (0x18002d070 starts at z->left and follows
//                                    `right` links), unlink it, and put it where
//                                    z was.
//
// Reading case 3 as a successor splice would be wrong in a way no balanced input
// reveals: it produces a valid AVL tree with the same key set and a DIFFERENT
// SHAPE, so a check of the key sequence alone would pass.
//
// CASE 1 RELEASES THE ELEMENT INLINE AND THEN CALLS 0x180009c30 DIRECTLY;
// cases 2 and 3 call the combined 0x18002ee40. Same effect, different code, and
// transcribed as retail wrote it — MSVC out-lined the pair for two of the three
// arms and inlined it for the other.
//
// The retrace start point differs per case and is chosen by a CMOV at
// 0x18002d15f (`CMOVZ RDX,RAX` on `TEST RSI,RSI`), so it is written as the same
// conditional rather than as two calls.
// ---------------------------------------------------------------------------
void Erase(Set* set, Node* z) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): the row is about to go.
    ::Object::NotifyResourceEvent(::Object::ResourceEvent::Erase, z, nullptr, nullptr);
    ResIndex().Erase(set, z);               // NOT RETAIL (block PATHINDEX): the key is still live
    VisitIdx().Erase(set, z, Elem(z)->external);   // NOT RETAIL (block VISITPASS)
    Node* retraceFromParent = z->parent;    // [RSP+0x50], "local A"
    set->count -= 1;                        // 0x18002cf85, before anything else

    Node* retraceFromChild = nullptr;       // [RSP+0x58], "local B"

    if (z->right == nullptr) {
        // ---- case 1 -------------------------------------------------------
        ++g_rc.erase0Child;
        Node* p = z->parent;
        Node* l = z->left;
        if (p) { if (p->left == z) p->left = l; else p->right = l; }
        else   { set->root = l; }

        Node* replacement = z->left;
        retraceFromChild  = replacement;
        if (replacement) replacement->parent = z->parent;

        ReleaseInline(&Elem(z)->path);      // 0x18002cfdc, inlined here
        rtalloc::FreeLockedIgnoringSelf(set, z);      // 0x18002d005
        rtresset::Retrace(set, replacement ? &retraceFromChild : &retraceFromParent);
        return;
    }

    if (z->left == nullptr) {
        // ---- case 2 -------------------------------------------------------
        ++g_rc.erase1Child;
        Node* p = z->parent;
        Node* r = z->right;
        if (p) { if (p->left == z) p->left = r; else p->right = r; }
        else   { set->root = r; }

        Node* replacement = z->right;
        retraceFromChild  = replacement;
        if (replacement) replacement->parent = z->parent;

        FreeEntryNode(r, z);                // 0x18002d066
        rtresset::Retrace(set, replacement ? &retraceFromChild : &retraceFromParent);
        return;
    }

    // ---- case 3: both children ---------------------------------------------
    ++g_rc.erase2Child;
    Node* s = z->left;
    while (s->right) s = s->right;          // 0x18002d070, the PREDECESSOR

    Node* spliced = nullptr;                // RSI; 0x18002d07e XOR ESI,ESI
    retraceFromParent = nullptr;            // 0x18002d080 stores RSI into local A

    if (s != z->left) {
        ++g_rc.erasePredSplice;
        Node* sp = s->parent;
        sp->right = s->left;                // 0x18002d092
        if (s->left) s->left->parent = sp;
        s->left = z->left;                  // 0x18002d0a3
        retraceFromParent = sp;             // local A = sp
        spliced           = sp;
        Recompute(sp);                      // 0x18002d0ac..0x18002d0eb
    }

    s->right = z->right;                    // 0x18002d0ef
    if (s->left)  s->left->parent  = s;
    if (s->right) s->right->parent = s;
    s->parent = z->parent;                  // 0x18002d115

    if (z->parent == nullptr) {
        ++g_rc.eraseRootSplice;
        set->root = s;
        s->parent = nullptr;                // 0x18002d123, a SECOND store
        retraceFromChild = set->root;       // re-read (0x18002d12b)
    } else {
        retraceFromChild = s;
        Node* p = z->parent;
        if (p->left == z) p->left = s; else p->right = s;
    }

    FreeEntryNode(s, z);                    // 0x18002d14d
    rtresset::Retrace(set, spliced ? &retraceFromParent : &retraceFromChild);
}

// ---------------------------------------------------------------------------
// 0x18002dd70 — find by key. The same descent InsertCore uses, without the
// insert arms. Returns the NODE, and every caller reads `element.external` out
// of it (`0x1800be2a2  MOV RBX,[RBX+0x8]`).
//
// `external.cpp`'s `ResourceSetFind` is the same body, ported in Wave 3 as a
// file-static.
// ---------------------------------------------------------------------------
Node* Find(const Set* set, const System::Filename* key) {
    // NOT RETAIL (block PATHINDEX): the index's answer when it has one; the
    // descent below otherwise.
    Node* indexed = nullptr;
    const bool answered = IndexedFind(set, set->count, key->_data, &indexed);
    if (answered && !OPENBA_PATHINDEX_CHECK)
        return indexed;
    Node* n = set->root;
    while (n != nullptr) {
        if (FilenameLess(key->_data, Elem(n)->path._data))      n = n->left;
        else if (FilenameLess(Elem(n)->path._data, key->_data)) n = n->right;
        else                                                  break;
    }
    Collection::FoldIndex::Check("resources", key->_data, answered, indexed, n);
    return n;
}

// ---------------------------------------------------------------------------
// 0x18002e3d0 — release `count` Filenames starting at `first`.
//
// The whole body is guarded on `count > 0` (0x18002e3d0 `TEST R8D,R8D / JLE` to
// the RET). First parameter ignored, node-free shape again.
// ---------------------------------------------------------------------------
void ArrayDestroyRange(void* /*ignored*/, System::Filename* first, int count) {
    if (count <= 0) return;
    for (int i = 0; i < count; ++i) first[i].~Filename();
}

// ---------------------------------------------------------------------------
// 0x18002dff0 — `Collection::Array<Filename>::Resize(newSize, fill)`.
//
// THE GROWTH RULE IS NOT `capacity * 2` AND IT IS NOT UNCONDITIONAL:
//
//   0x18002e02a  if (newSize <= capacity) DO NOT REALLOCATE AT ALL
//   0x18002e02f  capacity = newSize
//   0x18002e032  if (newSize == oldSize + 1 && oldSize != 0) capacity = newSize*3
//
// so a one-at-a-time grow from a non-empty array TREBLES, and every other grow
// takes exactly what was asked for. `Clean` only ever grows by one, so the
// treble arm is the one that runs — but the other is retail's and is kept.
// The `oldSize != 0` guard is what stops the FIRST append from trebling.
//
// The copy loop then fills `[0, newSize)`: slots below `oldSize` are
// copy-constructed from the OLD storage, slots at or above it from `fill`
// (0x18002e07d `CMP EBX,EBP / JL`). When no reallocation happened the loop
// STARTS at `oldSize` (0x18002e05a `CMP R15,[R14] / CMOVZ EBX,EBP`), so the
// already-live prefix is not re-constructed over itself.
//
// The per-slot `if (dst != null)` at 0x18002e07b comes from the flags of
// `ADD RCX,RDI`, not from a written test. Transcribed because it is there.
//
// Tail: if the storage moved, destroy the old range and free it; if it did not
// and the array SHRANK, destroy the tail. Both arms kept even though `Clean`
// only ever reaches the first.
// ---------------------------------------------------------------------------
void ArrayResize(FilenameArray* self, int newSize, const System::Filename* fill) {
    int oldSize = self->size;
    if (newSize == oldSize) return;

    System::Filename* oldData = self->data;
    self->size = newSize;

    if (newSize > self->capacity) {
        ++g_rc.cleanArrayGrow;
        self->capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) self->capacity = newSize * 3;
        self->data = static_cast<System::Filename*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(self->capacity) * 8, 8));
    }

    int i = (oldData == self->data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        System::Filename* dst = self->data + i;
        if (dst == nullptr) continue;                 // 0x18002e07b, from ADD's ZF
        const System::Filename* src = (i < oldSize) ? (oldData + i) : fill;
        new (dst) System::Filename(*src);
    }

    if (oldData != self->data) {
        ArrayDestroyRange(self, oldData, oldSize);    // 0x18002e0a3
        System::MemoryAllocator::MemFree(oldData);    // 0x18002e0ad..0x18002e0dd
    } else if (oldSize > newSize) {
        ArrayDestroyRange(self, oldData + newSize, oldSize - newSize);  // 0x18002e0f2
    }
}

}  // namespace rtresset

namespace RtObject {

// ============================================================================
// ?Clean@ExternalFactory@Object@@QEAAXXZ   retail 0x1800be310
//
// The teardown half of the resource cache. Three phases, in retail's order:
//
//   1. WALK the whole tree in order. Clear this factory's clean bit out of
//      every entry's `usageMask`, and COLLECT THE KEY (not the node) of every
//      entry whose mask is now empty into a local `Array<Filename>`.
//   2. For each collected key: look the node up AGAIN, destroy its `External`
//      through vtable slot 0 with the scalar-deleting flag, and erase the node.
//   3. Bump `_generation`, then release every collected key and free the array.
//
// *** WHY IT COLLECTS KEYS AND RE-LOOKS-THEM-UP, WHICH LOOKS LIKE WASTE ***
// Because phase 2 ERASES, and an erase rebalances: any node pointer collected
// in phase 1 could have been spliced or freed by an earlier erase in phase 2.
// The key is stable, the node pointer is not. This is the shape of the UAF the
// Work family paid for three times over, arrived at from the other side —
// retail already knew.
//
// *** RETAIL HARD-FAULTS ON A KEY IT CANNOT FIND, AND THE GUARD IS NOT ADDED ***
// 0x1800be451 `MOV RBX,R13` (R13 == 0) on both the empty-tree exit and the
// not-found exit, immediately followed by 0x1800be454 `MOV RCX,[RBX+0x8]` — a
// read of address 0x8. Retail asserts that a key it collected from this tree
// one loop earlier is still in it, and dies at once if it is not. This port
// keeps that deref unguarded: a null check here would not restore the
// behaviour, it would skip it, and it would hide exactly the desync the fault
// is diagnosing (CLAUDE.md, "Don't 'fix' a faithful crash with a null check").
//
// *** THE CLEAN BIT IS SIGNED AND -1 MEANS "EVICT EVERYTHING" ***
// 0x1800be36a `TEST ECX,ECX / JS` skips the mask clear when `_cleanBitIndex` is
// negative, and 0x1800be380 `CMP dword [R15],0 / JGE` then makes the eviction
// test `mask == 0 || cleanBitIndex < 0`. So a negative index evicts every entry
// regardless of its mask. That is the whole-cache flush, and reading the index
// as unsigned would silently turn it into a no-op.
//
// NO ORACLE, AND THE POSITIVE REASON. `Clean` mutates the one live factory
// tree and destroys the resources it evicts; running it twice against the same
// factory would double-free every `External` it touched. The instrument for its
// three-phase composition is the `g_rc.clean*` counters.
// ============================================================================
void RtObject_ExternalFactory_Clean(ExternalFactory* self) {
    using rtresset::Node;
    using rtresset::Set;
    using rtresset::Elem;
    using rtresset::g_rc;

    Set* set = reinterpret_cast<Set*>(&self->_resources);  // CAST-OK: elem_stride

    rtresset::FilenameArray evicted;         // [RSP+0x28]
    evicted.data     = nullptr;              // 0x1800be337
    evicted.size     = 0;                    // 0x1800be33e
    evicted.capacity = 0;
    int collected = 0;                       // EBP

    // ---- phase 1: walk in order, clear the bit, collect the empties --------
    Node* n = set->root;                     // 0x1800be342
    if (n) {
        while (n->left) n = n->left;         // 0x1800be350, leftmost
        while (n) {
            ++g_rc.cleanVisited;
            int32_t bit = self->_cleanBitIndex;
            if (bit >= 0) {                  // 0x1800be36c JS
                ++g_rc.cleanBitCleared;
                Elem(n)->usageMask &= ~(1u << bit);
            }
            if (Elem(n)->usageMask == 0 || self->_cleanBitIndex < 0) {
                ++g_rc.cleanEvicted;
                rtresset::ArrayResize(&evicted, collected + 1,
                                      &Elem(n)->path);   // ResourceEntry::path, the key at +0x00
                collected = evicted.size;    // 0x1800be396, RE-READ from the array
            }
            // in-order successor, transcribed from 0x1800be39a..0x1800be3e6
            if (n->right) {
                n = n->right;
                while (n->left) n = n->left;
            } else {
                Node* p = n->parent;
                while (p && p->right == n) { n = p; p = p->parent; }
                n = p;
            }
        }
    }
    System::Filename* keys = evicted.data;   // 0x1800be3ef, R14

    // ---- phase 2: destroy and erase, one key at a time ---------------------
    if (collected > 0) {
        System::Filename* k = keys;
        for (int remaining = collected; remaining != 0; --remaining, ++k) {
            Node* hit = rtresset::Find(set, k);
            // UNGUARDED, and retail's: 0x1800be454 reads [hit+8] on the
            // not-found path with hit == 0. See the banner.
            ::Object::External* ext = Elem(hit)->external;
            if (ext) {
                // vtable slot 0, scalar deleting destructor, flag 1.
                // B3X W0: by name on the object's own hierarchy root.
                delete ext;   // 0x1800be465
            }
            rtresset::Erase(set, hit);   // 0x1800be46e
        }
    }

    self->_generation += 1;              // 0x1800be47c

    // ---- phase 3: release the collected keys, free the array ---------------
    //
    // 0x1800be490..0x1800be4ff is `ArrayDestroyRange` INLINED — the same
    // `dec [p-4] / free at p-8 / else null the slot` loop, over `collected`
    // slots starting at `keys`. Written as the call because it IS that
    // function; MSVC out-lined it at 0x18002e3d0 for the two array-resize call
    // sites and inlined it here.
    rtresset::ArrayDestroyRange(nullptr, keys, collected);

    // 0x1800be501..0x1800be539: free the array storage itself, under the
    // allocator lock. Routed through the MemFree door for the reason in
    // THE ALLOCATOR BOUNDARY above. The `if (keys)` is retail's own
    // `TEST R14,R14 / JZ`, kept even though MemFree null-checks too.
    if (keys) System::MemoryAllocator::MemFree(keys);   // 0x1800c7ad0

    evicted.data     = nullptr;          // 0x1800be53f
    evicted.size     = 0;
    evicted.capacity = 0;

    // ONE LINE PER CALL. The `calls=` discipline: this body has no
    // ours-vs-theirs oracle, so the only thing separating "verified by
    // construction" from "never executed" is a live count. `Clean` runs at
    // world teardown, a handful of times per session, so an unconditional
    // printf costs nothing. The counters are running totals over every `Clean`
    // of the session (`g_rc` is never reset): `visited` counts nodes walked,
    // `evicted` entries destroyed; `arrayGrow` is the one branch of `ArrayResize`
    // this path reaches, and it is the ONLY live coverage that body gets.
    printf("[ResSet] Clean: visited=%llu evicted=%llu bitCleared=%llu "
                "arrayGrow=%llu cleanBit=%d generation=%d\n",
                (unsigned long long)g_rc.cleanVisited,
                (unsigned long long)g_rc.cleanEvicted,
                (unsigned long long)g_rc.cleanBitCleared,
                (unsigned long long)g_rc.cleanArrayGrow,
                static_cast<int>(self->_cleanBitIndex),
                static_cast<int>(self->_generation));
}

}  // namespace RtObject

// EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): object/resource_ext.h.
void Object::NotifyResourceEvent(ResourceEvent e, void* row, const External* external, const char* newPath) {
    ExternalFactory* const f = ExternalFactory::_singleton;
    if (f == nullptr)
        return;
    if (ExternalFactory::_extVisitReplay != nullptr)
        ExternalFactory::_extVisitReplay->event(*f, e, row, external, newPath);
    if (ExternalFactory::_extFindFast != nullptr)
        ExternalFactory::_extFindFast->event(*f, e, row, external, newPath);
}
