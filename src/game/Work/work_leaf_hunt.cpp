// src/game/Work/work_leaf_hunt.cpp
// Wave (Game-gap / Work-leaf) — faithful 1:1 ports of small exe helper functions
// referenced via ResolveAddr(Module::Exe, 0xADDR) from the Hunt / HarvestOrchard /
// Pasture / ConsumeProduce work classes. Each ResolveAddr returns 0 (silently
// skipped) unless the address is registered. This file provides the clean reimplementations so the
#include "Work/exe_bodies.h"
// standalone build fires real logic.
//
// Each function below is a byte-faithful port of the decompiled + disassembled exe
// helper (offsets verified against Application-x64.exe in Ghidra). Do NOT "improve"
// behaviour — these are 1:1 reproductions.
//
// SAFE subset only: every function here has its ENTIRE exe sub-callee closure already
// registered (or is engine-resolved). The intricate action-chain builders
// (0x1400bbbc0, 0x1400bb7b0, 0x1400bef70, 0x1400f2370) are DEFERRED — see the batch
// report — because their sub-callee closures are incomplete and/or the 11-arg
// XMM2/mixed-stack CreateRawMaterial call is too fidelity-risky to register blind on a
// path autostart does not exercise.
//
// Bodies are unconditional so this compiles as-is in the standalone exe.

#include <new>
#include "object/TypeInfo.h"
#include "Player/exe_bodies.h"                 // B3R: PlayerPath_FindComponent_3ea0
#include "Component/controller_state_table.h"   // B3R (StateMachine_TickReimpl went in B6 rows 392 / 510: Tick takes &Time(0))
#include "system/Value.h"                      // B3R: RtSystem_Time_Ctor
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/HuntComponent.h"                // ENDGAME B.3.9 D5
#include "Component/FruitComponent.h"                // ENDGAME B.3.9 D5
#include "Component/PastureComponent.h"              // ENDGAME B.3.9 D5
#include "Component/RadiusDescription.h"             // ENDGAME B.3.9 D5
#include "Component/ConsumeProduceDescription.h"     // ENDGAME B.3.9 D5
#include "Component/AnimalComponent.h"               // ENDGAME B.3.9 D5
#include "Component/AnimalDescription.h"             // ENDGAME B.3.9 D5
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/ConsumeProduceComponent.h"
#include "Component/ResourceProduceComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/LivestockComponent.h"

// ============================================================
// FindComponent-by-typeId family
// ============================================================
// Standard pattern: scan the entity's component pointer array, call each component's
// GetTypeId (vtable +0x10), and on the first type match run an IsDerivedFrom recheck
// before returning the component through an out InternalBasePtr.
//
// Two layout variants appear:
//   * "+0x30/+0x38" : list ptr at entity+0x30, count at entity+0x38, WITH CheckPtr on
//                     each slot (the canonical FindComponentByTypeId shape).
//   * "+0x28/+0x30" : list ptr at entity+0x28, count at entity+0x30, NO CheckPtr.

// (0x14002a730 / 0x14000a0b0 / 0x140004fe0, the B4 FORWARD forwarders to
// ComponentEntity::FindComponent<HuntComponent / FruitComponent / PastureComponent>, had
// no caller left: deleted, B4 P2 WORK-3.)

// (0x1400063d0 HuntWork_helper_63d0, WorkLeaf_FindComp_63d0 — the RadiusDescription
// sub-description finder — had no caller left: deleted, B4 wave X.)

// ============================================================
// 0x1401193a0  ConsumeProduceAction_helper_93a0
// ============================================================
// RCX is a ConsumeProduceComponent (the address sits among its bodies): a member.
// Finds the ConsumeProduceDescription among the
// sub-descriptions of the entity's description (+0x28/+0x30, NO CheckPtr), then:
//   if (found->+0x48) < 0 -> return true
//   else                  -> return consumeState (+0x68) < (found->+0x48)
// (matches FUN_1401193a0 disasm: tail compares dword[R15+0x68] with dword[found+0x48])
bool ConsumeProduceComponent::CanProduce() const {
    const uint32_t tid = ConsumeProduceDescription::TypeId(); // 0x1401c7388

    const ConsumeProduceDescription* found = nullptr;
    ComponentDescription* desc = _entity->_description;
    if (desc->_subDescCount > 0) {
        Description** list = desc->_subDescs;
        int n = desc->_subDescCount;
        int i = 0;
        for (;;) {
            Description* sub = desc->_subDescs[i];
            uint32_t t = sub->Type();
            if (t == tid) {
                Description* c = list[i];
                if (Object::IsDerivedFromNullSafe(c, tid)) found = static_cast<const ConsumeProduceDescription*>(c);
                break;
            }
            ++i;
            if (i >= n) break;
        }
    }
    int capacity = found->_i48;   // +0x48 (retail reads it unguarded)
    if (capacity < 0) return true;
    return consumeState < capacity;
}

