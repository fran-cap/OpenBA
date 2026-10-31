#pragma once
// ============================================================================
// src/game/Dialog/OrchardUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (B38_SLOTS.md §6): reflected "OrchardUI", parent "SelectResourceUI",
//   table 0x14015cf30, 14 slots, sizeof 0x48. create 0x140012ff0 -> ctor
//   0x1400756b0: ??0DialogController, stamp 0x14015d330, InternalBasePtr
//   (+0x30, null), stamp 0x14015cf30, +0x38 = +0x40 = 0.
//   [0] 0x1400130e0 scalar deleting dtor
//   [4] 0x1400755b0  OnCreated   [7] 0x1400754b0  OnUpdateLayout
//   [8] [9] = parent (SelectResourceUI's)
//   [11] 0x140075380 SetResource  [12] 0x140075430 GetResource
//   [13] 0x140075420 GetCategory -> 1
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Dialog/SelectResourceUI.h"
#include "Object/exe_type.h"

namespace UI { class ProgressElement; }

class OrchardUI final : public SelectResourceUI {
    DECLARE_EXE_TYPE(OrchardUI)

public:
    OrchardUI() = default;                                        // 0x1400756b0

    // 0x1400130e0 minus the free: restamps + ~DialogController only
    ~OrchardUI() override = default;                              // [0]  0x1400130e0
    void  OnCreated() override;                                   // [4]  0x1400755b0
    void  OnUpdateLayout() override;                              // [7]  0x1400754b0
    // [9] STANDALONE DEVIATION: retail inherits SelectResourceUI's 0x140078d80;
    // the tree's wrapper traces the event, then runs the same base body.
    void  OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t a, int32_t b,
                         int32_t c, int32_t d, int32_t e, void* userData) override;
    void  SetResource(ComponentDescription* type) override;       // [11] 0x140075380
    ComponentDescription** GetResource(ComponentDescription** out) override;  // [12] 0x140075430
    int   GetCategory() override;                                 // [13] 0x140075420  MOV EAX,1

    // ---- fields -----------------------------------------------------------------
    UI::ProgressElement* yield = nullptr;  // +0x38: "yield" ProgressElement (InternalBasePtr, OnCreated 0x1400755b0)
    uint64_t data40 = 0;           // +0x40
};

static_assert(sizeof(OrchardUI) == 0x48, "OrchardUI is 0x48 (create 0x140012ff0 MemAlloc(0x48))");
static_assert(alignof(OrchardUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(OrchardUI, yield) == 0x38, "yield @ +0x38 (0x1400755b0)");
static_assert(offsetof(OrchardUI, data40) == 0x40, "OrchardUI +0x40");
