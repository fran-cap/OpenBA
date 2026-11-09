// Dialog/EnhancedOptions.rsc -- the Options dialog's fifth tab "Enhanced" (NOT RETAIL; blocks AOMENU,
// FAA-MENU, BETTER-TREES, BETTERWATER, TEXQUALITY, COMPRESSAO, P8 render scale + frame rate cap, TOUCHMODE touch controls, P11b touch pointer, issue #96's "Touch screen" and "General" groups: twenty-six rows in the Game tab's container, docs/extensions/AO.md §6.3; was AoOptions.rsc). Added at run time by UI::TabElement::AddTab from
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
		{ String _name = "NaturalGrowth";	String _text = "Natural tree growth"; }
		{ String _name = "BetterWater";		String _text = "Better water"; }
		{ String _name = "BetterIce";		String _text = "Better ice"; }
		{ String _name = "BetterClouds";	String _text = "Better clouds"; }
		{ String _name = "FancyTerrain";	String _text = "Fancy terrain"; }
		{ String _name = "DepthBlur";		String _text = "Depth blur"; }
		{ String _name = "SharpUi";		String _text = "Sharp UI"; }
		{ String _name = "BetterCamera";	String _text = "Wider zoom"; }
		{ String _name = "CameraAngle";		String _text = "Camera angle"; }
		{ String _name = "FogDistance";	String _text = "Fog distance"; }
		{ String _name = "MaxShadowRes";	String _text = "Max shadow resolution"; }
		{ String _name = "ShadowZoom";		String _text = "Adaptive shadow map"; }
		{ String _name = "SeamlessLoops";	String _text = "Seamless ambient loops"; }
		{ String _name = "TextureQuality";	String _text = "Texture quality"; }
		{ String _name = "RenderScale";		String _text = "Render scale"; }
		{ String _name = "FpsCap";			String _text = "Frame rate cap"; }
		{ String _name = "TouchMode";		String _text = "Touch controls"; }
		{ String _name = "TouchPointer";	String _text = "Touch pointer"; }
		{ String _name = "TouchGroup";		String _text = "Touch screen"; }
		{ String _name = "GeneralGroup";	String _text = "General"; }
		{ String _name = "TouchPad";		String _text = "Camera pad"; }
		{ String _name = "TouchCameraButtons";	String _text = "Corner camera buttons"; }
		{ String _name = "TouchActionBar";	String _text = "Placement bar"; }
		{ String _name = "PhoneSafeMargin";	String _text = "Screen edge margin"; }
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
		{ String _name = "Level4";		String _text = "Lowest";	}
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

// Natural tree growth (NATURAL_GROWTH.md): ext.natural_tree_growth 0 / 1 (it takes effect while Better trees is on).
StringTable naturalGrowthItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "On";	}
	]
}

// Better water (BETTERWATER): 0 = the bool row ext.better_water off, 1 = on (Plain: the shore tint and
// foam line). Fancy joins as Item2 with its valued key when it is built.
StringTable betterWaterItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "Plain";	}
		{ String _name = "Item2";		String _text = "Fancy";	}
	]
}

// Better ice (WINTERICE): 0 = the bool row ext.winter_ice off, 1 = on (live).
StringTable betterIceItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "On";	}
	]
}

// Better clouds (BETTERCLOUDS): 0 = the bool row ext.better_clouds off, 1 = on with ext.better_clouds_mode 1 (the
// drifting clouds), 2 = on with ext.better_clouds_mode 2 (Fancy: also their shadows on the ground). Live.
StringTable betterCloudsItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "On";	}
		{ String _name = "Item2";		String _text = "Fancy";	}
	]
}

// Sharp UI (SHARPUI.md, SHARP_SPRITES.md): 0 = the bool row ext.sharp_ui off, 1 = on with ext.sharp_ui_icons 0 (the
// text: scale-matched fonts and whole-pixel quads), 2 = on with ext.sharp_ui_icons 1 (also the magnified sprite sheets).
StringTable sharpUiItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "Text";	}
		{ String _name = "Item2";		String _text = "Text + icons";	}
	]
}

// Wider zoom (BETTERCAM): 0 = the bool row ext.better_camera off, 1 = on. Class B: the request, taken by the next
// new game or load (a running game keeps the camera it started with; a loaded save keeps its own set).
StringTable betterCameraItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";				}
		{ String _name = "Item1";		String _text = "On (next game)";	}
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

