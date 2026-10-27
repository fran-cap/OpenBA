// src/ext/touch_mode/touch_math.cpp -- ext.touch_mode's pure decisions (NOT RETAIL; TOUCH_MODE.md)

#include "ext/touch_mode/touch_math.h"

#include <cmath>

namespace Ext {
namespace TouchMode {

namespace {

int32_t Round(float v) {
    return static_cast<int32_t>(std::floor(v + 0.5f));
}

Rect Make(int32_t x, int32_t y, int32_t w, int32_t h) {
    return Rect{x, y, x + w, y + h};
}

}  // namespace

int32_t Margin(int32_t width, int32_t height, int32_t phoneMargin) {
    if (phoneMargin > 0) return phoneMargin;
    const int32_t shortEdge = width < height ? width : height;
    if (shortEdge <= 0) return 0;
    return Round(static_cast<float>(shortEdge) * kDesktopMarginFraction);
}

Cluster LayoutCluster(int32_t width, int32_t height, int32_t margin) {
    Cluster c;
    const int32_t shortEdge = width < height ? width : height;
    if (shortEdge <= 0) return c;
    const float s = static_cast<float>(shortEdge);
    c.cell = Round(s * kCellFraction);
    c.gap = Round(s * kGapFraction);
    c.frame = Round(s * kFrameFraction);
    c.margin = margin;

    const int32_t padSide = 3 * c.cell;
    const int32_t padX = margin + c.frame;
    const int32_t padY = height - margin - c.frame - padSide;
    c.control[kPad] = Make(padX, padY, padSide, padSide);

    // The rotate pair above the pad: two halves of its width, 0.8 of a cell tall.
    const int32_t rotateW = (padSide - c.gap) / 2;
    const int32_t rotateH = Round(0.8f * static_cast<float>(c.cell));
    const int32_t rotateY = padY - c.gap - rotateH;
    c.control[kRotateLeft] = Make(padX, rotateY, rotateW, rotateH);
    c.control[kRotateRight] = Make(padX + padSide - rotateW, rotateY, rotateW, rotateH);

    // The zoom pair right of the pad, its height split in two.
    const int32_t zoomX = padX + padSide + c.gap;
    const int32_t zoomW = Round(1.4f * static_cast<float>(c.cell));
    const int32_t zoomH = (padSide - c.gap) / 2;
    c.control[kZoomIn] = Make(zoomX, padY, zoomW, zoomH);
    c.control[kZoomOut] = Make(zoomX, padY + padSide - zoomH, zoomW, zoomH);

    c.box = Rect{padX - c.frame, rotateY - c.frame, zoomX + zoomW + c.frame, padY + padSide + c.frame};
    c.barRegion = Rect{c.box.x2 + 2 * c.gap, margin, width - margin, height - margin};
    return c;
}

float ToolbarScale(float requested, int32_t regionWidth, int32_t ownWidth) {
    float k = requested;
    if (ownWidth > 0 && regionWidth > 0) {
        const float fit = static_cast<float>(regionWidth) / static_cast<float>(ownWidth);
        if (fit < k) k = fit;
    }
    if (k < 1.0f) k = 1.0f;
    return k;
}

Control HitControl(const Cluster& c, int32_t x, int32_t y) {
    for (int32_t i = 0; i < kControlCount; ++i) {
        if (c.control[i].Contains(x, y)) return static_cast<Control>(i);
    }
    return kNone;
}

PadPan PadVector(const Rect& pad, int32_t x, int32_t y, PadSpeed mode) {
    PadPan p;
    const float side = static_cast<float>(pad.Width() < pad.Height() ? pad.Width() : pad.Height());
    const float ox = static_cast<float>(x) - 0.5f * static_cast<float>(pad.x1 + pad.x2);
    const float oy = static_cast<float>(y) - 0.5f * static_cast<float>(pad.y1 + pad.y2);
    const float dead = kDeadZoneFraction * side;
    const float r = std::sqrt(ox * ox + oy * oy);
    if (r <= dead || r <= 0.0f) return p;
    float speed = 1.0f;
    const float edge = 0.5f * side;
    if (mode == kPadProportional && edge > dead) {
        speed = (r - dead) / (edge - dead);
        if (speed > 1.0f) speed = 1.0f;
    }
    p.x = ox / r * speed;
    p.y = oy / r * speed;
    return p;
}

Control Capture::Step(const Cluster& c, bool down, int32_t x, int32_t y) {
    if (!down) {
        control = kNone;
        wasDown = false;
        return kNone;
    }
    if (!wasDown) {
        // The press: whatever control it starts on (or none) holds until the release.
        wasDown = true;
        control = HitControl(c, x, y);
    }
    return control;
}

float HeldZoomSteps(int32_t dir, float dt) {
    if (dt <= 0.0f || dir == 0) return 0.0f;
    return static_cast<float>(dir > 0 ? 1 : -1) * kZoomStepsPerSecond * dt;
}

void ApplyHeldZoom(float steps, float stepDistance, float zMin, float zMax, float& target, float& distance) {
    if (steps == 0.0f) return;
    float z = target - steps * stepDistance;
    if (z < zMin) z = zMin;
    if (z > zMax) z = zMax;
    distance += z - target;
    target = z;
}

float SpeedRamp::Step(bool held, float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (held) {
        level += kRampInSeconds > 0.0f ? dt / kRampInSeconds : 1.0f;
    } else {
        level -= kRampOutSeconds > 0.0f ? dt / kRampOutSeconds : 1.0f;
    }
    if (level > 1.0f) level = 1.0f;
    if (level < 0.0f) level = 0.0f;
    return level * level * (3.0f - 2.0f * level);
}

int32_t ToOwn(int32_t layout, float k) {
    return static_cast<int32_t>(static_cast<float>(layout) / k);
}

int32_t ToLayout(int32_t own, float k) {
    return Round(static_cast<float>(own) * k);
}

int32_t SubToolbarRight(int32_t layoutWidth, float k) {
    return Round(static_cast<float>(layoutWidth) * k);
}

void TooltipOffset(int32_t ulX, int32_t ulY, int32_t h, float k, int32_t& dx, int32_t& dy) {
    // Retail: ulX = x, ulY = y - h, (x, y) in own units. Wanted: (x * k, y * k - h).
    const float x = static_cast<float>(ulX);
    const float y = static_cast<float>(ulY + h);
    dx = Round(x * k) - ulX;
    dy = Round(y * k) - h - ulY;
}

float ScaleOfPercent(int32_t percent) {
    return static_cast<float>(percent) / 100.0f;
}

void FingerDragToWorld(float dragX, float dragY, float yawDeg, float distance, float& dx, float& dy) {
    const float yawR = yawDeg * 0.01745329f;
    const float cy = std::cos(yawR);
    const float sy = std::sin(yawR);
    const float s = distance * kFingerDragWorldPerEdge;
    dx = -(cy * dragX + sy * dragY) * s;
    dy = -(sy * dragX - cy * dragY) * s;
}

}  // namespace TouchMode
}  // namespace Ext
