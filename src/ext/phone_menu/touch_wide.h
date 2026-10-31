#pragma once
// ============================================================================
// src/ext/phone_menu/touch_wide.h -- ext.phone_menu: a widened window's rows fill it (issue #73)
// ============================================================================
// NOT RETAIL. With the row on, a dialog's title bar is made at least as wide as its name and its
// finger-sized buttons side by side (touch_size.h), which widens windows such as a work place's. Retail's
// body was laid out for the narrower window: its bars and button row stop short of the new right edge.
// In a window whose title bar the row widened:
//   (a) a progress bar (a crop field's yield) or a slider (a pasture's herd size) is as wide as the room
//       its row has: its minimum width (UI::ProgressElement and UI::SliderElement always draw over the
//       whole width their cell gives them, ProgressElement::ComputeLayout 0x180119610's "the width always
//       stretches");
//   (b) the dialog's button bar (StandardDialog.rsc:buttonBar) spreads its buttons over the window's
//       width: the ribbon's gap (size_math.h SpreadSpacing).
// The room is measured from the title bar's minimum width, not from the window's laid-out width, so a
// filled row never feeds back into a wider window. Only rows reached through ribbons and groups are
// filled (a tab's page or a scrolling list is left alone), and a bar that shares its row with another bar
// is left alone. Row off, or a window the row did not widen: the description's minimum width and gap.
// Every change is on the element INSTANCE; main thread, from touch_size.cpp's walk.
// ============================================================================

#include <cstdint>

namespace UI {
class Element;
}

namespace Ext {
namespace PhoneMenu {

// After the size walk of one dialog: `titleBar` is its StandardDialog.rsc:titleBar (null: none, nothing
// done), `root` the dialog's root element. Returns the number of elements changed.
int32_t TouchWideFill(UI::Element* root, UI::Element* titleBar, bool on);

}  // namespace PhoneMenu
}  // namespace Ext
