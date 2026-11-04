// ============================================================================
// tests/unit/display_geometry_test.cpp -- outer / client arithmetic, the display probe
// ============================================================================
// Phase D.4 SDL step W4. src/platform/common/display_geometry.cpp carries the Win32
// rect arithmetic Video::Device::AdjustDisplay was transcribed against, for the SDL
// backend that places client areas: AdjustWindowRect's growth, SetWindowPos's outer rect
// turned back into a client rect, and MonitorFromRect(DEFAULTTONULL)'s "on no monitor"
// answer that SDL_GetDisplayForRect cannot give. Fake display bounds, no SDL.
// ============================================================================
#include "unit.h"

#include <cstdint>

#include "display_geometry.h"

using Platform::Rect;
using Platform::Window::ClientRectToOuter;
using Platform::Window::FrameBorders;
using Platform::Window::OuterToClient;
using Platform::Window::RectTouchesAnyDisplay;
using Platform::Window::UsableFrameMeasurement;

namespace {

// A Windows 10 WS_OVERLAPPEDWINDOW frame at 96 dpi: 8 px sides and bottom, 31 px top.
constexpr FrameBorders kFrame{31, 8, 8, 8};

Rect R(int32_t l, int32_t t, int32_t r, int32_t b) { return Rect{l, t, r, b}; }

}  // namespace

UNIT_TEST(display_geometry_client_to_outer_is_adjustwindowrect) {
    Rect r = R(0, 0, 1280, 720);
    ClientRectToOuter(&r, kFrame);
    CHECK_EQ(r.left, -8);
    CHECK_EQ(r.top, -31);
    CHECK_EQ(r.right, 1288);
    CHECK_EQ(r.bottom, 728);
    // A borderless window's frame is empty: the rect is unchanged.
    Rect p = R(0, 0, 1920, 1080);
    ClientRectToOuter(&p, FrameBorders{0, 0, 0, 0});
    CHECK_EQ(p.right, 1920);
    CHECK_EQ(p.bottom, 1080);
}

UNIT_TEST(display_geometry_outer_to_client_round_trip) {
    Rect r = R(0, 0, 1280, 720);
    ClientRectToOuter(&r, kFrame);
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    // AdjustDisplay's SetWindowPos(offX, offY, r.w, r.h) with the outer size.
    OuterToClient(100, 50, r.right - r.left, r.bottom - r.top, kFrame, &cx, &cy, &cw, &ch);
    CHECK_EQ(cx, 108);
    CHECK_EQ(cy, 81);
    CHECK_EQ(cw, 1280);
    CHECK_EQ(ch, 720);
}

// Retail's persisted origin is the CLIENT origin (MoveWindow: ClientToScreen of 0,0), and
// AdjustDisplay places the OUTER rect there: each save-and-reopen moves the client by the
// frame. The arithmetic must reproduce that, not "fix" it.
UNIT_TEST(display_geometry_saved_origin_drift_is_retail) {
    const int32_t savedX = 300, savedY = 200;     // Configuration::windowX / Y
    Rect r = R(0, 0, 1024, 768);
    ClientRectToOuter(&r, kFrame);
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OuterToClient(savedX, savedY, r.right - r.left, r.bottom - r.top, kFrame, &cx, &cy, &cw, &ch);
    CHECK_EQ(cx, savedX + kFrame.left);
    CHECK_EQ(cy, savedY + kFrame.top);
}

UNIT_TEST(display_geometry_probe_on_two_displays) {
    const Rect displays[2] = { R(0, 0, 1920, 1080), R(1920, 0, 4480, 1440) };
    CHECK(RectTouchesAnyDisplay(R(100, 100, 900, 700), displays, 2));
    CHECK(RectTouchesAnyDisplay(R(2000, 100, 2800, 700), displays, 2));
    // Straddling the seam touches both.
    CHECK(RectTouchesAnyDisplay(R(1800, 100, 2100, 700), displays, 2));
    // Partly off the desktop still touches a display (MonitorFromRect is non-NULL).
    CHECK(RectTouchesAnyDisplay(R(-500, -500, 10, 10), displays, 2));
}

UNIT_TEST(display_geometry_probe_off_every_display_is_null) {
    const Rect displays[2] = { R(0, 0, 1920, 1080), R(1920, 0, 4480, 1440) };
    // Below the shorter display, left of the taller one's span: no monitor.
    CHECK(!RectTouchesAnyDisplay(R(100, 1100, 900, 1400), displays, 2));
    // Far off.
    CHECK(!RectTouchesAnyDisplay(R(-5000, -5000, -4000, -4000), displays, 2));
    // Sharing only an edge is no shared area.
    CHECK(!RectTouchesAnyDisplay(R(-800, 0, 0, 600), displays, 2));
    // An empty rect touches nothing; no displays, nothing either.
    CHECK(!RectTouchesAnyDisplay(R(100, 100, 100, 700), displays, 2));
    CHECK(!RectTouchesAnyDisplay(R(100, 100, 900, 700), displays, 0));
}

