// Dialog/MapOverlays.rsc -- ext.map_overlays's views in the map window (NOT RETAIL; authored for this port, src/ext).
//
// docs/extensions/handins/MAPOVERLAYS.md, src/ext/map_overlays/map_window.cpp. The map window is retail's: the UtilityMap
// tool's StandardDialog.rsc with Dialog/Map.rsc:content (MapUI) injected at userGroup0 by its UIDescription. Nothing of it
// is copied or edited here. While the row is on, ToolUI::OnCreated injects one more element the same way into the same
// group: ribbonMapOverlays below, one row of three cells. The controller then moves retail's map content (the element
// MapUI built, by reference) into the first cell, so the window reads left to right: the map at full size, the column of
// view buttons, the shown view's controls. The window is as tall as the map; on the Map view the controls are hidden and
// only the button column stands beside the map.
//
// Controller: src/ext/map_overlays/map_window.cpp. It looks up, by name: ribbonMapOverlays, groupMapOverlaysMapSlot (the
// map's cell), columnMapOverlaysViews (its children are the eight buttons, in view order), groupMapOverlaysPages (its
// children are the eight pages, in view order), and in each page comboMapOverlaysCategory, comboMapOverlaysResource,
// comboMapOverlaysKind, rowMapOverlaysMonths, sliderMapOverlaysMonths, sliderMapOverlaysScale, ribbonMapOverlaysRamp
// (its children are the swatches), textMapOverlaysLegendMax, checkMapOverlaysTerrain. Texts:
// Dialog/ExtensionsStringTable.rsc (the MapOverlays* keys).

// The row: the map's cell, the view buttons, the shown view's controls.
RibbonDescription ribbonMapOverlays
{
	Alignment _alignment = TopLeft; int _cellPad = 4;
	ElementDescription _elements [ "groupMapOverlaysMapSlot" "columnMapOverlaysViews" "groupMapOverlaysPages" ]
}

// The map's cell: empty in the file; the controller moves retail's Map.rsc:content element into it.
GroupDescription groupMapOverlaysMapSlot
{
	Alignment _alignment = TopLeft;
}

// Map, Made, Used, Stock, Services, Traffic, Homes, Nature, top to bottom, all one width; the top pad lines the first
// button up with the minimap (Map.rsc:content pads its image by 2), clear of the title bar.
RibbonDescription columnMapOverlaysViews
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 1; int _topPad = 2;
	ElementDescription _elements
	[
		"buttonMapOverlaysTab" "buttonMapOverlaysTab" "buttonMapOverlaysTab" "buttonMapOverlaysTab"
		"buttonMapOverlaysTab" "buttonMapOverlaysTab" "buttonMapOverlaysTab" "buttonMapOverlaysTab"
	]
}

// Every view's button: the Mods dialog's tab button, with a caption the controller replaces.
ButtonDescription buttonMapOverlaysTab : "Mod.rsc:buttonMod"
{
	Flags _flags = ExpandWidth;
	ElementDescription _content = "tabCaptionMapOverlays";
}

TextDescription tabCaptionMapOverlays : "Mod.rsc:labelMod"
{
	int _leftPad = 3; int _rightPad = 3; int _topPad = 3; int _bottomPad = 3;
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "MapOverlaysTabMap";
}

// One page per view, in view order (overlay_core.h View): the controller shows the shown view's and hides the rest.
GroupDescription groupMapOverlaysPages
{
	Alignment _alignment = TopLeft; int _topPad = 2;
	ElementDescription _elements
	[
		"pageMapOverlaysMap" "pageMapOverlaysResource" "pageMapOverlaysResource" "pageMapOverlaysResource"
		"pageMapOverlaysKind" "pageMapOverlaysTraffic" "pageMapOverlaysKind" "pageMapOverlaysKind"
	]
}

// ---- the pages -----------------------------------------------------------------------------------------------------

// Map: retail's map alone.
GroupDescription pageMapOverlaysMap
{
}

// Production, Consumption, Stock: what to show (a category, then one resource of it), the months (not for Stock), and
// the shared rows.
RibbonDescription pageMapOverlaysResource
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 2; int _topPad = 2;
	ElementDescription _elements
	[
		"rowMapOverlaysCategory" "rowMapOverlaysResource" "rowMapOverlaysMonths"
		"rowMapOverlaysScale" "rowMapOverlaysLegend" "rowMapOverlaysTerrain"
	]
}

// Services, Citizens, Natural: a kind, and the shared rows.
RibbonDescription pageMapOverlaysKind
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 2; int _topPad = 2;
	ElementDescription _elements
	[
		"rowMapOverlaysKind" "rowMapOverlaysScale" "rowMapOverlaysLegend" "rowMapOverlaysTerrain"
	]
}

