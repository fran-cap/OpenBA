// ============================================================================
// src/ext/phone_menu/touch_size.cpp -- ext.phone_menu: every control the size of a finger (P11)
// ============================================================================
// The rules are in touch_size.h; the arithmetic is size_math.cpp.
//
// HOW RETAIL SIZES WHAT IS CHANGED HERE (read off the engine's bodies):
//   * A button (UI::ButtonElement, ComputeSize 0x180110d10; CheckElement and ComboElement derive from
//     it) is the union of its content's padded size (`_children[4]`, the label or icon) and its state
//     background's, floored by its own _minWidth / _minHeight. Its content and backgrounds are laid out
//     over the button's rectangle (ComputeLayout 0x180110e70), each by its own alignment, which is
//     TopLeft for most of the game's labels and icons. So the content's PADDING is what grows here: a
//     larger padded content makes a larger button with the content still centred in it (a larger
//     _minHeight would leave the label in the top-left corner).
//   * A check box (CheckElement 0x180111740) adds its tick (`_children[5]`), laid out over the same
//     rectangle: the tick gets the same icon size and padding as the content, so it lands where the
//     unticked icon is.
//   * An icon (UI::ImageElement, ComputeSize 0x180118650) is drawn over its own `_width` x `_height`
//     when they are set (the P9 close button's rule, touch_fit.cpp).
//   * A tab's buttons are the UI::TabElement's own children (GetButtonIndex 0x1800da120: 3 + 2 t),
//     a spinner's up / down buttons its `_children[1]` / `[2]`, stacked beside the edit field
//     (SpinnerElement::ComputeSize 0x1800d83b0: height = up + down).
//   * A drop-down's rows are the children of the UI::ListElement's ribbon (`_ribbon`, found by
//     ListElement's CreateInstance); its hit test and highlight read the ribbon's laid-out extents, so
//     padded rows are picked and highlighted whole.
//   * A slider (UI::SliderElement) draws its sprites at their own size and hit-tests its thumb sprite's
//     rectangle (Update 0x1800d67e0); there is no size field to change, so the engine reads a factor
//     through UI::SliderElement::_extSpriteScale (null = retail, 1 = retail).
// The padding is the description's (retail's) plus what brings the content to kTouchSide; with the row
// off, the description's again.
// ============================================================================

#include "ext/phone_menu/touch_size.h"
#include "ext/phone_menu/size_math.h"
#include "ext/phone_menu/touch_scroll.h"

#include <cstring>

#include "object/TypeInfo.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ComboElement.h"
#include "ui/ContainerElement.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/ImageDescription.h"
#include "ui/ImageElement.h"
#include "ui/ListElement.h"
#include "ui/ScrollElement.h"
#include "ui/SliderElement.h"
#include "ui/SpinnerElement.h"
#include "ui/TabElement.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// ButtonElement's child indices (buttonelement.cpp kChildLabel, checkelement.cpp kChildTick).
constexpr int32_t kChildContent = 4;
constexpr int32_t kChildTick = 5;

// The slider factor UI::SliderElement::_extSpriteScale points at: kSliderSpriteFactor with the row on,
// 1 (retail's sizes) off. When it changes, every slider walked over the next frames is sized again.
int32_t g_sliderScale = 1;
int32_t g_sliderResize = 0;

// What kind of control a button-derived element is, which decides the axes it grows on.
enum class Kind {
    None,
    TitleBar,   // a button or check in a dialog's title bar (StandardDialog.rsc:titleBar)
    Icon,       // a plain button with an icon (the `>` of a citizen's row)
    Text,       // a plain button with a label (Ok, Cancel, Apply, Follow, ...)
    Tab,        // a tab's button
    Check,      // a check box
    Combo,      // a drop-down
    Spinner,    // a spinner's up / down button
};

// The description's padding (ElementDescription's +0x20..+0x2c land on the element's four pads:
// dialog.cpp's CreateInstance applier; the description's field names are the tree's guesses).
struct Pads {
    int32_t left = 0;
    int32_t right = 0;
    int32_t top = 0;
    int32_t bottom = 0;
};

Pads RetailPads(const UI::ElementDescription* desc) {
    Pads p;
    p.left = desc->_x;
    p.right = desc->_y;
    p.top = desc->_width;
    p.bottom = desc->_height;
    return p;
}

