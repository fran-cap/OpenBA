#pragma once
// ============================================================================
// src/render/metal/inline_bytes.h -- small per-draw uploads without a buffer
// ============================================================================
// NOT RETAIL. Metal copies up to 4 KB handed to setVertexBytes / setFragmentBytes into the
// command stream itself, so a small per-draw upload (a constant block, a few vertices) needs
// no MTLBuffer at all. Apple's guidance is to use them for anything under 4 KB and a buffer
// (a ring slot, an upload page) above that. These helpers apply the limit in one place and
// say which path was taken, so a caller falls back to its buffer only when it must:
//
//   if (!VideoMetal::SetVertexBytesIfSmall(enc, data, size, index)) {
//       // too big for the command stream: bind a buffer instead
//   }
//
// Used by backend.mm (the expanded-vertex path).
// Include after VideoMetalData.h (which holds the Metal headers behind the Carbon rename).
// ============================================================================

#include "VideoMetalData.h"

namespace VideoMetal {

// Metal's documented limit for setVertexBytes / setFragmentBytes.
constexpr NSUInteger kInlineBytesLimit = 4096;

inline bool FitsInlineBytes(NSUInteger length) {
    return length > 0 && length <= kInlineBytesLimit;
}

// True when `length` bytes were bound to the vertex stage at buffer index `index`.
// False (nothing bound) when the block is empty or over the 4 KB limit.
inline bool SetVertexBytesIfSmall(id<MTLRenderCommandEncoder> encoder, const void* bytes, NSUInteger length,
                                  NSUInteger index) {
    if (!bytes || !FitsInlineBytes(length)) {
        return false;
    }
    [encoder setVertexBytes:bytes length:length atIndex:index];
    return true;
}

// Same for the fragment stage.
inline bool SetFragmentBytesIfSmall(id<MTLRenderCommandEncoder> encoder, const void* bytes, NSUInteger length,
                                    NSUInteger index) {
    if (!bytes || !FitsInlineBytes(length)) {
        return false;
    }
    [encoder setFragmentBytes:bytes length:length atIndex:index];
    return true;
}

}  // namespace VideoMetal
