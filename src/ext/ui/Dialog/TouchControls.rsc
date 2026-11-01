// Dialog/TouchControls.rsc -- ext.touch_mode's camera controls (NOT RETAIL; authored for this port, src/ext).
//
// docs/android/TOUCH_MODE.md section 3. The cluster at the bottom left of the game view: a D-pad (camera pan), Rotate
// left / right above it, Zoom in / out to its right. Every look is the game's own, read from the player's install: the
// buttons are the toolbar's own (Toolbar.rsc:buttonToolbar, SharedElements.rsc's raised button), the glyphs are sprites of the game's SpriteSheet.rsc
// (ArrowUp / Down / Left / Right, Plus, Minus, ToolCamera), the tooltips the Options Input tab's key names
// (StringTable.rsc:gameKeys). Nothing here is drawn by us.
//
// Controller: src/ext/touch_mode/touch_mode.cpp. It sizes and places every holder (its left / top padding) and button (its
// minimum size) from touch_math.h LayoutCluster each time the screen or the UI scale changes, and scales the glyphs. The
// holders are groups whose padding puts their button at its place inside the root group (groups overlap their children,
// as TutorialHighlight.rsc's do). Input is not read from these elements: the controller hit-tests the pointer against
// the same rectangles (a held control acts every frame; the buttons' own events come only on release).
//
// Issue #96 (the reporter's follow-up): a second dialog, `cameraButtons` (Dialog/TouchControls.rsc:cameraButtons),
// the corner camera buttons of ext.touch_mode_camera_buttons. It reuses the cluster's rotate and zoom holders and
// buttons as they are (each dialog builds its own elements from the descriptions); only its root and its content
// group are its own. The controller lays it out from touch_math.h LayoutCornerButtons: two rows of two in the
// upper right.

Dialog resource		// the dialog object: Dialog/TouchControls.rsc
{
	// No flags: not Modal, not PauseGame, not FirstDraw (a FirstDraw dialog hides everything drawn before it).
	ElementDescription _rootElement = "touchRoot";
}

// One frame around the whole group, the toolbar's own (Toolbar.rsc:toolBar: SharedElements.rsc's dialogBorder and
// sunkenBorderPad6 around the content); the controller pads the content by the cluster's frame inset.
GroupDescription touchRoot
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"SharedElements.rsc:dialogBorder"
		"SharedElements.rsc:sunkenBorderPad6"
		"groupTouchContent"
	]
}

GroupDescription groupTouchContent
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"holderTouchPad" "holderTouchRotateLeft" "holderTouchRotateRight" "holderTouchZoomIn" "holderTouchZoomOut"
	]
}

