#pragma once
// ============================================================================
// src/platform/common/platform_touch.h -- finger events for an extension (NOT RETAIL)
// ============================================================================
// Retail reads no touch: on a phone SDL's touch-to-mouse emulation turns the FIRST finger into
// the left button (docs/android/TOUCH_PLACEMENT_IDEAS.md §1). ext.touch_pointer
// (docs/android/P11_TOUCH_POINTER.md) takes the fingers itself: with a sink set the emulation is
// off (SDL_HINT_TOUCH_MOUSE_EVENTS 0), every finger event goes to the sink and nothing else sees
// it, and a real mouse's events are reported to the sink as well (they still reach the game: last
// device wins). With no sink: the emulation is on and nothing is observed (retail's path).
// The body is shared by every SDL platform (src/platform/sdl/touch_sdl.cpp); no SDL type here.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Touch {

enum class Phase : int32_t {
    Down = 0,
    Move = 1,
    Up = 2,
    Cancel = 3,
    MouseActivity = 4,   // a real mouse moved, clicked or scrolled (finger, x, y unused)
};

struct Event {
    Phase phase = Phase::Move;
    uint64_t finger = 0;   // SDL's finger id, unique while the finger is down
    float x = 0.0f;        // client engine pixels (the render scale applied), as the cursor's
    float y = 0.0f;
};

using Sink = void (*)(const Event& event);

// Main thread. Null restores the emulation.
void SetSink(Sink sink);

// Engine pixels (the cursor's unit) per pixel of the screen: the render scale (1 but on a phone at a
// lower render scale), so a size measured in screen pixels can be turned into cursor pixels.
float EnginePixelsPerDevicePixel();

}  // namespace Touch
}  // namespace Platform
