// ============================================================================
// src/platform/win/window_hook_win.cpp -- the Win32 messages SDL's events cannot carry
// ============================================================================
// Phase D.4 SDL step W3 (and its focus fix). Two retail facts the SDL event stream does
// not express are read here, off the real Win32 messages, and queued for the pump in
// order with SDL's own events. This file only observes: every message still reaches
// SDL's WIN_WindowProc unchanged.
//
// 1. WM_EXITSIZEMOVE (0x232), the end of a USER move / size loop.
//    Video::Device::MoveWindow is retail's callback for it: it persists the window
//    position once the user has finished dragging. SDL has no event for that, and
//    SDL_EVENT_WINDOW_MOVED also fires for the window's own SetWindowPos and for DXGI's
//    mode switches (src/platform/sdl/pump_sdl.cpp's banner). SDL calls the Windows
//    message hook for every message its WndProc receives while it is inside a modal
//    move / size loop (`data->in_modal_loop`, SDL_windowsevents.c), and WM_EXITSIZEMOVE
//    arrives while that count is still 1, so the hook sees it and queues the pump's
//    private event. SDL also calls the hook for every POSTED message its PeekMessage
//    loop takes; those are one compare each and are left alone.
//
// 2. WM_ACTIVATE (6), WM_SETFOCUS (7), WM_KILLFOCUS (8): activation and keyboard focus.
//    These are SENT, so the message hook never sees them outside a modal loop. SDL turns
//    them into SDL_EVENT_WINDOW_FOCUS_GAINED / _LOST only when the window is also the
//    FOREGROUND window (WIN_UpdateFocus: `has_focus = GetForegroundWindow() == hwnd`).
//    Win32 activates a window the foreground lock keeps in the background (a run
//    started from a background shell: WM_NCACTIVATE(0), WM_ACTIVATE(WA_ACTIVE),
//    WM_SETFOCUS, title bar drawn inactive), and retail's WindowProc got those
//    messages: `_running`, Input::Device::OnFocusMessage and DeviceData_OnActivate all
//    ran. Found by the coordinator's SDL-B routes: the RENDER-3 run logged
//    `WM_ACTIVATE w=1` and `WM_SETFOCUS` at the window's reveal, the SDL run logged
//    neither. So the window procedure is subclassed AHEAD of SDL's: the three messages
//    are queued exactly as received (message, wParam, lParam) and the procedure chains to
//    SDL's with CallWindowProcW. The pump drops SDL's foreground-based focus events on
//    Windows.
//
// 3. Harness only: with BANISHED_WINDOW_ANY_SIZE (tools/device_sim.sh) the subclass raises
//    WM_GETMINMAXINFO's max track size after SDL and DefWindowProc filled it, so the window
//    can keep a client area larger than the desktop (a 3392x2400 tablet on a 1440-high
//    monitor). The one place this file changes a message's answer.
//
// It lives under src/platform/win/ because it reads Win32 messages (<windows.h>); the
// rest of the SDL backend has no OS header.
// ============================================================================

#include <windows.h>

#include <cstddef>

#include <SDL3/SDL_system.h>

#include "../sdl/window_sdl_internal.h"
#include "platform_window.h"
#include "app/options.h"   // Opt().windowAnySize (harness, tools/device_sim.sh)

// The compile-time proof that platform_window.h's message ids, flags and Point / Rect are
// Win32's: the pump hands the window-procedure callbacks retail's message tuples, and this
// file queues the real WM_ACTIVATE / WM_SETFOCUS / WM_KILLFOCUS unchanged. Moved here from
// src/platform/win/window_win.cpp when Phase D.4 SDL-5 deleted it (its last two bodies,
// IsVisible / ShowDefault, are SDL's: src/platform/sdl/window_sdl.cpp).
static_assert(sizeof(Platform::Point) == sizeof(POINT), "Platform::Point must be a POINT");
static_assert(offsetof(Platform::Point, x) == offsetof(POINT, x), "Point::x");
static_assert(offsetof(Platform::Point, y) == offsetof(POINT, y), "Point::y");
static_assert(sizeof(Platform::Rect) == sizeof(RECT), "Platform::Rect must be a RECT");
static_assert(offsetof(Platform::Rect, left) == offsetof(RECT, left), "Rect::left");
static_assert(offsetof(Platform::Rect, top) == offsetof(RECT, top), "Rect::top");
static_assert(offsetof(Platform::Rect, right) == offsetof(RECT, right), "Rect::right");
static_assert(offsetof(Platform::Rect, bottom) == offsetof(RECT, bottom), "Rect::bottom");
static_assert(Platform::Window::kPosFrameChangedNoActivate == (SWP_FRAMECHANGED | SWP_NOACTIVATE),
              "SetWindowPos flags");
