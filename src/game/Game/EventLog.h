#pragma once
// ============================================================================
// src/game/Game/EventLog.h — ENDGAME B.4 GAME-a
//
// The 0x28-byte in-game event log at GameUpdateLoop+0x138 (m_eventNotifier):
// the two string tables its messages are formatted from and the message
// array the event-log dialog and the notification pop-ups read.
//
// CLASS NAME UNCONFIRMED: retail gives it no reflected name or vtable;
// "EventLog" is descriptive (the tree's eventlog.cpp).
// No vtable: a plain struct.
//
// RETAIL (Application-x64.exe):
//   Load 0x140096b00: MemAlloc(0x28, 4); qwords +0x00 / +0x08 / +0x18 / +0x20
//     = 0; dword +0x10 = 0x01010101, word +0x14 = 0x0101, then byte +0x10 = 0;
//     later +0x00 = Find("Dialog/StringTable.rsc:events"), +0x08 =
//     Find("Dialog/StringTable.rsc:seasons")
//   Serialize 0x1400952c0 (version 1): Serialize(6, bool*) at +0x10; the last
//     200 entries: uint color, String text, five ints
//   AddEvent 0x140095580 appends through the grow 0x140019b70 (the entry
//     Array's SetSize(n, fill)); Serialize resizes it with 0x1400199a0 (its
//     SetSize(n)); Shutdown 0x1400977c0 runs ~Array 0x14000a8a0 on +0x18
//     (0x140097ab0) and frees the log: nothing else is released
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "system/String.h"   // System::String (the entry's text)
#include "collection/Collection.h"   // Collection::Array (the entry array)

namespace UI { class StringTable; }
namespace IO { class Stream; }
namespace Object { class InternalBasePtr; }

// One message (stride 0x28): Serialize 0x1400952c0's field order.
struct EventLogEntry {
    uint32_t       color;        // +0x00 ARGB (Serialize uint)
    uint8_t        _pad04[4];    // +0x04 never written by the grow's copy
    System::String text;         // +0x08 formatted message (String::Serialize)
    int32_t        eventType;    // +0x10
    int32_t        severity;     // +0x14
    int32_t        tileIndex;    // +0x18 map cell to jump to, -1 = none (0x140095030)
    int32_t        iconIndex;    // +0x1C (0x1400950d0)
    int32_t        popup;        // +0x20 >= 0 shows the pop-up
    uint8_t        _pad24[4];    // +0x24

    // B7 audit F3 (B6 C8): retail builds the stack entry writing the seven fields only
    // (0x14009568d..0x14009569e); +0x04 / +0x24 are never written, by the writer or by the
    // grow's copy. This ctor writes the same seven and leaves the pads as the stack held
    // them (an aggregate initialiser zeroed them: two stores retail lacks).
    EventLogEntry(uint32_t color_, const System::String& text_, int32_t eventType_, int32_t severity_,
                  int32_t tileIndex_, int32_t iconIndex_, int32_t popup_)
        : color(color_), text(text_), eventType(eventType_), severity(severity_),
          tileIndex(tileIndex_), iconIndex(iconIndex_), popup(popup_) {}
    EventLogEntry() = default;   // the array's InitAs / ZeroEventEntry path builds entries in place
};

static_assert(sizeof(EventLogEntry) == 0x28, "EventLogEntry stride 0x28 (0x140019b70)");
static_assert(offsetof(EventLogEntry, color) == 0x00, "EventLogEntry::color @ +0x00");
static_assert(offsetof(EventLogEntry, _pad04) == 0x04, "EventLogEntry +0x04");
static_assert(offsetof(EventLogEntry, text) == 0x08, "EventLogEntry::text @ +0x08");
static_assert(offsetof(EventLogEntry, eventType) == 0x10, "EventLogEntry::eventType @ +0x10");
static_assert(offsetof(EventLogEntry, severity) == 0x14, "EventLogEntry::severity @ +0x14");
static_assert(offsetof(EventLogEntry, tileIndex) == 0x18, "EventLogEntry::tileIndex @ +0x18");
static_assert(offsetof(EventLogEntry, iconIndex) == 0x1C, "EventLogEntry::iconIndex @ +0x1C");
static_assert(offsetof(EventLogEntry, popup) == 0x20, "EventLogEntry::popup @ +0x20");
static_assert(offsetof(EventLogEntry, _pad24) == 0x24, "EventLogEntry +0x24");

