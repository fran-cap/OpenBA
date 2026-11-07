// engine/src/ui/controlui.cpp
// Clean reimplementation of ControlUI panel methods.
//
// ControlUI (0x40 bytes): Building enable/disable toggle panel.
// Layout: +0x30 = checkbox element (InternalBasePtr)
//         +0x38 = sub-component (InternalBasePtr) found via component array search

#include "Dialog/ui_panels.h"
#include <new>
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Object/factory_registration.h"   // Object::IsDerivedFrom
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <cstdio>
#include <cstdlib>
#include "Component/Component.h"
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Dialog/ControlUIConfig.h"   // X::TypeId() (ENDGAME B.3.9)
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "Dialog/ControlUI.h"

// ---- Element type ids: UI::ButtonElement and UI::CheckElement ----------------
// Retail reads these through exe .rdata exrefs 0x1401514f8 (ButtonElement, tested
// by Slot4 before storing the element at +0x30) and 0x1401516a8 (CheckElement,
// tested by Slot4/Slot7 before touching the checked byte at elem+0xd8). In
// standalone both live in the element-type-id table in
// standalone/reg_element_typeids.cpp, which is GATED on BANISHED_LIVE_ELEMENT_TIDS
// and therefore off by default — so TypeId_ButtonElement and
// TypeId_CheckboxElement are both normally null.
//
// MEASURED consequence before this fix (BANISHED_CONTROLUI_TRACE, demo pasture):
// all three pasture ControlUIs reported `elem(+0x30)=0` with a perfectly good
// `target(+0x38)`, because IsDerivedFrom(elem, 0) is always false. That null +0x30
// silently disabled everything this controller does — SetElementId(50000) was
// skipped (so Slot9 could never match a click), the +0xd8 checked byte was never
// written, and Slot7 returned at the `if (!elem)` guard before calling Enable().
// The four button predicates were correct all along; nothing ever applied them,
// which is why work/split/empty all sat at the element ctor default (enabled,
// unchecked) and read as "highlighted".
//
// ENDGAME B.3.9: UI::ButtonElement / UI::CheckElement now have a ported TypeId() (a
// class static, always populated), so the private-copy-vs-gated-global split above
// collapses to the static: both arms already answered the same id.
static uint32_t ControlUI_ButtonElementTypeId() {
    return UI::ButtonElement::TypeId();
}

static uint32_t ControlUI_CheckElementTypeId() {
    return UI::CheckElement::TypeId();
}

namespace controlui_sa {

// The panel's config, if it is a ControlUIConfig (retail's checked cast of +0x10).
static ControlUIConfig* Config(ControlUI* self) {
    UI::DialogControllerConfig* datasource = self->_config.Get();
    if (::Object::IsDerivedFromNullSafe(datasource, ControlUIConfig::TypeId()))
        return static_cast<ControlUIConfig*>(datasource);
    return nullptr;
}

// Report Slot4's INPUTS as well as its outputs. The first version of this printed only
// +0x30/+0x38 — which are Slot4's OUTPUTS — so when the type-id guard rejected every call
// before writing them, the trace read as "the framework never bound +0x20/+0x28" and sent
// the investigation at the binding path, which was working fine all along. Always print
// the thing the guard actually tests.
static void Trace(const char* what, ControlUI* self) {
    UI::DialogControllerConfig* cfg = self ? self->_config.Get() : nullptr;
    uint32_t wantTid = ControlUIConfig::TypeId();
    // The command is read unchecked (a diagnostic of whatever config is bound).
    int cmd = cfg ? static_cast<ControlUIConfig*>(cfg)->_command : -1;
    std::fprintf(stderr,
        "[ControlUI] %s self=%p | IN: cfgTid=%s(%u) cfg(+0x10)=%p elem(+0x20)=%p entity(+0x28)=%p"
        " | OUT: elem(+0x30)=%p target(+0x38)=%p cmd=%d\n",
        what, (void*)self,
        wantTid ? "ok" : "ZERO", wantTid, (void*)cfg,
        self ? (void*)self->_container : nullptr,
        self ? (void*)self->_dataSource : nullptr,
        self ? static_cast<void*>(self->button) : nullptr,
        self ? (void*)self->target : nullptr,
        cmd);
    std::fflush(stderr);
}


// idx7 (+0x38): per-frame enable / checked sync.
//
// UI::UserInterface::Update (Runtime 0x1801213c0) -> DialogInstance::ComputeLayout
// (0x180113d90) -> vtable idx6 OnUpdateLayout (0x1801153a0) walks +0x78 and dispatches
// controller idx7 every frame, with no dirty flag. Report the OUTCOME — the element
// state read back AFTER the writes — rather than the intent, and print in every arm so
// that silence can never read as success. Rate-limited because this is a per-frame path.
void TraceSlot7(ControlUI* self) {
    static int s_left = 12;
    if (s_left <= 0) return;
    --s_left;
    UI::DialogControllerConfig* cfg = self->_config.Get();
    UI::ButtonElement* elem = self->button;
    int cmd = cfg ? static_cast<ControlUIConfig*>(cfg)->_command : -1;
    if (!elem) {
        // The pre-fix state: correct predicates, nothing to apply them to.
        std::fprintf(stderr, "[ControlUI] Slot7 cmd=%d elem(+0x30)=NULL"
                     " -> no Enable, no checked write\n", cmd);
    } else {
        // kElementFlagDisabled = disabled (UI::Element::Enable, Runtime 0x180116910).
        uint32_t flags = elem->_flags;
        // _checked is the checked byte of a CheckElement ONLY (CheckElement::IsChecked,
        // Runtime 0x180048c80; CheckDescription::CreateInstance allocates 0xe0). Only
        // the cmd=2 work button is a CheckElement here, so gate the read on the same
        // type id Slot7 gates its write on.
        if (::Object::IsDerivedFromNullSafe(elem, ControlUI_CheckElementTypeId())) {
            unsigned checked = static_cast<UI::CheckElement*>(elem)->_checked;
            std::fprintf(stderr, "[ControlUI] Slot7 cmd=%d elem=%p disabled=%d checked=%u\n",
                         cmd, (void*)elem, (flags & UI::kElementFlagDisabled) ? 1 : 0, checked);
        } else {
            std::fprintf(stderr, "[ControlUI] Slot7 cmd=%d elem=%p disabled=%d"
                         " checked=n/a (ButtonElement, no +0xd8)\n",
                         cmd, (void*)elem, (flags & UI::kElementFlagDisabled) ? 1 : 0);
        }
    }
    std::fflush(stderr);
}

// idx8 (+0x40) is DELIBERATELY NOT OVERRIDDEN, matching retail.
// Slot4 is called ONCE, by UI::DialogInstance::AddController (Runtime 0x1801133b0), which
// writes +0x10 config / +0x18 dialog / +0x20 element / +0x28 entity in four consecutive
// stores and then dispatches idx4.

}  // namespace controlui_sa