// Adaptive shadow map (SHADOWZOOM.md section 6): ext.max_shadow_zoom 0 / 1 (it takes effect while Max shadow
// resolution is on): the casters drawn into a share of the sun map sized to the view. Live.
StringTable shadowZoomItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "On";	}
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

// Camera pad (issue #96): ext.touch_mode_pad_scale 0 (the cluster hidden) / 50 / 60 / 75 / 100 / 125 percent of
// its own size; 100 is the size it always had.
StringTable touchPadItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";		}
		{ String _name = "Item1";		String _text = "50%";		}
		{ String _name = "Item2";		String _text = "60%";		}
		{ String _name = "Item3";		String _text = "75%";		}
		{ String _name = "Item4";		String _text = "100%";		}
		{ String _name = "Item5";		String _text = "125%";		}
	]
}

// Corner camera buttons (issue #96, the reporter's follow-ups): ext.touch_mode_camera_buttons 0 (none) / 15 / 20 /
// 25 / 35 / 50 / 75 / 100 / 125 percent of the pad's button at pad 100 %.
StringTable touchCameraButtonItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";		}
		{ String _name = "Item1";		String _text = "15%";		}
		{ String _name = "Item2";		String _text = "20%";		}
		{ String _name = "Item3";		String _text = "25%";		}
		{ String _name = "Item4";		String _text = "35%";		}
		{ String _name = "Item5";		String _text = "50%";		}
		{ String _name = "Item6";		String _text = "75%";		}
		{ String _name = "Item7";		String _text = "100%";		}
		{ String _name = "Item8";		String _text = "125%";		}
	]
}

// Placement bar (issue #96): ext.touch_mode_action_bar_scale 25 / 30 / 35 / 50 / 60 / 75 / 100 / 125 percent; 100 = as
// before.
StringTable touchActionBarItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "25%";		}
		{ String _name = "Item1";		String _text = "30%";		}
		{ String _name = "Item2";		String _text = "35%";		}
		{ String _name = "Item3";		String _text = "50%";		}
		{ String _name = "Item4";		String _text = "60%";		}
		{ String _name = "Item5";		String _text = "75%";		}
		{ String _name = "Item6";		String _text = "100%";		}
		{ String _name = "Item7";		String _text = "125%";		}
	]
}

// Screen edge margin (issue #96): ext.phone_menu_safe_margin 0 / 2000 / 3375 / 4500 / 6000 thousandths of a percent of
// the screen height (Phone screen fit's safe area; 3375 is the phone default).
StringTable phoneSafeMarginItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "None";				}
		{ String _name = "Item1";		String _text = "2%";				}
		{ String _name = "Item2";		String _text = "3.4% (phone)";		}
		{ String _name = "Item3";		String _text = "4.5%";				}
		{ String _name = "Item4";		String _text = "6%";				}
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
// fits in it with the retail tabs' column widths (labels 200, controls 180). The vertical bar is named, not
// inherited: a mod's Options.rsc can rebuild gameOptions without one (The North 7, issue #96), which left
// the nineteen rows clipped at the page's height with nothing to scroll them.
ContainerDescription enhancedOptions : "Options.rsc:gameOptions"
{
	ElementDescription _verticalScroll = "SharedElements.rsc:verticalScrollbar";
	ElementDescription _content = "enhancedOptionsTable";
}

