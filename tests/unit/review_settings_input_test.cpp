// ============================================================================
// tests/unit/review_settings_input_test.cpp -- settings.cfg / flag values that reach UB (review
// F-099, F-100, F-130, F-134, F-156; docs/history/review_20261103)
//
//   * KeyMap::Load's slot store (src/engine/input/binding_value.h): an `Input<Name>[Alt]=<n>` value whose
//     device byte is not 0..2 or whose input byte is past that device's state array is not stored
//     (F-099, F-100). Valid values store exactly as retail does.
//   * ValidateConfiguration's refresh-ratio fallback (src/engine/video/validate_configuration.h): a zero
//     denominator, or INT_MIN / -1, is never divided (F-130); the multisample index is bounded by
//     the adapter's list before it is read (F-156).
//   * BANISHED_FPS_CAP (src/app/frame_limiter_ceiling.h): a ceiling below the floor, or NaN, paces
//     nothing instead of reaching the out-of-range int64 casts (F-134).
// Pure logic; every platform.
// ============================================================================
#include "unit.h"

#include "app/frame_limiter_ceiling.h"
#include "input/binding_value.h"
#include "video/validate_configuration.h"

#include <climits>
#include <cmath>
#include <cstdint>

namespace {

using Input::Binding;
using Input::StoreBindingValue;
using RefreshRate = Video::Device::RefreshRate;

// A binding as RegisterInput leaves it: slot 0 keyboard 0x11, slot 1 unbound.
Binding Registered() {
    Binding b{};
    b.exclusive  = 1;
    b.device[0]  = 1;
    b.inputId[0] = 0x11;
    b.device[1]  = Binding::kUnboundDevice;
    b.inputId[1] = 0;
    return b;
}

// ---------------------------------------------------------------- F-099 / F-100
UNIT_TEST(review_keymap_store_valid_values_as_retail) {
    Binding b = Registered();
    // Keyboard (device 1), last input 0x68.
    CHECK(StoreBindingValue(&b, 0, 0x0168));
    CHECK_EQ((int)b.device[0], 1);
    CHECK_EQ((int)b.inputId[0], 0x68);
    // Mouse (device 2), last input 12.
    CHECK(StoreBindingValue(&b, 1, 0x020C));
    CHECK_EQ((int)b.device[1], 2);
    CHECK_EQ((int)b.inputId[1], 0x0C);
    // Joystick (device 0), last input 17.
    CHECK(StoreBindingValue(&b, 0, 0x0011));
    CHECK_EQ((int)b.device[0], 0);
    CHECK_EQ((int)b.inputId[0], 0x11);
    // -1: stored as unbound.
    CHECK(StoreBindingValue(&b, 0, -1));
    CHECK_EQ((int)b.device[0], Binding::kUnboundDevice);
    CHECK_EQ((int)b.inputId[0], 0);
    // -2 (absent) and any other negative: nothing written.
    b = Registered();
    CHECK(!StoreBindingValue(&b, 0, -2));
    CHECK(!StoreBindingValue(&b, 0, -7));
    CHECK_EQ((int)b.device[0], 1);
    CHECK_EQ((int)b.inputId[0], 0x11);
}

UNIT_TEST(review_keymap_store_rejects_out_of_range_input) {
    // Each value would index Input::Device past its state array (or through the null base
    // DeviceArrayBase returns for device >= 3) on the next KeyMap::GetValue / Pressed / Released.
    const int32_t bad[] = {
        0x01FF,   // 511: keyboard input 255 (105 inputs), the ledger's ASan repro
        0x0169,   // keyboard input 105
        0x020D,   // mouse input 13 (13 inputs)
        0x0012,   // joystick input 18 (18 inputs)
        0x0300,   // 768: device 3 (null base)
        0x7F00,   // device 0x7F
    };
    for (int32_t v : bad) {
        Binding b = Registered();
        const bool wrote = StoreBindingValue(&b, 0, v);
        CHECK(!wrote);
        CHECK_EQ((int)b.device[0], 1);       // the RegisterInput default stays
        CHECK_EQ((int)b.inputId[0], 0x11);
    }
    // A device byte of 0x80..0xFF reads as unbound in every reader: stored as retail stores it.
    Binding u = Registered();
    CHECK(StoreBindingValue(&u, 0, 0xFF05));
    CHECK_EQ((int)u.device[0], Binding::kUnboundDevice);
    CHECK_EQ((int)u.inputId[0], 0x05);
    // Bits above the low 16 are dropped exactly as retail's byte stores drop them.
    Binding b = Registered();
    CHECK(StoreBindingValue(&b, 0, 0x10141));
    CHECK_EQ((int)b.device[0], 1);
    CHECK_EQ((int)b.inputId[0], 0x41);
}

// ---------------------------------------------------------------- F-130
UNIT_TEST(review_validate_config_refresh_ratio_as_retail) {
    using Video::ValidateConfig::PickRefreshByRatio;
    const RefreshRate rates[] = {{60000, 1001}, {60, 1}, {120000, 1001}, {144, 1}};
    CHECK_EQ(PickRefreshByRatio(rates, 4, 60, 1), 1);         // 60 == 60
    CHECK_EQ(PickRefreshByRatio(rates, 4, 59940, 1000), 0);   // 59 == 59 (integer ratio)
    CHECK_EQ(PickRefreshByRatio(rates, 4, 119, 1), 2);        // 119 == 119
    CHECK_EQ(PickRefreshByRatio(rates, 4, 75, 1), 0);         // no match: entry 0
    CHECK_EQ(PickRefreshByRatio(rates, 0, 0, 0), 0);          // no entries: nothing divided
}

UNIT_TEST(review_validate_config_refresh_zero_denominator) {
    using Video::ValidateConfig::PickRefreshByRatio;
    // settings.cfg VideoRefreshD=0 (and INT_MIN / -1): retail's IDIV faults at boot.
    const RefreshRate rates[] = {{60, 1}, {144, 1}};
    CHECK_EQ(PickRefreshByRatio(rates, 2, 60, 0), 0);
    CHECK_EQ(PickRefreshByRatio(rates, 2, INT_MIN, -1), 0);
    // A driver-reported 0/0 entry (DXGI allows it) is skipped, not divided.
    const RefreshRate withZero[] = {{0, 0}, {INT_MIN, -1}, {144, 1}};
    CHECK_EQ(PickRefreshByRatio(withZero, 3, 144, 1), 2);
}

// ---------------------------------------------------------------- F-156
UNIT_TEST(review_validate_config_multisample_index_bounded) {
    using Video::ValidateConfig::ClampMultisample;
    // The adapter's list is the 4 bytes at buf + 4; the bytes around it are readable here so the
    // unbounded read returns a value instead of faulting, which the assertions then catch.
    const uint8_t buf[12] = {1, 1, 1, 1, /* list */ 1, 1, 1, 0, /* past it */ 1, 1, 1, 1};
    const uint8_t* list = buf + 4;
    CHECK_EQ(ClampMultisample(list, 4, 0), 0);
    CHECK_EQ(ClampMultisample(list, 4, 2), 2);   // a true entry stays
    CHECK_EQ(ClampMultisample(list, 4, 3), 0);   // a false entry falls back to 0 (retail)
    CHECK_EQ(ClampMultisample(list, 4, 6), 0);   // past the list
    CHECK_EQ(ClampMultisample(list, 4, -2), 0);  // before it
}

// ---------------------------------------------------------------- F-134
UNIT_TEST(review_fps_cap_ceiling_floor) {
    using FrameLimiter::HarnessCap;
    using FrameLimiter::ResolveCeiling;
    // Ceilings that reach SetCeiling's (int64_t)(freq / fps) and WaitUntil's sleepTicks * 10^7 out of
    // range: not paced instead.
    CHECK(ResolveCeiling(HarnessCap::Fixed, 1e-300, 180.0, 0.0, true) == 0.0);
    CHECK(ResolveCeiling(HarnessCap::Fixed, 0.001, 180.0, 0.0, false) == 0.0);
    CHECK(ResolveCeiling(HarnessCap::Fixed, std::nan(""), 180.0, 0.0, true) == 0.0);
    // The floor itself and ordinary caps are unchanged.
    CHECK(ResolveCeiling(HarnessCap::Fixed, FrameLimiter::kMinCeilingFps, 180.0, 0.0, true) ==
          FrameLimiter::kMinCeilingFps);
    CHECK(ResolveCeiling(HarnessCap::Fixed, 1.0, 180.0, 0.0, true) == 1.0);
    CHECK(ResolveCeiling(HarnessCap::Fixed, 45.0, 180.0, 60.0, false) == 45.0);
}

}  // namespace
