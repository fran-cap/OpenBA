// src/game/Work/marketwork_bst_a9f0.cpp
// Wave (MarketWork BST) — the insert cluster 0x14002a9f0 -> 0x14002ab60 -> 0x14002ae50.
//
// These three functions are one dependency chain and implement ORDERED INSERT-IF-ABSENT
// into MarketWork's self-balancing (AVL) binary search tree:
//
//   0x14002a9f0  wraps a caller-supplied key + payload array into a temporary
//                {key, clonedArray} pair on the stack and hands it to 0x14002ab60,
//                then releases the temporary clone.
//   0x14002ab60  walks the tree comparing the UNSIGNED 64-bit key, returns the existing
//                node if the key is already present (without bumping the count), otherwise
//                links a fresh node under the node it stopped at, runs the AVL retrace
//                (0x140025920 -> the "PlayerTrade_Rot_*" rotations at 0x140014ed0 /
//                0x140015000), and increments the tree's element count.
//   0x14002ae50  allocates and initialises one 0x38-byte node, deep-copying the payload
//                array with 0x14000d9d0.
//
// The node's +0x30 int32 (zeroed at creation, never touched here) is the AVL balance
// factor — the retrace helper owns it.  Together with the two rotation callees reached
// via 0x140025920 that confirms this really is a height-balanced tree, not a plain BST.
//
// Node layout (0x38) — verified against the disassembly, byte offsets, NOT decompiler
// element indices (see trap #1 below):
//   +0x00 key (InternalBasePtr)   +0x08/+0x10 payload array {ptr, count:int32, cap:int32}
//   +0x18 parent   +0x20 left   +0x28 right   +0x30 balance (int32)
//
// Tree header: +0x00 root, +0x08 count (int32).
//
// ---------------------------------------------------------------------------
// GHIDRA MISNOMERS / DECOMPILER-vs-DISASSEMBLY DISAGREEMENTS found while porting
// ---------------------------------------------------------------------------
// * Ghidra's decompiler renders `puVar1[4]` / `puVar1[5]` on a `ulonglong*`, i.e. BYTE
//   offsets 0x20 / 0x28 — left / right child.  Taking those as byte offsets 4 / 5 would
//   silently read the middle of the key and of the payload pointer.  Every field access
//   below was re-derived from the raw `MOV ... [RDI + 0x20]` / `[RDI + 0x28]` forms.
// * The decompiler mangles 0x14002ab60's control flow into a `do { while (...) } while`
//   with a bogus `pIVar5` alias, and it emits two DIFFERENT first arguments for the two
//   0x14002ae50 call sites (`puVar4` vs `pIVar5`).  That is the tell that 0x14002ae50's
//   RCX parameter is DEAD: the disassembly shows RCX still holding whatever the previous
//   CheckPtr call site left there (the node on the left-insert path, the key pointer on
//   the right-insert path) — the compiler knew nobody reads it.  0x14002ae50 confirms
//   this: it spills RCX to its home slot at +0x8 and never loads it back.
// * Ghidra types 0x14002ab60/0x14002a9f0 as returning `undefined8*` and it is right —
//   both really do return their RDX out-param in RAX (`MOV RAX,RSI` @0x14002ac2f,
//   `MOV RAX,RDI` @0x14002aa68).  Callers that ignore it are fine, but the signature
//   must keep it (trap #4).
//
// ---------------------------------------------------------------------------
// Runtime primitives
// ---------------------------------------------------------------------------
// Retail reaches these through .rdata dispatch slots; B3R calls them by name (the
// REDZONE / ALLOC_STATS instruments sit on the allocator's own hook since W0-A0):
//   [0x140152b60] System::MemoryAllocator::MemAlloc (static 2-arg: size, align)
//   [0x140152b68] System::MemoryAllocator::MemFree  (static 1-arg)
//   0x14000d9d0   ActRm_VectorClone_d9d0     (Action/exe_bodies.h)
//   0x140025920   Avl38Retrace               (Player/exe_bodies.h)

#include "Work/marketwork_bst_a9f0.h"
#include "Action/exe_bodies.h"
#include "Player/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include <cstdint>
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete

