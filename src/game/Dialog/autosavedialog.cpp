#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "object/Internal.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "save/SaveLoad.h"   // ENDGAME B.3.7 callers (K1)
#include "Dialog/AutoSaveDialog.h"
#include "Dialog/ui_dialogs.h"
#include "Dialog/dialog_exe_helpers.h"
#include "Object/factory_registration.h"
#include <cstddef>
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/game.h"         // g_gameContainer
#include "Game/GameUpdateLoop.h"
#include "ui/String.h"
#include "ui/StringTable.h"
#include "system/String.h"
#include "system/Value.h"   // ENDGAME B.3.4b System callers: System::Time
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Dialog/exe_bodies.h"

// ============================================================
// [4] AutoSaveDialog::OnOpened (0x14005E110) — vtable +0x20
// ============================================================
// 5-byte JMP thunk to 0x14005E120 (timer setup).
// Stops the current timer, reads game speed setting, picks an
// autosave interval multiplier, and starts a new timer.
//

//   0 → 60.0f,  1 → 120.0f,  2 → 300.0f,  3 → 480.0f,
//   4 → 600.0f, 5 → 900.0f,  6 → 1800.0f, 7 → 3600.0f
// Final interval = multiplier * 3600.0f (seconds).
//
// Original: 0x14005E110 → 0x14005E120.

// UI::DialogInstance::StopTimer / StartTimer live in the Runtime DLL
// (0x180113730 / 0x1801137c0). The exe reaches them through the .rdata dispatch
// slots 0x140151618 / 0x140151620, and NEITHER of those is registered in
// standalone - so the previous `*(void**)ResolveAddr(...)` here dereferenced NULL
// and this function crashed the moment it ran. Resolve them from the Runtime DLL
// instead, preferring the export name and falling back to the verified entry
// address. Fixed 2026-08-30 (LANE1 rows 13/14).
// ENDGAME B.3.6 (K2): both are now direct UI::DialogInstance member calls; the
// two by-name caches went with their last callers.
// Time::Time(float) is the import slot 0x140152b00: RtSystem_Time_Ctor, called by
// name (B3R).

void AutoSaveDialog::OnOpened() {
    StopTimer(0);

    // Autosave-interval setting: gs[1] = the Options 'Autosave' combo index (0..7),
    // read with no test of the settings pointer (0x14005e120).
    int speedSetting = g_gameSettings->autosaveInterval;

    // Autosave interval table, in MINUTES, read off retail's .rdata (2026-08-30).
    // The switch at 0x14005e130 picks one of these eight floats and multiplies by
    // DAT_1401721f0; Time::Time() then treats the product as SECONDS:
    //   [0] g_fUVScale             0x14017214c = 1.0
    //   [1] g_pAgeMultiplier       0x140172198 = 5.0
    //   [2] DAT_1401721b0                      = 10.0
    //   [3] DAT_1401721bc                      = 15.0
    //   [4] DAT_1401721c4                      = 20.0
    //   [5] g_pNotifyTimerDuration 0x1401721cc = 30.0
    //   [6] DAT_1401721e0                      = 45.0
    //   [7] DAT_1401721f0                      = 60.0   <- also the multiplier
    // This table previously held {60,120,300,480,600,900,1800,3600} and multiplied by
    // 3600.0f - wrong twice over: four of the eight entries did not match retail, and
    // the multiplier is 60 (minutes->seconds), not 3600. At the default "5 minutes"
    // setting it asked for 120*3600 s = 5 DAYS of game time, so the timer could never
    // fire even once the dialog exists. See LANE1 rows 13/14.
    static const float kIntervalMinutes[8] = {
        1.0f, 5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f
    };
    static const float kMinutesToSeconds = 60.0f;   // DAT_1401721f0

    // Retail's switch has no default arm: an out-of-range setting leaves the timer
    // stopped rather than restarting it.
    if (speedSetting < 0 || speedSetting > 7)
        return;

    float interval = kIntervalMinutes[speedSetting] * kMinutesToSeconds;

    System::Time timerTime;
    RtSystem_Time_Ctor(&timerTime, interval);   // import slot Time::Time(float)

    StartTimer(0, timerTime._ticks);

}

// ============================================================
// [8] AutoSaveDialog::OnElementEvent (0x14005E0F0) — vtable +0x40, OnCommand
// ============================================================
// 12 bytes: CMP R8D,0x13; JZ target; RET
// If msg (param3) == 0x13 (timer event), triggers autosave via
// SaveLoad system. Otherwise returns immediately.
// The autosave body at 0x14005DEC0 is AutoSaveDialog::Autosave below.
//
// Original: 0x14005E0F0.

void AutoSaveDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                    UI::EventId param3, int32_t /*param4*/, int32_t /*c*/,
                                    int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 != UI::EventId::Timer)
        return;

    // 0x14005DEC0, the autosave body (a tail JMP in retail). Wired 2026-08-30
    // (LANE1 rows 13/14): before that the timer tick was swallowed.
    Autosave();
}

// ============================================================
// AutoSaveDialog::Autosave (0x14005DEC0)
// ============================================================
// Triggered by OnElementEvent on timer event 0x13. If the game is not paused and
// not in the bit-10 "blocked" state, and no save/load operation is already in
// progress, kicks off an autosave: 1s min-display time, the container's
// SaveLoadEmpty sprite, the localized "Autosave" name, then SetOperation(kOpSave, -4).

