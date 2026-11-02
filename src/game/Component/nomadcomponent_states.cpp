// src/game/Component/nomadcomponent_states.cpp
// The NOMAD state machine — one per member of an arriving nomad group. Ported
// 2026-09-05 alongside immigrationcomponent_states.cpp (Immigrants achievement).
//
// NomadComponent (0x78, a ControllerComponent on the nomad template referenced by
// ImmigrationDescription+0x20). Its behaviour is the state table registered once by
// NomadDescription_Setup -> helper_fe70 (0x14004fe70):
//
//   state 0  WALK IN     enter faf0 : MoveTo(meeting point +0x3c)
//                        tick  f9c0 : once the action stack is idle (arrived): if the
//                                     hall reference (+0x28) is gone, destroy self;
//                                     else post event 6 (ARRIVED) to the hall's SM, -> 1
//                        transitions -> {1}          event 1 f4a0 : LEAVE -> 2
//   state 1  LOITER      enter LAB_f9b0 : `mov al,1; ret`
//                        tick  f600 : when idle, count the wander timer (+0x48) down;
//                                     expired -> re-arm 30 + rand*15 s and MoveTo a
//                                     random walkable cell within 8 of the meeting point
//                        transitions -> {2}
//                        event 0 f420 : DESTROY (converted to a citizen)
//                        event 1 f4a0 : LEAVE -> 2
//   state 2  WALK OUT    enter f5e0 : MoveTo(spawn point +0x30)
//                        tick  f530 : once idle (back at the edge), destroy self
//
// "Idle" is CharacterComponent+0x60 == -1: the action-stack top index that
// Pf_ActStackPush_54e0 / WorkBase_TaskListCleanup_c5400 maintain — the walk gate.
//
// Component layout (retail ctor 0x14004fdb0): NomadComponent.h.
//
// Opt-in with the town hall side: BANISHED_LIVE_IMMIGRATION. Faithful port of
// 0x14004f420 .. 0x14004fb60 and 0x1400c49d0.
//
// LEFT (B.4 COMP-d): GameUpdateLoop m_game / m_entityList are void* (W0 header); the
// Map's cell record has no type (GAME-b); StateMachineComponent::controllerSelf is
// void* (COMP-e); the action-stack bodies (WORK / ACT) take raw receivers.

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Component/CharacterComponent.h"
#include "Component/ControllerComponent.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Action/exe_bodies.h"   // Pf_ActStackPush_54e0, Pf_MoveToCtor_9170, ActHunt_MarkEntityRemoval_c2ea0
#include "Game/exe_bodies.h"     // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"  // kRdata_1401721cc
#include "Work/exe_bodies.h"     // WorkBase_TaskListCleanup_c5400
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/NomadComponent.h"
#include "Action/MoveTo.h"   // 0x1400c9170, by name
#include "Component/ModelComponent.h"
#include "Component/ImmigrationComponent.h"   // fix.nomad_arrival: the hall's group
#include "ext/registry.h"                     // Ext::Fix().nomad_arrival
#include "ext/nomad_arrival/nomad_arrival_rule.h"

