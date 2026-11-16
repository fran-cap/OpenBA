// ============================================================================
// src/engine/collection/set_avl.cpp — Wave 9 lane VID2
//   The four unexported `Collection::Set` AVL bodies that stand between the
//   cutover boot and `Video::Device::PlatformStartup`.
// ============================================================================
//
// WHY A COLLECTION FILE IN A VIDEO LANE
// -------------------------------------
// `?PlatformStartup@Device@Video@@AEAA_NXZ` is the next boot wall after the IO
// chain: our ported `Engine::Startup` calls it at
// src/engine/engine/engine_lifecycle.cpp and a failure there returns 0 with
// `g_startupFailAt == 11`. Its last act is to register two USER32 message
// callbacks, and `PlatformShutdown` opens by removing them again. Both are AVL
// operations on the `Collection::Set` rooted at `Application+0x18`, and the
// five helpers underneath were the whole of lane VID2's recorded blocker.
//
// Four of them are here. The fifth — `0x180009c30`, the allocator-lock Free
// wrapper — is `rtalloc::FreeLockedIgnoringSelf` (see "THE FIFTH HELPER" below).
//
// THESE FOUR ARE ELEMENT-TYPE-AGNOSTIC, WHICH IS WHY THEY ARE HERE AND NOT IN
// src/engine/application/. They read `_parent` at +0x10, `_left` at +0x18,
// `_right` at +0x20 and the two 16-bit balance words at +0x28/+0x2a, and they
// NEVER touch the element. MSVC emitted one copy for every `Collection::Set`
// instantiation whose node header matches, so a single port at each address
// serves all of them. Nothing here asserts an element size for that reason.
//
// TRANSITIVE CLOSURE, COUNTED BEFORE ANY BODY WAS WRITTEN
// -------------------------------------------------------
// Lane PICK's finding is that porting the visible rows of a census can trade N
// unregistered doors for more than N new ones, so the closure is counted first
// (docs/history/runtime_port/WAVE9_LANE_PICK.md §2). Counted here:
//
//   0x18000a5b0  node alloc + element copy  -> 0x1800c6e60 (Allocate, REGISTERED)
//   0x18000adc0  AVL retrace                -> 0x18000b040, 0x18000b170
//   0x18000b040  rotate left                -> LEAF, no calls
//   0x18000b170  rotate right               -> LEAF, no calls
//   0x18000aba0  AVL erase                  -> 0x180009c30 (REGISTERED), 0x18000adc0
//
// **The closure is CLOSED at five bodies.** It does not explode the way
// Picking's did: both rotations are leaves (verified with
// `disassemble_function(filter_mnemonics="CALL,RET")` — the only RET, no CALL),
// and the erase and the retrace call only each other and two already-registered
// allocator entry points. `0x18000a5b0` lives with its caller in
// src/engine/application/message_callbacks.cpp because it is the one
// element-type-SPECIFIC member of the group (it copies 0x10 bytes and allocates
// 0x30).
//
//   doors before this file: 4 unregistered, reached by nothing (no caller yet)
//   doors after:            0 unregistered, reached by the message-callback port
//
// THE FIFTH HELPER — 0x180009c30
// -------------------------------
// It is the allocator-lock Free wrapper, `rtalloc::FreeLockedIgnoringSelf` in
// src/engine/system/allocator.cpp, with a trap worth restating: it takes TWO
// parameters and IGNORES THE FIRST, so a one-argument `MemFree` in its place
// would free the ignored `this` and corrupt the free list.
//
// This file relies only on that contract (ignore arg 1, free arg 2).
//
// ORACLE
// ------
// None of these four can be verified by an ours-then-theirs replay at a live
// call site: every one of them mutates the shared tree, and running the second
// implementation starts from the state the first one left — a different
// experiment whose first product is a corrupted tree. So they are verified in
// tests/unit/set_ported_test.cpp: random insert / remove / find sequences with
// an invariant walk (order, parent links, height, balance) after every
// operation, and the same sequence into a Set over the generic core compared by
// tree shape after every operation rather than once at the end.
//
// Comparing only at the end is not good enough here and the reason is specific:
// AVL defects CANCEL. A rotation that is wrong in one direction and wrong again
// in the other leaves a tree that is differently shaped in the middle and
// identical at the end, and a single final comparison reports green.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "collection/Collection.h"
#include "set_avl.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name

