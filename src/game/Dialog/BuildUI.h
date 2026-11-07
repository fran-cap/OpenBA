#pragma once
// ============================================================================
// src/game/Dialog/BuildUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "BuildUI", parent
//   "UI::DialogController", table 0x14015f210, 11 slots, sizeof 0x50.
//   create 0x1400209e0: MemAlloc(0x50, 8), ??0DialogController, stamp, fields.
//   [0] 0x140020a90 scalar deleting dtor (~BuildUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x1400b1f40  OnCreated
//   [7] 0x1400b1a50  OnUpdateLayout
//   [9] 0x1400b1ea0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array

namespace UI { class LabelElement; class TextElement; class ProgressElement; }

class BuildUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(BuildUI)

public:
    BuildUI() = default;                                 // 0x1400209e0 (ctor inlined in the factory)

    // 0x140020a90 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    // The Array member at +0x30 is freed by its own dtor, as retail.
    ~BuildUI() override = default;                                // [0] 0x140020a90
    void OnCreated() override;   // [4] 0x1400b1f40
    void OnUpdateLayout() override;   // [7] 0x1400b1a50
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400b1ea0

    // One storage-table row: its "labelStorage" and "textTotal" elements (the
    // element of the +0x30 Array, stride 0x10; OnCreated 0x1400b1f40 pushes them).
    struct StorageRow {
        UI::LabelElement* label;   // +0x00 InternalPtr<LabelElement>
        UI::TextElement*  total;   // +0x08 InternalPtr<TextElement>
    };

    // ---- fields (B4 DLG-a: named from the element strings OnCreated finds) ----
    // +0x30 Collection::Array<StorageRow> (B.4 P2 DLG-1, behaviour: H5).
    // Retail's create 0x1400209e0 zeroes +0x30 / +0x38 (0x140020a20 /
    // 0x140020a24): Array's default ctor. Retail's dtor 0x140020a90 MemFrees
    // +0x30 and zeroes +0x30 / +0x38 (0x140020ab7..0x140020ac7) before
    // ~DialogController: the member's ~Array.
    Collection::Array<StorageRow> rows;        // +0x30
    UI::ProgressElement* progress = nullptr;   // +0x40 "progress"
    UI::TextElement*     textTotal = nullptr;  // +0x48 the header row's "textTotal"
};

static_assert(sizeof(BuildUI) == 0x50, "BuildUI is 0x50 (create 0x1400209e0 MemAlloc(0x50))");
static_assert(alignof(BuildUI) == 8, "qword-aligned (the vptr)");
static_assert(sizeof(BuildUI::StorageRow) == 0x10, "BuildUI::StorageRow stride 0x10");
static_assert(offsetof(BuildUI::StorageRow, label) == 0x0, "StorageRow::label");
static_assert(offsetof(BuildUI::StorageRow, total) == 0x08, "BuildUI::StorageRow::total @ +0x08");
static_assert(offsetof(BuildUI, rows) == 0x30, "BuildUI::rows @ +0x30");
static_assert(sizeof(Collection::Array<BuildUI::StorageRow>) == 0x10, "Array is {data, count, capacity}");
static_assert(offsetof(BuildUI, progress) == 0x40, "BuildUI::progress @ +0x40");
static_assert(offsetof(BuildUI, textTotal) == 0x48, "BuildUI::textTotal @ +0x48");
