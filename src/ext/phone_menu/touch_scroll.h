#pragma once
// ============================================================================
// src/ext/phone_menu/touch_scroll.h -- ext.phone_menu: a tall dialog scrolls instead of shrinking (P11b)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md §2, session log docs/android/P11B_SCROLL_SIZE.md).
// The owner's ruling (P11, docs/runtime_port/NEXT_SESSION.md §2.10): the Enhanced tab's fit, a scrolling
// list inside the page, is the model for every Options page, and the Professions list must scroll (mods
// add professions). With the row on:
//   (a) a page retail lays out without a scroll bar is moved, at run time, into a scrolling frame of ours
//       (Dialog/PhoneScroll.rsc:phoneScroll, the Game page's own container with its size left to its
//       content): the Options dialog's Video page (Options.rsc:graphicsOptions) and Audio page
//       (soundOptions), and the Professions dialog's table (Profession.rsc:professionTable; retail's own
//       comment there: "could put parent object(content) in a Container with scroll bar"). The page's
//       element is MOVED, not rebuilt: the dialog's controller keeps every pointer it found at open.
//       Row off: the page is put back where it was and the frame deleted;
//   (b) a window taller than the safe area (touch_math.h FitsSafeArea, at the player's UI scale) gives
//       the height back from its scrolling lists: every vertical list of the window is capped at one
//       height (size_math.h ShrinkCap / GrowCap), so the window fits and keeps the player's UI scale;
//       only when the lists are down to kListFloor does touch_fit.cpp's scale-down (DecideScale) take
//       the rest. A control in the window is then drawn at its own touch size (85 device pixels at
//       UI 42), which the scale-down shrank (measured at UI 42: the Fixes and Extensions dialog was
//       drawn at 0.78 of its size, Options at 0.93). A cap that takes nothing off (the height is held
//       by something that does not scroll) is dropped. Row off: each list's description height again.
// Every change is on the element INSTANCE (Element::_description holds retail's values); main thread,
// from touch_size.cpp's walk (touch_fit.cpp TouchFrame, once a frame per UserInterface).
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// (a), before the size walk: wraps (row on) or unwraps (off) the pages of `dialog`. Returns the number of
// elements changed.
int32_t TouchScrollWrap(const UI::DialogInstance* dialog, bool on);

// (b), after the size walk (the controls' touch size is part of the height that must fit). Returns the
// number of lists changed.
int32_t TouchScrollCap(const UI::DialogInstance* dialog, bool on);

}  // namespace PhoneMenu
}  // namespace Ext
