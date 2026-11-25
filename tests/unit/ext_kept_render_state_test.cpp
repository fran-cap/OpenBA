// ============================================================================
// tests/unit/ext_kept_render_state_test.cpp -- what a kept derived render state does at a request
// ============================================================================
// src/ext/kept_render_state_rules.h: the rule behind Ext::KeptRenderState (ext.terrain_late,
// ext.show_grid, ext.map_overlays). The object itself needs a video device.
// ============================================================================
#include "unit.h"
#include "ext/kept_render_state_rules.h"

using Ext::KeptStateAction;
using Ext::KeptStateActionFor;

UNIT_TEST(ext_kept_render_state_first_request_makes_a_new_state) {
    CHECK(KeptStateActionFor(false, false, false) == KeptStateAction::MakeNew);
    CHECK(KeptStateActionFor(false, false, true) == KeptStateAction::MakeNew);
}

UNIT_TEST(ext_kept_render_state_same_source_is_reused) {
    CHECK(KeptStateActionFor(true, false, true) == KeptStateAction::Reuse);
}

UNIT_TEST(ext_kept_render_state_another_source_makes_a_new_state) {
    CHECK(KeptStateActionFor(true, false, false) == KeptStateAction::MakeNew);
}

// The restart rebuilds the source in place: the same address must not mean "reuse".
UNIT_TEST(ext_kept_render_state_lost_state_is_made_again_in_place) {
    CHECK(KeptStateActionFor(true, true, true) == KeptStateAction::MakeInPlace);
    CHECK(KeptStateActionFor(true, true, false) == KeptStateAction::MakeInPlace);
}
