// ============================================================================
// tests/unit/depth_copy_rule_test.cpp -- THE SHARED DEPTH COPY's reuse rule
// ============================================================================
// src/render/common/depth_copy/depth_copy_backend.h: a copy is reused only within its frame, for the same
// main depth (image, size, sample count), and only while no main-depth write (an engine draw that writes
// depth, a depth clear) came after it. Every platform.
// ============================================================================
#include "unit.h"

#include "depth_copy/depth_copy_backend.h"

using DepthCopyBackend::Fresh;
using DepthCopyBackend::Stamp;

namespace {

Stamp Made() {
    static int depth = 0;
    Stamp s;
    s.frame = 41;
    s.depthWrites = 7;
    s.depth = &depth;
    s.width = 2560;
    s.height = 1440;
    s.samples = 4;
    return s;
}

}  // namespace

UNIT_TEST(depth_copy_reused_while_the_depth_is_unchanged) {
    CHECK(Fresh(Made(), Made()));
}

UNIT_TEST(depth_copy_not_reused_after_a_depth_write) {
    Stamp now = Made();
    now.depthWrites = 8;
    CHECK(!Fresh(Made(), now));
}

UNIT_TEST(depth_copy_not_reused_in_another_frame) {
    Stamp now = Made();
    now.frame = 42;
    CHECK(!Fresh(Made(), now));
}

UNIT_TEST(depth_copy_not_reused_for_another_depth_or_shape) {
    static int other = 0;
    Stamp now = Made();
    now.depth = &other;
    CHECK(!Fresh(Made(), now));
    now = Made();
    now.width = 1280;
    CHECK(!Fresh(Made(), now));
    now = Made();
    now.height = 720;
    CHECK(!Fresh(Made(), now));
    now = Made();
    now.samples = 1;
    CHECK(!Fresh(Made(), now));
}

UNIT_TEST(depth_copy_never_made_is_never_fresh) {
    const Stamp none;
    CHECK(!Fresh(none, none));
    Stamp outside = Made();
    outside.frame = ~0ull;
    CHECK(!Fresh(outside, outside));
}
