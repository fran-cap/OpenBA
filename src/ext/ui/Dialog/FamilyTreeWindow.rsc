// Dialog/FamilyTreeWindow.rsc -- ext.family_tree's tree window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/FAMILYTREE.md section 4a. A utility-style window like Dialog/ExtensionsWindow.rsc (the game's own
// title bar, no flags: not modal, it does not pause the game): the buttons that slide the tree, a button that shows the
// Family page, and the drawn tree. The cells the tree is made of (boxes, spaces, line tiles) are in FamilyTreePage.rsc.
//
// Controller: src/ext/family_tree/tree_window.cpp and tree_view.cpp. It looks up, by name: labelTitle, buttonClose,
// borderTitle (the title bar's), buttonFamilyTreeLeft, buttonFamilyTreeRight, buttonFamilyTreeDetails, tableFamilyTree.

Dialog resource
{
	ElementDescription _rootElement = "dialogRoot";
}

GroupDescription dialogRoot
{
	Alignment _alignment = TopLeft; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 4;
	int _leftPad = 8; int _rightPad = 8; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "StandardDialog.rsc:titleBar" "ribbonFamilyTreeBar" "ribbonFamilyTree" ]
}

// The two buttons that slide the tree sideways when it is wider than the window, and the one that shows the Family page.
RibbonDescription ribbonFamilyTreeBar
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "buttonFamilyTreeLeft" "buttonFamilyTreeRight" "buttonFamilyTreeDetails" "textFamilyTreeNote" ]
}

ButtonDescription buttonFamilyTreeLeft : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyTreeLeft";
}

TextDescription textFamilyTreeLeft : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyTreeLeft";
}

ButtonDescription buttonFamilyTreeRight : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyTreeRight";
}

TextDescription textFamilyTreeRight : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyTreeRight";
}

ButtonDescription buttonFamilyTreeDetails : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyTreeDetails";
}

TextDescription textFamilyTreeDetails : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyTreeDetails";
}

TextDescription textFamilyTreeNote : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FamilyTreeNote";
}

RibbonDescription ribbonFamilyTree : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFamilyTreeBox" ]
}

ContainerDescription containerFamilyTreeBox : "Extensions.rsc:tableList"
{
	int _width = 572;
	int _height = 206;
	ElementDescription _content = "tableFamilyTree";
}

// One column of rows. The controller sizes it (seven rows: four of boxes, three of line tiles) and fills each with a
// ribbonFamilyTreeRow it has filled.
TableDescription tableFamilyTree
{
	int _width = 1;
	int _height = 1;
	int _spacing = 0;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonFamilyTreeRow"; } ]
}

// A row of the tree: boxes, spaces and line tiles side by side with no gap; the controller adds them.
RibbonDescription ribbonFamilyTreeRow
{
	int _cellPad = 0;
}
