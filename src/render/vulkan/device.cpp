// ============================================================================
// src/render/vulkan/device.cpp -- the Vulkan device, swap chain and frame loop
// ============================================================================
// NOT RETAIL. The DeviceData and ContextData of VideoVulkanData.h, the adapter
// enumeration (Enumerate) and the main-target attach slots. The shape follows the Metal
// backend (src/render/metal/backend.mm) and the slot roles the DX11 files
// (src/render/dx11/devicedata.cpp, contextdata.cpp); docs/vulkan/PLAN.md §4 is the design.
//
// P0 (PLAN.md §5): instance, physical device, logical device, SDL surface, swap chain and a
// frame loop (kFramesInFlight deep). P2: a frame is BeginCommands (wait for the slot, collect the retired
// objects, acquire an image, open the command buffer, reset the slot's ring and pools: draw.h),
// the draws (draw.cpp records them into the lazily-begun dynamic-rendering pass), then Finish (end
// the pass, submit the upload batch and the frame, present). The resource files are resources.h
// (memory, submission, retire queue), pipelines.h and draw.h; this file starts and stops them.
//
// Threads: BeginCommands / Clear / Finish run on whichever thread drives the frame (the
// main thread, or ext.render_pipe's render thread); the device lock (Video::Device::Lock,
// recursive) is taken in BeginCommands and dropped in Finish before the present, the DX11
// order. CreateDevice / Shutdown run on the main thread inside Engine::Startup / shutdown.
//
// Vulkan floor (PLAN.md §4.1): 1.1 with VK_KHR_swapchain and the Vulkan memory model (core in 1.2,
// VK_KHR_vulkan_memory_model before; the converted shaders declare it). A device with 1.3's
// dynamicRendering, synchronization2 and timelineSemaphore takes the 1.3 path, the one the backend is
// written for; one without any of them takes THE 1.1 PATH (vulkan11.h: render passes, vkCmdPipelineBarrier,
// fence serials, baked vertex strides, SPIR-V 1.3), chosen here from the caps. Nothing else is required;
// what is merely used when present is probed here and recorded in `caps`.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"
#include "present.h"
#include "pipelines.h"
#include "resources.h"
#include "spirv_legalize.h"
#include "vulkan11.h"
#include "vulkan_entry.h"
#include "ao/assao_pass.h"                         // P4: ext.ambient_occlusion's pass objects
#include "better_water/scene_depth_pass.h"         // ext.better_water's scene depth copy
#include "async_gpu_create/async_create.h"         // P4: ext.async_gpu_create (the loader bracket)
#include "boot_device_lost/boot_device_lost.h"     // P4: fix.boot_device_lost
#include "create_on_demand/lazy_create.h"          // P4: ext.create_on_demand
#include "share_gpu_data/share_create.h"           // P4: ext.share_gpu_data
#include "shader_rewrites.h"                       // P4: the DXBC rewrite stages

#include "compress_ao/bc4_chain.h"                 // P4: ext.compress_ao (the load's summary)
#include "etc2/etc2_chain.h"                       // P13: BC1 / BC3 to ETC2 (the load's summary)
#include "texture_quality/texture_quality.h"       // P4: ext.texture_quality (the load's summary)
#include "device_lost_drive.h"                     // BANISHED_FORCE_DEVICE_LOST=bgsave (src/diag)

#include "app/options.h"
#include "boot_window.h"   // BootWindow::ArmDeferredShow (src/app): the deferred first show
#include "stall_debug.h"   // StallDebug::Mark (src/diag): the boot timeline
#include "platform/common/cursor_image.h"            // Platform::Cursor::SetFromTexels
#include "platform/common/platform_window.h"         // Platform::Window::ShowDefault
#include "platform/common/platform_window_hooks.h"   // Platform::Window::QueryScreenForModes
#include "display_geometry.h"                        // Platform::Window::BuildResolutionList
#include "platform/sdl/window_sdl_internal.h"         // Platform::Window::Sdl::GameWindow
#include "system/Error.h"
#include "system/MemoryAllocator.h"
#include "video/Device.h"
#include "video/Context.h"
#include "video/ImageBuffer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace VideoVulkan {

namespace {

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;   // the one being recorded (the first, or a split's)
    VkCommandBuffer first = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> extra;          // SubmitFrameSoFar's continuations, reused per frame
    uint32_t extraUsed = 0;
    bool acquireWaited = false;                  // a split submit already waited on imageAvailable
    VkFence done = VK_NULL_HANDLE;               // signalled when this slot's submit completed
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
};

VkInstance instance = VK_NULL_HANDLE;
uint32_t instanceVersion = 0;   // the instance's apiVersion: the loader's, at most 1.3
VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
std::vector<VkPhysicalDevice> physicalDevices;   // the adapters Enumerate listed, in order
VkPhysicalDevice physical = VK_NULL_HANDLE;
uint32_t graphicsFamily = 0;
VkDevice device = VK_NULL_HANDLE;
VkQueue queue = VK_NULL_HANDLE;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkFormat swapFormat = VK_FORMAT_UNDEFINED;
VkExtent2D swapExtent{};     // the swap-chain images' extent (the panel's orientation when pre-rotated)
VkExtent2D renderExtent{};   // the frame's extent: the window's, landscape on a phone (present.h)
VkSurfaceTransformFlagBitsKHR swapTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
std::vector<VkImage> swapImages;
std::vector<VkImageView> swapViews;
std::vector<VkSemaphore> renderFinished;   // one per swap-chain image
Frame frames[kFramesInFlight];
unsigned frameSlot = 0;
uint32_t imageIndex = 0;
bool imageAcquired = false;
bool swapchainDirty = false;   // rebuild before the next acquire
Caps caps;
VkPhysicalDeviceProperties deviceProperties{};
int lastFullscreen = -1;
uint64_t frameCount = 0;
uint32_t mainSamples = 1;   // MainSampleCount (device_objects.h)
bool resourcesUp = false;   // ResourcesStartup / PipelinesStartup / DrawStartup ran
// The shader modules CreateShaderModule made and nothing destroyed yet (device_objects.h); Shutdown
// destroys the rest before the device. Its own lock: programs are created on the loader thread too.
std::mutex moduleLock;
std::unordered_set<VkShaderModule> liveModules;
// P4, device loss (PLAN.md §4.5; device_objects.h NoteResult): set by the first VK_ERROR_DEVICE_LOST of a
// submit, a wait, an acquire or a present; cleared when CreateDevice made the next device.
std::atomic<bool> lost{false};
bool swapchainLost = false;   // the last vkCreateSwapchainKHR answered VK_ERROR_DEVICE_LOST
bool lostFrame = false;       // BeginCommands began a frame it could not record: Finish reports the loss

// THE MOBILE LIFECYCLE (PLAN.md §3.6 / §4.5). When an Android app goes to the background its window is
// torn down by Java without waiting for a Vulkan app, so the swap chain and the surface must be gone
// by then. SDL delivers SDL_EVENT_WILL_ENTER_BACKGROUND synchronously to event watches on the thread that
// pumps events (the main thread), before it blocks that thread until the app comes back: the watch
// below waits for the frame the render thread may still be presenting (framesOpen: acquired and not yet
// presented), idles the device and destroys the swap chain and the surface. SDL_EVENT_DID_ENTER_FOREGROUND
// clears `backgrounded`; the next BeginCommands makes the surface again from the window's new native
// window, then the swap chain. Desktop platforms never send either event.
std::mutex frameLock;
std::condition_variable frameIdle;
int framesOpen = 0;
std::atomic<bool> backgrounded{false};
bool lifecycleWatch = false;

void FrameOpened() {
    std::lock_guard<std::mutex> guard(frameLock);
    ++framesOpen;
}

void FrameClosed() {
    {
        std::lock_guard<std::mutex> guard(frameLock);
        if (framesOpen > 0) --framesOpen;
    }
    frameIdle.notify_all();
}

// The SDL window the surface is made on. Device::windowHandle is the native handle (the HWND on
// Windows, retail's field); the SDL window that owns it is the one window_sdl.cpp created.
SDL_Window* GameWindow() {
    return Platform::Window::Sdl::GameWindow();
}

void DestroySwapchainObjects();

