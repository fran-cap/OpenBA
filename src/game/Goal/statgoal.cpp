// src/game/Goal/statgoal.cpp
// StatGoal (0x80 bytes) — achievement goal requiring a stat comparison.
// ENDGAME B.3.8 block GOAL: a REAL class (Goal/StatGoal.h) with a
// compiler-generated vtable; the retail evidence per slot is in the header.
//
// Field layout (from StatGoal_Ctor at 0x1400daa80 and Serialize at 0x1400da8b0):
//   +0x30: int32  statTypeA           — stat type enum for first operand (default 5)
//   +0x34: int32  statTypeB           — stat type enum for second operand (default 5)
//   +0x38: int32  comparisonType      — comparison enum (0-11: EQ/NE/LT/LE/GT/GE, even=A, odd=B)
//   +0x3c: int32  fixedValue          — fixed comparison value when not using statB (default 1)
//   +0x40: bool   requireOtherGoalsMet — adds 1 to result if true
//   +0x48: NamedExternalRef statA     — {String name, generation, External*} for stat A
//   +0x60: NamedExternalRef statB     — the same for stat B
//   +0x78: int32  statFlags           — flags/category (default 0x20)
//   +0x7c: int32  statSubType         — sub-type index (default 1)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Goal/StatGoal.h"
#include "Goal/goal_exe_helpers.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "system/String.h"
#include "system/Filename.h"
#include <new>
#include <cstdint>
#include <cstring>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7: QUALIFIED base calls
#include "ext/registry.h"                                // Ext::Fix().firefighter_wells
#include "ext/firefighter_wells/firefighter_wells_rule.h"

// The two exe helpers the slots call — FUN_1400265b0 (the lazy ExternalPtr
// resolve) and FUN_1400da4e0 (the stat value switch) — are ported
// (goal_exe_helpers.cpp, statvaluedispatch.cpp) and registered at those
// addresses by standalone/reg_goal.cpp. ENDGAME B.3.8 (B6-5): the members call
// them directly instead of a lazily filled ResolveAddr cache.

// ============================================================
// Construction / destruction
// ============================================================
// ctor 0x1400daa80: Goal(), the vptr, then the stat defaults.
StatGoal::StatGoal()
    : statTypeA(5), statTypeB(5), comparisonType(10), fixedValue(1),
      requireOtherGoalsMet(false), _pad41{}, statA(),
      statB(), statFlags(0x20), statSubType(1) {}

// The dtor inside [0] 0x140026540: the two name Strings (B then A, 0x140026570 /
// 0x140026580), then ~Goal. B4 Y GAMEA (H5): they are the members' ~String,
// run after this empty body in reverse declaration order.
StatGoal::~StatGoal() {}

// ============================================================
// StatGoal::Serialize (vtable +0x28, 0x1400da8b0)
// ============================================================
// Calls base Goal::Serialize first, then serializes the stat-specific fields.
// Retail ends in `MOV AL,1` (0x1400da9ee).
bool StatGoal::Serialize(IO::Stream& stream) {
    // Achievement::Goal::Serialize (slot 5, 0x18009ee20) — QUALIFIED (B7-13).
    Achievement::Goal::Serialize(stream);

    // The serializer (stack-local).
    IO::Serializer ser(stream, 0);

    // statTypeA / statTypeB / comparisonType, each through the UINT door
    // (readerWriter vtable +0x98). Retail 0x1400da8e2.
    ser.Serialize(statTypeA, nullptr);
    ser.Serialize(statTypeB, nullptr);
    ser.Serialize(comparisonType, nullptr);

    // fixedValue (+0x3c), Serialize(int&)
    ser.Serialize(fixedValue, nullptr);

    // requireOtherGoalsMet (+0x40), Serialize(bool&)
    ser.Serialize(requireOtherGoalsMet, nullptr);

    // statA's name (+0x48) via String::Serialize. Each leg is wrapped in the
    // Filename stack temporary retail's compiler emitted.
    {
        System::Filename filenameBuf;
        (void)filenameBuf;
        statA.name.Serialize(ser);

        // If loading (mode == 0), resolve the reference. Original (0x1400da960):
        // RAX = *(serializer+0) [the stream], then *(int*)(RAX+8) == 0.
        if (ser._stream && static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            statA.Resolve();
        }
    }

    // statB's name (+0x60)
    {
        System::Filename filenameBuf;
        (void)filenameBuf;
        statB.name.Serialize(ser);

        if (ser._stream && static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            statB.Resolve();
        }
    }

    // statFlags (+0x78), the same UINT door. Retail 0x1400da9bb.
    ser.Serialize(statFlags, nullptr);

    // statSubType (+0x7c) via Serialize(int&)
    ser.Serialize(statSubType, nullptr);
    return true;
}

