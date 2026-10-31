// src/game/Dialog/eventnotifydialog.cpp
// Clean reimplementation of EventNotifyDialog game logic.
//
// EventNotifyDialog (0xF0 bytes) — notification ribbon for game events.
// Extends DialogInstance (0xB8 base). Fields: EventNotifyDialog.h.
//
// Decompiled from Application-x64.exe.
//
// LEFT (B.4 W1 DLG-b): the event log is `GameUpdateLoop::m_eventNotifier`, a
// `void*` with no class in the tree yet (block GAME); its reads (+0x10 the
// category filter bytes, +0x18 the 0x28-byte entry array, +0x20 the count) stay
// offset reads until that class exists. `m_audioScene` and `m_game` are `void*`
// in the W0 header for the same reason.

#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // X::TypeId() (ENDGAME B.3.9)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/EventNotifyDialog.h"
#include "Dialog/EventPopupDialog.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerController.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Game/exe_constants.h"   // B3R: kRdata_*
#include <cstring>   // std::memcpy
#include "Game/EventLog.h"   // GameUpdateLoop::m_eventNotifier, EventLogEntry
#include "Game/Map.h"        // GameUpdateLoop::m_game

// ============================================================
// EventNotifyDialog_CreatePopup — modal popup creation (OnUpdate popup branch)
// ============================================================
// Faithful to the popup branch inside EventNotifyDialog::OnUpdate (0x140067C30):
// when an event's popup field (entry+0x20) is >= 0, the ribbon raises a modal
// EventPopupDialog for it. The exe allocates a 0xC0-byte DialogInstance built
// against the ribbon's referenced dialog resource (EventNotify.rsc declares
// `External _references ["EventPopup.rsc"]`), stamps the EventPopupDialog vtable
// and the event-log index at +0xB8, then adds it to the UserInterface.
//
// In standalone we load Dialog/EventPopup.rsc by path (yielding the same
// Dialog-typed External the exe reaches via _dialog -> Dialog::GetReference)
// and drive the EventPopupDialog ctor. Retail's shape otherwise (0x140067d88..
// 0x140067e3d): IsDerivedFrom Dialog, MemAlloc(0xC0, 8), the ctor only when the
// allocation succeeded, then AddDialog with the pointer, null or not; no test of
// the UI, the factory or the resource.

static EventPopupDialog* EventNotifyDialog_CreatePopup(EventNotifyDialog* self, int eventIndex) {
    UI::UserInterface* ui = self->_ui;

    // The popup template is the ribbon dialog's referenced resource [0]
    // (Dialog/EventNotify.rsc lists Dialog/EventPopup.rsc): `Dialog::GetReference(
    // _dialog, 0)` at 0x140067d88 / 0x140067d93 (R8D = 0), now the ported
    // 0x1800388d0. Until 2026-10-11 the port loaded Dialog/EventPopup.rsc by path
    // through the ExternalFactory instead (the stand-in for the unported body); the referenced
    // External is the same object, held by the ribbon's `_references` array for
    // the dialog's lifetime, so nothing is cached here.
    //
    // The IsDerivedFrom Dialog the exe applies to the reference (0x140067da8 +
    // CMOVNZ): the member, on the reference unguarded (MOV RCX,RBX straight from
    // the load at 0x140067d99; no null test -- B7 LX row 993).
    const UI::Dialog* popupRes = nullptr;
    {
        Object::ExternalPtr<Object::External> found = self->_dialog->GetReference(0);
        if (found.Get()->IsDerivedFrom(UI::Dialog::TypeId()))
            popupRes = static_cast<const UI::Dialog*>(found.Get());
    }

    void* mem = System::MemoryAllocator::MemAlloc(0xC0, 8);
    EventPopupDialog* popup = nullptr;
    if (mem != nullptr) {
        popup = ::new (mem) EventPopupDialog(ui, popupRes,
                                             Object::InternalPtr<UI::DialogInstance>(),
                                             eventIndex);
    }

    Object::InternalPtr<UI::DialogInstance> dialogPtr(popup);
    ui->AddDialog(&dialogPtr);
    return popup;
}

