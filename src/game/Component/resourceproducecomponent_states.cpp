// src/game/Component/resourceproducecomponent_states.cpp
// The SHARED ResourceProduceComponent controller state machine.
//
// Unlike Orchard/Pasture/CropField — which each own a private state table — every
// ResourceProduceComponent subclass INHERITS the base controller vtable
// (0x1401643a0) and therefore shares ONE state table (retail DAT_1401db920,
// count DAT_1401dc8a0). So this single file drives the Fishing Dock, the Hunting
// Cabin and the Gatherer's Hut (HerbalistComponent), plus the other RPC subclasses
// (Forester, Market, ConsumeProduce) once those are promoted off inert.
//
// Populated in retail by ResourceProduceDescription_helper_b230 (0x14012b230),
// reached from ResourceProduceDescription_Setup (0x14014f970). The table has
// exactly three states:
//
//   state 2  (idle)      enter 0x14012b1b0             — pick 13 or 14 from +0x48
//                        ->13 action 0x14012af90       — stamp the work positions
//                        ->14, ->3 (no action)
//   state 13 (producing) enter 0x14012ae60             — accept workers + post work
//                        trans38 0x14012b190           — per-frame produce tick
//                        ->3  action 0x14012af70       — stop producing
//                        event 3 0x14012adf0           — panel pause -> 14
//   state 14 (paused)    enter 0x14012ad20             — stop accepting + clear work
//                        ->13, ->3 (no action)
//                        event 3 0x14012acb0           — panel resume -> 13
//
// State 3 is the shared teardown id: a transition target only, with no body — so it
// is deliberately NOT registered here, exactly as retail leaves it.
//
// The per-class behaviour hangs off the COMPONENT vtable, not off this table:
//   slot 18 (+0x90) CreateWork      — _purecall in the base; every subclass overrides
//   slot 19 (+0x98) OnEnterProducing— base no-op 0x140006b10
//   slot 20 (+0xa0) OnExitProducing — base no-op 0x140006b10
//   slot 21 (+0xa8) ProduceTick     — base no-op 0x140006b10
// See docs/history/notes/FOOD_PRODUCTION_WAVE_BRIEF.md for the byte-level derivation.

#include "Component/controller_state_table.h"
#include "Component/ResourceProduceComponent.h"
#include "Object/factory_registration.h"
#include "system/Value.h"
#include "io/Serializer.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Object/exe_type.h"   // DEFINE_EXE_ABSTRACT_TYPE
#include "Component/exe_bodies.h"   // AttendanceComponent_GetJobHostComponent, CitizenAI_WorkScheduler_Post,
                                    // WorkPlaceComponent_SetAccepting, WorkComponent_CheckWorkSlot,
                                    // WorkComponent_RegisterWorkSlot
#include "Component/StateMachineComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/InteractComponent.h"   // NodeCount / NodePosition (0x140123070 / 0x140122e20)
#include "Game/Map.h"
#include "Game/PathfinderThread.h"   // SetCellTerrain (0x1400a6fe0)
#include "Action/PathSearch.h"       // RegionGraph::MarkCellDirty (0x1400a8c90)
#include "Game/GameUpdateLoop.h"
#include "Game/terrain_sidecar.h"     // [terrain sidecar] NoteWrite after the +0x29 store
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "Entity/ComponentEntity.h"

// RPCtrlTag lives in ResourceProduceComponent.h (the member type needs it).
using RPDispatch = ::ControllerLive::Dispatcher<RPCtrlTag>;

// The state handlers receive the owning component (the dispatcher's
// `MOV RCX,[node+0x18]`): every handler in this table belongs to an RP leaf.

// ── Engine helpers, by retail address ────────────────────────────────────────────
// 0x140003050 FindComponent<WorkComponent> — the work-ledger component that
//             CitizenAI_impl_ec60 posts against (same finder BuildComponent state 1 uses).
// 0x140002e10 FindComponent<WorkPlaceComponent>.
// 0x14012ec60 CitizenAI_WorkScheduler_Post(workComp, code, flag, workIBP) — post to the ledger.
// 0x14012fc00 WorkPlaceComponent_SetAccepting(wpc, accepting) — drives WPC+0x50.
// 0x14012ead0 WorkComponent_CheckWorkSlot / 0x14012eb50 WorkComponent_RegisterWorkSlot —
//             work-slot occupied test / release (citizen work scheduler).
// 0x140003ea0 FindComponent<StateMachineComponent>.