static_assert(Platform::Window::kMsgDestroy == WM_DESTROY, "WM_DESTROY");
static_assert(Platform::Window::kMsgSize == WM_SIZE, "WM_SIZE");
static_assert(Platform::Window::kMsgActivate == WM_ACTIVATE, "WM_ACTIVATE");
static_assert(Platform::Window::kMsgSetFocus == WM_SETFOCUS, "WM_SETFOCUS");
static_assert(Platform::Window::kMsgKillFocus == WM_KILLFOCUS, "WM_KILLFOCUS");
static_assert(Platform::Window::kMsgClose == WM_CLOSE, "WM_CLOSE");
static_assert(Platform::Window::kMsgQuit == WM_QUIT, "WM_QUIT");
static_assert(Platform::Window::kMsgEraseBackground == WM_ERASEBKGND, "WM_ERASEBKGND");
static_assert(Platform::Window::kMsgSetCursor == WM_SETCURSOR, "WM_SETCURSOR");
static_assert(Platform::Window::kMsgSysKeyDown == WM_SYSKEYDOWN, "WM_SYSKEYDOWN");
static_assert(Platform::Window::kMsgMouseWheel == WM_MOUSEWHEEL, "WM_MOUSEWHEEL");
static_assert(Platform::Window::kMsgExitSizeMove == WM_EXITSIZEMOVE, "WM_EXITSIZEMOVE");
static_assert(Platform::Window::kKeyF10 == VK_F10, "VK_F10");

namespace Platform {
namespace Window {
namespace Sdl {

namespace {

// SDL's procedure for the window, which the subclass chains to; null when not installed.
WNDPROC g_sdlProc = nullptr;

bool SDLCALL MessageHook(void* /*userdata*/, MSG* msg) {
    if (msg->message == WM_EXITSIZEMOVE) QueueMoveSizeEnded();
    return true;
}

LRESULT CALLBACK FocusSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ACTIVATE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        QueueRawMessage(msg, static_cast<uint64_t>(wParam), static_cast<int64_t>(lParam));
    }
    if (msg == WM_GETMINMAXINFO && Opt().windowAnySize) {
        // Harness only (BANISHED_WINDOW_ANY_SIZE, tools/device_sim.sh): DefWindowProc's default
        // max track size is the virtual screen plus the frame, so the window's own SetWindowPos
        // could not keep a tablet-sized client on a smaller desktop. SDL fills the struct first.
        const LRESULT result = CallWindowProcW(g_sdlProc, hwnd, msg, wParam, lParam);
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);   // CAST-OK: win32
        info->ptMaxTrackSize.x = 16384;
        info->ptMaxTrackSize.y = 16384;
        return result;
    }
    return CallWindowProcW(g_sdlProc, hwnd, msg, wParam, lParam);
}

}  // namespace

void InstallWindowsMessageHook(void* window) {
    SDL_SetWindowsMessageHook(&MessageHook, nullptr);
    if (window == nullptr || g_sdlProc != nullptr) return;
    HWND hwnd = static_cast<HWND>(window);   // CAST-OK: win32
    g_sdlProc = reinterpret_cast<WNDPROC>(   // CAST-OK: win32
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&FocusSubclassProc)));   // CAST-OK: win32
}

void RemoveWindowsMessageHook(void* window) {
    SDL_SetWindowsMessageHook(nullptr, nullptr);
    if (window == nullptr || g_sdlProc == nullptr) return;
    HWND hwnd = static_cast<HWND>(window);   // CAST-OK: win32
    // Restore SDL's procedure before SDL_DestroyWindow, so SDL tears down the window it
    // set up.
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_sdlProc));   // CAST-OK: win32
    g_sdlProc = nullptr;
}

}  // namespace Sdl
}  // namespace Window
}  // namespace Platform
