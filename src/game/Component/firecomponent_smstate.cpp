// src/game/Component/firecomponent_smstate.cpp
// The two "force the building's StateMachineComponent into state N" helpers that
// FireComponent::Ignite / ::Extinguish drive, plus the demolition gate they check.
//
//   0x14011e690  FireComponent_IsBeingDemolished  — abort gate for Ignite
//   0x14011e7b0  FireComponent_helper_e7b0        — begin burning  (SM state 3, Demolish)
//   0x14011ea10  FireComponent_helper_ea10        — stop burning   (SM state 7, Rubble)
//
// ⚠ SHARED WITH THE TORNADO LANE. The tornado building-damage arm reaches
// 0x14011e7b0 / 0x14011ea10 as well. They are ported ONCE,
// here, and registered once (standalone/reg_fire.cpp) so both lanes dispatch the
// same body — the "one retail fn ported twice, each copy faithful in a different
// half" failure mode this project has already been bitten by.
//
// ── What the two helpers actually are ────────────────────────────────────────
// Ghidra renders both as ~90 lines of nested loops. They are NOT bespoke: the
// whole tail is StateMachine::Tick (0x1400a9660) INLINED. Verified field by field
// against our own port (StateMachine::Tick, statemachinecomponent.cpp)
// with `sm = SMC + 0x48`:
//     Tick sm[0]        == *(void**)(SMC+0x48)   active state node
//     Tick (char)sm[1]  == *(int8*)(SMC+0x50)    current state
//     Tick *(sm+9)      == *(int8*)(SMC+0x51)    active index
//     Tick *(sm+10)     == *(int8*)(SMC+0x52)    pending state (0xff = none)
//     Tick *(sm+0xb)    == *(int8*)(SMC+0x53)    transition index
//     Tick param3       == 0   (the slot +0x30 Transition arm is the one taken)
//     Tick param4       == 1   (bVar2 starts true and is cleared after one pass)
// Entry test, both inner loop exits and the outer loop condition all match
// exactly. So the tail is precisely `StateMachine::Tick(SMC+0x48, Time(0), 0, 1)`.
//
// The head is the state request:
//     out = N
//     if ((int8)SMC[0x51] < 0) StateMachine::Tick(SMC+0x48, Time(0), 0, 0)
//     r = node->vtable[1](node, (int)(int8)SMC[0x51], &out)   // find transition to N
//     if (r >= 0) { SMC[0x53] = (int8)r; SMC[0x52] = (int8)out; }
//
// Decompiled READ-ONLY from Application-x64.exe.

#include "system/Value.h"
#include <cstdint>
#include "Component/firecomponent_funcs.h"
#include "Component/controller_state_table.h"
#include "Player/exe_bodies.h"
#include "Dialog/dialog_exe_helpers.h"
#include "Component/DestroyComponent.h"
#include "Component/BuildComponent.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"

