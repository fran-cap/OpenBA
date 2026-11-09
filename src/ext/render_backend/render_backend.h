// src/ext/render_backend/render_backend.h -- ext.render_backend (class A, next video reset): the renderer
// picked in the Options dialog, DirectX 11 or Vulkan, in the one Windows exe that carries both
//
// NOT RETAIL. Retail ships one renderer per DLL (VideoDX9-x64.dll, VideoDX11-x64.dll, a GL slot) and lets
// the player pick one in the Video tab's Renderer combo: OptionsDialog::ApplyVideoSettings (exe
// 0x140071e10) sees the interface change and runs the full video restart, MainLoop::OnVideoError
// (0x1800dc5e0) -> Device::ReleaseDynamic -> Device::Shutdown (0x1800f83f0) -> the configuration
// written -> Device::Startup (0x1800f8260) -> Device::RecreateDynamic -> MainLoop::OnVideoRestart
// (0x1800dc3b0). Every video External is destructed and reloaded, every dynamic companion deleted and
// made again by the new DeviceData. Device::PlatformStartup also falls back by itself: a slot whose
// CreateDevice fails is torn down and the next slot is tried.
//
// THE SLOT. The dual exe (BANISHED_RENDER_BACKEND=dx11+vulkan, src/render/common/README.md) carries both
// backends behind retail's DX11 slot (Device::dllSlots[1]): Device::activeInterface (+0xF8) picks which
// blob of each shader and mesh is read (ShaderProgram::PlatformSerialize 0x1800f52b0: one arm per
// interface), and the Vulkan backend reads the DX11 blob, so both must run as interface 1. Which backend
// slot 1 makes is Render::SelectBackend's (src/render/common/render_backend.h); this row decides it.
//
//   ext.render_backend  ext.render_api  the renderer
//   0 (or faithful)     any             DirectX 11 (retail's)
//   1                   0               DirectX 11
//   1                   1               Vulkan, DirectX 11 when Vulkan cannot start
//
// THE MENU. With the row on, the Video tab's Renderer combo lists one item per backend in slot 1's place
// and nothing else (the index placeholder "DirectX 9" is not listed: only DirectX 11 and Vulkan).
// Picking the other backend makes ApplyVideoSettings run retail's full restart (ReadVideoRow says the
// renderer changed); CommitVideoRow, between Device::Shutdown and
// Device::Startup, selects the backend and lists the adapters again (the two backends' adapter lists
// mean different things: DXGI adapter / output pairs, Vulkan physical devices).
//
// THE DEFAULT (maintainer rulings 2026-11-08, a deliberate exception to "extension rows default off"): a
// fresh install starts on Vulkan, an existing OpenBA player stays on DirectX 11. The table's default is
// off; SeedFreshInstall, before Engine::Startup, writes ext.render_backend=1 and ext.render_api=1 when
// there is no settings.cfg: with nothing to import (IO::SettingsStore::Source::None) a new file holds the
// two keys; with a retail registry import (Source::Legacy, a retail player's first OpenBA run, which
// counts as a fresh install) the file is written as the imported set plus the two keys. A settings.cfg
// without the keys keeps the default: DirectX 11, never a silent switch. The fallback to DirectX 11 is on
// in every case. The faithful profile turns every extension row off, so it always runs DirectX 11
// (retail's renderer).
//
// THE FALLBACK. At boot the wanted backend is skipped when its probe fails (Render::BackendAvailable:
// no Vulkan loader, a loader older than 1.1, no adapter). A CreateDevice that fails later (vkCreateDevice,
// the swap chain) reaches Device::PlatformStartup's own fallback, whose seam (Video::Device::
// _extInterfaceRetry) asks RetryInterface: the next backend is selected, the adapters listed again and
// slot 1 tried once more before the loop steps to another slot. A fallback is SAVED (maintainer ruling
// 2026-11-08): once Engine::Startup succeeded on the other backend, SaveFallback writes it to
// ext.render_api (and turns the row on), so the next launch starts on the backend that worked. Not in the
// faithful profile, which always wants DirectX 11.
//
// THE HARNESS. BANISHED_RENDER_SWITCH (src/app/options.h; tools/dual_backend_check.sh) runs the same
// restart from the game loop at given frames (src/diag/render_switch_drive.cpp -> SwitchNow).
#pragma once

