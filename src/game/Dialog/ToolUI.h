#pragma once
// ============================================================================
// src/game/Dialog/ToolUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "ToolUI", parent
//   "UI::DialogController", table 0x14015da70, 11 slots, sizeof 0x58.
//   create 0x140014580: MemAlloc(0x58, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400145f0 scalar deleting dtor (~ToolUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x14007c070  RET 0
//   [4] 0x14007bd70  OnCreated
//   [8] 0x14007b9b0  OnUpdate
//   [9] 0x14007bc30  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

class ComponentDescription;
namespace UI { class TextElement; }

class ToolUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(ToolUI)

public:
    ToolUI() = default;                                  // 0x140014580 (ctor inlined in the factory)

    // 0x1400145f0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~ToolUI() override = default;                                 // [0] 0x1400145f0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x14007c070  RET 0 in retail
    void OnCreated() override;   // [4] 0x14007bd70
    void OnUpdate(const System::Time& time) override;   // [8] 0x14007b9b0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14007bc30

    // ---- fields (OnCreated 0x14007bd70, OnUpdate 0x14007b9b0, OnElementEvent
    //      0x14007bc30; EntityUI 0x1400670d0 / 0x140066ad0 write +0x40 / +0x4C) ---
    // +0x30 the description the panel shows: the data source itself when it is a
    // ComponentDescription, else the ComponentEntity's _description. Also the
    // key OnUpdate / OnElementEvent look up in the tool manager's +0x50 set.
    ComponentDescription* description = nullptr;
    UI::TextElement* labelTitle = nullptr;   // +0x38 "labelTitle" (SetText of the ToolbarDescription name)
    // +0x40 PINNED: a mouse-down on the title drags at once; set by OnCreated
    // when the dialog has a "borderTitle", by OnUpdate when a pending drag
    // passes 5 px, and by EntityUI's pin check box (0x1400670d0); cleared by
    // OnCreated without a "borderTitle" and by EntityUI's unpin.
    uint8_t pinned = 0;
    uint8_t dragging = 0;         // +0x41: the dialog follows the mouse (OnUpdate)
    uint8_t _pad42[2] = {};        // +0x42: gap
    int32_t dragOffsetX = 0;      // +0x44: mouse offset from dialog left
    int32_t dragOffsetY = 0;      // +0x48: mouse offset from dialog top
    // +0x4C a mouse-down on an unpinned panel arms a pending drag (+0x4D) that
    // pins it: cleared by OnCreated, set every frame by EntityUI (0x140066ad0).
    uint8_t pinOnDrag = 0;
    uint8_t isDragPending = 0;    // +0x4D: waiting for mouse movement threshold
    uint8_t _pad4E[2] = {};        // +0x4E: gap
    int32_t dragStartX = 0;       // +0x50: drag start mouse X
    int32_t dragStartY = 0;       // +0x54: drag start mouse Y
};

static_assert(offsetof(ToolUI, description) == 0x30, "ToolUI::description @ +0x30 (0x14007bd70)");
static_assert(offsetof(ToolUI, labelTitle) == 0x38, "ToolUI::labelTitle @ +0x38 (0x14007bd70)");
static_assert(offsetof(ToolUI, pinned) == 0x40, "ToolUI::pinned @ +0x40 (0x14007bc30)");
static_assert(offsetof(ToolUI, dragging) == 0x41, "ToolUI::dragging @ +0x41 (0x14007b9b0)");
static_assert(offsetof(ToolUI, dragOffsetX) == 0x44, "ToolUI::dragOffsetX @ +0x44");
static_assert(offsetof(ToolUI, dragOffsetY) == 0x48, "ToolUI::dragOffsetY @ +0x48");
static_assert(offsetof(ToolUI, pinOnDrag) == 0x4C, "ToolUI::pinOnDrag @ +0x4C (0x14007bc30)");
static_assert(offsetof(ToolUI, isDragPending) == 0x4D, "ToolUI::isDragPending @ +0x4D");
static_assert(offsetof(ToolUI, dragStartX) == 0x50, "ToolUI::dragStartX @ +0x50");
static_assert(offsetof(ToolUI, dragStartY) == 0x54, "ToolUI::dragStartY @ +0x54");
static_assert(sizeof(ToolUI) == 0x58, "ToolUI is 0x58 (create 0x140014580 MemAlloc(0x58))");
static_assert(alignof(ToolUI) == 8, "qword-aligned (the vptr)");
