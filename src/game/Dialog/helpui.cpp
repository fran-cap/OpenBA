// engine/src/ui/helpui.cpp
// Clean reimplementation of HelpUI game logic.
//
// HelpUI (0x78 bytes) — help page navigation panel.
// Extends DialogController (0x30 base). Fields: HelpUI.h.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdio>
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ContainerElement.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ComboElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/DocumentTextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ScrollElement.h"   // X::TypeId() (ENDGAME B.3.9)
#include "ui/ButtonElement.h"   // X::TypeId() (ENDGAME B.3.9)
#include "collection/Collection.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// File-local helpers
// ============================================================

// Get the help topics ExternalList from the game world singleton
// (retail: `*(ExternalList**)(g_pGameWorldSingleton + 0xa0)`, verified against the
// disassembly of 0x140069f38 — the singleton IS the GameUpdateLoop at .rdata
// 0x1401c3dd8). Standalone deviation: the singleton is null before the game loop
// exists (and the list itself can be null if the help resources never loaded), so
// return null rather than fault; every caller below treats null as "no topics".
static ::Object::ExternalList* GetHelpExternalList() {
    if (!GameUpdateLoop::_singleton) return nullptr;
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (!world) return nullptr;
    return world->m_helpTopics;
}

// The topic StringTable at `index` (null when the resource is not one).
static UI::StringTable* HelpTopicAt(::Object::ExternalList* extList, int index) {
    ::Object::External* entry = extList->_resources[index];
    bool isStringTable = ::Object::IsDerivedFromNullSafe(entry, UI::StringTable::TypeId());
    return isStringTable ? static_cast<UI::StringTable*>(entry) : nullptr;
}


// ============================================================
// HelpUI::LoadPage (FUN_140069f20) — Load help page content
// ============================================================
// Gets the currently selected combo item, looks up the corresponding
// StringTable resource, sets its content (text index 2) on the document
// element, resets scroll to top, and pushes a history entry.
//
// Original: 0x140069f20.

void HelpUI::LoadPage() {
    ::Object::ExternalList* extList = GetHelpExternalList();

    UI::ComboElement* comboElem = comboTopics.Get();

    // Standalone guard (retail has none — its .rsc always yields both elements and
    // the world always owns the help list). If an element lookup failed (a null
    // element type-id makes FindElementType store null even when the element
    // exists) this would deref null at combo+0x118 / inside SetText.
    if (!extList || !comboElem || !document.Get() || !container.Get())
        return;
    int currentItem = comboElem->_currentItem;
    void* itemData = comboElem->GetItemData(currentItem);
    int pageResIndex = (int)(intptr_t)itemData;  // CAST-OK: ptr_int

    // Look up StringTable resource from ExternalList (IsDerivedFrom StringTable)
    UI::StringTable* validTable = HelpTopicAt(extList, pageResIndex);

    // UI::String with text index 2 (help content body)
    UI::String uiString(Object::ExternalPtr<UI::StringTable>(validTable), 2);
    uiString.pad0C = 0;   // built in a zeroed 16-byte slot before

    document->SetText(&uiString);

    // Reset scroll offsets to (0, 0)
    container->SetOffsets(0, 0);

    currentPage = pageResIndex;

    // History: truncate (was HelpUI_ArrayResize_Impl, FUN_140011d50) and append.
    // retail's helper passed 4 — not 8 — as MemAlloc's alignment argument for this
    // array (alignof(HelpHistoryEntry)), and it left a GROWN tail uninitialised,
    // which the trivially default-constructible element reproduces.
    Collection::Array<HelpHistoryEntry>& hist = history;

    // Truncate history to (historyIndex + 1) entries — removes forward history.
    hist.SetSize(historyIndex + 1);

    // Push new history entry: {pageResIndex, scrollY=0.0f} at the new end.
    hist.SetSize(hist.GetSize() + 1, HelpHistoryEntry{pageResIndex, 0.0f});

    historyIndex = hist.GetSize() - 1;
}

