// Dialog/ItemLimitsPanel.rsc -- ext.item_limits's row on a workshop's panel (NOT RETAIL; authored for this port,
// src/ext).
//
// docs/extensions/handins/ITEMLIMITS.md, src/ext/item_limits/panel_row.cpp. One row under the panel's Product
// row: a check box and "<product> limit". The controller adds a copy of the category limit's own spinner
// (the panel's "editLimit", made from its description) as the row's last element, so the spinner looks like
// that one, and puts the row in a column with the Product row (ribbonItemLimitsColumn).
//
// Controller: src/ext/item_limits/panel_row.cpp. It looks up, by name: checkItemLimitsRow, textItemLimitsRow.
//
// Texts: Dialog/ExtensionsStringTable.rsc (the ItemLimits* keys).

// The column the controller puts in the Product row's place: the Product row, then this row under it, so the
// row takes its own line at the panel's foot and the panel grows by its height.
RibbonDescription ribbonItemLimitsColumn
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
}

RibbonDescription ribbonItemLimitsRow
{
	Alignment _alignment = MidLeft;
	int _topPad = 2;
	int _cellPad = 4;
	ElementDescription _elements [ "checkItemLimitsRow" "textItemLimitsRow" ]
}

CheckDescription checkItemLimitsRow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ItemLimitsCheckTip";
}

// "@0 limit": the controller sets @0 to the product's name, as the panel's combo shows it.
TextDescription textItemLimitsRow : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ItemLimitsLabel";
}
