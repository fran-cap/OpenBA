// src/game/Game/game_settings_persist.cpp
//
// The GameSettings <-> IO::Persistence bridge — retail's two halves of
// "remember what the player chose last time":
//
//   0x140098b60  GameSettings_Load  — Persistence::Get  x16, called by GameMain
//                                     right after the struct is default-filled
//   0x140098970  GameSettings_Save  — Persistence::Set  x16, called by GameMain
//                                     at shutdown and by OptionsDialog's Apply
//
// Neither was ported, so the standalone always came up on the hardcoded defaults
// and never wrote anything back. That is what made the five New Game combo picks
// (LastTerrain / LastMapSize / LastClimate / LastStart / LastDisaster) survive a
// quit-to-menu but not a restart: NewGameDialog's OK writes them into the
// settings struct, and ONLY GameSettings_Save moves the struct into Persistence.
// Persistence::WriteSettings — which game_main.cpp already calls at shutdown —
// flushes Persistence to disk, but it has nothing to flush until Set runs.
//
// Settings struct is 0x34 bytes. Field map, as int32 indices (retail passes an
// int* and indexes it, mixing in three bools at raw byte offsets 0x15/0x16/0x17):
//   [0] UIScale            [1] AutosaveInterval   [2] StatusIconOpacity
//   [4] StatusIconScale    [7] LastOption         [8] LastTerrain
//   [9] LastMapSize        [10] LastClimate       [11] LastStart
//   [12] LastDisaster
// and SIX bools at raw byte offsets, NOT ints (retail casts the int* slot to bool*):
//   byte 0x0C StatusIconEnable   byte 0x14 EdgeScroll   byte 0x18 AutoPause
//   byte 0x15 ShowCelcius        byte 0x16 ShowWeather  byte 0x17 AutoSlow

#include "options.h"
#include "platform/common/display_density.h"
#include "io/Persistence.h"   // ENDGAME B.3.4a closing: Persistence::Get/Set members
#include <cstdint>
#include <cstdio>
#include "Game/exe_bodies.h"

namespace {

::IO::Persistence* PersistenceSingleton() {
    return ::IO::Persistence::_singleton;
}

// The 16 keys, ints and bools in separate tables. Retail (0x140098b60 / 0x140098970)
// makes the calls in field order with the bools in between: +0x0, +0x4, +0x8, +0xC,
// +0x10, +0x15, +0x14, +0x16, +0x17, +0x18, then +0x1C..+0x30. Each key is its own
// Get / Set, so only the call order differs.
struct IntKey  { const char* key; int32_t GameSettings::* field; };
struct BoolKey { const char* key; bool GameSettings::* field; };   // retail's bool, Get(bool&) / Set(bool)

const IntKey  kIntKeys[] = {
    { "UIScale", &GameSettings::uiScale },
    { "AutosaveInterval", &GameSettings::autosaveInterval },
    { "StatusIconOpacity", &GameSettings::statusIconOpacity },
    { "StatusIconScale", &GameSettings::statusIconScale },
    { "LastOption", &GameSettings::lastOption },
    { "LastTerrain", &GameSettings::lastTerrain },
    { "LastMapSize", &GameSettings::lastMapSize },
    { "LastClimate", &GameSettings::lastClimate },
    { "LastStart", &GameSettings::lastStart },
    { "LastDisaster", &GameSettings::lastDisaster },
};
// THREE of these were in kIntKeys until 2026-09-04 and are NOT ints. Retail
// (0x140098b60 / 0x140098970) passes `(bool*)(param_1 + 3)`, `(bool*)(param_1 + 5)`
// and `(bool*)(param_1 + 6)` — the bool& overload of Persistence::Get/Set, one byte
// at settings+0xC / +0x14 / +0x18. Reading them through the int& overload is a
// different Persistence key type, and the Set side wrote the key back as an INT,
// changing its stored type out from under retail and the Options dialog. The one
// that matters most is StatusIconEnable: settings byte +0xC is exactly what
// StatusIcon_Toggle checks before it will Graphics::Scene::AddObject an icon, so a
// wrong read there silently hides EVERY status icon in the game, citizen and
// building alike, with nothing logged anywhere.
const BoolKey kBoolKeys[] = {
    { "StatusIconEnable", &GameSettings::statusIconEnable },
    { "EdgeScroll", &GameSettings::edgeScroll },
    { "AutoPause", &GameSettings::autoPause },
    { "ShowCelcius", &GameSettings::showCelsius },
    { "ShowWeather", &GameSettings::showWeather },
    { "AutoSlow", &GameSettings::autoSlow },
};

}  // namespace