// ============================================================
// HelpUI_FindLinkTarget (FUN_14006a140) — Find page by link text
// ============================================================
// Iterates the help topics ExternalList, gets text at index 0
// (topic key) from each StringTable, compares with the search
// string. Returns the matching resource index, or -1 if not found.
//
// Takes ownership of searchStr (by value) and destroys it on return — the RAII
// destructor at scope exit reproduces retail's manual release on every path.
// It reads no field of the HelpUI retail passes in RCX: stays free.
//
// Original: 0x14006a140.
static int HelpUI_FindLinkTarget(System::String searchStr) {
    ::Object::ExternalList* extList = GetHelpExternalList();
    if (!extList) { return -1; }   // standalone guard
    int count = extList->GetResourceCount();

    for (int i = 0; i < count; i++) {
        ::Object::External* entry = extList->_resources[i];

        bool isStringTable = ::Object::IsDerivedFromNullSafe(entry, UI::StringTable::TypeId());
        if (isStringTable && entry != nullptr) {
            // GetText at index 0 = topic key/name for matching (non-trivial return)
            System::String nameStr(static_cast<const UI::StringTable*>(entry)->GetText(0));

            bool match = (searchStr == nameStr);

            if (match) {
                return i;
            }
        }

        // Re-check count each iteration (matches original)
        count = extList->GetResourceCount();
    }

    return -1;
}

// ---- HelpUI: PROMOTED to the real reimpl (help-dialog bring-up 2026-07-31) ----
// The toolbar "help" utility dialog (Template\UtilityHelp.rsc -> StandardDialog.rsc,
// content Dialog/Help.rsc).
//
// Slot map, read out of the retail vtable at exe .rdata 0x14015bf30 (qword N =
// slot N), NOT from our labels:
//   slot 4 (+0x20) 0x14006a500 OnCreated — find the 4 typed elements + the two
//                                       named buttons, populate the topic combo from
//                                       the help ExternalList, tail into LoadPage.
//   slot 5 (+0x28) 0x14006a4f0        — 3-byte RET no-op.
//   slot 7 (+0x38) 0x14006a230 OnUpdateLayout — enable/disable back+forward per history index.
//   slot 9 (+0x48) 0x14006a290 OnElementEvent — combo change (msg 0x14), document link click
//                                       and back/forward (msg 0x12).
// OnElementEvent's 7th parameter is the clicked link index: retail reads it from
// [RSP+0xb0] at 0x14006a2ed, which is arg 7 in this frame.
//
// ── Construction deviation ───────────────────────────────────────────────────
// HelpUI_Ctor (0x14006a920) zeroes +0x30..+0x58 and +0x68/+0x70 but writes
// **-1** to the qword at +0x60, i.e. BOTH the current page index (+0x60) and the
// history index (+0x64) start at -1. A plain zero-fill is not equivalent: LoadPage
// truncates the history to (historyIndex + 1) entries, so a 0 index leaves one
// uninitialised history slot in front of the first real page and the back button
// then navigates to a garbage page id. The member initialisers write the -1.
namespace helpui_sa {
    // ENDGAME B.3.9: UI::ScrollElement / UI::ComboElement / UI::DocumentTextElement /
    // UI::ContainerElement / UI::ButtonElement / UI::StringTable all have a ported
    // TypeId() now (class statics, always populated), so the EnsureTypeIds() fill
    // from Runtime exports and the Ready() pointer checks over them are dead.
    // B3R: the function-pointer readiness over the engine_funcs doors was always true
    // and is gone; the allocator singleton (the old g_pMemAllocSingleton snapshot)
    // is read live.
    bool Ready() {
        return System::MemoryAllocator::_singleton != nullptr;
    }
    // Report the OUTCOME of the readiness test, once, naming what is missing.
    bool CheckOnce() {
        static int s_ok = -1;
        if (s_ok >= 0) return s_ok != 0;
        s_ok = Ready() ? 1 : 0;
        if (!s_ok) {
            fprintf(stderr, "[HelpUI] slots stay no-op — unresolved engine function deps\n");
            fflush(stderr);
        }
        return s_ok != 0;
    }
    void Trace([[maybe_unused]] const char* slot, [[maybe_unused]] HelpUI* self) {
        static int n=0; if (n>=24) return; ++n;
        GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
        [[maybe_unused]] ::Object::ExternalList* topics = gw ? gw->m_helpTopics : nullptr;
        printf("[HelpUI] %s self=%p root(+0x20)=%p scroll=%p doc=%p combo=%p back=%p fwd=%p "
               "cont=%p page=%d hist=%d/%d topics=%p n=%d\n", slot, self,
               self->_container, self->scroll.Get(),
               self->document.Get(), self->comboTopics.Get(),
               self->buttonBack.Get(), self->buttonForward.Get(),
               self->container.Get(), self->currentPage,
               self->historyIndex, self->history._size, topics,
               topics
                   ? topics->GetResourceCount()
                   : -1);
        fflush(stdout);
    }
}

