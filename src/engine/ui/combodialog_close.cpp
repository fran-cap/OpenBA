// ============================================================================
// src/engine/ui/combodialog_close.cpp — NOT RETAIL (review S-035)
// ============================================================================
// Retail can read a freed drop-down. ComboElement::_openDialog (+0x120)
// names the open drop-down (a ComboDialog). Retail clears it only in the
// drop-down's own OnUpdate (0x180111c84) / OnElementEvent (0x180111ba2), in
// ComboElement::SetCurrentItem (0x18011256e) and in ComboDescription
// (0x180112c3d), each next to its CloseDialog of the drop-down. A drop-down
// closed by any other CloseDialog keeps the combo naming it, and the next
// CleanRemovedDialogs frees it; the combo's next ~ComboElement (0x180112490)
// or SetCurrentItem then reads `_openDialog->_ui` (+0x10) from freed memory
// and calls CloseDialog through it.
//
// No caller in the tree closes a drop-down that way today: the drop-down is
// NOT a child of the dialog that hosts the combo (its ctor zeroes the base's
// parent at 0x180111d31 and keeps it in `_parentDialog`), so closing the host
// leaves the drop-down live, and the host's ~ComboElement then closes it with
// both objects alive. The guard keeps a future caller (a close-all walk, a
// close of the topmost dialog) from reaching the freed read.
//
// The fix clears the back-pointer when the drop-down is first closed, from
// UserInterface::CloseDialog, after its "already queued for removal" test
// (Block C). Both destructors stay retail's: neither reads the other object.
//
// The combo is alive at that moment on every route that closes a drop-down:
//   * the drop-down's OnUpdate / OnElementEvent read `_combo` just before;
//   * SetCurrentItem, ComboDescription and ~ComboElement are the combo
//     itself (~ComboElement before any of its fields is torn down);
//   * any other caller closes a drop-down that is live or waiting in
//     `_addDialogs`, whose combo is alive (next point).
// A combo is freed only through ~ComboElement, which closes its open
// drop-down first; a drop-down whose combo is gone is therefore already in
// `_removedDialogs` and never reaches this call. The drop-down is modal
// (`_flags` bit 1), so the frame pump stops at it and the combo cannot open a
// second one over it.
//
// On the retail sites that close the drop-down this changes nothing: each of
// them clears `_openDialog` anyway, before (OnUpdate, OnElementEvent) or
// right after (SetCurrentItem, ComboDescription) its CloseDialog call.
// ============================================================================
#include "ui/ComboDialog.h"
#include "ui/ComboElement.h"
#include "ui/DialogInstance.h"

namespace UI {

void ForgetClosingComboDialog(DialogInstance* dialog) {
    if (dialog->Type() != ComboDialog::TypeId())
        return;
    ComboDialog* const dropDown = static_cast<ComboDialog*>(dialog);
    ComboElement* const combo = dropDown->_combo;
    if (combo != nullptr && combo->_openDialog == dropDown)
        combo->_openDialog = nullptr;
}

}  // namespace UI
