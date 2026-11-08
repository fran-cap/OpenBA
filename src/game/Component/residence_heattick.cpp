// src/game/Component/residence_heattick.cpp
// ResidenceComponent state-2 (built) per-frame tick — faithful port of
// 0x140129970 (Ghidra label "ResidenceDescription_helper_a350_sub_9970"; it is
// the residence HEAT tick). Registered as the state-2 trans38Handler in the
// ResidenceComponent controller state table (residencecomponent.cpp) and
// dispatched every frame by StateMachine::Tick through the node vtable slot 7
// (see controller_state_table.h Trans38 — the exe slot-7 dispatcher 0x140037a00
// passes RDX=sm and R8=Time* dt through its tail-JMP).
//
// Retail logic (decompile-verified):
//   1. heatRemaining(+0x48, System::Time int64 countdown) -= *dt, every tick.
//   2. [audio] occupied house whose crowd slot [1] is not playing: move the emitter
//              to the model position and play slot [1] (0x140002280/42960/42c60).
//   3. [mesh]  sub-part 6 when edible count > 0 (0x14012a9d0), sub-part 7 when
//              0x14012ab00 count > 0 — via FUN_14012c3f0 on the MeshInstance.
//   4. [abandon] occupancy==0 && storage non-empty && no posted work -> post a
//              ClearStorage work (0x14012ead0/ec60); otherwise a posted work is
//              taken back once the storage is empty (0x14012eb50).
//   5. BURN: if worldTemp < desc->heatThreshold(+0x28) AND heatRemaining < 0
//      AND residentSlotCount(+0x58) > 0: scan the house StorageComponent
//      (finder 0x140006270) slots for a stack whose RawMaterialDescription
//      (tid DWORD_1401c7cb8) has FUEL flag (+0x5c bit 6, 0x40) and available
//      units (slot.count(+0x8) - slot.reserved(+0xC) > 0); then refill
//      heatRemaining = Time(desc->fuelBurnDuration(+0x2C)), subtract 1 unit
//      (0x14012d150), bump the town per-category consumed counter
//      (townMgr(+0xf0)+0x170 row, cat via 0x140088f90, +0xC field), break.
//
// Every branch is wired (branches 2 and 4 since review F-177). Branch 3 is NOT
// a mesh op — 0x14012c3f0 is StatusIcon_Toggle (statusiconcomponent.cpp) and
// 0x140002f90 is the StatusIconComponent finder (Ghidra misnames both as
// MeshInstance ops). It CLEARS icon bit 6 (no-food) while the house has edible
// stock and bit 7 (no-firewood / cold) while it has fuel — the SET side lives
// in the Gather/Gather2 no-target branches (citizenai_gather.cpp). Branches
// 1 + 3 + 5 together close the retail warm/cold loop the building status icon
// displays.


#include "Component/ResidenceComponent.h"
#include "Component/StorageComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/WorkComponent.h"
#include "Component/ModelComponent.h"
#include "Component/AmbientEmitterComponent.h"   // what 0x140002280 returns
#include "Work/ClearStorage.h"
#include "audio/Audio.h"                         // Audio::Scene::SetEmitterVolume
#include "system/MemoryAllocator.h"
#include "Game/exe_constants.h"                  // kRdata_14017214c
#include <new>
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"   // CitizenBehavior::professionResources
#include "Game/WeatherSimulation.h"   // WeatherSimulation::temperature
#include "Component/ResidenceDescription.h"
#include "Component/RawMaterialDescription.h"
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"      // ActRm_StorageSubtract_d150, ActRm_GetResourceCategory_88f90
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays

// ---- exe helpers, by retail address ----------------------------------------
//   0x140006270 FindComponent<StorageComponent> (the REAL storage finder; NOT the
//               misnamed 0x140008090 residence finder)
//   0x14012d150 ActRm_StorageSubtract_d150   StorageComponent slot subtract (storage, stack, n)
//   0x140088f90 ActRm_GetResourceCategory_88f90  townMgr+0x170 counter row index
//   0x14012a9d0 CitizenComp_Storage_GetResourceCount  edible count
//   0x14012ab00 Storage_GetResourceCount2    fuel count
//   0x140002f90 FindComponent<StatusIconComponent>
//   0x14012c3f0 StatusIconComponent::SetFlag  status-icon bit toggle

