#pragma once
// The display test EditTool::Update's citizen fallback (edittool.cpp) applies to its
// projected anchor point. No retail address: the fallback is a standalone addition.

namespace EditToolAnchor {

// NOT RETAIL (review F-166): undefined behaviour. PlayerController::WorldToScreen
// (0x140098210) accepts any clip w > 0, so a point just in front of the camera plane
// projects to 1e9 or more (or to inf / NaN), and Update casts the rect midpoint to int,
// out of range. Retail never feeds an unclamped point there: its only rect producer,
// 0x1400fe960, rejects a box that lies entirely off the display and clamps the rest to
// [0, W] x [0, H]. For the fallback's single point that is "on the display or nothing".
// Written as a negated range test so a NaN coordinate is rejected too.
inline bool OnDisplay(float x, float y, int width, int height) {
    if (!(x >= 0.0f && x <= static_cast<float>(width)))
        return false;
    if (!(y >= 0.0f && y <= static_cast<float>(height)))
        return false;
    return true;
}

}  // namespace EditToolAnchor