// The SDL event watch (THE MOBILE LIFECYCLE above). Main thread, inside the event pump.
bool LifecycleWatch(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
        backgrounded.store(true);
        if (!device) return true;
        {
            std::unique_lock<std::mutex> guard(frameLock);
            if (!frameIdle.wait_for(guard, std::chrono::seconds(2), [] { return framesOpen == 0; })) {
                std::fprintf(stderr, "[Vulkan] background: a frame is still open after 2 s; tearing down anyway\n");
            }
        }
        vkDeviceWaitIdle(device);
        DestroySwapchainObjects();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        imageAcquired = false;
        swapchainDirty = true;
        std::fprintf(stderr, "[Vulkan] background: swap chain and surface destroyed\n");
        // P7: Android may kill a background process without a quit, so the pipeline cache and the
        // warm list are written now too (the shutdown writes them again on a clean quit).
        if (resourcesUp) PipelinesPersist();
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        if (backgrounded.exchange(false)) {
            std::fprintf(stderr, "[Vulkan] foreground: the surface is remade at the next frame\n");
        }
    }
    return true;
}

bool Fail(const char* message) {
    std::fprintf(stderr, "[Vulkan] %s\n", message);
    System::Error::SetText(message, false);
    return false;
}

bool Fail(const std::string& message) {
    return Fail(message.c_str());
}

const char* DeviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT types,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types;
    (void)user;
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "warning";
    std::fprintf(stderr, "[Vulkan] validation %s: %s\n", level, data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

// The one instance of the process: made by the first Enumerate (the adapter list needs the
// physical devices) and kept until exit. Shutdown destroys the device, not the instance, so a
// device rebuild (Options Apply through Device::PlatformStartup's fallback) reuses it.
bool EnsureInstance(std::string* why) {
    if (instance) return true;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        *why = std::string("SDL could not load the Vulkan loader: ") + SDL_GetError();
        return false;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!LoadGlobalFunctions(gipa, why)) return false;

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loaderVersion);
    if (loaderVersion < VK_API_VERSION_1_1) {
        *why = "the Vulkan loader is older than 1.1";
        return false;
    }

    Uint32 sdlCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    if (!sdlExtensions) {
        *why = std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError();
        return false;
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlCount);
    std::vector<const char*> layers;

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
    for (const auto& e : available) {
        if (std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) caps.debugUtils = true;
    }
#if defined(_DEBUG)
    // The debug flavour validates when the layer is installed (the Vulkan SDK on a dev box,
    // the layer pushed into the app on Android); the other flavours never look for it.
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    for (const auto& l : layerProps) {
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) caps.validation = true;
    }
    if (caps.validation) layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
    if (caps.debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app = Make<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "OpenBA";
    app.applicationVersion = 1;
    app.pEngineName = "OpenBA";
    app.engineVersion = 1;
    // The highest version the backend uses, as far as the loader goes (Android 12's loader is 1.1).
    instanceVersion = std::min<uint32_t>(loaderVersion, VK_API_VERSION_1_3);
    app.apiVersion = instanceVersion;
    VkInstanceCreateInfo ci = Make<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateInstance failed: ") + ResultName(r);
        instance = VK_NULL_HANDLE;
        return false;
    }
    if (!LoadInstanceFunctions(instance, why)) return false;
    if (caps.debugUtils && vkCreateDebugUtilsMessengerEXT) {
        auto mi = Make<VkDebugUtilsMessengerCreateInfoEXT>(
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = DebugMessage;
        vkCreateDebugUtilsMessengerEXT(instance, &mi, nullptr, &messenger);
    }

    // The adapters: every physical device with a graphics queue, in enumeration order.
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> all(count);
    vkEnumeratePhysicalDevices(instance, &count, all.data());
    physicalDevices.clear();
    for (VkPhysicalDevice pd : all) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> q(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, q.data());
        bool graphics = false;
        for (const auto& f : q) {
            if (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = true;
        }
        if (graphics) physicalDevices.push_back(pd);
    }
    if (physicalDevices.empty()) {
        *why = "no Vulkan device with a graphics queue was found";
        return false;
    }
    std::fprintf(stderr, "[Vulkan] instance: loader %u.%u.%u, %zu adapter(s)%s%s\n",
                 VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion),
                 VK_API_VERSION_PATCH(loaderVersion), physicalDevices.size(),
                 caps.validation ? ", validation on" : "", caps.debugUtils ? ", debug utils" : "");
    return true;
}

bool HasDeviceExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> props(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, props.data());
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) return true;
    }
    return false;
}

// dx11 devicedata.cpp ResolveMultisampleMode (0x180004510): clamp the configuration's index to the
// adapter's list, walk down to a supported mode, write the index back when it changed, answer index + 1.
// The adapter list (Enumerate) marks only the counts the device has for colour and depth together.
uint32_t ResolveMultisampleMode(Video::Device* dev) {
    Video::Configuration* cfg = Video::Configuration::_singleton;
    if (!cfg) return 1;
    int index = cfg->multisample;
    if (index >= dev->GetMultisampleModeCount()) index = dev->GetMultisampleModeCount() - 1;
    while (index > 0 && !dev->IsMultisampleModeSupported(index))
        --index;
    if (index < 0) index = 0;   // NOT RETAIL: retail's walk has no floor (mode 0 is always supported)
    if (cfg->multisample != index) cfg->multisample = index;
    return static_cast<uint32_t>(index + 1);
}

// ---- the swap chain ---------------------------------------------------------------------------

void DestroySwapchainObjects() {
    for (VkImageView v : swapViews) {
        ForgetImageView(v);
        vkDestroyImageView(device, v, nullptr);
    }
    swapViews.clear();
    for (VkSemaphore s : renderFinished) vkDestroySemaphore(device, s, nullptr);
    renderFinished.clear();
    swapImages.clear();
}

bool CreateSwapchain(SDL_Window* window, std::string* why) {
    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &surfaceCaps);
    if (r != VK_SUCCESS) {
        *why = std::string("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: ") + ResultName(r);
        return false;
    }

    // Retail's back buffer is BGRA8 UNORM; RGBA8 when a surface has no BGRA (Android).
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    if (!formats.empty()) chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    if (chosen.format != VK_FORMAT_B8G8R8A8_UNORM) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f;
                break;
            }
        }
    }

    // VSync on: FIFO (always available). Off: mailbox, then immediate, then FIFO anyway.
    const bool vsync = Video::Configuration::_singleton ? Video::Configuration::_singleton->vsync != 0 : true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync || Opt().stress) {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, modes.data());
        for (VkPresentModeKHR m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
        }
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            for (VkPresentModeKHR m : modes) {
                if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = m;
            }
        }
    }

    VkExtent2D extent = surfaceCaps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        extent.width = std::clamp(static_cast<uint32_t>(std::max(w, 1)), surfaceCaps.minImageExtent.width,
                                  surfaceCaps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(std::max(h, 1)), surfaceCaps.minImageExtent.height,
                                   surfaceCaps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        *why = "the window has no pixels (minimised)";
        return false;
    }
    // Pre-rotation (present.h, PLAN.md §4.5). A rotated surface (a phone in landscape: ROTATE_90 or
    // ROTATE_270) gets preTransform = currentTransform and images in the panel's own orientation; the
    // frame keeps the window's landscape extent and the present pass turns it. Anything else (IDENTITY
    // on every desktop, or a mirrored transform no device here reports) presents as before.
    const VkSurfaceTransformFlagBitsKHR current = surfaceCaps.currentTransform;
    const bool rotate = (current == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
                         current == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ||
                         current == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) &&
                        (surfaceCaps.supportedTransforms & current) != 0;
    const bool quarterTurn = current == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
                             current == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    VkExtent2D frameExtent = extent;
    VkExtent2D imageExtent = extent;
    if (rotate) {
        // The images are the panel's: the window's (landscape) pixel size, turned.
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        VkExtent2D full = extent;
        if (w > 0 && h > 0) full = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
        imageExtent = quarterTurn ? VkExtent2D{ full.height, full.width } : full;
    }
    if (rotate || Platform::Window::RenderScale() != 1.0f) {
        // The frame is the engine's back buffer: the window's size in engine pixels (P7: times the
        // render scale); the present pass scales it to the images when the two differ (present.h).
        int w = 0;
        int h = 0;
        Platform::Window::Sdl::WindowSizeInEnginePixels(window, &w, &h);
        if (w > 0 && h > 0) frameExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    }

    uint32_t imageCount = std::max(kSwapchainImages, surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount != 0) imageCount = std::min(imageCount, surfaceCaps.maxImageCount);

    VkSwapchainCreateInfoKHR ci = Make<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    ci.surface = surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = imageExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = rotate ? current
                             : (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                                   ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                   : surfaceCaps.currentTransform;
    ci.compositeAlpha = (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(device, &ci, nullptr, &created);
    swapchainLost = r == VK_ERROR_DEVICE_LOST;
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateSwapchainKHR: ") + ResultName(r);
        return false;
    }
    DestroySwapchainObjects();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = created;
    swapFormat = chosen.format;
    swapExtent = imageExtent;
    renderExtent = frameExtent;
    swapTransform = rotate ? current : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    swapImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapImages.data());
    swapViews.resize(count);
    renderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        vi.image = swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device, &vi, nullptr, &swapViews[i]);
        VkSemaphoreCreateInfo si = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(device, &si, nullptr, &renderFinished[i]);
    }
    swapchainDirty = false;
    imageAcquired = false;
    DrawTargetsChanged();
    // The present pass's source follows the swap chain; at the device's first swap chain the resource
    // layer is not up yet, and CreateDevice configures it after DrawStartup.
    std::string presentWhy;
    if (resourcesUp && !PresentConfigure(swapTransform, swapFormat, renderExtent, swapExtent, &presentWhy)) {
        std::fprintf(stderr, "[Vulkan] %s\n", presentWhy.c_str());
    }
    std::fprintf(stderr,
                 "[Vulkan] swap chain %ux%u, %u images (asked %u, surface min %u), format %d, present mode %d, "
                 "transform 0x%x (current 0x%x, currentExtent %ux%u)%s\n",
                 swapExtent.width, swapExtent.height, count, imageCount, surfaceCaps.minImageCount,
                 static_cast<int>(swapFormat),
                 static_cast<int>(presentMode), static_cast<unsigned>(ci.preTransform), static_cast<unsigned>(current),
                 surfaceCaps.currentExtent.width, surfaceCaps.currentExtent.height,
                 PresentActive() ? ", the present pass draws the frame into the images (turned or scaled)" : "");
    // P12: a new native window (a trip to the background and back) is told the paced frame rate
    // again (platform_window_hooks.h; nothing to do off Android).
    Platform::Window::ReapplyPreferredFrameRate();
    return true;
}

