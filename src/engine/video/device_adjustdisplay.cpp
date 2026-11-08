// ============================================================================
// src/engine/video/device_adjustdisplay.cpp — Wave 18 lane GFXIF
//   (src/platform/win/device_adjustdisplay_win.cpp until Phase D.4 LINUX-1, which moved it
//   here unchanged: since SDL W1 / W4 it calls only Platform::Window, so the one body serves
//   Windows and POSIX alike. The history below is the file's.)
//   ?AdjustDisplay@Device@Video@@QEAAXXZ    retail 0x1800f7e20
// ============================================================================
//
// The ninth of lane GFXIF's nine rows, and the only one that is pure Win32.
// PLAN §4.1 decision 1 keeps src/engine/ free of <windows.h> outside a `_win`
// twin, so it lives here and device_enum_types.cpp stays portable.
//
// B.4 (R-VIDEO): this TU includes src/engine/video/Device.h AFTER
// <windows.h> (as buffers_win.cpp does) and the body is the member
// `Video::Device::AdjustDisplay` on named fields; the literal offsets and the
// `RtVideo_Device_AdjustDisplay(void*)` C body are gone.
//
// WHAT IT DOES. Re-lays the window out for the current Video::Configuration,
// then reads back the client area and publishes it as the new backbuffer size:
//
//   device->adjustingDisplay = 1                   (a re-entrancy flag; cleared at the end)
//   style = GetWindowLongW(hwnd, GWL_STYLE)
//   MONITORINFO mi = {}; mi.cbSize = 0x28
//   if (device+0xA0) GetMonitorInfoW(device+0xA0, &mi)
//   if (config->fullscreen) {
//       style = (style & 0xFF30FFFF) | WS_POPUP
//       SetWindowLongW(hwnd, GWL_STYLE, style)
//       SetWindowPos(hwnd, NULL, mi.rcMonitor.left, mi.rcMonitor.top,
//                    device->backBufferWidth, device->backBufferHeight,
//                    SWP_FRAMECHANGED | SWP_NOACTIVATE)
//   } else {
//       ... the windowed branch, below ...
//   }
//   device->adjustingDisplay = 0
//
// EVERY IMPORT SLOT WAS RESOLVED BY NAME, NOT GUESSED FROM THE CALL SHAPE.
// pefile against the shipped DLL's import table:
//   0x18014b510 GetWindowLongW      0x18014b500 SetWindowLongW
//   0x18014b508 GetMonitorInfoW     0x18014b4f8 SetWindowPos
//   0x18014b4f0 GetWindowPlacement  0x18014b4e8 AdjustWindowRect
//   0x18014b4e0 MonitorFromRect     0x18014b4d8 GetDesktopWindow
//   0x18014b5a0 GetWindowRect       0x18014b558 GetClientRect
// Note GetWindowLong**W**, not GetWindowLongPtrW: the style travels as a 32-bit
// LONG throughout, which is why every mask below is 32-bit.
//
// TWO CORRECTIONS TO src/engine/video/Device.h, BOTH FROM THIS BODY
// -------------------------------------------------------------------
//  * Device+0xA0 is an HMONITOR. Device.h currently covers it with
//    `gap9E[0x12]` and records only "the ctor zeroes +0xa0 and +0xa8". This
//    body loads it and passes it straight to GetMonitorInfoW (0x1800f7e68),
//    which pins it.
//  * Adapter+0x2C and +0x30 are NOT unrecovered. Device.h says "meaning not
//    recovered" for both; src/render/dx11/devicedata.cpp:485-486 already names them
//    dxgiAdapterIndex and dxgiOutputIndex, written by the shipped DX11
//    EnumerateInterfaces. Recorded here because this lane read both files.
// Neither correction changes a byte of layout, so nothing is edited in the
// header by this lane; see docs/history/runtime_port/WAVE18_LANE_GFXIF.md.
//
// A RETAIL DEFECT, REPRODUCED DELIBERATELY
// ----------------------------------------
// In the windowed branch the adjusted RECT is offset by the persisted window
// origin, and retail adds the X offset to THREE of the four edges:
//
//   0x1800f7f84  [rect2.left]   = rect.left   + EDI      (EDI = windowX)
//   0x1800f7f8c  [rect2.right]  = rect.right  + EDI
//   0x1800f7f94  [rect2.top]    = rect.top    + ESI      (ESI = windowY)
//   0x1800f7f9c  [rect2.bottom] = rect.bottom + EDI      <-- EDI, not ESI
//
// `bottom` is displaced horizontally. The rectangle is used only as the
// argument to MonitorFromRect, so the consequence is a monitor lookup against a
// slightly wrong box — harmless whenever windowX and windowY are close, and
// invisible on a single-monitor desktop. It is reproduced rather than fixed:
// this port's job is to be the shipped behaviour, and a "corrected" version
// would pick a different monitor from retail in exactly the multi-monitor case
// where anyone would notice.
// ============================================================================

