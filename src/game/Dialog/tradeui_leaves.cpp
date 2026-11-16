// src/game/Dialog/tradeui_leaves.cpp
// The small exe helpers src/game/Dialog/tradeui.cpp calls: TradeComponent::GetOrderName
// (0x140015fd0), TradeComponent::AddPurchaseOrder (0x140059490) and the storage AVL insert
// (0x14000e2c0 / 0x14000e1c0). The other TradeUI leaves and the files that hold them are
// listed in the block at the BOTTOM of this file.
//
// ── The three clears ─────────────────────────────────────────────────────────
//   0x140016050  fn_DynArray_Clear_0x20      (sell entries, stride 0x20)
//   0x140016130  fn_DynArray_Clear_0x18_Buy  (buy entries,  stride 0x18)
//   0x140016200  fn_DynArray_Clear_0x18      (storage/order entries, stride 0x18)
//
// All three are the SAME generic array-resize body with newCount folded to 0 by the
// compiler. Decompiled (2026-07-30) they look substantial — a realloc branch, a
// copy/zero loop, a free — but every one of those paths is provably dead at
// newCount == 0:
//
//   iVar1 = count;  if (count != 0) {
//       old = data;  count = 0;
//       if (cap < 0)            { ... MemAlloc(0,8) ... }   // cap is never negative
//       iVar3 = (old == data) ? count_old : 0;              // data unchanged -> old count
//       if (iVar3 < 0) { ...copy/zero loop... }             // old count is never negative
//       if (old != data) MemFree(old);                      // data unchanged -> never
//   }
//
// So the entire observable effect is `count = 0`. Capacity and the buffer are
// deliberately RETAINED (this is a vector::clear(), not a shrink) — which is why the
// element strides differ between the three yet none of them appears below: the stride
// only ever mattered to the dead grow/copy path.
//
// Header layout, shared by all three (the standard engine dynamic-array header):
//   +0x00 void* data
//   +0x08 int32 count
//   +0x0c int32 capacity
//
// ⚠ If a future change makes any caller pass a non-zero newCount, these are NOT the
// right functions to extend — the real generic resize (with the live grow/copy path)
// already exists in the tree as Reimpl_Array_Resize_* in serialize_arrays.cpp.
// These three are the clear-only specialisations retail emitted separately.
#include "Component/exe_bodies.h"   // B3R: CitizenAI_Reg_helper_e360
#include "system/String.h"
#include "Component/TradeComponent.h"
#include <cstdint>
#include <new>
#include "Dialog/exe_bodies.h"
#include "Dialog/TradeUI.h"   // TradeStockEntry, TradeUI_StorageTree_Insert_e1c0
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree

// ── B.4 DLG-e: the three clears are Collection::Array<T>::Clear() ──────────
// `Clear()` is `SetSize(0)`, and rtcoll::SetSizeCore at newSize == 0 does
// exactly what the banner above says the retail bodies do and no more. The
// three arrays are TradeUI members (_sellItems +0x30, _buyItems +0x40,
// _storageItems +0x50, Dialog/TradeUI.h) and their callers call Clear() on
// them by name; the three C bodies are gone.

// ── 0x140015fd0 — fn_Building_GetOrderName ───────────────────────────────────
// Returns (by hidden out-param, MSVC by-value String convention) the name String of
// the CURRENTLY SELECTED order, or an empty String when nothing is selected.
//
//   tradeState + 0x2c  int32  selected order index; NEGATIVE means "no selection"
//   tradeState + 0x30  void*  order array base, element stride 0x20, String at +0x00
//
// Retail (FUN_140015fd0):
//   if (idx < 0)  *(uint64*)out = 0;                       // raw 8-byte zero, NOT a
//                                                          // default-ctor call
//   else          String::String(out, &orderArray[idx]);   // copy-ctor
//   return out;
//
// A System::String is 8 bytes here, which is why the empty case is a single zeroed
// qword and why the copy is a real copy-ctor (it refcounts / duplicates the buffer)
// rather than a memcpy.
//
// Returning `out` matters: callers use the return value, not their own buffer
// pointer, so dropping the return would leave them reading an uninitialised local.
void* TradeComponent::GetOrderName(System::String* out) {
    if (!out) return out;
    const int32_t idx = tradeId;
    if (idx < 0) {
        new (out) System::String();
        return out;
    }
    const TradeMerchantOrder* base = merchantOrders._data;
    if (!base) {      // standalone guard: retail derefs blind
        new (out) System::String();
        return out;
    }
    const System::String* src = &base[idx].merchantName;
    new (out) System::String(*src);
    return out;
}