namespace {

// Called directly: FUN_140003ea0 finds the entity's StateMachineComponent
// (PlayerPath_FindComponent_3ea0); FUN_14000d6c0 finds the entity's BuildComponent
// (Dialog_FindComponent_d6c0 — DAT_1401c7008 is the BuildComponent id);
// StateMachine::Tick 0x1400a9660 is StateMachine::Tick, handed &Time(0.0f).

// ---------------------------------------------------------------------------
// The shared body of e7b0 and ea10: request state `targetState` on the entity's
// StateMachineComponent and pump the machine until it settles.
// `owner` is the component's +0x10 entity. The StateMachine is the component's
// +0x48 member (StateMachineComponent::stateMachine).
// ---------------------------------------------------------------------------
void ForceSMState(ComponentEntity* owner, int targetState, const char* /*who*/) {
    // Row 922 (B7 RES): retail 0x14011e82c-0x14011e871 reads the found component
    // ([RBX+0x51]) and the active node ([[RBX+0x48]] slot +0x8) with no test; the tree's
    // null-component and empty-chain returns (the pre-B "inert controller" reason) are gone.
    StateMachineComponent* L =
        owner->FindComponent<StateMachineComponent>();   // 0x140003ea0

    StateMachine* sm = &L->stateMachine;          // +0x48
    ControllerComponent::Controller** node = &sm->current;  // sm[0] = the active state node

    int32_t out = targetState;

    // Settle a half-finished transition before asking for a new state.
    if (sm->activeState < 0) {   // +0x51
        ::System::Time t0 = {0};
        RtSystem_Time_Ctor(&t0, 0.0f);
        sm->Tick(&t0, 0, 0);   // 0x14011e83f-0x14011e85c: LEA RDX,[RSP+0x20]; R8D = R9D = 0
    }

    // node slot 1 (FindTransition: the table has no dtor slot) -> transition index (or < 0)
    ControllerComponent::Controller* n = *node;
    int r = n->FindTransition(static_cast<int>(sm->activeState), &out);
    if (r >= 0) {
        sm->transitionState = static_cast<int8_t>(r);   // +0x53
        sm->pendingState = static_cast<int8_t>(out);    // +0x52
    }


    // The inlined tail (0x14011e883-0x14011e9c7) == StateMachine::Tick(sm, &Time(0), useAlt=0,
    // doTransition=1): Time(0.0f) at [RSP+0x50], handed to node slot +0x30 in R8.
    ::System::Time tZero = {0};
    RtSystem_Time_Ctor(&tZero, 0.0f);
    sm->Tick(&tZero, 0, 1);
}

}  // namespace

// ============================================================
// FUN_14011e690 — "is this building already being demolished / reclaimed?"
// ============================================================
// param_1 = the DestroyComponent. Finds the entity's StateMachineComponent and
// returns true when the effective state (pending +0x52 if >= 0, else current
// +0x50) is 6 (Collapse) or 7 (Rubble). Ignite aborts on true: a building on its way down does not
// catch fire.
bool DestroyComponent::IsBeingDemolished() {
    StateMachineComponent* L =
        _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    // 0x14011e6bb-0x14011e6c0: [RCX+0x52] read with no test (B7 RES, with row 922).
    int8_t s = L->stateMachine.pendingState;                  // +0x52
    if (s < 0) s = L->stateMachine.currentInput;             // +0x50
    if (s == StateByte(BuildingState::Collapse)) return true;
    s = L->stateMachine.pendingState;
    if (s < 0) s = L->stateMachine.currentInput;
    return s == StateByte(BuildingState::Rubble);
}

// ============================================================
// FUN_14011e7b0 — begin burning.
// ============================================================
// param_1 = the DestroyComponent, param_2 = the (already halved) burn Time.
// Returns false — without touching anything — when the building is not finished
// (its BuildComponent's +0x48 progress word is still 0). Otherwise stamps the
// DestroyComponent's burning flag (+0x50 = 1) and burn duration (+0x48), then
// drives the entity's state machine to state 3 (Demolish).
bool DestroyComponent::BeginBurning(const System::Time* time) {
    // Unfinished-construction gate.
    {
        BuildComponent* build = _entity->FindComponent<BuildComponent>();   // 0x14000d6c0
        if (build != nullptr) {
            if (build->laborProgress == 0) {
                return false;
            }
        }
    }

    burning = true;
    burnTime._ticks = time->_ticks;   // 0x14011e802: MOV RAX,[RSI], no test (B7 RES)

    ForceSMState(_entity.Get(), SmId(BuildingState::Demolish), "begin");
    return true;
}

// ============================================================
// FUN_14011ea10 — stop burning.
// ============================================================
// param_1 = the DestroyComponent. Drives the entity's state machine to state 7 (Rubble).
// Note it does NOT clear +0x50 / +0x48; the state-7 handler owns that.
void DestroyComponent::StopBurning() {
    ForceSMState(_entity.Get(), SmId(BuildingState::Rubble), "stop");
}
