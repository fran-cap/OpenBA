#pragma once
// ============================================================================
// src/game/Dialog/WorkPlaceUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "WorkPlaceUI", parent
//   "UI::DialogController", table 0x14015e030, 11 slots, sizeof 0x68.
//   create 0x140016dc0: MemAlloc(0x68, 8), ??0DialogController, stamp, fields.
//   [0] 0x140016e90 scalar deleting dtor (~WorkPlaceUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x140087940  OnCreated
//   [7] 0x140087080  OnUpdateLayout
//   [8] retail inherits the base; the tree overrides it (STANDALONE DEVIATION)
//   [9] 0x1400877e0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class LabelElement; class TextElement; class ButtonElement; }

class WorkPlaceUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(WorkPlaceUI)

public:
    WorkPlaceUI() = default;                             // 0x140016dc0 (ctor inlined in the factory)

    // 0x140016e90 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~WorkPlaceUI() override = default;                            // [0] 0x140016e90
    void OnCreated() override;   // [4] 0x140087940
    void OnUpdateLayout() override;   // [7] 0x140087080
    // [8] STANDALONE DEVIATION: retail inherits DialogController::OnUpdate (RET); the tree drives this panel's per-frame refresh here.
    void OnUpdate(const System::Time& time) override;
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400877e0

    // 0x140087610: the jobCountUp / jobCountDown body (raise / lower the slot cap).
    void AdjustJobCount(int delta);
    // 0x1400876d0: the workPlaceButton body (cycle the roster, centre the camera).
    void CycleWorkplace();

    // ---- fields (B4 H1-H8: retail types from OnCreated 0x140087940) ----------
    // Each is an InternalPtr<T> retail narrows with IsDerivedFrom on the import
    // slot named beside it (the element type ids, perf/mintsel/*/elemtid.txt).
    UI::LabelElement*  workPlaceCount = nullptr;   // +0x30 exref 0x140151668 LabelElement
    UI::TextElement*   maxCount = nullptr;         // +0x38 exref 0x140151508 TextElement
    UI::ButtonElement* workerCountUp = nullptr;    // +0x40 exref 0x1401514f8 ButtonElement, id 0x1450
    UI::ButtonElement* workerCountDown = nullptr;  // +0x48 ButtonElement, id 0x1451
    UI::ButtonElement* jobCountUp = nullptr;       // +0x50 ButtonElement, id 0x1452
    UI::ButtonElement* jobCountDown = nullptr;     // +0x58 ButtonElement, id 0x1453
    int32_t workplaceIndex = 0;                    // +0x60 CycleWorkplace's roster cursor (0x1400876d0)
    uint8_t _pad64[4] = {};                        // +0x64: gap
};

static_assert(sizeof(WorkPlaceUI) == 0x68, "WorkPlaceUI is 0x68 (create 0x140016dc0 MemAlloc(0x68))");
static_assert(alignof(WorkPlaceUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(WorkPlaceUI, workPlaceCount)  == 0x30, "workPlaceCount +0x30 (0x1400879c8)");
static_assert(offsetof(WorkPlaceUI, maxCount)        == 0x38, "maxCount +0x38 (0x140087a3c)");
static_assert(offsetof(WorkPlaceUI, workerCountUp)   == 0x40, "workerCountUp +0x40 (0x140087ab0)");
static_assert(offsetof(WorkPlaceUI, workerCountDown) == 0x48, "workerCountDown +0x48 (0x140087b24)");
static_assert(offsetof(WorkPlaceUI, jobCountUp)      == 0x50, "jobCountUp +0x50 (0x140087b98)");
static_assert(offsetof(WorkPlaceUI, jobCountDown)    == 0x58, "jobCountDown +0x58");
static_assert(offsetof(WorkPlaceUI, workplaceIndex)  == 0x60, "workplaceIndex +0x60 (0x1400876d0)");
