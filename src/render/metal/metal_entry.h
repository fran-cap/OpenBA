#pragma once
// ============================================================================
// src/render/metal/metal_entry.h -- the pure C++ face of the Metal backend
// ============================================================================
// NOT RETAIL. Declarations that plain C++ translation units of the backend
// (render_backend_metal.cpp) need from the Objective-C++ side, without including
// any Metal header. Bodies: integration.mm (NewDeviceData) and backend.mm
// (Enumerate).
// ============================================================================

namespace Video {
class Device;
class DeviceData;
}  // namespace Video

namespace VideoMetal {

// A new Metal DeviceData for `device`, returned as its base.
::Video::DeviceData* NewDeviceData(::Video::Device* device);

// Lists the Metal adapter and modes under `interfaceName` in the device's interface list.
bool Enumerate(::Video::Device* device, const char* interfaceName);

// True when the ASSAO shaders converted and their pipelines can be built, so ambient
// occlusion can run. False before the device exists or when any ASSAO shader failed to
// convert. Declared here for AoBackend::Supported (integration.mm); the body belongs
// to the ASSAO pass (src/render/metal/passes/assao_pass.mm).
bool AssaoReady();

// The GPU's time for the last presented frame that completed, us (the frame's command buffer's
// GPUStartTime .. GPUEndTime, backend.mm). False: none completed yet. For ext.render_pipe's replayed
// frames (finish_split.h GpuFrameUs).
bool GpuFrameTime(long long* us);

}  // namespace VideoMetal
