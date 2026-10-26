// ============================================================================
// src/platform/sdl/touch_sdl.cpp -- Platform::Touch over SDL3 (NOT RETAIL)
// ============================================================================
// platform_touch.h. The pump (pump_sdl.cpp) offers every event to ObservePumpEvent first; with a
// sink set the finger events are converted to client engine pixels (the window's pixel size times
// the render scale, the unit of the cursor GetCursorPos reports) and consumed. A mouse event whose
// device is not SDL's touch mouse is reported as activity and left for the game.
// ============================================================================

#include "platform_touch.h"
#include "window_sdl_internal.h"
#include "platform_window_hooks.h"   // RenderScale (P7)

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_touch.h>

namespace Platform {
namespace Touch {

namespace {
Sink g_sink = nullptr;
}  // namespace

void SetSink(Sink sink) {
    if (sink == g_sink) return;
    g_sink = sink;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, sink != nullptr ? "0" : "1");
}

float EnginePixelsPerDevicePixel() {
    return Window::RenderScale();
}

namespace Sdl {

// pump_sdl.cpp: true when the event was a finger's and went to the sink.
bool ObservePumpEvent(const SDL_Event& ev) {
    if (g_sink == nullptr) return false;
    Event out;
    switch (ev.type) {
    case SDL_EVENT_FINGER_DOWN:
        out.phase = Phase::Down;
        break;
    case SDL_EVENT_FINGER_MOTION:
        out.phase = Phase::Move;
        break;
    case SDL_EVENT_FINGER_UP:
        out.phase = Phase::Up;
        break;
    case SDL_EVENT_FINGER_CANCELED:
        out.phase = Phase::Cancel;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (ev.motion.which != SDL_TOUCH_MOUSEID) {
            out.phase = Phase::MouseActivity;
            g_sink(out);
        }
        return false;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev.button.which != SDL_TOUCH_MOUSEID) {
            out.phase = Phase::MouseActivity;
            g_sink(out);
        }
        return false;
    case SDL_EVENT_MOUSE_WHEEL:
        if (ev.wheel.which != SDL_TOUCH_MOUSEID) {
            out.phase = Phase::MouseActivity;
            g_sink(out);
        }
        return false;
    default:
        return false;
    }
    // A mouse turned into touches by SDL (SDL_HINT_MOUSE_TOUCH_EVENTS) is the mouse, not a finger.
    if (ev.tfinger.touchID == SDL_MOUSE_TOUCHID) return true;
    int w = 0;
    int h = 0;
    Window::Sdl::WindowSizeInEnginePixels(Window::Sdl::GameWindow(), &w, &h);
    out.finger = static_cast<uint64_t>(ev.tfinger.fingerID);
    out.x = ev.tfinger.x * static_cast<float>(w);
    out.y = ev.tfinger.y * static_cast<float>(h);
    g_sink(out);
    return true;
}

}  // namespace Sdl
}  // namespace Touch
}  // namespace Platform
