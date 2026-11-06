// Dialog/FullscreenMode.rsc -- ext.fullscreen_mode's Video tab cell (NOT RETAIL;
// src/ext/fullscreen_mode/fullscreen_mode.h).
//
// The group takes the cell of retail's Fullscreen checkbox (Options.rsc:graphicsTable (1, 3)) at run time:
// child 0 is the combo Off / Exclusive / Borderless, child 1 an empty slot the checkbox is moved into
// (hidden, so retail's controller keeps its pointer). A group is sized to its visible children, so the
// cell is the combo's size. The combo inherits the Video tab's own combo look (Options.rsc:comboGraphics),
// so it lines up with Resolution and Refresh above it. Its base is comboRefresh, not comboGraphics: the
// game's packages hold only the objects something references by name, and comboGraphics is only a base
// (comboRefresh is comboGraphics with nothing added).

StringTable fullscreenModeItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";			}
		{ String _name = "Item1";		String _text = "Exclusive";		}
		{ String _name = "Item2";		String _text = "Borderless";	}
	]
}

GroupDescription fullscreenModeCell
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements [ "comboFullscreenMode" "fullscreenModeSlot" ]
}

ComboDescription comboFullscreenMode : "Options.rsc:comboRefresh"
{
	StringTable _initialData = "FullscreenMode.rsc:fullscreenModeItems";
}

GroupDescription fullscreenModeSlot
{
	ElementDescription _elements [ ]
}