// Slot4: Find the button element, find the target component by type, set initial state.
// [4] 0x14005f9b0
void ControlUI::OnCreated() {
    // Check the config's type
    ControlUIConfig* component = controlui_sa::Config(this);
    // SA guard: retail reads the command / target type unconditionally. If the config
    // is unbound or not a ControlUIConfig there is no command and no target type, so
    // there is nothing this button could drive.
    if (!component) return;

    // Check the container's type, store at +0x30
    uint32_t elemBaseTypeId = ControlUI_ButtonElementTypeId();
    UI::Element* dialogElem = _container;
    UI::ButtonElement* checkedElem = nullptr;
    if (::Object::IsDerivedFromNullSafe(dialogElem, elemBaseTypeId)) {
        checkedElem = static_cast<UI::ButtonElement*>(dialogElem);
    }
    button = checkedElem;

    // Set element ID 50000 (0xC350). SA guard: retail assumes the .rsc always binds
    // an element here; a dialog that lacks it would take SetElementId(null).
    if (button)
        button->SetElementId(50000);

    // Get entity, find the component of the config's target type
    Object::Base* entity = _dataSource;
    int subTypeKey = (int)component->_componentType;
    uint32_t entityTypeId = ComponentEntity::TypeId();
    ComponentEntity* entityChecked = nullptr;
    if (::Object::IsDerivedFromNullSafe(entity, entityTypeId)) {
        entityChecked = static_cast<ComponentEntity*>(entity);
    }
    // The generic find (0x1400c3680); the tree's copy returned null for a null entity.
    Component* subComp = entityChecked ? entityChecked->FindComponentByTypeId((uint32_t)subTypeKey) : nullptr;
    target = subComp;
    // SA guard: the checkbox sync below dispatches the component's slot 13. A selected
    // entity that simply lacks the target component is normal (the same .rsc button
    // template is reused across building types), so bail rather than fault.
    if (!subComp) return;

    // If the element IsA CheckElement, set initial checkbox state.
    //
    // This guard is LOAD-BEARING, not defensive: _checked (+0xd8) is one byte PAST the
    // end of a plain ButtonElement. ButtonDescription::CreateInstance (Runtime
    // 0x180111510) allocates 0xd8; CheckDescription::CreateInstance (0x1801119c0)
    // allocates 0xe0 precisely so the checked byte fits.
    //
    // FAITHFULNESS DEVIATION (deliberate, documented rather than silent): retail guards
    // this with Object::Base::IsA (exe slot 0x140151410, 0x1800491c0), an EXACT-type
    // test, whereas we use IsDerivedFrom. Identical for the pasture, different for a
    // CheckElement SUBCLASS (Runtime ships RadioElement).
    uint32_t checkboxTypeId = ControlUI_CheckElementTypeId();
    UI::ButtonElement* elemAt30 = button;
    bool isCheckbox = ::Object::IsDerivedFromNullSafe(elemAt30, checkboxTypeId);
    if (isCheckbox) {
        // Component slot 13: query the command state with the config's command
        uint32_t value = target->CommandMask(component->_command);

        // Validate element again for safe access
        UI::CheckElement* checkboxElem = nullptr;
        if (::Object::IsDerivedFromNullSafe(elemAt30, checkboxTypeId)) {
            checkboxElem = static_cast<UI::CheckElement*>(elemAt30);
        }
        // Set checkbox state: bit 1 of return value
        checkboxElem->_checked = (char)((value >> 1) & 1);
    }
    controlui_sa::Trace("Slot4", this);
}

