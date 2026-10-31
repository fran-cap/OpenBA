// Dialog/FamilyTreeSummary.rsc -- ext.family_tree's town summary window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/FAMILYTREE.md section 4d. A utility-style window like Dialog/FamilyTreeWindow.rsc (the game's
// own title bar, no flags: not modal, it does not pause the game): a note and a scrolling list of rows, each a heading or
// a label and its numbers. The rows are the Family page's relation cell (FamilyTreePage.rsc:ribbonFamilyRelationCell) with
// its person button hidden.
//
// Controller: src/ext/family_tree/summary_window.cpp. It looks up, by name: labelTitle, buttonClose, borderTitle (the
// title bar's) and tableFamilySummary.

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
	ElementDescription _elements [ "StandardDialog.rsc:titleBar" "textFamilySummaryNote" "ribbonFamilySummary" ]
}

TextDescription textFamilySummaryNote : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	String _text = "FamilySummaryNote";
}

RibbonDescription ribbonFamilySummary : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFamilySummaryBox" ]
}

// The width of the Extensions window's list and of the tree window's.
ContainerDescription containerFamilySummaryBox : "Extensions.rsc:tableList"
{
	int _width = 572;
	int _height = 300;
	ElementDescription _content = "tableFamilySummary";
}

// One column. The controller sizes it (one row per line of the summary) and fills it with the relation cell.
TableDescription tableFamilySummary
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "FamilyTreePage.rsc:ribbonFamilyRelationCell"; } ]
}
