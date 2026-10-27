// Dialog/EnhancedOptions.rsc -- the Options dialog's fifth tab "Enhanced" (NOT RETAIL; blocks AOMENU,
// FAA-MENU, BETTER-TREES, TEXQUALITY, COMPRESSAO, P8 render scale + frame rate cap, TOUCHMODE touch controls, P11b touch pointer: twelve rows in the Game tab's container, docs/extensions/AO.md §6.3; was AoOptions.rsc). Added at run time by UI::TabElement::AddTab from
// OptionsDialog::OnOpened (src/ext/enhanced_tab/enhanced_tab.cpp). Every name here is NEW:
// a loose .crs never overrides a name the game's packages hold (IO::FileSystem::OpenFile looks
// in the packfile index first, handins/AOSPIKE.md S5). The look inherits the kit's own
// Options.rsc / SharedElements.rsc objects (resolved from data*.pkg at compile and run time).

StringTable aoStrings
{
	Entry _strings
	[
		{ String _name = "AoTab";			String _text = "Enhanced"; }
		{ String _name = "AoQuality";		String _text = "Ambient occlusion"; }
		{ String _name = "AoOpacity";		String _text = "AO strength"; }
		{ String _name = "BetterTrees";		String _text = "Better trees"; }
		{ String _name = "MaxShadowRes";	String _text = "Max shadow resolution"; }
		{ String _name = "SeamlessLoops";	String _text = "Seamless ambient loops"; }
		{ String _name = "TextureQuality";	String _text = "Texture quality"; }
		{ String _name = "RenderScale";		String _text = "Render scale"; }
		{ String _name = "FpsCap";			String _text = "Frame rate cap"; }
		{ String _name = "TouchMode";		String _text = "Touch controls"; }
		{ String _name = "TouchPointer";	String _text = "Touch pointer"; }
		{ String _name = "DerivedCache";	String _text = "Data cache on disk"; }
		{ String _name = "DerivedCacheSize";	String _text = "@0"; }
		{ String _name = "DerivedCacheClear";	String _text = "Clear cache"; }
	]
}

// The combo's items, in Params::quality order (0 = off, 1 Low, 2 Medium, 3 High).
StringTable aoQualityLevels
{
	Entry _strings
	[
		{ String _name = "Level0";		String _text = "Off";		}
		{ String _name = "Level1";		String _text = "Low";		}
		{ String _name = "Level2";		String _text = "Medium";	}
		{ String _name = "Level3";		String _text = "High";		}
	]
}

// Better trees, in BetterTrees::Mode order (0 = the row off, 1 on = smooth edges + stable shading,
// 2 = + distance fill).
StringTable betterTreesModes
{
	Entry _strings
	[
		{ String _name = "Mode0";		String _text = "Off";							}
		{ String _name = "Mode1";		String _text = "On";					}
		{ String _name = "Mode2";		String _text = "On + distance fill";	}
	]
}

// Max shadow resolution: 0 = the bool row ext.max_shadow_res off, 1 / 2 = on with ext.max_shadow_size 4096 / 8192.
StringTable shadowResItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "4096";	}
		{ String _name = "Item2";		String _text = "8192";	}
	]
}

// Seamless ambient loops: 0 = the bool row ext.seamless_loops off, 1 = on (heard after the next start).
StringTable seamlessLoopsItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";					}
		{ String _name = "Item1";		String _text = "On (next start)";		}
	]
}

// Render scale (P8, docs/android/P8_INPUT_OPTIONS.md): VideoRenderScale 100 / 50 / 25, live. The
// platform decides whether it can change (Platform::Window::RenderScaleAdjustable: Android); elsewhere
// the combo shows Full, disabled.
StringTable renderScaleItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Full";		}
		{ String _name = "Item1";		String _text = "Half";		}
		{ String _name = "Item2";		String _text = "Quarter";	}
	]
}

// Frame rate cap (P8): 0 = the bool row ext.fps_cap off, else on with ext.fps_cap_rate 30 / 60 / 90 / 120.
StringTable fpsCapItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";		}
		{ String _name = "Item1";		String _text = "30 fps";	}
		{ String _name = "Item2";		String _text = "60 fps";	}
		{ String _name = "Item3";		String _text = "90 fps";	}
		{ String _name = "Item4";		String _text = "120 fps";	}
	]
}