namespace {

constexpr float kWanderRandRange = 15.0f;   // DAT_1401721bc
constexpr float kWanderJitter    = 0.33f;   // DAT_14017210c
constexpr float kHalfCell        = 0.5f;    // DAT_140172114
constexpr float kMoveSpeed       = -1.0f;   // DAT_140172280 (MoveTo speed arg: default)

// Helpers, called by name (B3R):
//   0x140003ea0 FindComponent<StateMachineComponent>
//   0x1400c5400 WorkBase_TaskListCleanup_c5400   0x1400c54e0 Pf_ActStackPush_54e0
//   0x1400c9170 Pf_MoveToCtor_9170               0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0
//               (ComponentEntity::MarkForRemoval)
//   0x14009f3e0 Reimpl_MapGetTerrainPos

// The .rdata float constants (Game/exe_constants.h) that are stored as bit patterns.
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

inline GameUpdateLoop* GW() { return GameUpdateLoop::_singleton; }
inline EntityList* Entities(GameUpdateLoop* gw) { return gw->m_entityList; }   // +0xE8

// `character->currentAction + 1 < 1` — the action stack is empty. The character is the
// entity's CharacterComponent (Entity_FindComponent_Family 0x1400012d0, DAT_1401c7078).
bool Idle(NomadComponent* nomad) {
    ComponentEntity* entity = nomad->_entity.Get();
    CharacterComponent* chr = entity ? entity->FindComponent<CharacterComponent>() : nullptr;
    if (!chr) return false;
    return chr->actionStack.current + 1 < 1;
}

// NOT RETAIL: fix.nomad_arrival (docs/retail_bugs.md §22). A nomad that gives up leaves its
// hall's group, so the hall does not wait for its ARRIVED; when the members left have all
// arrived the hall moves on to the answer (0x11), and when nobody is left it goes back to idle
// (2, the transition the fix appends to state 0x10). Then the nomad forgets the hall: idle at
// the map edge, retail's own `hall == nullptr` arm of TickWalkIn destroys it.
void NomadLeaveHall(NomadComponent* nomad) {
    ComponentEntity* hallEntity = nomad->hall.Get();
    ComponentEntity* self = nomad->_entity.Get();
    ImmigrationComponent* imm = hallEntity->FindComponent<ImmigrationComponent>();
    if (imm) {
        ImmigrationComponent::GroupList& group = imm->group;
        const int n = group._size;
        int kept = 0;
        for (int i = 0; i < n; ++i) {
            if (group._data[i].Get() == self) continue;
            if (kept != i) group._data[kept] = group._data[i];
            ++kept;
        }
        for (int i = kept; i < n; ++i) group._data[i] = nullptr;
        group._size = kept;
        StateMachineComponent* smc = hallEntity->FindComponent<StateMachineComponent>();
        switch (Ext::NomadArrival::AfterGiveUp(imm->arrivedCount, kept)) {
        case Ext::NomadArrival::HallStep::AllArrived:
            ::ControllerLive::RequestStateTransition(&smc->stateMachine, 0x11);
            break;
        case Ext::NomadArrival::HallStep::Empty:
            ::ControllerLive::RequestStateTransition(&smc->stateMachine, 2);
            break;
        case Ext::NomadArrival::HallStep::None:
            break;
        }
    }
    nomad->hall = nullptr;
}

// NOT RETAIL: fix.nomad_arrival. True when the idle nomad has arrived (retail's arm runs).
// Otherwise it has re-issued the walk to the meeting point (the first time) or given up and
// started back to its spawn point. The retry mark is a byte of the class's padding (+0x54,
// never saved: after a load the nomad gets its retry again).
bool NomadHasArrived(NomadComponent* nomad) {
    ModelComponent* model = nomad->_entity->FindComponent<ModelComponent>();
    const Math::Vector3& pos = model->transform.translation;
    uint8_t& retried = nomad->_pad54[0];
    const Ext::NomadArrival::Step step = Ext::NomadArrival::Decide(
        true, pos.x - nomad->meetPoint.x, pos.y - nomad->meetPoint.y, retried != 0);
    switch (step) {
    case Ext::NomadArrival::Step::Arrived:
        retried = 0;
        return true;
    case Ext::NomadArrival::Step::Retry:
        retried = 1;
        nomad->MoveTo(&nomad->meetPoint);
        return false;
    case Ext::NomadArrival::Step::GiveUp:
        retried = 0;
        NomadLeaveHall(nomad);
        nomad->MoveTo(&nomad->spawnPoint);
        return false;
    }
    return true;
}

}  // namespace

// ============================================================
// EntityList::QueueDestroy — port of 0x1400c49d0: MarkForRemoval + queue on the entity
// list's removal array (+0xd0/+0xd8), unless already marked (flag 0x400).
// ============================================================
void EntityList::QueueDestroy(ComponentEntity* const* entity) {
    if (*entity == nullptr) return;
    if (((*entity)->_entityFlags >> 10) & 1) return;
    (*entity)->MarkForRemoval();   // 0x1400c2ea0
    removalList.SetSize(removalList._size + 1, *entity);
}

