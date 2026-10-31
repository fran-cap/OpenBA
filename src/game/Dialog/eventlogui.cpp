// engine/src/ui/eventlogui.cpp
// Clean reimplementation of EventLogUI game logic.
//
// EventLogUI (0x80 bytes) — event log panel.
// Extends DialogController (0x30 base). Fields: EventLogUI.h.
//
// Element IDs: buttonPrevious=100, buttonNext=0x65, buttonBottom=0x66,
//              notification buttons=0x67..0x6B, display rows>=0x6C.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "collection/Collection.h"
#include "ui/CheckElement.h"
#include "ui/TableElement.h"   // the "display" table's GetRowCount (+0xC0)
#include "Game/PlayerController.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/EventLog.h"         // GameUpdateLoop::m_eventNotifier, EventLogEntry
#include "Game/Map.h"              // GameUpdateLoop::m_game
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)

// The three element arrays are EventLogUI's Collection::Array members.
using ElementArray = EventLogUI::ElementArray;

// The event log (GameUpdateLoop +0x138; Game/EventLog.h): +0x10 the category
// filter bytes, +0x18 the entry array (EventLogEntry, 0x28 stride), +0x20 count.
static EventLog* EventLogAddress() {
    return GameUpdateLoop::_singleton->m_eventNotifier;
}

// ============================================================
// EventLogUI::OnOpened (vtable +0x28) — Setup event log display
// ============================================================
// Finds navigation buttons, "display" container, notification buttons.
// Stores button ptrs, resizes display arrays, sets element IDs.
//
// Original: 0x140067640.

// [5] 0x140067640
void EventLogUI::OnOpened() {
    UI::Element* dialogElem = _container;
    UI::DialogInstance* dialogInst = _dialog;

    // --- Find navigation buttons ---
    {
        System::String nameStr("buttonPrevious");
        buttonPrevious = dialogElem->FindElement(nameStr);
    }
    {
        System::String nameStr("buttonNext");
        buttonNext = dialogElem->FindElement(nameStr);
    }
    {
        System::String nameStr("buttonBottom");
        buttonBottom = dialogElem->FindElement(nameStr);
    }

    // Set element IDs on buttons
    buttonPrevious->SetElementId(100);   // buttonPrevious = 100
    buttonNext->SetElementId(0x65);      // buttonNext = 0x65
    buttonBottom->SetElementId(0x66);    // buttonBottom = 0x66

    // --- Find the "display" table, narrowed to TableElement (IsDerivedFrom through
    // the import slot 0x140151510, CMOVNZ: 0x140067840..0x140067856) ---
    UI::TableElement* displayElem;
    {
        System::String nameStr("display");
        UI::Element* found = dialogElem->FindElement(nameStr).Get();
        displayElem = found->IsDerivedFrom(UI::TableElement::TypeId())   // 0x14006784c, no null test (B7 LX row 993)
                          ? static_cast<UI::TableElement*>(found) : nullptr;
    }

    // Resize display text array and display button array to the table's ROW count
    // (+0xC0, `MOV EDX,[RAX+0xc0]` at 0x14006787d / 0x14006789a, GetRowCount).
    // Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Both elements are 8-byte InternalPtr handles (byte-copy).
    displayTexts.SetSize(displayElem->GetRowCount());
    displayButtons.SetSize(displayElem->GetRowCount());

    // Iterate display rows: store child text element, store the row as a
    // button, set element IDs
    int rowCount = displayTexts._size;
    for (int i = 0; i < rowCount; i++) {
        // The "display" table is 2 columns (buttonEvent, textEvent) by GetRowCount rows,
        // so it holds two children per row. Retail steps the child index by 2
        // (0x1400678ce `lea r8d,[rsi+1]`, 0x1400678f1 `mov r8d,esi`, 0x140067961
        // `add esi,2`): the text is child 2*i+1, the row's button child 2*i.
        displayTexts[i] = displayElem->Child(2 * i + 1);

        // The row's button (the retail check is against Object::Internal, i.e. any element)
        displayButtons[i] = displayElem->Child(2 * i);

        // Set element ID on the button: 0x6C + index
        displayButtons[i]->SetElementId(0x6C + i);
    }

    // Reset scroll offset to 0
    scrollOffset = 0;

    // --- Find "notifybuttons" container in the dialog instance ---
    UI::Element* notifyContainer;
    {
        System::String nameStr("notifybuttons");
        notifyContainer = dialogInst->FindElement(nameStr).Get();
    }

    // Iterate notification button children, store them in the notify array, set IDs
    int notifyCount = notifyContainer->ChildCount();
    EventLog* eventLogSingleton = EventLogAddress();

    for (int i = 0; i < notifyCount; i++) {
        // Child button element
        UI::Element* btnElem = notifyContainer->Child(i).Get();

        // Set element ID: 0x67 + index
        btnElem->SetElementId(0x67 + i);

        // Copy the notification flag from the event log into the check box:
        // ONE byte, flags[i] (retail 0x140067a55 / 0x140067a69).
        static_cast<UI::CheckElement*>(btnElem)->_checked =
            eventLogSingleton->field_0x10[i];

        // Push button ptr to the notification array.
        ElementArray& notifyBtns = notifyButtons;
        notifyBtns.SetSize(notifyBtns.GetSize() + 1, Object::InternalPtr<UI::Element>(btnElem));
    }
}

