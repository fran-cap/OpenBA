// src/game/Dialog/tradeui_buyentries.cpp
// Wave A, last leaf — 0x140059670 fn_Building_GetBuyEntries.
//
// Fills `out` (an 8-byte-stride dynamic array) with every entity the docked merchant
// will deal in. It feeds the buy table directly, so a wrong port produces a
// POPULATED-but-wrong panel — not an empty one and not a crash. That is why the
// control flow below was taken from the DISASSEMBLY rather than the decompile.
//
// ── The `joined_r0x000140059767` back-edge is NOT an infinite loop ────────────
// Ghidra renders the ExternalFactory walk as a `joined_` label with a bare
// `if (x == 0) return;` at the top and a `goto` at the bottom, which reads like a
// spin. The disassembly at 0x140059740 shows what it really is:
//
//   140059740: MOV RAX, [0x1401529d8]      ; the ExternalFactory singleton slot
//   140059747: MOV EDX, [0x1401c7318]      ; typeId = ComponentDescription
//   14005974d: XOR R8D, R8D                ; cursor = null  -> "give me the first"
//   140059750: MOV R12, [RAX]              ; factory = *singleton
//   14005975b: CALL [0x1401514d0]          ; FindNext(factory, typeId, cursor)
//   140059761: MOV RBP, RAX
//   140059764: TEST RAX, RAX
//   140059767: JZ  <exit>                  ; <== the "joined_" target
//
// It is the standard cursor iteration: seed with null, advance with the previous
// entity, stop on null. The second `CALL [0x1401514d0]` at 0x1400598f4 is the
// advance.
//
// ⚠ AND `0x1401514d0` IS NOT `fn_Sprite_IsValid`. Ghidra labels both call sites as
// that and types the result as `bool`, which is what makes the loop look degenerate.
// It is `ExternalFactory::FindNext(self, typeId, prev) -> void*` — already in the
// tree as `::Object::ExternalFactory::FindNext`, with the exact RCX/EDX/R8 signature the
// disassembly shows. The dependency list this leaf was handed off with named
// `fn_Sprite_IsValid`; that came from the bad label and no sprite is involved.
//
// ── The merchant record (M = *(tradeDesc+0x48) + tradeState[+0x2c] * 0x48) ────
//   M + 0x10  uint32  resource-category mask for the raw-material match
//   M + 0x20  void**  the merchant's own goods list      | count at M + 0x28
//   M + 0x30  int32*  the merchant's select-type id list | count at M + 0x38
// The +0x20/+0x28 and +0x30/+0x38 pairs are independently corroborated by the
// existing dump in standalone/trade_probe.cpp, which prints them as "goods" and
// "ints" respectively.
//
// ── Three ways an entity gets into the buy list ──────────────────────────────
//   1. It is in the merchant's own goods list          (unconditional)
//   2. Its RawMaterialDescription flags at +0x5c hit the M+0x10 mask
//   3. Its SelectDescription id at +0x40 equals one of the M+0x30 entries
// Rules 2 and 3 are evaluated for EVERY ComponentDescription in the factory, and an
// entity can be appended more than once — rule 3 appends once per matching entry in
// the id list. That duplication is retail behaviour, not a bug in the transcription.
//
// ── Dead scans, preserved as a comment rather than as code ───────────────────
// Retail scans each entity's components for ValueDescription at three separate
// points and then throws the result away — nothing reads it, and the appended value
// is always the entity itself. Those scans call only the vtable type-id getter and
// IsDerivedFrom, both pure queries with no side effects, so omitting them is not
// observable. They are the residue of an inlined "find" whose consumer was optimised
// out. Noted here so the next reader does not go looking for the missing use.
#include "object/Internal.h"   // Object::Base::Type (B.3.8 callers)
#include "object/TypeInfo.h"
#include "Component/TradeDescription.h"   // ENDGAME B.3.9 TR: TradeDescription::TypeId()
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "Component/ComponentDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/SelectDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/TradeComponent.h"
#include <cstdint>
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

