#pragma once
// ============================================================================
// src/game/Dialog/ResidenceUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ResidenceUI", parent
//   "UI::DialogController", table 0x14015f6d0, 11 slots, sizeof 0x40.
//   create 0x140020f60: MemAlloc(0x40, 8), ??0DialogController, stamp, fields.
//   [0] 0x140020fc0 scalar deleting dtor (~ResidenceUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x1400b5db0  RET 0
//   [4] 0x1400b5850  OnCreated
//   [7] 0x1400b51f0  OnUpdateLayout
//   [8] retail inherits the base; the tree overrides it (STANDALONE DEVIATION)
//   [9] 0x1400b5820  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

namespace UI { class TextElement; }

// One resident row of the residence table (0x30 bytes, OnCreated 0x1400b5850).
struct ResidenceUIRow {
    UI::Element*     imageMale;       // +0x00
    UI::Element*     imageFemale;     // +0x08
    UI::TextElement* textName;        // +0x10
    UI::TextElement* textAge;         // +0x18
    UI::TextElement* textProfession;  // +0x20
    UI::Element*     buttonGoto;      // +0x28 element id 300 + row
};
static_assert(sizeof(ResidenceUIRow) == 0x30, "ResidenceUIRow is 0x30");
static_assert(offsetof(ResidenceUIRow, imageMale) == 0x0, "ResidenceUIRow::imageMale");
static_assert(offsetof(ResidenceUIRow, imageFemale) == 0x8, "ResidenceUIRow::imageFemale");
static_assert(offsetof(ResidenceUIRow, textAge) == 0x18, "ResidenceUIRow::textAge");
static_assert(offsetof(ResidenceUIRow, textName) == 0x10, "ResidenceUIRow +0x10");
static_assert(offsetof(ResidenceUIRow, textProfession) == 0x20, "ResidenceUIRow +0x20");
static_assert(offsetof(ResidenceUIRow, buttonGoto) == 0x28, "ResidenceUIRow +0x28");

class ResidenceUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ResidenceUI)

public:
    ResidenceUI() = default;                             // 0x140020f60 (ctor inlined in the factory)

    // 0x140020fc0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    // Retail frees the row array at +0x30: `rows`' own dtor does.
    ~ResidenceUI() override = default;                            // [0] 0x140020fc0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x1400b5db0  RET 0 in retail
    void OnCreated() override;   // [4] 0x1400b5850
    void OnUpdateLayout() override;   // [7] 0x1400b51f0
    // [8] STANDALONE DEVIATION: retail inherits DialogController::OnUpdate (RET); the tree drives this panel's per-frame refresh here.
    void OnUpdate(const System::Time& time) override;
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400b5820

private:
    void GoToCitizen(int citizenIndex);                           // 0x1400b56a0

public:

    // ---- fields -------------------------------------------------------
    // +0x30: retail's Collection::Array<ResidenceUIRow> (0x30-byte rows; OnCreated
    // 0x1400b5850 appends one per table row; ~ResidenceUI 0x140020fc0 frees it:
    // the Array's own dtor does, as the tree's teardown did).
    Collection::Array<ResidenceUIRow> rows;   // +0x30 data, +0x38 count, +0x3C capacity
};

static_assert(sizeof(ResidenceUI) == 0x40, "ResidenceUI is 0x40 (create 0x140020f60 MemAlloc(0x40))");
static_assert(alignof(ResidenceUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ResidenceUI, rows) == 0x30, "ResidenceUI::rows @ +0x30");
