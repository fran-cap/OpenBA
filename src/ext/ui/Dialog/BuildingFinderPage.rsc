// Dialog/BuildingFinderPage.rsc -- ext.building_finder's page in the extension window (NOT RETAIL; authored
// for this port, src/ext).
//
// docs/extensions/handins/BUILDINGFINDER.md, src/ext/building_finder/toolbar_page.cpp. A search box and
// every building the toolbar can place (the ones the town cannot build now too). A row is a name button that
// starts placing the building and a small button that adds it to the favorites (or takes it out). The window
// around the page is Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the
// page.
//
// The list is laid out like the Family page's (FamilyTreePage.rsc): the controller sizes the table and clones
// a row template into it.
//
// Controller: it looks up, by name: editFinderSearch, textFinderCount, textFinderStatus, buttonFinderClear,
// buttonFinderOpenFav, buttonFinderKey, textFinderKey, tableFinderAll, and in a row buttonFinderPick,
// textFinderPickName, buttonFinderStar and textFinderStarText. The row template is also used by
// FavoritesWindow.rsc.
//
// Texts: Dialog/ExtensionsStringTable.rsc (the Finder* keys).

// The page: the note, the search line, the list of buildings, the status line, the favorites window's two
// buttons. The window is one tab control as tall as its tallest page (the Foods page: a hint, a 350 px list
// and a line), so the list here is shorter. The favorites are in their own window (FavoritesWindow.rsc).
RibbonDescription pageBuildingFinder
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements
	[
		"textFinderHint" "ribbonFinderSearch" "ribbonFinderAll" "textFinderStatus" "ribbonFinderButtons"
	]
}

TextDescription textFinderHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	int _topPad = 6;
	String _text = "FinderHint";
}

// "Find:", the game's own edit field (as the Family page's), a button that empties it, and how many buildings
// match.
RibbonDescription ribbonFinderSearch
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	int _leftPad = 4;
	ElementDescription _elements [ "textFinderSearch" "editFinderSearch" "buttonFinderClear" "textFinderCount" ]
}

TextDescription textFinderSearch : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FinderSearch";
}

EditDescription editFinderSearch
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderSearchTip";
	ElementDescription _border = "Mod.rsc:borderLine";
	Font _font = "Font/FontSmall.rsc";
	int _maxLength = 32;
	int _minWidth = 200;
	int _minHeight = 20;
	int _leftPad = 3;
	int _rightPad = 3;
	int _topPad = 2;
	int _bottomPad = 2;
}

ButtonDescription buttonFinderClear : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderClearTip";
	ElementDescription _content = "textFinderClear";
}

TextDescription textFinderClear : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FinderClear";
}

// How many buildings the search finds ("38 buildings"), set by the controller.
TextDescription textFinderCount : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 8;
	int _minWidth = 110;
	String _text = "FinderParam";
}

// The last row: the button that opens the favorites window and the one that sets its hotkey.
RibbonDescription ribbonFinderButtons
{
	Alignment _alignment = MidLeft;
	int _cellPad = 8;
	int _leftPad = 4;
	ElementDescription _elements [ "buttonFinderOpenFav" "buttonFinderKey" ]
}

ButtonDescription buttonFinderOpenFav : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderOpenFavTip";
	Dialog _toolTipDialog = "ToolTip.rsc";
	ElementDescription _content = "textFinderOpenFav";
}

TextDescription textFinderOpenFav : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FinderOpenFav";
}

// The hotkey button: its label ("Favorites Window Hotkey: H") is set by the controller; a fixed minimum width
// keeps a longer key name from resizing the row.
ButtonDescription buttonFinderKey : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderKeyTip";
	Dialog _toolTipDialog = "ToolTip.rsc";
	ElementDescription _content = "textFinderKey";
}

TextDescription textFinderKey : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	int _minWidth = 190;
	String _text = "FinderParam";
}

// What the last press did. One line in a box that cannot grow, so the controller keeps the text short.
TextDescription textFinderStatus : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	String _text = "FinderParam";
}

// The list of all buildings: the Extensions dialog's frame (by reference) around the scrolling box.
RibbonDescription ribbonFinderAll : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFinderAllBox" ]
}

ContainerDescription containerFinderAllBox : "Extensions.rsc:tableList"
{
	int _height = 288;
	ElementDescription _content = "tableFinderAll";
}

// One column. The controller sizes it (rows = buildings shown) and fills it with ribbonFinderCell.
TableDescription tableFinderAll
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonFinderCell"; } ]
}

// One building: the small favorite button, then the name button (its text: the name, the group it is in, and
// the reason when its limit is reached). The controller gives the cell the list's width and the name button
// what is left of it.
RibbonDescription ribbonFinderCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _rightPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 4;
	ElementDescription _elements [ "buttonFinderStar" "buttonFinderPick" ]
}

ButtonDescription buttonFinderPick : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderPickTip";
	Dialog _toolTipDialog = "ToolTip.rsc";
	ElementDescription _content = "textFinderPickName";
}

TextDescription textFinderPickName : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FinderParam";
}

ButtonDescription buttonFinderStar : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FinderStarTip";
	Dialog _toolTipDialog = "ToolTip.rsc";
	int _minWidth = 30;
	ElementDescription _content = "textFinderStarText";
}

// The "+" / "-": a larger font than the labels, so the sign is easy to see and hit.
TextDescription textFinderStarText : "Mod.rsc:labelMod"
{
	Font _font = "Font/FontMedium.rsc";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FinderParam";
}
