// src/game/Goal/populationgoal.cpp
// PopulationGoal (0x38 bytes) — achievement goal requiring a population count.
// ENDGAME B.3.8 block GOAL: a REAL class (Goal/PopulationGoal.h) with a
// compiler-generated vtable; the retail evidence per slot is in the header.
//
// Fields (from Ghidra decompilation):
//   +0x30: int32_t  targetPop        — required population count (default 100)
//
// The world population count is at game+0xF0+0xA8.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Goal/PopulationGoal.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7: QUALIFIED base calls
#include "engine/UpdateLoop.h"  // ENDGAME B.3.7 callers (K2): UpdateLoop::IsRunning
#include "Entity/CitizenBehavior.h"
#include <new>

// ctor 0x1400d78f0: Goal(), the vptr, targetPop = 100.
PopulationGoal::PopulationGoal() : targetPop(100), _pad34{} {}

// ============================================================
// PopulationGoal::Serialize (vtable +0x28, 0x1400d7870)
// ============================================================
// Calls Goal::Serialize, then serializes targetPop (int32 at +0x30).
bool PopulationGoal::Serialize(IO::Stream& stream) {
    // Achievement::Goal::Serialize (slot 5, 0x18009ee20) — QUALIFIED (B7-13).
    Achievement::Goal::Serialize(stream);

    // The serializer.
    IO::Serializer serializer(stream, 0);

    // Serialize targetPop at +0x30
    serializer.Serialize(targetPop, nullptr);

    return true;
}

// ============================================================
// PopulationGoal::Reconstruct [6] 0x140025fb0 / Destruct [7] 0x140001640
// ============================================================
// retail 0x140025fb0: vtable[0](this, 0) (`~T()` (slot 0, flags 0): destroy in place),
// then the ctor on `this`.
Object::External* PopulationGoal::Reconstruct() {
    this->~PopulationGoal();
    return ::new (static_cast<void*>(this)) PopulationGoal();
}
void PopulationGoal::Destruct() { this->~PopulationGoal(); }

// ============================================================
// PopulationGoal::CheckGoalState (vtable +0x40, 0x1400d7820)
// ============================================================
//   if (g_pGameWorldSingleton != NULL && IsRunning(g_pGameWorldSingleton))
//       return *(int*)(*(int64_t*)(g_pGameWorldSingleton+0xF0) + 0xA8) >= targetPop;
//   return false;
Achievement::Goal::GoalState PopulationGoal::CheckGoalState() const {
    if (GameUpdateLoop::_singleton == nullptr) return kNotMet;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (gameWorld == nullptr) return kNotMet;

    // Check IsRunning — Engine::UpdateLoop::IsRunning (import slot 0x140152850),
    // non-virtual. The door's null-`this` test is covered by the check above.
    bool running = gameWorld->IsRunning();
    if (!running) return kNotMet;

    // The citizen list at game+0xF0
    CitizenBehavior* worldData = gameWorld->m_citizenList;
    if (worldData == nullptr) return kNotMet;

    // Compare the citizen count (+0xA8) against targetPop
    int currentPop = worldData->citizens._size;
    return (currentPop >= targetPop) ? kMet : kNotMet;
}

// ============================================================
// PopulationGoal::Construct (vtable +0x50, 0x140025f60)
// ============================================================
Object::External* PopulationGoal::Construct() {
    return ::new (static_cast<void*>(this)) PopulationGoal();
}

// create 0x140025f00: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* PopulationGoal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PopulationGoal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) PopulationGoal());
}
// createInPlace 0x140025f60.
::Object::Base* PopulationGoal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) PopulationGoal());
}

DEFINE_EXE_TYPE(PopulationGoal, "Achievement::Goal")
