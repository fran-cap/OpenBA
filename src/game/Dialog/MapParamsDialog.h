#pragma once
// ============================================================================
// src/game/Dialog/MapParamsDialog.h — ENDGAME B.3.8 block DLG
//
// The in-game "Map" dialog (name / seed / terrain / climate / condition /
// disaster of the running map).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "MapParamsDialog", parent "UI::DialogInstance", ti 0x1401c50f8
//   create 0x140012100 (MemAlloc(0xf8, 8)), createInPlace 0x140012140
//   ctors 0x14006bd80 (the door the tree registers), 0x14006be20
//   table 0x14015c180, 10 slots:
//     [0] 0x140012150 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x14006b810 OnOpened
//     [5] 0x14006b800 OnClosed `RET 0`      [6] 0x14006b790 OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x14006b5f0 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xf8: eight borrowed element pointers, all zeroed by the ctor.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

namespace UI { class EditElement; class NumberElement; class TextElement; class ButtonElement; }

class MapParamsDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(MapParamsDialog)

public:
    MapParamsDialog();                                              // create 0x140012100
    MapParamsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                    Object::InternalPtr<UI::DialogInstance> parent);  // 0x14006bd80

    ~MapParamsDialog() override = default;                         // [0] 0x140012150
    void OnOpened() override;                                      // [4] 0x14006b810
    void OnClosed() override;                                      // [5] 0x14006b800 RET
    void OnUpdateLayout() override;                                // [6] 0x14006b790
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x14006b5f0

    void ApplyName();                                              // 0x14006b690
    void GenerateName();                                           // 0x14006b700

public:
    UI::EditElement*   editName;         // +0xB8: "editName"
    UI::NumberElement* numberSeed;       // +0xC0: "numberSeed"
    UI::TextElement*   textTerrain;      // +0xC8: "textTerrain"
    UI::TextElement*   textTerrainSize;  // +0xD0: "textTerrainSize"
    UI::TextElement*   textClimate;      // +0xD8: "textClimate"
    UI::TextElement*   textCondition;    // +0xE0: "textCondition"
    UI::TextElement*   textDisaster;     // +0xE8: "textDisaster"
    UI::ButtonElement* buttonOk;         // +0xF0: "buttonOk"
};

static_assert(offsetof(MapParamsDialog, editName) == 0xB8, "MapParamsDialog +0xb8");
static_assert(offsetof(MapParamsDialog, numberSeed) == 0xC0, "MapParamsDialog +0xc0");
static_assert(offsetof(MapParamsDialog, textTerrain) == 0xC8, "MapParamsDialog +0xc8");
static_assert(offsetof(MapParamsDialog, textTerrainSize) == 0xD0, "MapParamsDialog +0xd0");
static_assert(offsetof(MapParamsDialog, textClimate) == 0xD8, "MapParamsDialog +0xd8");
static_assert(offsetof(MapParamsDialog, textCondition) == 0xE0, "MapParamsDialog +0xe0");
static_assert(offsetof(MapParamsDialog, textDisaster) == 0xE8, "MapParamsDialog +0xe8");
static_assert(offsetof(MapParamsDialog, buttonOk) == 0xF0, "MapParamsDialog +0xf0");
static_assert(sizeof(MapParamsDialog) == 0xF8, "MapParamsDialog is 0xf8 (create 0x140012100)");
static_assert(alignof(MapParamsDialog) == 8, "MapParamsDialog is qword-aligned");