// ============================================================
// EventNotifyDialog_RemoveEntry (0x140011670)
// ============================================================
// Removes element [removeIndex] of the +0xD8 array: the removed entry's String
// is destroyed, every later entry is copied one slot down (its String
// copy-CONSTRUCTED into the slot), and the count drops by one. The transcription
// is kept exactly (Collection::Array has no remove member).
static void EventNotifyDialog_RemoveEntry(Collection::Array<EventNotifyEntry>& entries, int removeIndex) {
    int count = entries._size;

    if (removeIndex < count) {
        entries._data[removeIndex].text.~String();

        // Shift subsequent entries down (removeIndex+1 ... count-1 -> removeIndex ... count-2)
        for (int i = removeIndex + 1; i < count; i++) {
            EventNotifyEntry& dst = entries._data[i - 1];
            const EventNotifyEntry& src = entries._data[i];
            dst.timer = src.timer;
            dst.eventIndex = src.eventIndex;
            dst.icon = src.icon;
            new (&dst.text) System::String(src.text);
            dst.color = src.color;
        }
    }
    entries._size = count - 1;
}

// ============================================================
// EventNotifyDialog::OnOpened (vtable +0x20) — 0x1400686d0
// ============================================================
// Finds "ribbonNotify" -> ribbonNotify.
// Finds "notifyContent" container element.
// For each child of notifyContent:
//   - child -> slot.group; its "notifyImage" -> slot.image; its "notifyButton"
//     -> slot.button, element id 1500 + index
//   - appends the slot to `slots`
// Finally copies the event log counter to lastEventIndex.
//
// OnClosed (vtable +0x28) 0x1400686c0 is `XOR EAX,EAX; RET`.

void EventNotifyDialog::OnClosed() {}

void EventNotifyDialog::OnOpened() {
    ribbonNotify = FindElement(System::String("ribbonNotify"));

    Object::InternalPtr<UI::Element> notifyContent = FindElement(System::String("notifyContent"));

    int childCount = notifyContent->ChildCount();

    for (int i = 0; i < childCount; i++) {
        EventNotifySlot slot = {};

        slot.group = notifyContent->Child(i);
        {
            System::String nameStr("notifyImage");
            Object::InternalPtr<UI::Element> child = notifyContent->Child(i);
            slot.image = child->FindElement(nameStr);
        }
        {
            System::String nameStr("notifyButton");
            Object::InternalPtr<UI::Element> child = notifyContent->Child(i);
            slot.button = child->FindElement(nameStr);
        }

        // Set button element ID to 1500 + i (0x5DC + i)
        slot.button->SetElementId(i + 0x5DC);

        slots.SetSize(slots.GetSize() + 1, slot);
    }

    // lastEventIndex = the event log's count (GameUpdateLoop +0x138, its +0x20).
    EventLog* eventLog = GameUpdateLoop::_singleton->m_eventNotifier;
    lastEventIndex = eventLog->entries.GetSize();
}

// ============================================================
// EventNotifyDialog::OnUpdate (vtable +0x38) — 0x140067C30
// ============================================================
// Decrements timers for shown notifications, removes expired ones.
// Scans the event log for new events since lastEventIndex.
// Maps event types to categories, checks filter flags.
// Creates notification entries with icon, color, text, timer.
// Plays sound effect if new events were processed.
// Trims excess entries beyond the slot count.