// The rows: Options.rsc:gameOptionsTable's shape, twenty-six rows. First the "Touch screen" group (issue #96: its header,
// Touch controls, Camera pad, Placement bar, Touch pointer, Screen edge margin), at the top so a phone shows it without
// scrolling the page; then the "General" header over the rest: AO combo, AO slider, Better trees, natural tree growth,
// Better water, Better ice (WINTERICE), Better clouds (BETTERCLOUDS), Sharp UI, Wider zoom (BETTERCAM), Camera angle (CAMERAANGLE), Fog distance (NOFOG), shadow resolution, adaptive shadow map (SHADOWZOOM), loops, texture quality (TEXQUALITY), render scale and frame rate cap (P8), the derived-data cache's
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
	int _height = 27;

	TableItem _items
	[
		{ int _x = 0; int _y = 0; ElementDescription _element = "textTouchGroup"; }
		{ int _x = 0; int _y = 1; ElementDescription _element = "textTouchMode"; }
		{ int _x = 0; int _y = 2; ElementDescription _element = "textTouchPad"; }
		{ int _x = 0; int _y = 3; ElementDescription _element = "textTouchCameraButtons"; }
		{ int _x = 0; int _y = 4; ElementDescription _element = "textTouchActionBar"; }
		{ int _x = 0; int _y = 5; ElementDescription _element = "textTouchPointer"; }
		{ int _x = 0; int _y = 6; ElementDescription _element = "textPhoneSafeMargin"; }
		{ int _x = 0; int _y = 7; ElementDescription _element = "textGeneralGroup"; }
		{ int _x = 0; int _y = 8; ElementDescription _element = "textAoQuality"; }
		{ int _x = 0; int _y = 9; ElementDescription _element = "textAoOpacity"; }
		{ int _x = 0; int _y = 10; ElementDescription _element = "textBetterTrees"; }
		{ int _x = 0; int _y = 11; ElementDescription _element = "textNaturalGrowth"; }
		{ int _x = 0; int _y = 12; ElementDescription _element = "textBetterWater"; }
		{ int _x = 0; int _y = 13; ElementDescription _element = "textBetterIce"; }
		{ int _x = 0; int _y = 14; ElementDescription _element = "textBetterClouds"; }
		{ int _x = 0; int _y = 15; ElementDescription _element = "textFancyTerrain"; }
		{ int _x = 0; int _y = 16; ElementDescription _element = "textDepthBlur"; }
		{ int _x = 0; int _y = 17; ElementDescription _element = "textSharpUi"; }
		{ int _x = 0; int _y = 18; ElementDescription _element = "textBetterCamera"; }
		{ int _x = 0; int _y = 19; ElementDescription _element = "textCameraAngle"; }
		{ int _x = 0; int _y = 20; ElementDescription _element = "textFogDistance"; }
		{ int _x = 0; int _y = 21; ElementDescription _element = "textMaxShadowRes"; }
		{ int _x = 0; int _y = 22; ElementDescription _element = "textShadowZoom"; }
		{ int _x = 0; int _y = 23; ElementDescription _element = "textSeamlessLoops"; }
		{ int _x = 0; int _y = 24; ElementDescription _element = "textTextureQuality"; }
		{ int _x = 0; int _y = 25; ElementDescription _element = "textRenderScale"; }
		{ int _x = 0; int _y = 26; ElementDescription _element = "textFpsCap"; }
		{ int _x = 0; int _y = 27; ElementDescription _element = "textDerivedCache"; }

		{ int _x = 1; int _y = 1; ElementDescription _element = "comboTouchMode"; }
		{ int _x = 1; int _y = 2; ElementDescription _element = "comboTouchPad"; }
		{ int _x = 1; int _y = 3; ElementDescription _element = "comboTouchCameraButtons"; }
		{ int _x = 1; int _y = 4; ElementDescription _element = "comboTouchActionBar"; }
		{ int _x = 1; int _y = 5; ElementDescription _element = "comboTouchPointer"; }
		{ int _x = 1; int _y = 6; ElementDescription _element = "comboPhoneSafeMargin"; }
		{ int _x = 1; int _y = 8; ElementDescription _element = "comboAoQuality"; }
		{ int _x = 1; int _y = 9; ElementDescription _element = "sliderAoOpacity"; }
		{ int _x = 1; int _y = 10; ElementDescription _element = "comboBetterTrees"; }
		{ int _x = 1; int _y = 11; ElementDescription _element = "comboNaturalGrowth"; }
		{ int _x = 1; int _y = 12; ElementDescription _element = "comboBetterWater"; }
		{ int _x = 1; int _y = 13; ElementDescription _element = "comboBetterIce"; }
		{ int _x = 1; int _y = 14; ElementDescription _element = "comboBetterClouds"; }
		{ int _x = 1; int _y = 15; ElementDescription _element = "comboFancyTerrain"; }
		{ int _x = 1; int _y = 16; ElementDescription _element = "comboDepthBlur"; }
		{ int _x = 1; int _y = 17; ElementDescription _element = "comboSharpUi"; }
		{ int _x = 1; int _y = 18; ElementDescription _element = "comboBetterCamera"; }
		{ int _x = 1; int _y = 19; ElementDescription _element = "sliderCameraAngle"; }
		{ int _x = 1; int _y = 20; ElementDescription _element = "sliderFogDistance"; }
		{ int _x = 1; int _y = 21; ElementDescription _element = "comboMaxShadowRes"; }
		{ int _x = 1; int _y = 22; ElementDescription _element = "comboShadowZoom"; }
		{ int _x = 1; int _y = 23; ElementDescription _element = "comboSeamlessLoops"; }
		{ int _x = 1; int _y = 24; ElementDescription _element = "comboTextureQuality"; }
		{ int _x = 1; int _y = 25; ElementDescription _element = "comboRenderScale"; }
		{ int _x = 1; int _y = 26; ElementDescription _element = "comboFpsCap"; }
		{ int _x = 1; int _y = 27; ElementDescription _element = "textDerivedCacheSize"; }
		{ int _x = 1; int _y = 28; ElementDescription _element = "buttonDerivedCacheClear"; }
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
TextDescription textNaturalGrowth : "textAoOption" { String _text = "NaturalGrowth"; }
TextDescription textBetterWater : "textAoOption" { String _text = "BetterWater"; }
TextDescription textBetterIce : "textAoOption" { String _text = "BetterIce"; }
TextDescription textBetterClouds : "textAoOption" { String _text = "BetterClouds"; }
TextDescription textFancyTerrain : "textAoOption" { String _text = "FancyTerrain"; }
TextDescription textDepthBlur : "textAoOption" { String _text = "DepthBlur"; }
TextDescription textSharpUi : "textAoOption" { String _text = "SharpUi"; }
TextDescription textBetterCamera : "textAoOption" { String _text = "BetterCamera"; }
TextDescription textCameraAngle : "textAoOption" { String _text = "CameraAngle"; }
TextDescription textFogDistance : "textAoOption" { String _text = "FogDistance"; }
TextDescription textMaxShadowRes : "textAoOption" { String _text = "MaxShadowRes"; }
TextDescription textShadowZoom : "textAoOption" { String _text = "ShadowZoom"; }
TextDescription textSeamlessLoops : "textAoOption" { String _text = "SeamlessLoops"; }
TextDescription textTextureQuality : "textAoOption" { String _text = "TextureQuality"; }
TextDescription textRenderScale : "textAoOption" { String _text = "RenderScale"; }
TextDescription textFpsCap : "textAoOption" { String _text = "FpsCap"; }
TextDescription textTouchMode : "textAoOption" { String _text = "TouchMode"; }
TextDescription textTouchPointer : "textAoOption" { String _text = "TouchPointer"; }
// The group headers (issue #96): a label row of its own, white where the rows are grey, with the cache row's top
// spacing. "Touch screen" over the touch and phone rows at the top of the page, "General" over the rest.
TextDescription textTouchGroup : "textAoOption" { String _text = "TouchGroup"; Color _color = 0xFFFFFFFF; int _topPad = 5; int _bottomPad = 3; }
TextDescription textGeneralGroup : "textAoOption" { String _text = "GeneralGroup"; Color _color = 0xFFFFFFFF; int _topPad = 5; int _bottomPad = 3; }
TextDescription textTouchPad : "textAoOption" { String _text = "TouchPad"; }
TextDescription textTouchCameraButtons : "textAoOption" { String _text = "TouchCameraButtons"; }
TextDescription textTouchActionBar : "textAoOption" { String _text = "TouchActionBar"; }
TextDescription textPhoneSafeMargin : "textAoOption" { String _text = "PhoneSafeMargin"; }
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

ComboDescription comboNaturalGrowth : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:naturalGrowthItems";
}