GroupDescription holderTouchPad { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonTouchPad" ] }
GroupDescription holderTouchRotateLeft { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonTouchRotateLeft" ] }
GroupDescription holderTouchRotateRight { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonTouchRotateRight" ] }
GroupDescription holderTouchZoomIn { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonTouchZoomIn" ] }
GroupDescription holderTouchZoomOut { Alignment _alignment = TopLeft; ElementDescription _elements [ "buttonTouchZoomOut" ] }

// The D-pad: one raised button, the four arrows at its edges. Its tooltip is switched off by the controller (a finger
// parked on the pad after a pan would open it); the text is only there because the base has one.
ButtonDescription buttonTouchPad : "Toolbar.rsc:buttonToolbar"
{
	Alignment _alignment = TopLeft;	// inside its holder: the holder's padding is the place
	ElementDescription _content = "groupTouchPadArrows";
	Dialog _toolTipDialog = "ToolTip.rsc";
	StringTable _toolTipStringTable = "StringTable.rsc:gameKeys";
	String _toolTipText = "Camera_Forward";
}

GroupDescription groupTouchPadArrows
{
	Alignment _alignment = MidCenter;
	ElementDescription _elements [ "imageTouchUp" "imageTouchDown" "imageTouchLeft" "imageTouchRight" ]
}

ImageDescription imageTouchArrow
{
	SpriteSheet _spriteSheet = "SpriteSheet.rsc";
	String _spriteName = "ArrowUp";
}

ImageDescription imageTouchUp : "imageTouchArrow" { Alignment _alignment = TopCenter; String _spriteName = "ArrowUp"; }
ImageDescription imageTouchDown : "imageTouchArrow" { Alignment _alignment = BottomCenter; String _spriteName = "ArrowDown"; }
ImageDescription imageTouchLeft : "imageTouchArrow" { Alignment _alignment = MidLeft; String _spriteName = "ArrowLeft"; }
ImageDescription imageTouchRight : "imageTouchArrow" { Alignment _alignment = MidRight; String _spriteName = "ArrowRight"; }

// Rotate: the camera icon of the toolbar's Camera tool with an arrow on the side it turns to.
ButtonDescription buttonTouchRotateLeft : "Toolbar.rsc:buttonToolbar"
{
	Alignment _alignment = TopLeft;	// inside its holder: the holder's padding is the place
	ElementDescription _content = "ribbonTouchRotateLeft";
	Dialog _toolTipDialog = "ToolTip.rsc";
	StringTable _toolTipStringTable = "StringTable.rsc:gameKeys";
	String _toolTipText = "Camera_RotateLeft";
}

ButtonDescription buttonTouchRotateRight : "buttonTouchRotateLeft"
{
	ElementDescription _content = "ribbonTouchRotateRight";
	String _toolTipText = "Camera_RotateRight";
}

RibbonDescription ribbonTouchRotateLeft
{
	Alignment _alignment = MidCenter; bool _vertical = false; int _cellPad = 2;
	ElementDescription _elements [ "imageTouchRotateLeftArrow" "imageTouchCameraLeft" ]
}

RibbonDescription ribbonTouchRotateRight
{
	Alignment _alignment = MidCenter; bool _vertical = false; int _cellPad = 2;
	ElementDescription _elements [ "imageTouchCameraRight" "imageTouchRotateRightArrow" ]
}

ImageDescription imageTouchRotateLeftArrow : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "ArrowLeft"; }
ImageDescription imageTouchRotateRightArrow : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "ArrowRight"; }
ImageDescription imageTouchCameraLeft : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "ToolCamera"; }
ImageDescription imageTouchCameraRight : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "ToolCamera"; }

// Zoom: the game's Plus and Minus.
ButtonDescription buttonTouchZoomIn : "Toolbar.rsc:buttonToolbar"
{
	Alignment _alignment = TopLeft;	// inside its holder: the holder's padding is the place
	ElementDescription _content = "imageTouchZoomIn";
	Dialog _toolTipDialog = "ToolTip.rsc";
	StringTable _toolTipStringTable = "StringTable.rsc:gameKeys";
	String _toolTipText = "Camera_ZoomIn";
}

ButtonDescription buttonTouchZoomOut : "buttonTouchZoomIn"
{
	ElementDescription _content = "imageTouchZoomOut";
	String _toolTipText = "Camera_ZoomOut";
}

ImageDescription imageTouchZoomIn : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "Plus"; }
ImageDescription imageTouchZoomOut : "imageTouchArrow" { Alignment _alignment = MidCenter; String _spriteName = "Minus"; }

// ---- the corner camera buttons (issue #96) ------------------------------------------------------------------------

Dialog cameraButtons
{
	// No flags, as the cluster's.
	ElementDescription _rootElement = "cameraButtonsRoot";
}

// The cluster's frame (touchRoot) around the four buttons; the controller pads the content by the frame inset.
GroupDescription cameraButtonsRoot
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"SharedElements.rsc:dialogBorder"
		"SharedElements.rsc:sunkenBorderPad6"
		"groupCameraButtonsContent"
	]
}

GroupDescription groupCameraButtonsContent
{
	Alignment _alignment = TopLeft;
	ElementDescription _elements
	[
		"holderTouchRotateLeft" "holderTouchRotateRight" "holderTouchZoomIn" "holderTouchZoomOut"
	]
}
