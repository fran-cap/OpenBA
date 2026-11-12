// Dialog/TraderRequestPanel.rsc -- ext.trader_request's row on the trading post window's Orders tab (NOT
// RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/TRADERREQUEST.md, src/ext/trader_request/panel_row.cpp. One row at the foot of the
// tab: the text "Request trader (price mult)". The controller adds two copies of the tab's order combo
// (the window's "comboOrder", made from its description) as the row's last elements, so they look like that
// one: the traders ("Any trader" first) and the mode (next visit / every visit). It puts the row in a column
// with the order combo's parent (ribbonTraderRequestColumn).
//
// Controller: src/ext/trader_request/panel_row.cpp. It looks up, by name: textTraderRequestRow.
//
// Texts: Dialog/ExtensionsStringTable.rsc (the TraderRequest* keys).

// The column the controller puts in the place of the order combo's parent: that parent, then this row.
// The bottom pad keeps the row's combos clear of the page's lower border (a pad on the row itself adds none).
RibbonDescription ribbonTraderRequestColumn
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _bottomPad = 6;
}

RibbonDescription ribbonTraderRequestRow
{
	Alignment _alignment = MidLeft;
	int _leftPad = 8;
	int _topPad = 2;
	int _cellPad = 4;
	ElementDescription _elements [ "textTraderRequestRow" ]
}

TextDescription textTraderRequestRow : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "TraderRequestLabel";
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "TraderRequestTip";
}
