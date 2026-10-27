#pragma once
// ============================================================================
// src/ext/phone_menu/touch_hold.h -- ext.phone_menu: the press-and-hold tooltip (P11b)
// ============================================================================
// NOT RETAIL (docs/android/P11B_POINTER.md §2, the owner's ruling of 2026-10-26). Retail's
// DialogInstance::Update counts hover time (the 0.25 s tooltip delay) only with no mouse button
// down, so a finger, which is always down while it rests on a control, never saw a tooltip (and
// touch_fit.cpp FingerUpHidesToolTip hides one when the finger lifts). With the row on and the
// pointer last moved by a touch, a dialog under a held LEFT button (no right / middle) counts
// hover time as if the button were up: a press held 0.25 s on a control opens its tooltip (the
// building costs of the sub-toolbar), and the lift hides it (the existing rule). A real mouse,
// and the desktop, keep retail's rule. Seam: UI::UserInterface::_extHoverWhileDown, installed by
// phone_menu.cpp Sync (null under the faithful profile). Main thread.
// ============================================================================

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// UI::UserInterface::_extHoverWhileDown.
bool HoverWhileFingerDown(const UI::DialogInstance* dialog);

}  // namespace PhoneMenu
}  // namespace Ext
