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
//   * a spinner's up / down buttons: arrows twice their size, each kTouchSide square, side by side
//     (issue #73, through the engine seam UI::SpinnerElement::_extSideBySide; stacked, the pair would be
//     twice kTouchSide high);
//   * sliders: the thumb and track sprites drawn at kSliderSpriteFactor times their size, through the
//     engine seam UI::SliderElement::_extSpriteScale (the slider has no size field to change);
//   * a dialog's title bar (issue #73): a ribbon of stacked buttons in it (a work place's worker and job
//     count arrows, Work.rsc's workersUpDown / jobsUpDown) is laid out side by side, and the bar (a
//     GroupElement, its name and buttons drawn over each other) is at least as wide as the name and the
//     buttons side by side, so the window widens and the finger-sized buttons no longer cover the name;
//     the window's bars and button row then fill the new width (touch_wide.h).
// Left alone: a button its description already makes kTouchSide or larger (the toolbar's, the menus'
// squares), a button whose minimum is not its description's (sized at run time by its owner), every
// dialog ext.touch_mode places (TouchSeen), and the controls on a tab's page that does not scroll (they
// would make the dialog taller and the fitted UI scale, touch_fit.h (c), smaller; touch_size.cpp Walk).
// P11b: the Options dialog's Video and Audio pages and the Professions table are first moved into a
// scrolling frame (touch_scroll.h), so their controls are in a scrolling list and take the touch size.
// Every change is made on the element INSTANCE (pads, an image's _width / _height) and undone from
// the element's own description (Element::_description: retail's values) when the row is switched
// off; an element built without a description is left alone. The close button's icon stays the P9
// rule's (touch_fit.cpp FitClose). Main thread only (the slider factor is read by the UI's draw).
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
class UserInterface;
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

// P11b: true once TouchSeen has seen `dialog` (with its current description). phone_menu.cpp Place asks
// it before TouchSeen to find a dialog's first placement or layout (touch_fit.h TouchFirstPlace).
bool TouchKnown(const UI::DialogInstance* dialog);

// P11c: touch_fit.cpp TouchFrame, once a frame per UserInterface (row on or off): forgets every dialog of
// `ui` that is no longer in its open list, here and in touch_scroll.h's per-window caps. What is known of a
// dialog is keyed by its address, and a dialog opened again (the Options dialog, closed and reopened) is
// often allocated at the address the closed one had: without this it was taken for the closed one (no
// first-placement walk; its lists left uncapped under the old one's settled cap, so it was scaled down;
// docs/android/P11C_DIALOGS.md).
void TouchForgetClosed(const UI::UserInterface* ui);

// Boot (phone_menu.cpp Sync): points UI::SliderElement::_extSpriteScale at the slider factor
// (null under the faithful profile).
void SyncTouchSize(bool faithful);

}  // namespace PhoneMenu
}  // namespace Ext