ComboDescription comboBetterWater : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterWaterItems";
}

ComboDescription comboBetterIce : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterIceItems";
}

ComboDescription comboBetterClouds : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterCloudsItems";
}

// Fancy terrain (FANCYTERRAIN): Off / On = the bool row ext.fancy_terrain (live); the Off / On table is Better ice's.
ComboDescription comboFancyTerrain : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterIceItems";
}

// Depth blur (docs/extensions/DOF.md): Off / On = the valued key ext.depth_blur of the row ext.depth_of_field (live); the Off / On table is Better ice's.
ComboDescription comboDepthBlur : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterIceItems";
}

ComboDescription comboSharpUi : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:sharpUiItems";
}

ComboDescription comboBetterCamera : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterCameraItems";
}

// Camera angle (CAMERAANGLE): 0..100, 50 = the row ext.camera_angle off (retail), any other value = on with ext.camera_tilt. Live.
SliderDescription sliderCameraAngle : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

// Fog distance (NOFOG): 0 = the row ext.no_fog off (retail's fog), 1..100 = on with ext.fog_distance (100 = no fog). Live.
SliderDescription sliderFogDistance : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

ComboDescription comboMaxShadowRes : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:shadowResItems";
}

ComboDescription comboShadowZoom : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:shadowZoomItems";
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

ComboDescription comboTouchPad : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:touchPadItems";
}

ComboDescription comboTouchCameraButtons : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:touchCameraButtonItems";
}

ComboDescription comboTouchActionBar : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:touchActionBarItems";
}

ComboDescription comboPhoneSafeMargin : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:phoneSafeMarginItems";
}
