// Dialog/FamilyTreePage.rsc -- ext.family_tree's page in the extension window (NOT RETAIL; authored for this port,
// src/ext).
//
// docs/extensions/handins/FAMILYTREE.md, src/ext/family_tree/toolbar_page.cpp. Two lists: the town's people (the living,
// and the dead when "Show people who have died" is checked), and the family of the person picked in the first: that
// person's own dates, parents, grandparents, spouses, children and siblings, one row each. A name in the second list is a
// button: pressing it picks that person, so the page walks up and down the tree. The window around the page is
// Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the page.
//
// The lists are laid out like the Priority page's (ItemPriorityPage.rsc): the controller sizes each table and clones a row
// template into it, as TownHallUI::InitInventory does.
//
// Controller: src/ext/family_tree/toolbar_page.cpp. It looks up, by name: checkFamilyShowDead, tableFamilyPeople,
// tableFamilyDetail, textFamilyDetailTitle, and in a people row buttonFamilyPick and textFamilyPickName; in a detail row
// textFamilyRelation, buttonFamilyRelated, textFamilyRelatedName and textFamilyInfo.
//
// Texts: Dialog/ExtensionsStringTable.rsc (the Family* keys).

// The page: the hint, the people list, the "show the dead" check box with the button that opens the tree window, the
// title of the family list, the family list, and at the bottom the button that saves the village picture with its status.
RibbonDescription pageFamilyTree
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements [ "textFamilyHint" "ribbonFamilySearch" "ribbonFamilyPeople" "ribbonFamilyShowDead" "ribbonFamilyDetailTitle" "ribbonFamilyDetail" "ribbonFamilyImage" "ribbonFamilyImageLiving" "textFamilyImageStatus" ]
}

// The note under the tab: what the page shows.
TextDescription textFamilyHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	int _topPad = 6;
	String _text = "FamilyHint";
}

RibbonDescription ribbonFamilyShowDead
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "checkFamilyShowDead" "textFamilyShowDead" "buttonFamilyShowTree" "ribbonFamilySummaryGap" ]
}

// The search line above the people list: "Find:" and the game's own edit field (as the new game dialog's name). The
// controller reads its text every frame and shows the people whose name has every word typed. The field's border has no
// color of its own; the controller gives it a light fill (toolbar_page.cpp) so the box can be seen.
RibbonDescription ribbonFamilySearch
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	int _leftPad = 4;
	ElementDescription _elements [ "textFamilySearch" "editFamilySearch" "textFamilyCount" ]
}

// A separate function from the tree button beside it, not an alternative: the gap sets it apart.
RibbonDescription ribbonFamilySummaryGap
{
	Alignment _alignment = MidLeft;
	int _leftPad = 24;
	ElementDescription _elements [ "buttonFamilyShowSummary" ]
}

// How many people the list below shows ("42 people"), set by the controller.
TextDescription textFamilyCount : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 8;
	int _minWidth = 90;
	String _text = "FamilyParam";
}

TextDescription textFamilySearch : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FamilySearch";
}

EditDescription editFamilySearch
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilySearchTip";
	ElementDescription _border = "Mod.rsc:borderLine";
	Font _font = "Font/FontSmall.rsc";
	int _maxLength = 24;
	int _minWidth = 220;
	int _minHeight = 20;
	int _leftPad = 3;
	int _rightPad = 3;
	int _topPad = 2;
	int _bottomPad = 2;
}

CheckDescription checkFamilyShowDead : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyShowDeadTip";
}

// The whole-town picture: the buttons that save it in Screenshots (a PNG, and an SVG that stays sharp at any size), the
// check box that leaves out the dead who are not ancestors, and a line that says how it went
// (src/ext/family_tree/family_picture.h, family_raster.h).
RibbonDescription ribbonFamilyImage
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "buttonFamilyImage" "buttonFamilyImageSvg" "checkFamilyImageJobs" "textFamilyImageJobs" ]
}

RibbonDescription ribbonFamilyImageLiving
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "checkFamilyImageLiving" "textFamilyImageLiving" "checkFamilyImageHighlight" "textFamilyImageHighlight" ]
}

// Saves the PNG.
ButtonDescription buttonFamilyImage : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyImageButton";
}

TextDescription textFamilyImageButton : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyImageButton";
}