// ============================================================
// HelpUI::OnCreated (vtable +0x20) — Setup / initialization
// ============================================================
// Finds typed UI elements from the dialog:
//   - ScrollElement -> scroll
//   - DocumentTextElement -> document, ID=0xE75
//   - ComboElement -> comboTopics, ID=0xE74
//   - ContainerElement -> container
// Finds named elements:
//   - "buttonBack" -> buttonBack, ID=0xE76
//   - "buttonForward" -> buttonForward, ID=0xE77
// Populates combo with help topic names from ExternalList.
// Calls LoadPage to display the first topic.
//
// Original: 0x14006A500.

// [4] 0x14006a500
void HelpUI::OnCreated() {
    if (!helpui_sa::CheckOnce()) return;
    // Setup is not idempotent: a second run appends every topic to the combo
    // again and pushes a second history entry. The framework dispatches it once
    // per dialog build; skip if the element cache is already populated.
    if (comboTopics.Get()) {
        helpui_sa::Trace("Slot4-skip(already set up)", this);
        return;
    }
    UI::Element* dialogElem = _container;

    // FindElementType calls use different type IDs from _typeInfo_exref.
    // Each finds a different typed element in the dialog tree.
    // Order matches original: scroll first, then combo, document, container.
    UI::Element* elem;
    bool derived;

    // 1. ScrollElement (type 0x1401517e8)
    elem = dialogElem->FindElementType(UI::ScrollElement::TypeId()).Get();
    derived = ::Object::IsDerivedFromNullSafe(elem, UI::ScrollElement::TypeId());
    scroll = Object::InternalPtr<UI::ScrollElement>(derived ? static_cast<UI::ScrollElement*>(elem) : nullptr);

    // 2. ComboElement (type 0x140151690)
    elem = dialogElem->FindElementType(UI::ComboElement::TypeId()).Get();
    derived = ::Object::IsDerivedFromNullSafe(elem, UI::ComboElement::TypeId());
    comboTopics = Object::InternalPtr<UI::ComboElement>(derived ? static_cast<UI::ComboElement*>(elem) : nullptr);

    // 3. DocumentTextElement (type 0x1401517f0)
    elem = dialogElem->FindElementType(UI::DocumentTextElement::TypeId()).Get();
    derived = ::Object::IsDerivedFromNullSafe(elem, UI::DocumentTextElement::TypeId());
    document = Object::InternalPtr<UI::DocumentTextElement>(derived ? static_cast<UI::DocumentTextElement*>(elem) : nullptr);

    // 4. ContainerElement (type 0x1401517f8)
    elem = dialogElem->FindElementType(UI::ContainerElement::TypeId()).Get();
    derived = ::Object::IsDerivedFromNullSafe(elem, UI::ContainerElement::TypeId());
    container = Object::InternalPtr<UI::ContainerElement>(derived ? static_cast<UI::ContainerElement*>(elem) : nullptr);

    // Set element IDs on combo and document elements
    // (standalone guard: SetElementId derefs its `this`, so skip a failed lookup)
    if (comboTopics.Get()) comboTopics->SetElementId(0xE74);
    if (document.Get()) document->SetElementId(0xE75);

    // Find "buttonBack" by name, set ID 0xE76
    UI::Element* backBtn;
    {
        System::String nameStr("buttonBack");
        backBtn = dialogElem->FindElement(nameStr).Get();
    }
    derived = ::Object::IsDerivedFromNullSafe(backBtn, UI::ButtonElement::TypeId());
    buttonBack = Object::InternalPtr<UI::ButtonElement>(derived ? static_cast<UI::ButtonElement*>(backBtn) : nullptr);

    // Find "buttonForward" by name, set ID 0xE77
    UI::Element* fwdBtn;
    {
        System::String nameStr("buttonForward");
        fwdBtn = dialogElem->FindElement(nameStr).Get();
    }
    derived = ::Object::IsDerivedFromNullSafe(fwdBtn, UI::ButtonElement::TypeId());
    buttonForward = Object::InternalPtr<UI::ButtonElement>(derived ? static_cast<UI::ButtonElement*>(fwdBtn) : nullptr);

    if (buttonBack.Get()) buttonBack->SetElementId(0xE76);
    if (buttonForward.Get()) buttonForward->SetElementId(0xE77);

    // Populate combo with help topic names from ExternalList
    ::Object::ExternalList* extList = GetHelpExternalList();
    UI::ComboElement* comboElem = comboTopics.Get();
    if (!extList || !comboElem) return;    // standalone guard
    int topicCount = extList->GetResourceCount();

    for (int i = 0; i < topicCount; i++) {
        ::Object::External* entry = extList->_resources[i];

        bool isStringTable = ::Object::IsDerivedFromNullSafe(entry, UI::StringTable::TypeId());
        if (isStringTable && entry != nullptr) {
            // Get topic name (text index 1) — non-trivial return
            System::String topicName(static_cast<const UI::StringTable*>(entry)->GetText(1));

            // Default sprite
            UI::Sprite sprite;

            // Add item to combo: (sprite, name, userData=index)
            comboElem->AddItem(sprite, topicName, (void*)(intptr_t)i);  // CAST-OK: ptr_int
        }

        // Re-check count each iteration (matches original)
        topicCount = extList->GetResourceCount();
    }

    // Select first item and load its content
    comboElem->SetCurrentItem(0);
    LoadPage();
    helpui_sa::Trace("Slot4", this);
}

