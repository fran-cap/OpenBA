// src/ext/phone_menu/touch_hold.cpp -- ext.phone_menu: the press-and-hold tooltip (P11b; touch_hold.h)

#include "ext/phone_menu/touch_hold.h"

#include "ext/registry.h"

#include "input/Device.h"                      // Input::Device::_singleton: the buttons (a finger is the left)
#include "platform/common/platform_input.h"   // Platform::Input::PointerIsTouch (P11)

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

}  // namespace PhoneMenu
}  // namespace Ext