// Saves the SVG.
ButtonDescription buttonFamilyImageSvg : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyImageSvgTip";
	ElementDescription _content = "textFamilyImageSvgButton";
}

TextDescription textFamilyImageSvgButton : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyImageSvgButton";
}

CheckDescription checkFamilyImageLiving : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyImageLivingTip";
}

TextDescription textFamilyImageLiving : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FamilyImageLiving";
}

CheckDescription checkFamilyImageHighlight : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyImageHighlightTip";
}

CheckDescription checkFamilyImageJobs : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyImageJobsTip";
}

TextDescription textFamilyImageJobs : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 8;
	String _text = "FamilyImageJobs";
}

TextDescription textFamilyImageHighlight : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FamilyImageHighlight";
}

TextDescription textFamilyImageStatus : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	String _text = "FamilyParam";
}

// Moves the camera to the picked person and opens their panel; shown only for a person living in the town. It sits beside
// the name in the family list's title.
ButtonDescription buttonFamilyShowInTown : "Mod.rsc:buttonMod"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "FamilyShowInTownTip";
	ElementDescription _content = "textFamilyShowInTown";
}

TextDescription textFamilyShowInTown : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyShowInTown";
}

// Opens the town summary window (src/ext/family_tree/summary_window.cpp).
ButtonDescription buttonFamilyShowSummary : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyShowSummary";
}

TextDescription textFamilyShowSummary : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilySummaryButton";
}

// Opens the family tree window (src/ext/family_tree/tree_window.cpp).
ButtonDescription buttonFamilyShowTree : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyShowTree";
}

TextDescription textFamilyShowTree : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyShowTree";
}

TextDescription textFamilyShowDead : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "FamilyShowDead";
}

// The title of the family list with the Show in town button beside the name, so the button reads as the picked person's.
RibbonDescription ribbonFamilyDetailTitle
{
	Alignment _alignment = MidLeft;
	int _cellPad = 8;
	ElementDescription _elements [ "textFamilyDetailTitle" "buttonFamilyShowInTown" ]
}

// The title of the family list: "Family of Anna", set by the controller from the table's "@0".
TextDescription textFamilyDetailTitle : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;
	int _topPad = 6;
	String _text = "FamilyParam";
}

// The people list: the Extensions dialog's frame (by reference) around the scrolling box. The window is one tab control that
// is as tall as its tallest page, and the other pages are a hint, a 350 px list and a line (RestrictedFoodsPage.rsc), so
// this page's heights (the two lists and the rows around them) must add up to no more than that: a taller list here makes the
// window taller on every tab.
RibbonDescription ribbonFamilyPeople : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFamilyPeopleBox" ]
}

ContainerDescription containerFamilyPeopleBox : "Extensions.rsc:tableList"
{
	int _height = 120;
	ElementDescription _content = "tableFamilyPeople";
}

// One column. The controller sizes it (rows = people shown) and fills it with ribbonFamilyPersonCell.
TableDescription tableFamilyPeople
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonFamilyPersonCell"; } ]
}

RibbonDescription ribbonFamilyDetail : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerFamilyDetailBox" ]
}

ContainerDescription containerFamilyDetailBox : "Extensions.rsc:tableList"
{
	int _height = 150;
	ElementDescription _content = "tableFamilyDetail";
}

// One column. The controller sizes it (rows = the picked person's relations) and fills it with ribbonFamilyRelationCell.
TableDescription tableFamilyDetail
{
	int _width = 1;
	int _height = 1;
	int _spacing = 4;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonFamilyRelationCell"; } ]
}

// One person of the people list: a button over the whole row, its text the name and what the town knows of the person.
// The border is the Mods list's row border, tinted by the controller for the picked person. No width here: the
// controller makes the cell as wide as the list's viewport (Ext::ListWidth, src/ext/ui/list_width.h).
RibbonDescription ribbonFamilyPersonCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _rightPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 4;
	ElementDescription _elements [ "buttonFamilyPick" ]
}

ButtonDescription buttonFamilyPick : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyPickName";
}

TextDescription textFamilyPickName : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyParam";
}

// One relation of the picked person: what the relation is ("Mother"), the related person as a button, and a note
// (dates, "married Year 3, month 4"). A row with no person (their own dates, an unknown parent) hides the button.
RibbonDescription ribbonFamilyRelationCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _rightPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 4;
	ElementDescription _elements [ "textFamilyRelation" "buttonFamilyRelated" "textFamilyInfo" ]
}

