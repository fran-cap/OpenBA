// src/ext/high_limits/spinner.cpp -- ext.high_limits' spinner glue (NOT RETAIL; exe only)
//
// docs/extensions/handins/HIGHLIMITS.md. Called when the row is active from ResourceLimitUI::OnCreated (where
// every retail limit spinner is set up) and from ext.item_limits' panel row (panel_row.cpp).

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include <algorithm>

#include "ui/Element.h"
#include "ui/EditElement.h"
#include "ui/NumberEditElement.h"
#include "ui/SpinnerElement.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"

#include "ext/high_limits/high_limits.h"

namespace Ext {
namespace HighLimits {

void ApplyToSpinner(UI::SpinnerElement* spinner)
{
    if (spinner == nullptr)
        return;
    UI::Element* child = spinner->Child(0).Get();
    if (::Object::IsDerivedFromNullSafe(child, UI::NumberEditElement::TypeId())) {
        UI::NumberEditElement* edit = static_cast<UI::NumberEditElement*>(child);
        // The edit field's typed-character limit (the description's 6), then a minimum width that fits nine
        // digits. The width is raised, never lowered: a description that asked for a wider box keeps it.
        // Element::SetMinSize is declared but not defined in this port; OnCreated runs before the first
        // layout, so the field is enough.
        edit->_maxLength = MaxDigits();
        edit->_minWidth = std::max(edit->_minWidth, MinWidth());
    }
    // SetMinMax re-clamps the field's current text through the new maximum.
    spinner->SetMinMax(0, MaxValue());
}

}  // namespace HighLimits
}  // namespace Ext
