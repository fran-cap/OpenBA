// ============================================================================
// tests/unit/ext_toolbar_window_bounds_test.cpp -- the toolbar host record's position from a save
// ============================================================================
// A crafted `toolbar` host record (u8 1, i32 INT32_MAX, i32 INT32_MAX, String "") reaches
// DialogInstance::SetUpperLeft unchanged (toolbar_window.cpp), whose faithful clamp computes
// `w + x` / `h + y` in int32: signed overflow for any positive dialog size. The game itself never
// writes such a position (OnUpdate stores drag positions from int16 mouse coordinates).
// ============================================================================
#include "unit.h"
#include "ext/savetail.h"
#include "ext/toolbar/window_state.h"

#include <climits>
#include <vector>

UNIT_TEST(ext_toolbar_window_decode_refuses_overflowing_position) {
    std::vector<uint8_t> hostile;
    Ext::SaveTail::Writer w(hostile);
    const uint8_t open = 1;
    w.Bytes(&open, 1);
    w.I32(INT32_MAX);
    w.I32(INT32_MAX);
    w.String("");

    const Ext::SaveTail::HostCodec& c = Ext::Toolbar::WindowCodec();
    const bool accepted = c.decode(hostile.data(), hostile.size(), 1);
    const Ext::Toolbar::WindowState& s = Ext::Toolbar::SavedWindow();
    // What SetUpperLeft computes next (dialog.cpp, retail 0x180114a60): `w + x` with the dialog's
    // positive padded width. 1 is the smallest such width.
    int32_t sum = 0;
    const bool overflows = accepted && s.open && __builtin_add_overflow(int32_t(1), s.x, &sum);
    if (accepted) {
        std::printf("  decode accepted open=%d x=%d y=%d; 1 + x overflows int32: %s\n", s.open ? 1 : 0, s.x, s.y,
                    overflows ? "yes" : "no");
    }
    CHECK(!overflows);
    CHECK(!accepted);
    Ext::SaveTail::ResetAllState();
}