// ── state 2 enter — 0x14012b1b0 ───────────────────────────────────────────────────
// target = (self+0x48 == 0) ? 13 : 14, then request it. +0x48 is the produceFlag
// (the panel's pause bit), zeroed by ResourceProduceComponent_Ctor.
static char RP_State2Enter(ResourceProduceComponent* self, StateMachine* sm) {
    const bool paused = self->produceFlag;
    const int target = paused ? SmId(BuildingState::Paused) : SmId(BuildingState::Producing);
    ::ControllerLive::RequestStateTransition(sm, target);
    return 0;
}

// ── state 13 enter — 0x14012ae60 ──────────────────────────────────────────────────
// If the produceFlag is set, bounce straight to 14 (retail's early-out on param_1[9],
// which is the qword at +0x48 read as a byte). Otherwise:
//   work = self->vtable[18](self, &out)      CreateWork — per-class
//   CitizenAI_impl_ec60(workComp, 1, 0, &work)
//   SetAccepting(wpc, 1)
//   return self->vtable[19](self)            OnEnterProducing
static char RP_State13Enter(ResourceProduceComponent* self, StateMachine* sm) {
    ResourceProduceComponent* rp = self;

    // Entry trace as well as the outcome trace below: without it a crash anywhere in
    // this body is indistinguishable from the state never being entered at all.

    // The pause flag is the LOW BYTE of the qword at +0x48 (retail: `(char)param_1[9]`),
    // and nothing in our port ever writes it non-zero. So either something retail-side
    // sets it, or it was never initialised and we are reading whatever the allocator
    // left. Print the WHOLE qword: a clean 1 means a deliberate pause, anything
    // pointer-shaped means uninitialised memory. Log both arms, so "entered producing"
    // and "bounced to paused" are distinguishable.

    if (rp->produceFlag) {
        ::ControllerLive::RequestStateTransition(sm, BuildingState::Paused);
        return 1;
    }

    ComponentEntity* owner = rp->_entity.Get();

    // CreateWork (slot 18) — allocates the class's Work object and hands back an
    // InternalBasePtr. Abstract in the base, so a null here means the subclass did
    // not override it: report rather than crash on a _purecall.
    ::Object::InternalPtr<Work> workIbp;
    rp->CreateWork(&workIbp);

    // Post it to the reservation ledger so an idle citizen claims it.
    WorkComponent* workComp = owner->FindComponent<WorkComponent>();   // 0x140003050
    if (workComp != nullptr && workIbp) {
        workComp->PostWork(1, 0, &workIbp);
    }

    // WorkPlaceComponent +0x50 = accepting. Without this the building has workers
    // assigned but every gather bails "no target": the same trap the pasture wave hit.
    WorkPlaceComponent* wpc = owner->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    if (wpc != nullptr) {
        wpc->SetAccepting(true);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)
    }


    return rp->OnEnterProducing();
}

// ── state 14 enter — 0x14012ad20 ──────────────────────────────────────────────────
// Release the held work slot if occupied, stop accepting, then OnExitProducing.
static char RP_State14Enter(ResourceProduceComponent* self, StateMachine* /*sm*/) {
    ComponentEntity* owner = self->_entity.Get();

    WorkComponent* workComp = owner->FindComponent<WorkComponent>();   // 0x140003050
    if (workComp != nullptr) {
        if (workComp->IsWorkPosted()) {
            workComp->CancelWork();
        }
    }

    WorkPlaceComponent* wpc = owner->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    if (wpc != nullptr) {
        // FIXED 2026-08-15 — this line used to read
        //     s_setAccepting(*reinterpret_cast<uintptr_t*>(wpcIbp), 0);
        // i.e. one deref too many. `s_findWpc(owner, &wpcIbp)` writes the COMPONENT
        // POINTER into the local, so the extra `*` passed the WPC's own VTABLE POINTER
        // as the component. SetAccepting then wrote `*(char*)(vtable+0x50)` and read
        // `vtable+0x10` as the owner entity, so the mesh finder walked a garbage
        // component array and dereferenced a -1 slot: ACCESS_VIOLATION reading
        // 0xffffffffffffffff in FindComponentByTypeId.
        //
        // Retail 0x14012ad20 is unambiguous — `FUN_140002e10(param_1[2], &local_res8);
        // Entity_impl_fc00(local_res8, '\0')` — the local goes in DIRECTLY. State13Enter
        // above (the `s_setAccepting(wpcIbp, 1)` call) already had it right, so the two
        // halves of the same pause/resume pair disagreed with each other in one file.
        //
        // Latent, not new: nothing reached state 14 until ConsumeProduce/Market were
        // promoted to live controllers, because no ResourceProduce subclass had its
        // state machine running in a default build.
        wpc->SetAccepting(false);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)
    }


    self->OnExitProducing();
    return 1;
}

