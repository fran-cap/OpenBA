// ============================================================================
// src/ext/mods_persist/mods_in_game_warning.h -- fix.mods_persist, the in-game Mods warning
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §23.
//
// With fix.mods_persist on, the pause menu's Mods button opens this Yes / No message box
// (Dialog/Extensions.rsc:modsInGame) before the Mods window: changing mods in a game reloads the
// town under the new set, and turning off a mod the town uses can damage it (§21). Yes opens the
// window; No leaves the pause menu as it was. The change itself is kept (ModDialog::Apply writes
// the list to the settings), so it no longer reverts at the next main menu.
// ============================================================================
#pragma once

namespace UI {
class UserInterface;
}

namespace Ext {
namespace ModsInGameWarning {

// Shows the warning modally over `ui`; true when the player answered Yes. An unattended run
// (Opt().unattendedBoot) or a BANISHED_MODAL_ANSWER run answers without showing it, as the save /
// load notices do. True as well when the box cannot be loaded, so the Mods window still opens.
bool Confirm(UI::UserInterface* ui);

}  // namespace ModsInGameWarning
}  // namespace Ext
