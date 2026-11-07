// src/game/Game/eventlog.cpp
// Clean reimplementation of the in-game event log writer.
//
// The event-log manager is the 0x28-byte struct at GameUpdateLoop+0x138 (field
// [0x27]); g_pGameWorldSingleton IS the GameUpdateLoop. Layout:
//   +0x00 events StringTable   +0x08 seasons StringTable
//   +0x10 5 category-filter bytes
//   +0x18 event array (0x28-byte entries)   +0x20 count   +0x24 capacity
//
// Event entry (0x28 bytes):
//   +0x00 uint32 ARGB color   +0x08 System::String text
//   +0x10 int eventType   +0x14 int severity   +0x18 int tileIdx
//   +0x1c int iconIndex   +0x20 int popup (>=0 shows popup)
//
// Reimplements (from Application-x64.exe), as EventLog members where retail's
// receiver is the log:
//   AddEvent          0x140095580  — format text, build entry, append
//   AddEventWithName  0x140095750  — {2}=name
//   AddCitizenEvent   0x140094e00  — citizen life-event variant: extra format args
//                                    {2}=citizen name {3}=title {4}=param string, plus
//                                    drop-to-1x-speed tail on critical events
//   EventArray_Grow   0x140019b70  — vector<Event>::resize/append primitive
//   YearString        0x1400951d0  — String::Format("%i", year)
//   SeasonString      0x140095230  — seasons table lookup by month index
//   IconIndex         0x1400950d0  — eventType -> icon index switch
//   TileIndex         0x140095030  — entity -> map tile index (null entity => -1)

#include "ui/UserInterface.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Game/Calendar.h"   // m_dayCounter's class (B.4 GAME-a)
#include "Game/Map.h"   // m_game's class (B.4 GAME-a)
#include "Game/EventLog.h"   // the class these bodies are members of (B.4 GAME-a)
#include "Game/PlayerToolManager.h"   // m_toolManager (the speed tail)
#include "Component/MapComponent.h"   // the tile index's footprint
#include "object/Internal.h"
#include "Component/exe_bodies.h"   // FloorComponent_FindResData
#include "system/String.h"
#include "collection/Collection.h"
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "ui/StringTable.h"    // ENDGAME B.3.6 UI callers (K4)
#include "Game/exe_bodies.h"

// ============================================================
// EventLog::entries' element row — grow 0x140019b70, resize 0x1400199a0, ~Array 0x14000a8a0
// ============================================================
// Retail instantiates the entry Array three times over ONE element: the grow
// (SetSize(n, fill), AddEvent's append), the resize (SetSize(n), Serialize's
// load) and ~Array (Shutdown). Their assign (String copy-ctor) and destroy
// (~String) are identical; only the resize reaches the init arm. One row, one
// typed member (EventLog::EntryArray).
// The grow, compared arm for arm against retail 0x140019b70 (re-decompiled
// 2026-09-13) and against SetSizeCore:
//   * exact-equal early return; `_size` stored before the capacity test;
//   * capacity = newSize, trebled when newSize == oldSize+1 && oldSize != 0;
//   * MemAlloc(capacity * 0x28, align 8);
//   * loop from 0 (moved) or oldSize (in place) to newSize, skipping a null
//     computed element address; i < oldSize copies old[i], else copies `fill`
//     (retail's else arm is UNCONDITIONALLY the fill — there is no init arm);
//   * moved: ~String on every old element, then MemFree(old) — retail calls
//     MemFree without an `if (old)` (the null test is inside 0x1800c7ad0);
//   * in place and shrinking: ~String on the truncated tail.
// The resize (0x1400199a0) is the same core with the init arm (the seed below).
// Two deliberate differences, both out-of-memory-only: the allocation goes
// through the LOCKING static `?MemAlloc@MemoryAllocator@System@@` (same heap),
// and a null `fill` does not fault (retail would). Every grow caller passes a
// non-null entry (AddEventCore below, Component/diseasecomponent.cpp).
namespace {

// The element: EventLogEntry (EventLog.h), stride 0x28, align 8.
// +0x04..+0x07 and +0x24..+0x27 are touched by NEITHER arm.
void CopyEventEntry(EventLogEntry* dst, const EventLogEntry* src) {
    dst->color = src->color;
    new (&dst->text) System::String(src->text);
    dst->eventType = src->eventType;   // +0x10..+0x23: five ints
    dst->severity = src->severity;
    dst->tileIndex = src->tileIndex;
    dst->iconIndex = src->iconIndex;
    dst->popup = src->popup;
}
// The resize's init arm (0x1400199a0). Seed values are load-bearing:
// color=-1, tileIndex=iconIndex=popup=-1, eventType=severity=0.
void ZeroEventEntry(EventLogEntry* dst) {
    dst->color = System::kColorWhite;
    dst->text._data = nullptr;         // null String (retail: one qword 0)
    dst->eventType = 0;                // retail: one qword store, H7
    dst->severity  = 0;
    dst->tileIndex = -1;               // retail: one qword store, H7
    dst->iconIndex = -1;
    dst->popup     = -1;
}
void DestroyEventEntries(EventLogEntry* entries, int count) {
    for (int i = 0; i < count; ++i)
        entries[i].text.~String();
}
void AssignEventEntry(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<EventLogEntry, &CopyEventEntry>(d, s);
}
void InitEventEntry(uint8_t* dst) {
    rtcoll::InitAs<EventLogEntry, &ZeroEventEntry>(dst);
}

}  // namespace