// Options Apply (resolution / fullscreen) and a user drag-resize raise settingsChanged. The DX11
// backend answers with Dx11_DeviceData_Resize: Device::AdjustDisplay sizes the real window to the
// engine's back buffer, then the buffers are rebuilt. Here (as on Metal): size the window if the
// engine's size differs from it or fullscreen flipped, publish the REAL pixel size as the back
// buffer, then rebuild the swap chain and every dynamic target.
void ApplyEngineSettingsChange(Video::Device* dev) {
    auto w = GameWindow();
    if (!w || (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED)) return;
    auto cfg = Video::Configuration::_singleton;
    const int fs = cfg && cfg->fullscreen ? 1 : 0;
    int cw = 0;
    int ch = 0;
    Platform::Window::Sdl::WindowSizeInEnginePixels(w, &cw, &ch);
    const bool fsFlip = lastFullscreen >= 0 && lastFullscreen != fs;
    lastFullscreen = fs;
    if (fsFlip || dev->backBufferWidth != cw || dev->backBufferHeight != ch) {
        const int wantW = dev->backBufferWidth;
        const int wantH = dev->backBufferHeight;
        dev->AdjustDisplay();
        SDL_SyncWindow(w);
        Platform::Window::Sdl::WindowSizeInEnginePixels(w, &cw, &ch);
        dev->backBufferWidth = cw;
        dev->backBufferHeight = ch;
        if (cfg && !fs) {
            cfg->windowWidth = cw;
            cfg->windowHeight = ch;
        }
        std::fprintf(stderr, "[Vulkan] resize: wanted %dx%d px fullscreen=%d -> window %dx%d px\n", wantW, wantH,
                     fs, cw, ch);
    }
    if (cw > 0 && ch > 0 && device) {
        vkDeviceWaitIdle(device);
        // Options Apply may change the multisample index: the main targets follow (draw.cpp remakes
        // them through DrawTargetsChanged, which the swap-chain rebuild below calls).
        const uint32_t samples = ResolveMultisampleMode(dev);
        if (samples != mainSamples) {
            std::fprintf(stderr, "[Vulkan] multisample %ux -> %ux\n", mainSamples, samples);
            mainSamples = samples;
        }
        // Apply also changes shadowScalar without changing the window: recreate every dynamic
        // image / render buffer so the engine's extents agree with the new back buffer.
        dev->ReleaseDynamic();
        dev->RecreateDynamic();
        std::string why;
        if (!CreateSwapchain(w, &why)) {
            std::fprintf(stderr, "[Vulkan] swap chain rebuild failed: %s\n", why.c_str());
            swapchainDirty = true;
        }
    }
}

// BANISHED_FORCE_DEVICE_LOST (src/app/options.h): dx11 contextdata.cpp's forced device-lost drive, on
// this backend's present. `<n>` injects VK_ERROR_DEVICE_LOST on present #n, `bgsave` on the first present
// after a background save starts (src/diag/device_lost_drive.h); once per process. One count for the
// faithful Finish and the render thread's packets (dx11 keeps two; with ext.render_pipe on from boot every
// present is a packet's anyway). The device is not lost: the recovery's teardown and rebuild are real.
bool ForceDeviceLostThisPresent() {
    static int wanted = -2;   // -2 = not yet read, -1 = disabled, -3 = on request (`bgsave`)
    static int64_t frame = 0;
    static bool fired = false;
    if (wanted == -2) {
        const char* v = Opt().forceDeviceLost;
        wanted = DeviceLostDrive::Armed() ? -3 : (v && *v) ? std::atoi(v) : -1;
        if (wanted > 0) {
            std::fprintf(stderr, "[DeviceLost] ARMED: a synthetic VK_ERROR_DEVICE_LOST will be injected on present "
                                 "#%d (Vulkan)\n", wanted);
        }
        if (wanted == -3) {
            std::fprintf(stderr, "[DeviceLost] ARMED: a synthetic VK_ERROR_DEVICE_LOST will be injected on the first "
                                 "present after a background save starts (Vulkan)\n");
        }
    }
    ++frame;
    if (wanted == -3) {
        if (fired || !DeviceLostDrive::TakeRequest()) return false;
        fired = true;
        return true;
    }
    if (wanted <= 0 || fired || frame < wanted) return false;
    fired = true;
    return true;
}

void DestroyFrames() {
    for (Frame& f : frames) {
        f.extra.clear();   // freed with the pool
        if (f.done) vkDestroyFence(device, f.done, nullptr);
        if (f.imageAvailable) vkDestroySemaphore(device, f.imageAvailable, nullptr);
        if (f.pool) vkDestroyCommandPool(device, f.pool, nullptr);
        f = Frame{};
    }
}

bool CreateFrames(std::string* why) {
    for (Frame& f : frames) {
        auto pi = Make<VkCommandPoolCreateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = graphicsFamily;
        if (vkCreateCommandPool(device, &pi, nullptr, &f.pool) != VK_SUCCESS) {
            *why = "vkCreateCommandPool failed";
            return false;
        }
        auto ai = Make<VkCommandBufferAllocateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &ai, &f.first) != VK_SUCCESS) {
            *why = "vkAllocateCommandBuffers failed";
            return false;
        }
        f.commands = f.first;
        VkFenceCreateInfo fi = Make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fi, nullptr, &f.done);
        VkSemaphoreCreateInfo si = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(device, &si, nullptr, &f.imageAvailable);
    }
    return true;
}

}  // namespace

// ---- the entry points (vulkan_entry.h) ----------------------------------------------------------

::Video::DeviceData* NewDeviceData(::Video::Device* dev) {
    return new DeviceData(dev);
}

bool DeviceLive() {
    return device != VK_NULL_HANDLE;
}

// ---- the live device (device_objects.h) ---------------------------------------------------------

VkDevice LiveDevice() {
    return device;
}

VkPhysicalDevice LivePhysical() {
    return physical;
}

VkQueue LiveQueue() {
    return queue;
}

uint32_t LiveGraphicsFamily() {
    return graphicsFamily;
}

const VkPhysicalDeviceProperties& LiveProperties() {
    return deviceProperties;
}

const Caps& LiveCaps() {
    return caps;
}

VkInstance LiveInstance() {
    return instance;
}

