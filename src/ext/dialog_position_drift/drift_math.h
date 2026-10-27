// ============================================================================
// src/ext/dialog_position_drift/drift_math.h -- fix.dialog_position_drift's arithmetic
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §13, docs/extensions/handins/DIALOGPOSITIONDRIFT.md). Standard C++ only
// (tests/unit/ext_dialog_position_drift_test.cpp links drift_math.cpp on its own).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace DialogPositionDrift {

// The upper-left a laid-out dialog's root is at (`rootLeft` / `rootTop` / `rootRight` / `rootBottom`: its
// laid-out rectangle, `leftPad` / `topPad` its pads), on the axes its `alignment` (ElementDescription::
// Alignment: 0..8, x = alignment % 3, y = alignment / 3) anchors at the upper-left; `upperLeftX` /
// `upperLeftY` are left as they are on a centred or right / bottom anchored axis and for a root not laid
// out yet (empty). DialogInstance::SetUpperLeft (0x180114a60) moves the laid-out tree by (new -
// _upperLeft), so when the layout put the root elsewhere (DialogInstance::ComputeLayout 0x180113d90 pulls
// a dialog that crosses the far edge back to it; ext.phone_menu's safe area raises one inside the margin)
// the move lands it off by that difference for one frame, until the next layout (an entity's panel placed
// at its unbuilt size, 88 units high, then laid out at 220 against the bottom edge, was drawn 3 units too
// high, then snapped down: docs/android/P11B_DIALOG_SNAP.md cause 3).
void LaidOutUpperLeft(int32_t alignment, int32_t rootLeft, int32_t rootTop, int32_t rootRight,
                      int32_t rootBottom, int32_t leftPad, int32_t topPad, int32_t& upperLeftX,
                      int32_t& upperLeftY);

}  // namespace DialogPositionDrift
}  // namespace Ext
