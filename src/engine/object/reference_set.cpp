// ============================================================================
// src/engine/object/reference_set.cpp — Wave 11 follow-up 3, lane AVL
//   `Collection::Set<ReferenceEntry>` — the insert side of the
//   `Object::ExternalFactory::_references` tree.
// ============================================================================
//
// Byte-faithful port of the disassembly at the retail address named above each
// function (Runtime-x64.dll, image base 0x180000000, read 2026-09-10 with
// `disassemble_function(address=)` — NOT the decompiled C, which drops
// dereferences and prints MOVSXD as a zero extension,
// reference_ghidra_c_output_vs_disassembly.md).
//
// Layout evidence, the address table against the two sibling instantiations,
// and the destroy/free shape argument: reference_set.h.
// Wave write-up: docs/history/runtime_port/WAVE11F_LANE_AVL.md.
//
// ---------------------------------------------------------------------------
// THE COMPARISON AGAINST THE TWO SIBLING FAMILIES, DONE NOT ASSUMED
// ---------------------------------------------------------------------------
// Three of the seven bodies here are the same algorithm other lanes ported at
// other addresses. That is a claim, so it was CHECKED by reading both
// disassemblies rather than asserted from the node layout, which is identical:
//
//   rotate right  0x18002dbf0 vs 0x18000b170   Same length (90 instructions),
//                     same opening 20 instructions instruction-for-instruction.
//   retrace       0x18002d4b0 vs 0x18000adc0   NOT identical. 71 instructions
//                     against 72, and the first difference is at the third
//                     instruction of the loop body: 0x18002d4b0 carries the
//                     node in RDX (`MOV RDX,RAX` @ 0x18002d4d3) where
//                     0x18000adc0 carries it in R8 (`MOV R8,RAX` @ 0x18000ade3).
//                     Nothing writes `*slot` between the two points, so the two
//                     are semantically one — the same no-difference lane COLL
//                     recorded for 0x18000adc0 vs 0x18002a310, and lane RES for
//                     0x18002d370 vs 0x180023170.
//   rotate left   0x18002dac0 vs 0x18000b040   same shape; both transcribed
//                     independently here and diffed against RotateRight below,
//                     which differs in exactly the six link offsets.
//
// **They are still transcribed separately here.** Reusing
// `rtsetavl::RotateRight` at 0x18002dbf0 would work today, but a body used
// at an address it was not read from carries no evidence of its own.
// external_set.cpp set that precedent and this file follows it.
//
// ---------------------------------------------------------------------------
// WHAT IS NOT HERE
// ---------------------------------------------------------------------------
// No `Find` and no `Erase` for this instantiation. `ExternalFactory::
// GetPathForReferenceId` (0x1800bf1d0) INLINES its own descent and is already
// ported in external.cpp; nothing in the DLL erases a single reference entry —
// the table is torn down wholesale by `0x18002ebf0` and rebuilt. Porting an
// `Erase` with no caller would be a body no oracle in this tree can reach.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include <new>          // placement new into raw node element bytes

#include "reference_set.h"
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc by name

// `??0Filename@System@@QEAA@AEBV01@@Z` (0x1800067d0) used to be forward-declared
// here as a C entry point. ENDGAME B.3.1 step 2 calls it as what it is — the
// `System::Filename` copy constructor, placement-constructed into the raw node
// bytes. Its first action is still `MOV qword ptr [RBX],0`: the ported ctor
// member-initialises `_data` to null, so no caller pre-zeroes for it.