// ── transition 2 -> 13 action — 0x14012af90 ───────────────────────────────────────
// Stamp the building's work positions into the map (review F-179). For every
// "create" node (kind 2) of the InteractComponent and its parts — the count re-read
// each pass (0x14012b015: desc+0x50, plus NodeCount(2) of each +0x28 part, which is
// NodeCount(2)'s own body) — the node's cell is retyped to 2: the walkable-zone bit
// (old type in 0x1e0) is ORed into Map+0xa2, +0x29 = 2, the region graph is marked
// dirty when the old type was walkable, the pathfinder is told, and the cell's
// occupant (+0x08) is nulled. The same retype as the pasture / cemetery stampers.
// Retail tests nothing: the InteractComponent, the map and its objects are read
// unguarded. Dispatched through OnState, so the signature is void(data).
static void RP_Trans2to13(ResourceProduceComponent* self) {
    InteractComponent* interact = self->_entity->FindComponent<InteractComponent>();   // 0x140002ed0
    Map* map = GameUpdateLoop::_singleton->m_game;                                    // +0xf8
    for (int node = 0; node < interact->NodeCount(2); ++node) {
        Math::Vector3 xy;
        interact->NodePosition(&xy, 2, node, nullptr);   // 0x140122e20
        const int dim = map->size;
        const float half = static_cast<float>(dim >> 1);
        int gx = static_cast<int>(xy.x + half);
        int gy = static_cast<int>(xy.y + half);
        if (gx < 0) gx = 0;
        if (gx >= dim) gx = dim - 1;
        if (gy < 0) gy = 0;
        if (gy >= dim) gy = dim - 1;
        const int cell = dim * gy + gx;

        MapCell& rec = map->cells.GetData()[cell];
        const uint8_t old = static_cast<uint8_t>(rec.terrainType);      // +0x29
        const bool wasZoneless = ((1u << (old & 0x1f)) & Bits(PathBits::Zoneless)) != 0;  // old 5..8
        map->field_0xA2 |= static_cast<uint8_t>(wasZoneless);           // 0x14012b112
        rec.terrainType = PathType::Normal;
        TerrainSidecar::NoteWrite(&rec);   // [terrain sidecar] mirror
        CellStamp::NoteWrite(&rec);   // [AMBMEMO] cell write clock
        if (wasZoneless)
            map->regionGraph->MarkCellDirty(cell % map->size, cell / map->size);   // +0x70, 0x1400a8c90
        map->pathfinder->SetCellTerrain(cell, ToUnderlying(PathType::Normal));      // +0x68, 0x1400a6fe0
        rec.occupant = nullptr;                                                     // 0x14012b15e
        CellStamp::NoteWrite(&rec);   // [AMBMEMO] cell write clock
    }
}

// ── state 13 per-frame — 0x14012b190 ──────────────────────────────────────────────
// A one-line forward to the component's ProduceTick (slot 21). The base leaves it a
// no-op; FishingComponent is the only subclass in this wave that installs a real one.
static char RP_State13PerFrame(ResourceProduceComponent* self, StateMachine* /*sm*/, const System::Time* time) {
    return self->ProduceTick(time);
}

// ── transition 13 -> 3 action — 0x14012af70 ───────────────────────────────────────
// Teardown path: run OnExitProducing so the class drops whatever the produce tick
// was holding. Dispatched through OnState, so the signature is void(data).
static void RP_Trans13to3(ResourceProduceComponent* self) {
    self->OnExitProducing();
}

// ── event 3 on state 13 — 0x14012adf0 — panel PAUSE ───────────────────────────────
static char RP_Event3Pause(ResourceProduceComponent* /*self*/, StateMachine* sm) {
    // Event 0 TOGGLES (13 -> pause, 14 -> resume), so a single stray post closes a
    // producing workplace: state 14 calls SetAccepting(false), which raises the
    // "work is disabled at this location" sign AND evicts the roster. Trace the
    // poster — an unasked-for pause is the whole bug.
    ::ControllerLive::RequestStateTransition(sm, BuildingState::Paused);
    return 1;
}

