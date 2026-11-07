#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/EventPopupDialog.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/EventLog.h"         // GameUpdateLoop::m_eventNotifier, EventLogEntry
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B

// ENDGAME B.3.8 block DLG: EventPopupDialog is a real class (Dialog/EventPopupDialog.h).

// [4] OnOpened (0x140068C20) — Setup
// Finds imageIcon, textEvent, buttonOk elements.
// Reads the event log entry at eventIndex.
// Sets imageIcon sprite to "EventIcon<N>", sets textEvent parameter 0
// to event text, sets buttonOk element ID to 200.
void EventPopupDialog::OnOpened() {
    UI::ImageElement* imageIcon = nullptr;
    { System::String nameStr("imageIcon");
      imageIcon = static_cast<UI::ImageElement*>(FindElement(nameStr).Get()); }

    UI::TextElement* textEvent = nullptr;
    { System::String nameStr("textEvent");
      textEvent = static_cast<UI::TextElement*>(FindElement(nameStr).Get()); }

    UI::Element* buttonOk = nullptr;
    { System::String nameStr("buttonOk");
      buttonOk = FindElement(nameStr).Get(); }

    // The event log entry (0x28 bytes) at eventIndex.
    // (Game/EventLog.h: +0x18 the entry array; the entry's +0x08 text and +0x20,
    // EventLogEntry::popup, the pop-up's icon.)
    EventLog* eventMgr = GameUpdateLoop::_singleton->m_eventNotifier;
    EventLogEntry* eventArray = eventMgr->entries.GetData();
    const EventLogEntry* eventEntry = eventArray + eventIndex;

    // Format sprite name "EventIcon<iconIdx>"
    uint32_t iconIdx = static_cast<uint32_t>(eventEntry->popup);
    System::String fmtStr = System::String::Format("EventIcon%i", iconIdx);

    // The same sheet, the index named fmtStr (-1 without a sheet)
    UI::Sprite oldSprite(imageIcon->GetSprite());
    UI::Sprite newSprite(oldSprite.GetSpriteSheet(), fmtStr);
    newSprite.pad0C = 0;   // built in a zeroed slot before
    imageIcon->SetSprite(&newSprite);

    // Set text parameter 0 on textEvent to the event's text
    textEvent->SetParameter(0, eventEntry->text);

    // Set button ID 200 on buttonOk
    buttonOk->SetElementId(200);
}

// ============================================================
// [8] OnElementEvent (0x140068BD0) — vtable +0x40, OnCommand
// ============================================================
// When msg==0x12 (button click) and id==200 (buttonOk):
//   Wraps self in InternalBasePtr, calls UserInterface::CloseDialog.
//
// Original: 0x140068BD0.

void EventPopupDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                      UI::EventId param3, int32_t param4, int32_t /*c*/,
                                      int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 == UI::EventId::Activated && param4 == 200) {
        Object::InternalPtr<UI::DialogInstance> selfIBP(this);
        _ui->CloseDialog(&selfIBP);
    }
}

// ============================================================
// Construction, destruction, reflection
// ============================================================
// EventPopupDialog (DialogInstance, 0xC0 bytes, exe vtable 0x14015bd80): the
// DialogInstance base ctor, then the event index this popup displays at +0xB8
// (OnOpened reads it). EventNotifyDialog's popup branch builds it.

#include "Object/factory_registration.h"
#include <cstddef>

EventPopupDialog::EventPopupDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                                   Object::InternalPtr<UI::DialogInstance> parent,
                                   int32_t index)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0), eventIndex(index) {}

// create 0x140011920: MemAlloc(0xc0, 8), then the ctor.
::Object::Base* EventPopupDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EventPopupDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) EventPopupDialog());
}
// createInPlace 0x140011970 (the tree's in-place build is kept, B8-13).
::Object::Base* EventPopupDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) EventPopupDialog());
}

DEFINE_EXE_TYPE(EventPopupDialog, "UI::DialogInstance")
