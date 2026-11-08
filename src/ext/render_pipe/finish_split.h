// ============================================================================
// src/ext/render_pipe/finish_split.h -- RENDERPIPE-1: ContextData::Finish in
// two halves, so the device-lost recovery runs on the main thread
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE1.md section 4).
//
// Retail's `Video::ContextData::Finish` (src/render/dx11/contextdata.cpp, slot 3,
// 0x1800017c0) presents and, when the present failed for good, runs the whole
// device-lost recovery inline: Device+0x50 (MainLoop::OnVideoError: every video
// External destructed, the loop told, the Context destroyed), the device torn
// down and recreated, Device+0x58 (MainLoop::OnVideoRestart: a new Context, every
// resource reloaded). On the render thread that recovery would run while the
// main thread gathers the next frame from the very objects it destroys. So the
// render thread runs only the first half and the main thread, at its next join,
// the second. Both are COPIES of the faithful body, cut at the `onLost` read; the
// faithful Finish is untouched and still finishes every sequential frame.
//
// Defined in src/render/dx11/render_pipe_finish.cpp (the dx11 TUs are the only
// ones that see the D3D headers).
// ============================================================================
#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)

#include <cstdint>

namespace Video { class Context; class Device; }

namespace Ext {
namespace RenderPipe {
BANISHED_RENDER_IMPL_BEGIN

// Steps 1-4 of Finish: Present(vsync, 0), drop the device lock, the
// BANISHED_FORCE_DEVICE_LOST drive (its own count: the Nth packet Present), the
// DXGI_ERROR_DEVICE_REMOVED / _RESET test, GetDeviceRemovedReason. 0: presented
// (or the error was transient) -- done. Otherwise the result the recovery needs.
int32_t FinishPresent(Video::Context& ctx);

// Step 5 of Finish, from the `onLost` read on: the notify, ReleaseDynamic, the
// device null, DeviceData Shutdown + CreateDevice, then the error text or
// RecreateDynamic + the restore notify. `device` is the lost context's device
// (the Context itself is destroyed by the notify).
void FinishRecover(Video::Device& device, int32_t hr);

BANISHED_RENDER_IMPL_END
}  // namespace RenderPipe
}  // namespace Ext
