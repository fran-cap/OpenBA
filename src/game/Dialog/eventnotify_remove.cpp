// ============================================================================
// src/game/Dialog/eventnotify_remove.cpp — the ribbon's entry removal
//
// Its own file so the unit test (tests/unit/eventnotify_remove_test.cpp) links the
// real body without the dialog.
// ============================================================================

#include "Dialog/EventNotifyDialog.h"
#include <new>

// ============================================================
// EventNotifyDialog_RemoveEntry (0x140011670)
// ============================================================
// Removes element [removeIndex] of the +0xD8 array. For each slot i from removeIndex
// to the last, retail destroys slot i's String (~String, CALL [0x140152af0] at
// 0x1400116c7, inside the loop), then, when slot i + 1 exists, copies its fields
// into slot i and copy-CONSTRUCTS its String there (CALL [0x140151428] at
// 0x140011712). The vacated last slot is destroyed and not rebuilt; the count
// drops by one (DEC [RBX + 0x8]) whether or not removeIndex was in range.
// (Collection::Array has no remove member.)
void EventNotifyDialog_RemoveEntry(Collection::Array<EventNotifyEntry>& entries, int removeIndex) {
    for (int i = removeIndex; i < entries._size; i++) {
        entries._data[i].text.~String();
        if (i + 1 < entries._size) {
            EventNotifyEntry& dst = entries._data[i];
            const EventNotifyEntry& src = entries._data[i + 1];
            dst.timer = src.timer;
            dst.eventIndex = src.eventIndex;
            dst.icon = src.icon;
            new (&dst.text) System::String(src.text);
            dst.color = src.color;
        }
    }
    entries._size--;
}