VkFormat DepthStencilFormat() {
    return caps.d24s8 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat SwapchainFormat() {
    return swapFormat;
}

uint32_t MainSampleCount() {
    return mainSamples;
}

VkCommandBuffer SubmitFrameSoFar() {
    Frame& f = frames[frameSlot];
    vkEndCommandBuffer(f.commands);
    FlushUploads();
    VkSemaphoreSubmitInfo wait = Make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    wait.semaphore = f.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const uint64_t serial = SubmitCommands(f.commands, f.acquireWaited ? nullptr : &wait, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE);
    f.acquireWaited = true;
    WaitSerial(serial);
    if (f.extraUsed == f.extra.size()) {
        auto ai = Make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &ai, &cb);
        f.extra.push_back(cb);
    }
    f.commands = f.extra[f.extraUsed++];
    VkCommandBufferBeginInfo bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.commands, &bi);
    return f.commands;
}

// ---- device objects (device_objects.h) ----------------------------------------------------------

void NoteResult(VkResult r, const char* what) {
    if (r != VK_ERROR_DEVICE_LOST) return;
    if (!lost.exchange(true)) {
        std::fprintf(stderr, "[DeviceLost] %s: VK_ERROR_DEVICE_LOST: the device-lost recovery runs at the frame's "
                             "Finish\n", what);
        std::fflush(stderr);
    }
}

bool DeviceLost() {
    return lost.load();
}

VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, std::string* why) {
    std::lock_guard<std::mutex> guard(moduleLock);
    if (device == VK_NULL_HANDLE) {
        if (why) *why = "no live device";
        return VK_NULL_HANDLE;
    }
    // The 1.1 path takes SPIR-V 1.3 (spirv_legalize.h DowngradeSpirv): the cache keeps the 1.6 module.
    const std::vector<uint32_t>* code = &spirv;
    std::vector<uint32_t> downgraded;
    if (Vulkan11Path()) {
        downgraded = spirv;
        if (!Shaders::DowngradeSpirv(&downgraded, why)) return VK_NULL_HANDLE;
        code = &downgraded;
    }
    auto ci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    ci.codeSize = code->size() * sizeof(uint32_t);
    ci.pCode = code->data();
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult r = vkCreateShaderModule(device, &ci, nullptr, &module);
    if (r != VK_SUCCESS) {
        if (why) *why = std::string("vkCreateShaderModule: ") + ResultName(r);
        return VK_NULL_HANDLE;
    }
    liveModules.insert(module);
    return module;
}

