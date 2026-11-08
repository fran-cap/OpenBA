// ============================================================================
// src/diag/render_switch_drive.h -- BANISHED_RENDER_SWITCH (diagnostic, NOT RETAIL)
// ============================================================================
// The dual exe's renderer switch, driven without the Options dialog (tools/dual_backend_check.sh).
// The flag is a comma list of tokens:
//   <frame>:<backend>      at game frame <frame> (GameUpdateLoop's count), the full video restart onto
//                          <backend> ("dx11", "vulkan"): Ext::RenderBackend::SwitchNow, the sequence the
//                          Options dialog's Renderer change runs (src/ext/render_backend/render_backend.h)
//   failcreate=<backend>   at boot, the device Video::Device::PlatformStartup asks <backend> for is a
//                          placeholder whose CreateDevice fails (src/render/common/multi/
//                          render_backend_dispatch.cpp), so its fallback runs; once per process, and
//                          only before the game loop's first frame (the adapter listing, which makes a
//                          DeviceData too, never takes it: PlatformStartup has set Device::windowHandle)
//   failswitch=<backend>[+<backend>]  after the game loop's first frame, each named backend's next
//                          CreateDevice fails once (the same placeholder): a switch whose picked backend
//                          and fallback both fail, so SwitchNow's "no backend started" arm runs
// A one-backend build parses nothing and does nothing.
// ============================================================================
#pragma once

namespace RenderSwitchDrive {

// Once per GameUpdateLoop::Update (gameupdateloop.cpp): a switch whose frame is `frameNum` runs now.
void Tick(int frameNum);

// The dispatcher's question before slot 1 makes a DeviceData of `backendId`: true once when the flag asks
// for that backend's boot CreateDevice to fail. `deviceStartup`: the caller is PlatformStartup (the
// Device's window handle is set), not the adapter listing.
bool TakeFailCreate(const char* backendId, bool deviceStartup);

}  // namespace RenderSwitchDrive
