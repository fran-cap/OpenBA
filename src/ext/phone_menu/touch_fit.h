#pragma once
// ============================================================================
// src/ext/phone_menu/touch_fit.h -- ext.phone_menu: the game's dialogs fitted for a finger (P9)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md §2, session log docs/android/P9_DIALOGS.md).
// Same row, same switch and same seam as the rest of ext.phone_menu (UI::UserInterface::
// _extDialogPlace); phone_menu.cpp's Place hands every call here first. With the row on:
//   (a) every scroll bar is kScrollBarFactor times thicker (touch_math.h), the scrolling list beside
//       it keeps its own width (the container grows by what the bar gained);
//   (b) every dialog's close button (`buttonClose`) shows its cross kCloseButtonFactor times larger;
//   (c) a dialog larger than the safe area (the Options and Extensions dialogs below about UI 45 on
//       a 19.5:9 phone) is shown whole: that dialog alone (with its child dialogs, tooltip and combo
//       drop-downs) is laid out, hit-tested and drawn at the lowest UI scale at which it fits
//       (touch_math.h FitScale), through the per-dialog scale seam UI::UserInterface::
//       _extDialogScale; the rest of its UserInterface keeps the player's scale (P11: before, the
//       whole interface was scaled, which moved and shrank everything behind the dialog);
//   (d) the main and pause menus' backdrop gets its bottom border back (touch_math.h LowerBackdrop) and
//       their version line moves down into it (LowVersionLayout, phone_menu.cpp).
// Every change is made on the element INSTANCE and undone from the element's own description
// (Element::_description: retail's values) when the row is switched off; an element built without
// a description is left alone. Main thread only, except (d), which runs on the UI's draw.
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// UserInterface::_extDialogPlace, every call, before phone_menu.cpp's own handling (row on or off).
// At kExtPlaceLayout on the first dialog of a UserInterface's list (once a frame per UI) it applies
// (a) and (b) to every open dialog of that UI (or undoes them with the row off) and decides (c).
void TouchFrame(int32_t point, const UI::DialogInstance* dialog);

// (c): the own scale k of `dialog` (own units = layout units / k; k < 1 shows it smaller), 1.0 for a
// dialog that is not fitted. A child dialog, a tooltip and a combo drop-down follow the window they
// belong to. Any thread (the seam's Draw call point reads it too).
float FitDialogScale(const UI::DialogInstance* dialog);

// kExtPlaceBackdropLower for the main and pause menus (`rect`: the backdrop, answered with the extra
// draw under it) with the row on.
void BackdropLowerDraw(int32_t width, int32_t height, int32_t* rect);

}  // namespace PhoneMenu
}  // namespace Ext
