// Dialog/Extensions.rsc -- the Extensions dialog, its menu button and the save-load notices
// (NOT RETAIL; authored for this port, src/ext).
//
// Shape of the dialog: a column (dialogBody) holding a heading row, two tabs with an entry list each
// (Optional, shown first: the registry rows off by default; Default: the rows on by default), and a
// right-aligned button row. The list is the game's own Mods list (Dialog/Mod.rsc:tableList,
// tableMods, borderLine) taken by reference, so the Extensions dialog sits in the same frame and
// scrolls the same way as the Mods dialog next to it; everything of ours derives from a game element
// and overrides only what differs.
//
// Controller: src/game/Dialog/extensionsdialog.cpp. It looks up, by name: buttonOk, buttonCancel
// (SharedElements), textProfile, tableOptional, tableDefault, and in each row (template 0 = entryRow) textModName,
// textModDescription, textKind, textState, imageTier, checkLoaded. textModName / textModDescription
// take the registry's text through the general table's "Parameter" (@0); textKind / textState keys
// are chosen by the controller from our table. src/ext/load_notice.cpp names the three notice dialogs.
//
// Texts: Dialog/ExtensionsStringTable.rsc; sprites: Dialog/ExtensionsSprites.rsc (both ours).
// Compiled by tools/ext_resources.sh over the kit's resource/ tree, so the references to the game's
// resources (SharedElements, Mod, MainMenu, MessageBox, StringTable, the fonts) resolve; in the game
// they come from the packages.

Dialog resource		// the dialog object: Dialog/Extensions.rsc
{
	Flags _flags = Modal | PauseGame | FirstDraw;
	ElementDescription _rootElement = "dialogRoot";

	ElementDescription _templates [ "entryRow" ]			// GetTemplate(0): one per registry entry

	// Built along with the dialog (as SaveLoad.rsc lists its own notices); DoLoad shows them.
	External _references [ "extensionNotInBuild" "extensionNewer" "extensionTurnedOff" ]
}

GroupDescription dialogRoot
{
	Alignment _alignment = MidCenter; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

// The column: heading, list, buttons; 4 px between them, 8 px to the border.
RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 4;
	int _leftPad = 8; int _rightPad = 8; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "heading" "tabExtensions" "buttonRow" ]
}

// Heading: the icon + title label, and the profile note at its right (hidden unless a profile
// from settings.cfg overrides the per-entry toggles).
RibbonDescription heading
{
	int _cellPad = 4; ElementDescription _elements [ "labelTitle" "textProfile" ]
}

LabelDescription labelTitle : "Mod.rsc:labelTitle"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "ExtensionsTitle";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
}

TextDescription textProfile : "smallNote"
{
	Alignment _alignment = MidLeft; String _text = "ExtensionsProfile";
}

// Ok (apply) and Cancel, the shared buttons, flush right under the list.
RibbonDescription buttonRow
{
	Alignment _alignment = MidRight; int _cellPad = 4;
	ElementDescription _elements [ "SharedElements.rsc:buttonCancel" "SharedElements.rsc:buttonOk" ]
}

// Two tabs in the Mods dialog's tab frame; the first (shown when the dialog opens) is Optional.
TabDescription tabExtensions : "Mod.rsc:tabMod"
{
	Tab _tabs [
		{ ElementDescription _button = "tabButtonOptional"; ElementDescription _content = "entryListOptional"; }
		{ ElementDescription _button = "tabButtonDefault"; ElementDescription _content = "entryListDefault"; }
	]
}

ButtonDescription tabButton : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "tabCaption";
}

TextDescription tabCaption : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "ExtensionsTabOptional";
}

ButtonDescription tabButtonOptional : "tabButton" { ElementDescription _content = "tabCaptionOptional"; }
ButtonDescription tabButtonDefault  : "tabButton" { ElementDescription _content = "tabCaptionDefault"; }
TextDescription tabCaptionOptional  : "tabCaption" { String _text = "ExtensionsTabOptional"; }
TextDescription tabCaptionDefault   : "tabCaption" { String _text = "ExtensionsTabDefault"; }