void EventNotifyDialog::OnUpdate(const System::Time& time) {
    EventLog* eventLogPtr = GameUpdateLoop::_singleton->m_eventNotifier;

    // Phase 1: Decrement timers on shown notifications, remove expired ones
    if (entries._size > 0) {
        int idx = 0;
        do {
            entries._data[idx].timer._ticks -= time._ticks;

            // If timer expired (< 1), remove this entry
            if (entries._data[idx].timer._ticks < 1) {
                EventNotifyDialog_RemoveEntry(entries, idx);
                idx--;
            }

            idx++;
        } while (idx < entries._size);
    }

    // Phase 2: Scan event log for new events
    // DAT_1401721cc = 30.0f, the .rdata constant kRdata_1401721cc (B3R: the engine_funcs
    // pointer g_pNotifyTimerDuration was bound to that registered cell).
    float timerDuration;
    std::memcpy(&timerDuration, &kRdata_1401721cc, sizeof timerDuration);
    int lastEventIdx = lastEventIndex;
    int totalEvents = eventLogPtr->entries.GetSize() - 1;

    if (lastEventIdx < totalEvents) {
        bool hasNewEvents = false;
        int nextIdx = lastEventIdx + 1;
        int scanIdx = nextIdx;

        if (scanIdx <= totalEvents) {
            int remaining = totalEvents - scanIdx + 1;

            do {
                // The entry array is re-read per pass (0x28-byte EventLogEntry records).
                EventLogEntry* eventArray = eventLogPtr->entries.GetData();
                int severity = eventArray[scanIdx].severity;

                if (severity > 0) {
                    // Map event type to category
                    int eventType = eventArray[scanIdx].eventType;
                    int category;
                    switch (eventType) {
                        case 0: case 1: case 3: case 4:
                        case 5: case 6: case 7:
                            category = 1; break;
                        case 2:
                            category = 0; break;
                        case 0xB: case 0xC: case 0x1E: case 0x1F:
                            category = 2; break;
                        case 0xD: case 0xE:
                            category = 4; break;
                        case 0x11: case 0x12: case 0x13: case 0x14:
                        case 0x15: case 0x16: case 0x17: case 0x18:
                            category = 3; break;
                        default:
                            category = 5; break;
                    }

                    // Check category filter flag
                    if (eventLogPtr->field_0x10[category] != 0 &&
                        eventArray[scanIdx].iconIndex >= 0) {

                        // Check if popup needed (+0x20 field >= 0 means show popup).
                        // Raise the modal EventPopupDialog for this event-log index.
                        if (eventArray[scanIdx].popup >= 0) {
                            EventNotifyDialog_CreatePopup(this, scanIdx);
                        }

                        // Build the notification entry (0x30 bytes), zeroed first.
                        EventNotifyEntry notifyEntry = {};

                        notifyEntry.eventIndex = scanIdx;

                        // timer: Time constructed from float seconds
                        RtSystem_Time_Ctor(&notifyEntry.timer, timerDuration);

                        // icon: "EventIcon%i" on the first slot image's sprite sheet
                        int iconIndex = eventArray[scanIdx].iconIndex;
                        System::String iconNameStr = System::String::Format("EventIcon%i", (uint64_t)(uint32_t)iconIndex);

                        UI::Sprite slotSprite =
                            static_cast<UI::ImageElement*>(slots._data[0].image.Get())->GetSprite();
                        notifyEntry.icon = UI::Sprite(slotSprite.GetSpriteSheet(), iconNameStr);

                        // color from severity
                        int sev = eventArray[scanIdx].severity;
                        uint32_t color;
                        if (sev == 1)      color = 0xFFFFFF00;  // yellow
                        else if (sev == 2) color = 0xFF8080FF;  // pink
                        else if (sev == 3) color = 0xFFFF6060;  // red
                        else if (sev == 4) color = 0xFFEA7517;  // orange
                        else               color = 0xFFFFFFFF;  // white
                        notifyEntry.color = color;

                        // text: strip the category prefix before ':'.
                        // Event text is the entry's +0x08.
                        System::String& eventTextStr = eventArray[scanIdx].text;

                        int strLen = eventTextStr.GetLength();
                        const char* strData = eventTextStr._data;

                        // Find ':' separator
                        int colonPos = 0;
                        for (int c = 0; c < strLen; c++) {
                            if (strData[c] == ':') {
                                colonPos = c + 1;  // skip the colon
                                break;
                            }
                            colonPos = c + 1;
                        }

                        // If colon found, take substring after it; otherwise use whole string
                        if (colonPos == strLen) {
                            notifyEntry.text = eventTextStr;
                        } else {
                            notifyEntry.text = eventTextStr.SubString(colonPos + 1, strLen - 1);
                        }

                        // Append to the shown entries (0x140011750)
                        entries.SetSize(entries._size + 1, notifyEntry);

                        hasNewEvents = true;
                    }
                }

                scanIdx++;
                remaining--;
            } while (remaining != 0);
        }

        // Trim excess entries beyond the slot count
        if (slots._size < entries._size) {
            do {
                EventNotifyDialog_RemoveEntry(entries, 0);
            } while (slots._size < entries._size);
        }

        // Play sound effect if new events were found
        if (hasNewEvents) {
            System::Time zeroTime;
            RtSystem_Time_Ctor(&zeroTime, 0.0f);
            GameUpdateLoop::_singleton->m_audioScene->PlayEffect(
                g_gameContainer->warningEffect, static_cast<Audio::Scene::SoundType>(1),
                zeroTime);
        }
    }

    // Update last processed event index: the count - 1 read BEFORE the scan
    // (`MOV R14D,[RBP+0x67]` 0x140067fd5, stored at 0x140068047); an event logged
    // during the scan is picked up next frame.
    lastEventIndex = totalEvents;
}