// stays explicit: byteCopy=false with a String copy-ctor `assign`, a real
// `destroy`, and two unwritten holes ([0x04,0x08) and [0x24,0x28)) — a
// combination no C++ element type can express.
const rtcoll::ArraySpec EventLogRows::kSpec_EventEntry = {   // exe 0x140019b70 (grow),
    "Array<EventLogEntry>", 0x28, 8,                         //     0x1400199a0 (resize),
    &AssignEventEntry, &InitEventEntry, &rtcoll::DestroyAs<EventLogEntry, &DestroyEventEntries>, false,   // 0x14000a8a0 (~Array)
    /*assign unwritten*/ 0x24, 0x28,   // plus +0x04..+0x07
    /*init   unwritten*/ 0x24, 0x28,   // plus +0x04..+0x07
    /*seed zero       */ 0,    0 };

// ============================================================
// Helpers
// ============================================================

// YearString (0x1400951d0): String::Format("%i", *(int*)clock), clock = *(gw+0x128),
// read with no test (0x1400951f5 MOV RCX,[RAX+0x128] / 0x1400951fc MOV R8D,[RCX]).
System::String EventLog::YearString() const {
    Calendar* clock = GameUpdateLoop::_singleton->m_dayCounter;   // 0x1400951ee [0x1401c3dd8]
    return System::String::Format("%i", clock->year);
}

// SeasonString (0x140095230): monthIdx = (int)(AsSeconds(clock+8) / 3600 * 12);
// if monthIdx >= 12 -> 0 (0x140095286 CMP / CMOVGE). The text is GetText on this->seasons
// (+0x08) built straight into the caller's return slot (0x14009528e MOV RDX,RDI /
// 0x140095291 MOV RCX,[RBX+0x8]); neither the clock nor the table is tested.
System::String EventLog::SeasonString() const {
    Calendar* clock = GameUpdateLoop::_singleton->m_dayCounter;   // 0x140095259 [0x1401c3dd8]
    int idx = (int)(clock->yearTime.AsSeconds() / 3600.0f * 12.0f);
    if (idx >= 12) idx = 0;
    return seasons->GetText(idx);
}

// SeverityColor: severity -> ARGB text color (shared by both AddEvent variants).
static uint32_t EventLog_SeverityColor(int severity) {
    switch (severity) {
        case 1:  return 0xFFFFFF00u;  // yellow
        case 2:  return 0xFF8080FFu;  // pink
        case 3:  return 0xFFFF6060u;  // red
        case 4:  return 0xFFEA7517u;  // orange
        default: return System::kColorWhite;
    }
}

// PopupIndex: eventType -> popup icon index (0x1a/0x1c disaster start, 0x1b/0x1d
// disaster end; -1 = no modal popup). EventNotifyDialog raises EventPopupDialog
// when the entry's +0x20 field is >= 0.
static int EventLog_PopupIndex(int eventType) {
    if (eventType == 0x1a || eventType == 0x1c) return 0xc;
    if (eventType == 0x1b || eventType == 0x1d) return 0xd;
    return -1;
}

