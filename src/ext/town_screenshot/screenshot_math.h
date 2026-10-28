#pragma once
// ============================================================================
// src/ext/town_screenshot/screenshot_math.h -- ext.town_screenshot, the arithmetic of a map screenshot (NOT
// RETAIL)
// ============================================================================
// docs/extensions/handins/TOWNSCREENSHOT.md. Standard C++ only, so tests/unit/ext_town_screenshot_test.cpp
// can check it without the game.
//
// The picture is an isometric view of the whole map: a camera with no perspective (an orthographic one),
// looking down at `pitch` degrees and turned `yaw` degrees round the vertical axis. The map is a square of
// `size` cells centred on the world's origin, with ground between zLow and zHigh. Seen from that camera it
// covers a hexagon (the projection of the box); the picture is the hexagon's bounding rectangle, `ppc` pixels
// to the cell across, and the corners outside the hexagon are left transparent. A screen is far smaller than
// a picture, so the camera visits the picture a tile at a time: each tile is a screen, its edges (`margin`
// pixels, where screen-space effects such as ambient occlusion lack neighbours) are thrown away, and the
// middles are laid side by side.
//
// Picture coordinates: u to the right and v up are the camera's own axes in world units; pixel (px, py) has
// its centre at u = umin + (px + 0.5) / ppc and v = vmax - (py + 0.5) / ppc (py counts down from the top).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Ext {
namespace TownScreenshot {

constexpr float kDegToRad = 0.017453292f;

// The choices on the page.
constexpr int kQualityCount = 4;
constexpr int kAngleCount = 2;
constexpr int kTurnCount = 4;

// Pixels to the cell across the picture, by quality (Draft, Standard, High, Maximum).
inline float QualityPixelsPerCell(int quality) {
    static const float kPpc[kQualityCount] = { 4.0f, 8.0f, 12.0f, 16.0f };
    return kPpc[quality < 0 ? 0 : (quality >= kQualityCount ? kQualityCount - 1 : quality)];
}

// How far the camera looks down: 0 is the classic game look (2:1 pixel art), 1 true isometric (the angle at
// which the three axes of a cube are drawn the same length).
inline float AnglePitch(int angle) {
    return angle == 1 ? 35.26439f : 30.0f;
}

// The turn round the vertical axis: the diagonal view, then each quarter turn.
inline float TurnYaw(int turn) {
    return 45.0f + 90.0f * static_cast<float>(turn < 0 ? 0 : turn % kTurnCount);
}

// The camera's axes in the world: right, up (on the screen) and the way it looks. The camera sits at
// look - f * distance.
struct Basis {
    float rx, ry, rz;
    float ux, uy, uz;
    float fx, fy, fz;
};

inline Basis MakeBasis(float pitchDeg, float yawDeg) {
    const float cp = std::cos(pitchDeg * kDegToRad);
    const float sp = std::sin(pitchDeg * kDegToRad);
    const float cy = std::cos(yawDeg * kDegToRad);
    const float sy = std::sin(yawDeg * kDegToRad);
    Basis b;
    b.rx = cy;
    b.ry = sy;
    b.rz = 0.0f;
    b.ux = -sy * sp;
    b.uy = cy * sp;
    b.uz = cp;
    b.fx = -sy * cp;
    b.fy = cy * cp;
    b.fz = -sp;
    return b;
}

// The picture: its extent in the camera's plane and its size in pixels.
struct Footprint {
    float umin = 0.0f;
    float umax = 0.0f;
    float vmin = 0.0f;
    float vmax = 0.0f;
    int   width = 0;
    int   height = 0;
};

// The extent of a map of `size` cells (centred on the origin) whose ground and what stands on it lie between
// zLow and zHigh, at `ppc` pixels to the cell.
inline Footprint ComputeFootprint(const Basis& b, int size, float zLow, float zHigh, float ppc) {
    const float half = static_cast<float>(size) * 0.5f;
    Footprint f;
    f.umin = 1e30f;
    f.umax = -1e30f;
    f.vmin = 1e30f;
    f.vmax = -1e30f;
    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                const float x = ix ? half : -half;
                const float y = iy ? half : -half;
                const float z = iz ? zHigh : zLow;
                const float u = x * b.rx + y * b.ry + z * b.rz;
                const float v = x * b.ux + y * b.uy + z * b.uz;
                f.umin = std::min(f.umin, u);
                f.umax = std::max(f.umax, u);
                f.vmin = std::min(f.vmin, v);
                f.vmax = std::max(f.vmax, v);
            }
        }
    }
    f.width = static_cast<int>(std::ceil((f.umax - f.umin) * ppc));
    f.height = static_cast<int>(std::ceil((f.vmax - f.vmin) * ppc));
    return f;
}