// Phase D.4 SDL W1: every Win32 call listed above moved behind Platform::Window
// (src/platform/win/window_win.cpp), one primitive per call, arguments unchanged; this
// TU no longer includes <windows.h>.
//
// Phase D.4 SDL W4: the primitives are SDL's (src/platform/sdl/display_sdl.cpp), each
// keeping the Win32 meaning this body was transcribed against: SetWindowPos still takes
// the OUTER rect at the persisted origin (so retail's client-origin-as-outer-origin drift
// survives), AdjustWindowRect still grows by the frame, the probe still asks "is this
// rect on ANY monitor". Three spellings changed with the backend: the GWL_STYLE
// read-modify-write is Platform::Window::SetFramed (SDL owns the style bits; the masks
// retail applied are quoted at the two calls), AdjustWindowRect reads the window's own
// frame instead of taking the style, and MonitorFromRect / GetWindowRect(GetDesktopWindow())
// are RectOnAnyDisplay / GetPrimaryDisplayRect.
#include <cstdint>
#include <cstring>

#include "video/Device.h"   // Video::Device, Video::Configuration::_singleton
#include "platform_window.h"

// ?AdjustDisplay@Device@Video@@ — B.4 (R-VIDEO): the member itself, on named
// fields (Device +0x04/+0x08 backBufferWidth/Height, +0x90 windowHandle, +0x9D
// adjustingDisplay, +0xA0 monitor; Configuration +0x14..+0x20 window rect, +0x40
// fullscreen).
void Video::Device::AdjustDisplay() {
    // The re-entrancy flag. SizeWindow/MoveWindow read it to tell an
    // application-driven resize from this one; set before the first Win32 call
    // and cleared on BOTH exits (0x1800f7e37 and 0x1800f8088).
    adjustingDisplay = 1;

    void* const hwnd = windowHandle;
    // GetWindowLongW(hwnd, GWL_STYLE) at 0x18014b510: the style is read here and modified
    // below; over SDL the two states are set whole by SetFramed.

    // GetMonitorInfoW on a zeroed MONITORINFO (cbSize 0x28); only rcMonitor is read.
    // A null monitor leaves the zeros, as the zeroed MONITORINFO did.
    Platform::Rect monitorRect;
    std::memset(&monitorRect, 0, sizeof(monitorRect));
    void* const mon = monitor;
    if (mon) Platform::Window::GetMonitorRect(mon, &monitorRect);

    Configuration* cfg = Configuration::_singleton;

    if (cfg->fullscreen != 0) {
        // ---- fullscreen: borderless, pinned to the monitor origin ----------
        // 0xFF30FFFF clears the whole WS_* style nibble pair that carries the
        // caption and border bits; WS_POPUP (0x80000000) is then set:
        //     style = (style & 0xFF30FFFF) | WS_POPUP; SetWindowLongW(GWL_STYLE, style)
        Platform::Window::SetFramed(hwnd, false);

        Platform::Window::SetWindowPos(hwnd, Platform::Window::ZOrder::Top,
                     monitorRect.left, monitorRect.top,
                     backBufferWidth, backBufferHeight,
                     Platform::Window::kPosFrameChangedNoActivate);
        adjustingDisplay = 0;
        return;
    }

    // ---- windowed ---------------------------------------------------------
    // 0xCF0000 == WS_OVERLAPPEDWINDOW. OR'd in, not assigned: whatever else the
    // style carries survives:
    //     style |= WS_OVERLAPPEDWINDOW; SetWindowLongW(GWL_STYLE, style)
    // (over SDL the framed style is set whole; display_sdl.cpp says what that drops).
    Platform::Window::SetFramed(hwnd, true);

    // The desired CLIENT rect, at the origin.
    Platform::Rect rect;
    rect.left   = 0;
    rect.top    = 0;
    rect.right  = backBufferWidth;
    rect.bottom = backBufferHeight;

    // GetWindowPlacement (length 0x2C); its showCmd.
    // Retail tests showCmd against two ordinary SW_ values and two large
    // constants; all four are kept because only the DLL knows why the large
    // ones are there, and dropping a disjunct changes which branch runs.
    const uint32_t showCmd = Platform::Window::GetShowCmd(hwnd);
    const bool maximized = (showCmd == Platform::Window::kStyleMaximize ||
                            showCmd == Platform::Window::kShowCmdMaximized);   // SW_MAXIMIZE
    const bool minimized = (showCmd == Platform::Window::kStyleMinimize ||
                            showCmd == Platform::Window::kShowCmdMinimized);   // SW_MINIMIZE

    // Grow the client rect into a window rect for this style:
    //     AdjustWindowRect(&rect, style, FALSE)
    Platform::Window::AdjustWindowRect(hwnd, &rect);

    // A maximized window is placed at the origin; otherwise at the persisted
    // window position.
    int32_t offX = 0, offY = 0;
    if (!maximized) {
        offX = cfg->windowX;
        offY = cfg->windowY;
    }

    // The probe rectangle handed to MonitorFromRect. `bottom` takes offX, not
    // offY — retail's, see the banner.
    Platform::Rect probe;
    probe.left   = rect.left   + offX;
    probe.right  = rect.right  + offX;
    probe.top    = rect.top    + offY;
    probe.bottom = rect.bottom + offX;

    // MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) == NULL
    if (!Platform::Window::RectOnAnyDisplay(&probe)) {
        // The window would land off every monitor: centre it on the desktop.
        Platform::Rect desk;
        // GetWindowRect(GetDesktopWindow(), &desk)
        Platform::Window::GetPrimaryDisplayRect(&desk);

        // Retail's halving is SAR 1 on a CDQ-corrected difference, i.e. a
        // signed divide by two that rounds toward zero.
        offX = ((desk.right - desk.left) / 2) - ((rect.right - rect.left) / 2);
        offY = ((desk.bottom - desk.top) / 2) - ((rect.bottom - rect.top) / 2);

        // A minimized window keeps its persisted origin — the centred values
        // are used for THIS placement but are not written back.
        if (!minimized) {
            cfg->windowX = offX;
            cfg->windowY = offY;
        }
    }

    Platform::Window::SetWindowPos(hwnd, Platform::Window::ZOrder::NoTopmost,
                 offX, offY,
                 rect.right - rect.left,
                 rect.bottom - rect.top,
                 Platform::Window::kPosFrameChangedNoActivate);

    // Read the client area BACK — SetWindowPos may not have granted what was
    // asked — and publish it as the backbuffer size and the persisted window
    // size. This readback is why AdjustDisplay is on the boot path at all.
    Platform::Rect client;
    Platform::Window::GetClientRect(hwnd, &client);
    const int32_t cw = client.right - client.left;
    const int32_t ch = client.bottom - client.top;

    backBufferHeight  = ch;
    backBufferWidth   = cw;
    cfg->windowWidth  = cw;
    cfg->windowHeight = ch;

    adjustingDisplay = 0;
}
