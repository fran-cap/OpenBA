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
		// the filter button names what a click does, under the key of the mode being shown
		{ String _name = "ItemPriorityShowAll";	String _text = "Show all items"; }
		{ String _name = "ItemPriorityShowAllTip";	String _text = "Unchecked: only items your town has made or stored, and the ranked ones."; }

		// the load notices (Extensions.rsc:extension*, shown by SaveLoadDialog::DoLoad in place of
		// retail's ModMissing): @0 name, @1 the save's `ext:<id>`, @2 the saved format version
		{ String _name = "ExtensionNotInBuild";		String _text = "The following extension is not part of this version of the game.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so this one cannot be played here: loading it will stop the game with an error. Do you still want to load the save game?"; }
		{ String _name = "ExtensionNewer";			String _text = "The following extension was saved by a newer version of it than this game has.~n~n@0 [@1]~nSaved with extension version @2~n~nThis version of the game cannot read it: loading this save will stop the game with an error. Do you still want to load the save game?"; }
		// ext.load_progress's line on the boot loading screen (src/ext/load_progress/): @0 / @1 the mod numbers
		{ String _name = "LoadProgressGameData";	String _text = "Loading game data"; }
		{ String _name = "LoadProgressMod";			String _text = "Loading mod @0 of @1"; }

		{ String _name = "ExtensionTurnedOff";		String _text = "The following extension is turned off, but this save was made with it.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so it will be turned on for this game. Do you still want to load the save game?"; }

		// the Extensions dialog's two tabs (Extensions.rsc:tabCaptionOptional / tabCaptionDefault)
		{ String _name = "ExtensionsTabOptional";	String _text = "Optional"; }
		{ String _name = "ExtensionsTabDefault";		String _text = "Default"; }
	]
}
