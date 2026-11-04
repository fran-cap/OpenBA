// src/game/Work/marketwork_bst_b290.cpp
// MarketWork market-BST SERIALIZE pair — exe 0x14002b290 + 0x14002b5f0.
//
// These two are the SAVE/LOAD half of the MarketWork AVL/BST family (the insert half
// lives in marketwork_bst_a9f0.cpp, the cleanup half in work_leaf_market.cpp).
// MarketWork::Serialize (marketwork.cpp:MarketWork_Serialize_Impl) already dispatches
// to 0x14002b290 through an UNGUARDED function pointer, so until this file is
// registered that call is an EXEC-of-0 the first time a market save/load is reached.
//
// VERSION: MarketWork::Serialize constructs its IO::Serializer with version=1 and then
// branches on IO::Serializer::GetVersion:
//     version == 0 -> legacy simple-BST reader 0x1400296b0 + migration via MarketBst_a9f0
//     version != 0 -> 0x14002b290 (this file)
// So this pair implements the **v1 (current) format ONLY**. Nothing here is reachable
// for a v0 stream, and neither function contains a version test of its own.
//
// Node layout (0x38 bytes) and the on-disk record layout are documented in
// marketwork_bst_b290.h. Every offset below was read out of the disassembly, not the
// decompiler: Ghidra types the node pointer as `longlong*`/`InternalBasePtr*` and
// prints ELEMENT indices (`puVar3[4]`, `pIVar7 + 0x20`), which is the classic way this
// family gets silently mis-ported. The byte offsets confirmed from the raw
// instructions are:
//     14002b385  MOV [RBX+0x18],RDI    parent
//     14002b389  MOV [RBX+0x20],RDI    left
//     14002b38d  MOV [RBX+0x28],RDI    right
//     14002b391  MOV [RBX+0x30],EDI    balance (int32)
//     14002b36c  LEA RCX,[RBX+0x8]     payload array header
//     14002b4ad  INC dword ptr [R14+8] tree->count (int32)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/marketwork_bst_b290.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Player/exe_bodies.h"   // Avl38Retrace (0x140025920)
#include "Action/exe_bodies.h"   // ActRm_VectorClone_d9d0, ActRm_ArrayResize16_da70
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ============================================================
// External closure — every one of these is already ported + registered
// ============================================================

// 0x14000d9d0 ActRm_VectorClone_d9d0 (deep copy dst <- src) and 0x14000da70
// ActRm_ArrayResize16_da70 (SetSize), 16-byte stride: Action/exe_bodies.h.

// 0x140025920 (Avl38Retrace, Player/exe_bodies.h) — the AVL retrace/rebalance.
//   NOTE the argument: retail passes a pointer to the PARENT node slot
//   (LEA RDX,[RBP+0x48], which at that point still holds the node the new child was
//   linked under), NOT the newly created node. See the b3a0 descent loop below.

// 0x140002340 (Ghidra: Object_ExternalPtr_BuildComponentRef_Serialize)
//   serializes one ExternalPtr through the save-file string table.

// ============================================================
// Dispatch slots (.rdata) — resolved once
// ============================================================
// 0x140152ac8  IO::Serializer::Serialize(int*)  -- (serializer, int*, const char* name)
//              retail always passes R8D=0 for the name.
// 0x140152aa0  Object::InternalBasePtr::Serialize(this, serializer)
// 0x1401c7158  DWORD type id tested by IsDerivedFrom on the key. Ghidra labels this
//              g_BaseTypeInfo_CitizenComponent; other ports in this tree call the same
//              global "work entity type" (completetask.cpp) — the label is not
//              load-bearing here, only the DWORD value is.
// MemAlloc/MemFree are dispatch slots 0x140152b60/0x140152b68 in retail; we call
// System::MemoryAllocator::MemAlloc / MemFree by name (B3R).

// `*(int32*)(*(void**)serializer + 8) == 0` is the engine-wide "am I loading?" test
// (0 = loading, non-zero = saving). Same idiom as decaldescription.cpp et al.
// Retail re-reads it from memory at every use site; we do the same rather than
// caching, because the read is what the original does.
static inline bool SerIsLoading(IO::Serializer* serializer) {
    return static_cast<int>(serializer->GetDirection()) == 0;   // IO::Serializer::GetDirection, retail 0x180006210
}

using ClaimNode = MarketWork::ClaimNode;

