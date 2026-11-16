// src/game/Component/firecomponent.cpp
// FireComponent — the per-frame tick (vtable slot 9), the Serialize slot (3), and
// the factory.
//
// Two-state fire:
//   idle    (fireTimer == 0) — accumulate dt into +0x40 and roll for ignition,
//                              unless GameWorld+0xD0 bit 6 ("no fire") is set.
//   burning (fireTimer != 0) — count both timers down; spread when the spread
//                              timer expires; extinguish when the fire timer does.
//
// Original: 0x140048fb0 (53 instructions).
//
// ⚠ WHY THIS FILE USED TO DO NOTHING AT ALL — three stacked reasons, all fixed:
//   1. The factory was COMPONENT_INERT_FACTORY, so vtable slot 9 was Inert_Stub and
//      FireComponent_Update had no caller anywhere in the tree.
//   2. Ignite / Spread / Extinguish were unported, so ResolveAddr returned 0 and
//      every call site here was skipped by a null guard.
//   3. The FireDescription type-id global (0x1401c4218) was unregistered, so
//      IsDerivedFrom could never match and an added `if (buildingData)` guard
//      deleted the ignition roll outright. That guard is GONE — retail calls
//      FUN_1400a9290 unconditionally and so do we; see FireDesc() in
//      firecomponent_ignite.cpp for how the description is now obtained.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/firecomponent_funcs.h"
#include "Object/factory_registration.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/FireDescription.h"
#include "Action/exe_bodies.h"

// Leaf bodies (firecomponent_ignite.cpp): FireComponent::Ignite / SpreadToNeighbors /
// Extinguish.

// FUN_1400a9290 — {rate, weight} probability roll (BreakTimeCheck, Action/exe_bodies.h,
// called directly). Reads BOTH floats of the pair and CLEARS *time on every call,
// both arms.

// ============================================================
// ENDGAME B.3.8 CIT — FireComponent as a REAL class (FireComponent.h).
// ============================================================
// Before: a 48-slot hand-built g_FireComponent_LiveVtable filled at every create
// (slots 3 Serialize, 9 Update, 15 water applied), Inert_ScalarDtor in slot 0,
// and registered at the retail table address 0x140159380. After: the compiler's
// 16-slot vtable and a real ctor/dtor chain (B38_RULES D1, D2). The retail
// table address had no by-address reader in the tree (every SendComponentMessage
// +0x78 dispatch goes through the object's own vptr), so its registration went
// with the hand-built table (B38 D5).
#include "Object/factory_registration.h"
#include "Component/FireComponent.h"

// 0x140049b80. Component() is the CALL 0x1401188b0; the 0x140159380 stamp is
// the compiler's; +0x28..+0x40 = 0; flags |= 0x08 (0x88).
FireComponent::FireComponent()
    : bucketsApplied(0), bucketsAtSpread(0), fireTimer{0}, spreadTimer{0}, igniteAccum{0} {
    OrFlagsInCtor(0x08);
}

// [3] 0x140049a90.
// ============================================================
// FireComponent::Shutdown / Serialize (vtable slot 3, 0x140049a90)
// ============================================================
// Component_Serialize, then a version-1 Serializer block:
//   Time  +0x30, Time +0x38, int +0x28,
//   and, only when the stream version > 0, int +0x2c.
// This is the third independent body proving +0x28 / +0x2c are two int32s.
void FireComponent::Persist(IO::Stream& stream) {

    Component::Persist(stream);   // retail's direct CALL 0x1401189b0 (was the engine_funcs door)

    IO::Serializer ser(stream, 1);
    fireTimer.Serialize(ser);
    spreadTimer.Serialize(ser);
    ser.Serialize(bucketsApplied, nullptr);
    if (ser.GetVersion() > 0)
        ser.Serialize(bucketsAtSpread, nullptr);
}

// [9] 0x140048fb0. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af).
// ============================================================
// FireComponent::Update (vtable slot 9, 0x140048fb0)
// ============================================================
void FireComponent::Update(const System::Time* time) {
    const System::Time* pDeltaTime = time;
    if (!pDeltaTime) return;

    const int64_t dt = pDeltaTime->_ticks;

    if (fireTimer._ticks == 0) {
        // ---- idle: roll for a new fire ----
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        if (!gw) return;

        // GameWorld+0xD0 is the debug / new-game flag word: bit0 zeroCost, bit1 noHunger,
        // bit2 noCold, bit3 noHealth, bit4 noSick, bit5 noCropDisease, BIT6 noFire,
        // bit7 noTornado. The GameUpdateLoop ctor (gameupdateloop.cpp) writes the whole word
        // explicitly from NewGameDialog::StartGame's disaster combo: index 0 => 0 (fire ON),
        // otherwise 0xF0 (bits 4-7 => fire OFF). It is never left uninitialised.
        const uint32_t worldFlags = gw->m_gameFlags;
        if ((worldFlags >> 6) & 1) {
            return;
        }

        igniteAccum._ticks += dt;

        // Retail: FUN_1400a9290(&accum, (float*)(desc + 0x44), gw->rng) — called
        // UNCONDITIONALLY with the description pointer, which retail always has.
        // No `if (desc)` here: that guard is what used to delete this roll.
        // 0x140048ff0-0x140049008: IsDerivedFrom on [this+0x18] (no null test), CMOVNZ.
        Description* obj = _description;
        FireDescription* desc = obj->IsDerivedFrom(FireDescription::TypeId())
                   ? static_cast<FireDescription*>(obj) : nullptr;
        System::Random* rng = gw->m_random;
        float* pair = desc->igniteChance;

        const bool fired = BreakTimeCheck(&igniteAccum, pair, rng);   // 0x1400a9290 (was its ActMvDep_TimeCheck_a9290 forwarder)
        if (fired) {
            Ignite();
            return;
        }
    } else {
        // ---- burning: count down ----
        fireTimer._ticks   -= dt;
        spreadTimer._ticks -= dt;

        if (fireTimer._ticks > 0 && spreadTimer._ticks < 1)
            SpreadToNeighbors();

        if (fireTimer._ticks < 1)
            Extinguish();
    }
}


// create 0x1400055e0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* FireComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FireComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FireComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* FireComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FireComponent());
}

DEFINE_EXE_TYPE(FireComponent, "Component")
