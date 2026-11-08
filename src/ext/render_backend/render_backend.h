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
// (retail's DX9 placeholder item stays). Picking the other backend makes ApplyVideoSettings run retail's
// full restart (ReadVideoRow says the renderer changed); CommitVideoRow, between Device::Shutdown and
// Device::Startup, selects the backend and lists the adapters again (the two backends' adapter lists
// mean different things: DXGI adapter / output pairs, Vulkan physical devices).
//
// THE FALLBACK. At boot the wanted backend is skipped when its probe fails (Render::BackendAvailable:
// no Vulkan loader, a loader older than 1.1, no adapter). A CreateDevice that fails later (vkCreateDevice,
// the swap chain) reaches Device::PlatformStartup's own fallback, whose seam (Video::Device::
// _extInterfaceRetry) asks RetryInterface: the next backend is selected, the adapters listed again and
// slot 1 tried once more before the loop steps to another slot. ext.render_api is not rewritten by a
// fallback: the next start tries the wanted backend again (logged).
//
// THE HARNESS. BANISHED_RENDER_SWITCH (src/app/options.h; tools/dual_backend_check.sh) runs the same
// restart from the game loop at given frames (src/diag/render_switch_harness.cpp -> SwitchNow).
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
bool SwitchNow(int index);

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
