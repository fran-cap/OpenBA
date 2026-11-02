// src/game/Component/livestockcomponent.cpp
// Clean reimplementation of LivestockComponent::Update from Application-x64.exe.
//
// LivestockComponent (0x38 bytes) is a simple reproduction timer. Accumulates
// elapsed time into +0x30. When the accumulator exceeds a threshold
// (configComponent+0x2C * 300.0f), increments the birth counter at
// herdComponent(+0x28)+0x44 and wraps.
//
// Original: 0x140123b50
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/LivestockDescription.h"
#include "Component/PastureComponent.h"
#include "Entity/CitizenBehavior.h"   // CitizenBehavior::livestock (GameUpdateLoop::m_citizenList)
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/exe_bodies.h"   // Array<T>::Remove (0x140002cc0)
#include "Game/exe_constants.h"     // kRdata_140172234
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Timer.h"   // ENDGAME B.3.9 SS2: System::Timer::_secondsPerTick

// ============================================================
// LivestockComponent::Update (vtable slot [9], 0x140123b50)
// ============================================================
// Reproduction timer: accumulates delta time, fires birth event
// when threshold exceeded (configInterval * 300.0f seconds -> ticks).
// The multiplier is the .rdata constant 300.0f at 0x140172234 (kRdata_140172234).

// ============================================================
// LivestockComponent::OnAdd (vtable slot [6], 0x140123c70)
// ============================================================
// A livestock with no pasture (+0x28 == 0) files itself into the world's
// FREE-ROAMING livestock list at gameState+0x80 (count +0x88). This is what makes
// the start-condition livestock (Template\LivestockCow/Sheep/Chicken.rsc, spawned
// loose by the start-config simple pool) findable by a pasture's adopt pass.
//
// FUN_140003110 is DynArray8_ResizeFill; called with count+1 it appends.


// ============================================================
// LivestockComponent::OnRemove (vtable slot [7], 0x140123c00)
// ============================================================
// Erase from whichever list owns it: the world free-roaming list when it has no
// pasture, otherwise its pasture's animal list (pasture+0x30 / count +0x38).
// FUN_140002cc0 is DynArray8_EraseByValue.


// ===== ENDGAME B.3.8 CIT — LivestockComponent as a REAL class (LivestockComponent.h) =====
// Before: a 48-slot hand-built LivestockComponent_LiveVtable filled at every
// create (slots 3 Serialize, 6 OnAdd, 7 OnRemove, 9 Update), Inert_ScalarDtor in
// slot 0. After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2).
#include "Object/factory_registration.h"
#include "Component/LivestockComponent.h"


// 0x140123ee0. Component() is the CALL 0x1401188b0; the 0x1401639b0 stamp is
// the compiler's; +0x28 = +0x30 = 0; flags |= 0x0b (0x8b).
LivestockComponent::LivestockComponent() : pasture(nullptr), productionTimer{0} {
    OrFlagsInCtor(0x0b);
}

void LivestockComponent::OnAdded() {
    if (pasture != nullptr) return;
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;   // +0xF0
    if (!town) return;
    LivestockComponent* ibp = this;
    town->livestock
        .SetSizeFill(town->livestock._size + 1, &ibp);
}

void LivestockComponent::OnRemove() {
    // 0x140002cc0 is Collection::Array<InternalPtr<T>>::Remove (Collection.h; B9 RT).
    LivestockComponent* ibp = this;
    if (pasture == nullptr) {
        if (GameUpdateLoop::_singleton == nullptr) return;
        CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;   // +0xF0
        if (town) town->livestock.Remove(ibp);   // 0x140002cc0
        return;
    }
    pasture->animals.Remove(ibp);   // 0x140002cc0
}

// [9] 0x140123b50. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af); the ported body takes the Time by value.
void LivestockComponent::Update(const System::Time* time) {
    const int64_t deltaTime = time ? time->_ticks : 0;

    // Early out: no pasture
    if (pasture == nullptr) {
        return;
    }

    // The description, checked against LivestockDescription
    Description* desc = _description;
    bool isDerived = ::Object::IsDerivedFromNullSafe(desc, LivestockDescription::TypeId());
    const LivestockDescription* config = isDerived ? static_cast<const LivestockDescription*>(desc) : nullptr;

    if (config == nullptr) {
        return;
    }
    if (config->_byproduct == nullptr) {
        return;
    }

    // Accumulate delta time into the production timer
    productionTimer._ticks += deltaTime;

    // Compute the production threshold in ticks:
    //   threshold = Time(config->_productionInterval * timeMultiplier)
    //   Time(float seconds) = (int64_t)((double)seconds / _secondsPerTick)
    float intervalSeconds = config->_productionInterval;
    float thresholdSeconds = intervalSeconds * kRdata_140172234;
    int64_t thresholdTicks = static_cast<int64_t>(
        static_cast<double>(thresholdSeconds) / ::System::Timer::_secondsPerTick);

    // Credit the pasture while the accumulator exceeds the threshold
    if (thresholdTicks < productionTimer._ticks) {
        do {
            pasture->production = pasture->production + 1;
            productionTimer._ticks -= thresholdTicks;
        } while (thresholdTicks < productionTimer._ticks);
    }
}

// create 0x140035480: MemAlloc(0x38, 8), then the ctor.
::Object::Base* LivestockComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LivestockComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) LivestockComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* LivestockComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) LivestockComponent());
}

DEFINE_EXE_TYPE(LivestockComponent, "Component")