// A tab's content: the column headers over the scrolling list. entryList / tableList / tableMods are
// the bases (the in-game window's pages derive from entryList and tableList too); each tab has its
// own list and table, so the controller finds each table by its own name.
RibbonDescription entryList : "Mod.rsc:modGroup"
{
	ElementDescription _elements [ "columnHeaders" "tableList" ]
}

RibbonDescription entryListOptional : "entryList" { ElementDescription _elements [ "columnHeaders" "tableListOptional" ] }
RibbonDescription entryListDefault  : "entryList" { ElementDescription _elements [ "columnHeaders" "tableListDefault" ] }

RibbonDescription columnHeaders : "Mod.rsc:headerLine"
{
	ElementDescription _elements [ "headerName" "headerDesc" "headerOn" ]
}

// The base is a header the Mods dialog REACHES (Mod.rsc:textName, the name column's header):
// the game's packages hold only reached objects, and the exe derives at run time
// (KITFREE.md); Mod.rsc:textHeader itself is a template no package carries.
TextDescription header : "Mod.rsc:textName"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; int _leftPad = 0;
}

// Widths follow the row below: name column (icon + 4 + 200), description (wrapped at 192 + pads),
// the toggle.
TextDescription headerName : "header" { String _text = "ExtensionsHeaderName"; int _leftPad = 54; int _minWidth = 200; }
TextDescription headerDesc : "header" { String _text = "ExtensionsHeaderDesc"; int _minWidth = 220; }
TextDescription headerOn   : "header" { String _text = "ExtensionsHeaderOn";   int _minWidth = 48; }

// The scrolling list and the ribbon the rows go into, as the Mods dialog has them; the controller
// finds "tableOptional" / "tableDefault" and appends one entryRow per registry entry of that half.
// Its size is the game's own (596 x 350), set here too: a mod that makes the Mods window wider (MegaMod does) would otherwise leave a
// strip of empty list beside our rows.
ContainerDescription tableList : "Mod.rsc:tableList"
{
	int _width = 596;
	int _height = 350;
	int _scrollSpacing = 4;
	ElementDescription _content = "tableMods";
}

RibbonDescription tableMods : "Mod.rsc:tableMods"
{
}

ContainerDescription tableListOptional : "tableList" { ElementDescription _content = "tableOptional"; }
ContainerDescription tableListDefault  : "tableList" { ElementDescription _content = "tableDefault"; }
RibbonDescription tableOptional : "tableMods" { }
RibbonDescription tableDefault  : "tableMods" { }

// ---------------------------------------------------------------------------------------------
// A row: [icon] [name / kind / state column] ... [description] ... [toggle]. The text column is a
// vertical ribbon; the description starts 200 px into the row's text area, the toggle 230 px after
// that (the header widths above assume these).
// ---------------------------------------------------------------------------------------------
LayoutDescription entryRow
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1; int _topPad = 1; int _bottomPad = 1;

	LayoutItem _elements [
		{
			String _name = "glyph"; ElementDescription _element = "imageTier";
			LayoutHorizontal _xLayout = LeftToLeft; LayoutVertical _yLayout = TopToTop;
		}
		{
			String _name = "column"; ElementDescription _element = "entryColumn";
			LayoutHorizontal _xLayout = LeftToRight; String _xAnchorTo = "glyph"; int _xOffset = 4;
			LayoutVertical _yLayout = TopToTop; String _yAnchorTo = "glyph";
		}
		{
			String _name = "blurb"; ElementDescription _element = "textModDescription";
			LayoutHorizontal _xLayout = LeftToLeft; String _xAnchorTo = "column"; int _xOffset = 200;
			LayoutVertical _yLayout = TopToTop; String _yAnchorTo = "column";
		}
		{
			String _name = "toggle"; ElementDescription _element = "checkLoaded";
			LayoutHorizontal _xLayout = LeftToLeft; String _xAnchorTo = "blurb"; int _xOffset = 230;
			LayoutVertical _yLayout = TopToTop; String _yAnchorTo = "blurb";
		}
	]
}

// The entry's icon, the Mods row icon's size and frame; the controller sets the sprite
// (ExtensionsEntry_<id>, tools/ext_icons/).
ImageDescription imageTier : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsTierFix";
}

