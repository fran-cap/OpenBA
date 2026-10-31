// ============================================================================
// src/ext/phone_menu/touch_wide.cpp -- ext.phone_menu: a widened window's rows fill it (issue #73)
// ============================================================================
// The rules are in touch_wide.h; the arithmetic is size_math.cpp (SpreadSpacing).
// ============================================================================

#include "ext/phone_menu/touch_wide.h"
#include "ext/phone_menu/size_math.h"

#include <cstring>

#include "object/TypeInfo.h"
#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/GroupElement.h"
#include "ui/ProgressElement.h"
#include "ui/RibbonDescription.h"
#include "ui/RibbonElement.h"
#include "ui/SliderElement.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// Derived-from tests, not IsA (the exact type): a dialog's pages are a UI::PageElement, a GroupElement.
bool Is(const UI::Element* e, uint32_t typeId) {
    return ::Object::IsDerivedFromNullSafe(e, typeId);
}

bool IsBar(const UI::Element* e) {
    return Is(e, UI::ProgressElement::TypeId()) || Is(e, UI::SliderElement::TypeId());
}

bool IsHorizontalRibbon(const UI::Element* e) {
    return Is(e, UI::RibbonElement::TypeId()) && static_cast<const UI::RibbonElement*>(e)->_vertical == 0;
}

// `e`'s padded size, as its parent measures it (ComputeSize, the pads, the minimum).
void PaddedSize(UI::Element* e, int32_t& w, int32_t& h) {
    e->ComputeSize();
    w = e->_layoutWidth + e->_leftPad + e->_rightPad;
    h = e->_layoutHeight + e->_topPad + e->_bottomPad;
    if (w < e->_minWidth) w = e->_minWidth;
    if (h < e->_minHeight) h = e->_minHeight;
}

// The padded width `e` may take in the dialog's content ribbon (`content`, whose inner width is `width`):
// less every frame's pads on the way, and in a horizontal ribbon its other children and the gaps between
// them. -1: a frame on the way is not a ribbon or a group, or `e` shares a horizontal row with a bar.
int32_t Room(UI::Element* e, const UI::Element* content, int32_t width) {
    int32_t room = width;
    UI::Element* child = e;
    for (UI::Element* a = e->_parent; a != content; child = a, a = a->_parent) {
        if (a == nullptr) return -1;
        if (IsHorizontalRibbon(a)) {
            int32_t others = 0;
            int32_t sized = 0;
            for (int32_t i = 0; i < a->_children._size; ++i) {
                UI::Element* c = a->_children._data[i];
                if (c == nullptr || c == child || !c->IsVisible()) continue;
                if (IsBar(c)) return -1;
                int32_t w = 0;
                int32_t h = 0;
                PaddedSize(c, w, h);
                others += w;
                if (w > 0 && h > 0) ++sized;
            }
            room -= others + static_cast<UI::RibbonElement*>(a)->_spacing * sized;
        } else if (!Is(a, UI::RibbonElement::TypeId()) && !Is(a, UI::GroupElement::TypeId())) {
            return -1;
        }
        room -= a->_leftPad + a->_rightPad;
    }
    return room;
}

// (a): a bar's minimum width, the room its row has (`width` <= 0: the description's).
int32_t FillBar(UI::Element* bar, const UI::Element* content, int32_t width) {
    const UI::ElementDescription* desc = bar->_description;
    if (desc == nullptr) return 0;
    int32_t want = desc->_minWidth;
    if (width > 0) {
        const int32_t room = Room(bar, content, width);
        if (room > want) want = room;
    }
    if (bar->_minWidth == want) return 0;
    bar->_minWidth = want;
    bar->SetNeedsSizeCalc();
    return 1;
}

// (b): the button bar's gap, spreading its buttons over the room it has (`width` <= 0: the description's).
int32_t SpreadButtons(UI::RibbonElement* bar, const UI::Element* content, int32_t width) {
    const UI::ElementDescription* base = bar->_description;
    if (!::Object::IsDerivedFromNullSafe(base, UI::RibbonDescription::TypeId())) return 0;
    const int32_t retail = static_cast<const UI::RibbonDescription*>(base)->_spacing;
    int32_t want = retail;
    if (width > 0) {
        const int32_t room = Room(bar, content, width);
        if (room > 0) {
            int32_t used = 0;
            int32_t sized = 0;
            for (int32_t i = 0; i < bar->_children._size; ++i) {
                UI::Element* c = bar->_children._data[i];
                if (c == nullptr || !c->IsVisible()) continue;
                int32_t w = 0;
                int32_t h = 0;
                PaddedSize(c, w, h);
                used += w;
                if (w > 0 && h > 0) ++sized;
            }
            want = SpreadSpacing(retail, room - bar->_leftPad - bar->_rightPad, used, sized - 1);
        }
    }
    if (bar->_spacing == want) return 0;
    bar->_spacing = want;
    bar->SetNeedsSizeCalc();
    return 1;
}

int32_t Fill(UI::Element* e, const UI::Element* titleBar, const UI::Element* content, int32_t width) {
    if (e == nullptr || e == titleBar) return 0;
    if (IsBar(e)) return FillBar(e, content, width);
    int32_t changes = 0;
    const char* name = e->_name;
    if (name != nullptr && name[0] == 'b' && std::strcmp(name, "buttonBar") == 0 && IsHorizontalRibbon(e)) {
        changes += SpreadButtons(static_cast<UI::RibbonElement*>(e), content, width);
    }
    for (int32_t i = 0; i < e->_children._size; ++i) {
        changes += Fill(e->_children._data[i], titleBar, content, width);
    }
    return changes;
}

}  // namespace

int32_t TouchWideFill(UI::Element* root, UI::Element* titleBar, bool on) {
    if (root == nullptr || titleBar == nullptr) return 0;
    const UI::Element* content = titleBar->_parent;
    if (content == nullptr) return 0;
    // The content ribbon's inner width, as the row widened it: the title bar's minimum (0 when the row is
    // off or did not widen it: every row the description's).
    const UI::ElementDescription* desc = titleBar->_description;
    const bool widened = on && desc != nullptr && titleBar->_minWidth > desc->_minWidth;
    const int32_t width = widened ? titleBar->_minWidth : 0;
    return Fill(root, titleBar, content, width);
}

}  // namespace PhoneMenu
}  // namespace Ext