// AdjustDisplay's probe as retail builds it: `bottom` takes the X offset (0x1800f7f9c,
// EDI not ESI). With a far-right saved X and a small Y on a single display the probe
// collapses to an empty rect and reads "off every monitor", so the window is centred.
UNIT_TEST(display_geometry_retail_probe_bottom_uses_x_offset) {
    const Rect one[1] = { R(0, 0, 1920, 1080) };
    Rect rect = R(0, 0, 800, 600);
    ClientRectToOuter(&rect, kFrame);
    const int32_t offX = 100, offY = 2000;   // a Y below the display, an X on it
    Rect probe;
    probe.left   = rect.left   + offX;
    probe.right  = rect.right  + offX;
    probe.top    = rect.top    + offY;
    probe.bottom = rect.bottom + offX;       // retail's
    // top 1969 > bottom 708: empty, so no monitor; a correct probe would also miss here.
    CHECK(!RectTouchesAnyDisplay(probe, one, 1));
    // With a Y on the display and a large X, retail's bottom grows past top: on screen.
    const int32_t offX2 = 1000, offY2 = 100;
    probe.left   = rect.left   + offX2;
    probe.right  = rect.right  + offX2;
    probe.top    = rect.top    + offY2;
    probe.bottom = rect.bottom + offX2;
    CHECK(RectTouchesAnyDisplay(probe, one, 1));
}

// ---- the windowed opening size (the 1x1-collapse regression) ----------------------------
using Platform::Window::InitialWindowPoints;

UNIT_TEST(display_geometry_initial_window_unsaved_uses_video_size_as_points) {
    int32_t w = 0, h = 0;
    InitialWindowPoints(-1, -1, 1280, 720, 2.0f, 1700, 1000, &w, &h);   // Retina, nothing saved
    CHECK_EQ(w, 1280);
    CHECK_EQ(h, 720);
}

UNIT_TEST(display_geometry_initial_window_saved_pixels_round_trip) {
    int32_t w = 0, h = 0;
    InitialWindowPoints(2000, 1200, 1280, 720, 2.0f, 1700, 1000, &w, &h);
    CHECK_EQ(w, 1000);
    CHECK_EQ(h, 600);
}

UNIT_TEST(display_geometry_initial_window_guards) {
    int32_t w = 0, h = 0;
    InitialWindowPoints(0, 0, 0, 0, 2.0f, 0, 0, &w, &h);       // nothing at all: default
    CHECK_EQ(w, 1280);
    CHECK_EQ(h, 720);
    InitialWindowPoints(-1, 5, -3, 0, 1.0f, 0, 0, &w, &h);      // negative / zero: default
    CHECK_EQ(w, 1280);
    CHECK_EQ(h, 720);
    InitialWindowPoints(20, 20, 1280, 720, 2.0f, 0, 0, &w, &h); // tiny saved size: the minimum
    CHECK_EQ(w, 640);
    CHECK_EQ(h, 360);
    InitialWindowPoints(-1, -1, 3000, 2000, 2.0f, 1700, 1000, &w, &h);   // larger than the screen
    CHECK_EQ(w, 1700);
    CHECK_EQ(h, 1000);
    InitialWindowPoints(-1, -1, 1280, 720, 2.0f, 500, 300, &w, &h);      // screen under the minimum
    CHECK_EQ(w, 500);
    CHECK_EQ(h, 300);
}

UNIT_TEST(display_geometry_resolution_list_filters_to_the_usable_screen) {
    int32_t m[32][2];
    // A 1512x982-point Retina screen: usable about 3024 x 1850 pixels.
    int32_t n = Platform::Window::BuildResolutionList(3024, 1850, 3024, 1964, m, 32);
    bool has1080 = false, has2880x1800 = false, hasNative = false, has4k = false;
    for (int32_t i = 0; i < n; ++i) {
        CHECK(m[i][0] <= 3024);
        has1080 |= (m[i][0] == 1920 && m[i][1] == 1080);
        has2880x1800 |= (m[i][0] == 2880 && m[i][1] == 1800);
        hasNative |= (m[i][0] == 3024 && m[i][1] == 1964);
        has4k |= (m[i][0] == 3840);
    }
    CHECK(has1080);
    CHECK(has2880x1800);
    CHECK(hasNative);   // the display's own size is always offered
    CHECK(!has4k);      // bigger than the screen: filtered
}

