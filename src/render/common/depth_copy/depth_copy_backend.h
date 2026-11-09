#pragma once
// ============================================================================
// src/render/common/depth_copy/depth_copy_backend.h -- THE SHARED DEPTH COPY: one R32F copy of the main
// depth per frame for every reader that needs one
// ============================================================================
// NOT RETAIL, and no extension row: plumbing under three rows, identical output. No graphics-API types:
// the rule below is backend-neutral and unit-tested (tests/unit/depth_copy_rule_test.cpp); each backend
// that has the readers implements the copy (src/render/dx11/depth_copy/, src/render/vulkan/depth_copy/).
// A backend without them (Metal: none of the three passes) needs none of it.
//
// THE READERS. Three passes read the main depth as a single-sampled float (0 near .. 1 far, the
// hardware value; sample 0 of a multisampled depth, the depth itself when single-sampled):
//   ext.ambient_occlusion   the AO seam, right after the Opaque list (renderer_ext_ao.h)
//   ext.better_clouds       the cloud shadow, the ground-draw seam, right after the AO seam
//   ext.better_water        the contact foam's scene depth, the frame-copy seam before the water draws
// Each used to make its own copy, three full-resolution copies (and, on Vulkan, three moves of the
// main depth to SHADER_READ_ONLY and back) of a depth that had not changed in between: the draws
// between the AO seam and the water's frame copy (the cloud shadow, the decals and the rest of the
// Blended list before the water) test the depth and never write it (RenderDoc, the stress town, both
// backends: the main depth's bytes are identical at the three points).
//
// THE RULE. The first reader of a frame makes the copy; a later reader reuses it while it is still the
// main depth: the same frame, the same depth (size, sample count, device), and no engine draw that
// could write the main depth and no depth clear since it was made. The backends count those writes
// (a draw whose state tests and writes depth into a pass with the main depth attached; every depth
// clear): a mod's Blended material that writes depth before the water simply makes the water copy
// again, as it always did. Each reader still works alone: with the others off it makes the copy
// itself, at its own point, exactly as before.
// ============================================================================

#include <cstdint>

namespace DepthCopyBackend {

// What a copy was made of, and the main depth as it stands when a reader asks.
struct Stamp {
    uint64_t frame = ~0ull;        // the frame being recorded (~0: outside a frame)
    uint64_t depthWrites = 0;      // the backend's count of main-depth writes this frame
    const void* depth = nullptr;   // the main depth (the backend's image / texture), compared only
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t samples = 0;
};

// True when a copy made at `made` is still the main depth at `now`.
inline bool Fresh(const Stamp& made, const Stamp& now) {
    return made.frame != ~0ull && made.frame == now.frame && made.depthWrites == now.depthWrites &&
           made.depth != nullptr && made.depth == now.depth && made.width == now.width &&
           made.height == now.height && made.samples == now.samples;
}

}  // namespace DepthCopyBackend