// An icon at its touch size (row on) or retail's (off). Returns 1 when it changed. An icon whose size is
// neither retail's nor the touch size was sized by someone else (ext.touch_mode's camera cluster scales
// its glyphs to its buttons) and is left alone.
int32_t FitIcon(UI::ImageElement* icon, bool on) {
    const UI::ElementDescription* base = icon->_description;
    if (!::Object::IsDerivedFromNullSafe(base, UI::ImageDescription::TypeId())) return 0;
    const UI::ImageDescription* desc = static_cast<const UI::ImageDescription*>(base);
    const int32_t sw = icon->_sprite.Width();
    const int32_t sh = icon->_sprite.Height();
    const int32_t tw = TouchIconSide(sw);
    const int32_t th = TouchIconSide(sh);
    if (tw <= 0 || th <= 0) return 0;
    // Only an icon drawn at its sprite's size is doubled (an explicit size in the layout that is not the
    // sprite's is a picture meant at that size).
    const bool natural = (desc->_width < 0 || desc->_width == sw) && (desc->_height < 0 || desc->_height == sh);
    if (!natural || (tw == sw && th == sh)) return 0;
    const bool retail = icon->_width == desc->_width && icon->_height == desc->_height;
    const bool touch = icon->_width == tw && icon->_height == th;
    if (!retail && !touch) return 0;
    if (on == touch) return 0;
    icon->_width = on ? tw : desc->_width;
    icon->_height = on ? th : desc->_height;
    icon->SetNeedsSizeCalc();
    return 1;
}

// `e`'s padding: retail's plus what brings its padded size to `targetW` x `targetH` (0: that axis is
// retail's). Returns 1 when it changed.
int32_t FitPads(UI::Element* e, bool on, int32_t targetW, int32_t targetH) {
    const UI::ElementDescription* desc = e->_description;
    if (desc == nullptr) return 0;
    const Pads retail = RetailPads(desc);
    Pads want = retail;
    if (on) {
        e->ComputeSize();
        int32_t w = e->_layoutWidth + retail.left + retail.right;
        int32_t h = e->_layoutHeight + retail.top + retail.bottom;
        if (w < e->_minWidth) w = e->_minWidth;
        if (h < e->_minHeight) h = e->_minHeight;
        if (targetW > 0) {
            const PadSplit x = PadToTarget(w, targetW);
            want.left += x.before;
            want.right += x.after;
        }
        if (targetH > 0) {
            const PadSplit y = PadToTarget(h, targetH);
            want.top += y.before;
            want.bottom += y.after;
        }
    }
    if (e->_leftPad == want.left && e->_rightPad == want.right && e->_topPad == want.top &&
        e->_bottomPad == want.bottom) {
        return 0;
    }
    e->_leftPad = want.left;
    e->_rightPad = want.right;
    e->_topPad = want.top;
    e->_bottomPad = want.bottom;
    e->SetNeedsSizeCalc();
    return 1;
}

UI::Element* Child(const UI::Element* e, int32_t index) {
    if (index >= e->_children._size) return nullptr;
    return e->_children._data[index];
}

bool IsIcon(const UI::Element* e) {
    return ::Object::IsDerivedFromNullSafe(e, UI::ImageElement::TypeId());
}

Kind Classify(const UI::ButtonElement* b, bool inTitle) {
    const UI::Element* parent = b->_parent;
    if (parent != nullptr && parent->IsA(UI::TabElement::TypeId())) return Kind::Tab;
    if (parent != nullptr && parent->IsA(UI::SpinnerElement::TypeId())) return Kind::Spinner;
    if (b->IsA(UI::ComboElement::TypeId())) return Kind::Combo;
    if (inTitle) return Kind::TitleBar;
    if (b->IsA(UI::CheckElement::TypeId())) return Kind::Check;
    return IsIcon(Child(b, kChildContent)) ? Kind::Icon : Kind::Text;
}

// One content element of a button (its label or icon, a check's tick) at the touch size.
int32_t FitContent(UI::Element* content, bool on, int32_t targetW, int32_t targetH) {
    if (content == nullptr) return 0;
    int32_t changes = 0;
    if (IsIcon(content)) changes += FitIcon(static_cast<UI::ImageElement*>(content), on);
    changes += FitPads(content, on, targetW, targetH);
    return changes;
}

