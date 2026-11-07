#pragma once
// src/game/Game/inputbindings.h
// Default input-binding registration helper.
//
// Original: FUN_1400992f0 in Application-x64.exe.
// Walks the global KeyMap (via g_pInputConfigSingletonExref triple deref), clears
// existing bindings, registers all 50 default bindings (mouse axes, hotkeys,
// camera presets, toolbar slots), then loads any saved overrides via
// Input::KeyMap::Load.
//
// Called by:
//   - PlacementTool_Ctor (game-startup binding install)
//   - OptionsDialog reset-to-defaults action
//   - FUN_140072740 (called from OptionsDialog accept)
//
// In standalone mode we invoke this from RunGameMain right after Engine::Startup
// so the KeyMap is populated before any dialog (including OptionsDialog) opens
// and ButtonElement::Update starts polling KeyMap::Pressed for hotkey hints.

#include <cstdint>
#include "system/EnumFlags.h"   // ToUnderlying (a GameKey as the KeyMap id)

// The binding ids RegisterDefaultInputBindings registers with Input::KeyMap (the
// id column of kDefaultBindings, with retail's RegisterInput names): what
// KeyMap::Pressed / Released / GetValue(joystick, id) take. The engine KeyMap API
// stays int32_t (it is game-agnostic): a call spells ToUnderlying(GameKey::X)
// (system/EnumFlags.h). Not the Input::Device mouse slots (GameKey::MouseLeft is
// 5, the device's left-button slot is 4).
enum class GameKey : int32_t {
    MouseX         = 0x00,   // mouse x axis
    MouseY         = 0x01,   // mouse y axis
    MouseDeltaX    = 0x02,   // mouse x delta
    MouseDeltaY    = 0x03,   // mouse y delta
    MouseWheel     = 0x04,   // mouse wheel
    MouseLeft      = 0x05,   // left button (place / drag)
    MouseRight     = 0x06,   // right button (cancel)
    MouseMiddle    = 0x07,   // middle button
    Cancel         = 0x08,   // cancel the tool / close
    RotateLeft     = 0x09,   // camera yaw left
    RotateRight    = 0x0A,   // camera yaw right
    Forward        = 0x0B,   // camera forward
    Back           = 0x0C,   // camera back
    Left           = 0x0D,   // camera left
    Right          = 0x0E,   // camera right
    ZoomIn         = 0x0F,   // camera zoom in
    ZoomOut        = 0x10,   // camera zoom out
    PitchUp        = 0x11,   // camera pitch up
    PitchDown      = 0x12,   // camera pitch down
    StructureLeft  = 0x13,   // rotate the placement counter-clockwise
    StructureRight = 0x14,   // rotate the placement clockwise
    StructureType  = 0x15,   // flip the placement
    PathMethod     = 0x16,   // road diagonal mode (Shift)
    TogglePause    = 0x17,   // pause / resume
    Toolbar0       = 0x18,   // toolbar slot 0
    Toolbar1       = 0x19,   // toolbar slot 1
    Toolbar2       = 0x1A,   // toolbar slot 2
    Toolbar3       = 0x1B,   // toolbar slot 3
    Toolbar4       = 0x1C,   // toolbar slot 4
    Toolbar5       = 0x1D,   // toolbar slot 5
    Toolbar6       = 0x1E,   // toolbar slot 6
    Toolbar7       = 0x1F,   // toolbar slot 7
    Toolbar8       = 0x20,   // toolbar slot 8
    ScreenshotNoUI = 0x21,   // screenshot without the UI
    Screenshot     = 0x22,   // screenshot
    Tool0          = 0x23,   // tool slot 0
    Tool1          = 0x24,   // tool slot 1
    Tool2          = 0x25,   // tool slot 2
    Tool3          = 0x26,   // tool slot 3
    Tool4          = 0x27,   // tool slot 4
    Tool5          = 0x28,   // tool slot 5
    Tool6          = 0x29,   // tool slot 6
    Tool7          = 0x2A,   // tool slot 7
    Tool8          = 0x2B,   // tool slot 8
    Tool9          = 0x2C,   // tool slot 9
    Camera0        = 0x2D,   // camera preset 0
    Camera1        = 0x2E,   // camera preset 1
    Camera2        = 0x2F,   // camera preset 2
    Camera3        = 0x30,   // camera preset 3
    Camera4        = 0x31,   // camera preset 4
};

namespace Game {

// Populates the global Input::KeyMap with the 50 default Banished bindings,
// then calls Input::KeyMap::Load to merge any user-saved overrides.
//
// No-op (with a printf) if g_pInputConfigSingletonExref or any of fn_KeyMap_* is
// unresolved — never crashes if engine fns aren't ready.
void RegisterDefaultInputBindings();

} // namespace Game
