// src/ext/phone_menu/touch_hold.cpp -- ext.phone_menu: the press-and-hold tooltip (P11b, P11c; touch_hold.h)

#include "ext/phone_menu/touch_hold.h"

#include "ext/phone_menu/hold_math.h"   // PlaceHoldTip (P11c)
#include "ext/phone_menu/phone_math.h"  // InsetLayout: the safe area
#include "ext/registry.h"

#include "input/Device.h"                      // Input::Device::_singleton: the buttons (a finger is the left)
#include "platform/common/platform_input.h"   // Platform::Input::PointerIsTouch (P11)
#include "platform/common/platform_touch.h"   // Platform::Touch::EnginePixelsPerDevicePixel (P11c)
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/ToolTipDialog.h"
#include "ui/UserInterface.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// Input::Device::_mouse: 4 / 5 / 6 = left / right / middle (src/game/Game/inputbindings.cpp).
constexpr int kMouseLeft = 4;
constexpr int kMouseRight = 5;
constexpr int kMouseMiddle = 6;

}  // namespace

bool HoverWhileFingerDown(const UI::DialogInstance* /*dialog*/) {
    if (!Ext().phone_menu) return false;
    if (!::Platform::Input::PointerIsTouch()) return false;
    const ::Input::Device* in = ::Input::Device::_singleton;
    if (in == nullptr) return false;
    if (in->_mouse[kMouseRight].value > 0 || in->_mouse[kMouseMiddle].value > 0) return false;
    return in->_mouse[kMouseLeft].value > 0;
}

bool PlaceHeldToolTip(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
                      int32_t* rect) {
    if (point != UI::UserInterface::kExtPlaceLayout && point != UI::UserInterface::kExtPlaceUpperLeft) return false;
    if (!Ext().phone_menu || dialog == nullptr) return false;
    const UI::UserInterface* ui = dialog->_ui;
    if (ui == nullptr || ui->_toolTipDialog == nullptr) return false;
    if (dialog != ui->_toolTipDialog) return false;
    if (!::Platform::Input::PointerIsTouch()) return false;
    // SetUpperLeft (BeginToolTip, then the MOVE arm every frame): retail's clamp, and the upper-left
    // stays the pointer's. phone_menu.cpp's own kExtPlaceUpperLeft arm would store the laid-out
    // upper-left instead (P11b), and the next SetUpperLeft(x, y - h) would then move the laid-out
    // tooltip by the difference: back under the finger.
    if (point == UI::UserInterface::kExtPlaceUpperLeft) return true;
    const ::Input::Device* in = ::Input::Device::_singleton;
    UI::Element* root = const_cast<UI::Element*>(dialog->_root.Get());
    if (in == nullptr || root == nullptr) return false;

    // The finger: the cursor, which ext.touch_pointer holds on the press point while the finger rests
    // (P11b), in global layout units as UserInterface::Update reads it (0x180121635).
    const float scale = ui->_uiScale;
    const int32_t fx = static_cast<int32_t>(static_cast<float>(in->_mouse[0].value) * scale);
    const int32_t fy = static_cast<int32_t>(static_cast<float>(in->_mouse[1].value) * scale);

    // The tooltip's padded size in its own units (a tooltip follows its owner's fitted scale k,
    // touch_fit.h (c)), then in global layout units.
    const float k = UI::UserInterface::_extDialogScale != nullptr ? UI::UserInterface::_extDialogScale(dialog) : 1.0f;
    root->ComputeSize();
    int32_t w = root->_layoutWidth + root->_leftPad + root->_rightPad;
    int32_t h = root->_layoutHeight + root->_topPad + root->_bottomPad;
    if (w < root->_minWidth) w = root->_minWidth;
    if (h < root->_minHeight) h = root->_minHeight;
    const int32_t wl = static_cast<int32_t>(static_cast<float>(w) * k + 0.5f);
    const int32_t hl = static_cast<int32_t>(static_cast<float>(h) * k + 0.5f);

    const int32_t liftPx = HoldTipLiftDevicePx(Ext().touch_pointer, Val().touch_pointer_lift);
    const int32_t lift = DeviceToLayout(liftPx, ::Platform::Touch::EnginePixelsPerDevicePixel(), scale);
    const HoldTip tip = PlaceHoldTip(fx, fy, wl, hl, lift, InsetLayout(width, height), width, height);

    // ComputeLayout puts the dialog at _upperLeft + (rect[0], rect[1]) in its own units, the engine
    // having divided the rectangle by k: the offset from the upper-left retail stored (the pointer,
    // in whichever units the owner hit-tested it) to the one wanted. The far edges stay the screen's.
    rect[0] = tip.x - static_cast<int32_t>(static_cast<float>(dialog->_upperLeftX) * k);
    rect[1] = tip.y - static_cast<int32_t>(static_cast<float>(dialog->_upperLeftY) * k);
    rect[2] = width;
    rect[3] = height;
    return true;
}

}  // namespace PhoneMenu
}  // namespace Ext