// ============================================================================
// 0x14002ae50 — create one market BST node
// ============================================================================
// __fastcall Node* MarketWork_BstNewNode(void* unusedRCX, Claim* src [RDX], Node* parent [R8])
//
// Disassembly walk (0x14002ae50 - 0x14002aed3):
//   ae50  MOV [RSP+8],RCX            ; RCX homed and NEVER read again -> dead parameter
//   ae6a  MOV RSI,R8                 ; parent
//   ae6d  MOV RDI,RDX                ; src
//   ae70  MOV EDX,8 / LEA ECX,[RDX+0x30]  ; == MemAlloc(size=0x38, align=8)
//   ae78  CALL [0x140152b60]
//   ae84  JZ  aec7                   ; alloc failure -> return NULL, nothing initialised
//   ae92  MOV [RAX],RBP              ; node+0x00 = 0            (RBP == 0 from XOR EBP,EBP)
//   ae95  MOV RAX,[RDI] / ae98 MOV [RBX],RAX ; node+0x00 = src+0x00 (the 8-byte key)
//   ae9b  LEA RCX,[RBX+8]
//   aea4  MOV [RCX],RBP              ; node+0x08 = 0   (array ptr)
//   aea7  MOV [RCX+8],RBP            ; node+0x10 = 0   (count+cap, ONE 8-byte store — trap #2)
//   aeab  LEA RDX,[RDI+8] / aeaf CALL 0x14000d9d0  ; deep-copy src's array into node+0x08
//   aeb5  MOV [RBX+0x18],RSI         ; node+0x18 = parent
//   aeb9  MOV [RBX+0x20],RBP         ; node+0x20 = 0   (left)
//   aebd  MOV [RBX+0x28],RBP         ; node+0x28 = 0   (right)
//   aec1  MOV dword [RBX+0x30],EBP   ; node+0x30 = 0   (AVL balance + height, one dword)
//   aec4  MOV RAX,RBX                ; return node
//
// The redundant `node+0x00 = 0` immediately followed by `node+0x00 = key` is the inlined
// InternalBasePtr default-ctor followed by the copy-assign; kept verbatim.
extern "C" MarketWork::ClaimNode* MarketBst_ae50(MarketWork::ClaimTree* unused, MarketWork::Claim* src,
                                                 MarketWork::ClaimNode* parent) {
    (void)unused;   // homed at [RSP+8] by retail and never loaded — genuinely dead.

    MarketWork::ClaimNode* n = static_cast<MarketWork::ClaimNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (n == nullptr) return nullptr;   // faithful: retail's JZ skips ALL initialisation

    n->element.citizen = nullptr;                     // ae92
    n->element.citizen = src->citizen;                // ae95/ae98

    n->element.carries.data = nullptr;                // aea4 — array ptr
    n->element.carries.count = 0;                     // aea7 — count(+0x10) AND cap(+0x14):
    n->element.carries.capacity = 0;                  // one 8-byte store in retail

    // 0x14000d9d0 — deep-copy the 16-byte-element dynamic array from src+0x08.
    MaterialArrayAssign(&n->element.carries, &src->carries);

    n->parent  = parent;    // aeb5
    n->left    = nullptr;   // aeb9 — left
    n->right   = nullptr;   // aebd — right
    n->balance = 0;         // aec1 — AVL balance and height (one dword)
    n->height  = 0;

    return n;
}