// The pixels per cell to use: the one asked for, or less when the picture would hold more than `maxPixels`
// (a picture is held in memory whole until it is written). Never below 1.
inline float ChoosePixelsPerCell(const Basis& b, int size, float zLow, float zHigh, float wanted,
                                 int64_t maxPixels) {
    float ppc = wanted;
    for (int i = 0; i < 64 && ppc > 1.0f; ++i) {
        const Footprint f = ComputeFootprint(b, size, zLow, zHigh, ppc);
        if (static_cast<int64_t>(f.width) * f.height <= maxPixels) break;
        ppc *= 0.95f;
    }
    return ppc < 1.0f ? 1.0f : ppc;
}

// How the screen-sized tiles lie on the picture.
struct TileGrid {
    int margin = 0;     // pixels thrown away on each side of a tile
    int payloadW = 0;   // what a tile contributes: the screen less the margins
    int payloadH = 0;
    int cols = 0;
    int rows = 0;
    int count() const { return cols * rows; }
};

inline TileGrid MakeGrid(int imageW, int imageH, int screenW, int screenH, int margin) {
    TileGrid g;
    g.margin = margin;
    g.payloadW = std::max(1, screenW - 2 * margin);
    g.payloadH = std::max(1, screenH - 2 * margin);
    g.cols = (imageW + g.payloadW - 1) / g.payloadW;
    g.rows = (imageH + g.payloadH - 1) / g.payloadH;
    return g;
}

// The camera's aim for a tile, in the picture's plane (u right, v up), so that the tile's middle falls on the
// picture's pixels [col * payloadW, ...) by [row * payloadH, ...).
inline void TileCentre(const Footprint& f, const TileGrid& g, float ppc, int col, int row, int screenW,
                       int screenH, float& cu, float& cv) {
    cu = f.umin + (static_cast<float>(col * g.payloadW) + static_cast<float>(screenW) * 0.5f -
                   static_cast<float>(g.margin)) / ppc;
    cv = f.vmax - (static_cast<float>(row * g.payloadH) + static_cast<float>(screenH) * 0.5f -
                   static_cast<float>(g.margin)) / ppc;
}

// The world point the camera looks at for a tile centre (cu, cv): the point of the plane through the origin
// that the camera sees there.
inline void TileLookAt(const Basis& b, float cu, float cv, float& x, float& y, float& z) {
    x = cu * b.rx + cv * b.ux;
    y = cu * b.ry + cv * b.uy;
    z = cu * b.rz + cv * b.uz;
}

// Whether the picture's pixel (px, py) shows the map: whether some ground height in [zLow, zHigh] puts a
// point of the map square on the line of sight through that pixel. The line through a pixel hits height z at
// x(z), y(z), both linear in z, so the heights inside the square form an interval.
inline bool PixelInsideMap(const Basis& b, const Footprint& f, float ppc, int size, float zLow, float zHigh,
                           int px, int py) {
    const float half = static_cast<float>(size) * 0.5f;
    const float u = f.umin + (static_cast<float>(px) + 0.5f) / ppc;
    const float v = f.vmax - (static_cast<float>(py) + 0.5f) / ppc;
    // x * rx + y * ry = u and x * ux + y * uy = v - z * uz; the determinant is rx * uy - ry * ux (sin pitch)
    const float det = b.rx * b.uy - b.ry * b.ux;
    if (std::fabs(det) < 1e-6f) return true;   // looking along the ground (pitch 0): no line of sight to solve
    // x(z) = x0 + kx * z, y(z) = y0 + ky * z
    const float x0 = (u * b.uy - b.ry * v) / det;
    const float kx = (b.ry * b.uz) / det;
    const float y0 = (b.rx * v - b.ux * u) / det;
    const float ky = (-b.rx * b.uz) / det;
    float lo = zLow;
    float hi = zHigh;
    // |x0 + kx z| <= half and |y0 + ky z| <= half
    const float k[2] = { kx, ky };
    const float c[2] = { x0, y0 };
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(k[i]) < 1e-9f) {
            if (std::fabs(c[i]) > half) return false;
            continue;
        }
        float z1 = (-half - c[i]) / k[i];
        float z2 = (half - c[i]) / k[i];
        if (z1 > z2) std::swap(z1, z2);
        lo = std::max(lo, z1);
        hi = std::min(hi, z2);
        if (lo > hi) return false;
    }
    return lo <= hi;
}

}  // namespace TownScreenshot
}  // namespace Ext
