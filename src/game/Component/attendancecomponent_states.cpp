// src/game/Component/attendancecomponent_states.cpp
// The AttendanceComponent controller state machine.
//
// AttendanceComponent backs the buildings citizens VISIT rather than work at all day
// (school house, chapel, tavern seating, town hall gathering). It is NOT a
// ResourceProduceComponent subclass, so it does not share the RPC state table — it
// owns a private one, filled in retail by AttendanceDescription_Setup (0x14014a7e0)
// -> AttendanceDescription_helper_4630 (0x140044630) -> sub_28b0 / sub_29d0
// (retail table DAT_1401c81d0, count DAT_1401c9150, stride 0xf8 — the same shape
// controller_state_table.h models).
//
// The retail populator is exactly six calls, and it registers only TWO states:
//
//   state 2 (OPEN)    enter    0x140043c90  — register in the town attendance list,
//                                             mint a LocationWork, post it to the
//                                             citizen ledger, open the workplace
//                     trans38 0x140043c30  — PER-FRAME: drive the "full" mesh flag
//                     -> 3
//   state 3 (CLOSED)  enter    0x140043bd0  — unregister, send everyone home, close
//                                             the workplace
//                     -> 6, -> 4, -> 5
//
// Transitions carry no action handlers (retail passes 0 for every one), and states
// 4/5/6 are teardown targets with no body of their own — so they are deliberately
// NOT registered here, exactly as retail leaves them.
//
// Default state is 2 (component vtable slot 16 = `MOV EAX,2; RET` at 0x1400026c0)
// and the embedded node sits at +0x38 (slot 17 = `LEA RAX,[RCX+0x38]; RET` at
// 0x1400026b0) — NOT the ResourceProduce family's +0x28. AttendanceComponent_Ctor
// (0x140044580) confirms it: it writes the node vtable to +0x38 and the owning
// component back-pointer to +0x50 (= node + 0x18).

#include "Component/controller_state_table.h"
#include "Work/LocationWork.h"   // B.3.8 CB1a (Vtable)
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B

// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<AttendanceCtrlTag>, AttendanceComponent.h).
#include "Component/AttendanceComponent.h"
#include "Component/AttendanceDescription.h"   // ENDGAME B.3.9 C1: 0x1401c3f78 cell class
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"
using AttendanceDispatch = ::ControllerLive::Dispatcher<AttendanceCtrlTag>;

// AttendanceComponent_ProcessQueue (attendancecomponent.cpp, declared in exe_bodies.h)
// — state 3's enter drains the waiting queue.

// ── Engine helpers, called directly ───────────────────────────────────────────────
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised into
// the exe (ENDGAME B.3.2 step 2) — the town-list append, converted at its call
// site below to the typed attendanceComponents.SetSizeFill(n, &fill).
// 0x1400f72e0 Work_Ctor25(work, ownerEntityIBP)                — base Work ctor.
// 0x140003050 WorkBase_FindComponent_3050(entity, outIBP)       — the citizen work ledger.
// 0x140002e10 AttendanceComponent_GetJobHostComponent(entity, outIBP) — WorkPlaceComponent.
// 0x14012ec60 CitizenAI_WorkScheduler_Post(workComp, code, flag, workIBP) — post to the ledger.
// 0x14012fc00 WorkPlaceComponent_SetAccepting(wpc, accepting).
// 0x140002f90 WorkBase_FindComponent_2f90(entity, outIBP)       — MeshInstance.
// 0x14012c3f0 CitizenComp_FUN_14012c3f0(mesh, flagIdx, value)  — MeshInstance set-state-flag.

#include "Component/StatusIconComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"

// The town attendance registry: gameWorld+0xf0 is the town object, +0x60 its array
// base and +0x68 the element count. Both state 2's enter (append) and the component's
// vtable slot 7 (erase) operate on it, and AllFull below walks it.
// (CitizenBehavior::attendanceComponents / attendanceComponentCount.)
static CitizenBehavior* AC_TownObject() {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    return GameUpdateLoop::_singleton->m_citizenList;
}

// ── AttendanceDescription_helper_3db0 (0x140043db0) — "is every building of my kind
//    full?" ─────────────────────────────────────────────────────────────────────────
// Walks the town attendance registry looking at every OTHER component that shares this
// one's Description (comp+0x18). If ANY of them still has room — queue count (+0x30)
// below the description's capacity (desc+0x20) — the answer is 0 (not full). Only when
// every one is at capacity does it return 1.
//
// Note the scope: the flag is per-DESCRIPTION, not per-building, so a town with two
// schools shows both as "full" only once both are. That is retail's behaviour, and it
// is what makes the visual read as "no more room in town for this" rather than "this
// particular door is shut".
//
// ⚠ The desc deref after IsDerivedFrom is unguarded in retail and left unguarded here:
// the equality test on the line above already established that entry carries this exact
// description pointer, so the cast cannot fail.
char AttendanceComponent::AllFull() {
    CitizenBehavior* const town = AC_TownObject();
    if (!town) return 1;

    const Object::InternalPtr<AttendanceComponent>* base = town->attendanceComponents._data;
    const int       count = town->attendanceComponents._size;
    const Description* myDesc = _description;

    for (int i = 0; i < count && base; ++i) {
        AttendanceComponent* entry = base[i].Get();
        if (!entry) continue;
        if (entry->_description != myDesc) continue;

        Description* d = entry->_description;
        AttendanceDescription* desc = ::Object::IsDerivedFromNullSafe(d, AttendanceDescription::TypeId())
                                          ? static_cast<AttendanceDescription*>(d) : nullptr;
        const int capacity = desc->_maxCitizens;
        if (entry->queue._size < capacity)
            return 0;
    }
    return 1;
}

