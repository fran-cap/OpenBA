#pragma once
// ============================================================================
// src/platform/common/platform_window.h -- Platform::Window, the window / display seam
// ============================================================================
// Phase D.4 (D4_RULES C1, SDL plan §C, step W1). Free functions, no OS header, no SDL
// header. Implementations: SDL3 on every host since SDL-5 (src/platform/sdl/: the window
// and its state in window_sdl.cpp, the pump in pump_sdl.cpp, geometry in display_sdl.cpp,
// the cursor in cursor_sdl.cpp, ClipCursor in input_sdl.cpp). The Win32 bodies of step W1
// (src/platform/win/window_win.cpp) are gone; src/platform/win/window_hook_win.cpp is
// the Windows-only message observer the pump needs.
//
// W1 is a PURE MOVE: every primitive below is ONE Win32 call (plus the struct set-up that
// call needs), with the arguments the retail-faithful callers passed. The callers keep
// their arithmetic, their literals and their retail comments; only the OS call moved.
//
// A window and a monitor are opaque handles (`void*`): the HWND / HMONITOR on Win32,
// which is what `Application::_windowHandle` (+0x00) and `Video::Device::windowHandle`
// (+0x90) / `monitor` (+0xA0) already hold. `Rect` and `Point` are binary-identical to
// Win32's RECT / POINT (window_hook_win.cpp static_asserts it), so a caller that punned a RECT
// corner as a POINT still can.
// ============================================================================

#include <cstdint>

