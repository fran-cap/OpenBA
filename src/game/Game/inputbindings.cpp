// src/game/Game/inputbindings.cpp
// Clean reimpl of FUN_1400992f0 (Application-x64.exe).
//
// Original sequence:
//   KeyMap* km = ***(KeyMap****)g_pInputConfigSingletonExref;
//   Input::KeyMap::ClearAll(km);
//   Input::KeyMap::RegisterInput(km, 0,  2, 0,    "MouseX",       false);
//   ... (50 RegisterInput calls covering mouse axes, hotkeys, camera, toolbar)
//   Input::KeyMap::Load(km);
//
// All 50 bindings are reproduced verbatim from the Ghidra decompile.
//
// This is the implementation of that function. RunGameMain calls us right after
// Engine::Startup so OptionsDialog can poll hotkey bindings without crashing.

#include "inputbindings.h"
#include <cstdint>
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "system/Filename.h"
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdio>
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "ui/StringTable.h"
#include "Game/exe_bodies.h"

namespace Game {

namespace {

using Device = Input::Device::InputDevice;
using MouseInput = Input::Device::MouseInput;

// One row in the binding table. Mirrors the original RegisterInput call args.
struct Binding {
    GameKey     id;
    Device      deviceType;   // 0=gamepad, 1=keyboard, 2=mouse
    int         keyCode;      // a MouseInput slot on the mouse rows, a key code on the keyboard rows
    const char* name;
    bool        flag;          // true=button-style, false=axis (mouse axes)
};

// 50 default bindings, exact match to FUN_1400992f0.
static const Binding kDefaultBindings[] = {
    // Mouse axes (deviceType=2)
    {GameKey::MouseX,         Device::Mouse,    ToUnderlying(MouseInput::X),      "MouseX",         false},
    {GameKey::MouseY,         Device::Mouse,    ToUnderlying(MouseInput::Y),      "MouseY",         false},
    {GameKey::MouseDeltaX,    Device::Mouse,    ToUnderlying(MouseInput::DeltaX), "MouseDeltaX",    false},
    {GameKey::MouseDeltaY,    Device::Mouse,    ToUnderlying(MouseInput::DeltaY), "MouseDeltaY",    false},
    {GameKey::MouseWheel,     Device::Mouse,    ToUnderlying(MouseInput::Wheel),  "MouseWheel",     false},
    {GameKey::MouseLeft,      Device::Mouse,    ToUnderlying(MouseInput::Left),   "MouseLeft",      true },
    {GameKey::MouseRight,     Device::Mouse,    ToUnderlying(MouseInput::Right),  "MouseRight",     true },
    {GameKey::MouseMiddle,    Device::Mouse,    ToUnderlying(MouseInput::Middle), "MouseMiddle",    true },

    // Keyboard hotkeys (deviceType=1)
    {GameKey::Cancel,         Device::Keyboard, 0x00, "Cancel",         true},
    {GameKey::RotateLeft,     Device::Keyboard, 0x27, "RotateLeft",     true},
    {GameKey::RotateRight,    Device::Keyboard, 0x1B, "RotateRight",    true},
    {GameKey::Forward,        Device::Keyboard, 0x2D, "Forward",        true},
    {GameKey::Back,           Device::Keyboard, 0x29, "Back",           true},
    {GameKey::Left,           Device::Keyboard, 0x17, "Left",           true},
    {GameKey::Right,          Device::Keyboard, 0x1A, "Right",          true},
    {GameKey::ZoomIn,         Device::Keyboard, 0x4D, "ZoomIn",         true},
    {GameKey::ZoomOut,        Device::Keyboard, 0x50, "ZoomOut",        true},
    {GameKey::PitchUp,        Device::Keyboard, 0x4F, "PitchUp",        true},
    {GameKey::PitchDown,      Device::Keyboard, 0x52, "PitchDown",      true},
    {GameKey::StructureLeft,  Device::Keyboard, 0x28, "StructureLeft",  true},
    {GameKey::StructureRight, Device::Keyboard, 0x2A, "StructureRight", true},
    {GameKey::StructureType,  Device::Keyboard, 0x1C, "StructureType",  true},
    {GameKey::PathMethod,     Device::Keyboard, 0x3E, "PathMethod",     true},
    {GameKey::TogglePause,    Device::Keyboard, 0x47, "TogglePause",    true},

    // Toolbar slots
    {GameKey::Toolbar0,       Device::Keyboard, 0x01, "Toolbar0", true},
    {GameKey::Toolbar1,       Device::Keyboard, 0x02, "Toolbar1", true},
    {GameKey::Toolbar2,       Device::Keyboard, 0x03, "Toolbar2", true},
    {GameKey::Toolbar3,       Device::Keyboard, 0x04, "Toolbar3", true},
    {GameKey::Toolbar4,       Device::Keyboard, 0x05, "Toolbar4", true},
    {GameKey::Toolbar5,       Device::Keyboard, 0x06, "Toolbar5", true},
    {GameKey::Toolbar6,       Device::Keyboard, 0x07, "Toolbar6", true},
    {GameKey::Toolbar7,       Device::Keyboard, 0x08, "Toolbar7", true},
    {GameKey::Toolbar8,       Device::Keyboard, 0x09, "Toolbar8", true},

    // Screenshot
    {GameKey::ScreenshotNoUI, Device::Keyboard, 0x4A, "ScreenshotNoUI", true},
    {GameKey::Screenshot,     Device::Keyboard, 0x4B, "Screenshot",     true},

    // Tool slots
    {GameKey::Tool0,          Device::Keyboard, 0x0E, "Tool0", true},
    {GameKey::Tool1,          Device::Keyboard, 0x0F, "Tool1", true},
    {GameKey::Tool2,          Device::Keyboard, 0x10, "Tool2", true},
    {GameKey::Tool3,          Device::Keyboard, 0x11, "Tool3", true},
    {GameKey::Tool4,          Device::Keyboard, 0x12, "Tool4", true},
    {GameKey::Tool5,          Device::Keyboard, 0x13, "Tool5", true},
    {GameKey::Tool6,          Device::Keyboard, 0x14, "Tool6", true},
    {GameKey::Tool7,          Device::Keyboard, 0x15, "Tool7", true},
    {GameKey::Tool8,          Device::Keyboard, 0x16, "Tool8", true},
    {GameKey::Tool9,          Device::Keyboard, 0x0D, "Tool9", true},

    // Camera presets
    {GameKey::Camera0,        Device::Keyboard, 0x30, "Camera0", true},
    {GameKey::Camera1,        Device::Keyboard, 0x2E, "Camera1", true},
    {GameKey::Camera2,        Device::Keyboard, 0x19, "Camera2", true},
    {GameKey::Camera3,        Device::Keyboard, 0x2C, "Camera3", true},
    {GameKey::Camera4,        Device::Keyboard, 0x18, "Camera4", true},
};

constexpr size_t kBindingCount = sizeof(kDefaultBindings) / sizeof(kDefaultBindings[0]);
static_assert(kBindingCount == 50, "Banished has exactly 50 default bindings");

} // namespace

void RegisterDefaultInputBindings() {
    // Resolve the actual KeyMap via the same triple-deref pattern the
    // original exe uses. See CLAUDE.md note about External reference
    // indirection levels.
    if (Input::Configuration::_singleton == nullptr) {
        printf("[InputBindings] FATAL: Input::Configuration::_singleton not initialized\n");
        return;
    }

    // Input::Configuration::_keyMap (+0x00) is the KeyMap.
    Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
    if (!keyMap) {
        printf("[InputBindings] FATAL: KeyMap exref level-3 (object) ptr is NULL\n");
        return;
    }


    printf("[InputBindings] Populating KeyMap %p with %zu default bindings...\n",
           (void*)keyMap, kBindingCount);

    keyMap->ClearAll();

    for (const Binding& b : kDefaultBindings) {
        keyMap->RegisterInput(ToUnderlying(b.id), ToUnderlying(b.deviceType), b.keyCode, b.name, b.flag);
    }

    // Load saved overrides from Persistence (settings.cfg's Input... entries).
    // No-op on a fresh install.
    keyMap->Load();

    printf("[InputBindings] Done — %zu bindings + Load applied.\n",
           kBindingCount);
}

} // namespace Game
