// src/game/Goal/timegoalinstance.cpp
// TimeGoalInstance (0x28 bytes) — tracks elapsed time for TimeGoal.
// ENDGAME B.3.8 block GOAL: a REAL class (Goal/TimeGoalInstance.h) with a
// compiler-generated vtable; the retail evidence per slot is in the header.
//
// Fields:
//   +0x10..+0x1F: GoalInstance base (_flags, _goal = the parent TimeGoal)
//   +0x20: int64   elapsedTime — accumulated time in engine ticks (System::Time)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Goal/TimeGoalInstance.h"
#include "Goal/TimeGoal.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "object/TypeInfo.h"      // Object::Base::IsDerivedFrom
#include "Object/factory_registration.h"
#include <cstdint>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7: QUALIFIED base calls
#include "system/Value.h"    // ENDGAME B.3.4b System callers: System::Time
#include <new>

// ctor 0x1400dad90 (inlined in create 0x140026670): GoalInstance(), the vptr,
// elapsedTime = 0.
TimeGoalInstance::TimeGoalInstance() : elapsedTime{} {}

// ============================================================
// TimeGoalInstance::Persist (vtable +0x18, 0x1400dad30)
// ============================================================
// GoalInstance::Persist first, then the elapsed time via Time::Serialize.
void TimeGoalInstance::Persist(IO::Stream& stream) {
    // Achievement::GoalInstance::Persist (slot 3, retail 0x18009ef70) — QUALIFIED
    // (B37_RULES B7-13). Retail calls the base UNCONDITIONALLY; the base reads an
    // ExternalPtr + a uint off the stream, and skipping it desyncs everything
    // after this object.
    Achievement::GoalInstance::Persist(stream);

    // The serializer.
    IO::Serializer serializer(stream, 0);

    // Serialize elapsed time (+0x20) via System::Time::Serialize
    elapsedTime.Serialize(serializer);
}

// ============================================================
// TimeGoalInstance::Update (vtable +0x28, 0x1400dac30)
// ============================================================
//   elapsedTime += time;
//   InternalBasePtr ibp(this);
//   if (!list.AreGoalsMetExcept(&ibp)) elapsedTime = 0;
//   GoalInstance::Update(time, list);
void TimeGoalInstance::Update(const System::Time& time, const Achievement::GoalList& list) {
    // Accumulate time
    elapsedTime._ticks += time._ticks;

    // Check if other goals are met (except this instance). The InternalPtr is
    // built through the IBP ctor (import slot 0x140152a90) exactly as retail.
    const Object::InternalPtr<Achievement::GoalInstance> self(this);

    // Achievement::GoalList::AreGoalsMetExcept (retail 0x18009f070, NON-virtual)
    // — the InternalPtr argument by HIDDEN POINTER (B37_RULES B7-5).
    bool othersMet = list.AreGoalsMetExcept(&self);
    if (!othersMet) {
        // Reset elapsed time to zero — other goals not met
        elapsedTime._ticks = 0;
    }

    // Achievement::GoalInstance::Update (slot 5, retail 0x18009f010) — QUALIFIED (B7-13).
    Achievement::GoalInstance::Update(time, list);
}

// ============================================================
// TimeGoalInstance::CheckGoalState (vtable +0x30, 0x1400dacb0)
// ============================================================
//   goal = this->_goal
//   timeGoal = IsDerivedFrom(goal, TypeInfo_TimeGoal) ? goal : null
//   seconds = Time::AsSeconds(&elapsedTime)
//   return (timeGoal->requiredYears * 0xe10) <= seconds
// 0xe10 = 3600 = seconds per year in game time.
Achievement::Goal::GoalState TimeGoalInstance::CheckGoalState() const {
    // Get parent goal pointer
    const Object::Base* goal = _goal.Get();

    // Cast to TimeGoal via IsDerivedFrom (import slot 0x140152a98) against the
    // id in TypeInfo_TimeGoal (dword 0x1401c62e8 = TimeGoal's own record, i.e.
    // TimeGoal::TypeId()). The tree used to ResolveAddr that cell, which is
    // not registered, so it answered 0 and the goal could never be met.
    // B6 row 88: 0x1400dacd4 IsDerivedFrom([this+0x18]) with no null test of the goal,
    // 0x1400dace4 CMOVNZ RBX,RDI over RBX = 0, 0x1400dacee MOV ECX,[RBX+0x30] with no test.
    const TimeGoal* timeGoal = nullptr;
    if (goal->IsDerivedFrom(TimeGoal::TypeId())) {
        timeGoal = static_cast<const TimeGoal*>(goal);
    }

    // Convert elapsed time to seconds
    float elapsedSeconds = elapsedTime.AsSeconds();

    // Compare against required time
    float requiredSeconds = (float)(timeGoal->requiredYears * 0xe10);
    return (requiredSeconds <= elapsedSeconds) ? Achievement::Goal::kMet : Achievement::Goal::kNotMet;
}

// create 0x140026670: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* TimeGoalInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TimeGoalInstance), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TimeGoalInstance());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (null);
// the tree's in-place build is kept (B38_RULES B8-13).
::Object::Base* TimeGoalInstance::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TimeGoalInstance());
}

DEFINE_EXE_TYPE(TimeGoalInstance, "Achievement::GoalInstance")
