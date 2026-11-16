#pragma once
// ============================================================================
// src/render/vulkan/present.h -- the pre-rotated present pass (NOT RETAIL)
// ============================================================================
// docs/vulkan/PLAN.md §3.5 / §4.5. A surface whose currentTransform is not IDENTITY (an Android
// phone held in landscape: its panel's natural orientation is portrait, so the transform is
// ROTATE_90 or ROTATE_270) is presented WITHOUT the compositor's rotation pass when the swap
// chain is made with preTransform = currentTransform and its images in the panel's own
// orientation; the content must then be drawn rotated. Creating it with IDENTITY instead works,
// but every present answers VK_SUBOPTIMAL_KHR (Android 10+), which the frame loop answers with a
// rebuild, every frame.
//
// The engine keeps rendering one landscape frame of the back buffer's size. When the swap chain
// is pre-rotated, that frame is the backend's own image (SourceImage / SourceView: the frame's
// "main colour" for draw.cpp, its Resolve and read-back source), and the frame's end draws it,
// rotated, into the acquired swap-chain image (Record): one fullscreen triangle sampling it at
// the inverse of the transform. The shaders are OpenBA's own (present/present.vert, .frag, compiled
// to present/present_spirv.h by tools/vulkan/gen_present_spirv.sh). On an IDENTITY surface (every
// desktop) nothing here is made and the frame renders straight into the swap-chain image as before.
// ============================================================================
#include "vk_api.h"

#include <string>

namespace VideoVulkan {

// The pass's pipeline and descriptor objects (device created). Shutdown: the device is idle.
bool PresentStartup(std::string* why);
void PresentShutdown();

// The swap chain was (re)made, the device idle. `transform` its preTransform; `renderExtent` the
// landscape frame's size; `swapExtent` the images'; `format` the swap-chain format. IDENTITY with the
// two extents equal releases the source image. P7: a frame smaller than the window (the render scale,
// platform_window_hooks.h RenderScale) also renders into the source, and this pass scales it up
// (linear filtering), turned or not.
bool PresentConfigure(VkSurfaceTransformFlagBitsKHR transform, VkFormat format, VkExtent2D renderExtent,
                      VkExtent2D swapExtent, std::string* why);
// True while the frame renders into the source image (pre-rotated, or scaled).
bool PresentActive();
VkImage PresentSourceImage();
VkImageView PresentSourceView();
// The layout the source image is in when the next frame begins: UNDEFINED until a present pass has
// read it (a fresh image: the first frame, every swap-chain rebuild), SHADER_READ_ONLY after it.
VkImageLayout PresentSourceLayout();
// Records the rotated copy: the caller has moved the source image to SHADER_READ_ONLY and `swapImage`
// to COLOR_ATTACHMENT; `swapImage` is left there (the caller moves it to PRESENT_SRC).
void PresentRecord(VkCommandBuffer cb, VkImage swapImage, VkImageView swapView, VkExtent2D swapExtent);

}  // namespace VideoVulkan