// ============================================================
// 0x14002b5f0 — MarketBst_b5f0: serialize one payload dynamic array
// ============================================================
// TRUE signature (Ghidra prints `void __fastcall(longlong*, Serializer*)`, which is
// correct for once — 2 args, void return, confirmed: only RCX/RDX are read and RAX is
// dead at RET 14002b673).
//
//   arr [RCX] : the Array<Carry> header { data @+0x00, int32 count @+0x08, int32 cap @+0x0C }
//               (`MOV EAX,dword ptr [RCX+0x8]` at 14002b5fa reads the count as a DWORD)
//   serializer [RDX]
//
// Wire format: int32 count, then `count` records of { ExternalPtr ref, int32 value }
// (stride 0x10: `ADD RBX,0x10` at 14002b65b and `ADD RSI,0x10` at 14002b57b).
// On LOAD the array is resized to the stored count BEFORE the element loop; on SAVE
// the resize is skipped and the existing buffer is written out.
void MarketBst_b5f0(MarketWork::CarryList* arr, IO::Serializer* serializer) {

    // 14002b5fa..b612: copy the count into a stack slot and serialize it in place.
    // The stack slot is what gets read back on load — the array's own count field is
    // NOT the serialize target; the resize below is what commits it.
    int count = arr->count;
    serializer->Serialize(count, nullptr);

    // 14002b618..b628
    if (SerIsLoading(serializer)) {
        MaterialArraySetSize(arr, count);   // 0x14000da70
    }

    // 14002b62d..b662: element loop. Note retail reloads the buffer pointer AFTER the
    // resize (MOV RBX,qword ptr [RBX]) — reading it earlier would use the freed buffer.
    MarketWork::Carry* p = arr->data;
    for (int i = 0; i < count; ++i) {
        ::Object::SerializeExternal<ComponentDescription>(*serializer, p->resource);
        serializer->Serialize(p->count, nullptr);
        ++p;
    }
}

// 14002b35a..b391 / b408..b43f / b463..b49a — three byte-identical copies of the node
// constructor emitted by the compiler (empty-tree, right-child, left-child). Folded
// into one helper here; the stores are exact (retail's merged stores are the named
// fields in address order):
//   key = 0 then = key        -- retail really does write it twice
//   carries.data = 0, count = cap = 0 (one 8-byte zero in retail)
//   ActRm_VectorClone_d9d0(&carries, srcArray)
//   parent, left = 0, right = 0, balance + height = 0 (one dword)
// Returns null when MemAlloc fails — retail then links the NULL in anyway (see callers).
static ClaimNode* MarketBst_MakeNode(CitizenComponent* key, MarketWork::CarryList* srcArray, ClaimNode* parent) {
    // 14002b3ed: MOV EDX,0x8 / LEA ECX,[RDX+0x30] -> MemAlloc(size=0x38, align=8)
    ClaimNode* n = static_cast<ClaimNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (n == nullptr) return nullptr;
    n->element.citizen = nullptr;
    n->element.citizen = key;
    n->element.carries.data = nullptr;
    n->element.carries.count = 0;      // with capacity: one 8-byte zero
    n->element.carries.capacity = 0;
    MaterialArrayAssign(&n->element.carries, srcArray);   // 0x14000d9d0
    n->parent  = parent;
    n->left    = nullptr;
    n->right   = nullptr;
    n->balance = 0;
    n->height  = 0;
    return n;
}