namespace Platform {

struct Point {
    int32_t x;
    int32_t y;
};

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

namespace Window {

// ---------------------------------------------------------------------------
// Window message ids (SDL plan W2, the A.2 table). `Application::_messageCallbacks` is
// keyed by these Win32 ids (its layout is retail's) and `Application::WindowProc`
// switches on them; from W3 the SDL pump synthesises them from SDL events. The values are
// the WM_* ids (window_hook_win.cpp static_asserts each against <windows.h>).
// ---------------------------------------------------------------------------
constexpr uint32_t kMsgDestroy         = 0x0002;   // WM_DESTROY
constexpr uint32_t kMsgSize            = 0x0005;   // WM_SIZE        Video::Device::SizeWindow
constexpr uint32_t kMsgActivate        = 0x0006;   // WM_ACTIVATE    DeviceData_OnActivate
constexpr uint32_t kMsgSetFocus        = 0x0007;   // WM_SETFOCUS    Input::Device::OnFocusMessage
constexpr uint32_t kMsgKillFocus       = 0x0008;   // WM_KILLFOCUS   Input::Device::OnLostFocusMessage
constexpr uint32_t kMsgClose           = 0x0010;   // WM_CLOSE
constexpr uint32_t kMsgQuit            = 0x0012;   // WM_QUIT
constexpr uint32_t kMsgEraseBackground = 0x0014;   // WM_ERASEBKGND
constexpr uint32_t kMsgSetCursor       = 0x0020;   // WM_SETCURSOR   DeviceData_OnSetCursor
constexpr uint32_t kMsgSysKeyDown      = 0x0104;   // WM_SYSKEYDOWN
constexpr uint32_t kMsgMouseWheel      = 0x020A;   // WM_MOUSEWHEEL  Input::Device::OnMouseWheelMessage
constexpr uint32_t kMsgExitSizeMove    = 0x0232;   // WM_EXITSIZEMOVE Video::Device::MoveWindow
// The one key WindowProc tests: VK_F10 (WM_SYSKEYDOWN's wParam).
constexpr uint64_t kKeyF10             = 0x79;

// SetWindowPos's insert-after argument, the two values retail passes.
enum class ZOrder : int32_t {
    Top,         // nullptr (== HWND_TOP): AdjustDisplay's fullscreen branch
    NoTopmost,   // HWND_NOTOPMOST: AdjustDisplay's windowed branch
};

// SetWindowPos flags retail passes: SWP_FRAMECHANGED | SWP_NOACTIVATE. From W4 the SDL
// body applies the frame itself (SetFramed) and never activates, so it reads no flag.
constexpr uint32_t kPosFrameChangedNoActivate = 0x30;

// ---------------------------------------------------------------------------
// Geometry and displays. W1 put one Win32 call behind each; from SDL step W4 the bodies
// are SDL's (src/platform/sdl/display_sdl.cpp), keeping the Win32 MEANING each caller was
// written against: rects are Win32-shaped (client rects at 0,0; window rects OUTER, frame
// included), coordinates are screen pixels (the process is DPI-unaware, as retail), and
// `window` is the game window's native handle. ClipCursor is SDL's since SDL-C I3
// (src/platform/sdl/input_sdl.cpp).
// ---------------------------------------------------------------------------
// GetClientRect: {0, 0, width, height} of the client area; all zeros while minimised
// (Win32 reports an iconic window's client area as empty).
void GetClientRect(void* window, Rect* out);
// GetWindowRect: the OUTER rect, frame included, in screen coordinates.
void GetWindowRect(void* window, Rect* out);
// ClientToScreen, in place.
void ClientToScreen(void* window, Point* inOut);
// GetWindowPlacement's showCmd: SW_SHOWMINIMIZED (2) while minimised, SW_SHOWMAXIMIZED (3)
// while maximised, otherwise SW_SHOWNORMAL (1).
uint32_t GetShowCmd(void* window);
constexpr uint32_t kShowCmdMinimized = 2;            // SW_SHOWMINIMIZED
constexpr uint32_t kShowCmdMaximized = 3;            // SW_SHOWMAXIMIZED / SW_MAXIMIZE
// The WS_MAXIMIZE / WS_MINIMIZE style bits, which retail's AdjustDisplay /
// IsMinimized also compare the showCmd against (kept as retail does).
constexpr uint32_t kStyleMaximize    = 0x01000000;   // WS_MAXIMIZE
constexpr uint32_t kStyleMinimize    = 0x20000000;   // WS_MINIMIZE
// IsIconic.
bool IsIconic(void* window);
// IsWindowVisible (RENDER step 3: DeviceData_Resize's early-out, TraceGeom). SDL's
// SDL_WINDOW_HIDDEN flag of the game window since SDL-5 (src/platform/sdl/window_sdl.cpp).
bool IsVisible(void* window);
// ShowWindow(window, SW_SHOWDEFAULT) (RENDER step 3: CreateDevice step 6 and
// BootWindow::ShowNow, the deferred reveal). SDL_ShowWindow since SDL-5
// (src/platform/sdl/window_sdl.cpp): ShowWindow(SW_SHOW) on Windows, which does not ask
// for the STARTUPINFO show state SW_SHOWDEFAULT honoured (known_port_issues row 1277).
void ShowDefault(void* window);
// The two window styles AdjustDisplay sets (W4; were GetStyle / SetStyle on GWL_STYLE):
// framed = WS_OVERLAPPEDWINDOW (caption, border, sizing frame, system menu, min / max
// boxes), not framed = WS_POPUP with none of those.
void SetFramed(void* window, bool framed);
// SetWindowPos of the OUTER rect (x, y, width, height); `insertAfter` NoTopmost clears
// always-on-top, Top makes no z-order change (see display_sdl.cpp).
void SetWindowPos(void* window, ZOrder insertAfter, int32_t x, int32_t y,
                  int32_t width, int32_t height, uint32_t flags);
// AdjustWindowRect for the window's CURRENT frame (W4: was by style bits), in place.
void AdjustWindowRect(void* window, Rect* inOut);
// ClipCursor; nullptr releases the clip.
void ClipCursor(const Rect* screenRect);
// GetMonitorInfoW(monitor).rcMonitor for the HMONITOR the renderer stored in
// Video::Device::monitor (+0xA0); all zeros when no display matches (the zeroed
// MONITORINFO retail's failed call left).
void GetMonitorRect(void* monitor, Rect* out);
// MonitorFromRect(rect, MONITOR_DEFAULTTONULL) != NULL: does `rect` touch any display?
bool RectOnAnyDisplay(const Rect* rect);
// GetWindowRect(GetDesktopWindow()): the primary display's rect (W4: was the pair).
void GetPrimaryDisplayRect(Rect* out);
// EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS)'s dmPelsWidth / dmPelsHeight: the
// primary display's current mode; zeros when it cannot be read (retail's unchecked call).
void GetCurrentDisplaySize(int32_t* width, int32_t* height);
// The primary display's current refresh rate in whole Hz, 0 when unknown
// (src/app/frame_limiter.cpp's query; ours, not retail's).
int32_t GetCurrentDisplayRefreshHz();

// The window procedure's OS half (SDL plan W2). DefWindowProcW: the default handling
// Application::WindowProc falls through to. From W3 SDL owns the real WndProc and has
// already run DefWindowProcW on the real message, so the SDL body returns 0 and does
// nothing (src/platform/sdl/window_sdl.cpp).
int64_t DefaultWindowProc(void* window, uint32_t message, uint64_t wParam, int64_t lParam);
// PostQuitMessage: WindowProc's WM_DESTROY arm, the frame's and PlatformUpdate's quit
// request. From W3 it raises the quit FLAG that Platform::Pump reports once the queue is
// drained, which is Win32's own WM_QUIT rule (src/platform/sdl/pump_sdl.cpp).
void PostQuitMessage(int32_t exitCode);

// ---------------------------------------------------------------------------
// SDL step W3: the window's lifetime and the message pump (src/platform/sdl/).
// ---------------------------------------------------------------------------
// True when the video subsystem is up (src/app/main.cpp's SDL_Init(SDL_INIT_VIDEO)
// and the window class registered with it). Application::PlatformStartup's
// "failed to register a window class" arm is the false case.
bool VideoAvailable();
// Create THE game window: hidden, `width` x `height` client pixels, titled `title`
// (UTF-8). Returns its native handle (NativeHandle()), or nullptr on failure.
void* Create(const char* title, int32_t width, int32_t height);
// Destroy the window Create made. The caller's copy of the handle is not touched.
void Destroy(void* window);
// The renderer's handle for the game window (D4_RULES C5): the HWND on Windows, from
// SDL_PROP_WINDOW_WIN32_HWND_POINTER; nullptr before Create / after Destroy.
void* NativeHandle();

// The shape of Application::WindowProc and of every registered message callback's
// caller: (window, message id, wParam, lParam), the Win32 message vocabulary above.
using MessageHandler = int64_t (*)(void* window, uint32_t message, uint64_t wParam,
                                   int64_t lParam);
// A diagnostic observer called with each message BEFORE the handler, on the pumping
// (main) thread (src/app/main.cpp's [WndMsg] logger; it replaces a WH_CALLWNDPROC hook).
// nullptr removes it.
using MessageObserver = void (*)(void* window, uint32_t message, uint64_t wParam,
                                 int64_t lParam);
void SetMessageObserver(MessageObserver observer);

// ---------------------------------------------------------------------------
// SDL step W6: the fatal error box (System::Error::Display's MessageBoxW(nullptr, text,
// caption, MB_ICONERROR)). Modal, no owner window, one OK button; returns when it is
// dismissed. Both strings are NUL-terminated UTF-16LE code units, the buffers retail
// builds with its own clamp; the SDL body converts them to UTF-8. Callable before the
// video subsystem is up and from any thread.
void ShowErrorMessage(const uint16_t* textUtf16, const uint16_t* captionUtf16);

}  // namespace Window

// The message pump (SDL step W3): drain the window system's event queue, translate
// each event into the (message, wParam, lParam) tuples retail's WindowProc received
// (src/platform/common/window_messages.h) and hand them to `handler`. Returns true
// when the drain met WM_QUIT (a posted quit, or the window system's own quit event):
// the caller's "WM_QUIT seen" exit, as retail's PeekMessageW loops break on it.
bool Pump(Window::MessageHandler handler);

// ---------------------------------------------------------------------------
// The mouse cursor (RENDER plan step 3, section C.2: VideoDX11::DeviceData slot 8,
// slot 3 and the WM_SETCURSOR callback). A cursor is an opaque 8-byte handle (a pointer):
// since SDL step W5 a slot holding the SDL_Cursor* (src/platform/sdl/cursor_sdl.cpp; it
// was the HCURSOR under RENDER-3's Win32 bodies). `VideoDX11::DeviceData::cursor` (+0x48)
// holds one.
// ---------------------------------------------------------------------------
struct CursorObject;                  // defined only by the backend: the handle is opaque
using CursorHandle = CursorObject*;

namespace Cursor {

// CreateCursor(instance, hotX, hotY, width, height, andPlane, xorPlane), the argument
// order Win32 takes. The planes are monochrome, 1 bit per pixel, each row width / 8
// bytes, the leftmost pixel the MSB of the row's first byte. AND set = transparent; AND
// clear and XOR set = white; both clear = black. `instance` is the module handle
// (Application::GetInstance, the HINSTANCE); an SDL implementation ignores it. Returns
// nullptr when the OS call fails.
CursorHandle Create(void* instance, int32_t hotX, int32_t hotY, int32_t width,
                    int32_t height, const void* andPlane, const void* xorPlane);
// SetCursor. The previous cursor it returns is not used by any caller. nullptr is
// Win32's SetCursor(NULL): no cursor image over the client area.
void Set(CursorHandle cursor);
// ShowCursor(show). The display counter it returns is not used by any caller; the SDL
// body is a flag (only Show(true) is ever called).
void Show(bool show);
// DestroyCursor. A handle already destroyed is ignored (Win32 fails the call; SDL finds
// no live cursor with that address).
void Destroy(CursorHandle cursor);

}  // namespace Cursor

}  // namespace Platform