// Touch controls (TOUCHMODE, docs/android/TOUCH_MODE.md): 0 = the bool row ext.touch_mode off, else on with the
// toolbar at ext.touch_mode_toolbar_scale 100 / 125 / 150 / 175 / 200 percent (shrunk to fit beside the controls).
StringTable touchModeItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";				}
		{ String _name = "Item1";		String _text = "Toolbar 100%";		}
		{ String _name = "Item2";		String _text = "Toolbar 125%";		}
		{ String _name = "Item3";		String _text = "Toolbar 150%";		}
		{ String _name = "Item4";		String _text = "Toolbar 175%";		}
		{ String _name = "Item5";		String _text = "Toolbar 200%";		}
	]
}

// Touch pointer (P11b, docs/android/P11B_POINTER.md): 0 = the row ext.touch_pointer off (its keys kept), 1..4 = the
// row on with the presets of src/ext/touch_pointer/touch_pointer_math.h kTouchPresets (ext.touch_pointer_lift /
// _pick_radius / _tap_twice set together), 5 = Custom: settings.cfg values no preset matches (choosing it keeps them).
StringTable touchPointerItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";				}
		{ String _name = "Item1";		String _text = "Default";			}
		{ String _name = "Item2";		String _text = "Close aim";			}
		{ String _name = "Item3";		String _text = "Far aim";			}
		{ String _name = "Item4";		String _text = "Tap twice";			}
		{ String _name = "Item5";		String _text = "Custom";			}
	]
}

ButtonDescription buttonEnhanced : "Options.rsc:buttonGame"
{
	Alignment _alignment = MidCenter;
	ElementDescription _content = "labelEnhanced";
}

// Texture quality (TEXQUALITY): 0 = the bool row ext.texture_quality off (every level, retail), 1 = on
// with ext.texture_quality_drop 1 (seen after the next start). Full and Half only: Quarter was removed by
// the user's ruling (too blurry).
StringTable textureQualityItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Full";					}
		{ String _name = "Item1";		String _text = "Half (next start)";		}
	]
}

// The tab button, the combos and the slider derive from the Options tabs' own objects
// (Options.rsc:buttonGame, comboAutosave, sliderUI: the packages hold only objects a game dialog
// reaches, and the exe derives at run time, KITFREE.md); each base sets only fields overridden
// here, so the compiled result is the SharedElements template's.
// Options.rsc:labelOptions's look, with our string table and our icon (ExtensionsSprites.rsc).
LabelDescription labelEnhanced
{
	Alignment _alignment = MidCenter;

	int _bottomPad = 5;
	int _leftPad = 5;
	int _rightPad = 5;
	int _topPad = 5;

	int _minWidth = 40;
	int _minHeight = 40;

	Font _font = "Font\FontSmall.rsc";
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc";

	int _spacing = 2;
	LabelPosition _labelPosition = TextBottom;
	Alignment _textAlignment = MidCenter;

	String _text = "AoTab";
	String _spriteName = "OptionsEnhanced";
}

// The page: the Game tab's own scrolling container (Options.rsc:gameOptions: its size, clip region and
// vertical scroll bar), so the Enhanced tab takes exactly the room the retail tabs take and never widens
// the dialog. Its viewport is the container's width less the scroll bar and its gap; every row below
// fits in it with the retail tabs' column widths (labels 200, controls 180).
ContainerDescription enhancedOptions : "Options.rsc:gameOptions"
{
	ElementDescription _content = "enhancedOptionsTable";
}

