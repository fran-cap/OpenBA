#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/MessageDialog.h"
#include "Dialog/ui_dialogs.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Dialog/exe_bodies.h"

// ENDGAME B.3.8 block DLG: MessageDialog is a real class (Dialog/MessageDialog.h)
// with a compiler-generated vtable. Retail evidence per slot is in the header.

namespace {
// The shared tail of slots 10-13: result into +0x24, then
// UserInterface::CloseDialog(InternalPtr(this)).
void CloseWithResult(MessageDialog* self, int32_t result) {
    self->_result = result;
    Object::InternalPtr<UI::DialogInstance> selfIBP(self);
    self->_ui->CloseDialog(&selfIBP);
}
}  // namespace

// [4] OnOpened (0x14006D140) — Setup
// Finds buttonCancel, buttonOk, buttonYes, buttonNo elements by name.
// Sets element IDs: Cancel=0, Ok=1, Yes=2, No=3.
void MessageDialog::OnOpened() {
    struct { const char* name; int id; } buttons[] = {
        { "buttonCancel", 0 },
        { "buttonOk",     1 },
        { "buttonYes",    2 },
        { "buttonNo",     3 },
    };

    for (auto& btn : buttons) {
        System::String nameStr(btn.name);
        UI::Element* elem = FindElement(nameStr).Get();

        if (elem) {
            elem->SetElementId(btn.id);
        }
    }
}

// [5] OnClosed (0x14006D0C0) — `RET 0`: an empty override of the base body.
void MessageDialog::OnClosed() {}

// [8] OnElementEvent (0x14006D070) — dispatches a button click (msg 0x12)
// through the vtable, so a subclass override runs:
//   id 0 -> [11] OnCancel, 1 -> [10] OnOk, 2 -> [12] OnYes, 3 -> [13] OnNo.
void MessageDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                   UI::EventId a, int32_t b, int32_t /*c*/,
                                   int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (a != UI::EventId::Activated)
        return;

    switch (b) {
    case 0: OnCancel(); break;
    case 1: OnOk();     break;
    case 2: OnYes();    break;
    case 3: OnNo();     break;
    }
}

// [10] 0x14006CFF0 / [11] 0x14006D030 / [12] 0x14006CFB0 / [13] 0x14006CF70:
// result 1 / 0 / 2 / 3 into +0x24, then close.
void MessageDialog::OnOk()     { CloseWithResult(this, 1); }
void MessageDialog::OnCancel() { CloseWithResult(this, 0); }
void MessageDialog::OnYes()    { CloseWithResult(this, 2); }
void MessageDialog::OnNo()     { CloseWithResult(this, 3); }

// ============================================================
// Construction, destruction, reflection
// ============================================================
// Original ctor (decompiled @ 0x14006d300): DialogInstance::DialogInstance then
// the vtable store. No additional fields beyond DialogInstance's.

#include "Object/factory_registration.h"
#include <cstdio>

MessageDialog::MessageDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                             Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0) {}

// create 0x1400125d0: MemAlloc(0xc0, 8), then the default ctor.
::Object::Base* MessageDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MessageDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) MessageDialog());
}
// createInPlace 0x140012620 (retail returns null for Internal-rooted classes;
// the tree's in-place build is kept, B8-13).
::Object::Base* MessageDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) MessageDialog());
}

DEFINE_EXE_TYPE(MessageDialog, "UI::DialogInstance")