// ============================================================
// HelpUI::OnUpdateLayout (vtable +0x38) — Update handler
// ============================================================
// Enables/disables prev/next buttons based on current page index.
//
// Original: 0x14006A230.

// [7] 0x14006a230
void HelpUI::OnUpdateLayout() {
    if (!helpui_sa::CheckOnce()) return;
    int pageIndex = historyIndex;
    int pageCount = history._size;

    // Enable prev button if not on first page
    // (standalone guard: a failed "buttonBack"/"buttonForward" lookup leaves the
    // field null, and retail dereferences it unconditionally)
    UI::ButtonElement* prevBtn = buttonBack.Get();
    if (prevBtn) {
        prevBtn->Enable(pageIndex > 0);   // Element slot 8 Enable
    }

    // Enable next button if not on last page
    UI::ButtonElement* nextBtn = buttonForward.Get();
    if (nextBtn) {
        nextBtn->Enable(pageIndex < pageCount - 1);   // Element slot 8 Enable
    }
}

// ============================================================
// HelpUI::OnElementEvent (vtable +0x48) — Notification handler
// ============================================================
// Handles UI events:
//   Message 0x12 (button/link click):
//     - 0xE75 (document link): get link text, find target page,
//       save scroll position to history, navigate to target
//     - 0xE76 (back): go to previous history entry, restore scroll
//     - 0xE77 (forward): go to next history entry, restore scroll
//   Message 0x14 (combo selection change):
//     - 0xE74: topic changed, reload page content
//
// Original: 0x14006A290.

