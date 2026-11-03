// ============================================================================
// tests/unit/review_engine_ub_test.cpp -- undefined behaviour the 2026-11-03 review found in
// engine and launcher code (docs/history/review_20261103, group engine-ub-misc). Each test
// drives the input that reached the UB; the fixed bodies give retail's results for it.
//   * F-003 MemoryAllocator::ExpandMemoryRangeTable: memcpy from the null first range table.
//   * F-009 UI::String{null, -1} -> System::String: StringTable::GetText on a null `this`.
//   * F-087 atoi on settings.cfg / environment text: out-of-range text clamps.
// The terrain ray's INT_MIN cell (F-004) is in terrain_ray_test.cpp, beside its Terrain fixture.
//
// F-003's memcpy(dst, nullptr, 0) is only seen by UBSan's nonnull-attribute check (glibc
// declares memcpy nonnull; MinGW does not), so on Windows the test passes before and after.
// F-009 traps under -fsanitize=undefined -fsanitize-trap=undefined (MinGW GCC too).
// ============================================================================
#include "unit.h"

#include <climits>
#include <cstdint>

#include "app/clamped_atoi.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "ui/String.h"
#include "ui/StringTable.h"

// F-003: the first Allocate on a fresh allocator grows the range table from capacity 0, whose
// `ranges` is null. Retail copies zero bytes from it; the port skips the copy.
UNIT_TEST(review_ub_allocator_first_range_table_from_null) {
    RtSystemAllocatorInstallPlatform();
    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    CHECK(a.ranges == nullptr);
    CHECK_EQ(a.rangeCapacity, 0);
    CHECK(a.Startup(1, 0));
    void* p = a.Allocate(64, 8);
    CHECK(p != nullptr);
    CHECK(a.ranges != nullptr);
    CHECK_EQ(a.rangeCapacity, 2);
    CHECK_EQ(a.rangeCount, 1);
    a.Free(p);
    a.Shutdown();
}

// F-009: a UI::String that names no text is {table = null, index = -1}; its conversion to
// System::String is retail's `table->GetText(index)`, and GetText(int) returns the empty
// string for a negative index before it reads the table.
UNIT_TEST(review_ub_ui_string_null_table_converts_to_empty) {
    const UI::String none;
    CHECK(none.table == nullptr);
    CHECK_EQ(none.index, -1);
    const System::String text = none;
    CHECK(text._data == nullptr);
}

// F-087: atoi's result is undefined when the value does not fit an int. The launcher's
// settings reader and Options read through ClampedAtoi: atoi's parse (leading space, a sign,
// decimal digits up to the first other character, 0 when there are none), clamped to int.
UNIT_TEST(review_ub_clamped_atoi_text) {
    CHECK_EQ(ClampedAtoi("42"), 42);
    CHECK_EQ(ClampedAtoi("  -17x"), -17);
    CHECK_EQ(ClampedAtoi("+8"), 8);
    CHECK_EQ(ClampedAtoi("abc"), 0);
    CHECK_EQ(ClampedAtoi(""), 0);
    CHECK_EQ(ClampedAtoi("2147483647"), INT_MAX);
    CHECK_EQ(ClampedAtoi("-2147483648"), INT_MIN);
    CHECK_EQ(ClampedAtoi("2147483648"), INT_MAX);
    CHECK_EQ(ClampedAtoi("-2147483649"), INT_MIN);
    CHECK_EQ(ClampedAtoi("99999999999999999999999"), INT_MAX);
    CHECK_EQ(ClampedAtoi("-99999999999999999999999"), INT_MIN);
}
