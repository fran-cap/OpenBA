// ============================================================================
// tests/unit/ext_dialog_position_drift_test.cpp -- fix.dialog_position_drift
// ============================================================================
// src/ext/dialog_position_drift/drift_math.h without the game: the stored upper-left a laid-out dialog
// is moved from (DialogInstance::SetUpperLeft's delta base). The registry row is in
// ext_registry_test.cpp; the seam (UserInterface::_extDialogPlace at kExtPlaceUpperLeft) needs the game.
// ============================================================================
#include "unit.h"
#include "ext/dialog_position_drift/drift_math.h"

namespace {

void Sync(int32_t alignment, int32_t l, int32_t t, int32_t r, int32_t b, int32_t leftPad, int32_t topPad,
          int32_t& x, int32_t& y) {
    Ext::DialogPositionDrift::LaidOutUpperLeft(alignment, l, t, r, b, leftPad, topPad, x, y);
}

}  // namespace

UNIT_TEST(ext_dialog_position_drift_laid_out_upper_left) {
    // The S25 trace: placed at (343, 216), laid out 404 x 220 and pulled back to the bottom margin (433).
    int32_t x = 343;
    int32_t y = 216;
    Sync(0, 343, 213, 747, 433, 0, 0, x, y);
    CHECK(x == 343);
    CHECK(y == 213);
    // Pads: the root is laid out at upper-left + pad.
    x = 0;
    y = 0;
    Sync(0, 24, 26, 100, 100, 4, 6, x, y);
    CHECK(x == 20);
    CHECK(y == 20);
    // A centred axis (4: centre / centre) and a bottom-right one (8) keep their upper-left.
    x = 7;
    y = 9;
    Sync(4, 100, 100, 200, 200, 0, 0, x, y);
    CHECK(x == 7);
    CHECK(y == 9);
    Sync(8, 100, 100, 200, 200, 0, 0, x, y);
    CHECK(x == 7);
    CHECK(y == 9);
    // Top-centred (1): only y follows the root.
    Sync(1, 100, 50, 200, 200, 0, 0, x, y);
    CHECK(x == 7);
    CHECK(y == 50);
    // Not laid out yet (an empty root) or an alignment out of range: nothing.
    x = 343;
    y = 216;
    Sync(0, 343, 216, 343, 216, 0, 0, x, y);
    CHECK(x == 343);
    CHECK(y == 216);
    Sync(9, 0, 0, 100, 100, 0, 0, x, y);
    CHECK(x == 343);
    CHECK(y == 216);
}

UNIT_TEST(ext_dialog_position_drift_sync_is_idempotent) {
    // ext.phone_menu and the fix may both sync before one SetUpperLeft: the second is a no-op.
    int32_t x = 100;
    int32_t y = 300;
    Sync(0, 104, 280, 504, 500, 4, 0, x, y);
    const int32_t x1 = x;
    const int32_t y1 = y;
    Sync(0, 104, 280, 504, 500, 4, 0, x, y);
    CHECK(x == x1 && y == y1);
    CHECK(x == 100);
    CHECK(y == 280);
}
