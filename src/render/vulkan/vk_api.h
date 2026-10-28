#pragma once
// ============================================================================
// src/render/vulkan/vk_api.h -- the Vulkan entry points the backend calls, loaded at run time
// ============================================================================
// NOT RETAIL. Nothing links against the Vulkan loader: SDL opens it when the window is
// created (SDL_WINDOW_VULKAN) and hands out vkGetInstanceProcAddr
// (SDL_Vulkan_GetVkGetInstanceProcAddr); every function below is resolved from that, the
// instance ones through vkGetInstanceProcAddr and the device ones through
// vkGetDeviceProcAddr (so a call skips the loader's dispatch trampoline). The headers are
// the pinned Vulkan-Headers submodule (third_party/Vulkan-Headers), used with
// VK_NO_PROTOTYPES so that no symbol is imported.
//
// The X-lists are the whole surface the backend uses; a new call is added to the list it
// belongs to and nothing else changes. An instance or device extension function
// that the driver does not provide stays null (the callers test the one they need).
// ============================================================================

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <string>

namespace VideoVulkan {

// Resolved with the loader's vkGetInstanceProcAddr(nullptr, ...), before any instance exists.
#define VIDEO_VULKAN_GLOBAL_FUNCS(X)                     \
    X(vkEnumerateInstanceVersion)                        \
    X(vkEnumerateInstanceExtensionProperties)            \
    X(vkEnumerateInstanceLayerProperties)                \
    X(vkCreateInstance)

// Resolved with vkGetInstanceProcAddr(instance, ...).
#define VIDEO_VULKAN_INSTANCE_FUNCS(X)                   \
    X(vkDestroyInstance)                                 \
    X(vkEnumeratePhysicalDevices)                        \
    X(vkGetPhysicalDeviceProperties)                     \
    X(vkGetPhysicalDeviceProperties2)                    \
    X(vkGetPhysicalDeviceFeatures2)                      \
    X(vkGetPhysicalDeviceQueueFamilyProperties)          \
    X(vkGetPhysicalDeviceMemoryProperties)               \
    X(vkGetPhysicalDeviceMemoryProperties2)              \
    X(vkGetPhysicalDeviceFormatProperties)               \
    X(vkEnumerateDeviceExtensionProperties)              \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)              \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)         \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)              \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)         \
    X(vkDestroySurfaceKHR)                               \
    X(vkCreateDevice)                                    \
    X(vkGetDeviceProcAddr)

// VK_EXT_debug_utils (the debug flavour's validation messenger); null when the instance
// was created without the extension.
#define VIDEO_VULKAN_INSTANCE_EXT_FUNCS(X)               \
    X(vkCreateDebugUtilsMessengerEXT)                    \
    X(vkDestroyDebugUtilsMessengerEXT)

// Resolved with vkGetDeviceProcAddr(device, ...).
#define VIDEO_VULKAN_DEVICE_FUNCS(X)                     \
    X(vkDestroyDevice)                                   \
    X(vkGetDeviceQueue)                                  \
    X(vkDeviceWaitIdle)                                  \
    X(vkQueueWaitIdle)                                   \
    X(vkCreateSwapchainKHR)                              \
    X(vkDestroySwapchainKHR)                             \
    X(vkGetSwapchainImagesKHR)                           \
    X(vkAcquireNextImageKHR)                             \
    X(vkQueuePresentKHR)                                 \
    X(vkCreateImageView)                                 \
    X(vkDestroyImageView)                                \
    X(vkCreateCommandPool)                               \
    X(vkDestroyCommandPool)                              \
    X(vkResetCommandPool)                                \
    X(vkAllocateCommandBuffers)                          \
    X(vkBeginCommandBuffer)                              \
    X(vkEndCommandBuffer)                                \
    X(vkCreateFence)                                     \
    X(vkDestroyFence)                                    \
    X(vkWaitForFences)                                   \
    X(vkResetFences)                                     \
    X(vkCreateSemaphore)                                 \
    X(vkDestroySemaphore)                                \
    X(vkGetFenceStatus)                                  \
    X(vkQueueSubmit)                                     \
    X(vkCmdPipelineBarrier)                              \
    X(vkCmdClearColorImage)                              \
    X(vkCreateShaderModule)                              \
    X(vkDestroyShaderModule)                             \
    X(vkFreeCommandBuffers)                              \
    X(vkAllocateMemory)                                  \
    X(vkFreeMemory)                                      \
    X(vkMapMemory)                                       \
    X(vkUnmapMemory)                                     \
    X(vkFlushMappedMemoryRanges)                         \
    X(vkInvalidateMappedMemoryRanges)                    \
    X(vkBindBufferMemory)                                \
    X(vkBindImageMemory)                                 \
    X(vkBindBufferMemory2)                               \
    X(vkBindImageMemory2)                                \
    X(vkGetBufferMemoryRequirements)                     \
    X(vkGetImageMemoryRequirements)                      \
    X(vkGetBufferMemoryRequirements2)                    \
    X(vkGetImageMemoryRequirements2)                     \
    X(vkCreateBuffer)                                    \
    X(vkDestroyBuffer)                                   \
    X(vkCreateImage)                                     \
    X(vkDestroyImage)                                    \
    X(vkCreateSampler)                                   \
    X(vkDestroySampler)                                  \
    X(vkCreateDescriptorSetLayout)                       \
    X(vkDestroyDescriptorSetLayout)                      \
    X(vkCreatePipelineLayout)                            \
    X(vkDestroyPipelineLayout)                           \
    X(vkCreateDescriptorPool)                            \
    X(vkDestroyDescriptorPool)                           \
    X(vkResetDescriptorPool)                             \
    X(vkAllocateDescriptorSets)                          \
    X(vkUpdateDescriptorSets)                            \
    X(vkCreatePipelineCache)                             \
    X(vkDestroyPipelineCache)                            \
    X(vkGetPipelineCacheData)                            \
    X(vkCreateGraphicsPipelines)                         \
    X(vkDestroyPipeline)                                 \
    X(vkCmdCopyBuffer)                                   \
    X(vkCmdCopyBufferToImage)                            \
    X(vkCmdCopyImage)                                    \
    X(vkCmdBlitImage)                                    \
    X(vkCmdCopyImageToBuffer)                            \
    X(vkCmdFillBuffer)                                   \
    X(vkCmdClearDepthStencilImage)                       \
    X(vkCmdBindPipeline)                                 \
    X(vkCmdBindDescriptorSets)                           \
    X(vkCmdBindVertexBuffers)                            \
    X(vkCmdBindIndexBuffer)                              \
    X(vkCmdSetViewport)                                  \
    X(vkCmdSetScissor)                                   \
    X(vkCmdSetStencilReference)                          \
    X(vkCmdDraw)                                         \
    X(vkCmdDrawIndexed)                                  \
    X(vkCmdPushConstants)                                \
    X(vkCreateRenderPass)                                \
    X(vkDestroyRenderPass)                               \
    X(vkCreateFramebuffer)                               \
    X(vkDestroyFramebuffer)                              \
    X(vkCmdBeginRenderPass)                              \
    X(vkCmdEndRenderPass)

