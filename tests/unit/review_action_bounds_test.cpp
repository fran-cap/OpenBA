// tests/unit/review_action_bounds_test.cpp
// The tests Actions apply to values read back from a save (src/game/Action/saved_bounds.h):
// review F-105 (CompleteRoad roadTileIndex), F-106 (CreateRawMaterial materialCellIdx),
// F-107 (MoveTo pathCount / pathIndex) and F-160 (MoveTo / PathSetup float -> cell).
// The call sites (AbstractImpl, Persist, PathSetup) need a live world; they call these.
// All platforms.

#include "unit.h"
#include "Action/saved_bounds.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

// Retail's CVTTSS2SI answer for an out-of-range or NaN float is 0x80000000, which the
// clamp turns into cell 0. The conversion is evaluated at compile time here: a float
// outside int's range makes the old static_cast<int> body a non-constant expression.
constexpr float kHuge = 1e10f;
static_assert(ActionBounds::WorldToCellAxis(kHuge, 256) == 0, "1e10 -> 0x80000000 -> cell 0");
static_assert(ActionBounds::WorldToCellAxis(-kHuge, 256) == 0, "-1e10 -> 0x80000000 -> cell 0");
static_assert(ActionBounds::WorldToCellAxis(2147483648.0f, 256) == 0, "2^31 -> 0x80000000 -> cell 0");
static_assert(ActionBounds::WorldToCellAxis(std::numeric_limits<float>::quiet_NaN(), 256) == 0,
              "NaN -> 0x80000000 -> cell 0");
static_assert(ActionBounds::WorldToCellAxis(2147483520.0f, 256) == 255, "the largest float below 2^31 clamps high");

}  // namespace

UNIT_TEST(review_action_bounds_map_cell) {
    const int32_t cellCount = 256 * 256;
    CHECK(ActionBounds::IsMapCell(0, cellCount));
    CHECK(ActionBounds::IsMapCell(cellCount - 1, cellCount));
    CHECK(!ActionBounds::IsMapCell(cellCount, cellCount));
    CHECK(!ActionBounds::IsMapCell(-1, cellCount));
    CHECK(!ActionBounds::IsMapCell(INT32_MIN, cellCount));
    CHECK(!ActionBounds::IsMapCell(INT32_MAX, cellCount));
    CHECK(!ActionBounds::IsMapCell(0, 0));
}

UNIT_TEST(review_action_bounds_path_count) {
    // A saved record {pathCount = 0x140}: keep 0x100, discard 0x40.
    int32_t count = 0x140;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0x40);
    CHECK_EQ(count, 0x100);

    count = 0x7FFFFFFF;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0x7FFFFFFF - 0x100);
    CHECK_EQ(count, 0x100);

    count = -5;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0);
    CHECK_EQ(count, 0);

    // Valid counts are untouched.
    count = 0x100;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0);
    CHECK_EQ(count, 0x100);
    count = 17;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0);
    CHECK_EQ(count, 17);
    count = 0;
    CHECK_EQ(ActionBounds::ClampSavedPathCount(count), 0);
    CHECK_EQ(count, 0);
}

UNIT_TEST(review_action_bounds_path_index) {
    CHECK_EQ(ActionBounds::SavedPathIndex(-7), -1);
    CHECK_EQ(ActionBounds::SavedPathIndex(INT32_MIN), -1);
    // -1 and node numbers are untouched; one at or past pathCount is Execute's own "done".
    CHECK_EQ(ActionBounds::SavedPathIndex(-1), -1);
    CHECK_EQ(ActionBounds::SavedPathIndex(0), 0);
    CHECK_EQ(ActionBounds::SavedPathIndex(0xFF), 0xFF);
    CHECK_EQ(ActionBounds::SavedPathIndex(0x7FFFFFFF), 0x7FFFFFFF);
}

UNIT_TEST(review_action_bounds_world_to_cell) {
    // In-range values: truncation toward zero, then the clamp, as CVTTSS2SI + clamp.
    CHECK_EQ(ActionBounds::WorldToCellAxis(12.7f, 256), 12);
    CHECK_EQ(ActionBounds::WorldToCellAxis(0.0f, 256), 0);
    CHECK_EQ(ActionBounds::WorldToCellAxis(-0.5f, 256), 0);
    CHECK_EQ(ActionBounds::WorldToCellAxis(-3.5f, 256), 0);
    CHECK_EQ(ActionBounds::WorldToCellAxis(255.99f, 256), 255);
    CHECK_EQ(ActionBounds::WorldToCellAxis(256.0f, 256), 255);
    CHECK_EQ(ActionBounds::WorldToCellAxis(1e9f, 256), 255);
    // Out of int's range or NaN: cell 0, retail's 0x80000000 clamped.
    volatile float huge = 1e10f;
    volatile float nan = std::nanf("");
    CHECK_EQ(ActionBounds::WorldToCellAxis(huge, 256), 0);
    CHECK_EQ(ActionBounds::WorldToCellAxis(-huge, 256), 0);
    CHECK_EQ(ActionBounds::WorldToCellAxis(nan, 256), 0);
}