void DestroyShaderModule(VkShaderModule module) {
    if (module == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> guard(moduleLock);
    if (liveModules.erase(module) == 0 || device == VK_NULL_HANDLE) return;
    vkDestroyShaderModule(device, module, nullptr);
}

// One GraphicsInterface with one Adapter per physical device (dxgiAdapterIndex = its index in
// physicalDevices; CreateDevice reads it back from Device::dxgiAdapterIndex). The mode list is
// the usable screen's, as on Metal: there is no DXGI output to ask.
bool Enumerate(Video::Device* dev, const char* name) {
    using D = Video::Device;
    using A = System::MemoryAllocator;
    std::string why;
    if (!EnsureInstance(&why)) {
        std::fprintf(stderr, "[Vulkan] no adapters listed: %s\n", why.c_str());
        return false;
    }
    auto old = dev->interfaceData;
    const int n = dev->interfaceCount;
    auto list = static_cast<D::GraphicsInterface*>(A::MemAlloc(sizeof(D::GraphicsInterface) * (n + 1), 4));
    for (int i = 0; i < n; ++i) {
        new (list + i) D::GraphicsInterface(old[i]);
        old[i].~GraphicsInterface();
    }
    A::MemFree(old);
    new (list + n) D::GraphicsInterface();
    auto& g = list[n];
    g.name.Set(name);
    const int adapterCount = static_cast<int>(physicalDevices.size());
    g.adapterData = static_cast<D::Adapter*>(A::MemAlloc(sizeof(D::Adapter) * adapterCount, 4));
    g.adapterCount = g.adapterCapacity = adapterCount;

    int mw = 0;
    int mh = 0;
    int nw = 0;
    int nh = 0;
    Platform::Window::QueryScreenForModes(&mw, &mh, &nw, &nh);
    int32_t sizes[32][2];
    const int modeCount = Platform::Window::BuildResolutionList(mw, mh, nw, nh, sizes, 32);
    const D::RefreshRate rate{ 60, 1 };

    for (int i = 0; i < adapterCount; ++i) {
        auto& a = *new (g.adapterData + i) D::Adapter();
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
        a.name.Set(props.deviceName);
        a.dxgiAdapterIndex = i;
        a.dxgiOutputIndex = 0;
        // dx11: 16. P8 round 2: 0 (the Options' combo then lists no anisotropic level) when the device
        // lacks the samplerAnisotropy feature: maxSamplerAnisotropy alone does not say it can be used.
        auto features = Make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        vkGetPhysicalDeviceFeatures2(physicalDevices[i], &features);
        a.maxFilterLevel = features.features.samplerAnisotropy
                               ? std::min(16, static_cast<int>(props.limits.maxSamplerAnisotropy))
                               : 0;
        // Array<bool>: multisampleData[k] is "k+1 samples works" for k = 0..7.
        a.multisampleData = static_cast<uint8_t*>(A::MemAlloc(8, 4));
        std::memset(a.multisampleData, 0, 8);
        a.multisampleData[0] = 1;
        a.multisampleCount = a.multisampleCap = 8;
        const VkSampleCountFlags samples =
            props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        for (unsigned s : { 2u, 4u, 8u }) {
            if (samples & static_cast<VkSampleCountFlags>(s)) a.multisampleData[s - 1] = 1;
        }
        for (int m = 0; m < modeCount; ++m) {
            D::DisplayMode mode{};
            mode.width = sizes[m][0];
            mode.height = sizes[m][1];
            a.FindOrAddMode(mode, rate);
        }
    }
    dev->interfaceData = list;
    dev->interfaceCount = dev->interfaceCapacity = n + 1;
    return true;
}

// ---- DeviceData ---------------------------------------------------------------------------------

bool DeviceData::EnumerateInterfaces() {
    return Enumerate(_owner, "Vulkan");
}

bool DeviceData::CreateDevice() {
    ::Video::Device* dev = _owner;
    StallDebug::Mark("  Vulkan CreateDevice: entry");
    std::string why;
    if (!EnsureInstance(&why)) return Fail("Vulkan: " + why);

    // 1 -- the adapter the configuration picked (Adapter::dxgiAdapterIndex, copied to the device).
    int adapterIndex = dev->dxgiAdapterIndex;
    if (adapterIndex < 0 || adapterIndex >= static_cast<int>(physicalDevices.size())) adapterIndex = 0;
    physical = physicalDevices[static_cast<size_t>(adapterIndex)];
    vkGetPhysicalDeviceProperties(physical, &deviceProperties);
    caps.apiVersion = deviceProperties.apiVersion;
    // The version the device is used at: its own, capped by the instance's.
    const uint32_t usable = std::min(deviceProperties.apiVersion, instanceVersion);
    if (usable < VK_API_VERSION_1_1) {
        return Fail(std::string("Vulkan: ") + deviceProperties.deviceName + " reports Vulkan " +
                    std::to_string(VK_API_VERSION_MAJOR(deviceProperties.apiVersion)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(deviceProperties.apiVersion)) +
                    "; OpenBA's Vulkan renderer needs 1.1");
    }

    // 2 -- the path. The 1.3 path needs dynamic rendering, synchronization2 and timeline semaphores (1.3
    // core features, still optional bits); a device without them takes the 1.1 path (vulkan11.h). The
    // 1.2 / 1.3 feature structures are valid only on a device used at 1.3, so a 1.1 device leaves them 0.
    auto have13 = Make<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    auto have12 = Make<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    auto have11 = Make<VkPhysicalDeviceVulkan11Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    have13.pNext = &have12;
    have12.pNext = &have11;
    auto have2 = Make<VkPhysicalDeviceFeatures2>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    if (usable >= VK_API_VERSION_1_3) have2.pNext = &have13;
    vkGetPhysicalDeviceFeatures2(physical, &have2);
    caps.vulkan11 = !have13.dynamicRendering || !have13.synchronization2 || !have12.timelineSemaphore;
    // The 1.1 path's own needs, from the 1.1-valid structures: the Vulkan memory model (every converted
    // module declares it: core in 1.2, VK_KHR_vulkan_memory_model before), shaderDrawParameters, and,
    // when present, bufferDeviceAddress (modules declare PhysicalStorageBufferAddresses; core in 1.2,
    // VK_KHR_buffer_device_address before).
    auto haveDraw = Make<VkPhysicalDeviceShaderDrawParametersFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES);
    auto haveMemoryModel = Make<VkPhysicalDeviceVulkanMemoryModelFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES);
    auto haveAddress = Make<VkPhysicalDeviceBufferDeviceAddressFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
    const bool memoryModelExtension = caps.vulkan11 && usable < VK_API_VERSION_1_2 &&
                                      HasDeviceExtension(physical, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
    const bool addressExtension = caps.vulkan11 && usable < VK_API_VERSION_1_2 &&
                                  HasDeviceExtension(physical, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    if (caps.vulkan11) {
        auto probe = Make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        probe.pNext = &haveDraw;
        void** next = &haveDraw.pNext;
        if (memoryModelExtension || usable >= VK_API_VERSION_1_2) {
            *next = &haveMemoryModel;
            next = &haveMemoryModel.pNext;
        }
        if (addressExtension || usable >= VK_API_VERSION_1_2) *next = &haveAddress;
        vkGetPhysicalDeviceFeatures2(physical, &probe);
        if (!haveMemoryModel.vulkanMemoryModel) {
            return Fail("Vulkan: the device lacks the Vulkan memory model (VK_KHR_vulkan_memory_model), which "
                        "the converted shaders declare");
        }
    }
    SetVulkan11Path(caps.vulkan11);
    if (!HasDeviceExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return Fail("Vulkan: the device has no VK_KHR_swapchain");
    }
    caps.pushDescriptor = HasDeviceExtension(physical, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (caps.pushDescriptor) {
        auto push = Make<VkPhysicalDevicePushDescriptorPropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR);
        auto props2 = Make<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        props2.pNext = &push;
        vkGetPhysicalDeviceProperties2(physical, &props2);
        caps.maxPushDescriptors = push.maxPushDescriptors;
    }
    caps.vertexInputDynamicState =
        HasDeviceExtension(physical, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
    caps.extendedDynamicState2 = HasDeviceExtension(physical, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
    caps.loadStoreOpNone = HasDeviceExtension(physical, VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME) ||
                           HasDeviceExtension(physical, VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME);
    {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
        caps.d24s8 = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    }
    caps.msrtss = false;
    // Not on the 1.1 path: its render passes have no VkMultisampledRenderToSingleSampledInfoEXT.
    if (!caps.vulkan11 &&
        HasDeviceExtension(physical, VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME)) {
        auto msrtss = Make<VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT);
        auto probe = Make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        probe.pNext = &msrtss;
        vkGetPhysicalDeviceFeatures2(physical, &probe);
        caps.msrtss = msrtss.multisampledRenderToSingleSampled != 0;
    }
    caps.colorSamples = deviceProperties.limits.framebufferColorSampleCounts;
    caps.fragmentStores = have2.features.fragmentStoresAndAtomics != 0;
    caps.storageWriteWithoutFormat = have2.features.shaderStorageImageWriteWithoutFormat != 0;
    {
        // P4: ASSAO's targets (R16F / RG8 / R32F colour attachments sampled, RGBA8 storage) and the main
        // depth sampled (ao/assao_pass.cpp). Optimal tiling, the features each one is used with.
        const auto has = [&](VkFormat f, VkFormatFeatureFlags need) {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(physical, f, &fp);
            return (fp.optimalTilingFeatures & need) == need;
        };
        const VkFormatFeatureFlags target = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        const VkFormat depth = caps.d24s8 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
        caps.aoFormats = has(VK_FORMAT_R16_SFLOAT, target | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
                         has(VK_FORMAT_R8G8_UNORM, target | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
                         has(VK_FORMAT_R32_SFLOAT, target) &&
                         has(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
                         has(depth, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    }
    caps.samplerAnisotropy = have2.features.samplerAnisotropy != 0;
    caps.fillModeNonSolid = have2.features.fillModeNonSolid != 0;
    caps.textureCompressionBC = have2.features.textureCompressionBC != 0;
    caps.textureCompressionETC2 = have2.features.textureCompressionETC2 != 0;
    caps.etc2Transcode = false;
    if (!caps.textureCompressionBC && caps.textureCompressionETC2) {
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        caps.etc2Transcode = true;
        for (const VkFormat f : { VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,
                                  VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK }) {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(physical, f, &fp);
            if ((fp.optimalTilingFeatures & need) != need) caps.etc2Transcode = false;
        }
    }

    // 3 -- the graphics queue family (it must also present to the surface, checked after the surface).
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, families.data());
    graphicsFamily = qCount;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            break;
        }
    }
    if (graphicsFamily == qCount) return Fail("Vulkan: no graphics queue family");
    StallDebug::Mark("  Vulkan adapter + features");

    // 4 / 5 -- the back buffer from the selected mode, then the windowed override (dx11 steps 4-5).
    ::Video::Configuration* cfg = Video::Configuration::_singleton;
    if (dev->modeData && dev->displayModeIndex >= 0 && dev->displayModeIndex < dev->modeCount) {
        dev->backBufferWidth = dev->modeData[dev->displayModeIndex].width;
        dev->backBufferHeight = dev->modeData[dev->displayModeIndex].height;
    }
    if (cfg && cfg->fullscreen == 0) {
        if (cfg->windowWidth >= 0) dev->backBufferWidth = cfg->windowWidth;
        if (cfg->windowHeight >= 0) dev->backBufferHeight = cfg->windowHeight;
    }
    if (Opt().background) {
        dev->backBufferWidth = 640;
        dev->backBufferHeight = 360;
    }
    auto w = GameWindow();
    if (!w) return Fail("Vulkan: no game window");

    // 6 -- the deferred first show (src/app/boot_window.cpp) or the shipped inline ShowWindow.
    BootWindow::ArmDeferredShow(cfg, dev->windowHandle);
    if (!cfg || cfg->field42 == 0) {
        Platform::Window::ShowDefault(dev->windowHandle);
        StallDebug::Mark("  Vulkan ShowWindow  <== WINDOW NOW VISIBLE");
    }
    // 7 -- no DXGI output: the monitor stays unset and AdjustDisplay pins fullscreen to the origin.
    dev->monitor = nullptr;
    // 8 -- settle the window to the back buffer, then publish the real pixel size.
    dev->AdjustDisplay();
    SDL_SyncWindow(w);
    Platform::Window::Sdl::WindowSizeInEnginePixels(w, &dev->backBufferWidth, &dev->backBufferHeight);
    lastFullscreen = cfg && cfg->fullscreen ? 1 : 0;
    StallDebug::Mark("  Vulkan AdjustDisplay (window sized)");

    // 9 -- the surface, and the queue's right to present to it.
    if (!SDL_Vulkan_CreateSurface(w, instance, nullptr, &surface)) {
        surface = VK_NULL_HANDLE;
        return Fail(std::string("Vulkan: SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    }
    VkBool32 presentable = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical, graphicsFamily, surface, &presentable);
    if (!presentable) return Fail("Vulkan: the graphics queue cannot present to the window");
    if (!lifecycleWatch) {
        lifecycleWatch = SDL_AddEventWatch(&LifecycleWatch, nullptr);
    }

    // 10 -- the device.
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = Make<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qi.queueFamilyIndex = graphicsFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char*> extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (caps.pushDescriptor) extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    // P7: VK_EXT_vertex_input_dynamic_state is probed (the log line) but not enabled: no pipeline uses its
    // state, and Qualcomm's Adreno guidance is to leave it off (docs/android/PERF_P7.md §2 row 11).
    if (caps.extendedDynamicState2) extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
    if (caps.msrtss) extensions.push_back(VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME);
    if (memoryModelExtension) extensions.push_back(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
    if (addressExtension && haveAddress.bufferDeviceAddress) {
        extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }
    auto want13 = Make<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    want13.dynamicRendering = VK_TRUE;
    want13.synchronization2 = VK_TRUE;
    // The converted shaders (dxbc-spirv) rely on these when present: maintenance4 lets a vertex output
    // be wider than the pixel input it feeds (D3D's register linking), shaderDrawParameters backs the
    // DrawParameters capability (SV_InstanceID's base). Validation flags their absence.
    want13.maintenance4 = have13.maintenance4;
    want13.shaderDemoteToHelperInvocation = have13.shaderDemoteToHelperInvocation;   // D3D discard
    auto want11 = Make<VkPhysicalDeviceVulkan11Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    want11.shaderDrawParameters = have11.shaderDrawParameters;
    auto want12 = Make<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    want12.timelineSemaphore = have12.timelineSemaphore;
    want12.bufferDeviceAddress = have12.bufferDeviceAddress;
    want12.vulkanMemoryModel = have12.vulkanMemoryModel;
    want12.vulkanMemoryModelDeviceScope = have12.vulkanMemoryModelDeviceScope;
    want13.pNext = &want12;
    auto wantEds2 = Make<VkPhysicalDeviceExtendedDynamicState2FeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT);
    wantEds2.extendedDynamicState2 = VK_TRUE;
    auto wantMsrtss = Make<VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT);
    wantMsrtss.multisampledRenderToSingleSampled = VK_TRUE;
    want12.pNext = &want11;
    // The 1.1 path chains the 1.1-valid structures instead of the 1.1 / 1.2 / 1.3 ones (which a device
    // used at 1.1 does not know, and which may not be chained together with these).
    auto wantDraw = Make<VkPhysicalDeviceShaderDrawParametersFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES);
    wantDraw.shaderDrawParameters = haveDraw.shaderDrawParameters;
    auto wantMemoryModel = Make<VkPhysicalDeviceVulkanMemoryModelFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES);
    wantMemoryModel.vulkanMemoryModel = VK_TRUE;
    wantMemoryModel.vulkanMemoryModelDeviceScope = haveMemoryModel.vulkanMemoryModelDeviceScope;
    wantDraw.pNext = &wantMemoryModel;
    auto wantAddress = Make<VkPhysicalDeviceBufferDeviceAddressFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
    wantAddress.bufferDeviceAddress = VK_TRUE;
    void** tail = caps.vulkan11 ? &wantMemoryModel.pNext : &want11.pNext;
    if (caps.vulkan11 && haveAddress.bufferDeviceAddress) {
        *tail = &wantAddress;
        tail = &wantAddress.pNext;
    }
    if (caps.extendedDynamicState2) {
        *tail = &wantEds2;
        tail = &wantEds2.pNext;
    }
    if (caps.msrtss) {
        *tail = &wantMsrtss;
        tail = &wantMsrtss.pNext;
    }
    auto want2 = Make<VkPhysicalDeviceFeatures2>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    want2.pNext = caps.vulkan11 ? static_cast<void*>(&wantDraw) : static_cast<void*>(&want13);
    want2.features.samplerAnisotropy = have2.features.samplerAnisotropy;
    want2.features.textureCompressionBC = have2.features.textureCompressionBC;
    want2.features.textureCompressionETC2 = have2.features.textureCompressionETC2;
    want2.features.fillModeNonSolid = have2.features.fillModeNonSolid;
    want2.features.independentBlend = have2.features.independentBlend;
    want2.features.depthClamp = have2.features.depthClamp;
    want2.features.shaderClipDistance = have2.features.shaderClipDistance;
    // P4: the AO pass's normals UAV (device_objects.h Caps::fragmentStores).
    want2.features.fragmentStoresAndAtomics = have2.features.fragmentStoresAndAtomics;
    want2.features.shaderStorageImageWriteWithoutFormat = have2.features.shaderStorageImageWriteWithoutFormat;
    VkDeviceCreateInfo di = Make<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    di.pNext = &want2;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    di.ppEnabledExtensionNames = extensions.data();
    VkResult r = vkCreateDevice(physical, &di, nullptr, &device);
    if (r != VK_SUCCESS) {
        device = VK_NULL_HANDLE;
        return Fail(std::string("Vulkan: vkCreateDevice: ") + ResultName(r));
    }
    if (!LoadDeviceFunctions(device, !caps.vulkan11, &why)) return Fail("Vulkan: " + why);
    vkGetDeviceQueue(device, graphicsFamily, 0, &queue);
    StallDebug::Mark("  Vulkan vkCreateDevice");

    // 11 -- the frames, the main targets' sample count, and the swap chain.
    if (!CreateFrames(&why)) return Fail("Vulkan: " + why);
    mainSamples = ResolveMultisampleMode(dev);
    if (!CreateSwapchain(w, &why)) return Fail("Vulkan: " + why);
    frameSlot = 0;
    frameCount = 0;
    dev->settingsChanged = 0;
    StallDebug::Mark("  Vulkan swap chain");

    // 12 -- the resource layer (P2): memory and submission, the pipeline layout and cache, the draw state.
    if (!ResourcesStartup(&why)) return Fail("Vulkan: " + why);
    resourcesUp = true;
    if (!PipelinesStartup(&why)) return Fail("Vulkan: " + why);
    if (!DrawStartup(&why)) return Fail("Vulkan: " + why);
    if (!PresentStartup(&why)) return Fail("Vulkan: " + why);
    if (!PresentConfigure(swapTransform, swapFormat, renderExtent, swapExtent, &why)) {
        return Fail("Vulkan: " + why);
    }
    if (PresentActive()) {
        std::fprintf(stderr, "[Vulkan] present pass: frame %ux%u drawn into %ux%u (transform 0x%x)\n",
                     renderExtent.width, renderExtent.height, swapExtent.width, swapExtent.height,
                     static_cast<unsigned>(swapTransform));
    }
    StallDebug::Mark("  Vulkan resources");
    // P4: a device made: a device-lost recovery is over, and ext.create_on_demand's deferred creates may
    // run on it (the table was paused while there was none).
    lost.store(false);
    swapchainLost = false;
    lostFrame = false;
    CreateOnDemand::Resume();

    std::fprintf(stderr,
                 "[Vulkan] device %s (%s) api %u.%u.%u driver 0x%x created; path %s; push_descriptor=%d (max %u) "
                 "vertex_input_dynamic=%d (not enabled) eds2=%d load_store_none=%d d24s8=%d msrtss=%d validation=%d "
                 "ao=%d/%d/%d bc=%d etc2=%d (transcode %d); multisample %ux\n",
                 deviceProperties.deviceName, DeviceTypeName(deviceProperties.deviceType),
                 VK_API_VERSION_MAJOR(caps.apiVersion), VK_API_VERSION_MINOR(caps.apiVersion),
                 VK_API_VERSION_PATCH(caps.apiVersion), deviceProperties.driverVersion,
                 caps.vulkan11 ? "1.1 (render passes, pipeline barriers, fence serials, SPIR-V 1.3)" : "1.3",
                 caps.pushDescriptor,
                 caps.maxPushDescriptors, caps.vertexInputDynamicState, caps.extendedDynamicState2, caps.loadStoreOpNone, caps.d24s8,
                 caps.msrtss, caps.validation, caps.fragmentStores, caps.storageWriteWithoutFormat, caps.aoFormats,
                 caps.textureCompressionBC, caps.textureCompressionETC2, caps.etc2Transcode, mainSamples);
    return true;
}

void DeviceData::Shutdown() {
    // P4: no deferred create runs against the device going away (ext.async_gpu_create's queued jobs
    // finish first; ext.create_on_demand's table pauses: a record that meets no device stays recorded).
    AsyncCreate::Drain();
    CreateOnDemand::Pause();
    if (device) {
        vkDeviceWaitIdle(device);
        if (resourcesUp) {
            // The extensions' backend objects (P4: the AO pass, the rewritten programs, the share table),
            // the draw state, the pipelines (their cache is written here), then everything the companions
            // still hold (resources.h); the shader modules after them, below.
            Assao::Shutdown();
            SceneDepth::Shutdown();
            Rewrites::Shutdown();
            ShareGpuData::Clear();
            PresentShutdown();
            DrawShutdown();
            PipelinesShutdown();
            ResourcesShutdown();
            Vulkan11Shutdown();   // the 1.1 path's render passes (its framebuffers went with the retire queue)
            resourcesUp = false;
        }
        DestroySwapchainObjects();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        DestroyFrames();
        {
            // Programs still alive at shutdown: their modules die with the device (device_objects.h).
            std::lock_guard<std::mutex> guard(moduleLock);
            if (!liveModules.empty()) {
                std::fprintf(stderr, "[Vulkan] shutdown: %zu shader module(s) still held, destroyed with the device\n",
                             liveModules.size());
            }
            for (VkShaderModule module : liveModules)
                vkDestroyShaderModule(device, module, nullptr);
            liveModules.clear();
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
    }
    if (surface) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    physical = VK_NULL_HANDLE;
    imageAcquired = false;
    swapchainDirty = false;
    // The instance (and its messenger) stay for the process: Enumerate's adapter list and a
    // device rebuild reuse them.
}

// The loader bracket (dx11 devicedata_stubs.cpp Slot4): retail's body is empty. NOT RETAIL, the dx11
// body's extension work (P4): `true` arms ext.async_gpu_create for the loader thread, `false` waits for
// its deferred creates and disarms it, then the load-end summaries of ext.compress_ao, the ETC2 transcode
// (docs/android/P13_MEM.md), ext.share_gpu_data,
// ext.create_on_demand and ext.texture_quality, and the backend's own program summary (companions.cpp).
void DeviceData::Slot4(bool loading) {
    if (loading) {
        AsyncCreate::Begin();
        return;
    }
    AsyncCreate::End();
    // P13 (P13_MEM.md §9): the ETC2 transcodes of the textures this load queued run before it ends.
    if (LiveCaps().etc2Transcode) CreateOnDemand::FinishPrefetches();
    ::CompressAo::LoadEnd();
    Etc2Chain::LoadEnd();
    ShareGpuData::LoadEnd();
    CreateOnDemand::LoadEnd();
    ::TextureQuality::PrintIfChanged("load end (run total)");
    LogProgramSummary();
}

float DeviceData::Slot5() {
    return 0.0f;
}

bool DeviceData::Slot6() {
    return false;
}

bool DeviceData::Slot7() {
    return true;
}

// Retail slot 8 (dx11 devicedata.cpp 0x180005450): the game cursor from a 32x32 ImageBuffer, rebuilt
// only when (image, hotspot) change. The texels are read back under the device lock as retail does; the
// SDL platform builds the cursor from them with retail's 1-bit classification (Platform::Cursor::
// SetFromTexels, the Metal backend's route).
void DeviceData::SetImageBuffer(Video::ImageBuffer* imageBuffer, int hotX, int hotY) {
    if (imageBuffer == cursorImage && hotX == cursorHotX && hotY == cursorHotY) return;
    uint32_t texels[32 * 32];
    _owner->Lock();
    const bool ok = imageBuffer->CopyFromTexture(texels, 32, 32);
    _owner->Unlock();
    if (!ok) {
        std::fprintf(stderr, "[Vulkan] cursor image %p not readable: the platform cursor stays\n",
                     static_cast<void*>(imageBuffer));
        return;
    }
    Platform::Cursor::SetFromTexels(texels, hotX, hotY);
    if (!cursorImage) {
        std::fprintf(stderr, "[Vulkan] cursor set from image %p (format %d, hotspot %d,%d)\n",
                     static_cast<void*>(imageBuffer), static_cast<int>(imageBuffer->_format), hotX, hotY);
    }
    cursorImage = imageBuffer;
    cursorHotX = hotX;
    cursorHotY = hotY;
}

Video::ContextData* DeviceData::CreateContextData(Video::Context* owner) {
    return new ContextData(owner);
}
Video::ImageBufferData* DeviceData::CreateImageBufferData(Video::ImageBuffer* owner) {
    return new ImageBufferData(owner);
}
Video::ConstantBufferData* DeviceData::CreateConstantBufferData(Video::ConstantBuffer* owner) {
    return new ConstantBufferData(owner);
}
Video::IndexBufferData* DeviceData::CreateIndexBufferData(Video::IndexBuffer* owner) {
    return new IndexBufferData(owner);
}
Video::VertexBufferData* DeviceData::CreateVertexBufferData(Video::VertexBuffer* owner) {
    return new VertexBufferData(owner);
}
Video::VertexLayoutData* DeviceData::CreateVertexLayoutData(Video::VertexLayout* owner) {
    return new VertexLayoutData(owner);
}
Video::VertexProgramData* DeviceData::CreateVertexProgramData(Video::VertexProgram* owner) {
    return new VertexProgramData(owner);
}
Video::PixelProgramData* DeviceData::CreatePixelProgramData(Video::PixelProgram* owner) {
    return new PixelProgramData(owner);
}
Video::RenderStateData* DeviceData::CreateRenderStateData(Video::RenderState* owner) {
    return new RenderStateData(owner);
}
Video::VideoProgramData* DeviceData::CreateVideoProgramData(Video::VideoProgram* owner) {
    return new VideoProgramData(owner);
}
Video::RenderBufferData* DeviceData::CreateRenderBufferData(Video::RenderBuffer* owner) {
    return new RenderBufferData(owner);
}

// ---- ContextData: the frame loop ----------------------------------------------------------------

namespace {

// P4, device loss: a frame on a lost device is begun with nothing to record into (every draw, clear and
// pass is skipped: draw.cpp records only inside DrawBeginFrame / DrawEndFrame), the device lock taken as
// for any frame; its Finish reports the loss and the recovery runs where DX11 runs it. Refusing the
// frame instead (return false) would spin: the loop asks again and again, and no Finish ever runs.
bool BeginLostFrame(::Video::Device* dev, ::Video::Context* ctx) {
    dev->Lock();
    lostFrame = true;
    imageAcquired = false;
    ctx->SetRenderBuffer(ctx->_defaultRenderBuffer);
    Video::Viewport vp{};
    vp.width = dev->backBufferWidth;
    vp.height = dev->backBufferHeight;
    ctx->SetViewport(vp);
    return true;
}

}  // namespace

// Slot 1 (dx11 contextdata.cpp 0x180001650). A settings change is answered and the frame refused,
// as retail does (Dx11_DeviceData_Resize, then `return false`); the next frame begins normally.
bool ContextData::BeginCommands() {
    ::Video::Context* const ctx = _owner;
    ::Video::Device* const dev = ctx->_device;
    if (!device) return false;

    ctx->_trianglesSubmitted = 0;
    ctx->_verticesSubmitted = 0;
    ctx->_drawsSubmitted = 0;
    ctx->_objectsSubmitted = 0;

    if (dev->settingsChanged != 0) {
        dev->Lock();
        ApplyEngineSettingsChange(dev);
        dev->settingsChanged = 0;
        dev->Unlock();
        return false;
    }
    if (lost.load()) return BeginLostFrame(dev, ctx);
    auto w = GameWindow();
    if (backgrounded.load()) return false;   // THE MOBILE LIFECYCLE: no surface while in the background
    if (!surface) {
        // Back from the background: a new surface on the window's new native window, then a swap chain.
        if (!SDL_Vulkan_CreateSurface(w, instance, nullptr, &surface)) {
            surface = VK_NULL_HANDLE;
            return false;   // the native window is not there yet: try again next frame
        }
        VkBool32 presentable = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, graphicsFamily, surface, &presentable);
        std::fprintf(stderr, "[Vulkan] foreground: surface remade (presentable=%u)\n", presentable);
        swapchainDirty = true;
    }
    if (swapchainDirty) {
        vkDeviceWaitIdle(device);
        std::string why;
        if (!CreateSwapchain(w, &why)) {
            // FIX SEAM (NOT RETAIL, boot_device_lost/boot_device_lost.h): with fix.boot_device_lost on, a
            // rebuild that failed on a lost device enters the device-lost recovery (this frame's Finish).
            // Row off (and under the faithful profile): retried every frame, as DX11 retries its reset.
            if (swapchainLost && BootDeviceLost::On()) {
                std::fprintf(stderr, "[DeviceLost] swap-chain rebuild failed on a lost device: device-lost recovery\n");
                NoteResult(VK_ERROR_DEVICE_LOST, "vkCreateSwapchainKHR (fix.boot_device_lost)");
                return BeginLostFrame(dev, ctx);
            }
            return false;   // minimised, or the surface is gone: try again next frame
        }
    }

    Frame& f = frames[frameSlot];
    NoteResult(vkWaitForFences(device, 1, &f.done, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    if (lost.load()) return BeginLostFrame(dev, ctx);
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE,
                                       &imageIndex);
    if (r == VK_ERROR_DEVICE_LOST) {
        NoteResult(r, "vkAcquireNextImageKHR");
        return BeginLostFrame(dev, ctx);
    }
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        swapchainDirty = true;
        return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) {
        swapchainDirty = true;   // present this frame, rebuild before the next
    } else if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkAcquireNextImageKHR: %s\n", ResultName(r));
        return false;
    }
    imageAcquired = true;
    FrameOpened();
    vkResetFences(device, 1, &f.done);
    vkResetCommandPool(device, f.pool, 0);
    f.commands = f.first;
    f.extraUsed = 0;
    f.acquireWaited = false;
    VkCommandBufferBeginInfo bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.commands, &bi);

    dev->Lock();
    // The slot's previous frame completed (the fence above): its ring, pools and the objects
    // released before it can go.
    CollectRetired();
    FrameTarget target;
    target.cb = f.commands;
    target.slot = frameSlot;
    if (PresentActive()) {
        // present.h: the frame renders into the landscape source; Finish turns it into the swap image.
        target.image = PresentSourceImage();
        target.view = PresentSourceView();
        target.extent = renderExtent;
        target.presentImage = swapImages[imageIndex];
        target.presentView = swapViews[imageIndex];
        target.presentExtent = swapExtent;
    } else {
        target.image = swapImages[imageIndex];
        target.view = swapViews[imageIndex];
        target.extent = swapExtent;
    }
    DrawBeginFrame(target);
    ctx->SetRenderBuffer(ctx->_defaultRenderBuffer);
    Video::Viewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = dev->backBufferWidth;
    vp.height = dev->backBufferHeight;
    ctx->SetViewport(vp);
    return true;
}

