// src/ext/dynamic_professions/dynamic_professions.h -- ext.dynamic_professions' entry points (NOT RETAIL)
//
// docs/extensions/handins/DYNAMICPROFESSIONS.md. Class A: with the extension on, the toolbar's Professions
// button opens a window that lists only the professions the town can use, instead of retail's list of all of
// them. The one engine seam is PlayerToolManager::ToggleUtilityDialog (toolbardialog.cpp).
#pragma once

class UIDescription;
namespace UI {
class DialogInstance;
class UserInterface;
}

namespace Ext {
namespace DynamicProfessions {

// True when the extension is on and `ui` is the Professions utility's description (one of its controllers is
// retail's ProfessionUI).
bool Handles(const UIDescription* ui);

// Opens the window, or closes it when it is open (a utility button's toggle). Returns the window it opened,
// null when it closed one or could not open. `declined` is set when the row of the game's Professions
// layout is not retail's shape (a mod rebuilt Profession.rsc): nothing was opened, and the caller opens
// retail's own.
UI::DialogInstance* Toggle(UI::UserInterface* ui, bool& declined);

}  // namespace DynamicProfessions
}  // namespace Ext