// Standalone guard helper: every navigation branch below (link / back / forward)
// dereferences the scroll bar, the document, the combo, the container and the
// history array without a null check, exactly as retail does. If setup could not
// find an element, treat the event as unhandled.
static bool HelpUI_NavReady(const HelpUI* self) {
    return self->scroll.Get() && self->document.Get() && self->comboTopics.Get() &&
           self->container.Get() && self->history._data;
}

// Back / forward: save the scroll value, step the history index by `step`,
// show that entry's page and restore its scroll value. The two arms of
// OnElementEvent are the same code with -1 / +1 (retail spells both out).
static void HelpUI_StepHistory(HelpUI* self, int step) {
    int histIdx = self->historyIndex;

    // Save current scroll position before navigating
    self->history[histIdx].scrollY = self->scroll->GetCurrentValue();

    // Move one entry
    histIdx += step;
    self->historyIndex = histIdx;

    // Page index from history
    int pageIdx = self->history[histIdx].pageIndex;

    // Look up StringTable from ExternalList
    ::Object::ExternalList* extList = GetHelpExternalList();
    UI::StringTable* validTable = HelpTopicAt(extList, pageIdx);

    // UI::String with text index 2 (content), set on document
    UI::String uiString(Object::ExternalPtr<UI::StringTable>(validTable), 2);
    uiString.pad0C = 0;   // built in a zeroed 16-byte slot before
    self->document->SetText(&uiString);

    // Restore scroll position from history
    histIdx = self->historyIndex;
    float savedScroll = self->history[histIdx].scrollY;
    self->container->SetOffsets(0, (int)savedScroll);

    // Update combo to show current page
    int comboIdx = self->history[histIdx].pageIndex;
    self->comboTopics->SetCurrentItem(comboIdx);
}

// [9] 0x14006a290
void HelpUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId msg, int32_t id,
                            int32_t /*param5*/, int32_t /*param6*/, int32_t linkIndex, void* /*userData*/) {
    if (!helpui_sa::CheckOnce()) return;
    helpui_sa::Trace("Slot9", this);

    if (msg == UI::EventId::SelectionChanged && id == 0xE74) {
        // Combo selection changed — reload page
        LoadPage();
        return;
    }

    if (msg != UI::EventId::Activated)
        return;

    if (id != 0xE75 && id != 0xE76 && id != 0xE77)
        return;
    if (!HelpUI_NavReady(this))
        return;   // standalone guard, see above

    if (id == 0xE75) {
        // Document link clicked — navigate to linked page
        System::String linkStr(document->GetLink(linkIndex));

        // FindLinkTarget takes its own copy (by value) and destroys it; returns
        // the matching index or -1.
        int targetIdx = HelpUI_FindLinkTarget(linkStr);

        if (targetIdx >= 0) {
            // Save current scroll position to history
            history[historyIndex].scrollY = scroll->GetCurrentValue();

            // Navigate to target page
            comboTopics->SetCurrentItem(targetIdx);
            LoadPage();
        }

        return;
    }

    if (id == 0xE76) {
        // Back button — go to previous history entry
        HelpUI_StepHistory(this, -1);
        return;
    }

    if (id == 0xE77) {
        // Forward button — go to next history entry
        HelpUI_StepHistory(this, +1);
        return;
    }
}

// ============================================================
// HelpUI::OnOpened (vtable +0x28) — No-op (HelpUI.h)
// ============================================================
