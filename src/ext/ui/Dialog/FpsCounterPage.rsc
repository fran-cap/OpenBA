// Dialog/FpsCounterPage.rsc -- ext.fps_counter's page in the extension window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/FPSCOUNTER.md, src/ext/fps_counter/toolbar_page.cpp. The frame rate the game draws at: the
// current, average, minimum and maximum (frames a second, each over windows of one second), how long that covers, and a
// Reset button. The window around the page is Dialog/ExtensionsWindow.rsc (one tab per active extension page); this
// file is only the page.
//
// Controller: it looks up, by name: textFpsCounterCurrentValue, textFpsCounterAverageValue, textFpsCounterMinimumValue,
// textFpsCounterMaximumValue, textFpsCounterMeasuredValue and buttonFpsCounterReset. Texts:
// Dialog/ExtensionsStringTable.rsc (the FpsCounter* keys).

// The page: a label and its figure on each of five rows, then the button; 4 px between the rows.
RibbonDescription pageFpsCounter
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 4;
	ElementDescription _elements
	[
		"rowFpsCounterCurrent" "rowFpsCounterAverage" "rowFpsCounterMinimum" "rowFpsCounterMaximum" "rowFpsCounterMeasured"
		"buttonFpsCounterReset"
	]
}

// A label and its figure; the controller sets the figure (the text's parameter) each frame the page shows.
RibbonDescription rowFpsCounterCurrent
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textFpsCounterCurrentLabel" "textFpsCounterCurrentValue" ]
}

RibbonDescription rowFpsCounterAverage
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textFpsCounterAverageLabel" "textFpsCounterAverageValue" ]
}

RibbonDescription rowFpsCounterMinimum
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textFpsCounterMinimumLabel" "textFpsCounterMinimumValue" ]
}

RibbonDescription rowFpsCounterMaximum
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textFpsCounterMaximumLabel" "textFpsCounterMaximumValue" ]
}

RibbonDescription rowFpsCounterMeasured
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textFpsCounterMeasuredLabel" "textFpsCounterMeasuredValue" ]
}

// The labels are one width so the figures line up.
TextDescription textFpsCounterCurrentLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "FpsCounterCurrentLabel";
}

TextDescription textFpsCounterAverageLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "FpsCounterAverageLabel";
}

TextDescription textFpsCounterMinimumLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "FpsCounterMinimumLabel";
}

TextDescription textFpsCounterMaximumLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "FpsCounterMaximumLabel";
}

TextDescription textFpsCounterMeasuredLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 90; String _text = "FpsCounterMeasuredLabel";
}

TextDescription textFpsCounterCurrentValue : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "FpsCounterValue";
}

TextDescription textFpsCounterAverageValue : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "FpsCounterValue";
}

TextDescription textFpsCounterMinimumValue : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "FpsCounterValue";
}

TextDescription textFpsCounterMaximumValue : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "FpsCounterValue";
}

TextDescription textFpsCounterMeasuredValue : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "FpsCounterValue";
}

// Starts the sums over (a new town, a changed setting).
ButtonDescription buttonFpsCounterReset : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "textFpsCounterReset";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "FpsCounterResetTip";
}

TextDescription textFpsCounterReset : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "FpsCounterReset";
}
