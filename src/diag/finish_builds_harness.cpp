// src/diag/finish_builds_harness.cpp
//
// BANISHED_FINISH_BUILDS=<interval frames> — finish every structure still under
// construction, the way the stress placer already finishes its own.
//
// Two achievements are blocked by construction rather than by anything they measure:
//
//   Golden Gate wants a bridge at least 50 units long. The bridge tool commits a
//   137-cell span happily (21 segments, CalcCost valid, dock reachable), but stat 25
//   (count of a building type) and stat 29 (bridge length) count only COMPLETED
//   buildings -- a committed foundation reads as have=0 -- and 21 segments' worth of
//   material hauled to a river 70 tiles from town does not finish inside any run
//   length that is practical to sit through.
//
//   Highwaymen wants 2,000 stone-paved road tiles, which is the same problem two
//   orders of magnitude larger.
//
// The mechanism is the one src/game/Game/starttown.cpp already uses for every
// structure it places (its "instant-path FALLBACK"): a StateMachineComponent carries
// its CURRENT state at +0x50 and its PENDING state at +0x52 (0xff = nothing pending),
// and stamping pending = 2 makes the next tick enter the built state through the
// component's own state machine. Nothing here writes a mesh, a stock level or a
// counter -- the state machine does the finishing, and every stat that then moves is
// computed by ported retail code from a genuinely-built structure.
//
// This is a TEST FIXTURE and a deviation, exactly as the placer's stamp is: it stands
// in for builders delivering material. It is not a claim that the build path works --
// the short bridge in an ordinary run finishes on its own and is the evidence for
// that. Opt-in, off by default.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include "Component/BuildComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/BuildingState.h"
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

int s_interval = -2;      // -2 = env unread, -1 = disabled
int s_finished = 0;

void Init() {
    if (s_interval != -2) return;
    const char* e = Opt().finishBuilds;
    if (!e || !e[0] || e[0] == '0') { s_interval = -1; return; }
    const int v = HarnessAtoi(e);
    s_interval = (v > 1) ? v : 600;
    std::fprintf(stderr, "[FinishBuilds] armed: every %d frames, stamp pending state 2 on "
                         "every unbuilt structure (test fixture, stands in for builders "
                         "delivering material)\n", s_interval);
    std::fflush(stderr);
}

Component* FindComp(ComponentEntity* ent, uint32_t tid) {
    if (!ent || !tid) return nullptr;
    const int n = ent->_components._size;                          // +0x38
    Object::InternalPtr<Component>* a = ent->_components._data;        // +0x30
    for (int i = 0; i < n && a; ++i) {
        Component* c = a[i].Get();
        if (!c) continue;
        if (c->Type() == tid) return c;
    }
    return nullptr;
}

}  // namespace

extern "C" void FinishBuildsHarness_Tick(int frameNum) {
    Init();
    if (s_interval <= 0 || frameNum == 0 || (frameNum % s_interval) != 0) return;

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    EntityList* entityList = gw->m_entityList;                                        // +0xe8
    Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;      // +0x08
    const int n = entityList ? entityList->masterArray._size : 0;                     // +0x10
    if (!arr || n <= 0) return;

    const uint32_t tidBuild = BuildComponent::TypeId();
    const uint32_t tidSM    = StateMachineComponent::TypeId();
    if (!tidBuild || !tidSM) return;

    int scanned = 0, stamped = 0, alreadyBuilt = 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        BuildComponent* bc = static_cast<BuildComponent*>(FindComp(ent, tidBuild));   // exact type id
        if (!bc) continue;                       // not a constructable structure
        ++scanned;
        StateMachineComponent* sm = static_cast<StateMachineComponent*>(FindComp(ent, tidSM));
        if (!sm) continue;
        const int cur  = sm->stateMachine.currentInput;    // +0x50
        const int pend = sm->stateMachine.pendingState;    // +0x52
        if (cur == SmId(BuildingState::Built) || pend == SmId(BuildingState::Built)) {
            ++alreadyBuilt;
            continue;
        }

        // Drive the BuildComponent's OWN completion rather than poking the state.
        // Stamping pending = 2 works for a start-town structure the placer spawns
        // already-finished, but a tool-created entity (bridge segment, road tile) is
        // mid-construction and its state machine has no 8->2 edge to take on demand:
        // the stamp was re-applied to the same 25 segments every pass and none of them
        // ever moved. The real sequence is state 1 (awaiting materials) -> state 8
        // (building, progress bc+0x48 ramping toward target) -> state 2, so give each
        // stage what it is waiting for and let State8Update finish it:
        //   state 1  -> request 8 (materials treated as delivered)
        //   state 8  -> stamp progress at the target, which is what State8Update's own
        //               completion test reads before it requests 8->2
        if (cur == SmId(BuildingState::Construct)) {
            // bc+0x48 is progress, bc+0x4c the build multiplier. State8Update completes
            // on progress >= target; a saturated progress satisfies that whatever the
            // target works out to, and the transition is still made by retail code.
            bc->laborProgress = 0x40000000;            // +0x48
            ++stamped;
        } else {
            sm->stateMachine.pendingState = StateByte(BuildingState::Construct);   // +0x52
            ++stamped;
        }
    }
    s_finished += stamped;

    if (stamped > 0 || scanned > 0) {
        std::fprintf(stderr, "[FinishBuilds] f=%d structures=%d built/pending=%d stamped=%d "
                             "(cumulative %d)\n",
                     frameNum, scanned, alreadyBuilt, stamped, s_finished);
        std::fflush(stderr);
    }
}
