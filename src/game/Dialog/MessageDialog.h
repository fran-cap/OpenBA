#pragma once
// ============================================================================
// src/game/Dialog/MessageDialog.h — ENDGAME B.3.8 block DLG (B38_RULES D1-D5)
//
// The OK / Cancel / Yes / No message box, and the base of
// OptionInputBindDialog. A real class with a compiler-generated vtable.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "MessageDialog", parent "UI::DialogInstance", ti 0x1401c51d8
//   create 0x1400125d0 (MemAlloc(0xc0, 8)), createInPlace 0x140012620
//   ctor 0x14006d300: DialogInstance(ui, dialog, parent, 0, 0, 0), stamp
//   table 0x14015c480, 14 slots:
//     [0] 0x140012660 scalar deleting dtor   [1] TypeName   [2] Type
//     [3] DialogInstance::Persist (import)   [4] 0x14006d140 OnOpened
//     [5] 0x14006d0c0 OnClosed `RET 0`        [6] DialogInstance::OnUpdateLayout
//     [7] DialogInstance::OnUpdate (import)   [8] 0x14006d070 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings [10] 0x14006cff0 OnOk
//     [11] 0x14006d030 OnCancel  [12] 0x14006cfb0 OnYes  [13] 0x14006cf70 OnNo
//   sizeof 0xc0: one qword at +0xb8 that neither ctor writes.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

class MessageDialog : public UI::DialogInstance {
    DECLARE_EXE_TYPE(MessageDialog)

public:
    MessageDialog() = default;                                      // create 0x1400125d0
    MessageDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                  Object::InternalPtr<UI::DialogInstance> parent);  // 0x14006d300

    ~MessageDialog() override = default;                           // [0] 0x140012660
    void OnOpened() override;                                      // [4] 0x14006d140
    void OnClosed() override;                                      // [5] 0x14006d0c0 RET
    // [6] / [7]: DialogInstance's bodies (import thunks), inherited (B38 D3.1).
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x14006d070
    virtual void OnOk();                                           // [10] 0x14006cff0
    virtual void OnCancel();                                       // [11] 0x14006d030
    virtual void OnYes();                                          // [12] 0x14006cfb0
    virtual void OnNo();                                           // [13] 0x14006cf70

public:
    uint64_t _padB8;                                               // +0xb8 never written
};

static_assert(offsetof(MessageDialog, _padB8) == 0xb8, "MessageDialog +0xb8");
static_assert(sizeof(MessageDialog) == 0xc0, "MessageDialog is 0xc0 (create 0x1400125d0 MemAlloc(0xc0))");
static_assert(alignof(MessageDialog) == 8, "MessageDialog is qword-aligned");
