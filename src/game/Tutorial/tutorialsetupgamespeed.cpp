// src/game/Tutorial/tutorialsetupgamespeed.cpp
// Clean reimplementation of TutorialSetupGameSpeed vtable methods.
//
// TutorialSetupGameSpeed (0x28 bytes) -- set game speed for tutorial steps.
// Vtable: 0x140160da0
// Extends TutorialSetup -> Object::External
//
// Fields:
//   +0x20: int32_t speed      -- game speed to set (default 1)
//   +0x24: uint8_t unpause   -- if nonzero, also pause (set controller +0x30 to 0)
//
// The Apply function (Slot8) writes speed to the game's time/speed controller,
// and optionally pauses the game.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialSetupGameSpeed.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"
#include "Game/Map.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e2e40 (inlined in create 0x1400289c0 / createInPlace 0x140028a20):
// External(), the two stamps (the compiler's), speed = 1, +0x24 = 0.
TutorialSetupGameSpeed::TutorialSetupGameSpeed() : speed(1), unpause(0), _pad25{} {}

// ============================================================
// TutorialSetupGameSpeed::Begin (vtable +0x40, 0x1400e2d60)
// ============================================================
// Original disassembly:
//   MOV R8,[g_pGameWorldSingleton]   ; R8 = game singleton
//   MOV EAX,[RCX+0x20]              ; EAX = self->speed
//   MOV RDX,[R8+0x120]              ; RDX = time controller
//   MOV [RDX+0x34],EAX              ; controller->speed = self->speed
//   CMP byte [RCX+0x24],0           ; if (unpause)
//   JZ skip
//   MOV RAX,[R8+0x120]              ;   re-read controller
//   MOV byte [RAX+0x30],0           ;   controller->paused = 0
//   skip: RET
void TutorialSetupGameSpeed::Begin() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // game+0x120 = the PlayerToolManager (the speed / pause controller)
    PlayerToolManager* timeCtrl = game->m_toolManager;
    if (timeCtrl == nullptr) return;

    // Write speed to controller +0x34
    timeCtrl->gameSpeed = speed;

    // If unpause is set, also clear the controller's +0x30 (paused)
    if (unpause) {
        // Re-read controller (matching original code)
        timeCtrl = game->m_toolManager;
        timeCtrl->paused = false;
    }
}

// ============================================================
// TutorialSetupGameSpeed::Serialize (vtable +0x28, 0x1400e2d90)
// ============================================================
// speed (int32) and unpause (bool).
bool TutorialSetupGameSpeed::Serialize(IO::Stream& stream) {
    // The serializer.
    IO::Serializer ser(stream, 0);

    ser.Serialize(speed, nullptr);
    ser.Serialize(unpause, nullptr);
    return true;
}

// ============================================================
// TutorialSetupGameSpeed::Reconstruct [6] 0x140028a70 / Destruct [7] 0x140001640
// ============================================================
Object::External* TutorialSetupGameSpeed::Reconstruct() {
    this->~TutorialSetupGameSpeed();
    return ::new (static_cast<void*>(this)) TutorialSetupGameSpeed();
}
void TutorialSetupGameSpeed::Destruct() { this->~TutorialSetupGameSpeed(); }

// ============================================================
// TutorialSetupGameSpeed::Construct (vtable +0x58, 0x140028a20)
// ============================================================
Object::External* TutorialSetupGameSpeed::Construct() {
    return ::new (static_cast<void*>(this)) TutorialSetupGameSpeed();
}

// create 0x1400289c0: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* TutorialSetupGameSpeed::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialSetupGameSpeed), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialSetupGameSpeed());
}
// createInPlace 0x140028a20.
::Object::Base* TutorialSetupGameSpeed::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialSetupGameSpeed());
}

DEFINE_EXE_TYPE(TutorialSetupGameSpeed, "TutorialSetup")
