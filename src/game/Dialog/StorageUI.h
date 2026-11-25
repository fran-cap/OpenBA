#pragma once
// ============================================================================
// src/game/Dialog/StorageUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "StorageUI", parent
//   "UI::DialogController", table 0x14015f970, 11 slots, sizeof 0x40.
//   create 0x140021330: MemAlloc(0x40, 8), ??0DialogController, stamp, fields.
//   [0] 0x140021390 scalar deleting dtor (~StorageUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x1400b8ca0  RET 0
//   [4] 0x1400b8820  OnCreated
//   [7] 0x1400b8450  OnUpdateLayout
//   [8] 0x14003a96c  JMP [0x1401515f8]: DialogController's (inherited)
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

namespace UI { class Element; class LabelElement; class NumberElement; }

// One row of StorageUI's +0x30 array (stride 0x18): the two elements
// OnCreated 0x1400b8820 finds in a table cell, and the cell itself.
struct StorageUIRow {
    UI::LabelElement*  labelStorage;   // +0x00 "labelStorage": the material's sprite and name
    UI::NumberElement* numberCount;    // +0x08 "numberCount": the amount stored
    UI::Element*       cell;           // +0x10 the table cell (InternalPtr<Element>)
};
static_assert(sizeof(StorageUIRow) == 0x18, "StorageUI row stride 0x18 (0x1400b8820)");
static_assert(offsetof(StorageUIRow, labelStorage) == 0x0, "StorageUIRow::labelStorage");
static_assert(offsetof(StorageUIRow, numberCount) == 0x08, "StorageUIRow::numberCount @ +0x08");
static_assert(offsetof(StorageUIRow, cell) == 0x10, "StorageUIRow::cell @ +0x10");

class StorageUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(StorageUI)

public:
    StorageUI() = default;                               // 0x140021330 (ctor inlined in the factory)

    // 0x140021390 minus the free: ~Array on +0x30 (MemFree the data, zero the
    // header), then ~DialogController (and so ~Internal: RemoveObject).
    // [0] 0x140021390 (the body is storageui.cpp's: one EXT SEAM line)
    ~StorageUI() override;
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x1400b8ca0  RET 0 in retail
    void OnCreated() override;   // [4] 0x1400b8820
    void OnUpdateLayout() override;   // [7] 0x1400b8450
    // [9] inherited from DialogController in retail; overridden only to give ext.storage_filter's
    // tab its events
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;
    // [8] inherited: DialogController::OnUpdate (table slot 0x14003a96c jumps to the import);
    // the refresh is slot 7, which DialogInstance::ComputeLayout dispatches every frame

    // ---- fields ----------------------------------------------------------
    // +0x30 Collection::Array<StorageUIRow>: one row per table cell, appended
    // by OnCreated 0x1400b8820; freed by the dtor 0x140021390.
    Collection::Array<StorageUIRow> rows;
};

static_assert(offsetof(StorageUI, rows) == 0x30, "StorageUI::rows @ +0x30 (0x1400b8820)");
static_assert(sizeof(StorageUI) == 0x40, "StorageUI is 0x40 (create 0x140021330 MemAlloc(0x40))");
static_assert(alignof(StorageUI) == 8, "qword-aligned (the vptr)");
