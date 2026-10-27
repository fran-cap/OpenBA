// ============================================================================
// src/platform/sdl/pump_sdl.cpp -- Platform::Pump: the message pump over SDL3
// ============================================================================
// Phase D.4 SDL step W3 (plan §A.2, §E rules 5 and 9).
//
// RETAIL (Application::PlatformUpdate, 0x18009fdf0; MainLoop::Frame's inlined copy,
// 0x1800dd204..0x1800dd282):
//     if (_quitRequested) PostQuitMessage(0);
//     while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
//         TranslateMessage(&msg); DispatchMessageW(&msg);
//         if (msg.message == WM_QUIT) { <quit>; break; }
//     }
//
// WHAT SDL DOES DIFFERENTLY
//  * SDL owns the WndProc. Win32 SENT messages (WM_SIZE, WM_ACTIVATE, WM_SETFOCUS,
//    WM_SETCURSOR) reached retail's WindowProc synchronously, inside whatever call caused
//    them; SDL queues an event and Pump dispatches the equivalent tuple here, at the next
//    pump. The tuples are retail's (src/platform/common/window_messages.h); only the
//    moment moves.
//  * WM_QUIT: Win32's PostQuitMessage does not queue a message, it raises a flag that
//    PeekMessage turns into ONE WM_QUIT once the queue holds nothing else, however often
//    it was called. Platform::Window::PostQuitMessage raises the same flag and Pump reports
//    it after the drain. SDL_PushEvent (the plan's spelling) would queue one SDL_EVENT_QUIT
//    per call, and the frame and PlatformUpdate both call it every tick while
//    `_quitRequested` is set: a stale QUIT would outlive the drain that consumed the first.
//  * SDL's own SDL_EVENT_QUIT (no window close: SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE is 0, set
//    in src/app/main.cpp, so a close goes through WM_DESTROY's PostQuitMessage as retail's
//    did) is honoured as a WM_QUIT where it sits in the queue.
//  * WM_EXITSIZEMOVE has no SDL event. SDL_EVENT_WINDOW_MOVED fires for EVERY move,
//    including the window's own SetWindowPos and DXGI's mode switches; Video::Device::
//    MoveWindow would then persist the client origin after each programmatic placement
//    and the saved window position would drift by the frame width every run (retail
//    persists only after a USER move). On Windows the end of the user's move / size loop
//    is read from SDL's message hook (src/platform/win/window_hook_win.cpp), which SDL
//    calls for every message its WndProc receives inside the modal loop, WM_EXITSIZEMOVE
//    included; it queues a private SDL event (QueueMoveSizeEnded) so the tuple keeps its
//    place in the event order. Elsewhere a window manager
//    has no modal loop and MOVED is the nearest thing.
//  * Focus on Windows: SDL_EVENT_WINDOW_FOCUS_GAINED / _LOST follow the FOREGROUND window
//    only, and a window activated without the foreground (the foreground lock, e.g. a
//    run launched from a background shell) gets none, where retail's WindowProc got
//    WM_ACTIVATE(1) and WM_SETFOCUS. On Windows the real WM_ACTIVATE / WM_SETFOCUS /
//    WM_KILLFOCUS are read ahead of SDL's WndProc (src/platform/win/window_hook_win.cpp),
//    queued in order (QueueRawMessage) and dispatched with retail's wParam / lParam; SDL's
//    focus events are dropped there. Elsewhere they are the mapping.
//  * The observer (src/app/main.cpp's [WndMsg] logger) is called here, before the
//    handler, on this thread: not SDL_AddEventWatch, whose filter "may run in a different
//    thread", and the observer resets the render context.
// ============================================================================

#include "platform_window.h"
#include "window_messages.h"
#include "window_sdl_internal.h"
#include "platform_window_hooks.h"   // RenderScale (P7)
#include "platform_input_hooks.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <deque>