// ── event 3 on state 14 — 0x14012acb0 — panel RESUME ──────────────────────────────
// Retail gates the resume on +0x48 == 0: a component whose produceFlag is set stays
// paused, so the event is a no-op rather than a toggle. Faithful, not a guard.
static char RP_Event3Resume(ResourceProduceComponent* self, StateMachine* sm) {
    const bool paused = self->produceFlag;
    if (!paused) {
        ::ControllerLive::RequestStateTransition(sm, BuildingState::Producing);
    }
    return 1;
}

// ── component vtable slot 13 — retail 0x14012b520 — the panel COMMAND MASK ────────
// Shared by every ResourceProduceComponent subclass (Ghidra labels it
// "HerbalistComponent_Slot13", but the byte lives on the base vtable and all three food
// subclasses inherit it — see docs/history/notes/FOOD_PRODUCTION_WAVE_BRIEF.md).
//
// ⚠ This is NOT CanCommand(self, id). Retail's body takes only `self` and returns a
// BITMASK of which panel commands are currently available:
//     smc  = FindComponent_StateMachine(owner)                  (0x140003ea0)
//     cur  = smc+0x52 (pending state); if negative, smc+0x50 (current state)
//     mask = (cur != 14) ? 2 : 0                                 // pause offered
//     if (self+0x48 == 0) mask |= ((unsigned)(cur - 13) < 2)     // resume offered
// So bit 1 is "pause" (offered unless already paused in state 14) and bit 0 is
// "resume" (offered while in state 13 or 14 and not flagged). Any extra argument a
// caller passes is ignored, exactly as retail ignores it.
uint32_t ResourceProduceComponent::CommandMask(int /*command*/) {
    StateMachineComponent* smc = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    if (!smc) return 0;

    int8_t cur = smc->stateMachine.pendingState;
    if (cur < 0) cur = smc->stateMachine.currentInput;

    uint8_t mask = (cur != StateByte(BuildingState::Paused)) ? 2 : 0;
    if (!produceFlag)
        // Producing or Paused (13 / 14): retail's unsigned range test.
        mask |= (static_cast<uint32_t>(cur - StateByte(BuildingState::Producing)) < 2u) ? 1 : 0;
    return mask;
}

// ── component vtable slot 14 — retail 0x14012b5a0 — the panel COMMAND ─────────────
//     smc = FindComponent_StateMachine(owner)
//     node = *(smc+0x48); node->vtable[2](node, smc+0x48, smc+0x51, 0)   // PostEvent
//     smc->vtable[8](smc, Time(0.0))                                      // force a tick
//
// ⚠ The event id is a hard-coded 0 (`XOR R9D,R9D` in the disassembly) and NO command id
// is read — the slot takes only `self`.
//
// CORRECTED 2026-08-15. The comment here used to claim the id-0-vs-3 mismatch was
// "retail's, reproduced rather than corrected". That was a misread, and it made the
// work pause/resume button a permanent no-op. Retail's event registrar for THIS
// description is `ResourceProduceDescription_helper_b230_sub_7f90` @ 0x140037f90, whose
// bytes are:
//     4883EC18            SUB  RSP,0x18
//     8B05 06491A00       MOV  EAX,[rip+...]
//     C70424 00000000     MOV  dword ptr [RSP],0x0    <-- event id LITERAL 0; ECX discarded
//     48897424 08         MOV  [RSP+0x8],RDX          <-- handler
// So `sub_7f90(3, handler)` registers the handler under id **0**, and slot 14 posts 0 —
// in retail they MATCH. Our port transcribed the ignored first argument as the id, which
// is what broke it.
//
// The contrast proves the registrars really do differ, so this is not a blanket rule:
// the orchard's twin registrar 0x140009c70 has `890C24` (MOV [RSP],ECX) and stores its
// real id — which is why the orchard/cropfield tables keyed 3/4/5 are correct as written.
// See the TableAddEvent calls in RPCtrl_PopulateTable below.
void ResourceProduceComponent::Command(int /*command*/) {
    StateMachineComponent* smc = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    if (!smc) return;

    StateMachine& sm = smc->stateMachine;
    ControllerComponent::Controller* node = sm.current;
    if (node) {
        // node = the controller member the StateMachineComponent linked (D6); slot 2.
        const int curIdx = sm.activeState;
        node->PostEvent(&sm, curIdx, SmId(BuildingEvent::TogglePause));
    }

    // Force an immediate StateMachineComponent tick (its vtable slot 8) with Time(0).
    {
        System::Time zeroTime;
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        static_cast<Component*>(smc)->Tick(&zeroTime);   // slot 8, a virtual dispatch
    }
}