// TileIndex (0x140095030): entity IBP -> map tile index for the message's
// jump-to-location, from the entity's MapComponent (FUN_140004680, registered)
// footprint center: (y + h/2) * mapWidth + (x + w/2). mapWidth = the Map's size
// (gw+0xF8). Null/stale entity or missing MapComponent => -1.
static int EventLog_TileIndex(const ::Object::InternalBasePtr* entityIBP) {
    if (!entityIBP || entityIBP->Get() == nullptr) return -1;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return -1;
    Map* game = gameWorld->m_game;
    if (!game) return -1;

    MapComponent* mc = nullptr;   // the InternalPtr<MapComponent> FindResData writes
    mc = static_cast<ComponentEntity*>(entityIBP->Get())->FindComponent<MapComponent>();   // 0x140004680 (the slot holds the message's entity)
    if (mc == nullptr) return -1;

    // The footprint words, read signed (the tree's reads; retail MOVSX).
    int x = static_cast<int16_t>(mc->tileX0), y = static_cast<int16_t>(mc->tileY0);
    int w = static_cast<int16_t>(mc->tileX1), h = static_cast<int16_t>(mc->tileY1);
    return ((h >> 1) + y) * game->size + (w >> 1) + x;
}

// B4 FORWARD: 1 outside caller file (Component/diseasecomponent.cpp).
// The original 0x140095030 is (unused RCX, entityIBP RDX) — the first param is
// dead (stack-reuse artifact).
extern "C" int EventLog_TileIndex_Reg(void* /*unused*/, const ::Object::InternalBasePtr* entityIBP) {
    return EventLog_TileIndex(entityIBP);
}

// IconIndex (0x1400950d0): eventType -> icon index (feeds "EventIcon%i").
static int EventLog_IconIndex(int eventType) {
    switch (eventType) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: return 0;
        case 0xb: return 1;
        case 0xc: return 2;
        case 0xd: return 5;
        case 0xe: return 8;
        case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: return 6;
        case 0x17: case 0x18: return 7;
        case 0x19: return 9;
        case 0x1a: case 0x1c: return 10;
        case 0x1b: case 0x1d: return 0xb;
        case 0x1e: return 4;
        case 0x1f: return 3;
        default: return -1;
    }
}

