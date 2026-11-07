#pragma once
// ============================================================================
// src/game/Dialog/AutoSaveDialog.h — ENDGAME B.3.8 block DLG
//
// The invisible in-game dialog whose timer drives autosave, and whose two
// images spin while a save or load runs.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "AutoSaveDialog", parent "UI::DialogInstance", ti 0x1401c4d08
//   create 0x1400106c0 (MemAlloc(0xd0, 8)), createInPlace 0x140010730
//   ctor 0x14005e230 (the door the tree registers), 0x14005e200, 0x14005e320
//   table 0x14015b120, 10 slots:
//     [0] 0x140010780 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)
//     [4] 0x14005e110 OnOpened = JMP 0x14005e120 (restart the autosave timer)
//     [5] DialogInstance::OnClosed          [6] 0x14005dfc0 OnUpdateLayout
//     [7] 0x14005e090 OnUpdate              [8] 0x14005e0f0 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xd0.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

namespace UI { class ImageElement; }

class AutoSaveDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(AutoSaveDialog)

public:
    AutoSaveDialog();                                               // create 0x1400106c0
    AutoSaveDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                   Object::InternalPtr<UI::DialogInstance> parent); // 0x14005e230

    ~AutoSaveDialog() override = default;                          // [0] 0x140010780
    void OnOpened() override;                                      // [4] 0x14005e110 -> e120
    // [5] OnClosed: DialogInstance's (SaveSettings), inherited (B38 D3.1).
    void OnUpdateLayout() override;                                // [6] 0x14005dfc0
    void OnUpdate(const System::Time& time) override;              // [7] 0x14005e090
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x14005e0f0

    // 0x14005dec0 — the timer-event body: start an autosave (SaveLoad operation 4)
    // unless the UI pauses the game, the game is blocked or an operation runs.
    void Autosave();

public:
    UI::ImageElement* savingImage;  // +0xB8: FindElement("savingImage"), rotated by OnUpdateLayout
    UI::Element*   animatedImage;  // +0xC0: FindElement("animatedImage")
    float    angle;            // +0xC8: spinner angle, degrees
    uint8_t  _padCC[4];        // +0xCC
};

static_assert(offsetof(AutoSaveDialog, savingImage) == 0xB8, "AutoSaveDialog +0xb8");
static_assert(offsetof(AutoSaveDialog, animatedImage) == 0xC0, "AutoSaveDialog +0xc0");
static_assert(offsetof(AutoSaveDialog, angle) == 0xC8, "AutoSaveDialog +0xc8");
static_assert(sizeof(AutoSaveDialog) == 0xD0, "AutoSaveDialog is 0xd0 (create 0x1400106c0)");
static_assert(offsetof(AutoSaveDialog, _padCC) == 0xCC, "AutoSaveDialog::_padCC");
static_assert(alignof(AutoSaveDialog) == 8, "AutoSaveDialog is qword-aligned");

// The slot-4 door (autosavedialog.cpp): registered at 0x14005e120 / 0x14005e110.
void AutoSaveDialog_Slot4(AutoSaveDialog* self);
