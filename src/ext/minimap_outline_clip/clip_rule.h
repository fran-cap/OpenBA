// ============================================================================
// src/ext/minimap_outline_clip/clip_rule.h -- fix.minimap_outline_clip: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §31. Standard C++ only, so tests/unit/ext_minimap_outline_clip_test.cpp
// links it without the world.
//
// Retail draws the minimap's camera outline with four SpriteContext::AddLine calls inside a scissor set to
// the minimap rect, and AddSpriteDraw's scissor cull reads a line's two endpoints as a rectangle's min and
// max corners: a side drawn toward a corner left of or above the minimap is dropped whole. With the row on,
// FrustumOutlineElement::Draw clips each side to the rect first (Liang-Barsky), so both endpoints lie inside
// it and the cull keeps the line; a side wholly outside the rect is not drawn (retail draws nothing visible
// there either). The cull itself, shared by every sprite, is unchanged.
// ============================================================================
#pragma once

namespace Ext {
namespace MinimapOutlineClip {

// Clips the segment (x0, y0) - (x1, y1) to [left, right] x [top, bottom] in place. False when no part of it
// is inside (the endpoints are then unchanged).
inline bool ClipSegment(float& x0, float& y0, float& x1, float& y1, float left, float top, float right,
                        float bottom) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float t0 = 0.0f;
    float t1 = 1.0f;
    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { x0 - left, right - x0, y0 - top, bottom - y0 };
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return false;
            }
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (t > t1) {
                return false;
            }
            if (t > t0) {
                t0 = t;
            }
        } else {
            if (t < t0) {
                return false;
            }
            if (t < t1) {
                t1 = t;
            }
        }
    }
    const float sx = x0;
    const float sy = y0;
    x0 = sx + t0 * dx;
    y0 = sy + t0 * dy;
    x1 = sx + t1 * dx;
    y1 = sy + t1 * dy;
    return true;
}

}  // namespace MinimapOutlineClip
}  // namespace Ext
