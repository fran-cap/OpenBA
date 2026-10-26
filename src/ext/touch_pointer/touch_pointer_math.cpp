// src/ext/touch_pointer/touch_pointer_math.cpp -- ext.touch_pointer's pure decisions (NOT RETAIL;
// docs/android/P11_TOUCH_POINTER.md)

#include "ext/touch_pointer/touch_pointer_math.h"

#include <algorithm>
#include <cmath>

namespace Ext {
namespace TouchPointer {

namespace {
constexpr float kPi = 3.14159265f;
}  // namespace

void LatchQueue::Push(const PointerState& s) {
    const PointerState& last = Last();
    if (!_queue.empty() && last.left == s.left && last.right == s.right) {
        _queue.back() = s;
        return;
    }
    if (_queue.empty() && last == s) return;
    _queue.push_back(s);
}

void LatchQueue::Append(const PointerState& s, bool repeat) {
    if (!repeat && Last() == s) return;
    _queue.push_back(s);
}

PointerState LatchQueue::Pop() {
    if (!_queue.empty()) {
        _current = _queue.front();
        _queue.erase(_queue.begin());
    }
    return _current;
}

void LatchQueue::Reset(const PointerState& s) {
    _queue.clear();
    _current = s;
}

bool IsTap(float seconds, float travel, float shortEdge) {
    return seconds < kTapSeconds && travel < kTapSlopFraction * shortEdge;
}

void AimPoint(float fx, float fy, float lift, float& ax, float& ay) {
    ax = fx;
    ay = fy - (lift > 0.0f ? lift : 0.0f);
    if (ay < 0.0f) ay = 0.0f;
}

void EdgePan(float x, float y, int32_t w, int32_t h, int32_t& dx, int32_t& dy) {
    const float shortEdge = static_cast<float>(w < h ? w : h);
    const float band = kEdgeBandFraction * shortEdge;
    dx = 0;
    dy = 0;
    if (w <= 0 || h <= 0) return;
    if (x < band) dx = -1;
    if (x >= static_cast<float>(w) - band) dx = 1;
    if (y < band) dy = -1;
    if (y >= static_cast<float>(h) - band) dy = 1;
}

float AngleDelta(float a, float b) {
    float d = std::fmod(a - b, 360.0f);
    if (d <= -180.0f) d += 360.0f;
    if (d > 180.0f) d -= 360.0f;
    return d;
}

void TwoFingers::Begin(float x1, float y1, float x2, float y2, float shortEdge) {
    _shortEdge = shortEdge > 0.0f ? shortEdge : 1.0f;
    _cx = (x1 + x2) * 0.5f;
    _cy = (y1 + y2) * 0.5f;
    _dist = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    if (_dist < 1.0f) _dist = 1.0f;
    _angle = std::atan2(y2 - y1, x2 - x1) * 180.0f / kPi;
    _startCx = _cx;
    _startCy = _cy;
    _startDist = _dist;
    _startAngle = _angle;
    _pan = false;
    _pinch = false;
    _spin = false;
}

GestureStep TwoFingers::Step(float x1, float y1, float x2, float y2) {
    const float cx = (x1 + x2) * 0.5f;
    const float cy = (y1 + y2) * 0.5f;
    float dist = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    if (dist < 1.0f) dist = 1.0f;
    const float angle = std::atan2(y2 - y1, x2 - x1) * 180.0f / kPi;

    // Engage each component once its total since Begin passes its threshold; from then on it follows.
    const float travel = std::sqrt((cx - _startCx) * (cx - _startCx) + (cy - _startCy) * (cy - _startCy));
    if (!_pan && travel > kPanEngageFraction * _shortEdge) _pan = true;
    if (!_pinch && std::fabs(std::log(dist / _startDist)) > kPinchEngageLog) _pinch = true;
    if (!_spin && std::fabs(AngleDelta(angle, _startAngle)) > kSpinEngageDegrees) _spin = true;

    GestureStep out;
    if (_pan) {
        out.dragX = (cx - _cx) / _shortEdge;
        out.dragY = (cy - _cy) / _shortEdge;
    }
    if (_pinch) out.zoomSteps = std::log(dist / _dist) / kPinchLogPerStep;
    if (_spin) out.yawDegrees = AngleDelta(angle, _angle);
    _cx = cx;
    _cy = cy;
    _dist = dist;
    _angle = angle;
    return out;
}

std::vector<Sample> RingSamples(float x, float y, float radius) {
    std::vector<Sample> out;
    out.push_back(Sample{x, y, 0.0f});
    if (radius <= 0.0f) return out;
    const int32_t counts[3] = {8, 12, 16};
    for (int32_t ring = 0; ring < 3; ++ring) {
        const float r = radius * static_cast<float>(ring + 1) / 3.0f;
        const int32_t n = counts[ring];
        for (int32_t i = 0; i < n; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
            out.push_back(Sample{x + r * std::cos(a), y + r * std::sin(a), r});
        }
    }
    return out;
}

std::vector<Hit> Candidates(const std::vector<Hit>& hits) {
    std::vector<Hit> out;
    for (const Hit& h : hits) {
        if (h.entity == 0) continue;
        bool seen = false;
        for (Hit& o : out) {
            if (o.entity != h.entity) continue;
            seen = true;
            if (h.sample.distance < o.sample.distance) o.sample = h.sample;
        }
        if (!seen) out.push_back(h);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Hit& a, const Hit& b) { return a.sample.distance < b.sample.distance; });
    return out;
}

int32_t ChooseCandidate(const std::vector<Hit>& candidates, uintptr_t previous) {
    if (candidates.empty()) return -1;
    if (previous == 0) return 0;
    const int32_t n = static_cast<int32_t>(candidates.size());
    for (int32_t i = 0; i < n; ++i) {
        if (candidates[i].entity == previous) return (i + 1) % n;
    }
    return 0;
}

Bar LayoutBar(int32_t width, int32_t height, int32_t cell, int32_t gap, int32_t frame, int32_t margin,
              int32_t aboveY, uint32_t shown) {
    Bar bar;
    int32_t count = 0;
    for (int32_t i = 0; i < kButtonCount; ++i) {
        if (shown & (1u << i)) ++count;
    }
    if (count == 0 || cell <= 0) return bar;
    const int32_t boxW = 2 * frame + count * cell + (count - 1) * gap;
    const int32_t boxH = 2 * frame + cell;
    const int32_t bottom = aboveY > 0 ? aboveY - gap : height - margin;
    bar.box = Rect{width - margin - boxW, bottom - boxH, width - margin, bottom};
    int32_t x = bar.box.x1 + frame;
    for (int32_t i = 0; i < kButtonCount; ++i) {
        if (!(shown & (1u << i))) continue;
        bar.button[i] = Rect{x, bar.box.y1 + frame, x + cell, bar.box.y1 + frame + cell};
        x += cell + gap;
    }
    return bar;
}

int32_t HitButton(const Bar& bar, int32_t x, int32_t y) {
    for (int32_t i = 0; i < kButtonCount; ++i) {
        if (!bar.button[i].Empty() && bar.button[i].Contains(x, y)) return i;
    }
    return -1;
}

}  // namespace TouchPointer
}  // namespace Ext
