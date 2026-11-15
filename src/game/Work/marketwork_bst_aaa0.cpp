// src/game/Work/marketwork_bst_aaa0.cpp
// Wave (MarketWork BST) -- the ERASE cluster 0x14002aaa0 -> 0x14002ac40 -> 0x14002aee0.
//
// These three functions are one dependency chain and implement ERASE from MarketWork's
// self-balancing (AVL) binary search tree.  They are the mirror image of the insert
// cluster ported in marketwork_bst_a9f0.cpp.
//
//   0x14002aaa0  erase-BY-KEY  : unsigned-64 search from the root; on an exact key match
//                                hands the node to 0x14002ac40.  Silent no-op if absent.
//   0x14002ac40  erase-BY-NODE : decrement the count, unlink the node (three classic BST
//                                cases), destroy it, then AVL-retrace from the lowest
//                                node whose subtree changed shape (0x140025920 -> the
//                                "PlayerTrade_Rot_*" rotations 0x140014ed0 / 0x140015000).
//   0x14002aee0  destroy-node  : free the node's payload array, zero it, free the node.
//
// ---------------------------------------------------------------------------
// ROLE CORRECTION -- 0x14002aaa0 IS NOT AN INSERT
// ---------------------------------------------------------------------------
// The wave brief (and the shape of its call sites) suggested "BST insert".  It is not.
// The function allocates nothing, calls no allocator, never writes a child pointer, and
// its single non-CheckPtr callee is 0x14002ac40, which frees memory and decrements the
// element count.  It is erase(key).  Both of its callers are one-line vtable thunks that
// forward the SAME tree object:
//   0x1400eff50 (Ghidra "MarketWork_OnCreate", MarketWork vtable Slot7)
//        -> MarketWork_sub_aaa0(this + 0x48, keyIBP); returns void
//   0x1400eff60 (Ghidra "MarketWork_Draw",     MarketWork vtable Slot6)
//        -> MarketWork_sub_aaa0(this + 0x48, keyIBP); returns 0 (undefined1 -> bool false)
// So Slot6 and Slot7 do the same work and differ only in the return value -- the classic
// Work "CancelWork(entity)" / "RemoveWork(entity) -> bool" pair.  Naming either of them
// "insert" would be wrong, and Ghidra's undefined1
// return on 0x1400eff60 is a REAL return value, not void (trap #3).
//
// ---------------------------------------------------------------------------
// Node layout (0x38) -- every offset below re-derived from the DISASSEMBLY
// ---------------------------------------------------------------------------
//   +0x00 key (InternalBasePtr, compared UNSIGNED)
//   +0x08 payload array ptr        +0x10 count:int32 / cap:int32  (zeroed as one 8-byte 0)
//   +0x18 parent   +0x20 left   +0x28 right
//   +0x30 balance:int16            +0x32 height:int16
// Tree header (MarketWork+0x48):   +0x00 root (8 bytes)   +0x08 count (int32)
//
// The int16 balance/height pair is proved by 0x14002ac40's own rebalance fixup:
//   adb9  MOV word ptr [RBX + 0x32],AX     ; height  = max(childH_left, childH_right)
//   adc1  MOV word ptr [RBX + 0x30],R8W    ; balance = rightH - leftH
// (marketwork_bst_a9f0.cpp calls +0x30 an int32 balance; the insert path only ever ZEROES
// that word pair, so both descriptions agree on the bytes it writes.  For erase the
// distinction matters: a 4-byte store to +0x30 here would clobber the height.)
//
// ---------------------------------------------------------------------------
// GHIDRA DECOMPILER-vs-DISASSEMBLY DISAGREEMENTS
// ---------------------------------------------------------------------------
// * Trap #1 (elements vs bytes).  The decompiler renders 0x14002aaa0's children as
//   self[4] / self[5] on a ulonglong* -- BYTE offsets 0x20 / 0x28.  Likewise
//   0x14002ac40's param_2[3]/[4]/[5] are parent/left/right at 0x18/0x20/0x28.  Reading
//   those as byte offsets 3/4/5 would land inside the key and the payload pointer.  Every
//   access below matches a literal MOV ... [reg + 0x18/0x20/0x28] in the listing.
// * The decompiler's tail for 0x14002ac40 is wrong-looking but harmless: it hoists a
//   single "if (lVar2 == 0)" out of three different predecessors, and lVar2 aliases a
//   different register on each path (it is uninitialised on one of them as written).  The
//   disassembly is unambiguous -- each path does its own TEST on its own register and
//   CMOVZ/JZ picks between the two stack slots [RSP+0x50] and [RSP+0x58].  Ported as an
//   explicit per-path useB flag.
// * Ghidra shows 0x14002aee0 as (undefined8 param_1, void *param_2) and never uses
//   param_1.  That is correct and deliberate: aee5 homes RDX, and the RCX home slot is
//   never loaded back.  RCX is a DEAD parameter (the callee is a node destructor that
//   ignores its "this").  Kept in the signature, typed as the owning tree.
// * 0x14002ac40's "right == 0" path does NOT call 0x14002aee0 -- the compiler INLINED the
//   destructor there (acbc-acd1).  Same stores, same order; shared here as DestroyNodeBody.
//
// ---------------------------------------------------------------------------
// Cross-check
// ---------------------------------------------------------------------------
// 0x14002ac40 is the twin of 0x140025740 (Avl38Remove, ported in
// src/game/Player/tool_base_trade.cpp), which does the identical unlink dance on the
// same 0x38-byte node shape.  The only structural difference is the payload free at +0x08.
// The two ports were derived independently and agree instruction-for-instruction on the
// unlink/rebalance logic, which is the strongest available check on this one.
//
// ---------------------------------------------------------------------------
// Runtime primitives (retail reaches them through .rdata dispatch slots)
// ---------------------------------------------------------------------------
//   [0x140152b68] System::MemoryAllocator::MemFree  (static 1-arg)
// B3R: called by name. The REDZONE / ALLOC_STATS instruments sit on the allocator's
// own hook inside MemAlloc / MemFree (W0-A0), so a direct call is instrumented.
// 0x140025920 (retrace) is the shared Avl38Retrace template, called by name.

