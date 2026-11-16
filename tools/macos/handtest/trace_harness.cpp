// ============================================================================
// tools/macos/handtest/trace_harness.cpp -- a fake "owner" for the hands-on test pipeline
// ============================================================================
// Links the SHIPPING src/diag/darwin/input_trace_darwin.cpp (the logger) and src/platform/sdl/pump_sdl.cpp
// with src/platform/darwin/input_darwin.cpp (the Ctrl+Cmd+F chord) on SDL's DUMMY video driver (no window
// on screen, no sound, no focus change), then performs the whole hands-on checklist with synthetic events:
//   mouse to the four window edges, a click, a middle-drag and an Option+drag, wheel up and
//   down, keys W A S D Q E F1 Space, Right-Option+Up, Ctrl+Cmd+F twice, a resize, Cmd+Q.
// The trace it writes is what analyze.py is tested against (run_pipeline_test.sh).
//   usage: trace_harness <out-trace-file>
// ============================================================================
#include "input_trace.h"
#include "platform_window_hooks.h"
#include "platform_window.h"
#include "window_messages.h"
#include "window_sdl_internal.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

extern "C" {
bool SDL_SetKeyboardFocus(SDL_Window* window);
bool SDL_SendKeyboardKey(Uint64 timestamp, SDL_KeyboardID keyboardID, int rawcode, SDL_Scancode scancode, bool down);
}

static SDL_Window* g_win = nullptr;
namespace Platform {
namespace Window {
namespace Sdl {
SDL_Window* GameWindow() { return g_win; }
}  // namespace Sdl
void* NativeHandle() { return g_win; }
}  // namespace Window
}  // namespace Platform

// The Darwin window hook display_sdl.cpp calls (window_darwin.cpp is not linked here): identity.
namespace Platform {
namespace Window {
void FitToUsableScreen(SDL_Window*, const FrameBorders&, int32_t*, int32_t*, int32_t*, int32_t*) {
}
}  // namespace Window
}  // namespace Platform

static bool g_quit = false;
static int64_t Handler(void*, uint32_t, uint64_t, int64_t) { return 0; }
static void Pump() {
    if (Platform::Pump(Handler)) g_quit = true;
}

static void Move(float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(g_win);
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
    Pump();
    SDL_Delay(60);   // > the logger's 50 ms limit
}
static void Button(int b, bool down, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(g_win);
    e.button.button = (Uint8)b;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
    Pump();
}
static void Wheel(float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = SDL_GetWindowID(g_win);
    e.wheel.y = y;
    SDL_PushEvent(&e);
    Pump();
}
// A key as the OS driver delivers it: SDL's own entry point updates the state AND queues the event.
static void Key(SDL_Scancode sc, bool down) {
    SDL_SendKeyboardKey(0, 0, 0, sc, down);
    Pump();
}
static void Tap(SDL_Scancode sc) {
    Key(sc, true);
    SDL_Delay(30);
    Key(sc, false);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: trace_harness <out-trace-file>\n");
        return 2;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    g_win = SDL_CreateWindow("trace harness", 640, 360, SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        std::printf("window: %s\n", SDL_GetError());
        return 2;
    }
    SDL_SetKeyboardFocus(g_win);
    std::remove(argv[1]);
    if (!Diag::InputTrace::Install(argv[1])) {
        std::printf("Install failed\n");
        return 2;
    }
    SDL_PumpEvents();
    Pump();
    SDL_Delay(1100);   // one S line of the starting state

    const float W = 640, H = 360;
    // 1. edges (SDL clamps a real cursor to the last point: W-1, H-1)
    Move(300, 180);
    Move(0, 180);
    Move(W - 1, 180);
    Move(300, 0);
    Move(300, H - 1);
    Move(300, 180);
    SDL_Delay(1100);
    Pump();
    // 2. click, middle drag, option drag, wheel
    Button(1, true, 100, 20);
    Button(1, false, 100, 20);
    Button(2, true, 320, 180);
    Move(340, 190);
    Button(2, false, 340, 190);
    Key(SDL_SCANCODE_LALT, true);
    Button(1, true, 320, 180);
    Move(330, 185);
    Button(1, false, 330, 185);
    Key(SDL_SCANCODE_LALT, false);
    Wheel(1.0f);
    Wheel(-1.0f);
    // 3. keys
    SDL_Scancode keys[] = { SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_F1, SDL_SCANCODE_SPACE };
    for (SDL_Scancode k : keys)
        Tap(k);
    Key(SDL_SCANCODE_RALT, true);
    Tap(SDL_SCANCODE_UP);
    Key(SDL_SCANCODE_RALT, false);
    // 4. Ctrl+Cmd+F twice (the real pump chord toggles SDL full screen)
    for (int i = 0; i < 2; ++i) {
        Key(SDL_SCANCODE_LCTRL, true);
        Key(SDL_SCANCODE_LGUI, true);
        Key(SDL_SCANCODE_F, true);
        SDL_SyncWindow(g_win);
        SDL_PumpEvents();
        Pump();
        SDL_Delay(1100);
        Pump();
        Key(SDL_SCANCODE_F, false);
        Key(SDL_SCANCODE_LGUI, false);
        Key(SDL_SCANCODE_LCTRL, false);
    }
    // 5. resize
    SDL_SetWindowSize(g_win, 800, 450);
    SDL_SyncWindow(g_win);
    SDL_PumpEvents();
    Pump();
    SDL_Delay(1100);
    Pump();
    // 6. Cmd+Q: the key, then the quit the app menu sends
    Key(SDL_SCANCODE_LGUI, true);
    Key(SDL_SCANCODE_Q, true);
    Key(SDL_SCANCODE_Q, false);
    SDL_Event q;
    SDL_zero(q);
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    Pump();
    Key(SDL_SCANCODE_LGUI, false);
    Diag::InputTrace::Shutdown();
    std::printf("harness done, quit seen by pump: %d\n", g_quit ? 1 : 0);
    return 0;
}