// ============================================================
// EventNotifyDialog::OnUpdateLayout (vtable +0x30) — 0x1400680c0
// ============================================================
// Each frame:
//   1. Compute target Y for the ribbon: (parentRoot.y - selfRoot.y) + 44
//      — positions the ribbon just below the parent dialog (toolbar).
//   2. Lerp smoothedY toward target: smoothedY = smoothedY*0.8 + targetY*0.2
//   3. Apply smoothedY as top padding on the root element.
//   4. Show the ribbonNotify container element.
//   5. For each shown notification (0..entries-1):
//      - Copy the entry's icon sprite to the slot's image.
//      - Get child[4] of the button; if it is an ImageElement, copy the sprite to it too.
//      - Check the event log entry's popup field at +0x18:
//          if < 0 (no popup): hide button, show image.
//          if >= 0 (popup active): show button, hide image.
//      - Colour the image's border (vtable slot 11 GetBorder, slot 10 SetColor)
//        and each of the button's 4 children.
//      - The group element's tooltip string is the entry text.
//   6. For empty slots (entries..slots-1):
//      - Reset the image's border and colour to 0xFFFFFFFF.
//      - Clear the group element's tooltip string.
//      - Show the image element, hide the button element.
void EventNotifyDialog::OnUpdateLayout() {
    // -------------------------------------------------------
    // Step 1-3: Compute target Y and lerp smoothed position
    // -------------------------------------------------------
    UI::DialogInstance* parentDlg = parentDialog.Get();
    if (!parentDlg) return;
    UI::Element* parentRoot = parentDlg->_root.Get();
    if (!parentRoot) return;
    int   parentY    = parentRoot->_layoutWidth;

    UI::Element* selfRoot = _root.Get();
    if (!selfRoot) return;
    int   selfY    = selfRoot->_layoutWidth;

    int targetY = parentY - selfY + 44;  // 0x2c = 44

    // fVar16 = final target based on the shown count
    float fVar16 = 0.0f;
    int activeCount = entries._size;
    if (activeCount == 1) {
        fVar16 = (float)targetY;
    } else if (activeCount > 1) {
        // Additional entries add 40.0f spacing (DAT_1401721dc = 0x42200000 = 40.0f)
        fVar16 = (float)(activeCount - 1) * 40.0f + (float)targetY;
    }

    // Lerp: smoothedY = smoothedY * 0.8 + fVar16 * 0.2
    // DAT_140172128 = 0.8f, DAT_1401720f8 = 0.2f
    smoothedY = smoothedY * 0.8f + fVar16 * 0.2f;

    // Apply as top padding on this root: floor(smoothedY + 0.5f)
    // DAT_140172114 = 0.5f (rounding)
    selfRoot->SetPadding(0, (int)(smoothedY + 0.5f), 0, 2);

    // -------------------------------------------------------
    // Step 4: Show ribbonNotify element
    // -------------------------------------------------------
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return;
    EventLog* eventLogPtr = gameWorld->m_eventNotifier;
    if (!eventLogPtr) return;  // event log not initialized yet (e.g. during loading)
    EventLogEntry* eventArray = eventLogPtr->entries.GetData();
    if (!eventArray) return;

    UI::Element* ribbonElem = ribbonNotify.Get();
    if (ribbonElem) ribbonElem->SetFlagsOnOff(0, 1);  // clear flag 1 = show

    // -------------------------------------------------------
    // Steps 5 & 6: Sync notification slots
    // -------------------------------------------------------
    int capacity = slots._size;
    if (!slots._data || !entries._data) return;

    // --- Shown entries ---
    for (int i = 0; i < activeCount; i++) {
        const EventNotifyEntry& activeEntry = entries._data[i];
        const EventNotifySlot&  slotEntry   = slots._data[i];

        UI::Element* groupElem  = slotEntry.group.Get();
        UI::ImageElement* imageElem = static_cast<UI::ImageElement*>(slotEntry.image.Get());
        UI::Element* buttonElem = slotEntry.button.Get();
        if (!buttonElem || !groupElem || !imageElem) continue;

        int eventIndex = activeEntry.eventIndex;

        // Set icon sprite on image element
        UI::Sprite sprite = activeEntry.icon;
        imageElem->SetSprite(&sprite);

        // Get child[4] of button element — may be an ImageElement for embedded icon
        UI::Element* child4 = buttonElem->Child(4).Get();

        // If child4 is an ImageElement, copy the sprite to it as well
        {
            bool isImage = child4 && ::Object::IsDerivedFromNullSafe(child4, UI::ImageElement::TypeId());
            Object::InternalPtr<UI::ImageElement> imgChildPtr(
                isImage ? static_cast<UI::ImageElement*>(child4) : nullptr);
            UI::Sprite sprite2 = activeEntry.icon;
            UI::ImageElement* imgElemResolved = imgChildPtr.Get();
            if (imgElemResolved) {
                imgElemResolved->SetSprite(&sprite2);
            }
        }

        // Popup state: the event log entry's +0x18 (EventLogEntry::tileIndex: the
        // button jumps to the event's cell, so it hides when there is none).
        int popupField = eventArray[eventIndex].tileIndex;
        bool noPopup   = (popupField < 0);

        if (noPopup) {
            buttonElem->SetFlagsOnOff(1, 0);  // set flag 1 = hide
            imageElem->SetFlagsOnOff(0, 1);   // clear flag 1 = show
        } else {
            buttonElem->SetFlagsOnOff(0, 1);  // clear flag 1 = show
            imageElem->SetFlagsOnOff(1, 0);   // set flag 1 = hide
        }

        uint32_t color = activeEntry.color;

        // The IMAGE's border (slot 11 GetBorder, RETSLOT per B6-2, on the slot's +0x08 at
        // 0x1400682f9), then SetColor (slot 10).
        {
            Object::InternalPtr<UI::Element> border;
            UI::Element* borderElem = imageElem->GetBorder(&border)->Get();
            if (borderElem) {
                borderElem->SetColor(color);
            }
        }

        // Also set color on each of the 4 children of the button element.
        for (int c = 0; c < 4; c++) {
            UI::Element* kid = buttonElem->Child(c).Get();
            if (!kid) continue;
            kid->SetColor(color);
        }

        // The group element's tooltip string is the entry text.
        groupElem->SetToolTipString(activeEntry.text);
    }

    // --- Empty slots (activeCount..capacity-1): reset color, clear text, show empty image ---
    for (int i = activeCount; i < capacity; i++) {
        const EventNotifySlot& slotEntry = slots._data[i];

        UI::Element* groupElem  = slotEntry.group.Get();
        UI::Element* imageElem  = slotEntry.image.Get();
        UI::Element* buttonElem = slotEntry.button.Get();
        if (!buttonElem || !groupElem || !imageElem) continue;

        // Reset the image border's color to 0xFFFFFFFF (white = no tint): slot 11 on the
        // slot's +0x08, as in the shown arm.
        {
            Object::InternalPtr<UI::Element> border;
            UI::Element* borderElem = imageElem->GetBorder(&border)->Get();
            if (borderElem) {
                borderElem->SetColor(0xFFFFFFFFu);
            }
        }

        // Also reset color on the image element itself (vtable slot 10, the +0x08 again).
        imageElem->SetColor(0xFFFFFFFFu);

        // Clear the group element's tooltip string
        groupElem->SetToolTipString(System::String());

        // Show image element (clear flag 1), hide button element (set flag 1).
        // Empty image slots display nothing (no sprite set); button hidden to prevent clicks.
        imageElem->SetFlagsOnOff(0, 1);
        buttonElem->SetFlagsOnOff(1, 0);
    }
}