namespace rtsetavl {

// ---------------------------------------------------------------------------
// The height/balance recomputation, which appears SEVEN times across the three
// non-leaf bodies and is identical in all seven.
//
//   0x18000adee  RAX = n->left ;  dx = RAX ? RAX->height + 1 : 0
//   0x18000adfe  RAX = n->right;  cx = RAX ? RAX->height + 1 : 0
//   0x18000ae12  CMP DX,CX / MOVZX EAX,CX / CMOVG AX,DX      ; height = max(l,r)
//   0x18000ae1c  SUB CX,DX                                    ; balance = r - l
//   0x18000ae1f  n->balance = cx ; n->height = ax
//
// Note the arithmetic is 16-BIT throughout — `INC DX` on a word, `SUB CX,DX` on
// words, `CMOVG` on words. A tree deep enough to overflow a 16-bit height is
// not reachable, but the truncation is written as retail wrote it rather than
// widened, because "it cannot overflow" is a claim about call sites this lane
// does not own.
//
// The MAX defaults to the RIGHT height: `MOVZX EAX,CX` loads r first and
// `CMOVG` only replaces it when l > r. Equal heights therefore yield r. That is
// indistinguishable here (they are equal) and is kept so the shape of the
// transcription matches.
// ---------------------------------------------------------------------------
static inline void Recompute(Node* n) {
    const int16_t l = n->left  ? static_cast<int16_t>(n->left->height  + 1) : 0;
    const int16_t r = n->right ? static_cast<int16_t>(n->right->height + 1) : 0;
    n->balance = static_cast<int16_t>(r - l);
    n->height  = (l > r) ? l : r;
}

// ---------------------------------------------------------------------------
// 0x18000b040 — rotate LEFT about `n`. Returns the new subtree root.
//
// Retail's order, which is preserved because the intermediate states are
// observable to anything walking the tree concurrently (nothing does, but the
// order is free to keep):
//
//   if (n == set->_root) set->_root = n->right;      0x18000b040
//   p = n->parent; if (p) (p->left == n ? p->left : p->right) = n->right;
//   r = n->right;
//   r->parent = n->parent;                           0x18000b071
//   n->right  = r->left;                             0x18000b079
//   if (n->right) n->right->parent = n;
//   r->left   = n;                                   0x18000b086
//   n->parent = r;
//   Recompute(r->left); Recompute(r->right); Recompute(r);
//
// The three Recomputes are guarded on the child being non-null (`TEST R10,R10 /
// JZ`), and the last one is unguarded because `r` cannot be null on this path.
// Both kept as written.
//
// NOTE the root test reads `[RCX]` — RCX is the SET, not the node — and RCX is
// then REUSED for the parent at 0x18000b04c, destroying it. Faithful; the set
// pointer is not needed again.
// ---------------------------------------------------------------------------
Node* RotateLeft(Set* set, Node* n) {
    if (n == set->root) set->root = n->right;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->right;
        else              p->right = n->right;
    }

    Node* r  = n->right;
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
// 0x18000b170 — rotate RIGHT about `n`. The exact mirror of the above: every
// `+0x20` (right) becomes `+0x18` (left) and vice versa. Transcribed
// independently from 0x18000b170 rather than derived from RotateLeft, and then
// diffed against it — the two disassemblies differ in exactly the six link
// offsets and nowhere else.
// ---------------------------------------------------------------------------
Node* RotateRight(Set* set, Node* n) {
    if (n == set->root) set->root = n->left;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->left;
        else              p->right = n->left;
    }

