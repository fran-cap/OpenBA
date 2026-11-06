// Dialog/ProfessionsWindow.rsc -- ext.dynamic_professions' window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/DYNAMICPROFESSIONS.md. A utility-style window like Dialog/FavoritesWindow.rsc (the
// game's own title bar, no flags: not modal, it does not pause the game) that stands in for retail's Professions
// window when the extension is on. Its rows are retail's own (Dialog/Profession.rsc:professionRow: the name and
// icon, the spinner, "of", the worker slots, the goto arrow), so they look and behave as the player knows
// them; the controller makes one row for each profession the town can use.
//
// Controller: src/ext/dynamic_professions/professions_window.cpp. It looks up, by name: labelTitle,
// buttonClose, borderTitle (the title bar's) and tableProfessions, whose one template item is the row.

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
	ElementDescription _elements [ "titleBarProfessions" "ribbonProfessions" "borderProfessionsGrip" ]
}

// The game's title bar (StandardDialog.rsc:titleBar) with one more button before the close X: it switches the list
// between two columns and one (made from the close X's own button and image: the compiled game keeps only the blocks
// something uses, and titleButton / titleImage are not among them). Two buttons, one shown at a time: the one that is shown is the layout it switches to.
GroupDescription titleBarProfessions
{
	ElementDescription _elements
	[
		"StandardDialog.rsc:borderTitle"
		"StandardDialog.rsc:labelTitleGroup"
		"userTitleProfessions"
	]
}

RibbonDescription userTitleProfessions : "StandardDialog.rsc:userTitle"
{
	ElementDescription _elements [ "buttonProfessionsOneColumn" "buttonProfessionsTwoColumns" "StandardDialog.rsc:buttonClose" ]
}

ButtonDescription buttonProfessionsOneColumn : "StandardDialog.rsc:buttonClose"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ProfessionsOneColumnTip";
	ElementDescription _content = "imageProfessionsOneColumn";
}

ButtonDescription buttonProfessionsTwoColumns : "StandardDialog.rsc:buttonClose"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ProfessionsTwoColumnsTip";
	ElementDescription _content = "imageProfessionsTwoColumns";
}

ImageDescription imageProfessionsOneColumn : "StandardDialog.rsc:imageClose"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_professions_one_column";
}

ImageDescription imageProfessionsTwoColumns : "StandardDialog.rsc:imageClose"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_professions_two_columns";
}

// The strip under the list: press and drag it up or down to change the most the list may grow to (the game has no
// resize cursor: it shows the title bar's move cursor). Programmer art: a row of dots.
TextDescription borderProfessionsGrip : "StandardDialog.rsc:labelTitle"
{
	Alignment _alignment = MidCenter;
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ProfessionsGrip";
	int _topPad = 0;
	int _bottomPad = 0;
}

RibbonDescription ribbonProfessions : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerProfessionsBox" ]
}

// The rows scroll in a box of a fixed size: the list is as long as the town needs, up to a whole mod's worth.
ContainerDescription containerProfessionsBox : "Extensions.rsc:tableList"
{
	int _width = 440;
	int _height = 300;
	// as tall as the rows need, up to the height above; the width stays what two full columns need
	bool _allowShrinkWidth = false;
	bool _allowShrinkHeight = true;
	ElementDescription _content = "tableProfessions";
}

// Two columns, as retail's. The controller sizes it and clones the row into it.
TableDescription tableProfessions
{
	int _width = 2;
	int _height = 1;
	int _spacing = 2;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "Profession.rsc:professionRow"; } ]
}