// ============================================================
// EventNotifyDialog::OnElementEvent (0x140068680) — slot 8
// ============================================================
// Handles notification button clicks (element IDs 1500..1500+count-1).
// eventType 0x12 = button click, eventData in [0x5DC, 0x5DC+entries)
// Calls NavigateToEvent (0x1400685a0) to move to the event location on the map.

// ============================================================
// EventNotifyDialog::NavigateToEvent (0x1400685a0)
// ============================================================
// Moves the camera to the map location of shown entry [index]:
//  1. The entry's event-log index; the event log entry (0x28 bytes) holds the
//     tile index at +0x18.
//  2. Convert the tile index to world XY (centred, -0.5f per tile).
//  3. Pick the terrain (0x14009f3e0) for the 3D camera target.
//  4. Write it to the PlayerController's look-at target.
//
// Constant: DAT_140172114 = 0.5f (tile center offset).
void EventNotifyDialog::NavigateToEvent(int32_t index) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    int eventLogEntryIdx = entries._data[index].eventIndex;

    EventLog* eventLog = game->m_eventNotifier;
    EventLogEntry* entryList = eventLog->entries.GetData();
    int tileIdx = entryList[eventLogEntryIdx].tileIndex;

    if (tileIdx < 0) return;  // no valid tile position

    // mapGrid = m_game (Map*); gridSize = Map::size
    Map* mapGrid = game->m_game;
    int gridSize = mapGrid->size;
    float halfSize = (float)(gridSize >> 1) - 0.5f;  // = (gridSize/2) - 0.5f (DAT_140172114)
    float inXY[3] = {
        (float)(tileIdx % gridSize) - halfSize,  // worldX
        (float)(tileIdx / gridSize) - halfSize,  // worldY
        0.0f
    };

    // terrain pick — the actual 3D world position
    float outPos[3] = {};
    mapGrid->GetTerrainPos(outPos, inXY, '\0', nullptr);   // 0x14009f3e0

    PlayerController* camTarget = game->m_playerController;
    camTarget->target.x = outPos[0];
    camTarget->target.y = outPos[1];
    camTarget->target.z = outPos[2];
}

void EventNotifyDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (eventType == 0x12) {
        int activeCount = entries._size;
        if (eventData > 0x5DB && eventData < activeCount + 0x5DC) {
            NavigateToEvent(eventData - 0x5DC);
        }
    }
}

// ============================================================
// EventNotifyDialog: ctor, factory, registration
// ============================================================
// DialogInstance slot semantics (verified against FollowDialog + the exe vtable):
//   +0x00 idx0 ScalarDtor          -> ~Array x2 (the members) + base dtor
//   +0x10 idx2 GetTypeId
//   +0x18 idx3 Persist             -> no-op (serialize thunk)
//   +0x20 idx4 OnOpened            -> find ribbon elements, build slot array
//   +0x28 idx5 OnClosed            -> RET 0
//   +0x30 idx6 OnUpdateLayout      -> position ribbon, sync slot sprites  [per-frame]
//   +0x38 idx7 OnUpdate            -> scan event log, manage notifications [per-frame]
//   +0x40 idx8 OnElementEvent      -> ribbon button clicks -> navigate
//   +0x48 idx9 OnSerializeSettings -> no-op (serialize thunk)

#include "Object/factory_registration.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>

// B3R: OnUpdate's timer duration is the .rdata constant kRdata_1401721cc (30.0s, exe
// DAT_1401721cc); the old EnsureGlobals lazy-fill over g_pNotifyTimerDuration never
// ran (the cell is registered before EngineFuncsInit binds it) and is gone.
// ENDGAME B.3.9: the TypeId_Dialog / TypeId_ImageElement heals are dead too --
// UI::Dialog / UI::ImageElement have a ported TypeId().

// Retail EventNotifyDialog_CopyCtor (0x140068ab0): DialogInstance base ctor,
// then the ribbon fields. The parent is the toolbar dialog (the ribbon sits just
// below it — see OnUpdateLayout).
//   +0xB8 parent · +0xC0 smoothedY=0 · +0xC4 lastEventIndex=-1 · +0xC8..+0xE8 = 0
// The two arrays zero themselves (Collection::Array's ctor: the same stores).
EventNotifyDialog::EventNotifyDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                                     Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0) {
    parentDialog = parent;
    smoothedY = 0.0f;
    lastEventIndex = -1;
    ribbonNotify = Object::InternalPtr<UI::Element>();
}

// create 0x140011500: MemAlloc(0xf0, 8), then the ctor.
::Object::Base* EventNotifyDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EventNotifyDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) EventNotifyDialog());
}
// createInPlace 0x140011540 (the tree's in-place build is kept, B8-13).
::Object::Base* EventNotifyDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) EventNotifyDialog());
}

DEFINE_EXE_TYPE(EventNotifyDialog, "UI::DialogInstance")