#include <cstdint>

namespace UI {
class ComboElement;
class DialogInstance;
}  // namespace UI

namespace Ext {
namespace RenderBackend {

// The row on (resolved from settings.cfg before the registry is installed) and the exe carries more
// than one backend.
bool Offered();

// src/app/main.cpp, before Engine::Startup (THE DEFAULT above): in an exe with more than one backend and
// with no settings.cfg (nothing to import, or a retail registry import), settings.cfg is written with
// ext.render_backend=1 and ext.render_api=1 (after the imported entries, if any). No-op otherwise, and
// when an import already names ext.render_backend. Uses only IO::SettingsStore (no engine allocator yet).
void SeedFreshInstall();

// After a Device::Startup that succeeded -- the boot (src/game/Game/game_main.cpp, after
// Ext::InitFromSettings), the Options dialog's full restart and SwitchNow: a start that fell back to
// another backend (THE FALLBACK above) records it, ext.render_api = the backend now running and
// ext.render_backend = 1, and flushes settings.cfg. Returns whether a fallback happened (the Options
// dialog then fills its combos again). Records nothing in the faithful profile.
bool SaveFallback();

// The first Device::InitializeInterfaces (inside Engine::Startup, before the adapters are listed): the
// wanted backend selected, or the next available one; installs the PlatformStartup seam. No-op in a
// one-backend build.
void BootSelect();

// Video::Device::_extInterfaceRetry: slot `index`'s CreateDevice failed and its DeviceData is gone. For
// slot 1 with an untried available backend left: select it, list the adapters again, answer true (the
// loop tries slot 1 again). False otherwise (the loop steps on, as retail).
bool RetryInterface(int index);

// The restart retail's ApplyVideoSettings runs for a renderer change, onto backend `index`, from the
// game loop (the harness). False, nothing done, when `index` is the active backend or out of range.
//  * DEFERRED while the game window is minimised: neither backend can make a device on an iconic window
//    (DX11's colour buffer is 0 x 0, Vulkan's surface has no pixels), so the restart would end with no
//    device at all. Nothing is torn down; the switch is kept and RunPendingSwitch runs it once the window
//    is restored. (The Options dialog's Apply cannot be pressed on a minimised window.)
//  * A restart that still starts no backend (the picked one and the fallback both failed) starts the
//    backend that was running again. If that fails too there is no device: retail's own end for a device
//    that cannot be made again (the DX11 device-lost recovery, contextdata.cpp): System::Error::SetText
//    (stop) + Display, and the restart's tail (RecreateDynamic, OnVideoRestart) does not run on no device.
bool SwitchNow(int index);

// The switch SwitchNow deferred, run once the window is no longer minimised. Called every frame by the
// caller of SwitchNow (src/diag/render_switch_drive.cpp). No-op without one.
void RunPendingSwitch();

// True while the adapters are being listed again (each slot makes a throwaway DeviceData): the harness's
// failswitch injection (src/diag/render_switch_drive.cpp) skips those.
bool ListingAdapters();

// ---- the Options dialog's Video tab (src/game/Dialog/optionsdialog.cpp) ----------------------------

// OnOpened, after PopulateVideoComboBoxes: slot 1's item replaced by one item per backend, the active one
// current. No-op unless Offered().
void OpenVideoRow(UI::DialogInstance* dialog, UI::ComboElement* comboRenderer);

// ApplyVideoSettings, before its "nothing changed" test: true when the combo names a backend other than
// the active one (the caller runs the full restart).
bool ReadVideoRow(const UI::DialogInstance* dialog);

// ApplyVideoSettings, after Device::Shutdown when the restart runs: the picked backend selected,
// ext.render_api recorded (the caller flushes settings.cfg), the adapters listed again. RollbackVideoRow
// restores ext.render_api when Device::Startup failed. Both no-ops for any other dialog.
void CommitVideoRow(const UI::DialogInstance* dialog);
void RollbackVideoRow(const UI::DialogInstance* dialog);

// OnClosed: forget the combo. No-op for any other dialog.
void CloseVideoRow(const UI::DialogInstance* dialog);

}  // namespace RenderBackend
}  // namespace Ext
