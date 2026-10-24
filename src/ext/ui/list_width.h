// src/ext/ui/list_width.h -- Ext::ListWidth: the width a scrolling list gives its rows
//
// NOT RETAIL. A ContainerDescription's content is laid out at the content's OWN padded size, not at the
// viewport's (ClipRegionElement::ComputeLayout), so a list whose rows fill it has to be as wide as the
// viewport. Retail spells that width as a literal: Mod.rsc:tableMods is `_minWidth = 580`, the Mods list's
// 596 less its scroll bar and the bar's 4 px gap. Our pages derive it from the container instead, because
// the scroll bar is the game's (SharedElements.rsc:verticalScrollbar, which a mod may change) and our .rsc
// text cannot name its width. The value is the clip region's requested width, the one number
// ContainerElement::ComputeSize itself derives from the container's `_width`, its `_scrollSpacing` and
// the bar's padded width: no constant here. The rows then stretch across it with the engine's own
// `Flags _flags = ExpandWidth` (retail's StatusBar.rsc:groupCommon pairs it with a `_minWidth` the same way).
#pragma once

#include <cstdint>

#include "object/TypeInfo.h"
#include "ui/ClipRegionElement.h"
#include "ui/ContainerElement.h"
#include "ui/Element.h"

namespace Ext {

// The viewport width of the scrolling list whose content is `content` (a table or ribbon given as a
// ContainerDescription's `_content`); 0 when `content` is not a container's content.
inline int32_t ListWidth(UI::Element* content) {
    if (content == nullptr) {
        return 0;
    }
    UI::Element* clip = content->_parent;
    if (!::Object::IsDerivedFromNullSafe(clip, UI::ClipRegionElement::TypeId())) {
        return 0;
    }
    UI::Element* container = clip->_parent;
    if (!::Object::IsDerivedFromNullSafe(container, UI::ContainerElement::TypeId())) {
        return 0;
    }
    // The container publishes the viewport's width into the clip region when it sizes itself; a container
    // that has not been sized since it was built (the page has just opened) is sized here, by its own body.
    container->ComputeSize();
    return static_cast<UI::ClipRegionElement*>(clip)->_reqWidth;
}

}  // namespace Ext
