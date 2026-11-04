#pragma once
// ============================================================================
// src/game/Dialog/EventNotifyDialog.h — ENDGAME B.3.8 block DLG
//
// The event-notification ribbon under the toolbar.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "EventNotifyDialog", parent "UI::DialogInstance", ti 0x1401c4f70
//   create 0x140011500 (MemAlloc(0xf0, 8)), createInPlace 0x140011540
//   ctor 0x140068ab0, 0x140068b60
//   table 0x14015bca0, 10 slots:
//     [0] 0x140011550 scalar deleting dtor (frees the two arrays)
//     [1] TypeName  [2] Type  [3] DialogInstance::Persist (import)
//     [4] 0x1400686d0 OnOpened   [5] 0x1400686c0 OnClosed `RET 0`
//     [6] 0x1400680c0 OnUpdateLayout   [7] 0x140067c30 OnUpdate
//     [8] 0x140068680 OnElementEvent   [9] DialogInstance::OnSerializeSettings
//   sizeof 0xf0.
//
// B.4 W1 (DLG-b): the fields are named from what retail does with them. The
// two arrays are retail's `Collection::Array<T>` members: the scalar deleting
// dtor 0x140011550 runs `~Array<Entry>` 0x140011600 on +0xd8, then the inlined
// trivial `~Array` on +0xc8 (MemFree, then one qword zero store at +0xc8 and one
// at +0xd0) — the compiler's member destructors, in reverse declaration order.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "ui/Sprite.h"
#include "collection/Collection.h"
#include "system/String.h"
#include "system/Value.h"
#include "Object/exe_type.h"

// One ribbon slot (0x18): OnOpened 0x1400686d0 fills it from "notifyContent"'s
// children; OnUpdateLayout 0x1400680c0 reads it.
struct EventNotifySlot {
    Object::InternalPtr<UI::Element> group;    // +0x00 notifyContent child i (its tooltip string is the text)
    Object::InternalPtr<UI::Element> image;    // +0x08 "notifyImage" (an ImageElement)
    Object::InternalPtr<UI::Element> button;   // +0x10 "notifyButton", element id 1500 + i
};
static_assert(sizeof(EventNotifySlot) == 0x18, "EventNotifySlot is 0x18 (the +0xc8 array stride)");
static_assert(offsetof(EventNotifySlot, group) == 0x0, "EventNotifySlot::group");
static_assert(offsetof(EventNotifySlot, image) == 0x8, "EventNotifySlot::image");
static_assert(offsetof(EventNotifySlot, button) == 0x10, "EventNotifySlot::button");

// One shown notification (0x30): OnUpdate 0x140067c30 builds it on the stack
// and appends it with Array<T>::SetSize 0x140011750; 0x140011670 removes one.
struct EventNotifyEntry {
    System::Time   timer;        // +0x00 counts down by the frame time; removed below 1
    int32_t        eventIndex;   // +0x08 event-log index
    UI::Sprite     icon;         // +0x10 "EventIcon%i" from the first slot's sheet
    System::String text;         // +0x20 the event text past its "category:" prefix
    uint32_t       color;        // +0x28 ARGB from the event's severity
};
static_assert(offsetof(EventNotifyEntry, timer) == 0x00, "EventNotifyEntry::timer @ +0x00");
static_assert(offsetof(EventNotifyEntry, eventIndex) == 0x08, "EventNotifyEntry::eventIndex @ +0x08");
static_assert(offsetof(EventNotifyEntry, icon) == 0x10, "EventNotifyEntry::icon @ +0x10");
static_assert(offsetof(EventNotifyEntry, text) == 0x20, "EventNotifyEntry::text @ +0x20");
static_assert(offsetof(EventNotifyEntry, color) == 0x28, "EventNotifyEntry::color @ +0x28");
static_assert(sizeof(EventNotifyEntry) == 0x30, "EventNotifyEntry is 0x30 (the +0xd8 array stride)");

// 0x140011670: removes entries[removeIndex] (eventnotify_remove.cpp). OnUpdate 0x140067c30
// calls it when a shown notification's timer runs out.
void EventNotifyDialog_RemoveEntry(Collection::Array<EventNotifyEntry>& entries, int removeIndex);

class EventNotifyDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(EventNotifyDialog)

public:
    EventNotifyDialog() = default;                                  // create 0x140011500
    EventNotifyDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                      Object::InternalPtr<UI::DialogInstance> parent);  // 0x140068ab0

    // 0x140011550's body is the member destructors alone.
    ~EventNotifyDialog() override = default;                       // [0] 0x140011550
    void OnOpened() override;                                      // [4] 0x1400686d0
    void OnClosed() override;                                      // [5] 0x1400686c0 RET
    void OnUpdateLayout() override;                                // [6] 0x1400680c0
    void OnUpdate(const System::Time& time) override;              // [7] 0x140067c30
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        int32_t a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140068680

    // 0x1400685a0: centre the camera on the map tile of shown entry `index`.
    void NavigateToEvent(int32_t index);                           // 0x1400685a0

public:
    Object::InternalPtr<UI::DialogInstance> parentDialog;  // +0xB8 the toolbar dialog (ctor's parent)
    float    smoothedY;                                    // +0xC0 ribbon top padding, lerped (MOVSS)
    int32_t  lastEventIndex;                               // +0xC4 last event-log index seen, ctor -1
    Collection::Array<EventNotifySlot>  slots;             // +0xC8 one per notifyContent child
    Collection::Array<EventNotifyEntry> entries;           // +0xD8 the notifications shown
    Object::InternalPtr<UI::Element> ribbonNotify;         // +0xE8 "ribbonNotify"
};

static_assert(offsetof(EventNotifyDialog, parentDialog) == 0xB8, "EventNotifyDialog +0xb8");
static_assert(offsetof(EventNotifyDialog, smoothedY) == 0xC0, "EventNotifyDialog +0xc0");
static_assert(offsetof(EventNotifyDialog, lastEventIndex) == 0xC4, "EventNotifyDialog +0xc4");
static_assert(offsetof(EventNotifyDialog, slots) == 0xC8, "EventNotifyDialog +0xc8");
static_assert(offsetof(EventNotifyDialog, entries) == 0xD8, "EventNotifyDialog +0xd8");
static_assert(offsetof(EventNotifyDialog, ribbonNotify) == 0xE8, "EventNotifyDialog +0xe8");
static_assert(sizeof(EventNotifyDialog) == 0xF0, "EventNotifyDialog is 0xf0 (create 0x140011500)");
static_assert(alignof(EventNotifyDialog) == 8, "EventNotifyDialog is qword-aligned");
