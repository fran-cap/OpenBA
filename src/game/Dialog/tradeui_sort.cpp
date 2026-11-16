// src/game/Dialog/tradeui_sort.cpp
// Wave A — the TradeUI table sorts: two introsort drivers, their two comparators,
// and their two small-range sorts. Six exe functions, one shared algorithm.
//
// tradeui.cpp calls only the two DRIVERS (0x1400162d0 fn_IntroSort_ByName,
// 0x1400165d0 fn_IntroSort_ByComponent); their comparators and small-range sorts are
// the other four.
//
// ── ABI, taken from the DISASSEMBLY (the decompile is wrong about arity) ──────
// Ghidra gives these functions phantom trailing parameters, because MSVC reuses the
// caller's register-home slots as String locals and the decompiler reads those slots
// as incoming arguments (`local_res8`, `local_res10`, ... are home slots, not
// locals). Every signature below was read off the prologue instead:
//
//   0x1400162d0  ByName driver        RCX=array, EDX=count            — 2 params.
//                                     R8/R9 are never read; the call site in
//                                     tradeui.cpp passes 2 args.
//   0x1400165d0  ByComponent driver   RCX=array, EDX=count, R8=ctx    — 3 params,
//                                     with an entry guard `CMP EDX,1 ; JLE ret`.
//   0x140015ad0  ByName small sort    RCX=first, RDX=last             — 2 params.
//   0x140016920  ByComponent small    RCX=first, RDX=last, R8=ctx     — 3 params.
//   0x1400845d0  ByName comparator    RCX=this(UNUSED), RDX=a, R8=b   — a stateless
//                                     functor: RCX is homed at 0x1400845d0 and then
//                                     never read.
//   0x140084430  ByComponent compare  RCX=ctx, RDX=a, R8=b            — this one
//                                     really does use its context.
//
// ── Element layout (stride 0x18, confirmed by the 0x2aaaaaaaaaaaaaab magic
//    divisor for /24 in both drivers) ────────────────────────────────────────────
//   +0x00  uint64  key         (the BST lookup key used by the ByComponent compare)
//   +0x08  void*   descriptor  (+0x28 = string table, +0x30 = string name)
//   +0x10  uint64  payload     (opaque; copied by the swaps)
//
// ── Why one body covers both drivers (the core is Dialog/introsort24.h) ──────
// 0x1400162d0 and 0x1400165d0 are the same generic introsort: median-of-3, an
// explicit 62-entry stack instead of recursion, recurse-on-the-smaller-side, and a
// cutoff to a max-selection sort at 8 elements or fewer. They differ only in which
// comparator and small-range sort they call. The partition is transcribed
// step-for-step rather than replaced with std::sort, because an introsort is
// UNSTABLE: for rows that compare equal the final order is a property of the exact
// partition sequence, and substituting a different sort would reorder equal-priced
// trade rows relative to retail. That is cosmetic, but it is not nothing, and it
// would be invisible in testing.
#include "ui/UserInterface.h"
#include "object/External.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "ui/String.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K1)
#include "Dialog/exe_bodies.h"
#include "Dialog/TradeUI.h"
#include "Component/RawMaterialDescription.h"
#include "Component/TradeComponent.h"
#include "Dialog/introsort24.h"   // the shared introsort core (driver + small sort)

