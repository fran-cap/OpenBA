// ============================================================================
// src/platform/common/display_geometry.cpp -- outer / client rect arithmetic, display probe
// ============================================================================
// Phase D.4 SDL step W4. See display_geometry.h. Pure.
// ============================================================================

#include "display_geometry.h"

#include <cmath>

namespace Platform {
namespace Window {

void ClientRectToOuter(Rect* r, const FrameBorders& b) {
    r->left   -= b.left;
    r->top    -= b.top;
    r->right  += b.right;
    r->bottom += b.bottom;
}

bool UsableFrameMeasurement(const FrameBorders& b, bool framed) {
    if (b.top < 0 || b.left < 0 || b.bottom < 0 || b.right < 0) return false;
    return !framed || (b.top | b.left | b.bottom | b.right) != 0;
}

void OuterToClient(int32_t x, int32_t y, int32_t w, int32_t h, const FrameBorders& b,
                   int32_t* clientX, int32_t* clientY, int32_t* clientW, int32_t* clientH) {
    *clientX = x + b.left;
    *clientY = y + b.top;
    *clientW = w - b.left - b.right;
    *clientH = h - b.top - b.bottom;
}

bool RectTouchesAnyDisplay(const Rect& r, const Rect* displays, int count) {
    if (r.right <= r.left || r.bottom <= r.top) return false;
    for (int i = 0; i < count; ++i) {
        const Rect& d = displays[i];
        if (r.left < d.right && d.left < r.right && r.top < d.bottom && d.top < r.bottom)
            return true;
    }
    return false;
}


namespace {
float SafeDensity(float d) { return d > 0.0f ? d : 1.0f; }
}  // namespace

int32_t PointsToPixels(float points, float density) {
    return static_cast<int32_t>(std::lround(points * SafeDensity(density)));
}

int32_t PixelsToPointsFloor(int32_t pixels, float density) {
    return static_cast<int32_t>(std::floor(static_cast<float>(pixels) / SafeDensity(density) + 1e-4f));
}

int32_t PixelsToPointsCeil(int32_t pixels, float density) {
    return static_cast<int32_t>(std::ceil(static_cast<float>(pixels) / SafeDensity(density) - 1e-4f));
}

int32_t PixelsToPointsRound(int32_t pixels, float density) {
    return static_cast<int32_t>(std::lround(static_cast<float>(pixels) / SafeDensity(density)));
}

int32_t CursorPointToPixel(float pointsRel, int32_t extentPoints, float density) {
    const float d = SafeDensity(density);
    int32_t px = static_cast<int32_t>(std::floor(pointsRel * d));
    if (d > 1.0f && pointsRel >= static_cast<float>(extentPoints) - 1.0f &&
        pointsRel <= static_cast<float>(extentPoints)) {
        const int32_t last = static_cast<int32_t>(std::lround(static_cast<float>(extentPoints) * d)) - 1;
        px = last;
    }
    return px;
}

void InitialWindowPoints(int32_t savedPxW, int32_t savedPxH, int32_t videoW, int32_t videoH,
                         float density, int32_t maxW, int32_t maxH,
                         int32_t* outW, int32_t* outH) {
    int32_t w, h;
    if (savedPxW > 0 && savedPxH > 0) {
        w = PixelsToPointsRound(savedPxW, density);
        h = PixelsToPointsRound(savedPxH, density);
    } else if (videoW > 0 && videoH > 0) {
        w = videoW;
        h = videoH;
    } else {
        w = kDefaultWindowPointsW;
        h = kDefaultWindowPointsH;
    }
    if (w < kMinWindowPointsW) w = kMinWindowPointsW;
    if (h < kMinWindowPointsH) h = kMinWindowPointsH;
    if (maxW > 0 && w > maxW) w = maxW;
    if (maxH > 0 && h > maxH) h = maxH;
    *outW = w;
    *outH = h;
}

// NOT RETAIL (port-only macOS code, review F-319 / F-323): the comparisons run in int64_t. The
// origin is a settings.cfg / engine value and can sit at the int limits, where the int sums
// overflowed (undefined).
bool SavedOriginOnDisplay(int32_t x, int32_t y, int32_t cw, int32_t ch, int32_t top,
                          const Rect& display) {
    const int64_t left = x;
    const int64_t titleTop = int64_t(y) - top;
    const int64_t right = int64_t(x) + cw;
    const int64_t bottom = int64_t(y) + ch;
    if (right <= left || bottom <= titleTop) return false;
    return left < display.right && display.left < right && titleTop < display.bottom &&
           display.top < bottom && titleTop >= display.top;
}

void FitClientToUsable(int32_t ux, int32_t uy, int32_t uw, int32_t uh, const FrameBorders& frame,
                       int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
    const int32_t maxW = uw - frame.left - frame.right;
    const int32_t maxH = uh - frame.top - frame.bottom;
    if (maxW > 0 && *w > maxW) *w = maxW;
    if (maxH > 0 && *h > maxH) *h = maxH;
    if (int64_t(*y) - frame.top < uy) *y = uy + frame.top;
    if (int64_t(*y) + *h + frame.bottom > int64_t(uy) + uh) {
        *y = uy + uh - *h - frame.bottom;
    }
    if (int64_t(*x) - frame.left < ux) *x = ux + frame.left;
    if (int64_t(*x) + *w + frame.right > int64_t(ux) + uw) {
        *x = ux + uw - *w - frame.right;
    }
}


int32_t BuildResolutionList(int32_t maxW, int32_t maxH, int32_t nativeW, int32_t nativeH,
                            int32_t (*out)[2], int32_t cap) {
    static const int32_t kCandidates[][2] = {
        {1024, 768},  {1280, 720},  {1280, 800},  {1366, 768},  {1440, 900},
        {1600, 900},  {1680, 1050}, {1920, 1080}, {1920, 1200}, {2048, 1152},
        {2560, 1440}, {2560, 1600}, {2880, 1620}, {2880, 1800}, {3440, 1440},
        {3840, 2160}};
    int32_t n = 0;
    auto add = [&](int32_t w, int32_t h) {
        if (n >= cap) return;
        for (int32_t i = 0; i < n; ++i)
            if (out[i][0] == w && out[i][1] == h) return;
        out[n][0] = w;
        out[n][1] = h;
        ++n;
    };
    for (const auto& c : kCandidates) {
        if (maxW > 0 && c[0] > maxW) continue;
        if (maxH > 0 && c[1] > maxH) continue;
        add(c[0], c[1]);
    }
    if (nativeW > 0 && nativeH > 0) add(nativeW, nativeH);
    if (n == 0) add(1280, 720);
    return n;
}

}  // namespace Window
}  // namespace Platform
