#pragma once
// ============================================================================
// src/ext/depth_of_field/dof_params.h -- ext.depth_of_field's look (NOT RETAIL)
// ============================================================================
// The defaults the pass draws with (src/render/common/dof/dof.hlsl has what each does). The debug tweak
// overlay moves a copy of them (Ext::Tweak::DofValues, debug flavour only); every other build reads these.
// ============================================================================

namespace Ext {
namespace DepthOfField {

// Where the focus is.
enum class FocusMode : int {
    CameraTarget = 0,   // the player camera's target (its `distance`), times focusScale: the default
    ScreenCentre = 1,   // the depth at the centre of the screen, read by the pass
    Manual = 2,         // focusDistance (world units from the camera)
};

struct Look {
    float gain = 1.01f;         // the CoC per unit of (d - f) / d, before the clamp to the max
    float maxCoc = 5.1f;        // the largest blur radius, half-resolution pixels at 1080 lines
    float band = 0.217f;        // the in-focus band, in (d - f) / d
    float boost = 2.0f;         // the highlight boost: a bright tap weighs up to 1 + boost in the gather
    float threshold = 0.8f;     // the luma where the boost starts (0..1; it is full at 1)
    int focusMode = static_cast<int>(FocusMode::CameraTarget);
    float focusScale = 0.74f;   // CameraTarget: the target's distance times this (< 1 focuses nearer)
    float focusDistance = 60.0f;   // Manual: the focus, world units from the camera
    bool showCoc = false;       // debug: the blur map over the picture (red far, blue near, clear in focus)
};

}  // namespace DepthOfField
}  // namespace Ext
