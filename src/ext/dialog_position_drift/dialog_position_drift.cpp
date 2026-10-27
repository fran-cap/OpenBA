// ============================================================================
// src/ext/dialog_position_drift/dialog_position_drift.cpp -- fix.dialog_position_drift
// ============================================================================
// The rules are in dialog_position_drift.h; the arithmetic is drift_math.cpp.
// ============================================================================

#include "ext/dialog_position_drift/dialog_position_drift.h"
#include "ext/dialog_position_drift/drift_math.h"
#include "ext/registry.h"

#include "ui/DialogInstance.h"
#include "ui/Element.h"

namespace Ext {
namespace DialogPositionDrift {

void SyncUpperLeft(UI::DialogInstance* dialog) {
    if (dialog == nullptr) return;
    const UI::Element* root = dialog->_root.Get();
    if (root == nullptr) return;
    LaidOutUpperLeft(root->_alignment, root->_layoutLeft, root->_layoutTop, root->_layoutRight,
                     root->_layoutBottom, root->_leftPad, root->_topPad, dialog->_upperLeftX,
                     dialog->_upperLeftY);
}

void OnSetUpperLeft(UI::DialogInstance* dialog) {
    if (!Fix().dialog_position_drift) return;
    SyncUpperLeft(dialog);
}

}  // namespace DialogPositionDrift
}  // namespace Ext
