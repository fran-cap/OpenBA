#pragma once
// ============================================================================
// src/game/Dialog/HelpUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "HelpUI", parent
//   "UI::DialogController", table 0x14015bf30, 11 slots, sizeof 0x78.
//   create 0x140011ca0: MemAlloc(0x78, 8), ??0DialogController, stamp, fields.
//   [0] 0x140011cf0 scalar deleting dtor (~HelpUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x14006a500  OnCreated
//   [5] 0x14006a4f0  RET 0
//   [7] 0x14006a230  OnUpdateLayout
//   [9] 0x14006a290  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
//
// B.4 W1 (DLG-b): the six elements are the typed pointers OnCreated stores
// after each IsDerivedFrom check; +0x68 is the history array's header.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array

namespace UI {
class ScrollElement; class DocumentTextElement; class ComboElement;
class ButtonElement; class ContainerElement;
}

// One history entry (8 bytes, align 4): LoadPage 0x140069f20 appends
// {page, 0.0f}; the link / back / forward arms of OnElementEvent 0x14006a290
// save the scroll bar's current value into it and restore it from it.
struct HelpHistoryEntry {
    int32_t pageIndex;   // +0x00 index into the help topics ExternalList
    float   scrollY;     // +0x04 the scroll bar's GetCurrentValue()
};
static_assert(sizeof(HelpHistoryEntry) == 8, "HelpHistoryEntry is 8 (the +0x68 array stride)");
static_assert(offsetof(HelpHistoryEntry, pageIndex) == 0x0, "HelpHistoryEntry::pageIndex");
static_assert(offsetof(HelpHistoryEntry, scrollY) == 0x4, "HelpHistoryEntry::scrollY");

class HelpUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(HelpUI)

public:
    HelpUI() = default;                                  // 0x140011ca0 (ctor inlined in the factory)

    // 0x140011cf0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    // The history Array at +0x68 is freed by its member dtor before
    // ~DialogController, as retail (0x140011d17).
    ~HelpUI() override = default;                                 // [0] 0x140011cf0
    void OnCreated() override;   // [4] 0x14006a500
    void OnOpened() override {}   // [5] 0x14006a4f0  RET 0 in retail
    void OnUpdateLayout() override;   // [7] 0x14006a230
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14006a290

    void LoadPage();                                     // 0x140069f20

    // ---- fields: OnCreated 0x14006a500 fills the six elements ----------
    Object::InternalPtr<UI::ScrollElement>       scroll;         // +0x30 the page's scroll bar
    Object::InternalPtr<UI::DocumentTextElement> document;       // +0x38 id 0xE75
    Object::InternalPtr<UI::ComboElement>        comboTopics;    // +0x40 id 0xE74
    Object::InternalPtr<UI::ButtonElement>       buttonBack;     // +0x48 "buttonBack", id 0xE76
    Object::InternalPtr<UI::ButtonElement>       buttonForward;  // +0x50 "buttonForward", id 0xE77
    Object::InternalPtr<UI::ContainerElement>    container;      // +0x58 scroll offsets
    int32_t currentPage = -1;      // +0x60 } the factory writes one qword -1
    int32_t historyIndex = -1;     // +0x64 }
    // +0x68: the history, Collection::Array<HelpHistoryEntry> (B.4 P2 DLG-1,
    // behaviour: H5). Retail's ctor 0x14006a920 zeroes +0x68 / +0x70
    // (0x14006a969 / 0x14006a96d): Array's default ctor. Retail's dtor
    // 0x140011cf0 MemFrees +0x68 and zeroes +0x68 / +0x70 (0x140011d17..0x140011d27)
    // before ~DialogController: the member's ~Array.
    Collection::Array<HelpHistoryEntry> history;   // +0x68
};

static_assert(sizeof(HelpUI) == 0x78, "HelpUI is 0x78 (create 0x140011ca0 MemAlloc(0x78))");
static_assert(alignof(HelpUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(HelpUI, scroll) == 0x30, "HelpUI::scroll @ 0x30");
static_assert(offsetof(HelpUI, document) == 0x38, "HelpUI::document @ 0x38");
static_assert(offsetof(HelpUI, comboTopics) == 0x40, "HelpUI::comboTopics @ 0x40");
static_assert(offsetof(HelpUI, buttonBack) == 0x48, "HelpUI::buttonBack @ 0x48");
static_assert(offsetof(HelpUI, buttonForward) == 0x50, "HelpUI::buttonForward @ 0x50");
static_assert(offsetof(HelpUI, container) == 0x58, "HelpUI::container @ 0x58");
static_assert(offsetof(HelpUI, currentPage) == 0x60, "HelpUI::currentPage @ 0x60");
static_assert(offsetof(HelpUI, historyIndex) == 0x64, "HelpUI::historyIndex @ 0x64");
static_assert(offsetof(HelpUI, history) == 0x68, "HelpUI::history @ 0x68");
static_assert(sizeof(Collection::Array<HelpHistoryEntry>) == 0x10, "Array is {data, count, capacity}");
