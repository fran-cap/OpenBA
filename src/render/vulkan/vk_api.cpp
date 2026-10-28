// ============================================================================
// src/render/vulkan/vk_api.cpp -- the run-time resolution of vk_api.h's function pointers
// ============================================================================
// NOT RETAIL. See vk_api.h.
// ============================================================================

#include "vk_api.h"

#include <cstdio>

namespace VideoVulkan {

#define VIDEO_VULKAN_DEFINE(name) PFN_##name name = nullptr;
VIDEO_VULKAN_GLOBAL_FUNCS(VIDEO_VULKAN_DEFINE)
VIDEO_VULKAN_INSTANCE_FUNCS(VIDEO_VULKAN_DEFINE)
VIDEO_VULKAN_INSTANCE_EXT_FUNCS(VIDEO_VULKAN_DEFINE)
VIDEO_VULKAN_DEVICE_FUNCS(VIDEO_VULKAN_DEFINE)
VIDEO_VULKAN_DEVICE_13_FUNCS(VIDEO_VULKAN_DEFINE)
VIDEO_VULKAN_DEVICE_EXT_FUNCS(VIDEO_VULKAN_DEFINE)
#undef VIDEO_VULKAN_DEFINE
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

bool LoadGlobalFunctions(PFN_vkGetInstanceProcAddr getInstanceProcAddr, std::string* why) {
    if (!getInstanceProcAddr) {
        if (why) *why = "no vkGetInstanceProcAddr (SDL could not load the Vulkan loader)";
        return false;
    }
    vkGetInstanceProcAddr = getInstanceProcAddr;
    const char* missing = nullptr;
#define VIDEO_VULKAN_LOAD(fn)                                                             \
    fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(nullptr, #fn));                 \
    if (!fn && !missing) missing = #fn;
    VIDEO_VULKAN_GLOBAL_FUNCS(VIDEO_VULKAN_LOAD)
#undef VIDEO_VULKAN_LOAD
    if (missing) {
        if (why) {
            *why = std::string("the Vulkan loader has no ") + missing + " (a Vulkan 1.1+ loader is needed)";
        }
        return false;
    }
    return true;
}

bool LoadInstanceFunctions(VkInstance instance, std::string* why) {
    const char* missing = nullptr;
#define VIDEO_VULKAN_LOAD(fn)                                                             \
    fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn));                \
    if (!fn && !missing) missing = #fn;
    VIDEO_VULKAN_INSTANCE_FUNCS(VIDEO_VULKAN_LOAD)
#undef VIDEO_VULKAN_LOAD
#define VIDEO_VULKAN_LOAD_OPTIONAL(fn) fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn));
    VIDEO_VULKAN_INSTANCE_EXT_FUNCS(VIDEO_VULKAN_LOAD_OPTIONAL)
#undef VIDEO_VULKAN_LOAD_OPTIONAL
    if (missing) {
        if (why) *why = std::string("the Vulkan instance has no ") + missing;
        return false;
    }
    return true;
}

bool LoadDeviceFunctions(VkDevice device, bool core13, std::string* why) {
    const char* missing = nullptr;
#define VIDEO_VULKAN_LOAD(fn)                                                             \
    fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn));                    \
    if (!fn && !missing) missing = #fn;
    VIDEO_VULKAN_DEVICE_FUNCS(VIDEO_VULKAN_LOAD)
#undef VIDEO_VULKAN_LOAD
    const char* missing13 = nullptr;
#define VIDEO_VULKAN_LOAD_13(fn)                                                          \
    fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn));                    \
    if (!fn && !missing13) missing13 = #fn;
    VIDEO_VULKAN_DEVICE_13_FUNCS(VIDEO_VULKAN_LOAD_13)
#undef VIDEO_VULKAN_LOAD_13
#define VIDEO_VULKAN_LOAD_OPTIONAL(fn) fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn));
    VIDEO_VULKAN_DEVICE_EXT_FUNCS(VIDEO_VULKAN_LOAD_OPTIONAL)
#undef VIDEO_VULKAN_LOAD_OPTIONAL
    if (missing) {
        if (why) *why = std::string("the Vulkan device has no ") + missing + " (Vulkan 1.1 core is required)";
        return false;
    }
    if (core13 && missing13) {
        if (why) *why = std::string("the Vulkan device has no ") + missing13 + " (its 1.3 path needs it)";
        return false;
    }
    return true;
}

const char* ResultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default: {
        static thread_local char number[16];
        std::snprintf(number, sizeof(number), "%d", static_cast<int>(result));
        return number;
    }
    }
}

}  // namespace VideoVulkan
