#pragma once
// ============================================================================
// src/ext/item_limits/panel_common.h -- NOT RETAIL. ext.item_limits: what the workshop panel's row
// (panel_row.cpp) and the pasture panel's rows (pasture_rows.cpp) share. Game side only (not in the unit
// build). docs/extensions/handins/ITEMLIMITS.md section 4.
//
// A row is our own description (ItemLimitsPanel.rsc:ribbonItemLimitsRow: a check box and "<item> limit") plus
// a copy of the panel's category limit spinner ("editLimit", ResourceLimitUI), made from its description so it
// looks like that one. Checked = the item has its own limit, the spinner's value; changing the spinner of an
// unchecked row checks it. Every panel showing an item follows a change at its next layout.
// ============================================================================
#include <cstdint>
#include <string>

#include "object/Internal.h"
#include "ui/EventId.h"

class ComponentDescription;
namespace System { class String; }
namespace UI {
class CheckElement;
class Dialog;
class DialogInstance;
class Element;
class ElementDescription;
class SpinnerElement;
}

namespace Ext {
namespace ItemLimits {
namespace PanelRows {

struct Row {
    UI::CheckElement* check = nullptr;
    UI::Element* text = nullptr;
    UI::SpinnerElement* spinner = nullptr;
    const void* product = nullptr;   // the ComponentDescription the row shows; null until it is pointed at one
    std::string path;                // its RawMaterialDescription's template path
};

// The window our descriptions are instanced against (ExtensionsWindow.rsc); null when it did not load.
const UI::Dialog* WindowDialog();
// One of our element descriptions by its rsc path; null when it did not load.
UI::ElementDescription* Template(const char* rsc);
// A vertical column (ItemLimitsPanel.rsc:ribbonItemLimitsColumn), made against `window`; false when it did not
// load. The caller owns it until it joins the panel's tree.
bool MakeColumn(const UI::Dialog* window, Object::InternalPtr<UI::Element>* out);
// Frees an element made by CreateInstance that never joined the panel's tree.
void Discard(Object::InternalPtr<UI::Element>& elem);
// Appends `elem` to `holder`: a ribbon through AddItem (its extents grow with its children), else AddChild.
void Append(UI::Element* holder, Object::InternalPtr<UI::Element>* elem);
// Puts `elem` on its own line under `target`: a vertical column takes `target`'s slot in `holder` (same
// alignment and stretch) and holds `target`, then `elem`. False (nothing changed) when the slot is not found or
// the column did not load.
bool AddUnder(UI::Element* holder, UI::Element* target, const UI::Dialog* window,
              Object::InternalPtr<UI::Element>* elem);
// Puts `fresh` in `old`'s slot in its parent and frees `old` (and its children). False (nothing changed, the
// caller still owns `fresh`) when `old` has no parent or is not among its children.
bool Replace(UI::Element* old, Object::InternalPtr<UI::Element>* fresh);
// The panel's category limit spinner ("editLimit"); null when the panel has none.
UI::SpinnerElement* CategorySpinner(UI::DialogInstance* dialog);
// One row: our check box and text, then a copy of `category`, with the given element ids and the category
// spinner's range (ext.high_limits' when it is on). False (logged, nothing left over) on a failure.
bool MakeRow(UI::SpinnerElement* category, const UI::Dialog* window, int32_t checkId, int32_t spinnerId, Row& row,
             Object::InternalPtr<UI::Element>* out);
// The town's current limit for a stock category (Town Hall Limits); 0 when there is no town or no such row.
int32_t CategoryLimit(int32_t category);
// Points the row at `product` (a ComponentDescription), named `name`: the label, and the item's limit when it
// has one, else `categoryLimit` unchecked.
void PointAt(Row& row, const ComponentDescription* product, const System::String& name, int32_t categoryLimit);
// The item's limit may have changed elsewhere (another panel, a load): show the current one.
void Follow(Row& row);
// The row's check box (`isCheck`) or spinner sent `msg`: sets or clears the item's limit.
void Apply(Row& row, UI::EventId msg, bool isCheck);

// pasture_rows.cpp: forget every pasture panel's rows (PanelsForget calls it).
void ForgetPastureRows();

}  // namespace PanelRows
}  // namespace ItemLimits
}  // namespace Ext
