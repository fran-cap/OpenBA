#pragma once
// ============================================================================
// src/game/Dialog/ControlUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ControlUI", parent
//   "UI::DialogController", table 0x14015b380, 11 slots, sizeof 0x40.
//   create 0x140010c00: MemAlloc(0x40, 8), ??0DialogController, stamp, fields.
//   [0] 0x140010e20 scalar deleting dtor (~ControlUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x14005f9b0  OnCreated
//   [7] 0x14005f870  OnUpdateLayout
//   [9] 0x14005f7f0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

class Component;
namespace UI { class ButtonElement; }

class ControlUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ControlUI)

public:
    ControlUI() = default;                               // 0x140010c00 (ctor inlined in the factory)

    // 0x140010e20 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~ControlUI() override = default;                              // [0] 0x140010e20
    void OnCreated() override;   // [4] 0x14005f9b0
    void OnUpdateLayout() override;   // [7] 0x14005f870
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14005f7f0

    // ---- fields (B4 DLG-a; OnCreated 0x14005f9b0 fills both) --------
    UI::ButtonElement* button = nullptr;   // +0x30 the container, if a ButtonElement (id 50000)
    Component*         target = nullptr;   // +0x38 the entity's component of the config's type
};

static_assert(sizeof(ControlUI) == 0x40, "ControlUI is 0x40 (create 0x140010c00 MemAlloc(0x40))");
static_assert(alignof(ControlUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ControlUI, button) == 0x30, "ControlUI::button @ +0x30");
static_assert(offsetof(ControlUI, target) == 0x38, "ControlUI::target @ +0x38");