void ContextData::EndCommands() {
}

// Slot 3 (dx11 contextdata.cpp 0x1800017c0): present, then the device-lost recovery when the frame
// found the device lost (PresentFrame, then RecoverLostDevice: the two halves ext.render_pipe runs on
// two threads, Ext::RenderPipe::FinishPresent / FinishRecover in integration.cpp).
void ContextData::Finish() {
    ::Video::Device* const dev = _owner->_device;
    const int32_t result = PresentFrame(*_owner);
    // The recovery destroys this Context (MainLoop::OnVideoError): nothing of `this` is read after it.
    if (result != 0) RecoverLostDevice(*dev, result);
}

// Finish steps 1-4 (vulkan_entry.h): end the pass and move the image to PRESENT_SRC (draw.cpp), drop the
// device lock, submit the upload batch and then the frame (resources.h: one queue, the timeline),
// present. VK_ERROR_DEVICE_LOST from any of them, or from the frame's begin (a lost frame), is the
// result; 0 otherwise (an out-of-date swap chain is rebuilt at the next begin, not a loss).
int32_t PresentFrame(::Video::Context& ctx) {
    ::Video::Device* const dev = ctx._device;
    if (lostFrame) {
        lostFrame = false;
        dev->Unlock();
        return static_cast<int32_t>(VK_ERROR_DEVICE_LOST);
    }
    if (!device || !imageAcquired) {
        dev->Unlock();
        return 0;
    }
    Frame& f = frames[frameSlot];
    DrawEndFrame();
    vkEndCommandBuffer(f.commands);
    const bool logFrame = frameCount == 0 || (frameCount + 1) % 300 == 0;
    if (logFrame) DrawLogFrame(frameCount + 1, &ctx);

    // The DX11 order: the device lock goes before the present, so a loader thread linking a new
    // resource never waits out the presentation.
    dev->Unlock();

    FlushUploads();
    VkSemaphoreSubmitInfo wait = Make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    wait.semaphore = f.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const uint64_t serial =
        SubmitCommands(f.commands, f.acquireWaited ? nullptr : &wait, renderFinished[imageIndex], f.done);
    NoteFrameSubmitted(serial);

    VkPresentInfoKHR pi = Make<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    VkResult r = PresentQueue(pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        swapchainDirty = true;
    } else if (r == VK_ERROR_DEVICE_LOST) {
        NoteResult(r, "vkQueuePresentKHR");
    } else if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkQueuePresentKHR: %s\n", ResultName(r));
    }
    imageAcquired = false;
    FrameClosed();
    frameSlot = (frameSlot + 1) % kFramesInFlight;
    ++frameCount;
    if (ForceDeviceLostThisPresent()) {
        std::fprintf(stderr, "[DeviceLost] INJECTING VK_ERROR_DEVICE_LOST (the real present returned %s)\n",
                     ResultName(r));
        std::fflush(stderr);
        NoteResult(VK_ERROR_DEVICE_LOST, "injected (BANISHED_FORCE_DEVICE_LOST)");
    }
    return lost.load() ? static_cast<int32_t>(VK_ERROR_DEVICE_LOST) : 0;
}