// The relation's name: a fixed width, so the names that follow it line up.
TextDescription textFamilyRelation : "Extensions.rsc:textProfile"
{
	int _minWidth = 110;
	String _text = "FamilyParam";
}

ButtonDescription buttonFamilyRelated : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFamilyRelatedName";
}

TextDescription textFamilyRelatedName : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyParam";
}

TextDescription textFamilyInfo : "Extensions.rsc:textProfile"
{
	String _text = "FamilyParam";
}

// ---- the drawn tree's cells (docs/extensions/handins/FAMILYTREE.md section 4a) ------------------------------------------
// The tree itself (bar, table, row) is Dialog/FamilyTreeWindow.rsc. The cell templates, found by name by tree_view.cpp:
// ribbonFamilyBox (with buttonFamilyBox, textFamilyBoxName), ribbonFamilySpaceBox, ribbonFamilySpaceLink and the line
// tiles imageFamilyLink1..15 (the arms: 1 up, 2 right, 4 down, 8 left), imageFamilyGap and imageFamilyGapStem.
// The tiles take their sprite's own size (26 x 14, gap tiles 26 x 40; tools/ext_tree_tiles.py), kTileW in toolbar_page.cpp.

// One person: a button with the name over a thin border. The controller tints the border (the picked person, the dead).
RibbonDescription ribbonFamilyBox
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _minWidth = 78;
	int _minHeight = 40;
	ElementDescription _elements [ "buttonFamilyBox" ]
}

ButtonDescription buttonFamilyBox : "Mod.rsc:buttonMod"
{
	// 3 px of the 78 x 40 cell stay clear all round: the ribbon's border shows there, tinted for the picked person
	int _leftPad = 3;
	int _rightPad = 3;
	int _topPad = 3;
	int _bottomPad = 3;
	int _minWidth = 72;
	int _minHeight = 34;
	ElementDescription _content = "textFamilyBoxName";
}

TextDescription textFamilyBoxName : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyParam";
	int _leftPad = 2;
	int _rightPad = 2;
	int _topPad = 2;
	int _bottomPad = 2;
	Alignment _alignment = MidCenter;
	int _wordWrapSize = 70;
}

// Empty space in a row of boxes / of line tiles: the controller sets the width.
RibbonDescription ribbonFamilySpaceBox
{
	int _minWidth = 26;
	int _minHeight = 40;
}

RibbonDescription ribbonFamilySpaceLink
{
	int _minWidth = 26;
	int _minHeight = 14;
}

ImageDescription imageFamilyLink1 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink1"; }
ImageDescription imageFamilyLink2 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink2"; }
ImageDescription imageFamilyLink3 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink3"; }
ImageDescription imageFamilyLink4 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink4"; }
ImageDescription imageFamilyLink5 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink5"; }
ImageDescription imageFamilyLink6 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink6"; }
ImageDescription imageFamilyLink7 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink7"; }
ImageDescription imageFamilyLink8 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink8"; }
ImageDescription imageFamilyLink9 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink9"; }
ImageDescription imageFamilyLink10 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink10"; }
ImageDescription imageFamilyLink11 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink11"; }
ImageDescription imageFamilyLink12 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink12"; }
ImageDescription imageFamilyLink13 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink13"; }
ImageDescription imageFamilyLink14 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink14"; }
ImageDescription imageFamilyLink15 { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeLink15"; }
ImageDescription imageFamilyGap { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeGap"; }
ImageDescription imageFamilyGapStem { SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "TreeGapStem"; }

// The "Family" button the citizen panel gets (citizen_button.cpp): a Mod.rsc button (SharedElements.rsc:borderButton does not load from our text), small font. The panel's
// code sets the right pad so it sits left of Follow.
ButtonDescription buttonFamilyCitizen : "Mod.rsc:buttonMod"
{
	int _topPad = 4;
	int _rightPad = 60;
	Alignment _alignment = TopRight;
	ElementDescription _content = "textFamilyCitizen";
}

TextDescription textFamilyCitizen
{
	int _leftPad = 12;
	int _rightPad = 12;
	int _topPad = 6;
	int _bottomPad = 6;
	Font _font = "Font/FontSmall.rsc";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FamilyCitizenButton";
}
