// Dialog/NewGameMap.rsc -- the New Game dialog's "Custom map" row (NOT RETAIL; authored for this port, src/ext;
// docs/extensions/handins/CUSTOMMAPS.md). Not a dialog of its own: the elements of one row that
// src/ext/custom_maps/new_game_map.cpp builds and adds to whichever Dialog/NewGame.rsc is in use, vanilla or a mod's (MegaMod
// rebuilds the dialog's table and adds hint rows), so every other row, size and note of the dialog stays as that dialog has it.
// Every name here is NEW; the look comes from the game's own NewGame.rsc / SharedElements.rsc objects, taken by reference.
//
// The row goes into the dialog's own table (tableNew) as two more rows, so it lines up with the others whatever the dialog's column widths are
// (vanilla's, MegaMod's). The first row is textCustomMap in the first column and cellCustomMap (the two buttons) in the second: a label and its
// control on one row, so the label is as high as the buttons the way every other row's label is as high as its control. The second row is an
// empty cell (cellCustomMapBlank) and cellCustomMapName (the picked map's name and the "Needs: ..." mod line, which wrap, so a long name or mod
// list never makes the dialog wider), on the line below the buttons. A dialog without that table gets rowCustomMap instead,
// appended under the description box: the label, the buttons and the name in one block. Names the controller looks up: buttonLoadMap,
// buttonClearMap, textMapName, textMapMods.

// The second column's cell of the first row: the two buttons.
RibbonDescription cellCustomMap
{
	bool _vertical = false;
	int _cellPad = 4;

	ElementDescription _elements
	[
		"buttonLoadMap"
		"buttonClearMap"
	]
}

// The second row's first cell: nothing (a table cell is never left without an element).
RibbonDescription cellCustomMapBlank
{
	bool _vertical = true;
}

// The second row's second cell: the picked map's name, then the mods it needs, under the buttons.
RibbonDescription cellCustomMapName
{
	bool _vertical = true;
	int _cellPad = 4;

	ElementDescription _elements
	[
		"textMapName"
		"textMapMods"
	]
}

RibbonDescription rowCustomMap
{
	bool _vertical = true;
	int _cellPad = 4;
	int _leftPad = 8;
	int _rightPad = 8;
	int _topPad = 4;
	int _bottomPad = 8;

	ElementDescription _elements
	[
		"rowButtons"
		"textMapName"
		"textMapMods"
	]
}

RibbonDescription rowButtons
{
	bool _vertical = false;
	int _cellPad = 4;

	ElementDescription _elements
	[
		"textCustomMapRow"
		"buttonLoadMap"
		"buttonClearMap"
	]
}

StringTable customMapStrings
{
	Entry _strings
	[
		{ String _name = "CustomMap";		String _text = "Custom map";		}
		{ String _name = "LoadMap";			String _text = "Load custom map";	}
		{ String _name = "ClearMap";		String _text = "Clear";				}
		{ String _name = "MapName";			String _text = "@0";				}
	]
}

// The row's label: the dialog's own row label (NewGame.rsc:textName), which the table sizes to its first column.
TextDescription textCustomMap : "NewGame.rsc:textName"
{
	StringTable _stringTable = "NewGameMap.rsc:customMapStrings";
	String _text = "CustomMap";
}

// The same label in the fallback block, which has no column to size it: narrower than the table's.
TextDescription textCustomMapRow : "textCustomMap"
{
	int _minWidth = 100;
}

// The button: SharedElements.rsc's Apply button with our caption (the Options dialog's own buttons are that object).
ButtonDescription buttonLoadMap : "SharedElements.rsc:buttonApply"
{
	ElementDescription _content = "textLoadMap";
}

TextDescription textLoadMap : "SharedElements.rsc:buttonApplyText"
{
	StringTable _stringTable = "NewGameMap.rsc:customMapStrings";
	String _text = "LoadMap";
}

ButtonDescription buttonClearMap : "SharedElements.rsc:buttonApply"
{
	ElementDescription _content = "textClearMap";
}

TextDescription textClearMap : "SharedElements.rsc:buttonApplyText"
{
	StringTable _stringTable = "NewGameMap.rsc:customMapStrings";
	String _text = "ClearMap";
}

// The picked map's name, or "None", and the "Needs: ..." line; the controller fills parameter 0. It wraps at 255, inside the second column
// (the dialog's name row there is about 280 wide), so it never widens the dialog.
TextDescription textMapName : "NewGame.rsc:textName"
{
	StringTable _stringTable = "NewGameMap.rsc:customMapStrings";
	String _text = "MapName";
	int _minWidth = 0;
	bool _wordWrap = true;
	int _wordWrapSize = 255;
}

// The mods the picked map needs ("Needs: MegaMod 10"), on a line of its own under the name; hidden when it needs none. The controller
// fills parameter 0. Same look and wrap as the name, so a long list still stays inside the column.
TextDescription textMapMods : "textMapName"
{
}
