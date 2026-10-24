// Dialog/ItemPriorityPage.rsc -- ext.item_priority's page in the extension window (NOT RETAIL; authored for this
// port, src/ext).
//
// docs/extensions/handins/ITEMPRIO.md, src/ext/item_priority/toolbar_page.cpp. The town ranks the items its
// citizens fetch from a storage, per category (tools, fuel, clothing, health): a tab strip chooses the category
// and each tab holds that category's list, a row per item: a check box to rank it (it goes to the bottom of the
// ranking), a number (its rank, best first) and Top / Up / Down to move it. A citizen takes the best-ranked item the
// storages hold; an item that is not ranked comes after every ranked one. The window around the page is
// Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the page.
//
// The strip is the engine's own tab control, declared here like the window's (ExtensionsWindow.rsc:tabPages):
// the tab that is showing is the category that is showing, so the page needs no title for the list. Each tab's
// content is its own list; the controller fills the table of the tab that is showing. The rows are laid out
// like the Foods page's (RestrictedFoodsPage.rsc): the controller sizes the table and clones
// ribbonItemPriorityCell into it, as TownHallUI::InitInventory does.
//
// Controller: src/ext/item_priority/toolbar_page.cpp. It looks up, by name: tabItemPriorityCategories (its four
// tab buttons are the category buttons: tools, fuel, clothing, health), tableItemPriorityTool / Fuel / Clothing
// / Health, checkItemPriorityShowAll, and in each cell checkItemPriorityRow, labelItemPriorityRow,
// textItemPriorityRank and ribbonItemPriorityMoves (its two children are Up and Down).
//
// Texts: Dialog/ExtensionsStringTable.rsc (the ItemPriority* keys).

// The page: the hint, the category tabs and their lists, the "Show all items" check box; 4 px between them.
// Left-aligned, as most of the game is.
RibbonDescription pageItemPriority
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements [ "textItemPriorityHint" "tabItemPriorityCategories" "ribbonItemPriorityShowAll" ]
}

// The note under the tab: what the list does.
TextDescription textItemPriorityHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;		// the first letter in line with the check boxes of the list
	int _topPad = 10;		// as much room above the line as below it
	String _text = "ItemPriorityHint";
}

// The category strip: the Mods dialog's tab frame, four tabs in the order tools, fuel, clothing, health. Each
// button is the Mods dialog's small tab button (the pair ExtensionsWindow.rsc:tabButtonPage uses), its caption
// a category name.
TabDescription tabItemPriorityCategories : "Mod.rsc:tabMod"
{
	Tab _tabs
	[
		{ ElementDescription _button = "buttonItemPriorityTool"; ElementDescription _content = "ribbonItemPriorityListTool"; }
		{ ElementDescription _button = "buttonItemPriorityFuel"; ElementDescription _content = "ribbonItemPriorityListFuel"; }
		{ ElementDescription _button = "buttonItemPriorityClothing"; ElementDescription _content = "ribbonItemPriorityListClothing"; }
		{ ElementDescription _button = "buttonItemPriorityHealth"; ElementDescription _content = "ribbonItemPriorityListHealth"; }
	]
}

ButtonDescription buttonItemPriorityTool : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textItemPriorityTool";
}

TextDescription textItemPriorityTool : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatTool";
}

ButtonDescription buttonItemPriorityFuel : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textItemPriorityFuel";
}

TextDescription textItemPriorityFuel : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatFuel";
}

ButtonDescription buttonItemPriorityClothing : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textItemPriorityClothing";
}

TextDescription textItemPriorityClothing : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatClothing";
}

ButtonDescription buttonItemPriorityHealth : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textItemPriorityHealth";
}

TextDescription textItemPriorityHealth : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatHealth";
}

// The check box under the list: unchecked shows the items the town has made or stored (and the ranked ones),
// checked shows every item of the category.
RibbonDescription ribbonItemPriorityShowAll
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "checkItemPriorityShowAll" "textItemPriorityShowAll" ]
}

CheckDescription checkItemPriorityShowAll : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ItemPriorityShowAllTip";
}

TextDescription textItemPriorityShowAll : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ItemPriorityShowAll";
}