// ============================================================================
// 0x14002ab60 — ordered insert-if-absent + AVL retrace
// ============================================================================
// __fastcall Node** MarketWork_BstInsert(Set* tree [RCX], Node** out [RDX], Claim* src [R8])
//
// Disassembly walk (0x14002ab60 - 0x14002ac3d):
//   ab75  MOV RDI,[RCX]              ; cur = tree->root
//   ab81  TEST RDI,RDI / JNZ ab9c
//   -- empty tree --
//   ab86  XOR R8D,R8D; MOV RDX,RBX; CALL 0x14002ae50   ; parent = NULL
//         (RCX is left holding `tree` here purely because it was never clobbered —
//          more evidence that ae50's first parameter is dead)
//   ab91  MOV [R14],RAX              ; tree->root = node
//   ab94  MOV [RSI],RAX              ; *out = node
//   ab97  JMP ac21                   ; -> count++
//   -- non-empty: descend --
//   ab9c/abcf/abe2  MOV [RSP+0x30],cur          ; the retrace scratch slot, refreshed
//                                               ; at every descent -> holds the PARENT
//                                               ; of the node we are about to link
//   aba1  MOV RCX,cur / CALL [0x140152ab0]      ; CheckPtr(&cur->key)
//   abaa  MOV RAX,[RDI]                         ; cur->key
//   abad  CMP [RBX],RAX / abb0 JC abd6          ; UNSIGNED: src.key < cur->key -> go left
//   abb2  MOV RCX,RBX  / CALL [0x140152ab0]     ; CheckPtr(&src->key)
//   abbb  MOV RAX,[RBX] / abbe CMP [RDI],RAX
//   abc1  JNC abfa                              ; cur->key >= src.key, and we already know
//                                               ; src.key >= cur->key  => EQUAL: found
//   abc3  MOV RAX,[RDI+0x28]                    ; right child
//   abca  JZ abe9 -> link new node at +0x28
//   abd6  MOV RAX,[RDI+0x20]                    ; left child
//   abdd  JZ abff -> link new node at +0x20
//   abfa  MOV [RSI],RDI / JMP ac25              ; found: *out = cur, NO count++, return out
//   ac0e  LEA RDX,[RSP+0x30]; MOV RCX,R14; CALL 0x140025920   ; AVL retrace from the parent
//   ac1e  MOV [RSI],RBX              ; *out = the NEW node (RBX saved from ae50's RAX)
//   ac21  INC dword [R14+8]          ; tree->count++  (32-bit)
//   ac2f  MOV RAX,RSI                ; return out
//
// Note both CheckPtr calls are unconditional in retail and are made on the tree node
// itself (its first 8 bytes ARE the InternalBasePtr) and on the source key.  CheckPtr
// is a bare RET (object/Internal.h) and the port does not call it.
// (`tree` is MarketWork's ClaimTree header {root +0x00, count +0x08}.)
extern "C" MarketWork::ClaimNode** MarketBst_ab60(MarketWork::ClaimTree* tree,
                                                  MarketWork::ClaimNode** out,
                                                  MarketWork::Claim* src) {

    MarketWork::ClaimNode* cur = tree->root;   // tree->root

    if (cur == nullptr) {
        MarketWork::ClaimNode* node = MarketBst_ae50(tree, src, nullptr);   // ab86: parent = NULL
        tree->root = node;                                      // ab91
        *out  = node;                                      // ab94
        tree->count += 1;        // ac21
        return out;
    }

    // `retraceSlot` is retail's [RSP+0x30]: a stack local holding the node we last
    // descended to.  0x140025920 takes its ADDRESS (and may write through it), so it
    // has to be a real local, not a value.
    MarketWork::ClaimNode* retraceSlot = cur;              // ab9c

    MarketWork::ClaimNode* newNode = nullptr;

    for (;;) {
        retraceSlot = cur;

        CitizenComponent* curKey = cur->element.citizen.Get();   // abaa
        CitizenComponent* srcKey = src->citizen.Get();           // abad (CMP [RBX],RAX)

        if (srcKey < curKey) {                             // abb0 JC — UNSIGNED
            MarketWork::ClaimNode* left = cur->left;       // abd6
            if (left == nullptr) {
                newNode = MarketBst_ae50(tree, src, cur);
                cur->left = newNode;                       // ac0a
                break;
            }
            cur = left;
            continue;
        }

        srcKey = src->citizen.Get();                             // abbb — re-read AFTER
                                                           // retail's CheckPtr
                                                           // (a bare RET)
        curKey = cur->element.citizen.Get();                     // abbe (CMP [RDI],RAX)

        if (curKey >= srcKey) {                            // abc1 JNC — key present
            *out = cur;                                    // abfa
            return out;                                    // no retrace, no count++
        }

        MarketWork::ClaimNode* right = cur->right;         // abc3
        if (right == nullptr) {
            newNode = MarketBst_ae50(tree, src, cur);
            cur->right = newNode;                          // abf4
            break;
        }
        cur = right;
    }

    // ac0e — AVL retrace/rebalance starting from the PARENT (retraceSlot), not the new
    // node. 0x140025920 is the shared AVL retrace (Avl38Retrace, Player/exe_bodies.h).
    Avl38Retrace(&tree->root, &retraceSlot);   // 0x140025920

    *out = newNode;                                        // ac1e
    tree->count += 1;            // ac21
    return out;                                            // ac2f
}

