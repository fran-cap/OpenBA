// src/engine/input/binding_value.h -- KeyMap::Load's store of one settings.cfg binding value
//
// Input::KeyMap::Load (retail 0x1800a7190, keymap_load.cpp) parses `Input<Name>[Alt]=<n>` and stores it
// into one slot of the binding. The three-way store lives here, header-only, so
// tests/unit/input_binding_value_test.cpp can drive it without the Persistence singleton (review
// F-099, F-100).
#pragma once

#include <cstdint>

#include "input/Input.h"

namespace Input {

// The store of one parsed value into `slot`; returns true when the slot was written.
//   value >= 0  : device = (value >> 8) & 0xFF, inputId = value & 0xFF
//   value == -1 : stored as unbound (device 0xFF, retail's R14B; inputId 0)
//   otherwise   : (-2, the key was absent) nothing is written, the RegisterInput default stays
inline bool StoreBindingValue(Binding* b, int slot, int32_t value) {
    if (value >= 0) {
        // NOT RETAIL (review F-099, F-100, undefined behaviour): retail 0x1800a7190 stores any value
        // >= 0 unchecked, and KeyMap::GetValue / Pressed / Released then index Input::Device's state
        // arrays with the input byte (past the 0x1200-byte object) or read through the null base a
        // device byte 3..0x7F gives. A value whose device byte is 3..0x7F, or whose input is past that
        // device's count, is treated as absent: the RegisterInput default stays. A device byte of
        // 0x80..0xFF reads as unbound (the readers skip a negative device) and is stored as retail does.
        const int32_t device = (value >> Binding::kDeviceShift) & 0xFF;
        const int32_t input  = value & 0xFF;
        constexpr int32_t kJoystick = static_cast<int32_t>(Device::InputDevice::Joystick);
        constexpr int32_t kKeyboard = static_cast<int32_t>(Device::InputDevice::Keyboard);
        constexpr int32_t kMouse    = static_cast<int32_t>(Device::InputDevice::Mouse);
        if (device == kJoystick && input >= Device::kJoystickInputCount) return false;
        if (device == kKeyboard && input >= Device::kKeyboardInputCount) return false;
        if (device == kMouse && input >= Device::kMouseInputCount) return false;
        if (device >= 3 && device <= Binding::kMaxPositiveDeviceByte) return false;
        b->device[slot]  = static_cast<uint8_t>((value >> Binding::kDeviceShift) & 0xFF);
        b->inputId[slot] = static_cast<uint8_t>(value & 0xFF);
        return true;
    }
    if (value == Binding::kValueUnbound) {
        b->device[slot]  = Binding::kUnboundDevice;   // retail stores R14B, the low byte of -1
        b->inputId[slot] = 0;
        return true;
    }
    return false;
}

}  // namespace Input
