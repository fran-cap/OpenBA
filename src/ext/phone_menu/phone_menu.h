#pragma once
// ============================================================================
// src/ext/phone_menu/phone_menu.h -- ext.phone_menu (class A, live)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md). The front end fitted to a phone held in
// landscape (19.5:9, rounded corners, a camera cut-out), and usable on any screen:
//   (a) on a screen wider than the main menu's backdrop overlay (16:9: the Banished logo and
//       the dark torn borders over the 3D menu scene; retail cover-fits it and cuts the logo's
//       top off at 19.5:9) the overlay is drawn with its top on the screen's top, so the logo is
//       whole and centred at the top, and the main and pause menus' buttons become a horizontal
//       row of square buttons across the middle of the safe area, wrapped to two rows when one
//       does not fit, the version line centred at the bottom (phone_math.h LogoTop / PlanRows;
//       owner's rulings 2026-10-25);
//   (b) otherwise (a desktop screen narrower than the backdrop, a row too wide for the screen) the
//       menu dialogs (the main / pause menu and the version line) are laid out inside a safe
//       area 4.5 % of the screen height in from every edge, the column centred between the logo
//       and the version line when it fits there (MenuBand), the version line moved to the
//       bottom-left when the column cannot sit above it (VersionAside);
//   (c) the Options UI scale slider goes down to 25 % (the UI four times larger than 100 %);
//   (d) every other dialog (the in-game toolbar and event notices bottom-right, the autosave
//       notice, the panels, tooltips and drop-downs, the front end's other dialogs) is kept
//       inside the same safe area: an edge or corner anchor moves in by the margin, a dialog
//       put at a screen point stays there unless the point is inside the margin, a centred
//       dialog does not move (phone_math.h SafeArea);
//   (e) P9, for a finger (touch_fit.h): scroll bars four times thicker, close buttons twice the
//       size, a dialog larger than the safe area shown whole at its own fitted scale while it is open,
//       the menus' backdrop's bottom border drawn back and the version line moved into it.
// Default OFF on a desktop; the Android first run switches it on (src/platform/android/
// app_android.cpp). Live: every hook reads the row each time. Under the faithful profile the
// engine seam stays null.

#include <cstdint>

namespace UI {
class CheckElement;
class ComboElement;
class Dialog;
}

namespace Ext {
namespace PhoneMenu {

// Boot only (registry_boot.cpp): installs UI::UserInterface::_extDialogPlace, _extBackdropOver and
// _extDialogScale (ext.touch_mode's toolbar scale, then the tall-dialog fit), all null under the
// faithful profile.
void Sync();

// MainMenuDialog::OnOpened (the main menu, and the pause menu with `pause`): the dialogs the
// menu layouts and the backdrop move apply to, by their description: the menu's own and the
// version line's (Dialog/BuildId.rsc). Either may be null.
void MenuOpened(const UI::Dialog* menu, const UI::Dialog* buildId, bool pause);

// OptionsDialog::OnOpened, after Platform::Display::UiScaleSliderRange: the slider's minimum
// (percent) while the row is on: UiScaleFloorNow on a screen wider than the menus' backdrop,
// else 25; `platformMinPercent` unchanged with the row off.
float UiScaleSliderMinimum(float platformMinPercent);

// The lowest UI scale (percent, a multiple of 5) at which the menus still fit as rows of squares
// on the screen the current back buffer stands for (phone_math.h UiScaleFloor; P9: in screen units,
// platform/common/ui_render_scale.h); 0 before the video device exists or
// on a screen not wider than the backdrop.
int32_t UiScaleFloorNow();

// GameContainer::ApplyKeyMapToUI (every loop's start, and Options' Apply), before the settings'
// UI scale becomes UserInterface::_uiScale: `percent` raised to UiScaleFloorNow while the row is
// on (a settings.cfg written before the floor, or by hand); unchanged otherwise.
int32_t ClampUiScale(int32_t percent);

// P8, retail's Video rows on a fixed surface (video_rows.cpp, docs/android/P8_INPUT_OPTIONS.md
// round 2). True while the row is on and Platform::Window::FixedDisplaySurface (Android): the
// surface is the screen at its own size and refresh, so retail's Resolution, Refresh rate and
// Fullscreen rows change nothing there, and VSync is kept on (the swap chain presents FIFO at the
// panel's rate; the frame rate below it is the Enhanced tab's Frame rate cap, ext.fps_cap, alone).
bool FixedVideoRows();

// Boot (registry_boot.cpp, after Sync): with FixedVideoRows, a VSync=false left in settings.cfg is
// turned on (Video::Configuration::vsync, then Video::Device::settingsChanged rebuilds the swap chain
// at the first frame), so a MAILBOX swap chain never renders frames the panel never shows.
void KeepVSyncOnFixedSurface();

// OptionsDialog::OnOpened, after retail filled the Video tab: with FixedVideoRows, the Resolution and
// Refresh rate combos and the Fullscreen and VSync checks are disabled, VSync shown ticked (Apply then
// reads values that change nothing, VSync on). Any argument may be null. Retail's rows elsewhere.
void FitVideoRows(UI::ComboElement* resolution, UI::ComboElement* refresh, UI::CheckElement* fullscreen,
                  UI::CheckElement* vsync);

}  // namespace PhoneMenu
}  // namespace Ext