namespace {

// The 8-byte-stride dynamic array header the out-param uses:
//   +0x00 void** data   +0x08 int32 count   +0x0c int32 capacity
struct PtrArray {
    const void** data;
    int32_t count;
    int32_t cap;
};

// Append one pointer. This is FUN_14000ee40 specialised to newCount = count + 1,
// which is how retail inlines it at both append sites. Identical growth rule to
// AddPurchaseOrder in tradeui_leaves.cpp: capacity = newCount, then newCount * 3
// when the array was non-empty; MemAlloc(cap * 8, 8) via the STATIC two-arg form.
//
// B.3.2 NOT CONVERTED to Collection::Array<T>::SetSize, same reason as
// AddPurchaseOrder's append in tradeui_leaves.cpp: the `if (!grown)` arm below
// restores count and cap and returns, and SetSize has no failure path — it
// commits the new size before allocating, exactly as retail does. That guard is
// standalone-only (retail never checks) and dropping it to fit the template
// would trade a bail-out for a wild write.
void AppendPtr(PtrArray* a, const void* value) {
    if (!a) return;
    const int32_t oldCount = a->count;
    const int32_t oldCap   = a->cap;
    const int32_t newCount = oldCount + 1;
    const void** oldData = a->data;
    a->count = newCount;
    if (a->cap < newCount) {
        a->cap = newCount;
        if (oldCount != 0) a->cap = newCount * 3;
        const void** grown = static_cast<const void**>(System::MemoryAllocator::MemAlloc(
                  static_cast<uint64_t>(a->cap) * 8, 8));
        if (!grown) {                        // standalone-only; retail never checks
            a->count = oldCount;
            a->cap   = oldCap;
            return;
        }
        a->data = grown;
    }
    for (int32_t i = (oldData == a->data) ? oldCount : 0; i < newCount; ++i) {
        a->data[i] = (i < oldCount) ? oldData[i] : value;
    }
    if (oldData != a->data) System::MemoryAllocator::MemFree(oldData);
}

// Find the first component of `typeId` on an entity, confirming with IsDerivedFrom.
// Entity layout: component pointer array at +0x28, count at +0x30; type id from
// vtable slot +0x10. Retail stops at the FIRST id match and yields null if the
// confirmation fails, rather than continuing the scan.
Description* FindComp(const ComponentDescription* entity, uint32_t typeId) {
    if (!entity) return nullptr;
    const int32_t count = entity->_subDescCount;
    Description* const* arr = entity->_subDescs;
    if (!arr || count <= 0) return nullptr;
    for (int32_t i = 0; i < count; ++i) {
        Description* comp = arr[i];
        if (!comp) continue;
        if (comp->Type() != typeId) continue;
        if (!::Object::IsDerivedFromNullSafe(comp, typeId)) return nullptr;
        return comp;
    }
    return nullptr;
}

} // namespace

void TradeComponent::GetBuyEntries(void* outArray) const {
    PtrArray* out = static_cast<PtrArray*>(outArray);   // the one body of 0x140059670 serves two 8-byte-stride out arrays (TradeUI::_purchaseItems, an Array<PurchaseItem>; tradecomponent_dock.cpp:273, a StackArray<ComponentDescription*>): the erased core views its out array  // CAST-OK: elem_stride
    if (!out) return;

    // Retail opens with FUN_140001bb0(out, 0) — the stride-8 resize-to-zero. Its only
    // observable effect is count = 0; the buffer and capacity are retained.
    out->count = 0;

    Description* tradeDescBase = _description;
    if (tradeDescBase && !::Object::IsDerivedFromNullSafe(tradeDescBase, TradeDescription::TypeId())) {
        tradeDescBase = nullptr;
    }
    if (!tradeDescBase) return;              // retail would deref null
    const TradeDescription* tradeDesc = static_cast<const TradeDescription*>(tradeDescBase);

    const int32_t merchantIdx = tradeId;
    const TradeMerchant* merchants = tradeDesc->_merchants;
    if (!merchants || merchantIdx < 0) return;
    const TradeMerchant& M = merchants[merchantIdx];

    // ── 1. Everything in the merchant's own goods list ────────────────────────
    ComponentDescription* const* goods = M.goods;
    const int32_t goodsN  = M.goodsCount;
    if (goods) {
        for (int32_t i = 0; i < goodsN; ++i) AppendPtr(out, goods[i]);
    }

    // ── 2 & 3. Walk every ComponentDescription the ExternalFactory knows ──────
    if (!::Object::ExternalFactory::_singleton) return;
    const ::Object::ExternalFactory* ebuyFactory = ::Object::ExternalFactory::_singleton;
    const uint32_t tidComponentDesc = ComponentDescription::TypeId();

    const uint32_t rawMatMask = M.sellFlags;
    const uint32_t* selectIds = M.naturalResources;
    const int32_t  selectN    = M.naturalResourceCount;

    const ::Object::External* cur = ebuyFactory->FindNext(tidComponentDesc, nullptr);
    while (cur) {
        const ComponentDescription* desc = static_cast<const ComponentDescription*>(cur);
        if (!::Object::IsDerivedFromNullSafe(cur, tidComponentDesc)) desc = nullptr;

        if (desc) {
            // 2. Raw-material category match against the merchant's M+0x10 mask.
            const RawMaterialDescription* rawMat = static_cast<const RawMaterialDescription*>(
                FindComp(desc, RawMaterialDescription::TypeId()));
            if (rawMat) {
                const uint32_t flags = rawMat->_flags;
                if ((rawMatMask & flags) != 0) AppendPtr(out, desc);
            }
        }

        // Retail fetches the NEXT entity here, before the select-id pass, and only
        // uses it at the bottom of the loop. Order preserved: FindNext is an engine
        // call and moving it could change what the iterator observes if the pass
        // below ever mutates the factory.
        const ::Object::External* next = ebuyFactory->FindNext(tidComponentDesc, cur);

        if (desc && selectIds) {
            // 3. Select-type id match. Note the append is INSIDE the id loop, so an
            // entity whose id appears more than once in the merchant's list is added
            // more than once — retail behaviour, deliberately not de-duplicated.
            const SelectDescription* sel = static_cast<const SelectDescription*>(
                FindComp(desc, SelectDescription::TypeId()));
            if (sel) {
                const uint32_t selId = sel->_group;
                for (int32_t k = 0; k < selectN; ++k) {
                    if (selId == selectIds[k]) AppendPtr(out, desc);
                }
            }
        }

        cur = next;
    }
}
