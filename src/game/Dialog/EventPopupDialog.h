#pragma once
// ============================================================================
// src/game/Dialog/EventPopupDialog.h — ENDGAME B.3.8 block DLG
//
// The modal popup the notification ribbon raises for a high-severity event.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "EventPopupDialog", parent "UI::DialogInstance", ti 0x1401c4fa8
//   create 0x140011920 (MemAlloc(0xc0, 8)), createInPlace 0x140011970
//   table 0x14015bd80, 10 slots:
//     [0] 0x1400119b0 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x140068c20 OnOpened
//     [5] DialogInstance::OnClosed          [6] DialogInstance::OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x140068bd0 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xc0: the event-log index at +0xb8 (OnOpened reads it).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

class EventPopupDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(EventPopupDialog)

public:
    EventPopupDialog() = default;                                   // create 0x140011920
    EventPopupDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                     Object::InternalPtr<UI::DialogInstance> parent,
                     int32_t eventIndex);                           // 0x140068e30 family

    ~EventPopupDialog() override = default;                        // [0] 0x1400119b0
    void OnOpened() override;                                      // [4] 0x140068c20
    // [5] / [6] / [7]: DialogInstance's bodies, inherited (B38 D3.1).
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140068bd0

public:
    int32_t eventIndex;                                            // +0xb8 event-log index
    int32_t _padBC;                                               // +0xbc
};

static_assert(offsetof(EventPopupDialog, eventIndex) == 0xb8, "EventPopupDialog +0xb8");
static_assert(sizeof(EventPopupDialog) == 0xc0, "EventPopupDialog is 0xc0 (create 0x140011920)");
static_assert(offsetof(EventPopupDialog, _padBC) == 0xBC, "EventPopupDialog::_padBC");
static_assert(alignof(EventPopupDialog) == 8, "EventPopupDialog is qword-aligned");