namespace rtrefset {

using RtObject::ReferenceEntry;

ProbeCounters g_fc = {};

// ---------------------------------------------------------------------------
// THE ALLOCATOR BOUNDARY.
//
// Retail INLINES `EnterCriticalSection(alloc+0x640) / Allocate / Leave` into
// 0x18002e5d0 rather than calling the static wrapper. This port goes through
// the static door `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`
// (0x1800c7a50) instead, for the reason avl_trees.cpp and external_set.cpp both
// give at length: the composed entry point takes the same critical section, so
// the observable behaviour is identical, and going through ONE door instead of
// pinning 0x1800c6e60 plus the `+0x640` offset plus the two `[0x18014b39x]`
// import slots keeps four fragile facts out of this file.
//
// B3R RT-Core: the static is called by name.
// ---------------------------------------------------------------------------

// The refcount release idiom retail inlines at 0x18002cbda IS `~Filename`
// (0x180006980), retail's release asymmetry included. ReferenceEntry::path is
// the real System::Filename, so the release is its destructor.

// ---------------------------------------------------------------------------
// The height/balance recompute retail inlines into the retrace and both
// rotations. Written once here because all three copies are the same six
// instructions with the same registers; each call site names the address it
// stands for.
//
//   lh = left  ? left->height  + 1 : 0        16-BIT `INC R8W`, wraps at 16
//   rh = right ? right->height + 1 : 0
//   height  = max(lh, rh)                     `CMOVG` on 16-bit operands
//   balance = rh - lh                         `SUB CX,R8W`, 16-bit
//
// A leaf therefore has height 0 and balance 0. The arithmetic is 16-bit
// THROUGHOUT in retail and is kept 16-bit here; widening it would only diverge
// on a tree 32767 deep, but the point of a byte-faithful port is that nobody
// has to work out whether that matters.
// ---------------------------------------------------------------------------
static void Recompute(Node* n) {
    int16_t lh = n->left  ? static_cast<int16_t>(n->left->height  + 1) : 0;
    int16_t rh = n->right ? static_cast<int16_t>(n->right->height + 1) : 0;
    n->balance = static_cast<int16_t>(rh - lh);
    n->height  = (lh > rh) ? lh : rh;
}

// ---------------------------------------------------------------------------
// 0x18002dac0 — rotate LEFT about `n`. Returns the new subtree root.
//
//   if (n == set->root) set->root = n->right;      18002dac0 CMP RDX,[RCX]
//   p = n->parent;                                 18002dacc [RDX+0x10]
//   if (p) (p->left == n ? p->left : p->right) = n->right;
//   r = n->right;                                  18002dae9 [RDX+0x20]
//   r->parent = n->parent;                         18002daf1
//   n->right  = r->left;                           18002daf9
//   if (n->right) n->right->parent = n;            18002dafd
//   r->left   = n;                                 18002db06
//   n->parent = r;                                 18002db0a
//   Recompute(r->left); Recompute(r->right); Recompute(r);
//   return r;                                      18002dbdd MOV RAX,R8
//
// The first two Recomputes are guarded on the child being non-null
// (18002db17, 18002db65); the last is UNGUARDED because `r` cannot be null on
// this path. Both kept as written.
//
// NOTE the root test reads `[RCX]` — RCX is the SET, not the node — and RCX is
// then REUSED for the parent at 0x18002dacc, destroying it. Faithful; the set
// pointer is not needed again.
// ---------------------------------------------------------------------------
Node* RotateLeft(Set* set, Node* n) {
    ++g_fc.rotL;
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
// 0x18002dbf0 — rotate RIGHT about `n`. The exact mirror: every `+0x20`
// (right) becomes `+0x18` (left) and vice versa. Transcribed independently from
// 0x18002dbf0 and then diffed against RotateLeft above — the two disassemblies
// differ in exactly the six link offsets and nowhere else.
//
//   18002dbf0 CMP RDX,[RCX] / MOV RAX,[RDX+0x18]      (vs +0x20)
//   18002dc19 MOV R8,[RDX+0x18]                        (vs +0x20)
//   18002dc25 MOV RAX,[R8+0x20] / MOV [RDX+0x18],RAX   (vs +0x18 / +0x20)
//   18002dc36 MOV [R8+0x20],RDX                        (vs +0x18)
// ---------------------------------------------------------------------------
Node* RotateRight(Set* set, Node* n) {
    ++g_fc.rotR;
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
// 0x18002d4b0 — the retrace. Walks from `*slot` to the root, recomputing
// balances and rotating where the invariant broke.
//
// *** THE SECOND PARAMETER IS A SLOT, NOT A NODE, AND THAT IS LOAD-BEARING. ***
// Retail re-reads `*RDI` at the top of every iteration (0x18002d4d0) and again
// after each rotation (0x18002d542, 0x18002d54d, 0x18002d576), because a
// rotation can move a different node into the position being examined. Passing
// the node by value and caching it would be a different algorithm that happens
// to agree on balanced inputs. Every call site hands it the address of a real
// stack local for the same reason.
//
// The parent is captured BEFORE any rotation (0x18002d4d6 `MOV RSI,[RAX+0x10]`)
// and it is that captured value, not a re-read, that `*slot` advances to at
// 0x18002d581.
//
// The rebalance predicates are transcribed from the FLAGS, not from the obvious
// form:
//
//   0x18002d51d  CMP CX,1 / JLE skip                -> enter when balance > 1
//   0x18002d530  TEST CX,CX / JG take / JNS take    -> pre-rotate ONLY when the
//                                                      right child's balance is
//                                                      strictly < 0
//   0x18002d550  CMP word [RDX+0x28],-1 / JGE skip  -> enter when balance < -1
//   0x18002d564  TEST CX,CX / JS take / JLE take    -> pre-rotate ONLY when the
//                                                      left child's balance is
//                                                      strictly > 0
//
// A null child skips the rotation ENTIRELY, not just the pre-rotation
// (`TEST RAX,RAX / JZ` at 0x18002d527 and 0x18002d55b jumps past BOTH calls).
// That is the difference between "unbalanced with a null child" being a no-op
// and being a crash, and it is retail's.
//
// The FIRST iteration's Recompute is fused into the loop head in retail
// (0x18002d4d6..0x18002d519 computes and stores balance/height before the
// `CMP CX,1`), which is why the balance read by the first predicate is CX, the
// value just computed, rather than a reload.
// ---------------------------------------------------------------------------
void Retrace(Set* set, Node** slot) {
    if (*slot == nullptr) return;

    for (;;) {
        ++g_fc.retraceIters;
        Node* n      = *slot;
        Node* parent = n->parent;      // 0x18002d4d6, captured BEFORE any rotation

        Recompute(n);                  // 0x18002d4da..0x18002d519

        Node* cur = *slot;             // retail keeps it in RDX; re-read at 0x18002d542
        if (cur->balance > 1) {
            Node* r = cur->right;
            if (r) {
                if (r->balance < 0) {
                    ++g_fc.preRotR;
                    rtrefset::RotateRight(set, r);
                    cur = *slot;       // 0x18002d542
                }
                rtrefset::RotateLeft(set, cur);
            }
        }

        cur = *slot;                   // 0x18002d54d
        if (cur->balance < -1) {
            Node* l = cur->left;
            if (l) {
                if (l->balance > 0) {
                    ++g_fc.preRotL;
                    rtrefset::RotateLeft(set, l);
                    cur = *slot;       // 0x18002d576
                }
                rtrefset::RotateRight(set, cur);
            }
        }

        *slot = parent;                // 0x18002d581 MOV [RDI],RSI
        if (!parent) return;
    }
}

// ---------------------------------------------------------------------------
// 0x18002e570 — the node's element constructor.
//
//   node->element.id = src->id;                            18002e593 DWORD copy
//   Filename::Filename(&node->element.path, &src->path);   18002e59f -> 0x1800067d0
//   node->parent = parent;                                 18002e5a5 +0x10
//   node->left = node->right = 0;                          18002e5ab/af
//   *(uint32_t*)&node->balance = 0;                        18002e5b3 +0x28 DWORD
//   return node;                                           18002e5b6 MOV RAX,RDI
//
// THE ID COPY IS ONE DWORD, NOT A QWORD. `ReferenceEntry::_pad` at +0x04 is
// never written by this constructor, so a freshly allocated node carries
// whatever the allocator left in those four bytes. That is retail, and it is
// observable to anything that memcmps a node.
//
// THE LAST STORE IS ONE DWORD AT +0x28 AND IT ZEROES BOTH 16-BIT FIELDS.
// Written as two int16 stores here because that is what the fields are; the
// observable result is identical and a `uint32_t` alias write would need a cast
// that hides which fields it covers.
// ---------------------------------------------------------------------------
Node* PairCtor(Node* node, const ReferenceEntry* src, Node* parent) {
    ReferenceEntry* e = Elem(node);
    e->id = src->id;                                   // DWORD only; _pad untouched
    new (&e->path) System::Filename(src->path);
    node->parent  = parent;
    node->left    = nullptr;
    node->right   = nullptr;
    node->balance = 0;
    node->height  = 0;
    return node;
}

// ---------------------------------------------------------------------------
// 0x18002e5d0 — allocate a node and construct its element.
//
// Retail: take the allocator critical section, `Allocate(0x30, align 8)`,
// release, and — ON A NULL RESULT RETURN NULL WITHOUT CONSTRUCTING. That early
// out is at 0x18002e637 (`XOR EAX,EAX`) and it is kept: it is the one place in
// this family where an allocation failure does not fault.
//
// `MOV EDX,0x30 / LEA R8D,[RDX-0x28]` is size 0x30, alignment 8. Ghidra prints
// the alignment as arithmetic on the size, which it is; the value is 8.
//
// THE FIRST PARAMETER IS IGNORED. Retail leaves RCX holding whatever the caller
// had (InsertCore's `Set*`) and 0x18002e5d0 never reads it — it loads the
// allocator from the `[0x1801fc718]` singleton instead. The signature keeps the
// parameter because InsertCore passes it.
// ---------------------------------------------------------------------------
Node* AllocNode(void* /*ignored, never read*/, const ReferenceEntry* src, Node* parent) {
    Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x30, 8));   // 0x1800c7a50
    if (n == nullptr) { ++g_fc.allocFail; return nullptr; }
    return PairCtor(n, src, parent);
}

// ---------------------------------------------------------------------------
// 0x18002d180 — the insert core. Descends from the root, and on a miss
// allocates a node, links it, and retraces from ITS PARENT.
//
// THE COMPARATOR IS A SIGNED 32-BIT INTEGER COMPARE, and this is the one place
// this family genuinely differs in kind from every sibling in the tree:
//
//   18002d1ca  MOV ECX,dword ptr [R8]     ; the search key, loaded ONCE
//   18002d1d7  MOV EAX,dword ptr [RDI]    ; the node key, reloaded each step
//   18002d1d9  CMP ECX,EAX / JL  ...      ; key < nodeKey  -> LEFT  (+0x18)
//   18002d1dd  CMP EAX,ECX / JGE ...      ; nodeKey >= key -> EQUAL
//                                         ; otherwise      -> RIGHT (+0x20)
//
// `JL` / `JGE` are the SIGNED forms even though `ReferenceEntry::id` is a
// `uint32`. Reproduced as an `int32_t` compare. It cannot matter for the ids
// this tree actually holds — SerializeReferenceTable numbers them 1..N — but a
// port that quietly used `<` on unsigned would be a different function on any
// input with the top bit set, and nothing downstream would say so.
//
// THE RETRACE SLOT IS THE PARENT, NOT THE NEW NODE. `[RSP+0x30]` tracks the
// last node the descent stood on and is handed to the retrace by ADDRESS
// (0x18002d22c `LEA RDX,[RSP+0x30]`), so a rotation that replaces that node is
// observed. Passing `&newNode` would retrace from a leaf whose balance is 0 and
// terminate immediately — a defect no balanced population reveals.
//
// THE COUNT INCREMENT MOVES. On the empty-tree path it happens BEFORE the root
// store (0x18002d1ac); on the insert path it happens AFTER the retrace
// (0x18002d23c). Both kept where they are: nothing between them reads the
// count, but a future reader comparing this to the shipped bytes should not
// have to wonder.
//
// AN EQUAL KEY RETURNS THE EXISTING NODE AND INSERTS NOTHING (0x18002d218),
// and it does NOT touch the count.
//
// THE RETURN IS THE OUT SLOT, not the node: `MOV RAX,RSI` at 0x18002d1b6 and
// 0x18002d24d, where RSI is the second parameter.
// ---------------------------------------------------------------------------
Node** InsertCore(Set* set, Node** out, const ReferenceEntry* pair) {
    Node* n = set->root;

    if (n == nullptr) {
        ++g_fc.rootInsert;
        Node* fresh = AllocNode(set, pair, nullptr);
        set->count += 1;               // 0x18002d1ac, BEFORE the root store
        set->root = fresh;
        *out = fresh;
        return out;
    }

    const int32_t key = static_cast<int32_t>(pair->id);   // 0x18002d1ca, loaded once
    Node* slot = n;                                       // [RSP+0x30]
    for (;;) {
        const int32_t nodeKey = static_cast<int32_t>(Elem(n)->id);
        if (key < nodeKey) {
            ++g_fc.cmpLt;
            Node* l = n->left;
            if (l == nullptr) {
                ++g_fc.descLeft;
                Node* fresh = AllocNode(set, pair, n);
                n->left = fresh;
                rtrefset::Retrace(set, &slot);   // slot still holds `n`
                set->count += 1;                 // 0x18002d23c, AFTER the retrace
                *out = fresh;
                return out;
            }
            n = l; slot = l;
            continue;
        }
        if (nodeKey >= key) {
            ++g_fc.cmpEq;
            ++g_fc.equalHit;
            *out = n;                  // 0x18002d218 — found, nothing inserted
            return out;
        }
        ++g_fc.cmpGt;
        Node* r = n->right;
        if (r == nullptr) {
            ++g_fc.descRight;
            Node* fresh = AllocNode(set, pair, n);
            n->right = fresh;
            rtrefset::Retrace(set, &slot);
            set->count += 1;
            *out = fresh;
            return out;
        }
        n = r; slot = r;
    }
}

// ---------------------------------------------------------------------------
// 0x18002cb90 — the public insert. Builds the 0x10-byte element on ITS OWN
// STACK from the key and the value, hands that temporary to the core, and then
// RELEASES the temporary's Filename.
//
//   18002cbaa  MOV EAX,[R8] / MOV [RSP+0x28],EAX   ; the id, a DWORD
//   18002cbb1  MOV RDX,R9 / LEA RCX,[RSP+0x30]
//   18002cbb9  CALL 0x1800067d0                    ; Filename copy ctor
//   18002cbca  CALL 0x18002d180                    ; the core
//   18002cbda  <the inlined String::Release>       ; the temporary's reference
//   18002cc0b  MOV RAX,RDI                         ; return the OUT SLOT
//
// The pair is built at [RSP+0x28] with the id at +0x00 of it and the Filename
// at +0x08 — which is exactly `ReferenceEntry`, and is the WRITE-side pin of
// that struct's layout.
//
// The copy constructor takes a reference on the caller's `value`; `PairCtor`
// copies the temporary AGAIN into the node, taking a second; the release here
// drops the temporary's. Net: one reference per node, exactly as retail.
//
// **The release runs on EVERY path, including the equal-key path where nothing
// was inserted.** A "tidied" version that skipped it when the core reported a
// hit would leak one string reference per repeated id.
//
// `[RSP+0x28]`'s +0x04 (`ReferenceEntry::_pad`) is NEVER WRITTEN by retail —
// the id store is a DWORD — so the temporary carries four bytes of caller stack
// garbage into `PairCtor`, which does not copy them either. The port leaves the
// field uninitialised for the same reason.
// ---------------------------------------------------------------------------
Node** Insert(Set* set, Node** out, const uint32_t* key, const System::Filename* value) {
    // DWORD id; tmp._pad deliberately untouched; then the Filename copy ctor.
    ReferenceEntry tmp(*key, *value);

    InsertCore(set, out, &tmp);

    return out;                        // ~ReferenceEntry: the release at 0x18002cbda                        // 0x18002cc0b MOV RAX,RDI — the out slot
}

}  // namespace rtrefset