// ============================================================
// EventLogUI::OnUpdateLayout (vtable +0x38) — Update event log entries
// ============================================================
// Iterates display rows from bottom to top, filling each with event
// data from the event log. Updates text parameters and button state
// based on scroll offset. Enables/disables the bottom / next buttons.
//
// Original: 0x140067440.

// [7] 0x140067440
void EventLogUI::OnUpdateLayout() {
    EventLog* eventLog = EventLogAddress();
    int totalEvents = eventLog->entries.GetSize();  // total event count

    int displayCount = displayTexts._size;  // number of display rows
    int scroll = scrollOffset;

    // Rows are filled bottom-up: the newest visible event lands in the BOTTOM row.
    // Entry index for row `row` = firstIdx - (displayCount-1) + row, where
    // firstIdx = totalEvents - scrollOffset - 1 (the newest visible entry). A row whose
    // index is below 0 is blanked and its button disabled. The loop runs whenever there is
    // a row: retail's one early exit (0x140067485 JS) tests displayCount-1, the flags of
    // the DEC at 0x14006747d (the MOVSXDs between them set none), NOT firstIdx. With no
    // events yet, or scrolled past the oldest, every row is blanked.
    int firstIdx = totalEvents - scroll - 1;
    if (displayCount - 1 >= 0) {
        int base = firstIdx - (displayCount - 1);
        EventLogEntry* eventArray = eventLog->entries.GetData();
        for (int row = displayCount - 1; row >= 0; row--) {
            int eventIdx = base + row;

            UI::TextElement* textElem = static_cast<UI::TextElement*>(displayTexts[row].Get());
            UI::Element* btnElem = displayButtons[row].Get();

            int enabled;
            if (eventIdx < 0) {
                // No event for this row — blank the text.
                System::String emptyStr("");
                textElem->SetParameter(0, emptyStr);
                enabled = 0;
            } else {
                const EventLogEntry& eventEntry = eventArray[eventIdx];
                // Text = event entry +0x08.
                textElem->SetParameter(0, eventEntry.text);
                // Colour = entry +0x00 (Element slot 10 SetColor).
                textElem->SetColor(eventEntry.color);
                // The nav button is enabled only if the event has a map location
                // (tileIdx at entry+0x18 >= 0).
                enabled = (eventEntry.tileIndex >= 0) ? 1 : 0;
            }
            // Element slot 8 Enable — common to both branches.
            btnElem->Enable(enabled != 0);
        }
    }

    // Enable/disable buttonBottom based on scroll offset > 0 (slot 8 Enable)
    buttonBottom->Enable(scroll > 0);

    // Enable/disable buttonNext based on scroll offset > 0
    buttonNext->Enable(scroll > 0);
}

// ============================================================
// EventLogUI::ToggleNotification (0x140067320)
// ============================================================
// Reads the check box's state and writes it into the event log's category
// filter byte (+0x10 + index).
void EventLogUI::ToggleNotification(int32_t btnIdx) {
    UI::CheckElement* btnElem = static_cast<UI::CheckElement*>(notifyButtons[btnIdx].Get());
    EventLog* eventLog = EventLogAddress();
    // LEFT (audit WORK:H8, not executable in this file set): the source
    // UI::CheckElement::_checked is uint8_t (src/engine/ui/CheckElement.h) and the
    // target EventLog::field_0x10 bool[6] (Game/EventLog.h); a uint8_t -> bool store
    // would normalise the check box's raw byte, the byte store does not.
    *reinterpret_cast<uint8_t*>(&eventLog->field_0x10[btnIdx]) = btnElem->_checked;   // LEFT: H8, byte_io candidate: retail 0x14006734f / 0x14006735b is MOVZX byte [check+0xd8] then MOV byte [eventLog+0x10+idx], a raw byte copy (MSVC bool to bool); the field stays bool because GameUpdateLoop serializes it through Serialize(6, bool*) (gameupdateloop_serialize.cpp:165); a uint8_t -> bool assignment adds a != 0 normalisation where retail moves the check box's byte (CheckElement::_checked, uint8_t): a byte_io marker candidate  // CAST-OK: byte_io (S4 ruling)
}

