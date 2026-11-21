// ============================================================================
// tests/unit/ext_edge_scroll_focus_test.cpp -- fix.edge_scroll_focus's rule
// ============================================================================
// src/ext/edge_scroll_focus/edge_scroll_focus.h without the world (docs/retail_bugs.md §37).
// ============================================================================
#include "unit.h"

#include "ext/edge_scroll_focus/edge_scroll_focus.h"

using Ext::EdgeScrollFocus::ReadsPointer;

UNIT_TEST(ext_edge_scroll_focus_row_off_is_retails_test) {
    // retail reads the pointer whenever the Edge Scroll option is on, foreground or not
    CHECK(ReadsPointer(true, false, true));
    CHECK(ReadsPointer(true, false, false));
    CHECK(!ReadsPointer(false, false, true));
    CHECK(!ReadsPointer(false, false, false));
}

UNIT_TEST(ext_edge_scroll_focus_row_on_skips_the_unfocused_frame) {
    CHECK(ReadsPointer(true, true, true));
    CHECK(!ReadsPointer(true, true, false));
}

UNIT_TEST(ext_edge_scroll_focus_row_on_keeps_the_option_off) {
    CHECK(!ReadsPointer(false, true, true));
    CHECK(!ReadsPointer(false, true, false));
}