// ============================================================
// StatGoal::Reconstruct [6] 0x140026520 / Destruct [7] 0x140001640
// ============================================================
// Retail: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor on `this`.
Object::External* StatGoal::Reconstruct() {
    this->~StatGoal();
    return ::new (static_cast<void*>(this)) StatGoal();
}
void StatGoal::Destruct() { this->~StatGoal(); }

// ============================================================
// StatGoal::CheckGoalState (vtable +0x40, 0x1400da720)
// ============================================================
// 1. Copies statA's ExternalPtr, resolves it, looks the stat value up
// 2. If comparisonType is odd, the same for statB; else fixedValue (+0x3c)
// 3. Switch on comparisonType (even/odd pairs share the comparison):
//    0,1 = EQ; 2,3 = NE; 4,5 = LT; 6,7 = LE; 8,9 = GT; 10,11 = GE
// 4. requireOtherGoalsMet: comparisonMet ? 2 : 1, otherwise comparisonMet ? 1 : 0

// Operand computation, split out unchanged so a diagnostic can read the two
// numbers the comparison is about to use. Retail has no such split; the
// instruction order inside is untouched.
// Retail copies the WHOLE 0x18 by-name reference to the stack, resolves the
// COPY and reads the External back out of it: the member is left untouched.
static const Object::External* ResolveCopy(const NamedExternalRef& ref) {
    NamedExternalRef local(ref);   // retail's stack copy of the reference
    local.Resolve();
    return local.resolved;         // then the copy's ~String
}

void StatGoal::ComputeOperands(int& statValueA, int& comparisonValue) const {
    // --- Resolve statA, look up stat value A ---
    const Object::External* extA = ResolveCopy(statA);
    statValueA = static_cast<int>(StatValue(statTypeA, extA));

    // --- Determine comparison value ---
    comparisonValue = 0;
    if ((comparisonType & 1) != 0) {
        // Odd comparisonType: use statB
        const Object::External* extB = ResolveCopy(statB);
        comparisonValue = static_cast<int>(StatValue(statTypeB, extB));
    } else {
        // Even comparisonType: use fixedValue
        // FIX firefighter_wells (docs/retail_bugs.md section 20): the Firefighter goal's data says
        // 40 wells where its description says 20; with the row on that one goal compares against 20.
        comparisonValue = Ext::FirefighterWells::Threshold(Ext::Fix().firefighter_wells, statTypeA,
                                                           fixedValue, statA.name._data);
    }
}

Achievement::Goal::GoalState StatGoal::CheckGoalState() const {
    int statValueA = 0;
    int comparisonValue = 0;
    ComputeOperands(statValueA, comparisonValue);

    // --- Evaluate comparison ---
    bool comparisonMet = false;
    int cmpType = static_cast<int>(comparisonType);
    if (cmpType >= 0 && cmpType <= 11) {
        switch (cmpType) {
            case 0: case 1:   comparisonMet = (statValueA == comparisonValue); break;
            case 2: case 3:   comparisonMet = (statValueA != comparisonValue); break;
            case 4: case 5:   comparisonMet = (statValueA <  comparisonValue); break;
            case 6: case 7:   comparisonMet = (statValueA <= comparisonValue); break;
            case 8: case 9:   comparisonMet = (statValueA >  comparisonValue); break;
            case 10: case 11: comparisonMet = (statValueA >= comparisonValue); break;
        }
    }

    // Apply requireOtherGoalsMet modifier
    if (requireOtherGoalsMet) {
        return static_cast<GoalState>(comparisonMet ? 2 : 1);
    }
    return static_cast<GoalState>(comparisonMet ? 1 : 0);
}

// ============================================================
// StatGoal_ProbeOperands — diagnostic accessor (not retail)
// ============================================================
// Returns the two numbers CheckGoalState compares. Uses the same member, so it
// can never disagree with the verdict.
extern "C" void StatGoal_ProbeOperands(const StatGoal* goal, int* outA, int* outB) {
    int a = 0, b = 0;
    if (goal) goal->ComputeOperands(a, b);
    if (outA) *outA = a;
    if (outB) *outB = b;
}

// ============================================================
// StatGoal::Construct (vtable +0x50, 0x140026510)
// ============================================================
Object::External* StatGoal::Construct() {
    return ::new (static_cast<void*>(this)) StatGoal();
}

// create 0x1400264d0: MemAlloc(0x80, 8), then the ctor (0x1400daa80).
::Object::Base* StatGoal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StatGoal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) StatGoal());
}
// createInPlace 0x140026510.
::Object::Base* StatGoal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) StatGoal());
}


DEFINE_EXE_TYPE(StatGoal, "Achievement::Goal")