// ============================================================
// 0x14002b290 — MarketBst_b290: serialize the whole market BST (v1)
// ============================================================
// TRUE signature: `void __fastcall(longlong* tree, Serializer*)`. Ghidra's `undefined`
// return is genuinely void here (RAX dead at the RET, 14002b5e1) and the arg count is
// genuinely 2 (R8/R9 are only ever used as scratch). This is NOT one of the family's
// under-counted prototypes — 0x14002a9f0 is (it takes 4).
//
//   tree [RCX] : the Set { node* root @+0x00, int32 count @+0x08 }  == MarketWork+0x48
//                (the container is two raw fields: the count is reached from the root
//                field's address: LEFT)
//   serializer [RDX]
void MarketBst_b290(MarketWork::ClaimTree* tree, IO::Serializer* serializer) {

    int32_t* const treeCount = &tree->count;

    // 14002b2b0..b2c0: node count in/out.
    int nodeCount = *treeCount;
    serializer->Serialize(nodeCount, nullptr);

    if (SerIsLoading(serializer)) {
        // ---------------- LOAD: rebuild the tree, node by node ----------------
        // 14002b2d3..b4d0.
        // Two dead stores live here in retail (14002b2e1..b2ed store &payload into
        // RBP+0x50 and RBP+0x58 and never read either back) — inlining debris, not
        // omitted behaviour.
        for (int i = 0; i < nodeCount; ++i) {
            // 14002b2f1..b2f9: key + payload header cleared every iteration (3 qwords).
            Object::InternalPtr<CitizenComponent> key = nullptr;
            MarketWork::CarryList payload = { nullptr, 0, 0 };   // 16 bytes, the exact
                                                                 // size ActRm_VectorClone_d9d0 reads.

            // 14002b2fd..b329: read the key, then drop it unless it derives from the
            // expected type. Retail overwrites the key with 0 on a type mismatch and
            // still inserts the node — a null-keyed node, not a skipped one.
            // (InternalBasePtr::Serialize on the typed slot.)
            key.InternalBasePtr::Serialize(*serializer);
            if (!key.IsNull() && !Object::IsDerivedFromNullSafe(key, CitizenComponent::TypeId())) {
                key = nullptr;
            }

            // 14002b334: the per-node payload block.
            MarketBst_b5f0(&payload, serializer);

            ClaimNode* root = tree->root;
            if (root == nullptr) {
                // 14002b341..b397: empty tree. NOTE the faithful oddity — when MemAlloc
                // fails retail stores the NULL straight into the root (MOV [R14],RBX
                // with RBX=0 at 14002b394) and still bumps the count. No retrace here.
                tree->root = MarketBst_MakeNode(key.Get(), &payload, nullptr);
                *treeCount += 1;   // 14002b4ad
            } else {
                // 14002b39c..b4ad: unsigned-key descent, insert-if-absent, then retrace.
                ClaimNode* cur = root;
                bool inserted = false;
                for (;;) {
                    // 14002b3a3: CheckPtr on the CURRENT NODE (the node pointer itself,
                    // which doubles as the address of its key field).
                    // 14002b3ac/b3b0: CMP [RBP-0x20],RAX / JC -> unsigned "key < node->key".
                    if (key.Get() < cur->element.citizen.Get()) {
                        ClaimNode* left = cur->left;
                        if (left != nullptr) { cur = left; continue; }
                        // 14002b448..b49d: link a new LEFT child (null on alloc failure).
                        cur->left = MarketBst_MakeNode(key.Get(), &payload, cur);
                        inserted = true;
                        break;
                    }
                    // 14002b3b2: CheckPtr on the KEY slot this time (&local key), not the node.
                    // 14002b3c0/b3c3: JNC when node->key >= key -> duplicate, nothing to do.
                    if (cur->element.citizen.Get() >= key.Get()) break;
                    ClaimNode* right = cur->right;
                    if (right != nullptr) { cur = right; continue; }
                    // 14002b3ed..b442: link a new RIGHT child.
                    cur->right = MarketBst_MakeNode(key.Get(), &payload, cur);
                    inserted = true;
                    break;
                }
                if (inserted) {
                    // 14002b4a1: retrace from the PARENT slot (RBP+0x48 still holds the
                    // node we linked under), NOT from the new child.
                    ClaimNode* parentSlot = cur;
                    Avl38Retrace(&tree->root, &parentSlot);   // 0x140025920
                    *treeCount += 1;   // 14002b4ad
                }
                // Duplicate key falls through to 14002b4b1 WITHOUT bumping the count and
                // without taking ownership of the payload — it is freed just below.
            }

            // 14002b4b1..b4bf: the loaded payload buffer is always freed here. On the
            // insert paths the node owns a CLONE (d9d0), so this frees the temporary;
            // on the duplicate path it drops the record entirely. Both zero stores are
            // 8 bytes wide (the three named fields).
            System::MemoryAllocator::MemFree(payload.data);
            payload.data = nullptr;
            payload.count = 0;
            payload.capacity = 0;
        }
    } else {
        // ---------------- SAVE: in-order walk, write each node ----------------
        // 14002b4d5..b5cd.
        ClaimNode* node = tree->root;
        if (node == nullptr) return;
        while (node->left != nullptr) node = node->left;   // 14002b4e1: leftmost

        while (node != nullptr) {
            // 14002b506..b524: write the key, and null it out in memory if it no longer
            // derives from the expected type. This MUTATES the live tree on save —
            // faithful, and deliberate in retail (it is how stale refs get culled).
            node->element.citizen.InternalBasePtr::Serialize(*serializer);
            // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
            // downcast to CitizenComponent*; the answer is the same.
            if (!node->element.citizen.IsNull() &&
                !Object::IsDerivedFromNullSafe(node->element.citizen, CitizenComponent::TypeId())) {
                node->element.citizen = nullptr;
            }

            // 14002b527..b582: the payload block, written INLINE rather than by calling
            // 0x14002b5f0 — same wire format, the compiler just duplicated the body.
            int count = node->element.carries.count;
            serializer->Serialize(count, nullptr);
            if (SerIsLoading(serializer)) {
                // 14002b540..b54c. Dead in this arm (we only get here when NOT loading),
                // but present in retail, so it is kept rather than "cleaned up".
                MaterialArraySetSize(&node->element.carries, count);   // 0x14000da70
            }
            MarketWork::Carry* p = node->element.carries.data;
            for (int i = 0; i < count; ++i) {
                ::Object::SerializeExternal<ComponentDescription>(*serializer, p->resource);
                serializer->Serialize(p->count, nullptr);
                ++p;
            }

            // 14002b584..b5cd: in-order successor.
            ClaimNode* right = node->right;
            if (right != nullptr) {
                node = right;
                while (node->left != nullptr) node = node->left;
            } else {
                ClaimNode* parent = node->parent;
                if (parent != nullptr && parent->left == node) {
                    node = parent;                       // 14002b5aa
                } else {
                    while (parent != nullptr && parent->right == node) {
                        node = parent;                   // 14002b5ba
                        parent = parent->parent;
                    }
                    node = node->parent;                 // 14002b5c6
                }
            }
        }
    }
}
