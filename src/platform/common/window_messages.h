#pragma once
// ============================================================================
// src/platform/common/window_messages.h -- window events -> retail's message tuples
// ============================================================================
// Phase D.4 SDL step W3 (plan §A.2, §C). `Application::_messageCallbacks` is keyed by
// Win32 message ids and `Application::WindowProc` switches on them; both are retail's
// and keep their layout. The SDL pump (src/platform/sdl/pump_sdl.cpp) therefore turns
// each window-system event into the (message, wParam, lParam) tuples a Win32 WndProc
// received for the same happening, and dispatches those.
//
// The translation is a PURE function over a neutral event record so the unit tests can
// drive it without linking SDL (D4_RULES C7): pump_sdl.cpp fills a `WindowEvent` from
// an SDL_Event, `TranslateWindowEvent` does the rest. No OS header, no SDL header.
//
// The table (Win32 facts the tuples reproduce):
//
//   event            tuples, in order                         Win32 source
//   FocusGained      ACTIVATE(WA_ACTIVE=1), SETFOCUS(0)        activation sends WM_ACTIVATE,
//   FocusLost        ACTIVATE(WA_INACTIVE=0), KILLFOCUS(0)       then WM_SET/KILLFOCUS
//   SizeChanged      SIZE(SIZE_RESTORED=0 | SIZE_MAXIMIZED=2,  WM_SIZE, lParam = client
//                         MAKELPARAM(w, h))                      width | height << 16
//   Minimized        SIZE(SIZE_MINIMIZED=1, 0)                 WM_SIZE on minimise, 0x0
//   Maximized        SIZE(SIZE_MAXIMIZED=2, MAKELPARAM(w, h))
//   Restored         SIZE(SIZE_RESTORED=0,  MAKELPARAM(w, h))
//   MoveSizeEnded    EXITSIZEMOVE(0, 0)                        end of a user move / size loop
//   CloseRequested   CLOSE(0, 0), DESTROY(0, 0)                DefWindowProcW(WM_CLOSE) ->
//                                                                DestroyWindow -> WM_DESTROY
//   MouseMotion      SETCURSOR(window, MAKELPARAM(HTCLIENT,    sent before each WM_MOUSEMOVE
//                         WM_MOUSEMOVE))                         over the client area
//   MouseButtonDown  SETCURSOR(window, MAKELPARAM(HTCLIENT,    sent before each button-down
//                         WM_xBUTTONDOWN))
//   MouseWheel       MOUSEWHEEL(MAKEWPARAM(0, +-120), 0)       the sign only (y == 0: none;
//                                                                flipped: negated)
//   Quit             QUIT(0, 0)                                the caller stops at it
//   RawMessage       (message, wParam, lParam) as received     a Win32 message read ahead
//                                                                of SDL's WndProc (below)
//
// FOCUS ON WINDOWS IS A RAW MESSAGE, NOT FocusGained / FocusLost. SDL raises its focus
// events only when the window is the FOREGROUND window (WIN_UpdateFocus,
// SDL_windowsevents.c: `has_focus = GetForegroundWindow() == hwnd`). Win32 sends
// WM_ACTIVATE / WM_SETFOCUS on ACTIVATION, which the foreground lock does not stop: a
// process launched from a background shell gets WM_NCACTIVATE(0), WM_ACTIVATE(1),
// WM_SETFOCUS with an inactive title bar, and retail's WindowProc saw all three. On
// Windows WM_ACTIVATE / WM_SETFOCUS / WM_KILLFOCUS are therefore read off the real messages
// (src/platform/win/window_hook_win.cpp) and arrive here as RawMessage with retail's
// wParam / lParam; the pump drops SDL's foreground-based focus events there.
// FocusGained / FocusLost remain the mapping on other platforms.
//
// Several of these differ from Win32 in WHEN they arrive, never in what they carry: a
// Win32 WndProc got WM_SIZE / WM_ACTIVATE synchronously inside the SetWindowPos or
// ShowWindow that caused them; SDL queues them and they are dispatched at the next
// pump. docs/runtime_port/phase_d/handins/SDL-B.md lists what that changes.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Window {

enum class WindowEventKind : uint8_t {
    None,              // nothing to dispatch (an SDL event this seam does not translate)
    FocusGained,
    FocusLost,
    SizeChanged,       // the client size in pixels changed (width / height)
    Minimized,
    Maximized,         // width / height: the client size after maximising
    Restored,          // width / height: the client size after restoring
    MoveSizeEnded,     // a user move / size loop ended (Win32: WM_EXITSIZEMOVE)
    CloseRequested,
    MouseMotion,
    MouseButtonDown,   // button: 1 left, 2 middle, 3 right, 4 / 5 the X buttons
    MouseWheel,        // wheelY, wheelFlipped
    Quit,
    RawMessage,        // rawMessage / rawWParam / rawLParam, dispatched as they are
};

struct WindowEvent {
    WindowEventKind kind = WindowEventKind::None;
    int32_t width = 0;           // SizeChanged / Maximized / Restored
    int32_t height = 0;
    bool    maximized = false;   // SizeChanged: the window is maximised (SIZE_MAXIMIZED)
    uint8_t button = 0;          // MouseButtonDown
    float   wheelY = 0.0f;       // MouseWheel: positive = away from the user
    bool    wheelFlipped = false;// MouseWheel: the platform reported a flipped direction
    uint32_t rawMessage = 0;     // RawMessage
    uint64_t rawWParam = 0;
    int64_t  rawLParam = 0;
};

struct WindowMessage {
    void*    window;
    uint32_t message;
    uint64_t wParam;
    int64_t  lParam;
};

// The most tuples one event produces (focus and close produce two).
constexpr int kMaxMessagesPerEvent = 2;

// Win32 values the tuples carry beyond the message ids (platform_window.h names those).
constexpr uint64_t kActivateInactive = 0;    // WA_INACTIVE
constexpr uint64_t kActivateActive   = 1;    // WA_ACTIVE
constexpr uint64_t kSizeRestored     = 0;    // SIZE_RESTORED
constexpr uint64_t kSizeMinimized    = 1;    // SIZE_MINIMIZED
constexpr uint64_t kSizeMaximized    = 2;    // SIZE_MAXIMIZED
constexpr int64_t  kHitClient        = 1;    // HTCLIENT
constexpr uint32_t kMsgMouseMove     = 0x0200;   // WM_MOUSEMOVE
constexpr uint32_t kMsgLButtonDown   = 0x0201;   // WM_LBUTTONDOWN
constexpr uint32_t kMsgRButtonDown   = 0x0204;   // WM_RBUTTONDOWN
constexpr uint32_t kMsgMButtonDown   = 0x0207;   // WM_MBUTTONDOWN
constexpr uint32_t kMsgXButtonDown   = 0x020B;   // WM_XBUTTONDOWN
constexpr int16_t  kWheelDelta       = 120;      // WHEEL_DELTA

// Fills `out` with the tuples for `event`, addressed to `window`; returns how many
// (0..kMaxMessagesPerEvent).
int TranslateWindowEvent(const WindowEvent& event, void* window,
                         WindowMessage out[kMaxMessagesPerEvent]);

}  // namespace Window
}  // namespace Platform
