// Dialog/StorageFilterTab.rsc -- ext.storage_filter's tab in a storage building's window (NOT RETAIL; authored for this
// port, src/ext).
//
// docs/extensions/handins/STORAGEFILTER.md, src/ext/storage_filter/panel_tab.cpp. One cell per item the town knows, a check
// box each: a checked item is delivered to this building, an unchecked one is not (citizens still take what is inside). An
// item the building's flags do not allow is not listed: the tab shows only what the building can store. The window round the
// tab is the building's own (Dialog/StandardDialog.rsc:entity, the tab layout); the controller adds this file's button and
// page to its tab control after the game's own tabs.
//
// Controller: it looks up tableStorageFilter in the page and, in each cell it clones (ribbonStorageFilterCell),
// checkStorageFilterRow and labelStorageFilterRow, whose text is the parameter of our table's "@0" string.
// Texts: Dialog/ExtensionsStringTable.rsc (the StorageFilter* keys). The looks are the game's own, by reference.

// The tab's button: the building window's Storage tab button, with our own text and our own glyph (a crate with a check mark).
ButtonDescription buttonStorageFilterTab : "Storage.rsc:buttonTabStorage"
{
	ElementDescription _content = "labelStorageFilterTab";
}

LabelDescription labelStorageFilterTab : "Storage.rsc:labelTabStorage"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc";
	String _spriteName = "ExtensionsGlyph_storage_filter_tab";
	String _text = "StorageFilterTab";
}

// The page: the hint, then the scrolling list. Left-aligned, 4 px between them.
RibbonDescription pageStorageFilter
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements [ "textStorageFilterHint" "containerStorageFilter" ]
}

// The note above the list: what the check boxes do.
TextDescription textStorageFilterHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	int _topPad = 4;
	String _text = "StorageFilterHint";
}

// The scrolling box, the building's own storage list (Storage.rsc:storageContainer) with our table as its content.
ContainerDescription containerStorageFilter : "Storage.rsc:storageContainer"
{
	// Always the container's full height (130 px), as the Inventory tab's list is: a short list must not shrink the window.
	bool _allowShrinkHeight = false;
	ElementDescription _content = "tableStorageFilter";
}

// Three columns here, as the building's own Storage tab has; the controller re-shapes it to as many columns as fit the box (rows = items / columns) and fills it
// with ribbonStorageFilterCell.
TableDescription tableStorageFilter
{
	int _width = 3;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonStorageFilterCell"; } ]
}

// One item: the check box and the compact label. The border is the Mods list's row border, tinted by the controller
// (red while the item is refused).
RibbonDescription ribbonStorageFilterCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	Flags _flags = ExpandWidth;
	int _leftPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 2;
	ElementDescription _elements [ "checkStorageFilterRow" "labelStorageFilterRow" ]
}

CheckDescription checkStorageFilterRow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "StorageFilterCheckTip";
}

// The Town Hall's inventory item label (20 px icon, small font), its text from our table's "@0".
LabelDescription labelStorageFilterRow : "TownHall.rsc:storageItem"
{
	int _minWidth = 80;
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "StorageFilterParam";
}
