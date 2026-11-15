// ============================================================================
// tests/unit/mac_keymap_test.cpp -- the Mac keyboard layer and the Retina coordinate maths
// ============================================================================
// src/platform/common/mac_keymap.h turns SDL's key state into what the DIK table is read
// from on macOS (Command = Control, Right Option = a Windows-keyboard layer, F13-F15 =
// Print Screen / Scroll Lock / Pause); src/platform/common/display_geometry.cpp carries the
// points <-> pixels conversions the SDL window and input code use at Retina density. Pure:
// no SDL, no window. docs/macos/INPUT_WINDOW.md says what each rule is for.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "display_geometry.h"
#include "mac_keymap.h"
#include "sdl_dik_tables.h"
#include "window_messages.h"

namespace MK = Platform::Input::MacKeys;
using Platform::Input::kSdlScancodeToDik;
using namespace Platform::Window;

namespace {

struct Keys {
    bool in[MK::kStateCount];
    bool out[MK::kStateCount];
    Keys() {
        std::memset(in, 0, sizeof(in));
        std::memset(out, 0, sizeof(out));
    }
    void Apply() { MK::ApplyMacKeyLayer(in, MK::kStateCount, out); }
};

constexpr int kA = 4, kC = 6;

}  // namespace

UNIT_TEST(mac_keys_plain_keys_pass_through) {
    Keys k;
    k.in[kA] = true;
    k.in[MK::kLCtrl] = true;
    k.in[MK::kPageDown] = true;   // fn+Down arrives as PageDown: untouched
    k.in[MK::kDelete] = true;     // fn+Delete
    k.Apply();
    CHECK(k.out[kA]);
    CHECK(k.out[MK::kLCtrl]);
    CHECK(k.out[MK::kPageDown]);
    CHECK(k.out[MK::kDelete]);
    CHECK(!k.out[MK::kPageUp]);
    // An empty keyboard stays empty.
    Keys e;
    e.Apply();
    for (int i = 0; i < MK::kStateCount; ++i)
        CHECK(!e.out[i]);
}

UNIT_TEST(mac_keys_command_acts_as_control) {
    for (const int gui : { static_cast<int>(MK::kLGui), static_cast<int>(MK::kRGui) }) {
        Keys k;
        k.in[gui] = true;
        k.in[kC] = true;
        k.Apply();
        CHECK(k.out[MK::kLCtrl]);
        CHECK(!k.out[MK::kLGui]);
        CHECK(!k.out[MK::kRGui]);
        CHECK(k.out[kC]);
    }
    // Physical Control is kept when Command is not down.
    Keys k;
    k.in[MK::kRCtrl] = true;
    k.Apply();
    CHECK(k.out[MK::kRCtrl]);
    CHECK(!k.out[MK::kLCtrl]);
}

UNIT_TEST(mac_keys_right_option_layer) {
    struct Case {
        int from;
        int to;
    };
    const Case cases[] = {
        { MK::kUp, MK::kPageUp },
        { MK::kDown, MK::kPageDown },
        { MK::kLeft, MK::kHome },
        { MK::kRight, MK::kEnd },
        { MK::kBackspace, MK::kDelete },
        { MK::kReturn, MK::kInsert },
        { MK::kF10, MK::kScrollLock },
        { MK::kF11, MK::kPrintScreen },
        { MK::kF12, MK::kPause },
    };
    for (const Case& c : cases) {
        Keys k;
        k.in[MK::kRAlt] = true;
        k.in[c.from] = true;
        k.Apply();
        CHECK(k.out[c.to]);
        CHECK(!k.out[c.from]);
        CHECK(!k.out[MK::kRAlt]);   // consumed by the mapping
    }
    // Without Right Option the same keys are themselves.
    Keys k;
    k.in[MK::kUp] = true;
    k.in[MK::kF11] = true;
    k.Apply();
    CHECK(k.out[MK::kUp]);
    CHECK(k.out[MK::kF11]);
    CHECK(!k.out[MK::kPageUp]);
    // Right Option held alone stays Right Alt; Left Option is never the layer.
    Keys alone;
    alone.in[MK::kRAlt] = true;
    alone.Apply();
    CHECK(alone.out[MK::kRAlt]);
    Keys left;
    left.in[MK::kLAlt] = true;
    left.in[MK::kUp] = true;
    left.Apply();
    CHECK(left.out[MK::kUp]);
    CHECK(left.out[MK::kLAlt]);
    // A key outside the layer keeps working under Right Option.
    Keys other;
    other.in[MK::kRAlt] = true;
    other.in[kA] = true;
    other.Apply();
    CHECK(other.out[kA]);
    CHECK(other.out[MK::kRAlt]);
}

