// ============================================================================
// src/ext/draw_batch/batch_fill.cpp -- batch_fill.h
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWBATCH.md.
// ============================================================================
#include "ext/draw_batch/batch_fill.h"

#include <cstring>

namespace Ext {
namespace DrawBatch {

bool RetailAccepts(int32_t vertexCapacityBytes, int64_t vertexCount, int32_t stride, int64_t indexCount) {
    const int64_t capacity = vertexCapacityBytes;
    if (capacity <= vertexCount * stride) return false;
    if (indexCount * 2 >= capacity) return false;   // sic: the vertex buffer's capacity, as retail
    return true;
}

bool RunFits(int32_t vertexCapacityBytes, int32_t stride, int64_t runVertices, int64_t runIndices,
             int64_t vertices, int64_t indices) {
    const int64_t v = runVertices + vertices;
    const int64_t i = runIndices + indices;
    if (v > kMaxRunVertices) return false;
    return RetailAccepts(vertexCapacityBytes, v, stride, i);
}

int64_t FillRun(const Span* spans, int32_t count, int32_t stride, uint8_t* vb, uint16_t* ib) {
    uint32_t base    = 0;
    int64_t  written = 0;
    for (int32_t s = 0; s < count; ++s) {
        const Span& span = spans[s];
        const size_t bytes = static_cast<size_t>(span.vertexCount) * static_cast<size_t>(stride);
        std::memcpy(vb, span.vertices, bytes);
        vb += bytes;
        for (int32_t k = 0; k < span.indexCount; ++k) {
            ib[k] = static_cast<uint16_t>(span.indices[k] + base);
        }
        ib += span.indexCount;
        written += span.indexCount;
        base += static_cast<uint32_t>(span.vertexCount);
    }
    return written;
}

bool JoinsBatch(const InstanceKey& head, const InstanceKey& next) {
    if (next.instance != head.instance) return false;
    if (next.listIndex != head.listIndex) return false;
    if (next.userData != head.userData) return false;
    return next.animationHandle == head.animationHandle;
}

int32_t WideBatchCap(int32_t arg8, int32_t backendCap) {
    if (arg8 != kRetailInstanceSlots) return arg8;
    if (backendCap <= arg8) return arg8;
    return backendCap / arg8 * arg8;
}

bool ContinuesPastCut(const InstanceKey& head, const InstanceKey& cut, const InstanceKey* afterCut) {
    if (!head.wideOk || !cut.wideOk) return false;
    if (!JoinsBatch(head, cut)) return false;
    if (cut.maxInstances != head.maxInstances) return false;
    if (cut.opacity != head.opacity || cut.opacityPrev != head.opacityPrev) return false;
    if (afterCut == nullptr) return false;
    return JoinsBatch(cut, *afterCut);
}

int64_t WideBatchesOfChain(int64_t retailBatches, int32_t perWide) {
    if (retailBatches <= 0) return 0;
    if (perWide <= 1) return retailBatches;
    return (retailBatches + perWide - 1) / perWide;
}

}  // namespace DrawBatch
}  // namespace Ext
