#pragma once
// ============================================================================
// src/game/Dialog/EventLogUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "EventLogUI", parent
//   "UI::DialogController", table 0x14015bc40, 11 slots, sizeof 0x80.
//   create 0x1400113e0: MemAlloc(0x80, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400114b0 scalar deleting dtor (~EventLogUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [5] 0x140067640  OnOpened
//   [6] 0x140067630  RET 0
//   [7] 0x140067440  OnUpdateLayout
//   [9] 0x1400675d0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
//
// B.4 W1 (DLG-b): the three buttons and the three element arrays OnOpened
// 0x140067640 fills. The arrays are retail's Collection::Array<InternalPtr<
// Element>> (0x1400114b0 frees all three), declared as such since B.4 P2 DLG-1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array
#include "object/Internal.h"         // Object::InternalPtr

class EventLogUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(EventLogUI)

public:
    EventLogUI() = default;                              // 0x1400113e0 (ctor inlined in the factory)

    // 0x1400114b0 minus the free (its body 0x140067b50). The compiler runs the
    // three Arrays' dtors (+0x70, +0x60, +0x50, as retail) and chains
    // ~DialogController (and so ~Internal: RemoveObject).
    ~EventLogUI() override = default;                             // [0] 0x1400114b0
    void OnOpened() override;   // [5] 0x140067640
    void OnClosed() override {}   // [6] 0x140067630  RET 0 in retail
    void OnUpdateLayout() override;   // [7] 0x140067440
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400675d0

    // 0x140067320: a notification check box's state into the event log's filter.
    void ToggleNotification(int32_t index);                       // 0x140067320
    // 0x140067370: centre the camera on display row `row`'s event.
    void NavigateToRow(int32_t row);                              // 0x140067370

    // ---- fields: OnOpened 0x140067640 ----------------------------------
    Object::InternalPtr<UI::Element> buttonBottom;     // +0x30 "buttonBottom", id 0x66
    Object::InternalPtr<UI::Element> buttonPrevious;   // +0x38 "buttonPrevious", id 100
    Object::InternalPtr<UI::Element> buttonNext;       // +0x40 "buttonNext", id 0x65
    int32_t scrollOffset = 0;                          // +0x48 rows scrolled back from the newest
    int32_t _pad4C = 0;                                // +0x4C
    // B.4 P2 DLG-1 (behaviour: H5): the three arrays are Collection::Array
    // members. Retail's create 0x1400113e0 zeroes +0x50..+0x7F (qwords
    // 0x14001142c..0x140011440) = Array's default ctor; retail's dtor 0x140067b50
    // frees +0x70, then +0x60, then +0x50 (MemFree data; qword 0 over data and
    // over count|capacity) = the members' ~Array in reverse declaration order.
    using ElementArray = Collection::Array<Object::InternalPtr<UI::Element>>;
    ElementArray displayTexts;     // +0x50 the "display" rows (text)
    ElementArray displayButtons;   // +0x60 the same rows as buttons (ids 0x6C + i)
    ElementArray notifyButtons;    // +0x70 "notifybuttons"' check boxes (ids 0x67 + i)
};

static_assert(sizeof(EventLogUI) == 0x80, "EventLogUI is 0x80 (create 0x1400113e0 MemAlloc(0x80))");
static_assert(offsetof(EventLogUI, _pad4C) == 0x4C, "EventLogUI::_pad4C");
static_assert(alignof(EventLogUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(EventLogUI, buttonBottom) == 0x30, "EventLogUI::buttonBottom @ 0x30");
static_assert(offsetof(EventLogUI, buttonPrevious) == 0x38, "EventLogUI::buttonPrevious @ 0x38");
static_assert(offsetof(EventLogUI, buttonNext) == 0x40, "EventLogUI::buttonNext @ 0x40");
static_assert(offsetof(EventLogUI, scrollOffset) == 0x48, "EventLogUI::scrollOffset @ 0x48");
static_assert(offsetof(EventLogUI, displayTexts) == 0x50, "EventLogUI::displayTexts @ 0x50");
static_assert(offsetof(EventLogUI, displayButtons) == 0x60, "EventLogUI::displayButtons @ 0x60");
static_assert(offsetof(EventLogUI, notifyButtons) == 0x70, "EventLogUI::notifyButtons @ 0x70");
static_assert(sizeof(EventLogUI::ElementArray) == 0x10, "Array is {data, count, capacity}");
