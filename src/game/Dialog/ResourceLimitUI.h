#pragma once
// ============================================================================
// src/game/Dialog/ResourceLimitUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ResourceLimitUI", parent
//   "UI::DialogController", table 0x14015d1f0, 11 slots, sizeof 0x40.
//   create 0x1400136b0: MemAlloc(0x40, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400138d0 scalar deleting dtor (~ResourceLimitUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x140077970  OnCreated
//   [7] 0x1400778d0  OnUpdateLayout
//   [9] 0x1400777e0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class SpinnerElement; }

class ResourceLimitUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ResourceLimitUI)

public:
    ResourceLimitUI() = default;                         // 0x1400136b0 (ctor inlined in the factory)

    // 0x1400138d0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~ResourceLimitUI() override = default;                        // [0] 0x1400138d0
    void OnCreated() override;   // [4] 0x140077970
    void OnUpdateLayout() override;   // [7] 0x1400778d0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400777e0

    // ---- fields -------------------------------------------------------
    UI::SpinnerElement* editLimit = nullptr;   // +0x30: the "editLimit" spinner (InternalBasePtr, OnCreated 0x140077970)
    uint64_t unusedElement = 0;           // +0x38: zeroed
};

static_assert(sizeof(ResourceLimitUI) == 0x40, "ResourceLimitUI is 0x40 (create 0x1400136b0 MemAlloc(0x40))");
static_assert(alignof(ResourceLimitUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ResourceLimitUI, editLimit) == 0x30, "ResourceLimitUI +0x30");
static_assert(offsetof(ResourceLimitUI, unusedElement) == 0x38, "ResourceLimitUI +0x38");
