#pragma once
// ============================================================================
// src/game/Dialog/PageToggleUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "PageToggleUI", parent
//   "UI::DialogController", table 0x14015cfb0, 11 slots, sizeof 0x48.
//   create 0x140013160: MemAlloc(0x48, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400131c0 scalar deleting dtor (~PageToggleUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x1400759a0  RET 0
//   [4] 0x140075850  OnCreated
//   [7] 0x140075780  OnUpdateLayout
//   [9] 0x140075710  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class CheckElement; class PageElement; }

class PageToggleUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(PageToggleUI)

public:
    PageToggleUI() = default;                            // 0x140013160 (ctor inlined in the factory)

    // 0x1400131c0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~PageToggleUI() override = default;                           // [0] 0x1400131c0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x1400759a0  RET 0 in retail
    void OnCreated() override;   // [4] 0x140075850
    void OnUpdateLayout() override;   // [7] 0x140075780
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140075710

    // ---- fields (OnCreated 0x140075850 fills them) --------------------
    UI::CheckElement* checkBox = nullptr;  // +0x30: the dialog's CheckElement (FindElementType), id 700
    UI::PageElement*  pages = nullptr;     // +0x38: the "pages" PageElement
    int32_t savedPage = 0;                 // +0x40: 0 or 4, the page the check box selects (u32 write)
    uint8_t _pad44[4] = {};                // +0x44: gap
};

static_assert(sizeof(PageToggleUI) == 0x48, "PageToggleUI is 0x48 (create 0x140013160 MemAlloc(0x48))");
static_assert(alignof(PageToggleUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(PageToggleUI, checkBox) == 0x30, "PageToggleUI +0x30");
static_assert(offsetof(PageToggleUI, pages) == 0x38, "PageToggleUI +0x38");
static_assert(offsetof(PageToggleUI, savedPage) == 0x40, "PageToggleUI +0x40");