#include "Work/marketwork_bst_aaa0.h"
#include "Player/exe_bodies.h"          // Avl38Retrace (0x140025920)
#include "system/MemoryAllocator.h"    // B3R: slot 0x140152b68 = System::MemoryAllocator::MemFree
#include <cstdint>
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete

namespace {

using ClaimNode = MarketWork::ClaimNode;

// height-of-child: null -> 0, else child.height + 1
// (ad87 MOVZX R9D,[RAX+0x32] / INC R9W, with the null arm taking EDX, which is 0 there.)
inline int16_t ChildH(ClaimNode* child) {
    return (child == nullptr) ? (int16_t)0 : (int16_t)(child->height + 1);
}

// The body of 0x14002aee0, minus its null test.  Retail emits these exact four steps
// twice: once inlined at 0x14002acbc-acd1, once as the standalone callee.
//   free(node->payload); node->payload = 0; node->count/cap = 0; free(node);
// The two zero stores are 8 BYTES EACH (MOV qword ptr [RBX],RDX / [RBX+8],RDX with
// RBX = node+8): here the three named fields, in address order.  The block is freed
// immediately afterwards, but the redzone allocator inspects freed blocks, so keep the
// stores faithful.
void DestroyNodeBody(ClaimNode* node) {
    System::MemoryAllocator::MemFree(node->element.carries.data);
    node->element.carries.data = nullptr;
    node->element.carries.count = 0;
    node->element.carries.capacity = 0;
    System::MemoryAllocator::MemFree(node);
}

} // namespace

