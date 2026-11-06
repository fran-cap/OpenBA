// ============================================================================
// src/render/vulkan/instance_bank.cpp -- instance_bank.h, and InstanceBank:: (src/render/common)
// ============================================================================
// NOT RETAIL. Issue #253: the wide instancing batches of ext.draw_batch on this backend.
// ============================================================================
#include "instance_bank.h"

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "vulkan_entry.h"   // DeviceLive

#include "draw_batch/instance_bank.h"   // src/render/common: the extension's view
#include "math/Math.h"
#include "video/Context.h"

#include <algorithm>
#include <cstring>

namespace VideoVulkan {

int32_t LiveInstanceCap() {
    if (kVulkanInstanceCap == kRetailTransformSlots) return kRetailTransformSlots;
    if (!DeviceLive()) return kRetailTransformSlots;
    const uint32_t limit = LiveProperties().limits.maxUniformBufferRange;
    const int32_t fits = static_cast<int32_t>(limit / kTransformBytes) / kRetailTransformSlots * kRetailTransformSlots;
    return std::max(kRetailTransformSlots, std::min(kVulkanInstanceCap, fits));
}

uint32_t ConstantBufferRange() {
    const uint32_t bank = static_cast<uint32_t>(LiveInstanceCap() * kTransformBytes);
    return std::max(kBaseConstantRange, bank);
}

}  // namespace VideoVulkan

namespace InstanceBank {

int32_t Cap() {
    return VideoVulkan::LiveInstanceCap();
}

void SetWide(Video::Context& context, int32_t slot, const Math::Matrix& m) {
    auto* data = static_cast<VideoVulkan::ContextData*>(context._contextData);
    const size_t at = static_cast<size_t>(slot) * sizeof(Math::Matrix);
    if (data->wideBank.size() < at + sizeof(Math::Matrix)) {
        data->wideBank.resize(static_cast<size_t>(VideoVulkan::kVulkanInstanceCap) * sizeof(Math::Matrix));
    }
    std::memcpy(data->wideBank.data() + at, &m, sizeof(Math::Matrix));
    // SetTransform's tail: the dirty bit, and the high-water mark outside any change test. A wide bank is
    // uploaded at every bind anyway (ContextData::BindTransforms), so no compare is kept here.
    context._dirty |= RtVideo::kDirtyTransformConstants;
    if (context._transformCount < slot + 1) {
        context._transformCount = slot + 1;
    }
}

}  // namespace InstanceBank
