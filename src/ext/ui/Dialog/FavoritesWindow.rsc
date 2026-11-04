// Dialog/FavoritesWindow.rsc -- ext.building_finder's favorites window (NOT RETAIL; authored for this port,
// src/ext).
//
// docs/extensions/handins/BUILDINGFINDER.md. A small utility-style window like Dialog/FamilyTreeWindow.rsc
// (the game's own title bar, no flags: not modal, it does not pause the game): the player's favorite
// buildings as picture buttons, the building's own toolbar icon, like the toolbar's (a press starts placing
// it; the tooltip is the name, the description and the limit note), a line of text, and a button to the Find
// Buildings page that shows only while there are no favorites. Favorites are added and taken out on that
// page, not here.
//
// Controller: src/ext/building_finder/favorites_window.cpp. It looks up, by name: labelTitle, buttonClose,
// borderTitle (the title bar's), tableFavoritesWindow, textFavoritesWindowNote and buttonFavoritesOpenPage
// (on the note's line).

Dialog resource
{
	ElementDescription _rootElement = "dialogRoot";
}

GroupDescription dialogRoot
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 4;
	int _leftPad = 8; int _rightPad = 8; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "StandardDialog.rsc:titleBar" "ribbonFavoritesWindow" "ribbonFavoritesNote" ]
}

// The line under the grid: the note (what the last press did, or that there are no favorites) and, on the
// same line, the button: to the Find Buildings page while there are no favorites, "Clear All" once there are.
// The note's minimum width and the button together are no wider than the grid, so the window keeps its size.
RibbonDescription ribbonFavoritesNote
{
	Alignment _alignment = MidLeft;
	int _cellPad = 8;
	ElementDescription _elements [ "textFavoritesWindowNote" "buttonFavoritesOpenPage" ]
}

TextDescription textFavoritesWindowNote : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	int _minWidth = 130;
	String _text = "FinderParam";
}

ButtonDescription buttonFavoritesOpenPage : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FavoritesOpenPageTip";
	Dialog _toolTipDialog = "ToolTip.rsc";
	ElementDescription _content = "textFavoritesOpenPage";
}

// The button's label is set by the controller: "Open Find Buildings" while there are no favorites, "Clear
// All" (then "Sure? Clear All") once there are. A fixed minimum width keeps the line from changing size.
TextDescription textFavoritesOpenPage : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	int _minWidth = 130;
	String _text = "FinderParam";
}

RibbonDescription ribbonFavoritesWindow : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFavoritesWindowBox" ]
}

ContainerDescription containerFavoritesWindowBox : "Extensions.rsc:tableList"
{
	int _width = 300;
	int _height = 220;
	ElementDescription _content = "tableFavoritesWindow";
}

// A grid. The controller sizes it (as many columns as the box is wide) and clones buttonFavoriteIcon into it.
TableDescription tableFavoritesWindow
{
	int _width = 4;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "buttonFavoriteIcon"; } ]
}

// One favorite: the game's own toolbar button (as the Town Screenshot page's picture buttons are). The
// controller puts the building's icon on the image and its text on the tooltip.
ImageDescription imageFavoriteIcon : "Mod.rsc:imageIcon"
{
	int _minWidth = 40;
	int _minHeight = 40;
}

ButtonDescription buttonFavoriteIcon : "ToolBar.rsc:buttonToolbar"
{
	ElementDescription _content = "imageFavoriteIcon";
	Dialog _toolTipDialog = "ToolTip.rsc";
}