// ============================================================
// 0x140128350  PastureWork_helper_8350
// ============================================================
// RCX is a PastureComponent (a member of it; its header is COMP-d's). For each of
// its livestock, find the AnimalComponent on the animal's entity, and track the
// index whose age (+0x44) is the strict maximum among those that (a) do not have
// bit 20 of their flags (+0x20) set and (b) exceed their AnimalDescription's
// +0x30 threshold. Returns the best index, or -1 if none.
// ABI: int fn(longlong self).
int PastureComponent::FindOldestLivestock() const {
    const uint32_t tidThresh = AnimalDescription::TypeId(); // 0x1401c6fd0

    int bestIndex = -1;
    float bestScore = 0.0f;
    int n = animals._size;
    if (n <= 0) return -1;

    for (int outer = 0; outer < n; ++outer) {
        LivestockComponent* livestock = animals[outer].Get();

        // The AnimalComponent (0x1401c6f98) of the animal's entity.
        AnimalComponent* sub = livestock->_entity->FindComponent<AnimalComponent>();

        // Candidate score = the animal's age. Keep only if strictly greater than current best.
        if (bestScore < sub->age) {
            // Skip if bit 20 of the flags is set.
            if ((sub->_flags >> 0x14 & 1) == 0) {
                float score = sub->age;
                Description* obj = sub->_description;
                AnimalDescription* derived = nullptr;
                if (Object::IsDerivedFromNullSafe(obj, tidThresh)) derived = static_cast<AnimalDescription*>(obj);
                // Keep only if score strictly exceeds the description's threshold.
                float threshold = derived->_f30;
                if (threshold <= score && score != threshold) {
                    bestScore = sub->age;
                    bestIndex = outer;
                }
            }
        }
    }
    return bestIndex;
}

// ============================================================
// 0x14012b310  ConsumeProduceAction_helper_b310
// ============================================================
// RCX is a ResourceProduceComponent (+0x48 produceFlag; the address sits among its
// bodies; its header is COMP-e's, so this stays free). Sets produceFlag = 1, finds the
// entity's StateMachineComponent (0x140003ea0), then if its state (the pending state,
// or the input when none is pending) is not 0x0e (Paused): when the active state is negative,
// tick the machine with a zero Time (StateMachine::Tick 0x1400a9660, useAlt 0, run 0),
// then ask the current controller node for the transition from the active state
// (Controller slot 1 FindTransition, 0x14012b3a4 `CALL [RAX+0x8]`, out int = 0x0e by
// default). On success (>= 0) store the transition (+0x53) and the queried state (+0x52).
void ResourceProduceComponent::RequestProduce() {
    produceFlag = 1;

    StateMachineComponent* machine = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    StateMachine& sm = machine->stateMachine;

    char state = sm.pendingState;
    if (state < 0) state = sm.currentInput;
    if (state == StateByte(BuildingState::Paused)) return;

    int outVal = SmId(BuildingState::Paused); // local_res8[0]

    // If the active state is negative, tick the SM with a zero Time first.
    if (sm.activeState < 0) {
        // System::Time::Time(&t, 0.0f), then StateMachine::Tick(&t, 0, 0): retail
        // passes the Time's ADDRESS (0x14012b380 `LEA RDX,[RSP+0x40]`); with run = 0
        // Tick never reads it.
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, 0.0f);
        sm.Tick(&timeVal, 0, 0);
    }

    // Controller slot 1: int FindTransition(int stateIndex, int* desiredStateId)
    int result = sm.current->FindTransition(static_cast<int>(sm.activeState), &outVal);
    if (result >= 0) {
        sm.transitionState = static_cast<int8_t>(result);
        sm.pendingState = static_cast<int8_t>(outVal);
    }
}
