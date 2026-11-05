#pragma once
// ============================================================================
// src/platform/common/display_geometry.h -- outer / client rect arithmetic, display probe
// ============================================================================
// Phase D.4 SDL step W4 (plan §A.3). Video::Device::AdjustDisplay is retail's Win32
// arithmetic on OUTER window rects: AdjustWindowRect grows the wanted client rect by the
// frame, SetWindowPos places the outer rect, MonitorFromRect(DEFAULTTONULL) asks whether
// a rect touches any monitor. SDL positions and sizes the CLIENT area and has no "is this
// rect on a display" call (SDL_GetDisplayForRect never answers "none": it returns the
// closest display). These pure helpers carry the arithmetic between the two so the SDL
// backend (src/platform/sdl/display_sdl.cpp) can keep retail's outer-rect behaviour, and
// so the unit tests can check it on fake bounds (tests/unit/display_geometry_test.cpp).
// No OS header, no SDL header.
// ============================================================================

#include <cstdint>

#include "platform_window.h"

namespace Platform {
namespace Window {

// The frame around the client area, in pixels, each side >= 0 (SDL_GetWindowBordersSize's
// four values; AdjustWindowRect's growth).
struct FrameBorders {
    int32_t top;
    int32_t left;
    int32_t bottom;
    int32_t right;
};

// AdjustWindowRect's arithmetic: grow a client rect by the frame, in place.
void ClientRectToOuter(Rect* inOut, const FrameBorders& b);

// Can a measured frame stand for the window's frame? Every side must be >= 0, and a framed
// window's frame must not be empty (a frame read before the window system applied the
// style). A negative side is what a measurement taken while a minimised window is being
// restored gives: the outer rect is still the iconic 160 x 28 while the client is already
// the full size, so the "frame" is minus the client size.
bool UsableFrameMeasurement(const FrameBorders& b, bool framed);

// SetWindowPos's arithmetic, reversed: the client rect an outer rect at (x, y), w x h
// leaves inside a frame `b`.
void OuterToClient(int32_t x, int32_t y, int32_t w, int32_t h, const FrameBorders& b,
                   int32_t* clientX, int32_t* clientY, int32_t* clientW, int32_t* clientH);

// MonitorFromRect(rect, MONITOR_DEFAULTTONULL) != NULL: does `rect` share any area with
// one of `count` display rects? An empty rect (right <= left or bottom <= top) touches
// nothing.
bool RectTouchesAnyDisplay(const Rect& rect, const Rect* displays, int count);


// ---------------------------------------------------------------------------
// Points <-> pixels (macOS Retina). SDL positions and sizes windows, and reports the
// cursor, in POINTS; the engine (back buffer, GetClientRect, the UI, the cursor) works in
// PIXELS. `density` is SDL_GetWindowPixelDensity (1.0 without SDL_WINDOW_HIGH_PIXEL_DENSITY,
// 2.0 on a Retina display with it; the SDL backend passes 1.0 on every non-Apple host, so
// these are the identity there). Positions are converted too, so the engine sees one unit
// (pixels of the window's display) for every screen coordinate.
// ---------------------------------------------------------------------------
int32_t PointsToPixels(float points, float density);   // round to nearest
int32_t PixelsToPointsFloor(int32_t pixels, float density);
int32_t PixelsToPointsCeil(int32_t pixels, float density);
int32_t PixelsToPointsRound(int32_t pixels, float density);

// A cursor coordinate along one axis, relative to the client area: `pointsRel` points from
// the client origin, `extentPoints` the client size in points. Returns the pixel the cursor
// is on (floor). The last point of the client area covers `density` pixels but the OS reports
// a cursor pinned against the edge at its first sub-pixel, so a cursor in the last point
// (and not beyond the edge) is snapped to the last PIXEL: otherwise edge scrolling at the
// right / bottom edge of a Retina window would stop one pixel short of the engine's
// "cursor at the edge" test.
int32_t CursorPointToPixel(float pointsRel, int32_t extentPoints, float density);

// ---------------------------------------------------------------------------
// The size a windowed game window OPENS at, in POINTS (what SDL_SetWindowSize takes).
// savedPxW/H: the persisted client size (Video::Configuration windowWidth/Height), which the
// engine writes in PIXELS (GetClientRect); <= 0 means never saved (-1 on a fresh install).
// videoW/H: the configured video size (VideoWidth/VideoHeight), used as POINTS when nothing
// was saved: 1280x720 means a 1280x720-point window (2560x1440 pixels on a Retina display),
// not a 640x360-point one. A zero / negative request falls back to 1280x720; the result is
// never below kMinWindowPointsW x kMinWindowPointsH, and never above maxW x maxH (the usable
// screen less the window frame; <= 0 = unknown, no cap) -- except that the cap wins over
// the minimum on a screen smaller than the minimum.
// ---------------------------------------------------------------------------
constexpr int32_t kMinWindowPointsW = 640;
constexpr int32_t kMinWindowPointsH = 360;
constexpr int32_t kDefaultWindowPointsW = 1280;
constexpr int32_t kDefaultWindowPointsH = 720;
void InitialWindowPoints(int32_t savedPxW, int32_t savedPxH, int32_t videoW, int32_t videoH,
                         float density, int32_t maxW, int32_t maxH,
                         int32_t* outW, int32_t* outH);

// The saved-origin test of the opening placement (window_darwin.cpp ApplyInitialWindowGeometry),
// in POINTS: a cw x ch client at (x, y), with a title bar `top` points above it, opens there when
// it shares area with `display` and the title bar is not above the display's top. The origin is
// settings.cfg WindowX / WindowY, so it can be any int.
bool SavedOriginOnDisplay(int32_t x, int32_t y, int32_t cw, int32_t ch, int32_t top,
                          const Rect& display);

// FitToUsableScreen's arithmetic (window_darwin.cpp), in POINTS: clamp a client of *w x *h at
// (*x, *y) inside a frame `frame` to the usable area ux, uy, uw x uh (size first, then slide the
// window so its title bar and bottom edge are on screen). The client rect comes from the engine's
// SetWindowPos, which can carry any int.
void FitClientToUsable(int32_t ux, int32_t uy, int32_t uw, int32_t uh, const FrameBorders& frame,
                       int32_t* x, int32_t* y, int32_t* w, int32_t* h);


// ---------------------------------------------------------------------------
// The resolution list the Options dialog offers (engine PIXELS). A windowed client of
// w x h pixels is a window of w/density x h/density points, so on a Retina Mac 1920x1080 is
// a 960x540-point window. A candidate is listed when it fits the usable screen: maxW x maxH
// are the usable area in PIXELS less the window frame (<= 0 = unknown, no filter). The
// display's own pixel size (nativeW x nativeH, <= 0 = unknown) is always appended when it
// is not already there, so a full screen has a mode that covers the display. The result is
// never empty (falls back to 1280x720). Returns the count written to out[i][0..1];
// `cap` bounds the output.
// ---------------------------------------------------------------------------
int32_t BuildResolutionList(int32_t maxW, int32_t maxH, int32_t nativeW, int32_t nativeH,
                            int32_t (*out)[2], int32_t cap);

}  // namespace Window
}  // namespace Platform
