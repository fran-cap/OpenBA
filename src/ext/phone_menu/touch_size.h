#pragma once
// ============================================================================
// src/ext/phone_menu/touch_size.h -- ext.phone_menu: every control the size of a finger (P11)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md §2, session log docs/android/P11_TOUCH_SIZE.md).
// The P9 walk (touch_fit.h: the 4x scroll bars, the 2x close button) extended to every control a
// finger presses: with the row on, each is brought to about kTouchSide units (size_math.h), the size
// of the enlarged close button:
//   * buttons in a dialog's title bar (StandardDialog.rsc:titleBar: pin, centre, the event log's
//     notify check), icon buttons in a row (the `>` on each citizen of a house, a work place, the
//     town hall), and check boxes: the icon twice its sprite's size when it then still fits (the
//     close button's rule), then padded on both sides to kTouchSide;
//   * text buttons (Ok, Cancel, Apply, Yes, No, Follow, the Options key bindings), tab buttons and
//     drop-downs (ComboElement): their content padded to kTouchSide in height (width is retail's);
//   * the rows of a drop-down's list: padded to kTouchSide in height;
//   * a spinner's up / down buttons: arrows twice their size, the pair kTouchSide high;
//   * sliders: the thumb and track sprites drawn at kSliderSpriteFactor times their size, through the
//     engine seam UI::SliderElement::_extSpriteScale (the slider has no size field to change).
// Left alone: a button its description already makes kTouchSide or larger (the toolbar's, the menus'
// squares), a button whose minimum is not its description's (sized at run time by its owner), every
// dialog ext.touch_mode places (TouchSeen), and the controls on a tab's page that does not scroll (they
// would make the dialog taller and the fitted UI scale, touch_fit.h (c), smaller; touch_size.cpp Walk).
// Every change is made on the element INSTANCE (pads, an image's _width / _height) and undone from
// the element's own description (Element::_description: retail's values) when the row is switched
// off; an element built without a description is left alone. The close button's icon stays the P9
// rule's (touch_fit.cpp FitClose). Main thread only (the slider factor is read by the UI's draw).
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// touch_fit.cpp TouchFrame, once a frame per UserInterface, for each open dialog (row on, or off while
// retail's values are being put back); only dialogs TouchSeen has seen, never one ext.touch_mode placed.
// Returns the number of elements changed.
int32_t TouchSizeWalk(const UI::DialogInstance* dialog, bool on);

// phone_menu.cpp Place, every call: the walk sizes a dialog once it has been seen here, and never one
// ext.touch_mode placed (`touchOwned`: its toolbar, sub-toolbars, event notices and camera cluster, which
// that row sizes for a finger itself).
void TouchSeen(const UI::DialogInstance* dialog, bool touchOwned);

}  // namespace PhoneMenu
}  // namespace Ext
