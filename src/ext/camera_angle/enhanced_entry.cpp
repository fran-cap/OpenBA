// src/ext/camera_angle/enhanced_entry.cpp -- ext.camera_angle's row of the Enhanced tab (CAMERAANGLE)
//
// NOT RETAIL. The slider "Camera angle", 0..100 with retail in the middle (50): it sets the valued key
// `ext.camera_tilt` (camera_angle.h has the curve: 0 top-down when zoomed out, 90 the same tilt at every zoom, 100 a
// touch flatter still). The row `ext.camera_angle` is ON by default on a desktop (ruled 2026-11-08); the slider
// never turns it off (50 with the row on is retail's pitch), and turns it on for any position but 50 when the
// player switched it off in the Extensions dialog. While the row is off the slider shows 50. Live: the next frame's
// pitch target follows.

#include "ext/camera_angle/camera_angle.h"
#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/SliderElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::SliderElement* g_slider = nullptr;

int SliderValue() {
    const Entry* row = FindEntry("camera_angle");
    return row != nullptr && Requested(*row) ? Val().camera_tilt : CameraAngle::kTiltRetail;
}

void Open(UI::Element* page) {
    g_slider = FindElement<UI::SliderElement>(page, "sliderCameraAngle");
    const int value = SliderValue();
    if (g_slider != nullptr) {
        g_slider->_minimum = static_cast<float>(CameraAngle::kTiltMin);
        g_slider->_maximum = static_cast<float>(CameraAngle::kTiltMax);
        g_slider->_value = static_cast<float>(value);
    }
    std::fprintf(stderr, "[CameraAngle] slider=%p value=%d\n", static_cast<void*>(g_slider), value);
}

void Apply() {
    if (g_slider == nullptr) return;
    const Entry* row = FindEntry("camera_angle");
    if (row == nullptr) return;
    const int value = static_cast<int>(g_slider->_value + 0.5f);
    // Row off and the slider left at retail: nothing to write (the row stays as the player set it).
    if (!Requested(*row) && value == CameraAngle::kTiltRetail) return;
    if (!Requested(*row)) SetAndRecord(*row, true);
    const ValueKey* key = FindValueKey("camera_tilt");
    if (key != nullptr && Val().camera_tilt != value) SetValueAndRecord(*key, value);
    std::fprintf(stderr, "[CameraAngle] Apply: value=%d requested=%d tilt=%d\n", value, Requested(*row) ? 1 : 0,
                 Val().camera_tilt);
}

void Close() { g_slider = nullptr; }

}  // namespace

const Contribution& CameraAngleEntry() {
    static const Contribution c = { "camera_angle", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
