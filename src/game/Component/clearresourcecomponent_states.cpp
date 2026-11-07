// src/game/Component/clearresourcecomponent_states.cpp
// Phase 3 / Track A — the clear-job's SELF-DESTRUCT state, i.e. what finally
// clears the on-ground clearing ICON once the resource has been harvested.
//
// WHY THIS FILE EXISTS (the icon bug). The RemovalTool spawns one clear-job
// entity per marked tile, cloned from Template/Clear.rsc. That template declares
// THREE ControllerComponents sharing ONE StateMachineComponent:
//
//   _descriptions [ "map","zone","createdrag","decal","work",
//                   "statemachine","cleararea","destroy","clearresource" ]
//
// Each Description class registers its OWN static state table, and a transition
// to state N fires the state-N handlers of EVERY controller on the entity. We had
// only ever ported the `cleararea` table (clearareacomponent_states.cpp), so the
// harvest ran to completion and then nothing ever removed the job — the `decal`
// component's terrain decal (Terrain/TiledDecals/ClearAreaMaterial.rsc = the icon
// the player sees) stayed on the ground forever.
//
// The icon is NOT cleared by an event or by the model-finalize path:
//   * ClearArea state 1's helper_7820 -> 0x140118270 is gated on the description's
//     _flattenGround, which Clear.rsc sets FALSE, so it correctly no-ops. Its
//     deferred `e0e0` terrain-regrow call does not touch the decal either.
//   * DecalComponent's +0x60 event handler (0x14011b760) free-then-RE-ADDS the
//     decal for both event 1 and event 5 — it is a refresh, never a removal.
// Retail simply DELETES the whole clear-job entity, from ClearResourceDescription's
// state-2 enter handler; releasing the decal is a side effect of that removal.
//
// ClearResourceDescription's entire state table is one state (verified in Ghidra):
//   ClearResourceDescription_Setup (0x14014e9f0)
//     -> helper_17f0 (0x1400317f0): registers state 2, enter = sub_8560 (the
//        handler ptr lands at table-entry +0x08 = the ENTER slot)
//     -> helper_1910 (0x140031910): adds the single transition -> {3}
//   (statics: base DAT_1401d5bc0, count DAT_1401d6b40, 0x10 entries x 0xf8 stride)
//
// ClearArea already requests 1 -> 2 from its own state-1 enter handler, so entering
// state 2 fires BOTH ClearArea's helper_77f0 (broadcast event 1) and this handler
// (destroy). `cleararea` precedes `clearresource` in _descriptions, so the
// broadcast runs first — that ordering is preserved by component array order and
// must not be disturbed.
//
// 🔑 WHY THE EARLIER FORCED-REMOVAL ATTEMPT CRASHED. A previous session force-called
// MarkForRemoval from ClearArea's state 2 and hit an ACCESS_VIOLATION READ 0x30 in
// the EntityList removal drain, which looked like "you cannot delete the clear-job
// while its SM is live". That conclusion was WRONG: retail destroys the entity from
// exactly this state, mid-tick, on the same entity. Removal is only a MARK
// (0x1400c2ea0 dispatches each component's +0x38 detach, then sets bit 0x400 at
// entity+0x40); the actual free happens later at EntityList::Update Step 5. What the
// earlier attempt lacked is the 0x400 idempotence guard below — Clear.rsc puts THREE
// mark-and-push-capable controllers on one entity (`clearresource` here at state 2
// and `destroy` at state 5, which carries the identical guard+push block), so
// without the guard the same entity is pushed onto the removal list twice and the
// drain walks a stale/duplicate slot (entity+0x30 is the components array — hence
// the READ 0x30 fault). The guard is what makes multiple controllers safe.
//
// Opt-in via BANISHED_LIVE_CLEARCTRL — the same gate that brings up the ClearArea
// controller, deliberately NOT a separate var. The two are one feature (retail
// always has both), and bringing this one up ALONE would be actively unsafe:
// StateMachineComponent_Startup (0x14012b6a0) takes its default state from the
// FIRST ControllerComponent only (`if (!bVar1)` guard), and our standalone linker
// treats "first with a non-null CreateState" as first. With `cleararea` inert this
// component would be first and its GetDefaultState of 2 (0x1400026c0: MOV EAX,2;
// RET) would start the SM in state 2 => every clear-job would delete itself on
// creation. With `cleararea` live, its 0 wins and this is unreachable except via
// the real 1 -> 2 harvest transition.
//
// Faithful offset-by-offset port.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/EntityList.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "Component/controller_state_table.h"
#include "Component/ClearResourceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Action/exe_bodies.h"

// ============================================================
// Helpers: 0x1400c2ea0 is ActHunt_MarkEntityRemoval_c2ea0, called directly.
// ============================================================
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call site below.

// ============================================================
// ClearResourceDescription_helper_17f0_sub_8560 (0x140118560) — state-2 ENTER.
// Deletes the finished clear-job entity; the DecalComponent's terrain decal (the
// clearing icon) is released by the +0x38 detach that MarkForRemoval dispatches
// (DecalComponent +0x38 = 0x14011b510 -> ReleaseDecal 0x1400b0d00, resetting the
// decal handle at comp+0x2c to -1).
//
// Original:
//   entity = *(param_1 + 0x10); list = *(gw + 0xe8);
//   if (entity) { CheckPtr; if (((*(uint*)(entity+0x40) >> 10) & 1) == 0) {
//       CheckPtr; FUN_1400c2ea0(entity);
//       FUN_140003110(list+0xd0, *(int*)(list+0xd8)+1, &entity); } }
//   return 1;
// param_1 is the COMPONENT (the enter dispatcher tail-calls with RCX = node+0x18 =
// the owning component); comp+0x10 is the owner-entity IBP.
// ============================================================
char ClearResourceComponent::State2Enter() {
    ComponentEntity* entity = _entity.Get();
    if (entity == nullptr) return 1;
    if (GameUpdateLoop::_singleton == nullptr) return 1;

    // GameUpdateLoop::m_entityList (+0xe8): the removal array is its removalList (+0xd0).
    EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;
    if (entityList == nullptr) return 1;

    // Idempotence guard (bit 0x400 of the entity flags) — see the header note: this is
    // what keeps the sibling `destroy` controller from double-pushing the entity.
    if ((entity->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) != 0) {
        return 1;
    }

    entity->MarkForRemoval();
    entityList->removalList
        .SetSize(entityList->removalList._size + 1, entity);

    return 1;
}

// The state table's handler adapter: the controller hands it its owner (node+0x18).
static char ClearResource_State2Enter(ClearResourceComponent* c, StateMachine* /*sm*/) { return c->State2Enter(); }   // 0x140118560

// ============================================================
// Dispatcher + state table for the ClearResourceComponent controller.
// ============================================================
// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<ClearResourceCtrlTag>, ClearResourceComponent.h).
#include "Component/ClearResourceComponent.h"
using ClearResourceDispatch = ::ControllerLive::Dispatcher<ClearResourceCtrlTag>;

extern "C" void ClearResourceCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = ClearResourceDispatch::table;
    if (t.count != 0) return;   // idempotent
    // The whole class: state 2, enter = sub_8560 (destroy the job), -> {3}.
    TableRegisterState(t, BuildingState::Built, &ClearResource_State2Enter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
}