// ============================================================
// EventLogUI::NavigateToRow (0x140067370)
// ============================================================
// Computes the row's event, its tile index, picks the terrain there and sets
// the camera's look-at target.
void EventLogUI::NavigateToRow(int32_t rowIdx) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    EventLog* eventLog = EventLogAddress();
    int totalEvents = eventLog->entries.GetSize();
    int eventIdx = (totalEvents - displayTexts._size - scrollOffset) + rowIdx;
    // Tile index from the event entry (+0x18)
    EventLogEntry* entryArray = eventLog->entries.GetData();
    int tileIdx = entryArray[eventIdx].tileIndex;
    if (tileIdx >= 0) {
        // Map::size at +0.
        Map* mapGrid = game->m_game;
        int gridSize = mapGrid->size;
        float halfSize = (float)(gridSize >> 1) - 0.5f;
        float inXY[3] = {
            (float)(tileIdx % gridSize) - halfSize,
            (float)(tileIdx / gridSize) - halfSize,
            0.0f
        };
        float outPos[3] = {};
        mapGrid->GetTerrainPos(outPos, inXY, '\0', nullptr);   // 0x14009f3e0
        PlayerController* camTarget = game->m_playerController;
        camTarget->target.x = outPos[0];
        camTarget->target.y = outPos[1];
        camTarget->target.z = outPos[2];
    }
}

// ============================================================
// EventLogUI::OnElementEvent (vtable +0x48) — Button dispatch
// ============================================================
// Handles scroll buttons, notification toggles, and row click navigation.
//
// Original: 0x1400675D0.

// [9] 0x1400675d0
void EventLogUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t msg, int32_t id,
                                int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (msg != 0x12) return;

    if (id == 100) {
        // buttonPrevious — scroll to older events
        scrollOffset++;
    } else if (id == 0x65) {
        // buttonNext — scroll to newer events
        scrollOffset--;
    } else if (id == 0x66) {
        // buttonBottom — jump to most recent
        scrollOffset = 0;
    } else if (id >= 0x67 && id <= 0x6B) {
        // Notification toggle buttons (5 categories)
        ToggleNotification(id - 0x67);
        return;
    } else if (id >= 0x6C) {
        // Display row click — navigate camera to event location
        NavigateToRow(id - 0x6C);
        return;
    }

    // Clamp scroll offset to >= 0
    if (scrollOffset < 0) scrollOffset = 0;
}

// [6] 0x140067630 (3-byte no-op) is EventLogUI::OnClosed, inline in EventLogUI.h.

// ============================================================
// STANDALONE EXE: real EventLogUI vtable + registration
// ============================================================
// EventLogUI (DialogController, 0x80 bytes, exe vtable 0x14015bc40). In standalone
// this type was a dormant UI_DIALOG_STUB (all slots no-op) so the event-log panel
// opened blank. Promote it to a real per-class vtable wiring the reimplemented
// slots, following the CitizenUI promotion pattern (citizenui.cpp).
//
// DialogController slot semantics (verified against CitizenUI + the exe vtable):
//   +0x00 idx0 ScalarDtor
//   +0x10 idx2 GetTypeId
//   +0x28 idx5 Slot5  = setup (find nav buttons, "display" rows, "notifybuttons")
//   +0x30 idx6 Slot6  = 3-byte RET 0 no-op
//   +0x38 idx7 Slot7  = content update (fill display rows from the event log)
//   +0x40 idx8        = base DialogController::OnUpdate (per-frame). Left INHERITED,
//                       matching retail. This comment used to claim the framework
//                       "dispatches ONLY +0x40 each frame" and therefore drove Slot7
//                       from here — that claim is FALSE (it also propagated to
//                       pastureui.cpp, where it caused the same bug). Ghidra:
//                       UI::UserInterface::Update (Runtime 0x1801213c0) ->
//                       DialogInstance::ComputeLayout (0x180113d90) -> vtable idx6
//                       OnUpdateLayout (0x1801153a0) walks +0x78 and dispatches
//                       controller idx7 EVERY frame with no dirty flag. So idx7 is
//                       the per-frame sync path; driving Slot7 from idx8 as well ran
//                       it twice per frame. Also note idx8's real signature takes a
//                       `const Time&`, which a void(*)(void*) override silently drops.
//   +0x48 idx9 Slot9  = button dispatch (scroll / notify toggle / row click)
//
// EventLogUI_Slot5/7/9 read the event-log struct (gameWorld+0x138) but dereference
// no exe-.rdata type-id globals, so no EnsureGlobals lazy-fill is needed.

#include "Object/factory_registration.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>





// create 0x1400113e0: MemAlloc(0x80, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* EventLogUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EventLogUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) EventLogUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* EventLogUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) EventLogUI());
}

DEFINE_EXE_TYPE(EventLogUI, "UI::DialogController")

