#pragma once
// ============================================================================
// src/game/Dialog/FollowDialog.h — ENDGAME B.3.8 block DLG
//
// The "follow this citizen" camera dialog opened from the citizen panel.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "FollowDialog", parent "UI::DialogInstance", ti 0x1401c4fe0
//   create 0x140011a20 (MemAlloc(0xd8, 8)), createInPlace 0x140011a90
//   ctors 0x140069510 / 0x1400695a0; CitizenUI's Follow button inlines one
//   table 0x14015be00, 10 slots:
//     [0] 0x140011af0 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x140069340 OnOpened
//     [5] 0x140069320 OnClosed              [6] 0x140068f70 OnUpdateLayout
//     [7] 0x140069250 OnUpdate              [8] 0x140069230 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xd8.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

class CitizenComponent;              // Component/CitizenComponent.h
class CitizenUI;                     // Dialog/CitizenUI.h
namespace UI { class TextElement; }

class FollowDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(FollowDialog)

public:
    FollowDialog();                                                 // create 0x140011a20
    FollowDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                 Object::InternalPtr<UI::DialogInstance> parent);   // 0x140069510 family

    ~FollowDialog() override = default;                            // [0] 0x140011af0
    void OnOpened() override;                                      // [4] 0x140069340
    void OnClosed() override;                                      // [5] 0x140069320
    void OnUpdateLayout() override;                                // [6] 0x140068f70
    void OnUpdate(const System::Time& time) override;              // [7] 0x140069250
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140069230

    // 0x140068f00: null the CitizenUI's back-reference, then close.
    void Close();                                                  // 0x140068f00

public:
    CitizenComponent* citizen;   // +0xB8: the followed citizen (CitizenUI's Follow button)
    CitizenUI*   citizenUI;      // +0xC0: the owning CitizenUI (back-reference)
    UI::TextElement* textFollow; // +0xC8: "textFollow" element
    UI::TextElement* textAction; // +0xD0: "textAction" element
};

static_assert(offsetof(FollowDialog, citizen) == 0xB8, "FollowDialog +0xb8");
static_assert(offsetof(FollowDialog, citizenUI) == 0xC0, "FollowDialog +0xc0");
static_assert(offsetof(FollowDialog, textFollow) == 0xC8, "FollowDialog +0xc8");
static_assert(offsetof(FollowDialog, textAction) == 0xD0, "FollowDialog +0xd0");
static_assert(sizeof(FollowDialog) == 0xD8, "FollowDialog is 0xd8 (create 0x140011a20)");
static_assert(alignof(FollowDialog) == 8, "FollowDialog is qword-aligned");