// ── 0x140059490 — fn_Building_AddPurchaseOrder ───────────────────────────────
// TOGGLES a resource in the purchase-order list of the CURRENTLY SELECTED order.
//
//   tradeState + 0x2c  int32  selected order index
//   tradeState + 0x30  void*  order array base, element stride 0x20
//
// and within the selected order record (base + idx*0x20):
//   rec + 0x10  int64*  resource-entity pointer array (element stride 8)
//   rec + 0x18  int32   count
//   rec + 0x1c  int32   capacity
//
// The name says "Add" but retail (FUN_140059490) is a toggle, and the branch is the
// part worth reading twice:
//
//   present = (resourceEntity is somewhere in arr[0..count))
//   if (addFlag == 0 || present)  erase EVERY occurrence of resourceEntity
//   else                          append resourceEntity
//
// So calling it with addFlag=1 on a resource that is already listed REMOVES it. A
// port that read the flag as "1 means add" and appended unconditionally would give
// a purchase list that silently grows duplicates — a believable-looking list that
// is wrong, not a crash. Verified against the disassembly at 0x1400594e6:
//   TEST R8B,R8B ; JZ erase      (addFlag == 0  -> erase)
//   TEST AL,AL   ; JNZ erase     (present       -> erase)
//
// The erase is a plain remove-all: shift the tail down one, decrement count, then
// RE-TEST the same index (retail does `DEC ESI` / `SUB R9,8` immediately before the
// unconditional `INC ESI` / `ADD R9,8`, which is how it avoids skipping an element
// that shifted into the hole).
//
// Growth on append is the engine's standard amortized rule, matching GrowTo in
// src/engine/video/device_enum_types.cpp: capacity = newCount, then newCount*3 when
// the array was non-empty. The allocator call is the STATIC two-arg form —
// disassembly at 0x140059533 is `MOVSXD RCX,cap ; MOV EDX,8 ; SHL RCX,3 ; CALL
// [0x140152b60]`, i.e. MemAlloc(cap * 8, 8) with NO hidden header byte. That was
// checked against the disassembly rather than trusted from the decompile, because a
// mis-sized allocation here would corrupt the neighbouring order record instead of
// faulting.
void TradeComponent::AddPurchaseOrder(
                                               ComponentDescription* resource,
                                               int8_t  addFlag) {
    ComponentDescription* const resourceEntity = resource;   // the list holds the pointer
    const int32_t orderIdx = tradeId;
    TradeMerchantOrder* const orderBase = merchantOrders._data;
    // Retail indexes unconditionally; a negative index (no order selected) would read
    // behind the array. Callers gate on the selection, so this only guards standalone.
    if (orderIdx < 0 || !orderBase) return;

    TradeMerchantOrder* rec = orderBase + orderIdx;
    ComponentDescription*** items = &rec->goods._data;
    int32_t*  count = &rec->goods._size;
    int32_t*  cap   = &rec->goods._capacity;

    bool present = false;
    if (*items) {
        for (int32_t i = 0; i < *count; ++i) {
            if ((*items)[i] == resourceEntity) { present = true; break; }
        }
    }

    if (addFlag == 0 || present) {
        // Remove every occurrence, compacting in place.
        if (!*items) return;
        for (int32_t i = 0; i < *count; ) {
            if ((*items)[i] == resourceEntity) {
                for (int32_t j = i; j + 1 < *count; ++j) (*items)[j] = (*items)[j + 1];
                --*count;
                continue;                    // re-test i: the tail shifted into it
            }
            ++i;
        }
        return;
    }

    // Append.
    // B.3.2 NOT CONVERTED to Collection::Array<T>::SetSize: the `if (!grown)`
    // arm below restores count and cap and returns, and `SetSize` has no
    // failure path to hang that on — rtcoll::SetSizeCore commits the new size
    // before it allocates, exactly as retail does. The guard is standalone-only
    // (retail never checks) and deleting it to fit the template would be a
    // behaviour change in the direction of a wild write, so this growth stays
    // hand-rolled until the guard can go.
    const int32_t oldCount = *count;
    const int32_t oldCap   = *cap;
    const int32_t newCount = oldCount + 1;
    ComponentDescription** oldItems = *items;
    *count = newCount;
    if (*cap < newCount) {
        *cap = newCount;
        if (oldCount != 0) *cap = newCount * 3;
        ComponentDescription** grown = static_cast<ComponentDescription**>(System::MemoryAllocator::MemAlloc(
                  static_cast<uint64_t>(*cap) * 8, 8));
        if (!grown) {                        // standalone-only: retail never checks.
            *count = oldCount;               // leave the array exactly as we found it
            *cap   = oldCap;
            return;
        }
        *items = grown;
    }
    for (int32_t i = (oldItems == *items) ? oldCount : 0; i < newCount; ++i) {
        (*items)[i] = (i < oldCount) ? oldItems[i] : resourceEntity;
    }
    if (oldItems != *items) System::MemoryAllocator::MemFree(oldItems);
}

