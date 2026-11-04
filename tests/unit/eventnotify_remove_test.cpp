// ============================================================================
// tests/unit/eventnotify_remove_test.cpp — EventNotifyDialog_RemoveEntry (0x140011670)
// ============================================================================
// Review F-212: retail destroys the String of every slot from removeIndex to the last
// (CALL [0x140152af0] ~String at 0x1400116c7, inside the loop) and copy-constructs slot i
// from slot i + 1 right after (CALL [0x140151428] at 0x140011712). The vacated last slot
// is destroyed and not rebuilt. Every String therefore keeps exactly one holder per slot
// that shows it. The test reads the refcounts after a removal.
// ============================================================================
#include "unit.h"
#include "Dialog/EventNotifyDialog.h"
#include "system/String.h"
#include <new>

namespace {

int RefCount(const System::String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-1] : 0;
}

// The array header, never destroyed: the test owns the element storage and empties
// the live slots itself (raw storage), so ~Array and its element spec are not linked.
struct EntryArray {
    union {
        Collection::Array<EventNotifyEntry> a;
    };
    alignas(EventNotifyEntry) unsigned char raw[4 * sizeof(EventNotifyEntry)];

    explicit EntryArray(const System::String* texts, int n) {
        new (&a) Collection::Array<EventNotifyEntry>();
        a._data = reinterpret_cast<EventNotifyEntry*>(raw);
        a._size = n;
        a._capacity = 4;
        for (int i = 0; i < n; i++) {
            EventNotifyEntry& e = *new (&a._data[i]) EventNotifyEntry();
            e.timer._ticks = 100 + i;
            e.eventIndex = 10 + i;
            e.text = texts[i];
            e.color = 0xff000000u + (uint32_t)i;
        }
    }
    ~EntryArray() {
        for (int i = 0; i < a._size; i++)
            a._data[i].text.~String();
    }
};

}  // namespace

UNIT_TEST(eventnotify_remove_first_of_three) {
    System::String texts[3] = {System::String("zero"), System::String("one"), System::String("two")};
    {
        EntryArray arr(texts, 3);
        CHECK_EQ(RefCount(texts[1]), 2);
        EventNotifyDialog_RemoveEntry(arr.a, 0);
        CHECK_EQ(arr.a._size, 2);
        CHECK_EQ(arr.a._data[0].eventIndex, 11);
        CHECK_EQ(arr.a._data[1].eventIndex, 12);
        CHECK(arr.a._data[0].text._data == texts[1]._data);
        CHECK(arr.a._data[1].text._data == texts[2]._data);
        // One holder in `texts`, one in the slot that shows it; "zero" is held by `texts` alone.
        CHECK_EQ(RefCount(texts[0]), 1);
        CHECK_EQ(RefCount(texts[1]), 2);
        CHECK_EQ(RefCount(texts[2]), 2);
    }
    // Every slot released: `texts` is the last holder of each.
    CHECK_EQ(RefCount(texts[0]), 1);
    CHECK_EQ(RefCount(texts[1]), 1);
    CHECK_EQ(RefCount(texts[2]), 1);
}

UNIT_TEST(eventnotify_remove_middle_and_last) {
    System::String texts[3] = {System::String("a"), System::String("b"), System::String("c")};
    {
        EntryArray arr(texts, 3);
        EventNotifyDialog_RemoveEntry(arr.a, 1);
        CHECK_EQ(arr.a._size, 2);
        CHECK(arr.a._data[0].text._data == texts[0]._data);
        CHECK(arr.a._data[1].text._data == texts[2]._data);
        CHECK_EQ(arr.a._data[1].color, 0xff000002u);
        CHECK_EQ(RefCount(texts[1]), 1);
        CHECK_EQ(RefCount(texts[2]), 2);
        // The last slot: destroyed, nothing shifted.
        EventNotifyDialog_RemoveEntry(arr.a, 1);
        CHECK_EQ(arr.a._size, 1);
        CHECK_EQ(RefCount(texts[0]), 2);
        CHECK_EQ(RefCount(texts[2]), 1);
    }
    CHECK_EQ(RefCount(texts[0]), 1);
    CHECK_EQ(RefCount(texts[1]), 1);
    CHECK_EQ(RefCount(texts[2]), 1);
}
