#pragma once
// ============================================================================
// src/render/metal/draw_bounds.h -- the byte ranges a Metal draw reads from the engine's buffers
// ============================================================================
// NOT RETAIL (the Metal backend has no retail counterpart). Pure C++, no Metal types, so the unit
// tests check it (tests/unit/metal_draw_bounds_test.cpp). Draw (backend.mm) asks two questions
// before it reads or hands Metal a range:
//
//   * WidenCount: how many vertices of a UChar4 attribute the int4 widening may read from a bound
//     vertex stream (`begin` bytes into a buffer of `length`, `stride` bytes a vertex, the attribute
//     `offset` bytes into each vertex).
//   * IndexRange: whether an indexed draw's 16-bit index range lies inside the bound index buffer,
//     and its byte offset.
//
// The layout offset, the start index, the index count and the index-buffer offset all come from
// resource data (VertexLayout / mesh TriangleList records), so a modded or crafted file controls them.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace VideoMetal {

// 0 when nothing may be read. The four UChar4 bytes must lie inside one vertex (offset + 4 <= stride):
// then vertex n - 1's last byte, begin + (n - 1) * stride + offset + 3, is below begin + n * stride, which
// the count keeps inside the buffer. A negative layout offset arrives wrapped (an NSUInteger) and fails
// the same test.
inline size_t WidenCount(size_t length, size_t begin, size_t stride, size_t offset) {
    if (!stride || begin >= length) {
        return 0;
    }
    if (stride < 4 || offset > stride - 4) {
        return 0;
    }
    return (length - begin) / stride;
}

// False (and *byteOffset untouched) for a negative offset, start or count, or a range past the end.
// Compared without a sum that can wrap.
inline bool IndexRange(int indexOffset, int start, int count, size_t length, size_t* byteOffset) {
    if (indexOffset < 0 || start < 0 || count < 0) {
        return false;
    }
    const uint64_t offset = uint64_t(indexOffset) + uint64_t(start) * 2;
    const uint64_t bytes = uint64_t(count) * 2;
    if (offset > length || bytes > length - offset) {
        return false;
    }
    *byteOffset = size_t(offset);
    return true;
}

}  // namespace VideoMetal
