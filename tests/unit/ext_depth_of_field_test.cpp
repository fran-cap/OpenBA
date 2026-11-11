// ============================================================================
// tests/unit/ext_depth_of_field_test.cpp -- ext.depth_of_field's max CoC (src/render/common/dof/dof_math.h)
// ============================================================================
// The max CoC the two backends' passes compute scales with the target's height. The passes themselves need
// the running game.
// ============================================================================
#include "unit.h"
#include "dof/dof_math.h"

#include <cmath>

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

UNIT_TEST(ext_dof_max_coc_scales_with_height) {
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 1080), 8.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 2160), 16.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 540), 4.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 10), 1.0f));   // at least a pixel
}