// ── 0x14000e2c0 / 0x14000e1c0 — the storage AVL insert ───────────────────────
// Node layout (0x30 bytes, allocated 8-aligned) — read off the disassembly at
// 0x14000e216, which zeroes +0x10/+0x18/+0x20 with one XOR'd register and +0x28
// with its low dword:
//   +0x00  uint64  key
//   +0x08  uint64  value
//   +0x10  node*   parent
//   +0x18  node*   left
//   +0x20  node*   right
//   +0x28  int16   balance   \  written as one dword by the ctor; the rebalance
//   +0x2a  int16   height    /  in Helper_e360 treats them as two int16s
//
// Tree header (param_1):
//   +0x00  node*   root
//   +0x08  int32   count
//
// ⚠ Keys compare UNSIGNED. The disassembly at 0x14000e23a is `CMP RCX,RAX ; JC`
// then `CMP RAX,RCX ; JNC` — carry-flag branches, not JL/JGE. Ghidra's `ulonglong`
// agrees, but this was checked against the instructions because a signed compare
// would put any key with bit 63 set on the wrong side of the tree and produce a
// silently mis-ordered (but structurally valid) BST.
// 0x14000e360 — the AVL rebalance walk, ported in citizenai_housing_idle.cpp as
// CitizenAI_Reg_helper_e360 and called by name (B3R; the lazy resolver is gone).

// The node's first 16 bytes come from the entry (TradeStockEntry, TradeUI.h):
// retail copies key and value as two qwords, so the value's pad dword is copied too.
extern "C" TradeResourceNode* TradeUI_StorageTreeNodeNew_e2c0(const ComponentDescription* /*key*/,
                                                              const TradeStockEntry* entry,
                                                              TradeResourceNode* parent) {
    // The key argument is genuinely unused: retail passes it but the ctor takes both
    // key and value from `entry`.
    TradeResourceNode* n = static_cast<TradeResourceNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
    if (!n) return nullptr;                  // retail checks this too
    n->resource = entry->resource;           // key
    n->quantity = entry->quantity;           // value (low dword)
    n->_pad0C[0] = entry->_pad0C[0];         // value (high dword: the pad)
    n->_pad0C[1] = entry->_pad0C[1];
    n->_pad0C[2] = entry->_pad0C[2];
    n->_pad0C[3] = entry->_pad0C[3];
    n->parent = parent;
    n->left = nullptr;
    n->right = nullptr;
    n->balance = 0;                          // balance + height: one dword in retail
    n->height = 0;
    return n;
}

extern "C" TradeResourceNode** TradeUI_StorageTree_Insert_e1c0(AvlTreeHead<TradeResourceNode>* head,
                                                               TradeResourceNode** out,
                                                               const TradeStockEntry* entry) {
    if (!head || !out || !entry) return out;   // standalone-only
    TradeResourceNode** tree = &head->root;
    int32_t* count = &head->count;

    if (*tree == nullptr) {
        // Empty tree: the root node is built inline rather than through e2c0 (retail
        // duplicates the ctor here), with a null parent and no rebalance.
        TradeResourceNode* n = static_cast<TradeResourceNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
        if (!n) {
            *tree = nullptr;
            *out  = nullptr;
        } else {
            n->resource = entry->resource;
            n->quantity = entry->quantity;
            n->_pad0C[0] = entry->_pad0C[0];
            n->_pad0C[1] = entry->_pad0C[1];
            n->_pad0C[2] = entry->_pad0C[2];
            n->_pad0C[3] = entry->_pad0C[3];
            n->parent = nullptr;
            n->left = nullptr;
            n->right = nullptr;
            n->balance = 0;
            n->height = 0;
            *out  = n;
            *tree = n;
        }
        ++*count;                            // retail bumps the count even when the
        return out;                          // allocation failed — kept faithful
    }

    const ComponentDescription* key = entry->resource;
    TradeResourceNode* node = *tree;
    TradeResourceNode* inserted = nullptr;
    for (;;) {
        if (key < node->resource) {          // unsigned, see note above
            if (node->left == nullptr) {
                inserted = TradeUI_StorageTreeNodeNew_e2c0(key, entry, node);
                node->left = inserted;
                break;
            }
            node = node->left;
        } else if (node->resource < key) {
            if (node->right == nullptr) {
                inserted = TradeUI_StorageTreeNodeNew_e2c0(key, entry, node);
                node->right = inserted;
                break;
            }
            node = node->right;
        } else {
            *out = node;                     // key already present: hand back the
            return out;                      // existing node, count UNCHANGED
        }
    }

    // Rebalance from the insertion point upward. `node` is the PARENT of the new
    // node, not the new node — retail passes the address of the local holding it and
    // Helper_e360 walks it up via +0x10 to the root, rotating as it goes. That helper
    // is 0x14000e360, CitizenAI_Reg_helper_e360, called by name (B3R).
    {
        TradeResourceNode* walk = node;
        CitizenAI_Reg_helper_e360(tree, &walk);   // 0x14000e360
    }
    *out = inserted;
    ++*count;
    return out;
}

// ── The other TradeUI leaves, each in its own file ───────────────────────
//   0x140059670                → src/game/Dialog/tradeui_buyentries.cpp
//   0x14005a520 + 0x14000ea80  → src/game/Dialog/tradeui_price.cpp
//   0x1400162d0 0x1400165d0 0x140015ad0 0x140016920 0x1400845d0 0x140084430
//                              → src/game/Dialog/tradeui_sort.cpp
//
// 0x140081420 TradeUI::OnTradeButton, the checkout, is in tradeui_checkout.cpp; the
// buttonTrade click (element id 0x4e20) in tradeui.cpp calls it.
