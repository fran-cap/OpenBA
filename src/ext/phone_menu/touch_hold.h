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
//
// P11c (docs/android/P11C_TOOLTIP.md, the owner's ruling of 2026-10-26): the tooltip opened that way
// sat where retail puts every tooltip -- its bottom-left on the pointer (BeginToolTip 0x180120a27 and
// the MOVE arm of Element::Update, SetUpperLeft(x, y - h)) -- under the finger, unreadable. While the
// pointer is a touch, PlaceHeldToolTip lays the tooltip out about the lift distance ABOVE the finger
// instead (ext.touch_pointer_lift with that row on, else 160 device pixels), inside the safe area,
// and flips it below the finger only when there is no room above (hold_math.h PlaceHoldTip). It is the
// ext placement point (UserInterface::_extDialogPlace, kExtPlaceLayout), called by phone_menu.cpp Place
// after ext.touch_mode's own arm, so a tooltip of the scaled toolbar is placed by it too. Retail's
// upper-left is left as SetUpperLeft stored it; the layout rectangle is offset from it. A real mouse,
// and the desktop, keep retail's placement.
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// UI::UserInterface::_extHoverWhileDown.
bool HoverWhileFingerDown(const UI::DialogInstance* dialog);

// UI::UserInterface::_extDialogPlace's tooltip arm (P11c): true, with `rect` written, when `dialog` is
// the tooltip up, the point is kExtPlaceLayout, the row is on and the pointer is a touch.
bool PlaceHeldToolTip(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
                      int32_t* rect);

}  // namespace PhoneMenu
}  // namespace Ext
