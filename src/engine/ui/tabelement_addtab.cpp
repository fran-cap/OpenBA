// ============================================================================
// src/engine/ui/tabelement_addtab.cpp — WAVE 10, lane UIORACLE
//
//   ?AddTab@TabElement@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@
//    V?$ExternalPtr@VElementDescription@UI@@@4@0@Z     retail 0x1800DA130
//                                                      ordinal 1589
// ============================================================================
//
// THE RECORDED BLOCKER, RE-DERIVED FROM THE DISASSEMBLY RATHER THAN INHERITED
// ---------------------------------------------------------------------------
// Three documents hold this row shut and each names a different reason:
//
//   * WAVE7_LANE_E.md §2 — "the body constructs a temporary ElementDescription/
//     DialogInstance and is not a leaf accessor".
//   * WAVE8_5_HOLDBACK_RECHECK.md row 22 — "needs ElementDescription::
//     CreateInstance reversed/owned first".
//   * The UI census — "not replayable, needs the predictive oracle"; parked
//     behind the allocator NO-GO.
//
// Re-checked (reference_holdback_blockers_are_stale_check_them_first.md):
//
//  1. THE FIRST ONE IS DISSOLVED, AND IT WAS A MISREADING OF THE FRAME.
//     The stack temporary is not an ElementDescription and it is not a
//     DialogInstance. It is a plain `UI::Dialog`, DEFAULT-CONSTRUCTED, and the
//     construction is `??0Dialog@UI@@QEAA@XZ` INLINED — the two listings agree
//     store for store:
//
//        AddTab 0x1800DA163..0x1800DA1D2     ??0Dialog 0x180112DC1..0x180112E2A
//        vptr := 0x180154788, 0x1801547A0    identical
//        +0x08/+0x10/+0x18 := 0              identical
//        vptr := 0x1801562A0 (Dialog vftbl)  identical
//        +0x20..+0x28 := 0 (one MOVDQA)      two MOV, same 16 bytes
//        +0x30 := 0xFFFFFFFF                 identical
//        +0x34/+0x38/+0x40/+0x48..+0x80 := 0 identical
//
//     …so the object is 0x88 bytes and the exported constructor and destructor
//     ARE the inline sequences. This port calls them instead of open-coding a
//     memset it would have to keep in sync with a class it does not own. The
//     teardown matches the same way (0x1800DA295..0x1800DA329 against
//     ??1Dialog@UI@@UEAA@XZ 0x180112E40).
//
//  2. THE SECOND ONE IS TRUE AND IS NOT A BLOCKER FOR THE *BODY*.
//     `ElementDescription::CreateInstance` is genuinely unported. But AddTab
//     does not call it by name — it makes an INDIRECT call through the
//     ExternalPtr's own vtable slot +0x40 (0x1800DA1E5, 0x1800DA235), which is
//     a pointer both this port and the DLL load from the same object. Porting
//     the body needs the slot, not the callee. What the missing callee DOES
//     block is the ORACLE's coverage of the two objects it creates — stated
//     below, and declared with Exclude() so it is counted rather than implied.
//
//  3. THE ALLOCATOR NO-GO IS NOT LOAD-BEARING HERE EITHER. Every allocation in
//     this function happens inside a callee: the two CreateInstance calls, and
//     `Collection::Array<InternalPtr<Element>>::SetSize(n, fill)` (0x180076900),
//     which has been PORTED and registered since Wave 9 lane UIW
//     (array.cpp, kSpec_Fill8Z). **This lane's conclusion does not depend on
//     lane ALLOC's re-test of that gate.**
//
//  4. NOT REPLAYABLE — STILL TRUE, AND MORE SO THAN THE ROW SAYS. A replay
//     oracle would have to run retail's body a second time; retail's body
//     CREATES TWO ELEMENTS AND APPENDS THEM. Running it twice does not produce
//     a comparable copy, it produces a TabElement with four children and a tab
//     count of two. That is not a weak oracle, it is corruption.
//
// ENDGAME B.3.6 block C5 — THE BODY IS NOW THE MEMBER `UI::TabElement::AddTab`
// ---------------------------------------------------------------------------
// The body moved into the member, written directly (the RtSink oracle
// scaffold it used to be written against had only its live arm in use). The
// two ExternalPtr vtable +0x40 calls are now the virtual slot-8
// `ElementDescription::CreateInstance` (RULE B6-2), the UpdateContainers
// RT_EXPORT_ONCE is the member call, and the two Dialog doors moved to
// namespace-scope atomics (RULE B6-5). The registered door keeps retail's
// (this, ret, button, content) shape and places the member's result.
// The dirty walk is still run TWICE, UNCONDITIONALLY (0x1800DA211 and
// 0x1800DA270) — contrast UpdateContainers', which is guarded on "did bit
// 0 change".
//
// ROUTING — CHECKED, NOT ASSUMED
// -------------------------------
// `get_xrefs_to 0x1800DA130` returns FOUR references and NOT ONE OF THEM IS A
// CALL: three data references (the export table and two RTTI/relocation slots)
// plus the external entry point. The DLL never calls its own AddTab. The only
// caller in the process is ours — `UIDesc_AddControllers_Impl`
// (src/game/Dialog/toolui.cpp:133), which runs while a dialog whose
// UIDescription carries tab entries is being built. So, unlike
// `TabElement::UpdateContainers`, THIS PORT NEEDS NO DETOUR: there is no split
// brain to close, because there is no second caller.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <new>

