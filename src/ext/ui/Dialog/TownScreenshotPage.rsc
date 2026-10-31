// Dialog/TownScreenshotPage.rsc -- ext.town_screenshot's page in the extension window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/TOWNSCREENSHOT.md, src/ext/town_screenshot/toolbar_page.cpp. One picture of the whole map from an
// isometric view: three drop downs (the quality, the angle and the turn), a button that takes the screenshot, one that
// stops it, and a line that says what is happening or how the last screenshot ended. The window around the page is
// Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the page.
//
// Controller: it looks up, by name: comboTownScreenshotQuality, comboTownScreenshotAngle, comboTownScreenshotTurn, buttonTownScreenshotTake,
// checkTownScreenshotWeather, checkTownScreenshotSnow, buttonTownScreenshotCancel and textTownScreenshotStatus. Texts: Dialog/ExtensionsStringTable.rsc (the TownScreenshot* keys).

// The page, in two sections: the town screenshot (the choices and its two buttons) and the view screenshot (what is on
// the screen now, a function of its own that sits beside the first, as the Terrain page's resource tool sits under its
// brushes), then the status line; 4 px between the rows.
RibbonDescription pageTownScreenshot
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 4;
	ElementDescription _elements
	[
		"textTownScreenshotHeading" "textTownScreenshotHint" "textTownScreenshotHint2"
		"rowTownScreenshotQuality" "rowTownScreenshotAngle" "rowTownScreenshotTurn" "rowTownScreenshotWeather" "rowTownScreenshotSnow"
		"rowTownScreenshotButtons"
		"textTownScreenshotHeadingView" "textTownScreenshotHint3" "buttonTownScreenshotView"
		"textTownScreenshotStatus"
	]
}

TextDescription textTownScreenshotHeading : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 2; String _text = "TownScreenshotHeading";
}

// The second section's heading: as much room above as the Terrain page's second heading.
TextDescription textTownScreenshotHeadingView : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 10; String _text = "TownScreenshotHeadingView";
}

// The note is short lines (a long one does not wrap and would make the window as wide as the line).
TextDescription textTownScreenshotHint : "Extensions.rsc:textProfile"
{
	String _text = "TownScreenshotHint";
}

TextDescription textTownScreenshotHint2 : "Extensions.rsc:textProfile"
{
	String _text = "TownScreenshotHint2";
}

TextDescription textTownScreenshotHint3 : "Extensions.rsc:textProfile"
{
	String _text = "TownScreenshotHint3";
}

// A label and its drop down: the labels are one width so the drop downs line up.
TextDescription textTownScreenshotQualityLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "TownScreenshotQuality";
}

TextDescription textTownScreenshotAngleLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "TownScreenshotAngle";
}

TextDescription textTownScreenshotTurnLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "TownScreenshotTurn";
}

ComboDescription comboTownScreenshotQuality : "Options.rsc:comboAutosave"
{
	int _minWidth = 220;
}

ComboDescription comboTownScreenshotAngle : "Options.rsc:comboAutosave"
{
	int _minWidth = 220;
}

ComboDescription comboTownScreenshotTurn : "Options.rsc:comboAutosave"
{
	int _minWidth = 220;
}

RibbonDescription rowTownScreenshotQuality
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTownScreenshotQualityLabel" "comboTownScreenshotQuality" ]
}

RibbonDescription rowTownScreenshotAngle
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTownScreenshotAngleLabel" "comboTownScreenshotAngle" ]
}

RibbonDescription rowTownScreenshotTurn
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTownScreenshotTurnLabel" "comboTownScreenshotTurn" ]
}

// The two check boxes: "Ignore Weather" (checked, the screenshot is drawn as on a clear day: no rain or snowfall and
// none of their lighting) and "Hide Snow on the Ground" (checked, the ground is bare even where snow lies).
RibbonDescription rowTownScreenshotWeather
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "checkTownScreenshotWeather" "textTownScreenshotWeather" ]
}

RibbonDescription rowTownScreenshotSnow
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "checkTownScreenshotSnow" "textTownScreenshotSnow" ]
}

CheckDescription checkTownScreenshotSnow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "TownScreenshotSnowTip";
}

TextDescription textTownScreenshotSnow : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "TownScreenshotSnow";
}

CheckDescription checkTownScreenshotWeather : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "TownScreenshotWeatherTip";
}

TextDescription textTownScreenshotWeather : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "TownScreenshotWeather";
}

// The two buttons: the picture button and the small text button of the Mods list and the tab strip.
RibbonDescription rowTownScreenshotButtons
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "buttonTownScreenshotTake" "buttonTownScreenshotCancel" ]
}

// Take Town Screenshot is a picture button (the game's own toolbar button with a camera glyph, as the Terrain page's
// tools are); its name is the tool tip. Cancel is a small text button.
ImageDescription imageTownScreenshotTake : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_town_screenshot_take";
}

ButtonDescription buttonTownScreenshotTake : "ToolBar.rsc:buttonToolbar"
{
	ElementDescription _content = "imageTownScreenshotTake";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TownScreenshotTake";
}

// The second picture button: a screenshot of what is on the screen now, the game's own (the same file as its
// screenshot key makes).
ImageDescription imageTownScreenshotView : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_town_screenshot_view";
}

ButtonDescription buttonTownScreenshotView : "ToolBar.rsc:buttonToolbar"
{
	ElementDescription _content = "imageTownScreenshotView";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TownScreenshotView";
}

ButtonDescription buttonTownScreenshotCancel : "Mod.rsc:buttonMod" { ElementDescription _content = "textTownScreenshotCancel"; }

TextDescription textTownScreenshotCancel : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "TownScreenshotCancel";
}

// What is happening, or how the last screenshot ended (the controller sets the parameter).
TextDescription textTownScreenshotStatus : "Extensions.rsc:textProfile"
{
	int _topPad = 6; String _text = "TownScreenshotStatus";
}
