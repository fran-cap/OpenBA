#pragma once
// ============================================================================
// src/render/vulkan/vulkan_entry.h -- what render_backend_vulkan.cpp needs from the backend
// ============================================================================
// NOT RETAIL. Declarations that the plain entry-point file (render_backend_vulkan.cpp) and
// the seam bodies (integration.cpp) need from the device file, without including any Vulkan
// header. Bodies: device.cpp.
// ============================================================================

namespace Video {
class Device;
class DeviceData;
}  // namespace Video

namespace VideoVulkan {

// A new Vulkan DeviceData for `device`, returned as its base.
::Video::DeviceData* NewDeviceData(::Video::Device* device);

// Appends one GraphicsInterface named `interfaceName` to the device's interface list, with
// one Adapter per Vulkan physical device that can draw, each listing the usable display
// modes. Shared with the placeholder slot (it lists the same adapters under its own name).
bool Enumerate(::Video::Device* device, const char* interfaceName);

// True between a successful CreateDevice and Shutdown.
bool DeviceLive();

// Whether Vulkan can start here, without making a device: the process's instance (made once and kept,
// as Enumerate makes it) loads and lists at least one adapter with a graphics queue. The dual exe's
// backend probe (render_backend_vulkan.cpp); the reason is logged when it cannot.
bool Available();

}  // namespace VideoVulkan

namespace Video {
class Context;
}  // namespace Video

namespace VideoVulkan {

// P4: ContextData::Finish's two halves (device.cpp; dx11 contextdata.cpp PresentFrame /
// RecoverLostDevice), which ext.render_pipe runs on two threads (integration.cpp).
// PresentFrame: end the frame, submit, present; VK_ERROR_DEVICE_LOST (as an int) when the device was lost
// in this frame (submit, acquire, present, a fence wait, or the injected loss), else 0.
int PresentFrame(::Video::Context& ctx);
// The device-lost recovery: OnVideoError, the device rebuilt, OnVideoRestart (or the error text).
void RecoverLostDevice(::Video::Device& device, int result);

}  // namespace VideoVulkan