// ── table population — replica of ResourceProduceDescription_helper_b230 ──────────
// Registration ORDER matters: it fixes the state INDEX that StateMachine::Tick carries
// in its +0x09/+0x0a/+0x0b bytes, so it mirrors retail's call order exactly.
static void RPCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = RPDispatch::table;
    if (t.count != 0) return;                       // idempotent

    // ── state 2 — DEFAULT / idle ──────────────────────────────────────────────────
    TableRegisterState(t, BuildingState::Built, &RP_State2Enter, nullptr,
                       nullptr, nullptr);
    // ->13 carries retail's 0x14012af90 (14012b24e LEA RDX,[0x14012af90]), which
    // stamps the building's work positions into the map (terrain byte +0x29 = 2).
    TableAddTransition(t, BuildingState::Producing, &RP_Trans2to13);
    TableAddTransition(t, BuildingState::Paused, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);

    // ── state 13 — PRODUCING ──────────────────────────────────────────────────────
    TableRegisterState(t, BuildingState::Producing, &RP_State13Enter, nullptr,
                       &RP_State13PerFrame, nullptr);
    TableAddTransition(t, BuildingState::Paused, nullptr);
    TableAddTransition(t, BuildingState::Demolish, &RP_Trans13to3);
    // Event id 0, NOT 3 — retail's registrar 0x140037f90 hardcodes 0 and discards the id
    // argument (see the byte listing on ResourceProduceComponent_Command above). Slot 14
    // posts 0, so registering 3 here meant PostEvent's linear scan never matched and the
    // work pause button did nothing.
    TableAddEvent(t, BuildingEvent::TogglePause, &RP_Event3Pause);

    // ── state 14 — PAUSED / not accepting ─────────────────────────────────────────
    TableRegisterState(t, BuildingState::Paused, &RP_State14Enter, nullptr,
                       nullptr, nullptr);
    TableAddTransition(t, BuildingState::Producing, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
    // Event id 0 for the same reason as the pause handler above. State 13 maps 0 -> Pause
    // and state 14 maps 0 -> Resume, so the single posted event toggles.
    TableAddEvent(t, BuildingEvent::TogglePause, &RP_Event3Resume);
}

// Built ONCE at static-init time, as retail's ResourceProduceDescription setup does
// (B38 D6/D7). Before, every RP leaf factory re-ran this from both threads.
static const bool s_rpStateTableBuilt = (RPCtrl_PopulateTable(), true);

// ============================================================================
// ENDGAME B.3.8 MI-B — ResourceProduceComponent as a REAL abstract class
// (ResourceProduceComponent.h). Before: six leaves each stamped the shared
// Dispatcher<RPCtrlTag> table into raw bytes at +0x28 and dispatched slots 18-21
// by index through their hand-built 48-slot tables.
// ============================================================================

// 0x14012b3d0: Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer (+0x40); `MOV byte [RBX+0x48],0`.
ResourceProduceComponent::ResourceProduceComponent()
    : _controller(this), produceFlag(false), _pad49{} {}

// [1] TypeName / [2] Type, the record and retail's abstract factory (create =
// the logging stub, in-place = null): ENDGAME B.3.9 RF.
DEFINE_EXE_ABSTRACT_TYPE(ResourceProduceComponent, "ControllerComponent")

// [3] 0x14012b4a0: `CALL 0x1401189b0` (Component::Persist), an EMPTY Serializer(0)
// scope (that pair is ControllerComponent::Persist, 0x1401195a0), then a v0 scope
// holding the +0x48 bool (the serializer's bool slot, dispatch 0x140152a58).
void ResourceProduceComponent::Persist(IO::Stream& stream) {
    ControllerComponent::Persist(stream);
    IO::Serializer scope(stream, 0);
    scope.Serialize(produceFlag, nullptr);
}

