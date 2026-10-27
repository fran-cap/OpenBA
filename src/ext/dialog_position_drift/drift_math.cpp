// ============================================================================
// src/ext/dialog_position_drift/drift_math.cpp -- fix.dialog_position_drift's arithmetic
// ============================================================================
// The rules are in drift_math.h. Standard C++ only.
// ============================================================================

#include "drift_math.h"

namespace Ext {
namespace DialogPositionDrift {

void LaidOutUpperLeft(int32_t alignment, int32_t rootLeft, int32_t rootTop, int32_t rootRight,
                      int32_t rootBottom, int32_t leftPad, int32_t topPad, int32_t& upperLeftX,
                      int32_t& upperLeftY) {
    if (rootRight <= rootLeft && rootBottom <= rootTop) return;
    if (alignment < 0 || alignment > 8) return;
    if (alignment % 3 == 0) upperLeftX = rootLeft - leftPad;
    if (alignment / 3 == 0) upperLeftY = rootTop - topPad;
}

}  // namespace DialogPositionDrift
}  // namespace Ext