// ============================================================
// NomadComponent::MoveTo — port of 0x14004fb60: clear the character's action stack
// and push a MoveTo(dest) (Slow, mask 0xe1f, default speed).
// ============================================================
void NomadComponent::MoveTo(Math::Vector3* dest) {
    ComponentEntity* entity = _entity.Get();
    CharacterComponent* chr = entity ? entity->FindComponent<CharacterComponent>() : nullptr;
    if (!chr) return;
    chr->actionStack.Clear();   // 0x1400c5400
    void* mem = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* act = nullptr;
    if (mem) {
        CharacterComponent* ownerIBP = chr;
        act = ::new (mem) ::MoveTo(&ownerIBP, 1 /*Slow*/, 0, &dest->x, 0xe1f, 0, kMoveSpeed);   // 0x1400c9170
    }
    ::Object::InternalPtr<Action> actIBP = act;
    ActionStackPush(&chr->actionStack, &actIBP);   // 0x1400c54e0
}

// ============================================================
// State handlers (the controller hands them this component and its StateMachine)
// ============================================================

// state 0 enter 0x14004faf0: walk to the meeting point
char NomadComponent::EnterWalkIn() {
    MoveTo(&meetPoint);   // 0x14004fb60
    return 1;
}
// state 0 tick 0x14004f9c0: arrived -> tell the hall (event 6) -> state 1
char NomadComponent::TickWalkIn(StateMachine* sm) {
    if (!Idle(this)) return 1;
    GameUpdateLoop* gw = GW();
    if (hall == nullptr) {
        ComponentEntity* ibp = _entity.Get();
        if (gw) Entities(gw)->QueueDestroy(&ibp);   // 0x1400c49d0
        return 1;
    }
    // FIX nomad_arrival (docs/retail_bugs.md §22): retail takes "idle" as "arrived"; with the
    // row on, a nomad idle away from its meeting point walks again once, then gives up.
    if (Ext::Fix().nomad_arrival && !NomadHasArrived(this)) return 1;
    StateMachineComponent* smc = hall->FindComponent<StateMachineComponent>();   // 0x140003ea0
    if (smc) {
        ControllerComponent::Controller* node = smc->stateMachine.current;
        if (node) {
            // node = the controller member the StateMachineComponent linked (D6); slot 2.
            node->PostEvent(
                &smc->stateMachine, smc->stateMachine.activeState, 6);
        }
    }
    ::ControllerLive::RequestStateTransition(sm, 1);
    return 1;
}
// event 1 0x14004f4a0: LEAVE -> state 2, forget the hall
char NomadComponent::OnLeave(StateMachine* sm) {
    ::ControllerLive::RequestStateTransition(sm, 2);
    hall = nullptr;   // a null InternalBasePtr
    return 1;
}
// state 1 tick 0x14004f600: loiter around the meeting point
char NomadComponent::TickLoiter(const System::Time* time) {
    if (!Idle(this)) return 1;
    wanderTimer._ticks -= time ? time->_ticks : 0;
    if (wanderTimer._ticks >= 0) return 1;
    GameUpdateLoop* gw = GW();
    if (!gw) return 1;
    System::Random* rng = gw->m_random;
    float secs = rng->RandFloatTol() * kWanderRandRange +
                 RdataF(kRdata_1401721cc);   // DAT_1401721cc = 30.0f, wander base
    System::Time t;
    t._ticks = 0;
    RtSystem_Time_Ctor(&t, secs);
    wanderTimer = t;

    Map* grid = gw->m_game;   // +0xF8
    const int W = grid->size;
    int cx = static_cast<int>(meetPoint.x + static_cast<float>(W >> 1));
    int cy = static_cast<int>(meetPoint.y + static_cast<float>(W >> 1));
    if (cx < 0) cx = 0;
    if (cx >= W) cx = W - 1;
    if (cy < 0) cy = 0;
    if (cy >= W) cy = W - 1;
    int centre = W * cy + cx;
    int x0 = centre % W - 8;
    int y0 = centre / W - 8;
    const MapCell* tiles = grid->cells.GetData();   // Map +0x80
    // Retail grows a DynArray<int> of candidates; a fixed 16x16 buffer holds them all.
    int cand[256];
    int nc = 0;
    for (int ry = 0; ry < 16; ++ry) {
        int yy = y0 + ry;
        for (int rx = 0; rx < 16; ++rx) {
            int xx = x0 + rx;
            if (xx < 0 || xx >= W || yy < 0 || yy >= W) continue;
            int c = yy * W + xx;
            if (tiles[static_cast<uintptr_t>(c)].terrainType < 3) cand[nc++] = c;   // +0x29
        }
    }
    if (nc != 0) {
        uint32_t pick = rng->Rand(static_cast<uint32_t>(nc));
        float jx = rng->RandFloatTol() * kWanderJitter;   // fVar18
        float jy = rng->RandFloatTol() * kWanderJitter;   // fVar19
        int c = cand[pick];
        float half = static_cast<float>(W >> 1) - kHalfCell;
        float cellPos[3] = { static_cast<float>(c % W) - half, static_cast<float>(c / W) - half, 0.0f };
        float pos[3] = {0, 0, 0};
        grid->GetTerrainPos(pos, cellPos, '\0', nullptr);
        Math::Vector3 dest(jy + pos[0], jx + pos[1], pos[2] + 0.0f);
        MoveTo(&dest);   // 0x14004fb60
    }
    return 1;
}
// event 0 0x14004f420: DESTROY (the nomad became a citizen)
char NomadComponent::OnDestroy() {
    GameUpdateLoop* gw = GW();
    ComponentEntity* ibp = _entity.Get();
    if (gw && ibp) Entities(gw)->QueueDestroy(&ibp);   // 0x1400c49d0
    return 1;
}
// state 2 enter 0x14004f5e0: walk back to the spawn point
char NomadComponent::EnterWalkOut() {
    MoveTo(&spawnPoint);   // 0x14004fb60
    return 1;
}
// state 2 tick 0x14004f530: back at the edge -> destroy
char NomadComponent::TickWalkOut() {
    if (!Idle(this)) return 1;
    GameUpdateLoop* gw = GW();
    ComponentEntity* ibp = _entity.Get();
    if (gw && ibp) Entities(gw)->QueueDestroy(&ibp);   // 0x1400c49d0
    return 1;
}