// GameSettings_Load — faithful port of 0x140098b60.
void GameSettings::Load() {
    ::IO::Persistence* p = PersistenceSingleton();
    if (!p) {
        fprintf(stderr, "[GameSettings] Load SKIPPED — persistence=%p\n", p);
        return;
    }
    // ENDGAME B.3.4a closing: onto IO::Persistence::Get. The field's type picks the
    // overload (int& / bool&) — the bool keys are one byte at
    // settings+0xC/+0x14/+0x18 and must not pick the int& overload.
    bool haveUiScale = false;
    for (const auto& k : kIntKeys) {
        const bool found = p->Get("Game", k.key, this->*k.field);
        if (k.field == &GameSettings::uiScale) haveUiScale = found;
    }
    // NOT RETAIL: first run on a high-density window with no saved UIScale gets the
    // scale that keeps the UI its 1x physical size (docs/macos/UI_SCALE.md). Identity at 1x.
    if (!haveUiScale)
        uiScale = Platform::Display::FirstRunUiScalePercent(uiScale);
    for (const auto& k : kBoolKeys)
        p->Get("Game", k.key, this->*k.field);   // 0x140098bc7 LEA R9,[RBX+0xc] -> [0x1401522f0] Get(bool&), likewise +0x14..+0x18

    fprintf(stderr, "[GameSettings] Load: LastTerrain=%d LastMapSize=%d LastClimate=%d "
            "LastStart=%d LastDisaster=%d (UIScale=%d AutosaveInterval=%d)\n",
            lastTerrain, lastMapSize, lastClimate,
            lastStart, lastDisaster,
            uiScale, autosaveInterval);
    fprintf(stderr, "[GameSettings] Load: StatusIconEnable=%d (settings+0xC) -- 0 hides "
            "EVERY status icon (citizen health, building warnings); it is the Options "
            "checkbox, persisted as GameStatusIconEnable in "
            "<Documents>/Banished/settings.cfg\n",
            (int)statusIconEnable);
    fflush(stderr);
}

// GameSettings_Save — faithful port of 0x140098970.
void GameSettings::Save() {
    ::IO::Persistence* p = PersistenceSingleton();
    if (!p) {
        fprintf(stderr, "[GameSettings] Save SKIPPED — persistence=%p\n", p);
        return;
    }
    // ENDGAME B.3.4a closing: onto IO::Persistence::Set, typed as above.
    for (const auto& k : kIntKeys)
        p->Set("Game", k.key, this->*k.field);
    // 0x1400989d7 MOVZX R9D,byte ptr [RBX+0xc] -> CALL [0x140152300] Set(bool): the byte is passed
    // raw, no `!= 0` (B6 row 21; likewise +0x15, +0x14, +0x16, +0x17, +0x18 at 0x140098a0e..0x140098a7e).
    for (const auto& k : kBoolKeys)
        p->Set("Game", k.key, this->*k.field);

    fprintf(stderr, "[GameSettings] Save: LastTerrain=%d LastMapSize=%d LastClimate=%d "
            "LastStart=%d LastDisaster=%d\n",
            lastTerrain, lastMapSize, lastClimate,
            lastStart, lastDisaster);
    fflush(stderr);
}