// One list per tab: the Extensions dialog's frame (by reference) around the scrolling box of items.
RibbonDescription ribbonItemPriorityListTool : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerItemPriorityBoxTool" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerItemPriorityBoxTool : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableItemPriorityTool";
}

// One column. The controller sizes it (rows = shown items) and fills it with ribbonItemPriorityCell.
TableDescription tableItemPriorityTool
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;		// a gap between rows, so a row's background never runs into the next
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonItemPriorityCell"; } ]
}

RibbonDescription ribbonItemPriorityListFuel : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerItemPriorityBoxFuel" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerItemPriorityBoxFuel : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableItemPriorityFuel";
}

// One column. The controller sizes it (rows = shown items) and fills it with ribbonItemPriorityCell.
TableDescription tableItemPriorityFuel
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;		// a gap between rows, so a row's background never runs into the next
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonItemPriorityCell"; } ]
}

RibbonDescription ribbonItemPriorityListClothing : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerItemPriorityBoxClothing" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerItemPriorityBoxClothing : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableItemPriorityClothing";
}

// One column. The controller sizes it (rows = shown items) and fills it with ribbonItemPriorityCell.
TableDescription tableItemPriorityClothing
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;		// a gap between rows, so a row's background never runs into the next
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonItemPriorityCell"; } ]
}

RibbonDescription ribbonItemPriorityListHealth : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerItemPriorityBoxHealth" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerItemPriorityBoxHealth : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableItemPriorityHealth";
}

// One column. The controller sizes it (rows = shown items) and fills it with ribbonItemPriorityCell.
TableDescription tableItemPriorityHealth
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;		// a gap between rows, so a row's background never runs into the next
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonItemPriorityCell"; } ]
}

// One item: the check box, the compact label, its rank, and Top / Up / Down. The border is the Mods list's row border,
// tinted by the controller while the item is ranked. No width here: the controller makes the cell as wide as the
// list's viewport (Ext::ListWidth, src/ext/ui/list_width.h) and gives the label what the check box, the rank and
// the buttons leave, so the row fills the list at any UI scale.
RibbonDescription ribbonItemPriorityCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _rightPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 4;
	ElementDescription _elements [ "checkItemPriorityRow" "labelItemPriorityRow" "textItemPriorityRank" "ribbonItemPriorityMoves" ]
}

CheckDescription checkItemPriorityRow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ItemPriorityCheckTip";
}

// The Town Hall's inventory item label (20 px icon, small font), its text from our table's "@0". Its width is
// the controller's (FitRow): the rest of the row, so the rank and Top / Up / Down end at the list's right.
LabelDescription labelItemPriorityRow : "TownHall.rsc:storageItem"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityParam";
}

// The item's place in the ranking ("1" for the best), or a dash while it is not ranked; the controller sets it.
// A fixed width: the controller pads its left (a pad counts toward the box, so there is no _minWidth) so "1", "10" and "-" end at the same place and the buttons stay in
// line (the engine justifies only wrapped text, and only against its other lines).
TextDescription textItemPriorityRank : "Extensions.rsc:textProfile"
{
	String _text = "ItemPriorityParam";
}

RibbonDescription ribbonItemPriorityMoves
{
	Alignment _alignment = MidLeft;
	int _cellPad = 2;
	ElementDescription _elements [ "buttonItemPriorityTop" "buttonItemPriorityUp" "buttonItemPriorityDown" ]
}

// The small button of the Mods list and the tab strip (not the dialog's large OK button): a row is as tall as
// its label.
ButtonDescription buttonItemPriorityTop : "Mod.rsc:buttonMod" { ElementDescription _content = "textItemPriorityTop"; }
ButtonDescription buttonItemPriorityUp : "Mod.rsc:buttonMod" { ElementDescription _content = "textItemPriorityUp"; }
ButtonDescription buttonItemPriorityDown : "Mod.rsc:buttonMod" { ElementDescription _content = "textItemPriorityDown"; }

TextDescription textItemPriorityTop : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityTop";
}
TextDescription textItemPriorityUp : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityUp";
}
TextDescription textItemPriorityDown : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityDown";
}