// The rows: Options.rsc:gameOptionsTable's shape, twelve rows (AO combo, AO slider, Better trees, shadow,
// loops, texture quality (TEXQUALITY), render scale and frame rate cap (P8), the derived-data cache's
// size and, under it, its "Clear cache" button: COMPRESSAO). The size sits in the control column, not in the label: "0 MB (cleared at the next
// start)" in the label column made that column, and so the dialog, wider than the retail tabs.
TableDescription enhancedOptionsTable
{
	int _leftPad = 6;
	int _topPad = 4;
	int _rightPad = 2;
	int _bottomPad = 4;

	Alignment _alignment = TopLeft;

	int _width = 2;
	int _height = 12;

	TableItem _items
	[
		{ int _x = 0; int _y = 0; ElementDescription _element = "textAoQuality"; }
		{ int _x = 0; int _y = 1; ElementDescription _element = "textAoOpacity"; }
		{ int _x = 0; int _y = 2; ElementDescription _element = "textBetterTrees"; }
		{ int _x = 0; int _y = 3; ElementDescription _element = "textMaxShadowRes"; }
		{ int _x = 0; int _y = 4; ElementDescription _element = "textSeamlessLoops"; }
		{ int _x = 0; int _y = 5; ElementDescription _element = "textTextureQuality"; }
		{ int _x = 0; int _y = 6; ElementDescription _element = "textRenderScale"; }
		{ int _x = 0; int _y = 7; ElementDescription _element = "textFpsCap"; }
		{ int _x = 0; int _y = 8; ElementDescription _element = "textTouchMode"; }
		{ int _x = 0; int _y = 9; ElementDescription _element = "textTouchPointer"; }
		{ int _x = 0; int _y = 10; ElementDescription _element = "textDerivedCache"; }

		{ int _x = 1; int _y = 0; ElementDescription _element = "comboAoQuality"; }
		{ int _x = 1; int _y = 1; ElementDescription _element = "sliderAoOpacity"; }
		{ int _x = 1; int _y = 2; ElementDescription _element = "comboBetterTrees"; }
		{ int _x = 1; int _y = 3; ElementDescription _element = "comboMaxShadowRes"; }
		{ int _x = 1; int _y = 4; ElementDescription _element = "comboSeamlessLoops"; }
		{ int _x = 1; int _y = 5; ElementDescription _element = "comboTextureQuality"; }
		{ int _x = 1; int _y = 6; ElementDescription _element = "comboRenderScale"; }
		{ int _x = 1; int _y = 7; ElementDescription _element = "comboFpsCap"; }
		{ int _x = 1; int _y = 8; ElementDescription _element = "comboTouchMode"; }
		{ int _x = 1; int _y = 9; ElementDescription _element = "comboTouchPointer"; }
		{ int _x = 1; int _y = 10; ElementDescription _element = "textDerivedCacheSize"; }
		{ int _x = 1; int _y = 11; ElementDescription _element = "buttonDerivedCacheClear"; }
	]
}

TextDescription textAoOption
{
	Font _font = "Font\FontMedium.rsc";
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	int _minWidth = 200;
	Color _color = 0xFFC0C0C0;
}

TextDescription textAoQuality : "textAoOption" { String _text = "AoQuality"; }
TextDescription textAoOpacity : "textAoOption" { String _text = "AoOpacity"; }
TextDescription textBetterTrees : "textAoOption" { String _text = "BetterTrees"; }
TextDescription textMaxShadowRes : "textAoOption" { String _text = "MaxShadowRes"; }
TextDescription textSeamlessLoops : "textAoOption" { String _text = "SeamlessLoops"; }
TextDescription textTextureQuality : "textAoOption" { String _text = "TextureQuality"; }
TextDescription textRenderScale : "textAoOption" { String _text = "RenderScale"; }
TextDescription textFpsCap : "textAoOption" { String _text = "FpsCap"; }
TextDescription textTouchMode : "textAoOption" { String _text = "TouchMode"; }
TextDescription textTouchPointer : "textAoOption" { String _text = "TouchPointer"; }
// The cache row's label and size take the combo rows' spacing (a combo row is 26 + 3 high, its label
// sits 5 down), so the size and the button under it do not crowd the row above.
TextDescription textDerivedCache : "textAoOption" { String _text = "DerivedCache"; int _topPad = 5; int _bottomPad = 3; }
TextDescription textDerivedCacheSize : "textAoOption" { String _text = "DerivedCacheSize"; int _minWidth = 180; int _topPad = 5; int _bottomPad = 3; }

// The derived-data cache's "Clear cache" (COMPRESSAO): SharedElements.rsc's Apply button with our
// caption (the Options dialog's own buttons are that object, so the packages hold it).
ButtonDescription buttonDerivedCacheClear : "SharedElements.rsc:buttonApply"
{
	ElementDescription _content = "textDerivedCacheClear";
}

TextDescription textDerivedCacheClear : "SharedElements.rsc:buttonApplyText"
{
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	String _text = "DerivedCacheClear";
}

ComboDescription comboAoQuality : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:aoQualityLevels";
}

SliderDescription sliderAoOpacity : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

ComboDescription comboBetterTrees : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterTreesModes";
}

ComboDescription comboMaxShadowRes : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:shadowResItems";
}

ComboDescription comboSeamlessLoops : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:seamlessLoopsItems";
}

ComboDescription comboTextureQuality : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:textureQualityItems";
}

ComboDescription comboRenderScale : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:renderScaleItems";
}

ComboDescription comboFpsCap : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:fpsCapItems";
}

ComboDescription comboTouchMode : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:touchModeItems";
}

ComboDescription comboTouchPointer : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:touchPointerItems";
}
