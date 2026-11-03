// ============================================================================
// tests/unit/review_engine_ub_test.cpp -- undefined behaviour the 2026-11-03 review found in
// engine and launcher code (docs/history/review_20261103, group engine-ub-misc). Each test
// drives the input that reached the UB; the fixed bodies give retail's results for it.
//   * F-003 MemoryAllocator::ExpandMemoryRangeTable: memcpy from the null first range table.
//   * F-009 UI::String{null, -1} -> System::String: StringTable::GetText on a null `this`.
//   * F-049 kSpec_MeshNode's init row: GraphicsMesh::Node::_parent left indeterminate.
//   * F-087 atoi on settings.cfg / environment text: out-of-range text clamps.
// The terrain ray's INT_MIN cell (F-004) is in terrain_ray_test.cpp, beside its Terrain fixture.
//
// F-003 and F-009 are observed only by a UBSan build on Linux (clang-19, -fsanitize=undefined
// -fno-sanitize=vptr): before the fixes it stops at "null pointer passed as argument 2, which is
// declared to never be null" (allocator.cpp, glibc's nonnull memcpy) and "member call on null
// pointer of type 'UI::StringTable'" (sprite.cpp). On MinGW both tests pass before and after:
// MinGW's memcpy carries no nonnull attribute, and GCC's UBSan did not flag the member call.
// F-049's indeterminate read is seen directly: the test plants a stale parent index.
// ============================================================================
#include "unit.h"

#include <climits>
#include <cstdint>
#include <cstring>

#include "app/clamped_atoi.h"
#include "graphics/GraphicsMesh.h"
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

// F-049: kSpec_MeshNode's init row (retail 0x18005aa30's fill loop) leaves the parent index
// at +0x08 unwritten, and the node write path (RtGraphics_SerializeMeshNodes) then serializes
// it: the fuzz seed rsc_serialize/seed_5_0 carries ASan's heap fill `be be be be` there. A
// shrink keeps the buffer (array_test.cpp), so re-growing in place re-runs the init row over
// a stale parent index; the fixed row zeroes it, the transforms are identity either way.
UNIT_TEST(review_ub_mesh_node_init_zeroes_parent) {
    Graphics::GraphicsMesh::NodeArray nodes;
    nodes.SetSize(2);
    CHECK_EQ(nodes._capacity, 2);
    Graphics::GraphicsMesh::Node* const buffer = nodes._data;
    for (int i = 0; i < 2; ++i) {
        std::memset(&buffer[i]._parent, 0xbe, sizeof(buffer[i]._parent));
    }
    nodes.SetSize(0);
    nodes.SetSize(2);
    CHECK(nodes._data == buffer);
    for (int i = 0; i < 2; ++i) {
        const Graphics::GraphicsMesh::Node& n = nodes._data[i];
        CHECK(n._name._data == nullptr);
        CHECK_EQ(n._parent, 0);
        CHECK_EQ(n._local.rotation.w, 1.0f);
        CHECK_EQ(n._bind.rotation.w, 1.0f);
    }
    nodes.SetSize(0);
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
