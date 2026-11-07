#pragma once
// ============================================================================
// src/game/Dialog/ModDetailsDialog.h — ENDGAME B.3.8 block DLG
//
// The per-mod details page opened from the mod list.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "ModDetailsDialog", parent "UI::DialogInstance", ti 0x1401c5248
//   create 0x140012790 (MemAlloc(0xc8, 8)), createInPlace 0x1400127e0
//   ctors 0x14006e3b0 / 0x14006e3e0 / 0x14006e450; ModDialog's details button
//   inlines one (+0xB8 = the mod's sprite, +0xC0 = the mod index)
//   table 0x14015c670, 10 slots:
//     [0] 0x140012890 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x14006d700 OnOpened
//     [5] DialogInstance::OnClosed          [6] DialogInstance::OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x14006d6a0 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xc8.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

namespace UI { class SpriteSheet; }

class ModDetailsDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(ModDetailsDialog)

public:
    ModDetailsDialog();                                             // create 0x140012790
    ModDetailsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                     Object::InternalPtr<UI::DialogInstance> parent,
                     UI::SpriteSheet* sprite, int32_t modIndex);    // inlined by ModDialog

    ~ModDetailsDialog() override;                                  // [0] 0x140012890
    void OnOpened() override;                                      // [4] 0x14006d700
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x14006d6a0

    // 0x14006d400 (update) / 0x14006d550 (delete): both open Dialog/ModUpdate.rsc.
    void OpenUpdateDialog(const char* rscPath);                    // 0x14006d400 / 0x14006d550

public:
    UI::SpriteSheet* spriteSheet;  // +0xB8: the mod row's thumbnail sheet (borrowed from ModDialog)
    int32_t  modIndex;             // +0xC0: index into the mod list
    int32_t  _padC4;               // +0xC4
};

static_assert(offsetof(ModDetailsDialog, spriteSheet) == 0xB8, "ModDetailsDialog +0xb8");
static_assert(offsetof(ModDetailsDialog, modIndex) == 0xC0, "ModDetailsDialog +0xc0");
static_assert(sizeof(ModDetailsDialog) == 0xC8, "ModDetailsDialog is 0xc8 (create 0x140012790)");
static_assert(offsetof(ModDetailsDialog, _padC4) == 0xC4, "ModDetailsDialog::_padC4");
static_assert(alignof(ModDetailsDialog) == 8, "ModDetailsDialog is qword-aligned");
