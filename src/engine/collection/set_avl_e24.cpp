// ============================================================================
// src/engine/collection/set_avl_e24.cpp — Wave 9 lane INPUT
//   The five unexported `Collection::Set` AVL bodies for the 0x18-byte element
//   instantiation — the ones `Input::KeyMap` reaches by address.
// ============================================================================
//
// WHY THIS FILE EXISTS
// --------------------
// `tools/addr_door_audit.py --csv` listed three unregistered by-address doors
// in src/engine/input/input.cpp:
//
//   0x180023060  input.cpp:245  the node allocator (KeyMap::RegisterInput)
//   0x180023130  input.cpp:137  the subtree destroy (KeyMap::ClearAll)
//   0x180023170  input.cpp:247  the retrace       (KeyMap::RegisterInput)
//
// All three are `Collection::Set` internals, element-type-agnostic within their
// instantiation, and unexported in BOTH builds (checked: no name in retail's
// 4,926 exports nor in Runtime-x64-profile.dll's 5,182 resolves to any of
// them).
//
// TRANSITIVE CLOSURE, COUNTED BEFORE A BODY WAS WRITTEN
// -----------------------------------------------------
// Lane PICK traded 5 visible rows for 14 new ones by porting a census instead
// of a closure. Counted here with `disassemble_function(filter_mnemonics=…)`
// on each body:
//
//   0x180023060  alloc     -> 0x1800c6e60 Allocate  (REGISTERED, allocator.cpp)
//                             + Enter/LeaveCriticalSection on the allocator lock
//                             — the same triple that IS `MemAlloc` 0x1800c7a50
//   0x180023130  destroy   -> ITSELF (recursion) + 0x180009c30 (REGISTERED)
//   0x180023170  retrace   -> 0x180023570, 0x1800236a0     <- 2 NEW
//   0x180023570  rot left  -> LEAF (only RET, no CALL)
//   0x1800236a0  rot right -> LEAF (only RET, no CALL)
//
// **The closure is CLOSED at five bodies and introduces ZERO new unregistered
// doors.** The three visible rows cost two more bodies, and both of those are
// leaves.
//
// RELATION TO set_avl.cpp — READ set_avl_e24.h's TABLE FIRST
// -----------------------------------------------------------
// Lane VID2 ported the SAME FOUR ALGORITHMS at four DIFFERENT addresses for the
// 0x10-byte element instantiation. Nothing is shared with it and nothing should
// be: every node offset differs (element first, so the whole header shifts by
// 8), and one walk over two node layouts is the defect
// `reference_one_bst_find_two_node_layouts.md` exists to prevent. The two files
// were transcribed independently from their own disassembly and then diffed
// against each other; they agree in structure and differ in exactly the offsets
// the table lists, which is the cross-check, not the source.
//
// ORACLE
// ------
// None of these five can be verified by an ours-then-theirs replay at a live
// call site: each mutates the one shared tree, so the second implementation
// starts from the state the first left.
//
// `DestroySubtree` is the one body with no ours-vs-theirs comparison available
// at all, and that is stated rather than papered over — see its banner.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "collection/Collection.h"
#include "set_avl_e24.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name