// The entry array's one element row (eventlog.cpp): retail's grow 0x140019b70,
// resize 0x1400199a0 and ~Array 0x14000a8a0 are three instantiations over the
// same element (String copy-ctor assign, ~String destroy; the resize adds the
// init arm).
namespace EventLogRows { extern const rtcoll::ArraySpec kSpec_EventEntry; }

class EventLog {
public:
    using EntryArray = Collection::Array<EventLogEntry, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&EventLogRows::kSpec_EventEntry>>;

    // Retail constructs the log inline in Load (0x14009700c..0x14009702c):
    // qwords +0x00 / +0x08 / +0x18 / +0x20 = 0, dword +0x10 = 0x01010101,
    // word +0x14 = 0x0101, then byte +0x10 = 0. The dtor is implicit: retail's
    // Shutdown releases only the entry array (~Array 0x14000a8a0 at 0x140097ab0).
    EventLog() : events(nullptr), seasons(nullptr), entries() {
        notifyCategories[0] = true; notifyCategories[1] = true; notifyCategories[2] = true;
        notifyCategories[3] = true; notifyCategories[4] = true; notifyCategories[5] = true;
        notifyCategories[0] = false;
    }

    UI::StringTable* events;      // +0x00 "Dialog/StringTable.rsc:events" (ExternalPtr)
    UI::StringTable* seasons;     // +0x08 "Dialog/StringTable.rsc:seasons" (ExternalPtr)
    bool           notifyCategories[6]; // +0x10 Serialize(6, bool*); ctor {0, 1, 1, 1, 1, 1}
    uint8_t        _pad16[2];     // +0x16 never written by the ctor
    EntryArray     entries;       // +0x18 data, +0x20 count, +0x24 capacity

    // ---- members (B4 GAME-a; bodies in gameupdateloop_serialize.cpp) ----
    void Serialize(IO::Stream& stream);   // 0x1400952c0

    // ---- members (B4 GAME-a; bodies in eventlog.cpp) ----
    // entityIBP: the entity the message jumps to (null / empty: no location).
    void AddEvent(int eventType, int severity, const Object::InternalBasePtr* entityIBP);   // 0x140095580
    void AddEventWithName(int eventType, int severity, const Object::InternalBasePtr* entityIBP,
                          const System::String* name);                                    // 0x140095750
    void AddCitizenEvent(int eventType, int severity, const Object::InternalBasePtr* entityIBP,
                         const System::String* name, const System::String* title,
                         const System::String* extra);                                    // 0x140094e00
    // The three writers' one shared body (retail inlines it in each).
    void AddEventCore(int eventType, int severity, const Object::InternalBasePtr* entityIBP,
                      const char* const* extra, int nExtra);
    // The date strings every event writer formats ({1} the year, {0} the season): retail
    // bodies of their own, called with the log in RCX (AddCitizenEvent 0x140094e2c MOV
    // R12,RCX; 0x140094e41 MOV RCX,R12) and the String return slot in RDX.
    System::String YearString() const;     // 0x1400951d0 (reads no field: Infect 0x14004845e leaves RCX unset)
    System::String SeasonString() const;   // 0x140095230 (reads seasons, [RCX+0x8] at 0x140095291)
};

static_assert(sizeof(EventLog) == 0x28, "EventLog is 0x28 (Load MemAlloc(0x28, 4))");
static_assert(offsetof(EventLog, events) == 0x00, "EventLog::events @ +0x00");
static_assert(offsetof(EventLog, seasons) == 0x08, "EventLog::seasons @ +0x08");
static_assert(offsetof(EventLog, notifyCategories) == 0x10, "EventLog +0x10");
static_assert(offsetof(EventLog, _pad16) == 0x16, "EventLog +0x16");
static_assert(offsetof(EventLog, entries) == 0x18, "EventLog::entries @ +0x18");
