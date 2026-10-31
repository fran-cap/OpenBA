// ============================================================================
// tests/unit/ext_outline_with_panel_test.cpp -- fix.outline_with_panel's per-frame record
// ============================================================================
// src/ext/outline_with_panel/hold_set.h: which outlines a frame holds back (docs/retail_bugs.md §16,
// docs/extensions/handins/OUTLINEWITHPANEL.md). The seams need the running game.
// ============================================================================
#include "unit.h"
#include "ext/outline_with_panel/hold_set.h"

namespace {

using Set = Ext::OutlineWithPanel::HoldSet<4>;

int a = 0;
int b = 0;
int c = 0;
int d = 0;
int e = 0;

}  // namespace

UNIT_TEST(ext_outline_with_panel_new_outline_waits_for_its_panel) {
    Set s;
    s.BeginFrame(false);
    CHECK(!s.Gathered(&a));          // an outline already on screen
    // The click frame: the tool added the panel (not laid out yet) and the new outline b.
    s.BeginFrame(true);
    CHECK(s.Holding());
    CHECK(!s.Gathered(&a));          // drawn last frame: untouched
    CHECK(s.Gathered(&b));           // new: held back
    CHECK(s.Gathered(&b));           // a second scene pass in the same frame: the same answer
    // The next frame: the panel is laid out, b is drawn with it.
    s.BeginFrame(false);
    CHECK(!s.Gathered(&a));
    CHECK(!s.Gathered(&b));
}

UNIT_TEST(ext_outline_with_panel_nothing_held_without_a_pending_dialog) {
    Set s;
    s.BeginFrame(false);
    CHECK(!s.Gathered(&a));
    s.BeginFrame(false);
    CHECK(!s.Gathered(&b));          // a new outline on a frame that opened no dialog: drawn
}

UNIT_TEST(ext_outline_with_panel_held_outline_not_recorded) {
    // Two holding frames in a row (two dialogs opened back to back): the outline held on the first is
    // still new on the second.
    Set s;
    s.BeginFrame(true);
    CHECK(s.Gathered(&a));
    s.BeginFrame(true);
    CHECK(s.Gathered(&a));
    s.BeginFrame(false);
    CHECK(!s.Gathered(&a));
    s.BeginFrame(true);
    CHECK(!s.Gathered(&a));          // drawn last frame now
}

UNIT_TEST(ext_outline_with_panel_full_record_holds_nothing) {
    Set s;
    s.BeginFrame(false);
    CHECK(!s.Gathered(&a));
    CHECK(!s.Gathered(&b));
    CHECK(!s.Gathered(&c));
    CHECK(!s.Gathered(&d));
    CHECK(!s.Gathered(&e));          // past the record's size: drawn, the record is marked full
    s.BeginFrame(true);
    CHECK(!s.Gathered(&e));          // an untrusted record holds nothing back
    CHECK(!s.Gathered(&a));
}
