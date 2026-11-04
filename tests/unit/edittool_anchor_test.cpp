// ============================================================================
// tests/unit/edittool_anchor_test.cpp -- review F-166: the citizen info-panel anchor
// ============================================================================
// EditTool::Update's citizen fallback (src/game/Player/edittool.cpp) projects the
// citizen's model origin through PlayerController::WorldToScreen and writes the point
// into the panel rect, whose midpoint is then cast to int. WorldToScreen accepts any
// clip w > 0, so a citizen just in front of the camera plane projects to 1e9 and more,
// and that cast is undefined behaviour. Player/edittool_anchor.h holds the test the
// fallback now applies: the point is used only when it lies on the display, the
// single-corner case of 0x1400fe960's off-display rejection. The rest of Update needs a
// live world and is not reachable from here.
// ============================================================================
#include "unit.h"
#include "Player/edittool_anchor.h"

#include <climits>
#include <limits>

UNIT_TEST(edittool_anchor_on_display) {
    // Inside and on the edges of a 1920 x 1080 back buffer: used as before.
    CHECK(EditToolAnchor::OnDisplay(960.0f, 540.0f, 1920, 1080));
    CHECK(EditToolAnchor::OnDisplay(0.0f, 0.0f, 1920, 1080));
    CHECK(EditToolAnchor::OnDisplay(1920.0f, 1080.0f, 1920, 1080));
}

UNIT_TEST(edittool_anchor_rejects_off_display) {
    // The ledger repro: clip.x = 5, w = 1e-6 on 1920 wide -> 4.8e9, past INT_MAX.
    const float hugeX = 5.0f / 1e-6f * 960.0f + 960.0f;
    CHECK(hugeX > static_cast<float>(INT_MAX));
    CHECK(!EditToolAnchor::OnDisplay(hugeX, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(-hugeX, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, 1e30f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, -1e30f, 1920, 1080));
    // Just past each edge.
    CHECK(!EditToolAnchor::OnDisplay(-0.5f, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(1920.5f, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, -0.5f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, 1080.5f, 1920, 1080));
    // NaN / infinity (clip / w with w denormal): never reaches the int cast.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(!EditToolAnchor::OnDisplay(nan, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, nan, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(inf, 540.0f, 1920, 1080));
    CHECK(!EditToolAnchor::OnDisplay(960.0f, -inf, 1920, 1080));
}