// The state-2 per-frame handler (the table's thunk in residencecomponent.cpp passes
// the Time* dt of the Trans38 dispatcher tail-call).
char ResidenceComponent::HeatTick(const System::Time* dt) {
    // 1. Heat countdown -= frame dt (unconditional, every tick).
    if (dt) {   // a guard of the tree's
        heatRemaining._ticks -= dt->_ticks;
    }

    // 2. Crowd audio (0x1401299b8-0x140129aa5): an occupied house whose slot [1]
    //    has no live effect moves its emitter to the model position and plays it.
    //    Retail reads the emitter, the model and the audio scene unguarded.
    {
        AmbientEmitterComponent* em = _entity->FindComponent<AmbientEmitterComponent>();   // 0x140002280
        if (static_cast<int>(CountResidents()) > 0 && em->slots._data[1].handle == 0) {      // 0x140129ff0, [+0x38]+0x10
            ModelComponent* model = _entity->FindComponent<ModelComponent>();                // 0x140003f60
            const Math::Vector3 pos = model->transform.translation;                         // +0x158..+0x160
            em->MoveTo(pos);    // 0x140042960
            em->PlaySlot(1);    // 0x140042c60
            AmbientEmitterSlot* slot = &em->slots._data[1];
            if (static_cast<uint32_t>(slot->handle) == 0) {
                slot->volume = 1.0f;   // retail stores 0x3f800000
            } else if (slot->volume != kRdata_14017214c) {
                slot->volume = 1.0f;   // retail stores 0x3f800000
                GameUpdateLoop::_singleton->m_audioScene->SetEmitterVolume(slot->handle, em->position,
                                                                          kRdata_14017214c);   // import 0x140152a08
            }
        }
    }

    // 3. Status-icon upkeep: while the house HAS food, clear icon bit 6
    //    (no-food); while it HAS firewood, clear bit 7 (no-firewood / cold).
    //    Faithful to the decompile: FUN_14012c3f0(*finder2f90(owner), bit, 0).
    {
        if (GetFoodCount() > 0) {
            StatusIconComponent* icon = _entity->FindComponent<StatusIconComponent>();
            if (icon) icon->SetFlag(6, 0);   // 0x14012c3f0
        }
        if (GetFuelCount() > 0) {
            StatusIconComponent* icon = _entity->FindComponent<StatusIconComponent>();
            if (icon) icon->SetFlag(7, 0);   // 0x14012c3f0
        }
    }

    // 4. Abandon (0x140129b27-0x140129c28): an empty house that still holds goods
    //    posts one ClearStorage work so labourers carry them off; once the storage is
    //    empty the posted work is taken back. Retail finds the StorageComponent
    //    (0x140006270) and the WorkComponent (0x140003050) first and reads both
    //    unguarded.
    {
        StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270
        WorkComponent* work = _entity->FindComponent<WorkComponent>();            // 0x140003050
        if (CountResidents() == 0 && storage->QueryCount() != 0 && !work->IsWorkPosted()) {
            ClearStorage* clear = nullptr;
            void* mem = System::MemoryAllocator::MemAlloc(0x68, 8);   // 0x140129b9b
            if (mem != nullptr) {
                ComponentEntity* owner = _entity.Get();
                clear = ::new (mem) ClearStorage(&owner);   // 0x1400e7450
            }
            Object::InternalPtr<Work> workIBP(clear);
            work->PostWork(1, 0, &workIBP);   // 0x14012ec60
        } else if (work->IsWorkPosted() && storage->QueryCount() == 0) {
            work->CancelWork();   // 0x14012eb50
        }
    }

    // 5. Burn branch.
    if (GameUpdateLoop::_singleton == nullptr) return 1;
    WeatherSimulation* wsim = GameUpdateLoop::_singleton->m_weather;
    if (!wsim) return 1;
    float worldTemp = wsim->temperature;

    Description* descObj = _description;
    if (!descObj || !Object::IsDerivedFromNullSafe(descObj, ResidenceDescription::TypeId())) return 1;
    ResidenceDescription* desc = static_cast<ResidenceDescription*>(descObj);
    float heatThreshold = desc->_coldThreshold;


    if (worldTemp >= heatThreshold) return 1;
    if (heatRemaining._ticks >= 0) return 1;
    if (residentSlots._size <= 0) return 1;

    // Find the house StorageComponent.
    StorageComponent* store = _entity->FindComponent<StorageComponent>();   // 0x140006270
    if (!store) return 1;

    int slotCount = store->storageSlots._size;
    StorageComponent::Item* slots = store->storageSlots._data;
    for (int i = 0; i < slotCount; ++i) {
        StorageComponent::Item* slot = &slots[i];
        ComponentDescription* stack = slot->_resource;
        if (!stack) continue;   // guard (retail assumes every slot has a stack)

        // Find the stack's RawMaterialDescription (GetTypeId via vtable+0x10,
        // then IsDerivedFrom — same idiom as fn_StorageComponent_QueryCount).
        RawMaterialDescription* mat = nullptr;
        {
            int compCount = stack->_subDescCount;
            for (int j = 0; j < compCount; ++j) {
                Description* comp = stack->_subDescs[j];
                if (!comp) continue;
                uint32_t tid = comp->Type();   // slot 2
                if (tid == RawMaterialDescription::TypeId()) {
                    if (Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId()))
                        mat = static_cast<RawMaterialDescription*>(comp);
                    break;
                }
            }
        }
        if (!mat) continue;     // guard (retail would deref null +0x5c)

        uint32_t matFlags = mat->_flags;
        int avail = slot->_count - slot->_reserve;
        if (((matFlags >> 6) & 1) != 0 && avail > 0) {
            // Refill the countdown to the description burn duration, then burn 1.
            System::Time t{0};
            RtSystem_Time_Ctor(&t, desc->_heatInterval);
            heatRemaining = t;

            store->SubtractItem(stack, 1);   // 0x14012d150 (ActRm_StorageSubtract_d150's body)
            if (Ext::MapOverlays::g_on) {
                // ext.map_overlays: one fuel burned, at the home (the branch returns: once per burn)
                Ext::MapOverlays::OnConsumedAt(stack, _entity.Get(), 1);
            }

            CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
            if (townMgr) {
                int cat = ResourceCategory(stack);   // 0x140088f90 (ActRm_GetResourceCategory_88f90's body; its first argument is unused)
                if (cat >= 0) {
                    // The counter rows (consumed count at +0xC) — same deref as the eat loop.
                    ProfessionResourceEntry* rows = townMgr->professionResources._data;
                    if (rows)
                        rows[cat].consumedCount += 1;
                }
            }

            // One line per burn (one per house per burn-duration — low volume);
            // uncapped so long soak runs show the full consumption sawtooth.
            // (PERF 2026-09-05: gated -- 800 houses in winter made this unbuffered stderr
            // line 1.7% of the EntityList phase in the perf build.)
            return 1;
        }
    }
    // No fuel burned: the house is COLD with no fuel available. heatRemaining stays
    // negative, the warm-check 0x14012ac30 reads cold, and Gather2's no-target branch
    // raises status-icon bit 7.
    return 1;
}