// ============================================================
// AddEvent family core (0x140095580 / 0x140095750 / 0x140094e00)
// ============================================================
// Retail ships THREE event-log writers that are byte-identical apart from how
// many extra args they hand System::String::FormatIndexed:
//
//   0x140095580 AddEvent          {0}=season {1}=year
//   0x140095750 AddEventWithName  ... {2}=name          (scenario win/loss)
//   0x140094e00 AddCitizenEvent   ... {2}=name {3}=title {4}=extra
//
// Everything else — the severity->colour table, the popup/icon switches, the
// entity->tile lookup, the 0x28-byte entry append, and the "drop to 1x speed on
// a critical event" tail — is the same instruction sequence in all three, so
// they share one core here. Porting the same body three times is how the copies
// drift apart (see reference_one_retail_fn_ported_twice_diverges); that already
// happened here — the previous AddEvent port was missing the speed tail that
// retail 0x140095580 does have.
//
// entityIBP is an optional InternalBasePtr* for the map-jump tile (null =>
// tileIdx -1, no location). `extra` holds the String DATA pointers (char*) for
// the {2}.. args, exactly as the original passes them.
void EventLog::AddEventCore(int eventType, int severity, const ::Object::InternalBasePtr* entityIBP,
                            const char* const* extra, int nExtra) {
    // B8 RES (row 1000): no test of the game world (retail's year / season helpers read
    // it), of the events table or of the settings / tool manager in the tail; the four
    // String locals die right after the grow, before the tail.

    {
        // --- Build the display text, in retail's construction order: year (0x1400951d0),
        // season (0x140095230), the template from GetText on [this+0x0] with no test
        // (0x1400955d5), the formatted text (0x1400955ea).
        System::String yearStr   = YearString();
        System::String seasonStr = SeasonString();
        System::String templateStr(events->GetText(eventType));
        System::String formatted;

        // FormatIndexed(fmt=template, %0=season, %1=year, %2..=extra). Args are the
        // String DATA pointers (char*). Fall back to the raw template if the export
        // is unresolved.
        const char* tmplData = templateStr._data;
        const char* seaData  = seasonStr._data;
        const char* yrData   = yearStr._data;
        switch (nExtra) {
            case 0:
                formatted = System::String::FormatIndexed(tmplData, seaData, yrData);
                break;
            case 1:
                formatted = System::String::FormatIndexed(tmplData, seaData, yrData,
                                                          extra[0]);
                break;
            default:  // 3 — the citizen variant
                formatted = System::String::FormatIndexed(tmplData, seaData, yrData,
                                                          extra[0], extra[1], extra[2]);
                break;
        }

        uint32_t color = EventLog_SeverityColor(severity);
        int popup      = EventLog_PopupIndex(eventType);
        int iconIndex  = EventLog_IconIndex(eventType);
        int tileIdx    = EventLog_TileIndex(entityIBP);  // null entity -> -1

        // --- Build the 0x28-byte entry and append ---
        // Retail 0x140095580 builds it on the stack at RSP+0x48 in this order: color
        // (0x140095675), the String copy ctor into +0x08 (0x140095686), the five
        // dwords (0x14009568d..0x14009569e); then the grow (0x1400956b2) and the
        // entry's ~String (0x1400956bd) right after it, before the autoSlow tail.
        // The block ends there. The two pads are never written: not by retail's writer, not
        // by the grow's copy (CopyEventEntry writes the fields only) -- B6 C8, audit F3.
        {
            const EventLogEntry entry(color, formatted, eventType, severity, tileIdx, iconIndex, popup);   // the pads stay unwritten (audit F3)
            int count = entries.GetSize();
            entries.SetSize(count + 1, entry);   // the grow, 0x140019b70
        }   // ~entry: the text's release, 0x1400956bd
    }   // ~formatted, ~template, ~season, ~year: 0x1400956c9 / 0x1400956d5 / 0x1400956e1 / 0x1400956ed

    // --- Tail: critical events (0xb/0xc/0x1e/0x1f) drop the game to 1x speed
    // when the settings byte +0x17 ("pause on events") is set. Speed lives in
    // the tool manager (gw+0x120)+0x34. Present in ALL THREE retail writers.
    // 0x140095705-0x140095720: [0x1401c3de0]+0x17 read, the singleton RE-READ
    // (0x140095712) and [[+0x120]+0x34] stored, nothing tested.
    if (eventType > 0xa && (eventType < 0xd || (unsigned)(eventType - 0x1e) < 2u)) {
        if (g_gameSettings->autoSlow != 0) {
            GameUpdateLoop::_singleton->m_toolManager->field_0x34 = 1;
        }
    }
}

// ============================================================
// AddEvent (0x140095580)
// ============================================================
// The plain overload: "{0}=season {1}=year".
void EventLog::AddEvent(int eventType, int severity, const ::Object::InternalBasePtr* entityIBP) {
    AddEventCore(eventType, severity, entityIBP, nullptr, 0);
}

// ============================================================
// AddEventWithName (0x140095750)
// ============================================================
// The named overload. Retail callers: GameUpdateLoop::OnScenarioComplete
// (0x140096830) — event types 0x1a/0x1b (scenario failed/won) with the parent
// Achievement's title as {2} — and the award toast (eventlog_achievement.cpp).
void EventLog::AddEventWithName(int eventType, int severity, const ::Object::InternalBasePtr* entityIBP,
                                const System::String* name) {
    const char* extra[1] = { name ? name->_data : "" };
    AddEventCore(eventType, severity, entityIBP, extra, 1);
}

// ============================================================
// AddCitizenEvent (0x140094e00)
// ============================================================
// Citizen life-event variant (only exe caller: CitizenComponent_PostLifeEvent
// 0x140113af0, the former "AssignState" misnomer). Three extra format args —
// {2}=citizen name {3}=title (Child/Student/profession) {4}=extra param string.
void EventLog::AddCitizenEvent(int eventType, int severity, const ::Object::InternalBasePtr* entityIBP,
                               const System::String* name, const System::String* title,
                               const System::String* extraStr) {
    const char* extra[3] = {
        name     ? name->_data     : "",
        title    ? title->_data    : "",
        extraStr ? extraStr->_data : "",
    };
    AddEventCore(eventType, severity, entityIBP, extra, 3);
}
