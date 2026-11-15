#pragma once
// ============================================================================
// src/render/vulkan/vk_mem.h -- the Vulkan Memory Allocator, configured for this backend
// ============================================================================
// NOT RETAIL. VMA (third_party/VulkanMemoryAllocator, GPUOpen, MIT; THIRD_PARTY_NOTICES.md) is a
// single header: every file of the backend that names a VMA type includes it through here, so the
// configuration below is the same in every translation unit (it changes VmaVulkanFunctions' layout).
// The implementation is compiled once, in vma_impl.cpp.
//
// Nothing links the Vulkan loader (vk_api.h): VMA's static and dynamic function fetching are both
// off, and ResourcesStartup (resources.cpp) hands it the backend's own run-time resolved pointers.
// The API version is pinned at 1.3, the highest the backend uses, whatever the Vulkan-Headers submodule
// declares; ResourcesStartup asks VMA for 1.1 on the 1.1 path (vulkan11.h).
// ============================================================================
#include "vk_api.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>
