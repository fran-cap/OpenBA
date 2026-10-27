#pragma once
// ============================================================================
// src/ext/dialog_position_drift/dialog_position_drift.h -- fix.dialog_position_drift (fix tier, default ON)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §13, docs/extensions/handins/DIALOGPOSITIONDRIFT.md). Retail's
// DialogInstance::SetUpperLeft (0x180114a60) moves a laid-out dialog by the difference between the new
// upper-left and the stored `_upperLeft`, but the layout (DialogInstance::ComputeLayout 0x180113d90) does
// not write back where it actually put the root: a dialog whose layout crossed the screen's far edge was
// pulled back to it with `_upperLeft` unchanged, so the next SetUpperLeft (an entity's panel, placed above
// its building every frame by EditTool::Update) lands it off by the pull-back for one frame (3 to 11
// layout units, measured on the S25), then the next layout puts it back: a one-frame jump on every
// platform. With the row on, `_upperLeft` is first set to where the root is laid out, so the move lands
// the dialog where the next layout keeps it.
//
// The call point is the engine seam UI::UserInterface::_extDialogPlace at kExtPlaceUpperLeft (no seam of
// its own): ext.phone_menu's Place (phone_menu.cpp) owns that pointer and is installed under every
// profile but faithful, the row on or off, and hands the call here before its own row's test, so the fix
// is independent of ext.phone_menu. ext.phone_menu (P11b) needs the same sync for its safe area and
// calls SyncUpperLeft itself when this row is off; with both on it runs once.
// ============================================================================

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace DialogPositionDrift {

// `dialog`'s stored upper-left set to where its root is laid out (drift_math.h LaidOutUpperLeft), on the
// axes its root's alignment anchors top / left. Nothing for a dialog without a root or not laid out yet.
// Main thread (SetUpperLeft's caller). Does not test the row.
void SyncUpperLeft(UI::DialogInstance* dialog);

// The kExtPlaceUpperLeft call point: SyncUpperLeft when the row is on.
void OnSetUpperLeft(UI::DialogInstance* dialog);

}  // namespace DialogPositionDrift
}  // namespace Ext
