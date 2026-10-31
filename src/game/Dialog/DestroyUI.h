#pragma once
// ============================================================================
// src/game/Dialog/DestroyUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "DestroyUI", parent
//   "UI::DialogController", table 0x14015bb00, 11 slots, sizeof 0x78.
//   create 0x140011130: MemAlloc(0x78, 8), ??0DialogController, stamp, fields.
//   [0] 0x140011200 scalar deleting dtor (~DestroyUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x140066150  OnCreated
//   [7] 0x140065b00  OnUpdateLayout
//   [8] inherits DialogController's (as retail)
//   [9] 0x140066080  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class Element; class ProgressElement; }

class DestroyUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(DestroyUI)

public:
    DestroyUI() = default;                               // 0x140011130 (ctor inlined in the factory)

    // 0x140011200 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~DestroyUI() override = default;                              // [0] 0x140011200
    void OnCreated() override;   // [4] 0x140066150
    void OnUpdateLayout() override;   // [7] 0x140065b00
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140066080

    // ---- fields (B4 DLG-a: named from the element strings OnCreated
    // 0x140066150 finds them by; typed with the class each is read as) --------
    UI::ProgressElement* progressRemove = nullptr;    // +0x30 "progressRemove"
    UI::ProgressElement* progressDestruct = nullptr;  // +0x38 "progressDestruct"
    UI::Element* textRemove = nullptr;                // +0x40 "textRemove"
    UI::Element* textUpgrade = nullptr;               // +0x48 "textUpgrade"
    UI::Element* textDamage = nullptr;                // +0x50 "textDamage"
    UI::Element* textFixing = nullptr;                // +0x58 "textFixing"
    UI::Element* buttonReclaim = nullptr;             // +0x60 "buttonReclaim" (id 0x1771)
    UI::Element* buttonDestroy = nullptr;             // +0x68 "buttonDestroy" (id 0x1773)
    UI::Element* buttonRebuild = nullptr;             // +0x70 "buttonRebuild" (id 0x1772)
};

static_assert(sizeof(DestroyUI) == 0x78, "DestroyUI is 0x78 (create 0x140011130 MemAlloc(0x78))");
static_assert(alignof(DestroyUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(DestroyUI, progressRemove) == 0x30, "DestroyUI::progressRemove @ +0x30");
static_assert(offsetof(DestroyUI, progressDestruct) == 0x38, "DestroyUI::progressDestruct @ +0x38");
static_assert(offsetof(DestroyUI, textRemove) == 0x40, "DestroyUI::textRemove @ +0x40");
static_assert(offsetof(DestroyUI, textUpgrade) == 0x48, "DestroyUI::textUpgrade @ +0x48");
static_assert(offsetof(DestroyUI, textDamage) == 0x50, "DestroyUI::textDamage @ +0x50");
static_assert(offsetof(DestroyUI, textFixing) == 0x58, "DestroyUI::textFixing @ +0x58");
static_assert(offsetof(DestroyUI, buttonReclaim) == 0x60, "DestroyUI::buttonReclaim @ +0x60");
static_assert(offsetof(DestroyUI, buttonDestroy) == 0x68, "DestroyUI::buttonDestroy @ +0x68");
static_assert(offsetof(DestroyUI, buttonRebuild) == 0x70, "DestroyUI::buttonRebuild @ +0x70");
