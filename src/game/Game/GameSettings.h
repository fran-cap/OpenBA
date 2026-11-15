#pragma once
// ============================================================================
// src/game/Game/GameSettings.h — ENDGAME B.4 W0-H2
//
// The 0x34-byte settings block GameMain allocates (MemAlloc(0x34, 4)) and
// publishes in the exe cell 0x1401c3de0 (0x14004202e). A plain struct: no
// vtable, no ctor, no dtor in retail (GameMain fills it field by field).
//
// CLASS NAME NOT SETTLED. Retail's own name for this block is unconfirmed: the
// profile build (Application-x64-profile.exe) lists `GameConfiguration.h` and
// `Globals.h` as candidates, but it was not open in Ghidra when this header
// was written. `GameSettings` is the tree's name (the persistence helpers are
// GameSettings::Load / Save).
//
// FIELDS AND NAMES (Application-x64.exe):
//   names  = retail's own IO::Persistence keys, section "Game", in
//            0x140098b60 (Get) / 0x140098970 (Set). The key spelled
//            "ShowCelcius" by retail is the field `showCelsius`.
//   types  = the Persistence overload retail passes: int& for the int32
//            fields, bool& (one byte) for the six flags, declared bool.
//            The Options dialog copies the checkbox byte in raw, as retail
//            does (0x14007249d MOV byte), and GameSettings::Save hands it to
//            Set(bool) raw (0x1400989d7 MOVZX R9D).
//   defaults (GameMain 0x140041f00): UIScale 100, AutosaveInterval 1,
//            StatusIconOpacity 100, StatusIconEnable 1, StatusIconScale 100,
//            EdgeScroll 1, ShowCelcius 0, ShowWeather 1, AutoSlow 1,
//            AutoPause 1, LastOption 0, LastTerrain 0, LastMapSize 0,
//            LastClimate 1, LastStart 1, LastDisaster 0.
//   The padding bytes (+0x0D..+0x0F, +0x19..+0x1B) are never written by
//   retail (MemAlloc garbage) or by the tree's GameMain.
// ============================================================================

#include <cstdint>
#include <cstddef>

// The six flags are `bool`: retail persists them through Persistence::Get(bool&) / Set(bool)
// (0x140098bc7 LEA R9,[RBX+0xc] -> [0x1401522f0]; 0x1400989d7 MOVZX R9D,byte [RBX+0xc] -> [0x140152300])
// and GameMain writes them as bytes (0x140042001, 0x14004200c, 0x140042013). B6 rows 21 / 22.
struct GameSettings {
    int32_t uiScale;             // +0x00 "UIScale"            (OptionsDialog sliderUIScale)
    int32_t autosaveInterval;    // +0x04 "AutosaveInterval"   (comboAutosave index)
    int32_t statusIconOpacity;   // +0x08 "StatusIconOpacity"  (sliderStatusIcon)
    bool    statusIconEnable;    // +0x0C "StatusIconEnable"   (checkStatusIcon)
    uint8_t _pad0D[3];           // +0x0D not written by retail (and not by the tree since B6 row 22)
    int32_t statusIconScale;     // +0x10 "StatusIconScale"    (sliderStatusIconScale)
    bool    edgeScroll;          // +0x14 "EdgeScroll"
    bool    showCelsius;         // +0x15 "ShowCelcius" (retail's spelling of the key)
    bool    showWeather;         // +0x16 "ShowWeather"
    bool    autoSlow;            // +0x17 "AutoSlow"
    bool    autoPause;           // +0x18 "AutoPause"
    uint8_t _pad19[3];           // +0x19 not written by retail (and not by the tree since B6 row 22)
    int32_t lastOption;          // +0x1C "LastOption"   (OptionsDialog's last tab)
    int32_t lastTerrain;         // +0x20 "LastTerrain"  (NewGameDialog)
    int32_t lastMapSize;         // +0x24 "LastMapSize"
    int32_t lastClimate;         // +0x28 "LastClimate"
    int32_t lastStart;           // +0x2C "LastStart"
    int32_t lastDisaster;        // +0x30 "LastDisaster"
    void Load();   // 0x140098b60 (Game/game_settings_persist.cpp)
    void Save();   // 0x140098970 (Game/game_settings_persist.cpp)
};

static_assert(sizeof(GameSettings) == 0x34, "GameSettings is 0x34 (GameMain MemAlloc(0x34, 4))");
static_assert(alignof(GameSettings) == 4, "GameSettings is dword-aligned");
static_assert(offsetof(GameSettings, uiScale) == 0x00, "uiScale @ +0x00");
static_assert(offsetof(GameSettings, autosaveInterval) == 0x04, "autosaveInterval @ +0x04");
static_assert(offsetof(GameSettings, statusIconOpacity) == 0x08, "statusIconOpacity @ +0x08");
static_assert(offsetof(GameSettings, statusIconEnable) == 0x0C, "statusIconEnable @ +0x0C");
static_assert(offsetof(GameSettings, _pad0D) == 0x0D, "_pad0D @ +0x0D");
static_assert(offsetof(GameSettings, statusIconScale) == 0x10, "statusIconScale @ +0x10");
static_assert(offsetof(GameSettings, edgeScroll) == 0x14, "edgeScroll @ +0x14");
static_assert(offsetof(GameSettings, showCelsius) == 0x15, "showCelsius @ +0x15");
static_assert(offsetof(GameSettings, showWeather) == 0x16, "showWeather @ +0x16");
static_assert(offsetof(GameSettings, autoSlow) == 0x17, "autoSlow @ +0x17");
static_assert(offsetof(GameSettings, autoPause) == 0x18, "autoPause @ +0x18");
static_assert(offsetof(GameSettings, _pad19) == 0x19, "_pad19 @ +0x19");
static_assert(offsetof(GameSettings, lastOption) == 0x1C, "lastOption @ +0x1C");
static_assert(offsetof(GameSettings, lastTerrain) == 0x20, "lastTerrain @ +0x20");
static_assert(offsetof(GameSettings, lastMapSize) == 0x24, "lastMapSize @ +0x24");
static_assert(offsetof(GameSettings, lastClimate) == 0x28, "lastClimate @ +0x28");
static_assert(offsetof(GameSettings, lastStart) == 0x2C, "lastStart @ +0x2C");
static_assert(offsetof(GameSettings, lastDisaster) == 0x30, "lastDisaster @ +0x30");