namespace {

typedef TradeComponent* const* SortCtx;      // the ByComponent context: the caller's TradeComponent*

// ── The name key ─────────────────────────────────────────────────────────────
// Both comparators derive a System::String from an element the same way:
//   P = *(void**)(elem + 8)
//   UIString ctor(buf, stringTable = *(uint64*)(P + 0x28), stringName = P + 0x30)
//   -> UIString::operator System::String -> copy-construct into the caller's slot
// The intermediate System::String is destroyed immediately; the copy is what gets
// compared. A System::String is 8 bytes here (the three live slots in the retail
// frame are 8 bytes apart), which is why `uint64_t` is the right storage.
//
// Retail gives the UIString 40 bytes of frame (RSP+0x28..RSP+0x50). 64 is used here
// purely as headroom; over-reserving a stack buffer cannot change behaviour.
// Note retail never destroys the UIString itself — only the System::String temps.
// That is reproduced rather than "fixed": adding a dtor call retail does not make
// would be a behaviour change smuggled in under the heading of correctness.
bool BuildNameKey(const TradeUI::StorageItem* elem, System::String* outString) {
    // The elements are TradeUI::StorageItem rows (_storageItems, +0x50); P is
    // the row's RawMaterialDescription.
    const RawMaterialDescription* p = elem->rawMaterial;
    if (!p) { return false; }

    alignas(8) uint8_t uiBuf[64];
    UI::String* ui = ::new (uiBuf) UI::String(
        ::Object::ExternalPtr<UI::StringTable>(p->_stringTable), p->_name);
    System::String tmp;
    ::new (&tmp) System::String(ui->operator System::String());
    *outString = tmp;
    return true;
}

// 0x1400845d0 — ByName comparator. `ctx` is the ignored functor `this`.
bool NameLess(SortCtx /*ctx*/, const TradeUI::StorageItem* a, const TradeUI::StorageItem* b) {
    System::String sa, sb;
    const bool okA = BuildNameKey(a, &sa);
    const bool okB = BuildNameKey(b, &sb);
    bool result = false;
    if (okA && okB) result = sa < sb;
    return result;
}

// The BST lookup both halves of the ByComponent comparator perform: the
// TradeComponent's stock tree (stockRoot +0x60, TradeResourceNode: +0x00 key,
// +0x08 value, +0x18 left, +0x20 right), the same storage AVL ported in
// tradeui_leaves.cpp, and the same UNSIGNED key comparison (pointer order).
// Returns the low 32 bits of the node's value, or 0 when the key is absent.
int32_t TreeCountFor(SortCtx ctx, const ComponentDescription* key) {
    if (!ctx) return 0;
    const TradeComponent* obj = *ctx;
    if (!obj) return 0;
    const TradeResourceNode* node = obj->stock.root;
    while (node) {
        if (key < node->resource)      node = node->left;
        else if (node->resource < key) node = node->right;
        else                           return node->quantity;
    }
    return 0;
}

// 0x140084430 — ByComponent comparator.
//
// ⚠ The name undersells it. This is not "compare by component"; it looks each
// element's key up in the context's tree, compares the resulting COUNTS DESCENDING,
// and falls back to name order only when the counts tie. Reading it as a plain
// component-identity compare would produce a table sorted by something else
// entirely while still looking sorted.
bool ComponentLess(SortCtx ctx, const TradeUI::StorageItem* a, const TradeUI::StorageItem* b) {
    const int32_t ca = TreeCountFor(ctx, a->resource);
    const int32_t cb = TreeCountFor(ctx, b->resource);
    if (cb < ca) return true;                // higher count sorts first
    if (ca < cb) return false;
    return NameLess(nullptr, a, b);          // tie -> name ascending
}

} // namespace

// ── Entry points (declared in Dialog/TradeUI.h) ──────────────────────────────

// 0x1400162d0 — sort the trade table by name. RCX=array, EDX=count: retail
// never reads R8/R9 (the two "phantom" parameters the caller used to pass are gone).
extern "C" void TradeUI_IntroSort_ByName_162d0(TradeUI::StorageItem* array, int count) {
    if (!array || count < 2) return;
    IntroSort24::Sort(array, count, [](const TradeUI::StorageItem* a, const TradeUI::StorageItem* b) {
        return NameLess(nullptr, a, b);
    });
}

// 0x1400165d0 — sort the trade table by stocked count (descending), name on ties.
extern "C" void TradeUI_IntroSort_ByComponent_165d0(TradeUI::StorageItem* array, int count,
                                                    TradeComponent* const* ctx) {
    if (!array || count < 2) return;         // retail: `CMP EDX,1 ; JLE ret`
    IntroSort24::Sort(array, count, [ctx](const TradeUI::StorageItem* a, const TradeUI::StorageItem* b) {
        return ComponentLess(ctx, a, b);
    });
}

// The BANISHED_TRADE_SORT_SELFTEST introsort self-test was deleted with the flag
// (ENDGAME C).