UNIT_TEST(mac_keys_extended_function_keys) {
    Keys k;
    k.in[MK::kF13] = true;
    k.in[MK::kF14] = true;
    k.in[MK::kF15] = true;
    k.Apply();
    CHECK(k.out[MK::kPrintScreen]);
    CHECK(k.out[MK::kScrollLock]);
    CHECK(k.out[MK::kPause]);
    CHECK(!k.out[MK::kF13]);
    CHECK(!k.out[MK::kF14]);
    CHECK(!k.out[MK::kF15]);
}

// Every layer target must be a key the DIK table knows (otherwise the layer would turn a
// press into retail's index-104 alias), and every layer source must be a real key too.
UNIT_TEST(mac_keys_layer_targets_reach_a_dik) {
    for (const MK::LayerEntry& e : MK::kOptionLayer) {
        CHECK(kSdlScancodeToDik[e.from] != 0);
        CHECK(kSdlScancodeToDik[e.to] != 0);
    }
    for (const MK::LayerEntry& e : MK::kExtendedFKeys) {
        CHECK(kSdlScancodeToDik[e.to] != 0);
    }
    // The four keys a Mac keyboard lacks land on the PC's DIK codes.
    CHECK_EQ(kSdlScancodeToDik[MK::kInsert], 0xD2);
    CHECK_EQ(kSdlScancodeToDik[MK::kPrintScreen], 0xB7);
    CHECK_EQ(kSdlScancodeToDik[MK::kScrollLock], 0x46);
    CHECK_EQ(kSdlScancodeToDik[MK::kPause], 0xC5);
}

UNIT_TEST(mac_keys_fullscreen_chord) {
    const int kF = 9;
    const uint16_t lctrl = 0x0040, rctrl = 0x0080, lgui = 0x0400, rgui = 0x0800, shift = 0x0001;
    CHECK(MK::IsFullscreenChord(lctrl | lgui, kF, false));
    CHECK(MK::IsFullscreenChord(rctrl | rgui, kF, false));
    CHECK(MK::IsFullscreenChord(lctrl | lgui | shift, kF, false));
    CHECK(!MK::IsFullscreenChord(lctrl | lgui, kF, true));    // key repeat
    CHECK(!MK::IsFullscreenChord(lgui, kF, false));           // Command+F alone
    CHECK(!MK::IsFullscreenChord(lctrl, kF, false));          // Control+F alone
    CHECK(!MK::IsFullscreenChord(lctrl | lgui, kA, false));   // wrong key
    CHECK(!MK::IsFullscreenChord(0, kF, false));
}

UNIT_TEST(retina_points_pixels_round_trip) {
    CHECK_EQ(PointsToPixels(640.0f, 2.0f), 1280);
    CHECK_EQ(PointsToPixels(640.0f, 1.0f), 640);
    CHECK_EQ(PointsToPixels(100.5f, 2.0f), 201);
    CHECK_EQ(PointsToPixels(33.0f, 0.0f), 33);      // a bad density is 1
    CHECK_EQ(PixelsToPointsRound(1280, 2.0f), 640);
    CHECK_EQ(PixelsToPointsFloor(1281, 2.0f), 640);
    CHECK_EQ(PixelsToPointsCeil(1281, 2.0f), 641);
    CHECK_EQ(PixelsToPointsFloor(1280, 2.0f), 640);
    CHECK_EQ(PixelsToPointsCeil(1280, 2.0f), 640);
    for (int p = 0; p < 4000; p += 37) {
        CHECK_EQ(PixelsToPointsRound(PointsToPixels(static_cast<float>(p), 2.0f), 2.0f), p);
        CHECK_EQ(PixelsToPointsRound(PointsToPixels(static_cast<float>(p), 1.0f), 1.0f), p);
    }
    // 1.5x displays do not drift either.
    CHECK_EQ(PixelsToPointsRound(PointsToPixels(800.0f, 1.5f), 1.5f), 800);
}

UNIT_TEST(retina_cursor_edge_snap) {
    // A 800 x 450 point window at density 2 is 1600 x 900 pixels.
    CHECK_EQ(CursorPointToPixel(0.0f, 800, 2.0f), 0);
    CHECK_EQ(CursorPointToPixel(400.25f, 800, 2.0f), 800);
    CHECK_EQ(CursorPointToPixel(798.9f, 800, 2.0f), 1597);   // inside the last point...
    CHECK_EQ(CursorPointToPixel(799.0f, 800, 2.0f), 1599);   // ...snaps to the last pixel
    CHECK_EQ(CursorPointToPixel(799.4f, 800, 2.0f), 1599);
    CHECK_EQ(CursorPointToPixel(800.0f, 800, 2.0f), 1599);
    CHECK_EQ(CursorPointToPixel(850.0f, 800, 2.0f), 1700);   // outside the window: no snap
    CHECK_EQ(CursorPointToPixel(-5.0f, 800, 2.0f), -10);
    // Density 1 is plain floor.
    CHECK_EQ(CursorPointToPixel(799.4f, 800, 1.0f), 799);
    CHECK_EQ(CursorPointToPixel(799.0f, 800, 1.0f), 799);
}

