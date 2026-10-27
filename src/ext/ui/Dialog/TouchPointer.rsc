// Dialog/TouchPointer.rsc -- ext.touch_pointer's action bar and aim marker (NOT RETAIL; authored for this port, src/ext).
//
// docs/android/P11_TOUCH_POINTER.md. The bar sits on the right above the toolbar: Place / Confirm, Rotate structure
// left / right, Change structure model, Cancel. Every look is the game's own, read from the player's install: the
// buttons are the toolbar's (Toolbar.rsc:buttonToolbar), the frame the toolbar's (SharedElements.rsc), the glyphs are
// sprites of the game's SpriteSheet.rsc (GoalSuccess, ToolbarHousing, ArrowLeftSmall / RightSmall, ToolClose,
// ArrowDown), the tooltips the Options
// Input tab's key names (StringTable.rsc:gameKeys). The rotate arrows are ext.terrain_edit's Undo / Redo glyphs
// (ExtensionsSprites.rsc: ExtensionsGlyph_terrain_undo / _redo, our own sprites; the owner's ruling of 2026-10-26).
//
// Controller: src/ext/touch_pointer/touch_pointer.cpp. It sizes and places every holder and button from
// touch_pointer_math.h LayoutBar and hides the buttons the current tool has no use for. Input is not read from these
// elements: the controller hit-tests the finger against the same rectangles, and the game's cursor never goes over
// the bar while a finger drives it.

Dialog resource		// the dialog object: Dialog/TouchPointer.rsc
{
	ElementDescription _rootElement = "pointerBarRoot";
}

GroupDescription pointerBarRoot
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"SharedElements.rsc:dialogBorder"
		"SharedElements.rsc:sunkenBorderPad6"
		"groupPointerContent"
	]
}

GroupDescription groupPointerContent
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"holderPointerPlace" "holderPointerRotateLeft" "holderPointerRotateRight" "holderPointerFlip" "holderPointerCancel"
	]
}

GroupDescription holderPointerPlace { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonPointerPlace" ] }
GroupDescription holderPointerRotateLeft { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonPointerRotateLeft" ] }
GroupDescription holderPointerRotateRight { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonPointerRotateRight" ] }
GroupDescription holderPointerFlip { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonPointerFlip" ] }
GroupDescription holderPointerCancel { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonPointerCancel" ] }

ImageDescription imagePointerGlyph
{
	SpriteSheet _spriteSheet = "SpriteSheet.rsc";
	String _spriteName = "GoalSuccess";
}

// Place / Confirm: the primary action at the parked cursor.
ButtonDescription buttonPointerPlace : "Toolbar.rsc:buttonToolbar"
{
	Alignment _alignment = TopLeft;	// inside its holder: the holder's padding is the place
	ElementDescription _content = "imagePointerPlace";
	Dialog _toolTipDialog = "ToolTip.rsc";
	StringTable _toolTipStringTable = "StringTable.rsc:gameKeys";
	String _toolTipText = "Mouse_Left";
}

ImageDescription imagePointerPlace : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "GoalSuccess"; }

ButtonDescription buttonPointerRotateLeft : "buttonPointerPlace"
{
	ElementDescription _content = "ribbonPointerRotateLeft";
	String _toolTipText = "Structure_Left";
}

ButtonDescription buttonPointerRotateRight : "buttonPointerPlace"
{
	ElementDescription _content = "ribbonPointerRotateRight";
	String _toolTipText = "Structure_Right";
}

RibbonDescription ribbonPointerRotateLeft
{
	Alignment _alignment = MidCenter; bool _vertical = false; int _cellPad = 2;
	ElementDescription _elements [ "imagePointerRotateLeftArrow" "imagePointerHouseLeft" ]
}

RibbonDescription ribbonPointerRotateRight
{
	Alignment _alignment = MidCenter; bool _vertical = false; int _cellPad = 2;
	ElementDescription _elements [ "imagePointerHouseRight" "imagePointerRotateRightArrow" ]
}

// Rotate left: the undo arrow (a counter-clockwise turn) before the house; rotate right: the house before the redo
// arrow. touch_pointer.cpp sizes each pair to fit the button with a margin (the house overlapped the edge in P11).
ImageDescription imagePointerRotateLeftArrow : "imagePointerGlyph"
{
	Alignment _alignment = MidCenter; SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_undo";
}
ImageDescription imagePointerRotateRightArrow : "imagePointerGlyph"
{
	Alignment _alignment = MidCenter; SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_redo";
}
ImageDescription imagePointerHouseLeft : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "ToolbarHousing"; }
ImageDescription imagePointerHouseRight : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "ToolbarHousing"; }

// Change structure model (retail's flip key).
ButtonDescription buttonPointerFlip : "buttonPointerPlace"
{
	ElementDescription _content = "ribbonPointerFlip";
	String _toolTipText = "Structure_Type";
}

RibbonDescription ribbonPointerFlip
{
	Alignment _alignment = MidCenter; bool _vertical = false; int _cellPad = 2;
	ElementDescription _elements [ "imagePointerFlipLeft" "imagePointerFlipRight" ]
}

// Change structure model: two small arrows (P11's look, kept by the owner's ruling of 2026-10-26: the two rotate
// arrows here read as "rotate both ways").
ImageDescription imagePointerFlipLeft : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "ArrowLeftSmall"; }
ImageDescription imagePointerFlipRight : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "ArrowRightSmall"; }

// Cancel (retail's right click).
ButtonDescription buttonPointerCancel : "buttonPointerPlace"
{
	ElementDescription _content = "imagePointerCancel";
	String _toolTipText = "Mouse_Right";
}

ImageDescription imagePointerCancel : "imagePointerGlyph" { Alignment _alignment = MidCenter; String _spriteName = "ToolClose"; }

// The aim marker: the game's down arrow, its tip on the cursor while a finger aims with the lift offset. Not hit-tested
// (the controller marks every element of it so), so the cursor under it stays on the world.
Dialog marker
{
	ElementDescription _rootElement = "pointerMarkerRoot";
}

GroupDescription pointerMarkerRoot
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements [ "imagePointerMarker" ]
}

ImageDescription imagePointerMarker : "imagePointerGlyph" { Alignment _alignment = TopLeft; String _spriteName = "ArrowDown"; }
