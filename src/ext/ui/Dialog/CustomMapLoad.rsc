// Dialog/CustomMapLoad.rsc -- the Custom map picker (NOT RETAIL; authored for this port, src/ext;
// docs/extensions/handins/CUSTOMMAPS.md). The game's Load screen (Dialog/SaveLoad.rsc) for the .bmap files of
// Documents\Banished\Maps (else Documents\Banished): the same frame, the same scrolling grid of thumbnails and the same row, one thumbnail with its
// name and date under it, but a title of its own and only Cancel and Load (no Save, no Delete: the game never deletes a map) plus Browse, the system's file picker.
//
// Everything here derives from or points at the game's own SaveLoad.rsc objects (the list, the scroll bar, the row template
// `icon`, the Load button), so a change to the game's screen reaches this one. The controller is
// src/ext/custom_maps/map_picker.cpp. It looks up, by name: buttonLoad, buttonBrowse, buttonCancel, tableSaveList, and in each row (template
// 0 = SaveLoad.rsc:icon) labelSaveName, labelSaveDate, imageSaveIcon, borderSave, imageCorrupt, selectSave -- the names the
// game's own Load screen looks up -- and labelMapAuthor, labelMapMods.
//
// The row is ours too (mapRow): the Load screen's two text lines are four here (name, author, size and date, mods).
// Unlike SaveLoad.rsc there is no busy page (the list is read before the dialog opens), so the frame holds the list alone.

Dialog resource		// Dialog/CustomMapLoad.rsc
{
	Flags _flags = PauseGame | Modal | FirstDraw;
	ElementDescription _rootElement = "main";

	// GetTemplate(0): one per map, and the blank rows that fill the grid.
	ElementDescription _templates [ "mapRow" ]

	External _references
	[
	]
}

GroupDescription main
{
	Alignment _alignment = MidCenter;
	ElementDescription _elements
	[
		"SharedElements.rsc:dialogBorder"
		"content"
	]
}

// SaveLoad.rsc:content with the title and the two buttons we keep.
LayoutDescription content : "SaveLoad.rsc:content"
{
	LayoutItem _elements
	[
		{
			String _name = "root";
			ElementDescription _element = "mainGroup";
			LayoutHorizontal _xLayout = LeftToLeft;
			LayoutVertical _yLayout = TopToTop;
		}
		{
			ElementDescription _element = "labelTitle";
			LayoutHorizontal _xLayout = LeftToLeft; String _xAnchorTo = "root";
			LayoutVertical _yLayout = BottomToTop; String _yAnchorTo = "root"; int _yOffset = -8;
		}
		{
			String _name = "load";
			ElementDescription _element = "SaveLoad.rsc:buttonLoad";
			LayoutHorizontal _xLayout = RightToRight; String _xAnchorTo = "root";
			LayoutVertical _yLayout = TopToBottom; String _yAnchorTo = "root"; int _yOffset = 4;
		}
		{
			String _name = "browse";
			ElementDescription _element = "buttonBrowse";
			LayoutHorizontal _xLayout = LeftToLeft; String _xAnchorTo = "root";
			LayoutVertical _yLayout = TopToBottom; String _yAnchorTo = "root"; int _yOffset = 4;
		}
		{
			String _name = "cancel";
			ElementDescription _element = "SharedElements.rsc:buttonCancel";
			LayoutHorizontal _xLayout = RightToLeft; String _xAnchorTo = "load"; int _xOffset = -4;
			LayoutVertical _yLayout = TopToBottom; String _yAnchorTo = "root"; int _yOffset = 4;
		}
	]
}

// SaveLoad.rsc:mainGroup (its width, its two borders) holding the list alone, taller than the Load screen's (360) so two rows of
// the taller row below show whole: 420 for the list is two rows of about 205 and the pad.
GroupDescription mainGroup : "SaveLoad.rsc:mainGroup"
{
	int _minHeight = 436;

	ElementDescription _elements
	[
		"SharedElements.rsc:sunkenBorder"
		"SharedElements.rsc:raisedBorderPad"
		"groupSelect"
	]
}

// SaveLoad.rsc:groupSelect (the scrolling list and its scroll bar) with a taller view.
ContainerDescription groupSelect : "SaveLoad.rsc:groupSelect"
{
	int _height = 420;
}

// The Load screen's row (SaveLoad.rsc:icon: the thumbnail group, then small centered text lines) with two more lines: the map's
// name, its author, "512 x 512, 11/13/2026" and the mods it needs. labelSaveName and labelSaveDate are the game's own, so the controller
// fills them as the Load screen's does; labelMapAuthor and labelMapMods are new.
RibbonDescription mapRow : "SaveLoad.rsc:icon"
{
	ElementDescription _elements
	[
		"SaveLoad.rsc:iconGroup"
		"SaveLoad.rsc:labelSaveName"
		"labelMapAuthor"
		"SaveLoad.rsc:labelSaveDate"
		"labelMapMods"
	]
}

TextDescription labelMapAuthor : "SaveLoad.rsc:labelSaveName"
{
}

TextDescription labelMapMods : "SaveLoad.rsc:labelSaveName"
{
}

StringTable customMapLoadStrings
{
	Entry _strings
	[
		{ String _name = "Title";	String _text = "Custom map";	}
		{ String _name = "Browse";	String _text = "Browse...";		}
	]
}

// The Load screen's title (its icon and spacing) with our words.
LabelDescription labelTitle : "SaveLoad.rsc:labelLoad"
{
	StringTable _stringTable = "CustomMapLoad.rsc:customMapLoadStrings";
	String _text = "Title";
}

// Where the Load screen's Delete button stands: Browse, the system's file picker for a map kept somewhere else. Based on the Apply
// button the New Game row's Load custom map button uses (SaveLoad.rsc's own borderButton is not in the game's packages, which hold only
// reached objects: the first version of this file named it and the picker never opened).
ButtonDescription buttonBrowse : "SharedElements.rsc:buttonApply"
{
	ElementDescription _content = "textBrowse";
}

TextDescription textBrowse : "SharedElements.rsc:buttonApplyText"
{
	StringTable _stringTable = "CustomMapLoad.rsc:customMapLoadStrings";
	String _text = "Browse";
}