#include "ui/Dialog.h"
#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/TabElement.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_Fill8Z

namespace {

// UI::Dialog, 0x88 bytes — see the store-for-store table in the banner.
constexpr size_t kDialogSize = 0x88;

// ENDGAME B.3.9 UI-D (B9-13 foreign site): the UI::Dialog temporary is a real
// `UI::Dialog` — `::new (dlg) UI::Dialog()` / `~T()` (slot 0, flags 0) — and the two
// ??0/??1Dialog doors this file resolved are retired.

// Array<InternalPtr<Element>>::SetSize(n, fill) — the kSpec_Fill8Z row
// (0x180076900) over `Element::_children`.
inline void ArrayCellsSetSizeFill(void* arrayHdr, int32_t n, const void* fill) {
    rtcoll::AsArray<&rtcoll::kSpec_Fill8Z>(arrayHdr)->SetSizeFill(n, fill);
}

// The UNCONDITIONAL dirty walk. Deliberately NOT shared with UpdateContainers'
// guarded one: retail spells this walk out twice here with no guard at all
// (0x1800DA211, 0x1800DA270).
void DirtyWalk(UI::Element* self) {
    for (UI::Element* p = self; p != nullptr; p = p->_parent)
        p->_flags |= UI::kElementFlagNeedsSizeCalc;
}

} // namespace

namespace UI {

// ---------------------------------------------------------------------------
// The body. `button` and `content` are the two ExternalPtr<ElementDescription>
// values (trivially copyable, in registers — retail reads each one's vtable
// straight out of the register, 0x1800DA1E5 / 0x1800DA235).
//
// Retail creates the CONTENT straight into the caller's hidden return slot
// (0x1800DA232 loads RSI, the incoming RDX) and reads the cell back from it;
// here the member returns the handle by value and the door places it in that
// slot, which nothing can observe between the two (the slot is the caller's
// and is read only after the return).
// ---------------------------------------------------------------------------
Object::InternalPtr<Element> TabElement::AddTab(Object::ExternalPtr<ElementDescription> button,
                                                Object::ExternalPtr<ElementDescription> content) {
    alignas(16) uint8_t dlg[kDialogSize];
    Dialog* const dialog = ::new (static_cast<void*>(dlg)) Dialog();   // 0x1800DA163..1D2

    Object::InternalPtr<Element> cell;   // retail's [RBP+0x67], the scratch InternalPtr

    // ---- tab BUTTON: created from `button` into the scratch cell -----------
    button->CreateInstance(&cell, dialog);                    // 0x1800DA1E5 — slot 8, virtual
    if (cell.Get() != nullptr) cell->_parent = this;          // 0x1800DA1F0
    ArrayCellsSetSizeFill(&_children, _children.GetSize() + 1, &cell);   // 0x1800DA209
    DirtyWalk(this);                                          // 0x1800DA211

    // ---- tab CONTENT: created from `content` (the return value) ------------
    Object::InternalPtr<Element> result;
    content->CreateInstance(&result, dialog);                 // 0x1800DA235 — slot 8, virtual
    cell = result;                                            // 0x1800DA238/23B
    if (cell.Get() != nullptr) cell->_parent = this;          // 0x1800DA244
    ArrayCellsSetSizeFill(&_children, _children.GetSize() + 1, &cell);   // 0x1800DA25E
    DirtyWalk(this);                                          // 0x1800DA270

    ++_tabCount;                                              // 0x1800DA286

    UpdateContainers();                                       // 0x1800DA28F

    dialog->Dialog::~Dialog();                                 // 0x1800DA295..329
    return result;
}

}  // namespace UI