// ── state 2 trans38 — retail 0x140043c30 — PER-FRAME ──────────────────────────────
// Pushes the "all full" answer into the building's status icon flag 2, which is the
// closed/full visual. This is the only per-frame work the attendance controller does;
// there is no update handler on either state.
char AttendanceComponent::State2Frame() {
    StatusIconComponent* mesh = _entity->FindComponent<StatusIconComponent>();   // 0x140002f90

    const char full = AllFull();
    // ⚠ Retail reads `*(longlong**)self` where `self` is the finder's RETURN value —
    // and the finder RETURNS ITS OUT-PARAM ADDRESS, so retail's deref is simply
    // "load the found component". Handing 12c3f0 the component's VTABLE faulted
    // (see reference_outparam_finder_recycled_ibp).
    mesh->SetFlag(2, full);   // 0x14012c3f0 (CitizenComp_FUN_14012c3f0's member)
    return 0;
}

// ── state 2 enter — retail 0x140043c90 (Ghidra: "LocationWork_Ctor") ──────────────
// This is the OPEN transition and it is four distinct steps:
//   1. append this component to the town attendance registry (gw+0xf0 +0x60/+0x68) —
//      the list AllFull walks, and the list slot 7 erases from;
//   2. mint a 0x50-byte LocationWork bound to the owner entity;
//   3. post it to the entity's WorkComponent ledger via CitizenAI_impl_ec60(.., 1, 0, ..)
//      — this is what makes citizens actually walk here;
//   4. open the workplace (SetAccepting(wpc, 1)).
// Steps 2-4 are the same shape as the ResourceProduce state-13 enter; step 1 is what
// makes attendance different — the building has to be discoverable town-wide.
//
// ⚠ Ghidra's name is a misnomer of the same family as "HerbalistWork_Ctor" for
// HerbalistComponent's slot 18: this is a state ENTER handler that happens to build a
// LocationWork, not LocationWork's constructor (that is 0x1400efc60).
char AttendanceComponent::State2Enter() {
    // 1. register in the town attendance list
    CitizenBehavior* const town = AC_TownObject();
    if (town) {
        Object::InternalPtr<AttendanceComponent> selfIbp(this);
        // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill).
        const int32_t newCount = town->attendanceComponents._size + 1;
        town->attendanceComponents
            .SetSizeFill(newCount, &selfIbp);
    }

    // 2. mint the LocationWork
    void* work = nullptr;
    LocationWork* location = nullptr;
    if (::System::MemoryAllocator::_singleton)
        work = ::System::MemoryAllocator::MemAlloc(0x50, 8);
    if (work) {
        // Retail inlines LocationWork's entity-seeded ctor here: Work_Ctor25
        // (0x1400f72e0), the 0x1401616a0 table, +0x48 = 0. ENDGAME B.3.9 OBJ:
        // the real class, not the `??0Internal` door + hand stamp.
        ComponentEntity* entity = _entity.Get();
        location = ::new (work) LocationWork(&entity);
    }
    Object::InternalPtr<Work> workIbp(location);

    // 3. post it to the citizen work ledger
    {
        WorkComponent* workComp = _entity->FindComponent<WorkComponent>();   // 0x140003050

        workComp->PostWork(1, '\0', &workIbp);
    }

    // 4. open the workplace
    {
        WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        wpc->SetAccepting(true);   // WorkPlaceComponent_SetAccepting's member
    }

    return 0;
}

// ── state 3 enter — retail 0x140043bd0 ────────────────────────────────────────────
// The CLOSED transition, the exact mirror of state 2:
//   1. component vtable slot 7 (+0x38) — erase from the town attendance registry;
//   2. ProcessQueue — send every queued citizen home;
//   3. SetAccepting(wpc, 0) — close the workplace.
// Step 1 goes through the vtable rather than calling the ported body directly, because
// that is what retail does.
char AttendanceComponent::State3Enter() {
    Component* self = this;
    self->OnRemove();   // slot 7, dispatched as retail does

    ProcessQueue();

    {
        WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        wpc->SetAccepting(false);   // WorkPlaceComponent_SetAccepting's member
    }
    return 0;
}

// ── the table — replicates AttendanceDescription_helper_4630 (0x140044630) ────────
// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm, time): [node+0x18] is the controller's back-pointer,
// this component, so each entry is a one-line adapter that calls the member.
static char Attendance_State2Enter(AttendanceComponent* a, StateMachine* /*sm*/) { return a->State2Enter(); }   // 0x140043c90
static char Attendance_State2Frame(AttendanceComponent* a, StateMachine* /*sm*/, const System::Time* /*time*/) { return a->State2Frame(); }   // 0x140043c30
static char Attendance_State3Enter(AttendanceComponent* a, StateMachine* /*sm*/) { return a->State3Enter(); }   // 0x140043bd0

extern "C" void AttendanceCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = AttendanceDispatch::table;
    if (t.count != 0) return;   // idempotent, like the retail Setup guard

    // sub_28b0(2, LocationWork_Ctor, .., .., sub_3c30) — enter + per-frame, no update.
    TableRegisterState(t, BuildingState::Built, &Attendance_State2Enter,
                       nullptr, &Attendance_State2Frame, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);      // sub_29d0(3)

    // sub_28b0(3, helper_3bd0, .., .., 0) — enter only.
    TableRegisterState(t, BuildingState::Demolish, &Attendance_State3Enter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Collapse, nullptr);      // sub_29d0(6)
    TableAddTransition(t, BuildingState::HaulAway, nullptr);      // sub_29d0(4)
    TableAddTransition(t, BuildingState::Deconstruct, nullptr);   // sub_29d0(5)
}