// 0x140099d40: the localized "Autosave" name — UI::String over the container's
// globalNames string table (+0xD8).
static UI::String AutosaveName(const GameContainer* game) {
    System::String nameStr("Autosave");
    return UI::String(game->globalNames, nameStr);
}

static void AsBail(const char* why, int32_t v) {
    fprintf(stderr, "[AutoSave] timer FIRED but bailed: %s (%d)\n", why, v);
    fflush(stderr);
}

void AutoSaveDialog::Autosave() {
    // Not while the UI pauses the game.
    if (_ui->PausesGame()) { AsBail("UI PausesGame", 1); return; }

    // Not while GameWorld flag bit 10 is set (the world read unguarded).
    uint32_t gwFlags = GameUpdateLoop::_singleton->m_gameFlags;
    if ((gwFlags >> 10) & 1) { AsBail("gameWorld+0xd0 bit10 set", (int32_t)gwFlags); return; }

    // Only when no operation is already in progress (kOpNone).
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    { Save::SaveLoad::Operation op = saveLoad->GetOperation();
      if (op != Save::SaveLoad::kOpNone) { AsBail("SaveLoad busy, Operation", (int32_t)op); return; } }

    // Min operation-display time = 1.0s.
    System::Time minTime;
    RtSystem_Time_Ctor(&minTime, 1.0f);   // import slot Time::Time(float)
    saveLoad->SetMinOperationTime(minTime);

    // The container's SaveLoadEmpty sprite (+0xB8), by value (the container read unguarded).
    UI::Sprite sprite = g_gameContainer->spriteSaveLoadEmpty;
    saveLoad->SetSaveSprite(&sprite);

    // Localized "Autosave" name -> System::String -> SetSaveName(slot 2).
    System::String sysStr = AutosaveName(g_gameContainer);
    saveLoad->SetSaveName(sysStr, 2);

    // Kick off the save operation (kOpSave, param -4).
    saveLoad->SetOperation(Save::SaveLoad::kOpSave, -4);
}

// [6] AutoSaveDialog::OnUpdateLayout (0x14005DFC0) — Draw
// Checks SaveLoad operation type. If operation==kOpSave (save), flips the indicators
// one way; any other operation flips them the other way.
// Applies rotation to the visible indicator.
void AutoSaveDialog::OnUpdateLayout() {
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    if (!saveLoad)
        return;

    Save::SaveLoad::Operation operation = saveLoad->GetOperation();

    // kOpSave is the save; any other operation flips the flags the other way
    uint32_t hideFlags = (uint32_t)(operation != Save::SaveLoad::kOpSave);
    uint32_t showFlags = (uint32_t)(operation == Save::SaveLoad::kOpSave);

    if (savingImage) {
        savingImage->SetFlagsOnOff(hideFlags, showFlags);
    }

    if (animatedImage) {
        animatedImage->SetFlagsOnOff(hideFlags, showFlags);
    }

    // Apply rotation to first indicator element
    if (savingImage) {
        savingImage->SetRotation(angle);
    }
}

// [7] AutoSaveDialog::OnUpdate (0x14005E090) — Update
// Accumulates rotation angle from elapsed time at 90 degrees/sec.
// Wraps angle to stay within [0, 360).
void AutoSaveDialog::OnUpdate(const System::Time& time) {
    float elapsed = time.AsSeconds();
    float newAngle = angle + elapsed * 90.0f;

    if (newAngle >= 360.0f) {
        do {
            newAngle -= 360.0f;
        } while (newAngle >= 360.0f);
    }

    angle = newAngle;
}

// ============================================================
// Construction, destruction, reflection
// ============================================================
// Retail AutoSaveDialog_CopyCtor (0x14005e230): DialogInstance base ctor, then
// resolve the two spinner elements by name and zero the angle.
// AutoSaveDialog owns no heap of its own: +0xB8/+0xC0 are borrowed element refs and
// +0xC8 is the spinner angle, so the dtor is just the DialogInstance base dtor.

AutoSaveDialog::AutoSaveDialog() : savingImage(nullptr), animatedImage(nullptr) {}

AutoSaveDialog::AutoSaveDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                               Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      savingImage(nullptr), animatedImage(nullptr) {
    {
        // Stored unchecked, as retail (0x14005e230); OnUpdateLayout rotates it.
        System::String nameStr("savingImage");
        savingImage = static_cast<UI::ImageElement*>(FindElement(nameStr).Get());
    }
    {
        System::String nameStr("animatedImage");
        animatedImage = FindElement(nameStr).Get();
    }
    angle = 0.0f;   // spinner angle
}

// create 0x1400106c0: MemAlloc(0xd0, 8), then the ctor.
::Object::Base* AutoSaveDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AutoSaveDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) AutoSaveDialog());
}
// createInPlace 0x140010730 (the tree's in-place build is kept, B8-13).
::Object::Base* AutoSaveDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) AutoSaveDialog());
}

DEFINE_EXE_TYPE(AutoSaveDialog, "UI::DialogInstance")

// The slot-4 DOOR, reached by address: reg_autosave.cpp registers it at
// 0x14005e120 / 0x14005e110 and OptionsDialog's apply path calls it through
// fn_UpdateAutoSaveTimer (engine_funcs.cpp). A QUALIFIED member call.
void AutoSaveDialog_Slot4(AutoSaveDialog* self) {
    self->AutoSaveDialog::OnOpened();
}