UNIT_TEST(display_geometry_resolution_list_never_empty_and_no_duplicates) {
    int32_t m[32][2];
    int32_t n = Platform::Window::BuildResolutionList(100, 100, 0, 0, m, 32);   // nothing fits
    CHECK_EQ(n, 1);
    CHECK_EQ(m[0][0], 1280);
    n = Platform::Window::BuildResolutionList(0, 0, 1920, 1080, m, 32);          // unknown screen, native in list
    int32_t count1080 = 0;
    for (int32_t i = 0; i < n; ++i) count1080 += (m[i][0] == 1920 && m[i][1] == 1080);
    CHECK_EQ(count1080, 1);
    n = Platform::Window::BuildResolutionList(0, 0, 0, 0, m, 3);                 // cap respected
    CHECK_EQ(n, 3);
}

// The macOS opening placement (window_darwin.cpp) at density 1: one usable display at 0,25
// 1920 x 1050 points, a 28-point title bar. WindowX / WindowY come from settings.cfg.
UNIT_TEST(display_geometry_saved_origin_on_display) {
    const Rect disp = R(0, 25, 1920, 1075);
    CHECK(Platform::Window::SavedOriginOnDisplay(100, 100, 800, 600, 28, disp));
    CHECK(!Platform::Window::SavedOriginOnDisplay(5000, 100, 800, 600, 28, disp));   // off every display
    CHECK(!Platform::Window::SavedOriginOnDisplay(100, 40, 800, 600, 28, disp));     // title bar above the top
}

// Review F-319 / F-323: an origin near the int limits (px + cw, py + ch, py - top past INT_MAX or
// INT_MIN) is simply not on the display. Under -fsanitize=signed-integer-overflow the old int sums trap.
UNIT_TEST(display_geometry_saved_origin_at_the_int_limits_is_not_placed) {
    const Rect disp = R(0, 25, 1920, 1075);
    CHECK(!Platform::Window::SavedOriginOnDisplay(2147483008, 0, 800, 600, 28, disp));
    CHECK(!Platform::Window::SavedOriginOnDisplay(0, 2147483008, 800, 600, 28, disp));
    CHECK(!Platform::Window::SavedOriginOnDisplay(0, INT32_MIN, 800, 600, 28, disp));
    CHECK(!Platform::Window::SavedOriginOnDisplay(INT32_MIN, 100, 800, 600, 28, disp));
}

UNIT_TEST(display_geometry_fit_client_to_usable) {
    const FrameBorders frame{28, 0, 0, 0};
    int32_t x = 100, y = 100, w = 800, h = 600;
    Platform::Window::FitClientToUsable(0, 25, 1920, 1050, frame, &x, &y, &w, &h);
    CHECK_EQ(x, 100);
    CHECK_EQ(y, 100);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 600);
    x = 1500;
    y = 900;
    w = 4000;
    h = 3000;
    Platform::Window::FitClientToUsable(0, 25, 1920, 1050, frame, &x, &y, &w, &h);
    CHECK_EQ(w, 1920);
    CHECK_EQ(h, 1022);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 53);
}

// Review F-323: a client rect at the int limits slides onto the screen like any other off-screen one.
UNIT_TEST(display_geometry_fit_client_at_the_int_limits) {
    const FrameBorders frame{28, 0, 0, 0};
    int32_t x = 2147483000, y = 100, w = 800, h = 600;
    Platform::Window::FitClientToUsable(0, 25, 1920, 1050, frame, &x, &y, &w, &h);
    CHECK_EQ(x, 1920 - 800);
    CHECK_EQ(y, 100);
    x = 100;
    y = INT32_MIN;
    Platform::Window::FitClientToUsable(0, 25, 1920, 1050, frame, &x, &y, &w, &h);
    CHECK_EQ(x, 100);
    CHECK_EQ(y, 25 + 28);
    x = INT32_MIN;
    y = INT32_MAX;
    Platform::Window::FitClientToUsable(0, 25, 1920, 1050, frame, &x, &y, &w, &h);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 25 + 1050 - 600);
}

// A frame measured while a minimised 2560 x 1440 window is being restored: the outer rect
// is still the iconic 160 x 28, so each pair of sides sums to minus the missing size.
// Taken as the frame, it turned the fullscreen branch's SetWindowPos(2560 x 1440) into a
// 4960 x 2852 client area. It must be refused, and a real frame kept.
UNIT_TEST(display_geometry_frame_measured_mid_restore_is_refused) {
    const FrameBorders midRestore{-706, -1200, -706, -1200};
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OuterToClient(0, 0, 2560, 1440, midRestore, &cx, &cy, &cw, &ch);
    CHECK_EQ(cw, 4960);
    CHECK_EQ(ch, 2852);
    CHECK(!UsableFrameMeasurement(midRestore, false));
    CHECK(!UsableFrameMeasurement(midRestore, true));
    CHECK(!UsableFrameMeasurement(FrameBorders{31, 8, 8, -1}, true));

    CHECK(UsableFrameMeasurement(kFrame, true));
    CHECK(UsableFrameMeasurement(FrameBorders{0, 0, 0, 0}, false));   // borderless: empty frame
    CHECK(!UsableFrameMeasurement(FrameBorders{0, 0, 0, 0}, true));   // framed but not applied yet
}
