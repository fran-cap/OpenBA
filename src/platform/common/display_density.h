#pragma once
// ============================================================================
// src/platform/common/display_density.h -- UI-scale defaults for a high-density display
// ============================================================================
// NOT RETAIL. The game's UI is laid out in pixel units (GameSettings::uiScale, percent;
// _uiScale = uiScale / 100 is LAYOUT UNITS PER DEVICE PIXEL, so a smaller value is a
// bigger UI). On a window whose back buffer has more pixels than points (a high-density
// window, SDL pixel density > 1) the same settings show the UI physically smaller. These
// are the two questions game code would ask so a platform can answer for that case:
//   * the UI scale a FIRST RUN gets (no "UIScale" saved yet);
//   * the Options > UI scale slider's range.
// Bodies: src/platform/common/display_density.cpp. Both answers are derived from the game
// window's pixel density (Platform::Window::PixelDensity, display_sdl.cpp, through
// GamePixelDensity); at density 1 they are the identity, which is every window not created
// with the high-density flag (Windows, Linux, Apple without the Retina mode). On a
// density-2 Apple window a first run gets 50 and the slider 38..75 (docs/macos/UI_SCALE.md).
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here.
//
// Callers: GameSettings::Load (game_settings_persist.cpp, first run only) and
// OptionsDialog::OnOpened (the slider range).
// ============================================================================

namespace Platform {
namespace Display {

// The UI scale (percent) to use when no "UIScale" is saved. `defaultPercent` is what the
// game would use otherwise; returned unchanged at density 1 and render scale 100. P9: times the
// render scale (FirstRunPercentAt), so a phone's first run at Half gets 50.
int FirstRunUiScalePercent(int defaultPercent);

// The UI-scale slider's range (percent). In/out: pass retail's 75 / 150. Unchanged at density 1.
// Not moved by the render scale: the setting is in screen units (ui_render_scale.h).
void UiScaleSliderRange(float* minPercent, float* maxPercent);

// P9 (ui_render_scale.h, docs/android/P9_LOADING_SCALE.md §2): the render scale as a percent
// (Platform::Window::RenderScale; 100 on every desktop).
int RenderPercent();

// P9: UserInterface::_uiScale for the setting `uiPercent` (GameSettings::uiScale, the UI scale at
// Full render scale): uiPercent / RenderPercent(), which is retail's uiPercent / 100.0f at 100.
// Callers: GameContainer::ApplyKeyMapToUI and OptionsDialog::ApplyGameSettings (optionsdialog.cpp),
// load_progress.cpp and sharp_ui.cpp; the Enhanced tab's Render scale row (render_scale_entry.cpp)
// calls UiScaleRatioAt.
float UiScaleRatio(int uiPercent);

}  // namespace Display
}  // namespace Platform