int32_t FitButton(UI::ButtonElement* b, bool on, bool inTitle) {
    if (b->_description == nullptr) return 0;
    // A button whose minimum is not its description's was sized at run time by its owner
    // (ext.touch_mode's camera cluster sizes its buttons to the cluster's cells): left alone.
    if (b->_minWidth != b->_description->_minWidth || b->_minHeight != b->_description->_minHeight) return 0;
    UI::Element* content = Child(b, kChildContent);
    if (content == nullptr) return 0;
    // The close button's cross is the P9 rule's (touch_fit.cpp FitClose); its padded icon is already
    // kTouchSide.
    const char* name = b->_name;
    if (name != nullptr && name[0] == 'b' && std::strcmp(name, "buttonClose") == 0 && IsIcon(content)) return 0;

    // A button already finger-sized in retail's layout (its own minimum) keeps it.
    const UI::ElementDescription* desc = b->_description;
    const int32_t square = desc->_minWidth >= kTouchSide ? 0 : kTouchSide;
    const int32_t tall = desc->_minHeight >= kTouchSide ? 0 : kTouchSide;

    int32_t changes = 0;
    switch (Classify(b, inTitle)) {
    case Kind::TitleBar:
    case Kind::Icon:
        changes += FitContent(content, on, square, tall);
        // The title bar's pin is a check: its tick over the same rectangle as its icon.
        if (b->IsA(UI::CheckElement::TypeId())) changes += FitContent(Child(b, kChildTick), on, square, tall);
        break;
    case Kind::Text:
        changes += FitContent(content, on, 0, tall);
        break;
    case Kind::Check:
        changes += FitContent(content, on, square, tall);
        changes += FitContent(Child(b, kChildTick), on, square, tall);
        break;
    case Kind::Combo:
        changes += FitContent(content, on, 0, tall);
        break;
    case Kind::Tab:
        changes += FitContent(content, on, 0, tall);
        break;
    case Kind::Spinner:
        // Up and down stacked: each half the touch side high, the pair kTouchSide.
        changes += FitContent(content, on, square, tall > 0 ? kTouchSide / 2 : 0);
        break;
    case Kind::None:
        break;
    }
    return changes;
}

// A drop-down's rows (ListElement::_ribbon's children), each kTouchSide high.
int32_t FitListRows(UI::ListElement* list, bool on) {
    UI::Element* ribbon = list->_ribbon;
    if (ribbon == nullptr) return 0;
    int32_t changes = 0;
    for (int32_t i = 0; i < ribbon->_children._size; ++i) {
        UI::Element* row = ribbon->_children._data[i];
        if (row != nullptr) changes += FitPads(row, on, 0, kTouchSide);
    }
    return changes;
}

// Where an element sits, for the rules that depend on it.
struct Place {
    bool title = false;       // inside a dialog's title bar
    bool fixedPage = false;   // inside a tab's page that does not scroll (below)
};

// True for a scrolling list: a UI::ContainerElement with its vertical bar (`_children[1]`, the P9
// rule's reading of ContainerElement::ComputeSize 0x1800cc850).
bool IsScrollingList(const UI::Element* e) {
    if (!e->IsA(UI::ContainerElement::TypeId())) return false;
    const UI::Element* bar = Child(e, 1);
    return bar != nullptr && bar->IsA(UI::ScrollElement::TypeId());
}