#if BANISHED_TWEAK_OVERLAY
#include "tweak_overlay.h"   // the debug tweak overlay (debug flavour only, src/diag)
#endif


namespace Platform {
namespace Window {

namespace {

bool            g_quitPosted = false;
MessageObserver g_observer   = nullptr;
// The private event type the Windows hook queues for WM_EXITSIZEMOVE; 0 until
// registered (SDL_RegisterEvents returns 0 when the user range is exhausted).
uint32_t        g_moveSizeEndedType = 0;
// The private event type QueueRawMessage queues, and the messages themselves, oldest
// first: one queued SDL event per message, so the pump pops one per event it meets.
uint32_t                   g_rawMessageType = 0;
std::deque<WindowMessage>  g_rawMessages;

void ClientSizeInPixels(int32_t* w, int32_t* h) {
    int iw = 0;
    int ih = 0;
    Sdl::WindowSizeInEnginePixels(Sdl::GameWindow(), &iw, &ih);   // P7: times the render scale
    *w = iw;
    *h = ih;
}

// One SDL_Event -> the neutral record window_messages.cpp translates.
WindowEvent FromSdl(const SDL_Event& ev) {
    WindowEvent e;
    switch (ev.type) {
#if !defined(SDL_PLATFORM_WINDOWS)
        // On Windows focus is the raw WM_ACTIVATE / WM_SETFOCUS / WM_KILLFOCUS (banner).
        case SDL_EVENT_WINDOW_FOCUS_GAINED: e.kind = WindowEventKind::FocusGained; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:   e.kind = WindowEventKind::FocusLost;   break;
#endif
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            e.kind   = WindowEventKind::SizeChanged;
            e.width  = ev.window.data1;
            e.height = ev.window.data2;
            if (RenderScale() != 1.0f) {
                // P7: the event's pixel size, in engine pixels (WindowSizeInEnginePixels).
                e.width  = static_cast<int32_t>(static_cast<float>(ev.window.data1) * RenderScale() + 0.5f);
                e.height = static_cast<int32_t>(static_cast<float>(ev.window.data2) * RenderScale() + 0.5f);
            }
            if (SDL_Window* win = Sdl::GameWindow())
                e.maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED: e.kind = WindowEventKind::Minimized; break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            e.kind = WindowEventKind::Maximized;
            ClientSizeInPixels(&e.width, &e.height);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            e.kind = WindowEventKind::Restored;
            ClientSizeInPixels(&e.width, &e.height);
            break;
#if !defined(SDL_PLATFORM_WINDOWS)
        case SDL_EVENT_WINDOW_MOVED: e.kind = WindowEventKind::MoveSizeEnded; break;
#endif
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: e.kind = WindowEventKind::CloseRequested; break;
        case SDL_EVENT_MOUSE_MOTION:           e.kind = WindowEventKind::MouseMotion;    break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            e.kind   = WindowEventKind::MouseButtonDown;
            e.button = ev.button.button;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            e.kind         = WindowEventKind::MouseWheel;
            e.wheelY       = ev.wheel.y;
            e.wheelFlipped = (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
            break;
        case SDL_EVENT_QUIT: e.kind = WindowEventKind::Quit; break;
        default:
            if (g_moveSizeEndedType != 0 && ev.type == g_moveSizeEndedType) {
                e.kind = WindowEventKind::MoveSizeEnded;
            } else if (g_rawMessageType != 0 && ev.type == g_rawMessageType &&
                       !g_rawMessages.empty()) {
                const WindowMessage& m = g_rawMessages.front();
                e.kind       = WindowEventKind::RawMessage;
                e.rawMessage = m.message;
                e.rawWParam  = m.wParam;
                e.rawLParam  = m.lParam;
                g_rawMessages.pop_front();
            }
            break;
    }
    return e;
}

}  // namespace

namespace Sdl {

void InstallPumpHooks() {
    if (g_moveSizeEndedType == 0) g_moveSizeEndedType = SDL_RegisterEvents(1);
    if (g_rawMessageType == 0) g_rawMessageType = SDL_RegisterEvents(1);
#if defined(SDL_PLATFORM_WINDOWS)
    InstallWindowsMessageHook(NativeHandle());
#endif
}

void RemovePumpHooks() {
#if defined(SDL_PLATFORM_WINDOWS)
    RemoveWindowsMessageHook(NativeHandle());
#endif
}

void QueueRawMessage(uint32_t message, uint64_t wParam, int64_t lParam) {
    if (g_rawMessageType == 0) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = g_rawMessageType;
    // The window is filled in by the pump (the native handle), as for every tuple.
    g_rawMessages.push_back(WindowMessage{nullptr, message, wParam, lParam});
    if (!SDL_PushEvent(&e)) g_rawMessages.pop_back();   // a full queue drops both
}

void QueueMoveSizeEnded() {
    if (g_moveSizeEndedType == 0) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = g_moveSizeEndedType;
    SDL_PushEvent(&e);
}

}  // namespace Sdl

void PostQuitMessage(int32_t /*exitCode*/) {
    // Win32 keeps the exit code for WM_QUIT's wParam; nothing in the tree reads it.
    g_quitPosted = true;
}

void SetMessageObserver(MessageObserver observer) {
    g_observer = observer;
}

}  // namespace Window

namespace {
// P11: the source of the last pointer event (Input::PointerIsTouch). Main thread (the pump).
bool g_pointerTouch = false;

void NotePointerSource(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_FINGER_DOWN || ev.type == SDL_EVENT_FINGER_UP || ev.type == SDL_EVENT_FINGER_MOTION) {
        g_pointerTouch = true;   // ext.touch_pointer's sink turns SDL's touch-to-mouse emulation off
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        g_pointerTouch = ev.motion.which == SDL_TOUCH_MOUSEID;
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        g_pointerTouch = ev.button.which == SDL_TOUCH_MOUSEID;
    }
}
}  // namespace

namespace Input {
bool PointerIsTouch() {
    return g_pointerTouch;
}
}  // namespace Input

bool Pump(Window::MessageHandler handler) {
    using namespace Window;
    void* const window = NativeHandle();

#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay's row / value changes, applied here on the main thread (one atomic test
    // when nothing is queued).
    Diag::TweakOverlay::MainThreadTick();
#endif
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        Input::PreprocessPumpEvent(&ev);
        NotePointerSource(ev);   // NOT RETAIL (P11): Input::PointerIsTouch (before the sink eats fingers)
        // NOT RETAIL (platform_touch.h, docs/android/P11_TOUCH_POINTER.md): with ext.touch_pointer's
        // sink set, the finger events are its own. No sink: false, retail's path.
        if (Touch::Sdl::ObservePumpEvent(ev)) continue;
#if BANISHED_TWEAK_OVERLAY
        // The toggle key, and while the overlay is open its mouse / keyboard events (the game sees none).
        if (Diag::TweakOverlay::OnSdlEvent(ev)) continue;
#endif
        if (Input::ConsumePumpEvent(ev)) continue;
        WindowMessage msgs[kMaxMessagesPerEvent];
        const int n = TranslateWindowEvent(FromSdl(ev), window, msgs);
        for (int i = 0; i < n; ++i) {
            const WindowMessage& m = msgs[i];
            // Retail tests WM_QUIT after DispatchMessageW, which does nothing for a
            // window-less WM_QUIT: stopping here is the same thing.
            if (m.message == kMsgQuit) return true;
            if (g_observer) g_observer(m.window, m.message, m.wParam, m.lParam);
            handler(m.window, m.message, m.wParam, m.lParam);
        }
    }

    // The queue is empty: a posted quit is delivered now, once (see the banner).
    if (g_quitPosted) {
        g_quitPosted = false;
        return true;
    }
    return false;
}

}  // namespace Platform