    Node* l  = n->left;
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
// 0x18000adc0 — the retrace. Walks from `*slot` to the root, recomputing
// balances and rotating where the invariant broke.
//
// *** THE SECOND PARAMETER IS A SLOT, NOT A NODE, AND THAT IS LOAD-BEARING. ***
// Retail re-reads `*RDI` at the top of every iteration (0x18000ade0) and again
// after each rotation (0x18000ae52, 0x18000ae5d, 0x18000ae86), because a
// rotation can move a different node into the position being examined. Passing
// the node by value and caching it would be a different algorithm that happens
// to agree on balanced inputs. Both call sites hand it the address of a real
// stack local for the same reason (`LEA RDX,[RSP+0x50]` in
// RegisterMessageCallback, `LEA RDX,[RSP+0x30]`/`[RSP+0x38]` in the erase).
//
// The rebalance predicates are transcribed from the flags, not from the
// obvious form:
//
//   0x18000ae2c  CMP word [RDX+0x28],1  / JLE skip     -> enter when balance > 1
//   0x18000ae40  TEST CX,CX / JG take / JNS take       -> pre-rotate ONLY when
//                                                          the right child's
//                                                          balance is < 0
//   0x18000ae60  CMP word [RDX+0x28],-1 / JGE skip     -> enter when balance < -1
//   0x18000ae74  TEST CX,CX / JS take / JLE take       -> pre-rotate ONLY when
//                                                          the left child's
//                                                          balance is > 0
//
// A null child skips the rotation ENTIRELY, not just the pre-rotation
// (`TEST RAX,RAX / JZ` jumps past both calls at 0x18000ae3a and 0x18000ae6e).
// That is the difference between "unbalanced with a null child" being a no-op
// and being a crash, and it is retail's.
// ---------------------------------------------------------------------------
void Retrace(Set* set, Node** slot) {
    if (*slot == nullptr) return;

    for (;;) {
        Node* n      = *slot;
        Node* parent = n->parent;      // captured BEFORE any rotation moves n

        Recompute(n);

        Node* cur = *slot;             // retail re-reads (0x18000ae29)
        if (cur->balance > 1) {
            Node* r = cur->right;
            if (r) {
                if (r->balance < 0) {
                    RotateRight(set, r);
                    cur = *slot;       // 0x18000ae52
                }
                RotateLeft(set, cur);
            }
        }

        cur = *slot;                   // 0x18000ae5d
        if (cur->balance < -1) {
            Node* l = cur->left;
            if (l) {
                if (l->balance > 0) {
                    RotateLeft(set, l);
                    cur = *slot;       // 0x18000ae86
                }
                RotateRight(set, cur);
            }
        }

        *slot = parent;
        if (!parent) return;
    }
}

// ---------------------------------------------------------------------------
// 0x18000aba0 — erase `z` from `set`, free its node, retrace.
//
// `set->_count` is decremented FIRST, at 0x18000abae, before anything can fail.
// Kept there.
//
// THREE CASES, and retail's third one is a PREDECESSOR splice, not the
// successor splice the textbook uses:
//
//   1. z->right == null            : splice z->left into z's place
//   2. z->right != null, left null : splice z->right into z's place
//   3. both children               : `s` = the RIGHTMOST node of z->LEFT
//                                     subtree (0x18000ac66 starts at z->left
//                                     and follows `right` links), unlink it,
//                                     and put it where z was.
//
// Reading case 3 as a successor splice would be wrong in a way no balanced
// input reveals: it produces a valid AVL tree with the same key set and a
// DIFFERENT SHAPE, so a probe that only checked the key sequence would pass.
// tests/unit/set_ported_test.cpp compares shape.
//
// The retrace start point differs per case and is chosen by a CMOV at
// 0x18000ad64 (`CMOVZ RDX,RAX` on `TEST RDI,RDI`), so it is written as the same
// conditional rather than as two calls.
// ---------------------------------------------------------------------------
void Erase(Set* set, Node* z) {
    Node* retraceFromParent = z->parent;    // [RSP+0x30], "local A"
    set->count -= 1;                        // 0x18000abae, before anything else

    Node* retraceFromChild = nullptr;       // [RSP+0x38], "local B"
    Node* replacement      = nullptr;       // RDI

    if (z->right == nullptr) {
        // ---- case 1 -------------------------------------------------------
        Node* p = z->parent;
        Node* l = z->left;
        if (p) { if (p->left == z) p->left = l; else p->right = l; }
        else   { set->root = l; }

        replacement      = z->left;
        retraceFromChild = replacement;
        if (replacement) replacement->parent = z->parent;

        SetNodeFree(set, z);
        Retrace(set, replacement ? &retraceFromChild : &retraceFromParent);
        return;
    }

    if (z->left == nullptr) {
        // ---- case 2 -------------------------------------------------------
        // Shares case 1's tail exactly (retail JMPs into it at 0x18000abe8).
        Node* p = z->parent;
        Node* r = z->right;
        if (p) { if (p->left == z) p->left = r; else p->right = r; }
        else   { set->root = r; }

        replacement      = z->right;
        retraceFromChild = replacement;
        if (replacement) replacement->parent = z->parent;

        SetNodeFree(set, z);
        Retrace(set, replacement ? &retraceFromChild : &retraceFromParent);
        return;
    }

    // ---- case 3: both children ---------------------------------------------
    Node* s = z->left;
    while (s->right) s = s->right;          // 0x18000ac70, the PREDECESSOR

    replacement      = nullptr;             // 0x18000ac7e XOR EDI,EDI
    retraceFromParent = nullptr;            // 0x18000ac80 stores RDI into local A

    if (s != z->left) {
        Node* sp = s->parent;
        sp->right = s->left;                // 0x18000ac92
        if (s->left) s->left->parent = sp;
        retraceFromParent = sp;             // local A = sp
        replacement       = sp;             // RDI = sp
        s->left = z->left;                  // 0x18000aca8
        Recompute(sp);                      // 0x18000acac..0x18000acf0
    }

    s->right = z->right;                    // 0x18000acf8
    if (s->left)  s->left->parent  = s;
    if (s->right) s->right->parent = s;
    s->parent = z->parent;

    if (z->parent == nullptr) {
        set->root = s;
        s->parent = nullptr;                // 0x18000ad28, a SECOND store
        retraceFromChild = set->root;       // re-read (0x18000ad30)
    } else {
        retraceFromChild = s;
        Node* p = z->parent;
        if (p->left == z) p->left = s; else p->right = s;
    }

    SetNodeFree(set, z);
    Retrace(set, replacement ? &retraceFromParent : &retraceFromChild);
}

// ---------------------------------------------------------------------------
// The 0x180009c30 boundary.
//
// TWO PARAMETERS. The first is ignored by the callee and retail passes junk in
// it (in case 1 whatever RCX last held, in case 3 the splice node `s`). The set
// pointer is passed here because that is what retail's register happens to hold
// most often; it is not read.
// ---------------------------------------------------------------------------
// The node goes back through `System::MemoryAllocator::MemFree`. It is the same
// free as 0x180009c30's own body (`FreeLockedIgnoringSelf`,
// src/engine/system/allocator.cpp), which drops the ignored argument and calls
// 0x1800c7ad0. The ignored first parameter stays in the SIGNATURE: set_avl.h
// declares it with two arguments.
void SetNodeFree(void* ignored, void* node) {
    (void)ignored;
    System::MemoryAllocator::MemFree(node);   // the null test is inside ?MemFree@, as before
}

}  // namespace rtsetavl