RibbonDescription entryColumn
{
	bool _vertical = true; ElementDescription _elements [ "textModName" "textKind" "textState" ]
}

TextDescription textModName : "Mod.rsc:textModName"
{
}

// The description's wrap width is our own, not the game's: the row puts the toggle 230 px past the description's left edge, and a mod that
// widens the Mods window's description text (MegaMod does) would otherwise push the text over the toggle.
TextDescription textModDescription : "Mod.rsc:textModDescription"
{
	int _wordWrapSize = 192;
}

// Small-font notes from our table: the base for the kind, the state and the profile texts.
TextDescription smallNote
{
	Font _font = "Font/FontSmall.rsc";
	StringTable _stringTable = "ExtensionsStringTable.rsc";
}

// The controller picks ExtensionsKindFix / KindA / KindB / KindC / KindOther ...
TextDescription textKind : "smallNote"
{
	String _text = "ExtensionsKindFix";
}

// ... and ExtensionsStateEnabled / Disabled / LockedEnabled / LockedDisabled / LockedOn / LockedOff.
TextDescription textState : "smallNote"
{
	String _text = "ExtensionsStateEnabled";
}

// The toggle: the Mods "loaded" check with our tooltip.
CheckDescription checkLoaded : "Mod.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "ExtensionsToggleTip";
}

// ---------------------------------------------------------------------------------------------
// The menu button (main menu and pause menu). The controller instantiates it beside the Mods
// button, so it is that button's shape with our caption, icon and tooltip.
// ---------------------------------------------------------------------------------------------
ButtonDescription buttonExtensions : "MainMenu.rsc:buttonMod"
{
	ElementDescription _content = "labelExtensions";
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "ExtensionsTip";
}

LabelDescription labelExtensions : "MainMenu.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "Extensions";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
}

// ---------------------------------------------------------------------------------------------
// The load notices (block EXTNOTICE, docs/extensions/handins/EXTNOTICE.md). SaveLoadDialog::DoLoad
// raises one per `ext:<id>` entry of a save this build cannot honour as it stands, where retail
// would raise its "mod missing" box. Each is the game's MessageBox with our text and only Yes / No
// showing (Yes loads anyway, No aborts -- the results DoLoad already handles). The texts take the
// parameters the game's ModMissing takes: @0 the name, @1 the save's `ext:<id>`, @2 the saved
// format version. The base is the game's own missing-mod notice (SaveLoad.rsc:modsMissing, the
// MessageBox shape the packages carry; MessageBox.rsc's unnamed dialog is a template they do not).
// ---------------------------------------------------------------------------------------------
Dialog extensionNotInBuild : "SaveLoad.rsc:modsMissing"
{
	ElementDescription _rootElement = "MessageBox.rsc:main";
	ElementOverride _override
	[
		{ String _name = "text"; ElementDescription _element = "noticeNotInBuild"; }
		{ String _name = "buttonOk"; }			// no element: hidden
		{ String _name = "buttonCancel"; }
	]
}

Dialog extensionNewer : "SaveLoad.rsc:modsMissing"
{
	ElementDescription _rootElement = "MessageBox.rsc:main";
	ElementOverride _override
	[
		{ String _name = "text"; ElementDescription _element = "noticeNewer"; }
		{ String _name = "buttonOk"; }
		{ String _name = "buttonCancel"; }
	]
}

Dialog extensionTurnedOff : "SaveLoad.rsc:modsMissing"
{
	ElementDescription _rootElement = "MessageBox.rsc:main";
	ElementOverride _override
	[
		{ String _name = "text"; ElementDescription _element = "noticeTurnedOff"; }
		{ String _name = "buttonOk"; }
		{ String _name = "buttonCancel"; }
	]
}

// The message box's text element, our table.
TextDescription notice : "MessageBox.rsc:text"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
}

TextDescription noticeNotInBuild : "notice" { String _text = "ExtensionNotInBuild"; }
TextDescription noticeNewer      : "notice" { String _text = "ExtensionNewer"; }
TextDescription noticeTurnedOff  : "notice" { String _text = "ExtensionTurnedOff"; }