UNIT_TEST(mac_option_click_release_orders) {
    for (uint16_t option : { uint16_t(0x0100), uint16_t(0x0200) }) {
        MK::MouseButtonRemapper mouse;
        CHECK_EQ(mouse.PolledButtons(1), 0u);
        CHECK_EQ(mouse.ButtonEvent(1, true, option), 2);
        CHECK_EQ(mouse.PolledButtons(1), 2u);
        // Option-up has no button event: the drag and its release stay middle.
        CHECK_EQ(mouse.PolledButtons(1), 2u);
        CHECK_EQ(mouse.ButtonEvent(1, false, 0), 2);
        CHECK_EQ(mouse.PolledButtons(0), 0u);
        // Mouse-up before Option-up has the same result.
        CHECK_EQ(mouse.ButtonEvent(1, true, option), 2);
        CHECK_EQ(mouse.ButtonEvent(1, false, option), 2);
        CHECK_EQ(mouse.PolledButtons(0), 0u);
    }
}

UNIT_TEST(mac_option_does_not_change_an_existing_left_drag) {
    MK::MouseButtonRemapper mouse;
    CHECK_EQ(mouse.ButtonEvent(1, true, 0), 1);
    CHECK_EQ(mouse.PolledButtons(1), 1u);
    CHECK_EQ(mouse.ButtonEvent(1, false, MK::kModOption), 1);
    CHECK_EQ(mouse.PolledButtons(0), 0u);
}

UNIT_TEST(mac_option_double_click_and_wheel) {
    MK::MouseButtonRemapper mouse;
    for (int click = 0; click < 2; ++click) {
        CHECK_EQ(mouse.ButtonEvent(1, true, MK::kModOption), 2);
        // Wheel events bypass ButtonEvent; polling must preserve the drag.
        CHECK_EQ(mouse.PolledButtons(5), 6u);
        CHECK_EQ(mouse.ButtonEvent(3, true, MK::kModOption), 3);
        CHECK_EQ(mouse.ButtonEvent(3, false, 0), 3);
        CHECK_EQ(mouse.ButtonEvent(1, false, 0), 2);
        CHECK_EQ(mouse.PolledButtons(0), 0u);
    }
    CHECK_EQ(mouse.ButtonEvent(1, true, 0), 1);
    CHECK_EQ(mouse.ButtonEvent(1, false, 0), 1);
    CHECK_EQ(mouse.ButtonEvent(2, true, MK::kModOption), 2);
    CHECK_EQ(mouse.PolledButtons(2), 2u);
}

UNIT_TEST(mac_option_focus_reset_and_physical_release) {
    MK::MouseButtonRemapper mouse;
    CHECK_EQ(mouse.ButtonEvent(1, true, MK::kModOption), 2);
    CHECK_EQ(mouse.PolledButtons(0), 0u);
    mouse.Reset();
    CHECK_EQ(mouse.PolledButtons(1), 0u);
    CHECK_EQ(mouse.ButtonEvent(1, true, 0), 1);
    CHECK_EQ(mouse.PolledButtons(1), 1u);
}

UNIT_TEST(mac_option_event_translation_uses_only_middle_and_preserves_wheel) {
    MK::MouseButtonRemapper mouse;
    WindowEvent event;
    event.kind = WindowEventKind::MouseButtonDown;
    event.button = mouse.ButtonEvent(1, true, MK::kModOption);
    WindowMessage messages[kMaxMessagesPerEvent];
    CHECK_EQ(TranslateWindowEvent(event, nullptr, messages), 1);
    CHECK_EQ(messages[0].message, kMsgSetCursor);
    CHECK_EQ((messages[0].lParam >> 16) & 0xFFFF, kMsgMButtonDown);
    CHECK_EQ(mouse.PolledButtons(1), 2u);

    event.kind = WindowEventKind::MouseWheel;
    event.wheelY = 1.0f;
    CHECK_EQ(TranslateWindowEvent(event, nullptr, messages), 1);
    CHECK_EQ(messages[0].message, kMsgMouseWheel);
    CHECK_EQ((messages[0].wParam >> 16) & 0xFFFF, static_cast<uint64_t>(kWheelDelta));
    CHECK_EQ(mouse.PolledButtons(1), 2u);
    CHECK_EQ(mouse.ButtonEvent(1, false, 0), 2);
}