// Vulkan 1.2 / 1.3 core, resolved with vkGetDeviceProcAddr(device, ...) when the device has them: the
// 1.3 path (device.cpp) requires every one, the 1.1 path (vulkan11.h) calls none of them.
#define VIDEO_VULKAN_DEVICE_13_FUNCS(X)                  \
    X(vkQueueSubmit2)                                    \
    X(vkCmdPipelineBarrier2)                             \
    X(vkGetSemaphoreCounterValue)                        \
    X(vkWaitSemaphores)                                  \
    X(vkGetDeviceBufferMemoryRequirements)               \
    X(vkGetDeviceImageMemoryRequirements)                \
    X(vkCmdBeginRendering)                               \
    X(vkCmdEndRendering)                                 \
    X(vkCmdBindVertexBuffers2)

// Resolved with vkGetDeviceProcAddr(device, ...) when present: null when the device lacks the
// extension (Caps says which were enabled). P7: VK_KHR_push_descriptor.
#define VIDEO_VULKAN_DEVICE_EXT_FUNCS(X)                 \
    X(vkCmdPushDescriptorSetKHR)

#define VIDEO_VULKAN_DECLARE(name) extern PFN_##name name;
VIDEO_VULKAN_GLOBAL_FUNCS(VIDEO_VULKAN_DECLARE)
VIDEO_VULKAN_INSTANCE_FUNCS(VIDEO_VULKAN_DECLARE)
VIDEO_VULKAN_INSTANCE_EXT_FUNCS(VIDEO_VULKAN_DECLARE)
VIDEO_VULKAN_DEVICE_FUNCS(VIDEO_VULKAN_DECLARE)
VIDEO_VULKAN_DEVICE_13_FUNCS(VIDEO_VULKAN_DECLARE)
VIDEO_VULKAN_DEVICE_EXT_FUNCS(VIDEO_VULKAN_DECLARE)
#undef VIDEO_VULKAN_DECLARE
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

// Resolves the global functions from `getInstanceProcAddr` (SDL's). False with a plain
// message in *why when the loader is missing one of them.
bool LoadGlobalFunctions(PFN_vkGetInstanceProcAddr getInstanceProcAddr, std::string* why);
// Resolves the instance functions (and the optional extension ones) for `instance`.
bool LoadInstanceFunctions(VkInstance instance, std::string* why);
// Resolves the device functions for `device`; with `core13`, the 1.2 / 1.3 list must resolve too.
bool LoadDeviceFunctions(VkDevice device, bool core13, std::string* why);

// A zeroed Vulkan structure with its sType set: `auto ci = Make<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_...)`.
template <class T>
T Make(VkStructureType type) {
    T s{};
    s.sType = type;
    return s;
}

// A VkResult as its enum name (the common ones; the number otherwise), for log lines.
const char* ResultName(VkResult result);

}  // namespace VideoVulkan
