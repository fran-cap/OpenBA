#pragma once
// ============================================================================
// src/game/Dialog/ImmigrationUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ImmigrationUI", parent
//   "UI::DialogController", table 0x14015c080, 11 slots, sizeof 0x50.
//   create 0x140011ee0: MemAlloc(0x50, 8), ??0DialogController, stamp, fields.
//   [0] 0x140011fd0 scalar deleting dtor (~ImmigrationUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x14006b330  OnCreated
//   [7] 0x14006b130  OnUpdateLayout
//   [9] 0x14006b300  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class TextElement; }

class ImmigrationUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ImmigrationUI)

public:
    ImmigrationUI() = default;                           // 0x140011ee0 (ctor inlined in the factory)

    // 0x140011fd0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~ImmigrationUI() override = default;                          // [0] 0x140011fd0
    void OnCreated() override;   // [4] 0x14006b330
    void OnUpdateLayout() override;   // [7] 0x14006b130
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14006b300

    // ---- fields (OnCreated 0x14006b330 finds them) ------------------------
    UI::TextElement* textNone = nullptr;     // +0x30 shown when no nomads wait
    UI::TextElement* textNomad = nullptr;    // +0x38 "%i nomads ... %s"
    UI::Element*     buttonAllow = nullptr;  // +0x40 id 39000
    UI::Element*     buttonDeny = nullptr;   // +0x48 id 0x9859
};

static_assert(offsetof(ImmigrationUI, textNone) == 0x30, "ImmigrationUI +0x30");
static_assert(offsetof(ImmigrationUI, textNomad) == 0x38, "ImmigrationUI +0x38");
static_assert(offsetof(ImmigrationUI, buttonAllow) == 0x40, "ImmigrationUI +0x40");
static_assert(offsetof(ImmigrationUI, buttonDeny) == 0x48, "ImmigrationUI +0x48");

static_assert(sizeof(ImmigrationUI) == 0x50, "ImmigrationUI is 0x50 (create 0x140011ee0 MemAlloc(0x50))");
static_assert(alignof(ImmigrationUI) == 8, "qword-aligned (the vptr)");
