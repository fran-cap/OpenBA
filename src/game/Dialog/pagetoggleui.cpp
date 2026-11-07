// engine/src/ui/pagetoggleui.cpp
// Clean reimplementation of PageToggleUI panel methods.
//
// PageToggleUI (0x48 bytes): A checkbox that toggles between page 0 and page 4.
// Layout: +0x30 = checkbox element (InternalBasePtr), +0x38 = "pages" PageElement,
//         +0x40 = saved page index (int32)

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include <cstdio>
#include "Object/factory_registration.h"   // Object::IsDerivedFrom
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/PageElement.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/CheckElement.h"  // X::TypeId() (B.3.9 CG2)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "Dialog/PageToggleUI.h"

// ---- UI::CheckElement type id -----------------------------------------------
// Retail reads this through exe .rdata exref 0x1401516a8 (import string
// "?_typeInfo@CheckElement@UI@@0VBaseTypeInfo@Object@@A" — read out of the exe's
// own .rdata at 0x1401b8ce8, and an export of Runtime-x64.dll). In standalone the
// whole element-type-id table lives in standalone/reg_element_typeids.cpp, gated on
// BANISHED_LIVE_ELEMENT_TIDS. That gate GRADUATED into the default-on table 2026-08-22 and is
// now a kill-switch, so TypeId_CheckboxElement is normally POPULATED — but it can still
// be null when someone A/Bs with BANISHED_LIVE_ELEMENT_TIDS=0, and then
// FindElementType(root, 0) can never match, leaving +0x30 null for Slot7 to fault on.
// The private-resolve below is what keeps that arm safe, so it stays.
//
// ENDGAME B.3.9 CG2: TypeId_CheckboxElement's initialiser (engine_funcs.cpp)
// binds it to the SAME exref (0x1401516a8) as TypeId_CheckElement — there is
// no separate "CheckboxElement" Runtime class (no CheckboxElement.h under
// src/engine/ui/); both globals are UI::CheckElement's id (Table 3 name
// lie). UI::CheckElement::TypeId() is the ported static over that class's own
// record (D6 W1) — a source independent of the exe-side reflection cell this
// file used to read through the shared global, so it answers correctly with
// BANISHED_LIVE_ELEMENT_TIDS off too. The private ResolveTypeIdByNameLazy
// fallback and its gate-dependent guard were scaffolding for that exe-side
// global's unresolved/gated state; they retire with this read.
static uint32_t PageToggleUI_CheckElementTypeId() {
    return UI::CheckElement::TypeId();
}

// True once the id is known. The promotion gate in ui_dialog_stub_factories.cpp
// calls this before dispatching ANY PageToggleUI slot, because Slot7 derefs the
// checkbox vtable with no null check of its own (retail 0x140075780).
bool PageToggleUI_TypeIdsReady() { return PageToggleUI_CheckElementTypeId() != 0; }

// ---- PageToggleUI: PROMOTED to the real reimpl (production-statistics button) --
// PageToggleUI drives the `checkProduction` CheckElement that Dialog/Production.rsc
// inserts into a workplace dialog's title bar (userTitle0). It is the ONLY thing
// that switches a StandardDialog between pageMain(0) and pageUser(4) — the page
// that holds the production-statistics content (Production.rsc:production4, driven
// by ProductionUI below). Retail bodies:
//   Slot4 (+0x20, Setup)  0x140075850 — FindElementType(CheckElement) -> +0x30,
//                         SetElementId(700), FindElement("pages") -> +0x38, then
//                         restore the saved page if the dialog is on page 0 or 4.
//   Slot7 (+0x38, Update) 0x140075780 — enable/disable the checkbox (vtable +0x40)
//                         depending on whether the active page is 0 or 4.
//   Slot9 (+0x48, Event)  0x140075710 — msg 0x12 + id 700 -> +0x40 = checked ? 4 : 0
//                         and PageElement::SetActivePage. It opens NO dialog.
//   Slot3 (+0x18)         0x1400759a0 is a 3-byte `RET 0` — left a no-op stub
//                         (see ui_panels.h:618).
// The generic stub made all three no-ops, so the button was inert: the id-700
// event never fired and clicking the control did nothing.
//
// HAZARD (retail-faithful, hence the gate below): Slot7 derefs the checkbox
// vtable with NO null check, so if Slot4's FindElementType misses, Slot7 faults.
// In standalone the checkbox type id is null by default — the exe reads it through
// .rdata exref 0x1401516a8, and that whole element-type-id table
// (standalone/reg_element_typeids.cpp) is gated on BANISHED_LIVE_ELEMENT_TIDS,
// which is NOT in the default-on table. With TypeId_CheckboxElement null,
// FindElementType(root, 0) can never match, +0x30 stays null, and Slot7 faults.
// pagetoggleui.cpp therefore resolves a PRIVATE copy of the id by name (the
// exref's import string, read out of exe .rdata at 0x1401b8ce8, is
// "?_typeInfo@CheckElement@UI@@0VBaseTypeInfo@Object@@A" — i.e. UI::CheckElement),
// without writing the shared global, and Ready() refuses to dispatch any slot
// until it is non-zero.
namespace pagetoggleui_sa {
    // The CheckElement type id is resolved PRIVATELY inside pagetoggleui.cpp
    // (PageToggleUI_TypeIdsReady) — see the long note there. We must not fill the
    // shared TypeId_CheckboxElement here: reg_element_typeids.cpp owns that
    // table and holds it behind BANISHED_LIVE_ELEMENT_TIDS on purpose.
    // Every slot below dereferences the checkbox / "pages" element unguarded, and
    // Slot4 cannot populate either without the type id. Report the outcome once.
    bool Ready() {
        bool ok = PageToggleUI_TypeIdsReady();
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            fprintf(stderr, "[PageToggleUI] Ready=%d (CheckElement id resolved=%d)\n",
                    (int)ok, (int)PageToggleUI_TypeIdsReady());
            fflush(stderr);
        }
        return ok;
    }
    // Slot7 runs every frame, including before Slot4 has populated +0x30/+0x38.
    // Retail cannot observe that ordering (its Slot4 always succeeds first), so
    // the empty-cache test below is a STANDALONE-ONLY precondition, not a
    // behaviour change: with both caches populated this is retail verbatim.
}