// Finish step 5 (vulkan_entry.h), dx11 contextdata.cpp RecoverLostDevice's sequence: the notify
// (Device+0x50, MainLoop::OnVideoError: every video External destructed, the Context destroyed),
// ReleaseDynamic, DeviceData Shutdown + CreateDevice, then RecreateDynamic + the restore notify
// (Device+0x58), or the error text when no device could be made. Main thread, no frame in progress.
void RecoverLostDevice(::Video::Device& device, int32_t result) {
    ::Video::DeviceData* const ddBase = device.deviceData;
    ::Video::Device* const owner = ddBase->_owner;
    void (*const onLost)() = owner->onVideoError;
    std::fprintf(stderr, "[DeviceLost] result=%s onLost=%p -> %s\n", ResultName(static_cast<VkResult>(result)),
                 reinterpret_cast<void*>(onLost), onLost ? "calling" : "*** NULL, notification SKIPPED ***");
    std::fflush(stderr);
    if (onLost) onLost();
    // NOT RETAIL (ext.async_gpu_create): creates queued against the lost device finish first.
    AsyncCreate::Drain();
    owner->ReleaseDynamic();
    ddBase->Shutdown();
    const bool created = ddBase->CreateDevice();
    if (!created) {
        System::Error::SetText("Vulkan Error: VK_ERROR_DEVICE_LOST!", true);
        System::Error::Display();
        return;
    }
    owner->RecreateDynamic();
    void (*const onRestored)() = owner->onVideoRestart;
    std::fprintf(stderr, "[DeviceLost] recreate OK; onRestored=%p -> %s\n", reinterpret_cast<void*>(onRestored),
                 onRestored ? "calling" : "*** NULL, notification SKIPPED ***");
    std::fflush(stderr);
    if (onRestored) onRestored();
    std::fprintf(stderr, "[DeviceLost] device-lost round trip COMPLETE\n");
    std::fflush(stderr);
}

