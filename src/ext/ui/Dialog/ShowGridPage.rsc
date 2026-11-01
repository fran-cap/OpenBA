// Dialog/ShowGridPage.rsc -- ext.show_grid's page in the extension window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/SHOWGRID.md, src/ext/show_grid/toolbar_page.cpp. A check box that shows or hides the grid and
// five sliders for how it looks (opacity, shade, width, hue, color). The window around the page is Dialog/ExtensionsWindow.rsc (one
// tab per active extension page); this file is only the page.
//
// Controller: it looks up, by name: checkShowGridShown, sliderShowGridOpacity, sliderShowGridShade,
// sliderShowGridWidth, sliderShowGridHue and sliderShowGridSaturation. Texts: Dialog/ExtensionsStringTable.rsc (the ShowGrid* keys).

// The page: a heading, the check box, then a label and a slider on each of five rows; 4 px between the rows.
RibbonDescription pageShowGrid
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 4;
	ElementDescription _elements
	[
		"textShowGridHeading" "textShowGridHint" "rowShowGridShown"
		"rowShowGridOpacity" "rowShowGridHue" "rowShowGridSaturation" "rowShowGridShade" "rowShowGridWidth"
	]
}

TextDescription textShowGridHeading : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 2; String _text = "ShowGridHeading";
}

// The note is one short line (a long one does not wrap and would make the window as wide as the line).
TextDescription textShowGridHint : "Extensions.rsc:textProfile"
{
	String _text = "ShowGridHint";
}

RibbonDescription rowShowGridShown
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "checkShowGridShown" "textShowGridShown" ]
}

CheckDescription checkShowGridShown : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ShowGridShownTip";
}

TextDescription textShowGridShown : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ShowGridShown";
}

// A label and a slider; the controller sets the slider's range and value when the page opens, and reads it back.
RibbonDescription rowShowGridOpacity
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textShowGridOpacityLabel" "sliderShowGridOpacity" ]
}

RibbonDescription rowShowGridHue
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textShowGridHueLabel" "sliderShowGridHue" ]
}

RibbonDescription rowShowGridSaturation
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textShowGridSaturationLabel" "sliderShowGridSaturation" ]
}

RibbonDescription rowShowGridShade
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textShowGridShadeLabel" "sliderShowGridShade" ]
}

RibbonDescription rowShowGridWidth
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textShowGridWidthLabel" "sliderShowGridWidth" ]
}

// The labels are one width so the sliders line up.
TextDescription textShowGridOpacityLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "ShowGridOpacityLabel";
}

TextDescription textShowGridHueLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "ShowGridHueLabel";
}

TextDescription textShowGridSaturationLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "ShowGridSaturationLabel";
}

TextDescription textShowGridShadeLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "ShowGridShadeLabel";
}

TextDescription textShowGridWidthLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "ShowGridWidthLabel";
}

// The sliders derive from the Options dialog's own (as TerrainEditPage.rsc's do): the compiled game only holds the
// blocks something refers to as an element.
SliderDescription sliderShowGridOpacity : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderShowGridHue : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderShowGridSaturation : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderShowGridShade : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderShowGridWidth : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}