// ============================================================
// Dispatcher + table (replicates NomadDescription_helper_fe70, 0x14004fe70)
// ============================================================
// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<NomadCtrlTag>, NomadComponent.h).
using NomadDispatch = ::ControllerLive::Dispatcher<NomadCtrlTag>;

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm[, time]): [node+0x18] is the controller's back-pointer, this
// component, so each entry is a one-line adapter that calls the member.
namespace {
char NomadEnterWalkIn(NomadComponent* c, StateMachine*)                { return c->EnterWalkIn(); }     // 0x14004faf0
char NomadTickWalkIn(NomadComponent* c, StateMachine* sm, const System::Time*)       { return c->TickWalkIn(sm); }    // 0x14004f9c0
char NomadOnLeave(NomadComponent* c, StateMachine* sm)                 { return c->OnLeave(sm); }       // 0x14004f4a0
char NomadEnterLoiter(NomadComponent*, StateMachine*)                  { return 1; }                    // LAB_14004f9b0 `mov al,1; ret`
char NomadTickLoiter(NomadComponent* c, StateMachine*, const System::Time* time)     { return c->TickLoiter(time); }  // 0x14004f600
char NomadOnDestroy(NomadComponent* c, StateMachine*)                  { return c->OnDestroy(); }       // 0x14004f420
char NomadEnterWalkOut(NomadComponent* c, StateMachine*)               { return c->EnterWalkOut(); }    // 0x14004f5e0
char NomadTickWalkOut(NomadComponent* c, StateMachine*, const System::Time*)         { return c->TickWalkOut(); }     // 0x14004f530
}  // namespace

extern "C" void NomadCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = NomadDispatch::table;
    if (t.count != 0) return;
    TableRegisterState(t, 0, &NomadEnterWalkIn, nullptr,
                       &NomadTickWalkIn, nullptr);
    TableAddTransition(t, 1, nullptr);
    TableAddEvent(t, 1, &NomadOnLeave);

    TableRegisterState(t, 1, &NomadEnterLoiter, nullptr,
                       &NomadTickLoiter, nullptr);
    TableAddTransition(t, 2, nullptr);
    TableAddEvent(t, 0, &NomadOnDestroy);
    TableAddEvent(t, 1, &NomadOnLeave);

    TableRegisterState(t, 2, &NomadEnterWalkOut, nullptr,
                       &NomadTickWalkOut, nullptr);
}