// Traffic: the months, and the shared rows.
RibbonDescription pageMapOverlaysTraffic
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 2; int _topPad = 2;
	ElementDescription _elements
	[
		"rowMapOverlaysMonths" "rowMapOverlaysScale" "rowMapOverlaysLegend" "rowMapOverlaysTerrain"
	]
}

// ---- the rows ------------------------------------------------------------------------------------------------------

RibbonDescription rowMapOverlaysCategory
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysCategoryLabel" "comboMapOverlaysCategory" ]
}

RibbonDescription rowMapOverlaysResource
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysResourceLabel" "comboMapOverlaysResource" ]
}

RibbonDescription rowMapOverlaysKind
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysKindLabel" "comboMapOverlaysKind" ]
}

RibbonDescription rowMapOverlaysMonths
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysMonthsLabel" "sliderMapOverlaysMonths" ]
}

// Fine on the left (the exact tiles), Coarse on the right (the widest blur).
RibbonDescription rowMapOverlaysScale
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysFine" "sliderMapOverlaysScale" "textMapOverlaysCoarse" ]
}

// The ramp: 0 on the left, the value at full colour on the right (LegendMax); the controller colours the swatches.
RibbonDescription rowMapOverlaysLegend
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textMapOverlaysLegendZero" "ribbonMapOverlaysRamp" "textMapOverlaysLegendMax" ]
}

RibbonDescription ribbonMapOverlaysRamp
{
	Alignment _alignment = MidLeft; int _cellPad = 0;
	ElementDescription _elements
	[
		"imageMapOverlaysSwatch" "imageMapOverlaysSwatch" "imageMapOverlaysSwatch" "imageMapOverlaysSwatch"
		"imageMapOverlaysSwatch" "imageMapOverlaysSwatch" "imageMapOverlaysSwatch" "imageMapOverlaysSwatch"
		"imageMapOverlaysSwatch" "imageMapOverlaysSwatch"
	]
}

RibbonDescription rowMapOverlaysTerrain
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "checkMapOverlaysTerrain" "textMapOverlaysTerrain" ]
}

// ---- the controls --------------------------------------------------------------------------------------------------

// The labels are one width so the controls line up.
TextDescription textMapOverlaysCategoryLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysCategoryLabel";
}

TextDescription textMapOverlaysResourceLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysResourceLabel";
}

TextDescription textMapOverlaysKindLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysKindLabel";
}

TextDescription textMapOverlaysMonthsLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysMonthsLabel";
}

TextDescription textMapOverlaysFine : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysFine";
}

TextDescription textMapOverlaysCoarse : "Extensions.rsc:textProfile"
{
	String _text = "MapOverlaysCoarse";
}

TextDescription textMapOverlaysLegendZero : "Extensions.rsc:textProfile"
{
	int _minWidth = 38; String _text = "MapOverlaysLegendZero";
}

// The value at full colour, in the view's unit; wide enough that a changing number does not resize the row.
TextDescription textMapOverlaysLegendMax : "Extensions.rsc:textProfile"
{
	int _minWidth = 54; String _text = "MapOverlaysParam";
}

TextDescription textMapOverlaysTerrain : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "MapOverlaysTerrain";
}

// The Town Hall inventory's combo (small font, an icon beside each item); the controller fills the items.
ComboDescription comboMapOverlaysCategory : "TownHall.rsc:inventoryCombo"
{
	int _minWidth = 146; int _topPad = 1; int _bottomPad = 1;
}

ComboDescription comboMapOverlaysResource : "TownHall.rsc:inventoryCombo"
{
	int _minWidth = 146; int _topPad = 1; int _bottomPad = 1;
}

ComboDescription comboMapOverlaysKind : "TownHall.rsc:inventoryCombo"
{
	int _minWidth = 146; int _topPad = 1; int _bottomPad = 1;
}

// The sliders derive from the Options dialog's own (as ShowGridPage.rsc's do); the controller sets the ranges.
SliderDescription sliderMapOverlaysMonths : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 146;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderMapOverlaysScale : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 106;
	ElementDescription _number = "Options.rsc:number";
}

// One step of the ramp: the game's plain white sprite (the one Map.rsc's images use, by reference), stretched to
// a small box and tinted by the controller.
ImageDescription imageMapOverlaysSwatch
{
	Flags _flags = ExpandWidth | ExpandHeight;
	int _minWidth = 9; int _minHeight = 10;
	SpriteSheet _spriteSheet = "SpriteSheet.rsc"; String _spriteName = "White";
}

CheckDescription checkMapOverlaysTerrain : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "MapOverlaysTerrainTip";
}