// ============================================================================
// 0x14002aee0 -- destroy one market BST node
// ============================================================================
// __fastcall void MarketWork_sub_ac40_sub_aee0(void* unusedRCX, void* node [RDX])
//
//   aee0  TEST RDX,RDX / JZ af33      ; the whole body is under a null test on the NODE
//   aee5  MOV [RSP+0x10],RDX          ; RDX homed; the RCX home slot is never read back
//   af05  LEA RBX,[RDX+8] / af0e MOV RCX,[RBX] / af11 CALL [0x140152b68]  ; free payload
//   af19  MOV [RBX],RAX / af1c MOV [RBX+8],RAX                            ; zero 0x08/0x10
//   af20  MOV RCX,RDI / af23 CALL [0x140152b68]                           ; free node
//
// The null test is RETAIL's, not a defensive addition -- 0x14002ac40 only ever reaches
// this with a value it has already proven non-null, so retail is being generous, not us.
extern "C" void MarketBst_aee0(MarketWork::ClaimTree* /*unusedTree*/, MarketWork::ClaimNode* node) {
    if (node == nullptr) return;
    DestroyNodeBody(node);
}

// ============================================================================
// 0x14002ac40 -- erase one KNOWN node from the tree, then AVL-retrace
// ============================================================================
// __fastcall void MarketWork_sub_ac40(Set* tree [RCX], Node* node [RDX])
//
// tree points at the Set header {root, count:int32} living at MarketWork+0x48 (the
// root field; the count is the int32 8 bytes on).
//
// Two stack slots carry the retrace start point; the tail picks one of them BY ADDRESS
// because 0x140025920 takes a pointer to a cursor it walks upward (and writes through):
//   [RSP+0x50] "cursorA" -- the parent of whatever left the tree
//   [RSP+0x58] "cursorB" -- the node that took the erased node's place
extern "C" void MarketBst_ac40(MarketWork::ClaimTree* tree, MarketWork::ClaimNode* z) {
    ClaimNode* cursorA = z->parent;    // ac61: MOV RAX,[RDX+0x18] -> [RSP+0x50]
    ClaimNode* cursorB = nullptr;      // [RSP+0x58]
    bool    useB    = false;

    // ac6a: DEC dword ptr [RCX+8].  The count really is a 32-bit field sharing the
    // header's second 8-byte slot -- this is a 4-byte RMW in retail, NOT a narrowed
    // 8-byte store (trap #2 cuts both ways: widening it would smash the upper half).
    tree->count -= 1;

    ClaimNode* const right = z->right;
    if (right == nullptr) {
        // ---- Case 1: no right child -- splice the left child into z's place --------
        ClaimNode* const parent = z->parent;
        ClaimNode* const leftAtSplice = z->left;             // ac7a
        if (parent == nullptr) {
            tree->root = leftAtSplice;                            // ac95: MOV [RSI],RAX
        } else if (parent->left == z) {
            parent->left = leftAtSplice;                     // ac89
        } else {
            parent->right = leftAtSplice;                    // ac8f
        }
        // ac98 re-reads z->left after the parent fixup (same value; kept literal).
        ClaimNode* const left = z->left;
        cursorB = left;
        if (left != nullptr) left->parent = z->parent;       // acaa
        DestroyNodeBody(z);                                  // inlined 0x14002aee0
        useB = (left != nullptr);                            // acd7: TEST RBP,RBP
    } else {
        ClaimNode* const left = z->left;
        if (left == nullptr) {
            // ---- Case 2: right child only -- splice it in -------------------------
            ClaimNode* const parent = z->parent;             // acfc
            if (parent == nullptr) {
                tree->root = right;                               // ad17
            } else if (parent->left == z) {
                parent->left = right;                        // ad0b
            } else {
                parent->right = right;                       // ad11
            }
            ClaimNode* const r = z->right;                   // ad1a (re-read, same value)
            cursorB = r;
            if (r != nullptr) r->parent = z->parent;         // ad2c
            // ad30: CALL 0x14002aee0 with RCX still holding `right` -- a dead argument,
            // passed here only to keep the call ABI-identical.
            MarketBst_aee0(tree, z);
            useB = (r != nullptr);                           // ad35: TEST RBX,RBX
        } else {
            // ---- Case 3: two children ---------------------------------------------
            // Replacement = in-order PREDECESSOR: rightmost node of the LEFT subtree.
            // ad40: RAX=[RCX+0x28]; while (RAX) { RCX=RAX; ... }
            ClaimNode* s = left;
            for (ClaimNode* nxt = s->right; nxt != nullptr; nxt = s->right) s = nxt;

            ClaimNode* detachParent = nullptr;   // RBX -- 0 means "s WAS z->left, nothing detached"
            cursorA = nullptr;                   // ad52: [RSP+0x50] = 0

            if (s != left) {            // ad57: CMP RCX,R8
                // Lift s out of its current spot: its parent adopts s's left subtree.
                detachParent = s->parent;                          // ad5c
                ClaimNode* const sLeft = s->left;                  // ad60
                detachParent->right = sLeft;                       // ad64
                if (sLeft != nullptr) sLeft->parent = detachParent;  // ad6d
                s->left = z->left;                                 // ad71/ad75
                cursorA = detachParent;                            // ad79

                // Fix detachParent's own metrics before the retrace walks past it.
                // Order is literal: height (+0x32) first, then balance (+0x30).
                const int16_t lh = ChildH(detachParent->left);
                const int16_t rh = ChildH(detachParent->right);
                detachParent->height = (lh < rh) ? rh : lh;        // adb9, CMOVG (signed)
                detachParent->balance = (int16_t)(rh - lh);        // adc1, SUB R8W,R9W
            }

            s->right = z->right;                                   // adc6/adca
            if (s->left  != nullptr) s->left->parent  = s;         // add7
            if (s->right != nullptr) s->right->parent = s;         // ade4

            ClaimNode* const parent = z->parent;                   // ade8
            s->parent = parent;                                    // adec
            if (parent == nullptr) {
                tree->root = s;                                         // adf6
                s->parent = nullptr;                               // adf9
                cursorB = tree->root;                                   // adfd/ae00
            } else {
                cursorB = s;                                       // ae07
                if (parent->left == z) parent->left = s;           // ae12
                else                   parent->right = s;          // ae18
            }
            // ae1f: CALL 0x14002aee0 with RCX = s (dead argument again).
            // NOTE: s keeps its OWN balance/height here -- retail does not copy z's.
            // The retrace is what re-derives them.
            MarketBst_aee0(tree, z);
            useB = (detachParent == nullptr);                      // ae2e/ae31: TEST/CMOVZ
        }
    }

    // ae38: CALL 0x140025920(tree, &cursor): the shared AVL retrace (Avl38Retrace).
    Avl38Retrace(&tree->root, useB ? &cursorB : &cursorA);
}

