// Dialog/StorageMetricsTab.rsc -- ext.storage_metrics' tab in the Town Hall window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/STORAGEMETRICS.md, src/ext/storage_metrics/townhall_tab.cpp. The used and total space of the
// town's stockpiles, storage barns and trading posts: a line per kind (buildings, "used / capacity", how full in
// percent) and, under a kind with more than one building type, a line per type. The window round the tab is the game's
// Dialog/TownHall.rsc; the controller adds this file's button and page to its tab control after the game's own tabs.
//
// Controller: it looks up tableStorageMetrics in the page and, in each row it clones (ribbonStorageMetricsTotalRow for a
// kind's total, ribbonStorageMetricsTypeRow for a building type), its name cell, textStorageMetricsBuildings,
// textStorageMetricsSpace and textStorageMetricsFull, whose text is the parameter of our table's "@0" string.
// Texts: Dialog/ExtensionsStringTable.rsc (the StorageMetrics* keys). The looks are the Town Hall's own, by reference.
//
// A base must be an object the game's dialogs use as an element themselves (TownHall.rsc:buttonInventory,
// storageHeader): the installed packages do not hold the ones that are only ever a base (StandardDialog.rsc:buttonTab,
// TownHall.rsc:tableText), so the plain cells below are written out in full.

// The tab's button: the Town Hall's Inventory tab button and label (icon left of the text), with our own 16 px
// crate glyph (tools/ext_icons/storagemetrics.py; the game's sheets have no box, and its storage icon is the
// Inventory tab's).
ButtonDescription buttonStorageMetricsTab : "TownHall.rsc:buttonInventory"
{
	ElementDescription _content = "labelStorageMetricsTab";
}

LabelDescription labelStorageMetricsTab : "TownHall.rsc:labelInventory"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc";
	String _spriteName = "ExtensionsGlyph_storage_metrics_tab";
	String _text = "StorageMetricsTab";
}

// The page: the title, the column headers, the scrolling list.
RibbonDescription pageStorageMetrics
{
	Alignment _alignment = TopLeft; bool _vertical = true;
	int _leftPad = 4; int _rightPad = 4; int _topPad = 4; int _bottomPad = 4;
	ElementDescription _elements [ "textStorageMetricsTitle" "ribbonStorageMetricsHeader" "containerStorageMetrics" ]
}

TextDescription textStorageMetricsTitle : "TownHall.rsc:inventoryTitle"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsTitle";
}

// The column headers, as wide as the rows' cells below them.
RibbonDescription ribbonStorageMetricsHeader
{
	Alignment _alignment = TopLeft; int _cellPad = 2; int _leftPad = 8;
	ElementDescription _elements
	[
		"textStorageMetricsHeaderName" "textStorageMetricsHeaderBuildings" "textStorageMetricsHeaderSpace"
		"textStorageMetricsHeaderFull"
	]
}

TextDescription textStorageMetricsHeaderName : "TownHall.rsc:storageHeader"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsHeaderName"; int _minWidth = 190;
}

TextDescription textStorageMetricsHeaderBuildings : "TownHall.rsc:storageHeader"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsHeaderBuildings"; int _minWidth = 64;
}

TextDescription textStorageMetricsHeaderSpace : "TownHall.rsc:storageHeader"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsHeaderSpace"; int _minWidth = 150;
}

TextDescription textStorageMetricsHeaderFull : "TownHall.rsc:storageHeader"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsHeaderFull"; int _minWidth = 56;
}

// The scrolling list, the Town Hall inventory's box: as wide as its rows, a fixed height with a scroll bar for a
// town (a modded one) with more building types than fit.
ContainerDescription containerStorageMetrics
{
	int _topPad = 2;
	int _leftPad = 8;
	int _bottomPad = 8;
	int _rightPad = 8;

	int _width = -1;
	int _height = 262;
	int _scrollSpacing = 4;

	bool _allowShrinkHeight = true;

	ElementDescription _verticalScroll = "SharedElements.rsc:verticalScrollbar";
	ElementDescription _content = "tableStorageMetrics";
}

// One column. The controller sizes it (rows = the lines shown) and fills it with the two row templates.
TableDescription tableStorageMetrics
{
	int _width = 1;
	int _height = 1;
	int _spacing = 2;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonStorageMetricsTotalRow"; } ]
}

// A kind's total: the name in the header look, then the three figures.
RibbonDescription ribbonStorageMetricsTotalRow
{
	int _cellPad = 2;
	ElementDescription _elements
	[
		"textStorageMetricsTotalName" "textStorageMetricsBuildings" "textStorageMetricsSpace" "textStorageMetricsFull"
	]
}

// One building type of the kind above it: the name set in from the left.
RibbonDescription ribbonStorageMetricsTypeRow
{
	int _cellPad = 2;
	ElementDescription _elements
	[
		"textStorageMetricsTypeName" "textStorageMetricsBuildings" "textStorageMetricsSpace" "textStorageMetricsFull"
	]
}

// The name cell of each row template; a row holds one of them, and the controller looks up both names.
TextDescription textStorageMetricsTotalName : "TownHall.rsc:storageHeader"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageMetricsParam"; int _minWidth = 190;
}

TextDescription textStorageMetricsTypeName
{
	ElementDescription _border = "SharedElements.rsc:flatButtonEnabledBorder";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	Alignment _alignment = MidLeft;
	Font _font = "Font/FontSmall.rsc";
	String _text = "StorageMetricsParam"; int _minWidth = 178;
	int _leftPad = 14; int _topPad = 2; int _bottomPad = 2;
}

// The figures: the Town Hall's table cell look (small font on the flat cell border), under their headers.
TextDescription textStorageMetricsBuildings
{
	ElementDescription _border = "SharedElements.rsc:flatButtonEnabledBorder";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	Alignment _alignment = MidLeft;
	Font _font = "Font/FontSmall.rsc";
	String _text = "StorageMetricsParam"; int _minWidth = 64;
	int _leftPad = 2; int _topPad = 2; int _bottomPad = 2;
}

TextDescription textStorageMetricsSpace
{
	ElementDescription _border = "SharedElements.rsc:flatButtonEnabledBorder";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	Alignment _alignment = MidLeft;
	Font _font = "Font/FontSmall.rsc";
	String _text = "StorageMetricsParam"; int _minWidth = 150;
	int _leftPad = 2; int _topPad = 2; int _bottomPad = 2;
}

TextDescription textStorageMetricsFull
{
	ElementDescription _border = "SharedElements.rsc:flatButtonEnabledBorder";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	Alignment _alignment = MidLeft;
	Font _font = "Font/FontSmall.rsc";
	String _text = "StorageMetricsParam"; int _minWidth = 56;
	int _leftPad = 2; int _topPad = 2; int _bottomPad = 2;
}