// Slot7: Update checkbox state from component, set enabled/disabled.
// [7] 0x14005f870
void ControlUI::OnUpdateLayout() {
    // Get the config
    ControlUIConfig* component = controlui_sa::Config(this);
    // SA guards, same reasoning as Slot4: no config => no command key; no target
    // component => nothing to query. Both are dispatched through unconditionally
    // in retail, which can rely on the .rsc binding always being complete.
    if (!component) return;
    if (!target) return;

    // Check if element IsA CheckElement
    uint32_t checkboxTypeId = ControlUI_CheckElementTypeId();
    UI::ButtonElement* elemAt30 = button;
    bool isCheckbox = ::Object::IsDerivedFromNullSafe(elemAt30, checkboxTypeId);
    if (isCheckbox) {
        // Query current state from the component's slot 13
        uint32_t value = target->CommandMask(component->_command);
        char newState = (char)((value >> 1) & 1);

        // Only update if state changed
        UI::CheckElement* checkboxElem = nullptr;
        if (::Object::IsDerivedFromNullSafe(elemAt30, checkboxTypeId)) {
            checkboxElem = static_cast<UI::CheckElement*>(elemAt30);
        }
        if (newState != (char)checkboxElem->_checked) {
            UI::CheckElement* checkboxElem2 = nullptr;
            if (::Object::IsDerivedFromNullSafe(elemAt30, checkboxTypeId)) {
                checkboxElem2 = static_cast<UI::CheckElement*>(elemAt30);
            }
            checkboxElem2->_checked = newState;
        }
    }

    // Set enabled/disabled (Element slot 8) from the component's enabled bit
    uint32_t enabledBits = target->CommandMask(component->_command);
    bool enabled = (enabledBits & 1) != 0;

    UI::ButtonElement* elem = button;
    if (!elem) return;
    elem->Enable(enabled);
    controlui_sa::TraceSlot7(this);
}

// Slot9: On checkbox click (event 0x12, id 50000), toggle component state.
// `element` is a POINTER to the caller's InternalPtr — compare what it holds
// against the stored element. Original asm: CMP [RDI],RAX where RDI=element,
// RAX=*(self+0x30)
// [9] 0x14005f7f0
void ControlUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId param3, int32_t param4,
                               int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 == UI::EventId::Activated && param4 == 50000) controlui_sa::Trace("Slot9(click)", this);

    if (param3 == UI::EventId::Activated && param4 == 50000) {
        // CheckPtr on stored element before comparison
        if (element->Get() == button) {
            // Get the config
            ControlUIConfig* component = controlui_sa::Config(this);
            if (!component) return;   // SA guard: no config => no command to send

            // Component slot 14 on the target: run the command
            Component* subCompPtr = target;
            if (!subCompPtr) return;  // SA guard: entity lacks the target component
            subCompPtr->Command(component->_command);
        }
    }
}

// ============================================================
// STANDALONE promotion — real ControlUI controller vtable
// ============================================================
// ControlUI was a UI_DIALOG_STUB (all-no-op), so every port above was dead code.
// These are the generic component-command buttons: on a pasture they are
// work / split / empty, all sharing element id 50000 and distinguished ONLY by
// the command int in ControlUIConfig+0x20 (work = 2, split = 1, empty = 0).
// With the stub in place Slot4 never stamped the element id and never resolved
// the target component, Slot7 never synced enable/checked state (which is why
// the buttons still LOOKED live — they kept their .rsc appearance), and Slot9
// landed on StubNop. The PastureComponent side (slots 13/14 -> Pasture_CanCommand /
// Pasture_Command) has been ported and live the whole time.
//
// Retail vtable @ 0x14015b380:
//   idx4 (+0x20) 0x14005f9b0 — setup   (Ghidra "ControlUI_Serialize"  — mislabeled)
//   idx7 (+0x38) 0x14005f870 — per-frame (Ghidra "ControlUI_OnCreate" — mislabeled)
//   idx9 (+0x48) 0x14005f7f0 — click   (Ghidra "ControlUI_Update"    — mislabeled)
// The Ghidra names are wrong on all three; the vtable INDEX is authoritative.
// (Ghidra's "ControlUI_Slot12/13/14/vslot17/vslot18" are ControlUIConfig methods.)
//
// Retail does NOT override idx8, but idx8 is the only slot the framework ticks
// per frame (see the note at pastureui.cpp:425), so — exactly as the PastureUI /
// StorageUI promotions do — idx8 here is a composite that self-heals Slot4 once
// and then drives Slot7.


// create 0x140010c00: MemAlloc(0x40, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* ControlUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ControlUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ControlUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* ControlUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ControlUI());
}

DEFINE_EXE_TYPE(ControlUI, "UI::DialogController")
