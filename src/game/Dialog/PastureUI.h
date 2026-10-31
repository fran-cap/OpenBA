#pragma once
// ============================================================================
// src/game/Dialog/PastureUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (B38_SLOTS.md §6): reflected "PastureUI", parent "SelectResourceUI",
//   table 0x14015f610, 14 slots, sizeof 0x40. create 0x140020e10 -> ctor
//   0x1400b51a0: ??0DialogController, stamp 0x14015d330, InternalBasePtr
//   (+0x30, null), stamp 0x14015f610, +0x38 = 0.
//   [0] 0x140020ee0 scalar deleting dtor
//   [4] 0x1400b5090  OnCreated   [7] 0x1400b4e50  OnUpdateLayout
//   [8] = parent (SelectResourceUI's 0x140078d30)
//   [9] 0x1400b4e20  OnElementEvent (combo pick + the livestock slider)
//   [11] 0x1400b4c40 SetResource  [12] 0x1400b4d00 GetResource
//   [13] 0x1400b4cf0 GetCategory -> 2
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Dialog/SelectResourceUI.h"
#include "Object/exe_type.h"

namespace UI { class SliderElement; }

class PastureUI final : public SelectResourceUI {
    DECLARE_EXE_TYPE(PastureUI)

public:
    PastureUI() = default;                                        // 0x1400b51a0

    // 0x140020ee0 minus the free: restamps + ~DialogController only
    ~PastureUI() override = default;                              // [0]  0x140020ee0
    void  OnCreated() override;                                   // [4]  0x1400b5090
    void  OnUpdateLayout() override;                              // [7]  0x1400b4e50
    void  OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t a, int32_t b,
                         int32_t c, int32_t d, int32_t e, void* userData) override;  // [9] 0x1400b4e20
    void  SetResource(ComponentDescription* type) override;       // [11] 0x1400b4c40
    ComponentDescription** GetResource(ComponentDescription** out) override;  // [12] 0x1400b4d00
    int   GetCategory() override;                                 // [13] 0x1400b4cf0  MOV EAX,2

private:
    void  UpdateSlider();                                         // 0x1400b4fa0

public:
    // ---- fields -----------------------------------------------------------------
    UI::SliderElement* slider = nullptr;   // +0x38: the livestock "slider" (InternalBasePtr)
};

static_assert(sizeof(PastureUI) == 0x40, "PastureUI is 0x40 (create 0x140020e10 MemAlloc(0x40))");
static_assert(alignof(PastureUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(PastureUI, slider) == 0x38, "slider @ +0x38 (0x1400b5090)");