// Slot4: Find checkbox by type, find "pages" element, restore saved page.
// [4] 0x140075850
void PageToggleUI::OnCreated() {
    if (!pagetoggleui_sa::Ready()) return;

    // Find checkbox element by type ID in the container's element tree
    uint32_t checkboxTypeId = PageToggleUI_CheckElementTypeId();
    UI::Element* elemPtr = _container->FindElementType(checkboxTypeId).Get();

    // IsDerivedFrom check + store at +0x30
    UI::CheckElement* checked = nullptr;
    if (elemPtr) {
        bool derived = ::Object::IsDerivedFromNullSafe(elemPtr, checkboxTypeId);
        if (derived) checked = static_cast<UI::CheckElement*>(elemPtr);
    }
    checkBox = checked;

    // SetElementId on checkbox = 700
    checkBox->SetElementId(700);

    // Find "pages" element in the owning dialog instance
    System::String nameStr("pages");
    pages = static_cast<UI::PageElement*>(_dialog->FindElement(nameStr).Get());

    // If active page is 0 or 4, restore saved page; otherwise reset saved page to 0
    int activePage = pages->GetActivePage();
    if (activePage != 0) {
        activePage = pages->GetActivePage();
        if (activePage != 4) {
            savedPage = 0;
            return;
        }
    }
    pages->SetActivePage(savedPage);
}

// Slot7: Enable/disable checkbox based on whether active page is 0 or 4.
// [7] 0x140075780
void PageToggleUI::OnUpdateLayout() {
    if (!pagetoggleui_sa::Ready()) return;
    // Standalone-only precondition: Slot7/Slot9 run before Slot4 has populated
    // +0x30/+0x38 (retail's Slot4 always succeeds first); with both caches
    // populated this is retail verbatim.
    if (!checkBox || !pages) return;

    UI::CheckElement* checkbox = checkBox;
    int activePage = pages->GetActivePage();
    bool enabled;
    if (activePage != 0) {
        activePage = pages->GetActivePage();
        enabled = (activePage == 4);
    } else {
        enabled = true;
    }
    // vtable[8] = SetEnabled(this, bool)
    checkbox->Enable(enabled);   // Element slot 8 Enable
}

// Slot9: On checkbox toggle event, switch between page 0 and page 4.
// [9] 0x140075710
void PageToggleUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4,
                                  int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!pagetoggleui_sa::Ready()) return;
    // Standalone-only precondition: Slot7/Slot9 run before Slot4 has populated
    // +0x30/+0x38 (retail's Slot4 always succeeds first); with both caches
    // populated this is retail verbatim.
    if (!checkBox || !pages) return;

    if (param3 == UI::EventId::Activated && param4 == 700) {
        // The checkbox state (CheckElement::_checked, +0xd8)
        char checkState = static_cast<char>(checkBox->_checked);
        if (checkState == 0) {
            savedPage = 0;
        } else {
            savedPage = 4;
        }
        pages->SetActivePage(savedPage);
    }
}

// ============================================================