// A tab element is as tall as its TALLEST page (TabElement::ComputeSize 0x1800d9ea0, shown or not),
// and a page that does not scroll cannot give height back: a padded control there makes the whole
// dialog taller, and a dialog taller than the safe area is shown at a fitted, smaller UI scale
// (touch_fit.h (c)), which makes every control in it smaller again (the Options dialog's Video page:
// measured on the S25 at UI 42, the fit went from 44 % to 57 %, docs/android/P11_TOUCH_SIZE.md). So
// controls on a tab's non-scrolling page keep retail's size; in a scrolling list on any page they grow.
// P11b: every Options page is a scrolling list by then (touch_scroll.h frames Video and Audio), so the
// rule holds back only other dialogs' non-scrolling tab pages.
int32_t Walk(UI::Element* e, bool on, Place place) {
    if (e == nullptr) return 0;
    if (::Object::IsDerivedFromNullSafe(e, UI::ButtonElement::TypeId())) {
        // A button's children are its content and backgrounds: sized here, not walked.
        return FitButton(static_cast<UI::ButtonElement*>(e), on && !place.fixedPage, place.title);
    }
    int32_t changes = 0;
    if (e->IsA(UI::SliderElement::TypeId())) {
        if (g_sliderResize > 0) {
            e->SetNeedsSizeCalc();
            ++changes;
        }
    } else if (e->IsA(UI::ListElement::TypeId())) {
        changes += FitListRows(static_cast<UI::ListElement*>(e), on && !place.fixedPage);
    } else if (IsScrollingList(e)) {
        place.fixedPage = false;
    }
    const char* name = e->_name;
    if (!place.title && name != nullptr && name[0] == 't' && std::strcmp(name, "titleBar") == 0) place.title = true;
    const bool tab = e->IsA(UI::TabElement::TypeId());
    for (int32_t i = 0; i < e->_children._size; ++i) {
        Place child = place;
        // A tab's pages are its children 4 + 2 t (TabElement::GetContainerIndex 0x1800da110).
        if (tab && i >= 4 && (i % 2) == 0) child.fixedPage = true;
        changes += Walk(e->_children._data[i], on, child);
    }
    return changes;
}

// The dialogs phone_menu's place hook has seen (TouchSeen), with the description each had then (a new
// dialog at a freed one's address is not mistaken for it) and whether ext.touch_mode placed it.
struct Seen {
    const UI::DialogInstance* dialog = nullptr;
    const UI::Dialog* description = nullptr;
    bool touchOwned = false;
};
constexpr int32_t kMaxSeen = 64;
Seen g_seen[kMaxSeen];
int32_t g_nextSeen = 0;

const Seen* FindSeen(const UI::DialogInstance* dialog) {
    for (const Seen& s : g_seen) {
        if (s.dialog == dialog && s.description == dialog->_dialog) return &s;
    }
    return nullptr;
}

}  // namespace

void TouchSeen(const UI::DialogInstance* dialog, bool touchOwned) {
    if (dialog == nullptr) return;
    for (Seen& s : g_seen) {
        if (s.dialog == dialog) {
            s.description = dialog->_dialog;
            s.touchOwned = touchOwned;
            return;
        }
    }
    Seen& s = g_seen[g_nextSeen];
    s.dialog = dialog;
    s.description = dialog->_dialog;
    s.touchOwned = touchOwned;
    g_nextSeen = (g_nextSeen + 1) % kMaxSeen;
}

int32_t TouchSizeWalk(const UI::DialogInstance* dialog, bool on) {
    UI::Element* root = dialog->_root.Get();
    // The slider factor follows the row (one value for every slider).
    const int32_t scale = on ? kSliderSpriteFactor : 1;
    if (scale != g_sliderScale) {
        g_sliderScale = scale;
        g_sliderResize = 32;   // the next few frames' walks (a few dialogs a frame)
    }
    // A dialog is walked once the place hook has seen it (from its second frame: the walk runs at the
    // first dialog's layout, before a newly opened one is placed), so a dialog ext.touch_mode places (its
    // toolbar, sub-toolbars, event notices and camera cluster, sized for a finger by that row) is never
    // touched.
    const Seen* seen = FindSeen(dialog);
    int32_t changes = 0;
    if (seen != nullptr && !seen->touchOwned) {
        // P11b (touch_scroll.h): a page retail does not scroll is framed first, so its controls are in a
        // scrolling list when they are sized; the lists are capped last, at the sized height.
        changes += TouchScrollWrap(dialog, on);
        changes += Walk(root, on, Place());
        changes += TouchScrollCap(dialog, on);
    }
    if (g_sliderResize > 0) --g_sliderResize;
    return changes;
}

void SyncTouchSize(bool faithful) {
    ::UI::SliderElement::_extSpriteScale = faithful ? nullptr : &g_sliderScale;
}

}  // namespace PhoneMenu
}  // namespace Ext
