// src/game/Goal/tutorialgoalgamespeed.cpp
// Clean reimplementation of TutorialGoalGameSpeed vtable methods.
//
// TutorialGoalGameSpeed (0x28 bytes) — tutorial goal waiting for game speed change.
// Vtable: 0x1401608e0
// Extends TutorialGoal -> Object::External
//
// Fields:
//   +0x20: int32_t targetSpeed — target game speed (default 1)
//
// The Check function is trivial: compare game's current speed against targetSpeed.
// Reads from: game(singleton) -> +0x120 (time/speed controller) -> +0x34 (current speed)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialGoalGameSpeed.h"
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
// ctor 0x1400df240 (inlined in create 0x140027630 / createInPlace 0x140027690):
// External(), the two stamps (the compiler's), targetSpeed = 1.
TutorialGoalGameSpeed::TutorialGoalGameSpeed() : targetSpeed(1), _pad24{} {}

// ============================================================
// TutorialGoalGameSpeed::IsComplete (vtable +0x40, 0x1400df150)
// ============================================================
// Original: MOV RAX,[g_pGameWorldSingleton]; MOV ECX,[RCX+0x20];
//           MOV RDX,[RAX+0x120]; CMP [RDX+0x34],ECX; SETZ AL; RET
bool TutorialGoalGameSpeed::IsComplete(const System::Time* /*time*/, void* /*instance*/) {
    if (GameUpdateLoop::_singleton == nullptr) return false;
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // game+0x120 = the PlayerToolManager (the speed controller)
    PlayerToolManager* timeCtrl = game->m_toolManager;
    if (timeCtrl == nullptr) return false;

    // its +0x34 = current game speed
    int32_t currentSpeed = timeCtrl->gameSpeed;

    return currentSpeed == targetSpeed;
}

// ============================================================
// TutorialGoalGameSpeed::Serialize (vtable +0x28, 0x1400df170)
// ============================================================
// 1. TutorialGoal::Serialize (its empty Serializer scope)
// 2. a Serializer over the stream: int +0x20 (targetSpeed)
bool TutorialGoalGameSpeed::Serialize(IO::Stream& stream) {
    TutorialGoal::Serialize(stream);   // QUALIFIED: the base body

    // The serializer.
    IO::Serializer ser(stream, 0);

    // Serialize targetSpeed (+0x20) — int
    ser.Serialize(targetSpeed, nullptr);
    return true;
}

// ============================================================
// TutorialGoalGameSpeed::Reconstruct [6] 0x1400276e0 / Destruct [7] 0x140001640
// ============================================================
Object::External* TutorialGoalGameSpeed::Reconstruct() {
    this->~TutorialGoalGameSpeed();
    return ::new (static_cast<void*>(this)) TutorialGoalGameSpeed();
}
void TutorialGoalGameSpeed::Destruct() { this->~TutorialGoalGameSpeed(); }

// ============================================================
// TutorialGoalGameSpeed::Construct (vtable +0x48, 0x140027690)
// ============================================================
Object::External* TutorialGoalGameSpeed::Construct() {
    return ::new (static_cast<void*>(this)) TutorialGoalGameSpeed();
}

// create 0x140027630: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* TutorialGoalGameSpeed::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialGoalGameSpeed), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialGoalGameSpeed());
}
// createInPlace 0x140027690.
::Object::Base* TutorialGoalGameSpeed::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialGoalGameSpeed());
}

DEFINE_EXE_TYPE(TutorialGoalGameSpeed, "TutorialGoal")