namespace rtsetavl24 {

unsigned g_destroyVisits = 0;

namespace {

// ---------------------------------------------------------------------------
// The allocator boundary for 0x180023060.
//
// `0x180023060` open-codes `EnterCriticalSection(*0x1801fc718 + 0x640);
// Allocate(*0x1801fc718, 0x38, 8); LeaveCriticalSection(...)` at
// 0x18002308b..0x1800230bc. That triple IS
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` at 0x1800c7a50 — lane Q
// disassembled that composition instruction for instruction, and
// src/engine/application/message_callbacks.cpp already reaches the E16
// twin's allocator the same way. Note how retail builds the alignment:
// `MOV EDX,0x38 ; LEA R8D,[RDX-0x30]` is `Allocate(size=0x38, align=8)`.
// ---------------------------------------------------------------------------
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);   // 0x1800c7a50
}

// ---------------------------------------------------------------------------
// The height/balance recomputation, which appears SEVEN times across the three
// non-leaf bodies here and is identical in all seven (three in each rotation,
// one at the top of the retrace loop):
//
//   0x1800231a3  RAX = n->left ; r8w = RAX ? RAX->height + 1 : 0
//   0x1800231ba  RAX = n->right; cx  = RAX ? RAX->height + 1 : 0
//   0x1800231c5  CMP R8W,CX / MOVZX EAX,CX / CMOVG AX,R8W    ; height = max
//   0x1800231d1  SUB CX,R8W                                  ; balance = r - l
//
// 16-BIT throughout, exactly as retail wrote it. `MOVZX EAX,CX` loads the RIGHT
// height first and `CMOVG` only replaces it when left > right, so equal heights
// yield the right one — indistinguishable (they are equal) and kept so the
// transcription matches.
// ---------------------------------------------------------------------------
inline void Recompute(Node* n) {
    const int16_t l = n->left  ? static_cast<int16_t>(n->left->height  + 1) : 0;
    const int16_t r = n->right ? static_cast<int16_t>(n->right->height + 1) : 0;
    n->balance = static_cast<int16_t>(r - l);
    n->height  = (l > r) ? l : r;
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x180023570 — rotate LEFT about `n`. Returns the new subtree root (RAX=R8).
//
// Retail's order, preserved:
//
//   if (n == set->root) set->root = n->right;      0x180023570 (`CMP RDX,[RCX]`)
//   p = n->parent; if (p) (p->left == n ? p->left : p->right) = n->right;
//   r = n->right;                                  0x180023599
//   r->parent = n->parent;                         0x1800235a1
//   n->right  = r->left;                           0x1800235a9
//   if (n->right) n->right->parent = n;            0x1800235b2
//   r->left   = n;                                 0x1800235b6
//   n->parent = r;                                 0x1800235ba
//   if (r->left)  Recompute(r->left);              0x1800235be, guarded
//   if (r->right) Recompute(r->right);             0x18002360e, guarded
//   Recompute(r);                                  0x18002365c, UNGUARDED
//
// The last Recompute is unguarded because `r` cannot be null on this path.
// Kept as written.
//
// NOTE the root test reads `[RCX]` — RCX is the SET, not the node — and RCX is
// then REUSED for the parent at 0x18002357c, destroying it. Faithful; the set
// pointer is not needed again.
// ---------------------------------------------------------------------------
Node* RotateLeft(Set* set, Node* n) {
    if (n == set->root) set->root = n->right;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->right;
        else              p->right = n->right;
    }

    Node* r   = n->right;
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
// 0x1800236a0 — rotate RIGHT about `n`. The exact mirror. Transcribed
// independently from 0x1800236a0 (not derived from RotateLeft) and then diffed
// against it: the two disassemblies differ in exactly the six link offsets
// (+0x20 <-> +0x28) and nowhere else.
// ---------------------------------------------------------------------------
Node* RotateRight(Set* set, Node* n) {
    if (n == set->root) set->root = n->left;

    Node* p = n->parent;
    if (p) {
        if (p->left == n) p->left  = n->left;
        else              p->right = n->left;
    }

    Node* l   = n->left;
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
// 0x180023170 — the retrace. Walks from `*slot` to the root, recomputing
// balances and rotating where the invariant broke.
//
// *** THE SECOND PARAMETER IS A SLOT, NOT A NODE, AND THAT IS LOAD-BEARING. ***
// Retail re-reads `*RDI` at the top of every iteration (0x180023190) and again
// after each rotation (0x180023202, 0x18002320d, 0x180023236), because a
// rotation can move a different node into the position being examined. Passing
// the node by value and caching it is a different algorithm that happens to
// agree on balanced inputs. `KeyMap::RegisterInput` (0x1800a7f56:
// `LEA RDX,[RSP+0x50]`) hands it the address of a real stack local for the
// same reason, and input.cpp spells it that way; set_core.cpp's
// PortedRetrace24 is its other caller.
//
// The rebalance predicates are transcribed from the FLAGS, not from the obvious
// form:
//
//   0x1800231dd  CMP CX,1 / JLE skip            -> enter when balance > 1
//   0x1800231f0  TEST CX,CX / JG take / JNS take -> pre-rotate ONLY when the
//                                                   RIGHT child's balance < 0
//   0x180023210  CMP word [RDX+0x30],-1 / JGE    -> enter when balance < -1
//   0x180023224  TEST CX,CX / JS / JLE           -> pre-rotate ONLY when the
//                                                   LEFT child's balance > 0
//
// A null child skips the rotation ENTIRELY, not just the pre-rotation
// (`TEST RAX,RAX / JZ` at 0x1800231ea and 0x18002321e jump past BOTH calls).
// That is the difference between "unbalanced with a null child" being a no-op
// and being a crash, and it is retail's.
//
// One transcription note where E24 and E16 differ in spelling but not in
// behaviour: retail here tests the balance it JUST computed (`CMP CX,1` with CX
// still live) instead of re-reading `*slot`, but RDX and `*slot` are provably
// the same node at that instruction, so the two are the same test.
// ---------------------------------------------------------------------------
void Retrace(Set* set, Node** slot) {
    if (*slot == nullptr) return;          // 0x18002317a, before the loop

    for (;;) {
        Node* n      = *slot;              // 0x180023190
        Node* parent = n->parent;          // 0x180023196, captured BEFORE any rotation

        Recompute(n);

        Node* cur = n;
        if (cur->balance > 1) {
            Node* r = cur->right;          // 0x1800231e3
            if (r) {
                if (r->balance < 0) {
                    RotateRight(set, r);   // 0x1800231fd
                    cur = *slot;           // 0x180023202
                }
                RotateLeft(set, cur);      // 0x180023208
            }
        }

        cur = *slot;                       // 0x18002320d
        if (cur->balance < -1) {
            Node* l = cur->left;           // 0x180023217
            if (l) {
                if (l->balance > 0) {
                    RotateLeft(set, l);    // 0x180023231
                    cur = *slot;           // 0x180023236
                }
                RotateRight(set, cur);     // 0x18002323c
            }
        }

        *slot = parent;                    // 0x180023241
        if (!parent) return;
    }
}

// ---------------------------------------------------------------------------
// 0x180023130 — destroy `n`'s subtree and free `n`. 18 instructions:
//
//   if (!n) return;                                   0x180023130
//   DestroySubtree(set, n->left);                     0x180023142/149
//   DestroySubtree(set, n->right);                    0x18002314e/155
//   SetNodeFree(set, n);                              0x18002315d
//
// The recursion passes RCX (the set) through unchanged — `MOV RDI,RCX` then
// `MOV RCX,RDI` before each recursive call — and the free call leaves RCX as
// the set too, which is the two-argument 0x180009c30 contract (first argument
// IGNORED).
//
// *** NO OURS-VS-THEIRS ORACLE EXISTS FOR THIS BODY, and that is stated rather
// than skipped. *** Both implementations FREE the nodes, so there is nothing
// left to compare afterwards. What is checkable instead is that the walk
// REACHES EVERY NODE EXACTLY ONCE: `g_destroyVisits` counts the visits, and
// RtInput_KeyMap_DeletingDtor (src/engine/input/keymap_save.cpp) compares the
// delta against the binding count minus the root it frees itself.
// ---------------------------------------------------------------------------
void DestroySubtree(Set* set, Node* n) {
    if (!n) return;
    ++g_destroyVisits;
    DestroySubtree(set, n->left);
    DestroySubtree(set, n->right);
    SetNodeFree(set, n);
}

// ---------------------------------------------------------------------------
// 0x180023060 — allocate a node, copy the 0x18-byte element, link the parent.
//
// *** THE FIRST PARAMETER IS NEVER READ. *** This is the function
// `reference_read_the_callee_before_holding_back_on_an_arg.md` was written
// about: `0x180023097 MOV RCX,RBX` destroys RCX with the allocator critical
// section before anything can read it, and the call sites agree in the stronger
// form — RCX is `this` at 0x1800a7ee6 and a zero-extended id byte at
// 0x1800a7f3a/0x1800a7f4d. A value that differs like that across call sites
// cannot be a parameter the callee reads.
//
// THE ELEMENT COPY IS THREE QWORDS (0x1800230cb..0x1800230dd), not a field
// copy. `Binding`'s `_pad02` and `_pad14` are therefore whatever the caller's
// stack held — `RegisterInput` does not write them (input.cpp says so at its
// element build) — so **the padding carries bytes of the caller's stack frame
// into the tree**. Reproduced, not repaired: an oracle comparing those bytes
// would fail ~100% of calls
// (reference_oracle_must_not_compare_unwritten_bytes.md), and zeroing them
// would differ from retail in a way nothing here would report.
//
// On allocation failure retail returns null and the CALLER stores that null as
// the child link — it does not check (0x1800230c7 `XOR EAX,EAX`). Faithful.
//
// The tail zeroes `left`, `right` and ONE DWORD at +0x30, which covers both
// 16-bit `balance` and `height` in a single store (0x1800230ef). `pad34` is NOT
// written.
// ---------------------------------------------------------------------------
Node* AllocNode(void* /*unused, never read*/, const void* src, Node* parent) {
    Node* n = static_cast<Node*>(MemAlloc(0x38, 8));
    if (!n) return nullptr;

    const uint64_t* s = static_cast<const uint64_t*>(src);  // the erased core copies the 0x18-byte element as three qwords  // CAST-OK: elem_stride
    n->element[0] = s[0];   // 0x1800230cb — element +0x00..+0x07
    n->element[1] = s[1];   // 0x1800230d1 — element +0x08..+0x0f
    n->element[2] = s[2];   // 0x1800230d9 — element +0x10..+0x17

    n->parent  = parent;    // 0x1800230e1 — RBP, the third argument
    n->left    = nullptr;   // 0x1800230e7
    n->right   = nullptr;   // 0x1800230eb
    n->balance = 0;         // 0x1800230ef, one DWORD covering both words
    n->height  = 0;
    return n;
}

// ---------------------------------------------------------------------------
// The 0x180009c30 boundary.
//
// TWO PARAMETERS, the first IGNORED by the callee. 0x180009c30's own body is
// `rtalloc::FreeLockedIgnoringSelf` (src/engine/system/allocator.cpp). See
// set_avl.cpp's "THE FIFTH HELPER" banner.
// ---------------------------------------------------------------------------
// Through `System::MemoryAllocator::MemFree`, the same free 0x180009c30
// performs. set_avl.cpp's copy of this boundary carries the full argument.
void SetNodeFree(void* ignored, void* node) {
    (void)ignored;
    System::MemoryAllocator::MemFree(node);
}

}  // namespace rtsetavl24