// ============================================================================
// 0x14002aaa0 -- erase BY KEY (MarketWork vtable Slot6 / Slot7 helper)
// ============================================================================
// __fastcall void MarketWork_sub_aaa0(Set* tree [RCX], InternalPtr* key [RDX])
//
// Plain unbalanced descent -- all balancing happens inside 0x14002ac40.  Keys compare
// UNSIGNED: aacf is JC (below) and aae0 is JNC (above-or-equal), not JL/JGE.
//
//   aac0  MOV RCX,RBX / CALL [0x140152ab0]   ; CheckPtr(node)  -- node+0x00 IS the IBP
//   aac9  MOV RAX,[RBX] / aacc CMP [RDI],RAX / aacf JC aae8   ; key < node.key -> LEFT
//   aad1  MOV RCX,RDI / CALL [0x140152ab0]   ; CheckPtr(keyIBP)  (only on this arm)
//   aada  MOV RAX,[RDI] / aadd CMP [RBX],RAX / aae0 JNC ab01  ; node.key >= key -> HIT
//   aae2  MOV RBX,[RBX+0x28]                 ; else RIGHT
//
// Both CheckPtr calls sit BEFORE the read they protect.  CheckPtr is a bare RET
// (object/Internal.h), so neither call is reproduced here; both keys are read fresh at
// each compare.  The asymmetry (node checked every iteration, keyIBP only on the >= arm)
// is retail's.
//
// Reaching the hit arm means key >= node.key AND node.key >= key, i.e. equality.
extern "C" void MarketBst_aaa0(MarketWork::ClaimTree* tree, CitizenComponent** key) {

    ClaimNode* node = tree->root;
    if (node == nullptr) return;

    do {
        if (*key < node->element.citizen.Get()) {
            node = node->left;
        } else {
            if (node->element.citizen.Get() >= *key) {
                MarketBst_ac40(tree, node);   // ab07
                return;
            }
            node = node->right;
        }
    } while (node != nullptr);
}
