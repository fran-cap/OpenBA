#pragma once
// ============================================================================
// src/game/Dialog/ConsumeProduceUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ConsumeProduceUI", parent
//   "UI::DialogController", table 0x14015b2e0, 11 slots, sizeof 0x40.
//   create 0x140010ae0: MemAlloc(0x40, 8), ??0DialogController, stamp, fields.
//   [0] 0x140010b90 scalar deleting dtor (~ConsumeProduceUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x14005f3d0  OnCreated
//   [7] 0x14005f1f0  OnUpdateLayout
//   [9] 0x14005f330  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class ComboElement; class ProgressElement; }

class ConsumeProduceUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ConsumeProduceUI)

public:
    ConsumeProduceUI() = default;                        // 0x140010ae0 (ctor inlined in the factory)

    // 0x140010b90 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~ConsumeProduceUI() override = default;                       // [0] 0x140010b90
    void OnCreated() override;   // [4] 0x14005f3d0
    void OnUpdateLayout() override;   // [7] 0x14005f1f0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14005f330

    // ---- fields (B4 DLG-a: the element strings OnCreated 0x14005f3d0 finds) ----
    UI::ComboElement*    comboProduct = nullptr;       // +0x30 "comboProduct" (id 22000)
    UI::ProgressElement* progressRemaining = nullptr;  // +0x38 "progressRemaining"
};

static_assert(sizeof(ConsumeProduceUI) == 0x40, "ConsumeProduceUI is 0x40 (create 0x140010ae0 MemAlloc(0x40))");
static_assert(alignof(ConsumeProduceUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ConsumeProduceUI, comboProduct) == 0x30, "ConsumeProduceUI::comboProduct @ +0x30");
static_assert(offsetof(ConsumeProduceUI, progressRemaining) == 0x38, "ConsumeProduceUI::progressRemaining @ +0x38");