// ---- the main targets ---------------------------------------------------------------------------
// The default render buffer's colour and depth images (and every other image aliased to them, as
// FrameCopyImageBuffer's) call these: the companion stamps the owner's descriptor exactly as dx11
// imagebufferdata.cpp AttachColorBuffer / AttachDepthBuffer (0x180002580 / 0x180002650) do, in
// their field order -- the back-buffer size, 2D, kind 0 / 1, format 5 (BGRA8) / 7 (D24S8), CLAMP
// addressing, POINT filter, flags 0x10 / 0x20 -- and marks itself a main target: the draws aim at
// the backend's main targets (draw.cpp), and a Bind of either as a texture reads the placeholder.
// The flags matter: Device::RecreateDynamic re-attaches an image by bit 4 / bit 5 after a settings
// change (dynamic_resources.cpp); without them the default depth came back as a plain depth image.

void ImageBufferData::AttachColorBuffer() {
    mainTarget = MainTarget::Color;
    auto dev = Video::Device::_singleton;
    Video::ImageBuffer* const ib = _owner;
    ib->_width = dev->backBufferWidth;
    ib->_height = dev->backBufferHeight;
    ib->_dimension = 0;
    ib->_kind = 0;
    ib->_format = 5;
    ib->_addressW = 1;
    ib->_addressV = 1;
    ib->_addressU = 1;
    ib->_filter = 0;
    ib->_flags = 0x10;
}

void ImageBufferData::AttachDepthBuffer() {
    mainTarget = MainTarget::Depth;
    auto dev = Video::Device::_singleton;
    Video::ImageBuffer* const ib = _owner;
    ib->_width = dev->backBufferWidth;
    ib->_height = dev->backBufferHeight;
    ib->_dimension = 0;
    ib->_kind = 1;
    ib->_format = 7;
    ib->_addressW = 1;
    ib->_addressV = 1;
    ib->_addressU = 1;
    ib->_filter = 0;
    ib->_flags = 0x20;
}

// dx11 0x180001070: OMSetRenderTargets(colour, depth). Here the render buffer becomes the draw target;
// the pass on it begins lazily at its first draw or flushed clear (draw.h).
void RenderBufferData::Bind(void* context) {
    (void)context;
    DrawSetRenderBuffer(_owner);
}

// dx11 0x1800010e0: ResolveSubresource FROM `other`'s colour INTO this one's; the depth buffers and
// the flag are not touched (retail's gap, kept). Here a copy (a blit where the formats differ) in the
// frame's command stream (draw.cpp DrawResolve): the main colour is already single-sampled when it is
// read (resolved by its passes, or the tiler's own MSAA).
void RenderBufferData::Resolve(void* context, Video::RenderBuffer* other, bool flag) {
    (void)context;
    (void)flag;
    DrawResolve(_owner, other);
}

}  // namespace VideoVulkan
