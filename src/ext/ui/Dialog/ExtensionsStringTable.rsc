// Dialog/ExtensionsStringTable.rsc -- the Extensions dialog's and menu button's texts (NOT RETAIL).
// Our own table, so the game's Dialog/StringTable.rsc is never edited (the kit's README, section 5:
// "Take care with resources that might be shared"). Compiled by tools/ext_resources.sh.
// The rows' names and descriptions come from the registry (src/ext/registry.cpp) through the
// general table's "Parameter" string (@0); everything fixed is here.

StringTable resource
{
	Entry _strings
	[
		// the menu button (main menu and pause menu)
		{ String _name = "Extensions";				String _text = "Extensions"; }
		{ String _name = "ExtensionsTip";			String _text = "Turn fixes for the game's known bugs and new features on or off."; }

		// ext.phone_menu's menu button (Android: the app's game-files screen, issue #96)
		{ String _name = "GameFiles";				String _text = "Game files"; }
		{ String _name = "GameFilesTip";			String _text = "Export your saves to a .zip, or import saves from one."; }

		// ext.touch_pointer's action bar (issue #96): the Shift toggle
		{ String _name = "TouchPointerShiftTip";	String _text = "Shift: roads go diagonal, the next building keeps this model"; }

		// the dialog's chrome
		{ String _name = "ExtensionsTitle";			String _text = "Fixes and Extensions"; }
		{ String _name = "ExtensionsProfile";		String _text = "(profile: @0)"; }
		{ String _name = "ExtensionsHeaderName";	String _text = "Name"; }
		{ String _name = "ExtensionsHeaderDesc";	String _text = "Description"; }
		{ String _name = "ExtensionsHeaderOn";		String _text = "On/Off"; }
		{ String _name = "ExtensionsToggleTip";		String _text = "Turn this fix or extension on or off. Press OK to apply."; }

		// a row's kind: the tier and the class letter (docs/extensions/DESIGN.md section 2)
		{ String _name = "ExtensionsKindFix";		String _text = "Bug fix"; }
		{ String _name = "ExtensionsKindA";			String _text = "Class A: takes effect immediately"; }
		{ String _name = "ExtensionsKindB";			String _text = "Class B: changes play, set per game"; }
		{ String _name = "ExtensionsKindC";			String _text = "Class C: extends the save, set per game"; }
		{ String _name = "ExtensionsKindOther";		String _text = "Extension"; }

		// a row's lock state
		{ String _name = "ExtensionsStateEnabled";			String _text = "Enabled"; }
		{ String _name = "ExtensionsStateDisabled";			String _text = "Disabled"; }
		{ String _name = "ExtensionsStateLockedEnabled";	String _text = "Enabled, locked for this game"; }
		{ String _name = "ExtensionsStateLockedDisabled";	String _text = "Disabled, locked for this game"; }
		{ String _name = "ExtensionsStateLockedOn";			String _text = "Disabled for this game, enabled from the next"; }
		{ String _name = "ExtensionsStateLockedOff";		String _text = "Enabled for this game, disabled from the next"; }

		// the in-game extension window (ExtensionsWindow.rsc) and ext.restricted_foods's page in it (RestrictedFoodsPage.rsc)
		{ String _name = "ExtensionsWindowTitle";	String _text = "Game extensions"; }
		{ String _name = "ExtensionsWindowTip";		String _text = "Game extensions: the pages of the extensions active in this town."; }
		{ String _name = "RestrictedFoodsTab";			String _text = "Foods"; }
		// ext.terrain_edit's page in it (TerrainEditPage.rsc)
		// the Town Screenshot page (src/ext/town_screenshot)
		{ String _name = "TownScreenshotTab";					String _text = "Town Screenshot"; }
		{ String _name = "TownScreenshotHeading";				String _text = "Town Screenshot"; }
		{ String _name = "TownScreenshotHint";				String _text = "Takes one picture of the whole town from above."; }
		{ String _name = "TownScreenshotHint2";				String _text = "The game pauses while it works."; }
		{ String _name = "TownScreenshotHeadingView";	String _text = "View Screenshot"; }
		{ String _name = "TownScreenshotHint3";			String _text = "Saves what is on the screen now."; }
		{ String _name = "TownScreenshotView";			String _text = "Take View Screenshot"; }
		{ String _name = "TownScreenshotQuality";				String _text = "Quality"; }
		{ String _name = "TownScreenshotAngle";				String _text = "Angle"; }
		{ String _name = "TownScreenshotTurn";				String _text = "Turn"; }
		{ String _name = "TownScreenshotWeather";				String _text = "Ignore Weather"; }
		{ String _name = "TownScreenshotWeatherTip";			String _text = "Draws the map without rain or snowfall or their lighting, whatever the weather is."; }
		{ String _name = "TownScreenshotSnow";				String _text = "Hide Snow on the Ground"; }
		{ String _name = "TownScreenshotSnowTip";				String _text = "Draws the ground bare even where snow lies on it."; }
		{ String _name = "TownScreenshotTake";				String _text = "Take Town Screenshot"; }
		{ String _name = "TownScreenshotCancel";				String _text = "Cancel"; }
		{ String _name = "TownScreenshotStatus";				String _text = "@0"; }
		{ String _name = "ShowGridTab";				String _text = "Grid"; }
		{ String _name = "ShowGridHeading";			String _text = "Show Grid"; }
		{ String _name = "ShowGridHint";			String _text = "Lines along the tiles' edges over the ground."; }
		{ String _name = "ShowGridShown";			String _text = "Show the grid"; }
		{ String _name = "ShowGridShownTip";			String _text = "Draws or hides the grid. The extension stays on either way."; }
		{ String _name = "ShowGridOpacityLabel";		String _text = "Opacity"; }
		{ String _name = "ShowGridShadeLabel";			String _text = "Brightness"; }
		{ String _name = "ShowGridHueLabel";			String _text = "Hue"; }
		{ String _name = "ShowGridSaturationLabel";		String _text = "Color"; }
		{ String _name = "ShowGridWidthLabel";			String _text = "Width"; }
		{ String _name = "FpsCounterTab";			String _text = "FPS"; }
		{ String _name = "FpsCounterCurrentLabel";			String _text = "Current"; }
		{ String _name = "FpsCounterAverageLabel";			String _text = "Average"; }
		{ String _name = "FpsCounterMinimumLabel";			String _text = "Minimum"; }
		{ String _name = "FpsCounterMaximumLabel";			String _text = "Maximum"; }
		{ String _name = "FpsCounterMeasuredLabel";			String _text = "Measured for"; }
		{ String _name = "FpsCounterValue";			String _text = "@0"; }
		{ String _name = "FpsCounterReset";			String _text = "Reset"; }
		{ String _name = "FpsCounterResetTip";			String _text = "Starts the count over."; }
		{ String _name = "TerrainEditTab";				String _text = "Terrain"; }
		{ String _name = "TerrainEditHint";				String _text = "Pick a brush, then hold the left mouse button over the map."; }
		{ String _name = "TerrainEditHint2";			String _text = "Lower digs lakes; Flatten levels ground to build on; Mountain raises peaks."; }
		{ String _name = "TerrainEditHint3";			String _text = "Right click ends the tool."; }
		{ String _name = "TerrainEditHeadingBrushes";		String _text = "Terrain Brushes"; }
		{ String _name = "TerrainEditHeadingResources";		String _text = "Place Resources"; }
		{ String _name = "TerrainEditHeadingShared";		String _text = "Both Tools"; }
		{ String _name = "TerrainEditUndoNote";			String _text = "Undo and Redo work on either tool."; }
		{ String _name = "TerrainEditHint4";			String _text = "Undo does not bring back trees or rocks a lake covered."; }
		{ String _name = "TerrainEditPlantHint";			String _text = "Plant: pick a resource, press the tree, then hold the left mouse button."; }
		{ String _name = "TerrainEditPlantHint2";			String _text = "Density is how full the area gets; resources already there count."; }
		{ String _name = "TerrainEditDensityLabel";		String _text = "Density"; }
		{ String _name = "TerrainEditPlant";			String _text = "Plant the resource picked on the right"; }
		{ String _name = "TerrainEditRaise";			String _text = "Raise"; }
		{ String _name = "TerrainEditLower";			String _text = "Lower"; }
		{ String _name = "TerrainEditFlatten";			String _text = "Flatten"; }
		{ String _name = "TerrainEditMountain";			String _text = "Mountain"; }
		{ String _name = "TerrainEditSmooth";			String _text = "Smooth"; }
		{ String _name = "TerrainEditUndo";				String _text = "Undo"; }
		{ String _name = "TerrainEditRedo";				String _text = "Redo"; }
		{ String _name = "TerrainEditSizeLabel";			String _text = "Size"; }
		{ String _name = "TerrainEditStrengthLabel";		String _text = "Strength"; }
		{ String _name = "TerrainEditStatus";			String _text = "@0"; }
		{ String _name = "RestrictedFoodsHint";			String _text = "Check a food to stop citizens eating it. It can still be stored, sold and cooked."; }
		{ String _name = "RestrictedFoodsParam";			String _text = "@0"; }
		{ String _name = "RestrictedFoodsCheckTip";		String _text = "Check to stop citizens eating this food."; }
		{ String _name = "RestrictedFoodsShowAll";		String _text = "Show all foods"; }
		{ String _name = "RestrictedFoodsShowAllTip";	String _text = "Unchecked: only foods your town has made or stored."; }

		// ext.item_priority's page in it (ItemPriorityPage.rsc)
		{ String _name = "ItemPriorityTab";			String _text = "Priority"; }
		{ String _name = "ItemPriorityHint";		String _text = "Rank the items citizens take first. A citizen takes the best-ranked item the storages hold; items not ranked come last."; }
		{ String _name = "ItemPriorityCatTool";		String _text = "Tools"; }
		{ String _name = "ItemPriorityCatFuel";		String _text = "Fuel"; }
		{ String _name = "ItemPriorityCatClothing";	String _text = "Clothing"; }
		{ String _name = "ItemPriorityCatHealth";	String _text = "Health"; }
		{ String _name = "ItemPriorityParam";		String _text = "@0"; }
		{ String _name = "ItemPriorityCheckTip";	String _text = "Check to rank this item (it goes to the bottom of the ranking)."; }
		{ String _name = "ItemPriorityTop";			String _text = "Top"; }
		{ String _name = "ItemPriorityUp";			String _text = "Up"; }
		{ String _name = "ItemPriorityDown";		String _text = "Down"; }
		{ String _name = "ItemPriorityNever";		String _text = "Never"; }
		{ String _name = "ItemPriorityNeverTip";	String _text = "Citizens never take this item, even when it is the only one the storages hold. Click again to allow it."; }
		// the filter button names what a click does, under the key of the mode being shown
		{ String _name = "ItemPriorityShowAll";	String _text = "Show all items"; }
		// ext.item_limits's row on a workshop's panel (ItemLimitsPanel.rsc)
		{ String _name = "ItemLimitsLabel";			String _text = "@0 limit"; }
		{ String _name = "ItemLimitsCheckTip";		String _text = "Check to give this item its own limit. Every workshop making it stops when the town holds this many, even under the category limit."; }
		{ String _name = "ItemLimitsSpinnerTip";	String _text = "The town-wide limit for this item, shared by every workshop that makes it."; }
		{ String _name = "ItemPriorityShowAllTip";	String _text = "Unchecked: only items your town has made or stored, and the ranked ones."; }

		// ext.family_tree's page in it (FamilyTreePage.rsc); the rows' texts are set by the controller through FamilyParam
		{ String _name = "FamilyTreeTab";			String _text = "Family"; }
		{ String _name = "FamilyHint";				String _text = "Pick a person to see their family: dates, parents, grandparents, spouses, children and siblings."; }
		{ String _name = "FamilyParam";				String _text = "@0"; }
		{ String _name = "FamilyTreeLeft";			String _text = "<"; }
		{ String _name = "FamilyTreeRight";			String _text = ">"; }
		{ String _name = "FamilyTreeNote";			String _text = "Click a person in the tree to center it on them."; }
		{ String _name = "FamilyImageButton";	String _text = "Save village family tree picture"; }
		{ String _name = "FamilyImageSvgButton";	String _text = "Save as SVG"; }
		{ String _name = "FamilyImageSvgTip";	String _text = "Save the picture as an SVG file instead: it stays sharp at any size, and a web browser opens it."; }
		{ String _name = "FamilyImageLiving";	String _text = "Only the living and their ancestors"; }
		{ String _name = "FamilyImageLivingTip";	String _text = "Leave out the people who died and are nobody's ancestor."; }
		{ String _name = "FamilyShowTree";	String _text = "Show family tree"; }
		{ String _name = "FamilyTreeDetails";	String _text = "Details"; }
		{ String _name = "FamilyTreeWindowTitle";	String _text = "Family Tree"; }
		{ String _name = "FamilyCitizenButton";	String _text = "Family"; }
		{ String _name = "FamilyCitizenTip";	String _text = "Show this citizen in the family tree."; }
		{ String _name = "FamilySummaryButton";		String _text = "Town summary"; }
		{ String _name = "FamilySummaryWindowTitle";	String _text = "Town Summary"; }
		{ String _name = "FamilySummaryNote";		String _text = "What the family record says about the town: where people came from, how they died, the generations and each year."; }
		{ String _name = "FamilyImageHighlight";	String _text = "Outline the picked person's descendants"; }
		{ String _name = "FamilyImageHighlightTip";	String _text = "In the saved picture, outline in orange the person picked on this page and everyone who descends from them."; }
		{ String _name = "FamilyImageJobs";		String _text = "Show each person's main job"; }
		{ String _name = "FamilyImageJobsTip";		String _text = "In the saved picture, add the job each person worked longest under their dates (for example Farmer 12 y). The boxes get a little taller."; }
		{ String _name = "FamilyShowInTown";		String _text = "Show in town"; }
		{ String _name = "FamilyShowInTownTip";		String _text = "Move the camera to the picked person and open their panel. Only for people living in the town now."; }
		{ String _name = "FamilySearch";			String _text = "Find:"; }
		{ String _name = "FamilySearchTip";			String _text = "Type part of a name to shorten the list of people. Click the box, type, and press Enter or click away when done. Clear it to see everyone."; }
		{ String _name = "FamilyShowDead";			String _text = "Show people who have died"; }
		{ String _name = "FamilyShowDeadTip";		String _text = "Unchecked: only the people living in your town. Checked: the dead too, after the living."; }

		// the load notices (Extensions.rsc:extension*, shown by SaveLoadDialog::DoLoad in place of
		// retail's ModMissing): @0 name, @1 the save's `ext:<id>`, @2 the saved format version
		{ String _name = "ExtensionNotInBuild";		String _text = "The following extension is not part of this version of the game.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so this one cannot be played here: loading it will stop the game with an error. Do you still want to load the save game?"; }
		{ String _name = "ExtensionNewer";			String _text = "The following extension was saved by a newer version of it than this game has.~n~n@0 [@1]~nSaved with extension version @2~n~nThis version of the game cannot read it: loading this save will stop the game with an error. Do you still want to load the save game?"; }
		// ext.load_progress's line on the boot loading screen (src/ext/load_progress/): @0 / @1 the mod numbers
		{ String _name = "LoadProgressGameData";	String _text = "Loading game data"; }
		{ String _name = "LoadProgressMod";			String _text = "Loading mod @0 of @1"; }

		// fix.save_failure_notice's message box (Extensions.rsc:saveFailed): @0 the save folder
		{ String _name = "SaveFailed";				String _text = "The game could not be saved.~n~nThe save folder could not be written:~n@0~n~nCheck that the folder is not read-only, that Windows Controlled Folder Access or an antivirus is not blocking the game, and that the disk has free space. The previous save of that name was kept."; }
		// fix.mods_persist's in-game warning (Extensions.rsc:modsInGame): Yes opens the Mods window
		{ String _name = "ModsInGame";				String _text = "Mods are safest to change from the main menu.~n~nChanging them during a game reloads this town with the new mods. Turning off a mod the town uses, such as one that adds buildings, resources or items, can damage the town or crash the game. Save first.~n~nOpen the Mods window anyway?"; }

		{ String _name = "ExtensionTurnedOff";		String _text ="The following extension is turned off, but this save was made with it.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so it will be turned on for this game. Do you still want to load the save game?"; }

		// the Extensions dialog's two tabs (Extensions.rsc:tabCaptionOptional / tabCaptionDefault)
		{ String _name = "ExtensionsTabOptional";	String _text = "Optional"; }
		{ String _name = "ExtensionsTabDefault";		String _text = "Default"; }
		{ String _name = "ExtensionsTabExperimental";	String _text = "Experimental"; }

		// ext.building_finder's page in it (BuildingFinderPage.rsc); the rows' texts are set by the controller through FinderParam
		{ String _name = "FinderTab";				String _text = "Find Buildings"; }
		{ String _name = "FinderHint";			String _text = "Press a name to place it. + adds it to your favorites."; }
		{ String _name = "FinderParam";			String _text = "@0"; }
		{ String _name = "FinderSearch";			String _text = "Find:"; }
		{ String _name = "FinderSearchTip";		String _text = "Type part of a name, or of the group a building is in (Housing, Farm). Every word you type must match. Click the box, type, and press Enter or click away when done."; }
		{ String _name = "FinderClear";			String _text = "Clear"; }
		{ String _name = "FinderClearTip";		String _text = "Empty the search box to see every building."; }
		{ String _name = "FinderOpenFav";			String _text = "Open Favorites Window"; }
		{ String _name = "FinderOpenFavTip";		String _text = "Open the favorites in a small window that stays beside the map while you place buildings. The hotkey button beside this one sets the key that opens or closes it on the map."; }
		{ String _name = "FinderKeyTip";			String _text = "Change the key that opens or closes the favorites window on the map. Press this button, then the new key; Esc cancels. The key is kept in settings.cfg."; }
		{ String _name = "FavoritesWindowTitle";	String _text = "Favorite Buildings"; }
		{ String _name = "FavoritesOpenPage";		String _text = "Open Find Buildings"; }
		{ String _name = "FavoritesClear";			String _text = "Clear All"; }
		{ String _name = "FavoritesClearSure";		String _text = "Sure? Clear All"; }
		{ String _name = "FavoritesClearTip";		String _text = "Take every building out of the favorites. Press it twice: the first press asks, the second clears the list."; }
		{ String _name = "FavoritesOpenPageTip";	String _text = "Open the Find Buildings page of the extensions window, where you search for buildings and press + to add them here."; }
		{ String _name = "FinderPickTip";		String _text = "Start placing this building, as the toolbar's button does. A building whose limit is reached cannot be placed."; }
		{ String _name = "FinderStarTip";		String _text = "+ adds the building to your favorites; - takes it out. The favorites window shows them; the list is kept in building_favorites.txt, beside settings.cfg."; }
		// ext.map_overlays: the tabs of the map window (MapOverlays.rsc)
		{ String _name = "MapOverlaysTabMap";			String _text = "Map"; }
		{ String _name = "MapOverlaysTabProduction";	String _text = "Made"; }
		{ String _name = "MapOverlaysTabConsumption";	String _text = "Used"; }
		{ String _name = "MapOverlaysTabStock";			String _text = "Stock"; }
		{ String _name = "MapOverlaysTabServices";		String _text = "Services"; }
		{ String _name = "MapOverlaysTabTraffic";		String _text = "Traffic"; }
		{ String _name = "MapOverlaysTabCitizens";		String _text = "Homes"; }
		{ String _name = "MapOverlaysTabNatural";		String _text = "Nature"; }
		{ String _name = "MapOverlaysTabMapTip";			String _text = "The map as the game draws it, with no overlay."; }
		{ String _name = "MapOverlaysTabProductionTip";	String _text = "Where goods were made: the items produced on each spot over the last months."; }
		{ String _name = "MapOverlaysTabConsumptionTip";	String _text = "Where goods were used: eaten, burned, worked into other goods or built with, over the last months."; }
		{ String _name = "MapOverlaysTabStockTip";		String _text = "Where goods are now: storage, stockpiles, markets and homes."; }
		{ String _name = "MapOverlaysTabServicesTip";	String _text = "Where a service reaches: brighter where several buildings of the kind overlap."; }
		{ String _name = "MapOverlaysTabTrafficTip";		String _text = "Where citizens walk: footsteps over the last months."; }
		{ String _name = "MapOverlaysTabCitizensTip";	String _text = "How homes fare: the residents' happiness, health, food, warmth or age."; }
		{ String _name = "MapOverlaysTabNaturalTip";		String _text = "Where the natural resources are: trees, stone, iron, wildlife and fish."; }
		{ String _name = "MapOverlaysCategoryLabel";		String _text = "Goods"; }
		{ String _name = "MapOverlaysResourceLabel";		String _text = "Item"; }
		{ String _name = "MapOverlaysKindLabel";			String _text = "Show"; }
		{ String _name = "MapOverlaysMonthsLabel";		String _text = "Months"; }
		{ String _name = "MapOverlaysFine";				String _text = "Fine"; }
		{ String _name = "MapOverlaysCoarse";			String _text = "Coarse"; }
		{ String _name = "MapOverlaysLegendZero";		String _text = "0"; }
		{ String _name = "MapOverlaysParam";				String _text = "@0"; }
		{ String _name = "MapOverlaysTerrain";			String _text = "Show overlay on terrain"; }
		{ String _name = "MapOverlaysTerrainTip";		String _text = "Draws the same colours on the ground."; }
		{ String _name = "MapOverlaysAllGoods";			String _text = "All goods"; }
		{ String _name = "MapOverlaysFood";				String _text = "Food"; }
		{ String _name = "MapOverlaysFuel";				String _text = "Fuel"; }
		{ String _name = "MapOverlaysOther";				String _text = "Other"; }
		{ String _name = "MapOverlaysAllOf";				String _text = "All of them"; }
		{ String _name = "MapOverlaysUnitItems";			String _text = "@0 items"; }
		{ String _name = "MapOverlaysUnitSteps";			String _text = "@0 steps"; }
		{ String _name = "MapOverlaysUnitPercent";		String _text = "@0 %"; }
		{ String _name = "MapOverlaysUnitCount";			String _text = "@0"; }
		{ String _name = "MapOverlaysServiceMarket";		String _text = "Market"; }
		{ String _name = "MapOverlaysServiceWell";		String _text = "Well"; }
		{ String _name = "MapOverlaysServiceHerbalist";	String _text = "Health"; }
		{ String _name = "MapOverlaysServiceChapel";		String _text = "Chapel"; }
		{ String _name = "MapOverlaysServiceSchool";		String _text = "School"; }
		{ String _name = "MapOverlaysServiceTavern";		String _text = "Tavern"; }
		{ String _name = "MapOverlaysServiceTownHall";	String _text = "Town Hall"; }
		{ String _name = "MapOverlaysCitizenHappiness";	String _text = "Happiness"; }
		{ String _name = "MapOverlaysCitizenHealth";		String _text = "Health"; }
		{ String _name = "MapOverlaysCitizenFood";		String _text = "Food"; }
		{ String _name = "MapOverlaysCitizenWarmth";		String _text = "Warmth"; }
		{ String _name = "MapOverlaysCitizenAge";		String _text = "Age"; }
		{ String _name = "MapOverlaysNaturalTrees";		String _text = "Trees"; }
		{ String _name = "MapOverlaysNaturalStone";		String _text = "Stone"; }
		{ String _name = "MapOverlaysNaturalIron";		String _text = "Iron"; }
		{ String _name = "MapOverlaysNaturalWildlife";	String _text = "Wildlife"; }
		{ String _name = "MapOverlaysNaturalFish";		String _text = "Fish"; }
		{ String _name = "ProfessionsWindowTitle";	String _text = "Professions"; }
		{ String _name = "ProfessionsOneColumnTip";	String _text = "Show one column"; }
		{ String _name = "ProfessionsTwoColumnsTip";	String _text = "Show two columns"; }
		{ String _name = "ProfessionsGrip";	String _text = "......"; }
	]
}
