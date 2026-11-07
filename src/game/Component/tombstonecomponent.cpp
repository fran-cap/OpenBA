// src/game/Component/tombstonecomponent.cpp
// Clean reimplementation of TombstoneComponent lifecycle methods from Application-x64.exe.
//
// TombstoneComponent (0x30 bytes) — manages tombstone lifetime.
// Fields: +0x28 = Time (int64, countdown timer).
//
// Vtable at 0x14015a980. Three non-trivial overrides:
//   Shutdown  (0x1400578E0) — Serialize with version halving
//   Serialize (0x140057860) — Actually OnCreate: init random lifetime
//   Update    (0x1400577D0) — Timer countdown + entity removal
//

#include "Component/TombstoneComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"   // EntityList::removalList
#include "Component/TombstoneDescription.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Action/exe_bodies.h"   // ActHunt_MarkEntityRemoval_c2ea0
#include "Game/exe_constants.h"

// ============================================================
// Helpers
// ============================================================
// Base::IsDerivedFrom — import slot 0x140152a98, called on the object.
// FUN_1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0 — mark entity for removal.
// FUN_140003110 — dynamic array push (GameColl SetSizeFill).
// .rdata 0x140172234 — seconds-per-tick multiplier (kRdata_140172234).
// The GameWorld cell 0x1401c3dd8 is g_pGameWorldSingleton.


// ============================================================================
// ENDGAME B.3.8 CIT — TombstoneComponent as a REAL class (TombstoneComponent.h).
//
// Before: a 48-slot hand-built TombstoneComponent_LiveVtable filled at every
// create (FillTypedVtable + FinishLiveVtable), Inert_ScalarDtor in slot 0 (only
// ~Internal). After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2). Slot 4 keeps the tree's standalone-robust Startup body,
// which is what the table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/TombstoneComponent.h"

// 0x1400579a0. Component() is the CALL 0x1401188b0; the 0x14015a980 stamp is
// the compiler's; +0x28 = 0; flags |= 0x88 (0x88 with Component's 0x80).
TombstoneComponent::TombstoneComponent() : lifetime{0} {
    OrFlagsInCtor(0x88);
}

// ============================================================
// TombstoneComponent::Persist (0x1400578E0)
// ============================================================
void TombstoneComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 2);

    lifetime.Serialize(ser);

    int version = ser.GetVersion();
    if (version < 2) {
        lifetime._ticks = lifetime._ticks / 2;
    }
}

// ============================================================
// TombstoneComponent::Startup (vtable slot [4], +0x20, original OnCreate 0x140057860)
// ============================================================
// Body-init dispatched at construction by ComponentEntity::StartupComponents (vtable
// +0x20), before any Persist of a loaded entity. As retail 0x140057860: the description
// (+0x18) narrowed to TombstoneDescription (cell 0x1401c4a68, null on a miss) and read
// unguarded, the world RNG read unguarded, and the countdown always written.
void TombstoneComponent::Startup() {
    Description* descRaw = _description;
    TombstoneDescription* desc = ::Object::IsDerivedFromNullSafe(descRaw, TombstoneDescription::TypeId())
        ? static_cast<TombstoneDescription*>(descRaw) : nullptr;

    // Random float in [0,1) from the world RNG.
    float randVal = GameUpdateLoop::_singleton->m_random->RandFloat();

    // lifetime seconds = (rand * range + minimum) * secondsPerTickMul  (DAT_140172234).
    float minTime = desc->_minLifetime;
    float range = desc->_lifetimeRange;
    float tickMul = kRdata_140172234;
    float seconds = (randVal * range + minTime) * tickMul;

    System::Time timeVal;
    RtSystem_Time_Ctor(&timeVal, seconds);
    lifetime = timeVal;
}

// [9] 0x1400577d0 — RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af: `LEA RDX,[RSP+0x40]; CALL [RAX+0x48]`).
// ============================================================
// TombstoneComponent::Update (0x1400577D0)
// ============================================================
void TombstoneComponent::Update(const System::Time* timeV) {
    const System::Time* pDeltaTime = timeV;

    // [LIVE-COMPONENT BRING-UP GUARD] On an inert-spawned standalone component the
    // countdown is zero unless Startup (slot +0x20) ran. If it is still zero but the
    // description is present, run the body-init now so the countdown starts from the
    // real random lifetime — otherwise the `timer < 1` test below fires on tick 1 and
    // marks the tombstone for instant removal.
    if (lifetime._ticks == 0 && _description != nullptr) {
        TombstoneComponent::Startup();
    }

    lifetime._ticks -= pDeltaTime->_ticks;


    if (lifetime._ticks < 1) {
        ComponentEntity* entityPtr = _entity.Get();
        if (GameUpdateLoop::_singleton == nullptr) return;
        EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;

        if (entityPtr != nullptr) {

            // Check bit 10 of the entity flags
            uint32_t entityFlags = entityPtr->_entityFlags;
            if ((entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
                entityPtr->MarkForRemoval();   // 0x1400c2ea0

                // Add to removal queue
                if (entityList) {
                    int count = entityList->removalList._size;
                    entityList->removalList
                        .SetSize(count + 1, _entity.Get());
                }
            }
        }
    }
}

// create 0x14000d280: MemAlloc(0x30, 8), then the ctor.
::Object::Base* TombstoneComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TombstoneComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TombstoneComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* TombstoneComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TombstoneComponent());
}

DEFINE_EXE_TYPE(TombstoneComponent, "Component")