// ============================================================================
// 0x14002a9f0 — insert (key, cloned payload array)
// ============================================================================
// __fastcall Node** MarketWork_BstInsertWithData(Set* tree [RCX], Node** out [RDX],
//                                                InternalPtr* key [R8], Array* vec [R9])
//
// Ghidra's `MarketWork_sub_a9f0(longlong*, undefined8*, undefined8*, longlong*)` is the
// correct arity.
//
// Stack frame (R11 == RSP+0x48 after `MOV R11,RSP; PUSH RDI; SUB RSP,0x40`):
//   [R11-0x20] == [RSP+0x28]  tmp.key
//   [R11-0x18] == [RSP+0x30]  tmp.array.ptr
//   [R11-0x10] == [RSP+0x38]  tmp.array.count/cap
// i.e. a temporary Claim {key, array} laid out EXACTLY like a node's first 0x18 bytes,
// which is why it can be handed straight to 0x14002ab60 -> 0x14002ae50.
//
//   aa10  MOV [R11-0x20],RSI        ; tmp.key = 0
//   aa14  MOV RAX,[R8] / aa17 MOV [R11-0x20],RAX   ; tmp.key = *key  (8 bytes, no IBP ctor)
//   aa23  MOV [R11-0x18],RSI        ; tmp.array.ptr = 0
//   aa27  MOV [R11-0x10],RSI        ; tmp.array.count/cap = 0
//   aa2b  MOV RDX,R9 / aa2e LEA RCX,[R11-0x18] / aa32 CALL 0x14000d9d0
//                                   ; tmp.array <- deep copy of *vec
//   aa38  LEA R8,[RSP+0x28] / aa40 MOV RCX,RBX / aa3d MOV RDX,RDI
//   aa43  CALL 0x14002ab60          ; insert (&tmp)
//   aa53  MOV RCX,[RSP+0x30] / aa58 CALL [0x140152b68]   ; MemFree(tmp.array.ptr)
//   aa5e/aa63  MOV [RSP+0x30],RSI / MOV [RSP+0x38],RSI   ; zero the dead temp (inlined dtor)
//   aa68  MOV RAX,RDI               ; return out
//
// The payload is therefore cloned TWICE: once from *vec into the stack temp, and again
// by 0x14002ae50 from the temp into the node — after which the temp's buffer is freed.
// Wasteful, and faithful; do not "optimise" it away, the temp's buffer is a distinct
// allocation from the node's and freeing it here is what keeps the node's copy valid.
extern "C" MarketWork::ClaimNode** MarketBst_a9f0(MarketWork::ClaimTree* tree,
                                                  MarketWork::ClaimNode** out,
                                                  CitizenComponent** key,
                                                  MarketWork::CarryList* vec) {

    MarketWork::Claim tmp;
    tmp.citizen = nullptr;                          // aa10
    tmp.citizen = *key;                             // aa14/aa17 — raw 8-byte copy
    tmp.carries.data = nullptr;                     // aa23
    tmp.carries.count = 0;                          // aa27 — one 8-byte zero covers count
    tmp.carries.capacity = 0;                       //        AND cap in retail

    MaterialArrayAssign(&tmp.carries, vec); // aa32

    MarketBst_ab60(tree, out, &tmp);                // aa43 (return value discarded here)

    System::MemoryAllocator::MemFree(tmp.carries.data);  // aa58 — free the temp buffer
    tmp.carries.data = nullptr;                     // aa5e
    tmp.carries.count = 0;                          // aa63
    tmp.carries.capacity = 0;

    return out;                                     // aa68
}
