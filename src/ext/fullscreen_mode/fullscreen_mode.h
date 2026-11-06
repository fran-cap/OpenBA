// src/ext/fullscreen_mode/fullscreen_mode.h -- ext.fullscreen_mode (class A, default ON): Off / Exclusive /
// Borderless
//
// NOT RETAIL. Retail's Video tab has one "Fullscreen" checkbox (Options.rsc:checkFullscreen, the table
// graphicsTable's cell (1, 3)), stored as Video::Configuration::fullscreen (+0x40, "VideoFullscreen"),
// and DX11 makes it a DXGI exclusive fullscreen: a display mode switch (swap chain Windowed = FALSE,
// SetFullscreenState(TRUE)), which drops to a window whenever the game loses focus
// (DeviceData_OnActivate). With the row on, the borderless mode is added beside it:
//
//   VideoFullscreen  ext.fullscreen_borderless  the window
//   0                any                        windowed (retail)
//   1                0                          exclusive (retail)
//   1                1                          borderless: a windowed swap chain in the popup window
//                                               retail's AdjustDisplay already makes for fullscreen,
//                                               sized to the whole monitor (the desktop's resolution,
//                                               whatever VideoWidth / VideoHeight say: they are kept
//                                               for Exclusive), left on screen on a focus loss
//
// THE MENU. OptionsDialog::OnOpened hands this file the Fullscreen checkbox (OpenVideoRow). A group
// (Dialog/FullscreenMode.rsc:fullscreenModeCell) takes the checkbox's cell: the combo "Off / Exclusive /
// Borderless" shown, the checkbox moved into the group as its hidden second child, so retail's
// pointer to it (OptionsDialog::checkFullscreen) stays valid and the dialog's teardown deletes both.
// ApplyVideoSettings reads the checkbox as retail does; ReadVideoRow has written the combo's choice
// into it first and says whether the borderless choice changed (Exclusive <-> Borderless leaves
// VideoFullscreen at 1, so retail's "nothing changed" test would skip the reset).
//
// THE RENDERER. Borderless() is read at DX11's four fullscreen sites (src/render/dx11/devicedata.cpp):
// FillSwapChainDesc (Windowed), CreateDevice (the boot size), DeviceData_Resize (the size, and
// SetFullscreenState(FALSE) first when leaving exclusive), DeviceData_OnActivate (no drop to a
// window). A backend without an exclusive mode (Render::SupportsExclusiveFullscreen false: Vulkan,
// Metal) is not offered the row: its fullscreen is already a popup window, and retail's checkbox stays.
#pragma once

#include <cstdint>

namespace UI {
class CheckElement;
class DialogInstance;
}

namespace Ext {
namespace FullscreenMode {

// The row on (not under the gate's faithful profile) and the backend has an exclusive mode.
bool Offered();

// Offered(), VideoFullscreen on and ext.fullscreen_borderless 1: the window is a borderless popup over
// the monitor with a windowed swap chain.
bool Borderless();

// The size of `monitor` (an HMONITOR: Video::Device::monitor) from its monitor rect. False, and the
// outputs untouched, for a null monitor or an empty rect.
bool DesktopSize(void* monitor, int32_t* width, int32_t* height);

// ---- the Options dialog's Video tab (src/game/Dialog/optionsdialog.cpp) ----------------------------

// OnOpened, after retail has set the checkbox from VideoFullscreen: the combo takes the checkbox's cell
// and shows the live choice. No-op unless Offered(), or when the resource or the cell is not found
// (the checkbox then stays, as retail).
void OpenVideoRow(UI::DialogInstance* dialog, UI::CheckElement* checkFullscreen);

// ApplyVideoSettings, before it reads the checkbox: the combo's choice written into the checkbox
// (Off = 0, Exclusive / Borderless = 1). True when the borderless choice differs from the live one.
// False (the checkbox untouched) when `dialog` is not the one OpenVideoRow filled.
bool ReadVideoRow(const UI::DialogInstance* dialog);

// ApplyVideoSettings, when a change is applied: records ext.fullscreen_borderless as the combo says
// (the caller flushes settings.cfg). RollbackVideoRow restores the value Commit replaced (the reset
// failed). Both no-ops for any other dialog.
void CommitVideoRow(const UI::DialogInstance* dialog);
void RollbackVideoRow(const UI::DialogInstance* dialog);

// OnClosed: forget the combo (the dialog's teardown deletes it). No-op for any other dialog.
void CloseVideoRow(const UI::DialogInstance* dialog);

}  // namespace FullscreenMode
}  // namespace Ext
