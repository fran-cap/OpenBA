// src/game/Component/attendancecomponent.cpp
// Clean reimplementation of AttendanceComponent::Update from Application-x64.exe.
//
// AttendanceComponent manages a queue of citizens waiting to attend a building
// (e.g., church, tavern, school). When the building is idle (not busy), the
// component processes the attendance queue.
//
// Update logic (0x140044370, 22 instructions):
//   1. Check if queue count (+0x30 as int) is nonzero; early-out if empty
//   2. Get the entity's building component via FUN_140002e10
//   3. Check if building is idle (building+0x60 == 0)
//   4. If idle, call FUN_140043f60 to process the attendance queue
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/AttendanceComponent.h"
#include "Component/StatusIconComponent.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // ::Object::GetTypeNameById (pre-fault report)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/WorkPlaceComponent.h"   // ENDGAME B.3.9 C1: 0x1401c80e0 cell class
#include "Component/CitizenComponent.h"     // ENDGAME B.3.9 C1: 0x1401c7158 cell class
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/CitizenBehavior.h"

// ============================================================
// Helpers, called directly (B.3R): FUN_140002e10 FindComponent<WorkPlaceComponent>
// (get the WorkPlaceComponent from the entity), FUN_140043f60 ProcessQueue (drain the
// attendance queue), both defined below.
// ============================================================

static void AttendanceComponent_ReportMissingWorkplace(AttendanceComponent* self, ComponentEntity* entity,
                                                       int32_t queueCount);

// ============================================================
// Reimpl helpers for the attendance functions
// ============================================================

// Pre-fault report for AttendanceComponent::Update (see the call site).
static void AttendanceComponent_ReportMissingWorkplace(AttendanceComponent* self, ComponentEntity* e,
                                                       int32_t queueCount) {
    fprintf(stderr, "[AttendanceComponent::Update] self=%p queue(+0x30)=%d owner entity=%p has NO "
            "WorkPlaceComponent (0x140002e10 returned null) -- about to fault faithfully\n",
            static_cast<void*>(self), queueCount, static_cast<void*>(e));
    if (e) {
        const uint32_t flags = e->_entityFlags;
        const int32_t  n     = e->_components._size;
        Object::InternalPtr<Component>* comps = e->_components._data;
        fprintf(stderr, "[AttendanceComponent::Update]   entity flags=0x%x components=%d:", flags, n);
        for (int i = 0; comps && i < n && i < 32; ++i) {
            Component* c = comps[i].Get();
            if (!c) { fprintf(stderr, " <null>"); continue; }
            const uint32_t tid = c->Type();   // slot 2
            const char* nm = ::Object::GetTypeNameById(tid);
            const bool isHost = tid == WorkPlaceComponent::TypeId();
            fprintf(stderr, " %s(%u%s)", (nm && nm[0]) ? nm : "?", tid,
                    isHost ? (::Object::IsDerivedFromNullSafe(c, tid)
                              ? ",HOST,derived" : ",HOST,NOT-derived") : "");
        }
        fprintf(stderr, "\n[AttendanceComponent::Update]   jobHost tid(1401c80e0)=%u\n",
                WorkPlaceComponent::TypeId());
    }
    fflush(stderr);
}

// Queue-drain dispatch helpers, called directly: 0x140115a50
// AttendanceComponent_ReleasePartnerSlot, 0x1401159b0 AttendanceComponent_ReleaseLink59b0,
// 0x140111650 AttendanceComponent_ReleaseLink1650 (all defined below).
// Callees of the two worker-release helpers (Entity_impl_1650/59b0): 0x140002cc0
// Array<T>::Remove, 0x140002f90 FindComponent<StatusIconComponent>, 0x14012c3f0
// CitizenComp_FUN_14012c3f0; CitizenComponent::TryFindPartner (0x140111840) is
// CitizenComp_TryEnrolInSchool.

// Slot-3 (Persist) helpers: Component::Persist 0x1401189b0; the queue array grow
// 0x1400032e0; and CitizenComponent::TypeId() (0x1401c7158), the type id the
// per-entry filter tests against.
// 0x1400032e0 was Collection::Array<void*>::SetSize(n) monomorphised into the
// exe (ENDGAME B.3.2 step 2) — converted at its call site below.
// ============================================================
// Reimpl of FUN_140115a50 (0x140115a50):
// release the citizen's school slot (+0x208): detach from the school's waiting
// queue (002cc0), reset its status icon flag 2 off (12c3f0), null the slot; then
// if param_2 set, immediately re-roll for a new school (TryFindPartner) and clear the
// slot again on failure. Closure-safe after the cascade registered TryFindPartner.
// A CitizenComponent member (0x140115a50).
void CitizenComponent::LeaveSchool(char reroll) {
    Object::InternalPtr<AttendanceComponent>& slot = school;
    if (slot == nullptr) return;

    Object::InternalPtr<CitizenComponent> selfIbp(this);
    AttendanceComponent* linked = slot.Get();
    linked->queue.Remove(selfIbp);

    // Retail's finder returns its out-slot, never null: 12c3f0 is called on the found
    // StatusIconComponent, null included (the tree's `if (mesh)` tested the slot address).
    StatusIconComponent* mesh = linked->_entity->FindComponent<StatusIconComponent>();   // 0x140002f90
    mesh->SetFlag(2, '\0');   // 0x14012c3f0 (CitizenComp_FUN_14012c3f0's member)

    slot = nullptr;

    if (reroll != '\0') {
        AttendanceComponent* exclIbp = nullptr;   // a null InternalPtr<AttendanceComponent>
        char found = TryEnrolInSchool(exclIbp);   // 0x140111840
        if (found == '\0') {
            slot = nullptr;
        }
    }
}

// ============================================================
// Reimpl of Entity_impl_1650 (0x140111650) and Entity_impl_59b0 (0x1401159b0):
// release a queued-worker link slot. Both are identical apart from the slot
// (1650 -> +0x230, 59b0 -> +0x228). When the slot holds a linked building: detach this
// worker from the building's waiting queue (FUN_140002cc0), reset that building's
// status icon flag 2 to off (FUN_14012c3f0), then null the slot.
// The two public bodies are CitizenComponent members (0x140111650 / 0x1401159b0).
static void AttendanceComponent_ReleaseLinkSlot(CitizenComponent* self, Object::InternalPtr<AttendanceComponent>& slot) {
    if (slot == nullptr) return;

    Object::InternalPtr<CitizenComponent> selfIbp(self);
    AttendanceComponent* linked = slot.Get();
    linked->queue.Remove(selfIbp);

    // Retail's finder returns its out-slot, never null: 12c3f0 is called on the found
    // StatusIconComponent, null included (the tree's `if (mesh)` tested the slot address).
    StatusIconComponent* mesh = linked->_entity->FindComponent<StatusIconComponent>();   // 0x140002f90
    mesh->SetFlag(2, '\0');   // 0x14012c3f0 (CitizenComp_FUN_14012c3f0's member)

    slot = nullptr;
}

void CitizenComponent::ReleaseLink230() {
    AttendanceComponent_ReleaseLinkSlot(this, field_0x230);
}
void CitizenComponent::ReleaseCareBuilding() {
    AttendanceComponent_ReleaseLinkSlot(this, careBuilding);
}

// ============================================================
// Reimpl of FUN_140043f60 (AttendanceComponent_sub_3f60:
//   drain the attendance queue, dispatching each worker by which list it is on)
// ============================================================
// The helpers below remove each worker from the queue, so the loop re-reads the
// count each pass. The dispatch compares this component against three worker
// fields (+0x208/+0x228/+0x230).
void AttendanceComponent::ProcessQueue() {
    while (queue._size > 0) {
        CitizenComponent* worker = queue[0].Get();
        if (worker->school == this) {
            worker->LeaveSchool('\x01');
        } else {
            if (worker->careBuilding == this) {
                worker->ReleaseCareBuilding();
            } else {
                if (worker->field_0x230 == this) {
                    worker->ReleaseLink230();
                }
            }
        }
    }
}

// ============================================================
// Vtable slots 13/14 — the panel COMMAND pair
// ============================================================
// AttendanceComponent is NOT a ResourceProduceComponent subclass, so it shares nothing
// with the base command pair. Byte-verified from vtable_AttendanceComponent
// (0x140158ce0): slot 13 = 0x140043f10, slot 14 = 0x140043e80.
//
// Both bodies delegate to the entity's WorkPlaceComponent rather than holding state of
// their own: the attendance building's "work" checkbox IS the workplace's accepting
// flag (WPC+0x50). That is why this class overrides instead of inheriting — and why
// both bodies start by re-finding the WPC off the owner entity via 0x140002e10
// (FindComponent<WorkPlaceComponent>). WorkPlaceComponent_SetAccepting
// (0x14012fc00) is declared in exe_bodies.h.

// ── slot 13 — retail 0x140043f10 — availability mask ──────────────────────────────
// Retail body: find the WPC, then `return -(*(char*)(wpc+0x50) != 0) & 2 | 1;`
// i.e. `accepting ? 3 : 1` — bit 0 "command offered", bit 1 "currently ON". It never
// returns 0, so the checkbox is never greyed by this slot.
//
// ⚠ Retail takes only `self`; the command id the caller passes in RDX is ignored. We
// declare the parameter so the vtable call is ABI-compatible, and ignore it as retail does.
//
// ⚠ The `wpc+0x50` deref is UNGUARDED in retail and left unguarded here: retail relies on
// an attendance building always carrying a WorkPlaceComponent. If this ever faults, the
// fault is an entity built without a WPC upstream — do not paper over it here.

// ── slot 14 — retail 0x140043e80 — flip the workplace's accepting flag ────────────
// Retail body:
//     wpc = FindWorkPlaceComponent(self+0x10)
//     cur = *(char*)(wpc+0x50)
//     WorkPlaceComponent_SetAccepting(wpc, cur == 0)      // 0x14012fc00
//     if (*(char*)(wpc+0x50) == 0) AttendanceComponent_ProcessQueue(self)
//
// The re-read after SetAccepting is load-bearing, not redundant: SetAccepting is a large
// body (mesh-visual toggle, status-icon bit, and — when turning OFF — a loop that releases
// every seated worker), and the queue drain must observe the flag it actually left behind.
// Draining on the OFF edge is what sends the students//workers home; on the ON edge there
// is nothing to drain.
//
// ⚠ Retail ignores the command id here too.

// ============================================================
// Vtable slot 7 — retail 0x1400443d0 — leave the town attendance registry
// ============================================================
// Ghidra calls this "AttendanceComponent_OnCreate". It is slot 7, which on this
// component family is OnRemove, not OnAdded — the usual off-by-one mislabel (see
// reference_ghidra_oncreate_onremove_mislabel). The body is a single erase-by-value
// from the town attendance list at gameWorld+0xf0 +0x60, the list that state 2's enter
// appends to and that AllFull walks. Without it a demolished school stays in the list
// forever and every surviving school reads as "full".
//
// It is also called directly by state 3's enter (through the vtable), which is the
// "close for the night" path — so this is not only teardown.

// ============================================================
// Vtable slot 3 — retail 0x140044410 — Shutdown / Serialize
// ============================================================
// Ghidra labels it "AttendanceComponent_Controller_Slot13"; the vtable BYTES at
// 0x140158ce0 put it at index 3, which is Shutdown across this whole family.
//
//   Component_Serialize(self, stream)                       0x1401189b0
//   { Serializer(stream, 0) }                               // empty block, subclass compat
//   Serializer s(stream, 0)
//   int n = self[+0x30];  s.Serialize(&n)                   // the waiting-queue count
//   if (s[+8] == 0) GrowArray(self+0x28, n)                 // READING: size the array first
//   for (i in 0..n) { InternalBasePtr::Serialize(&q[i], s)
//                     if (q[i] && !IsDerivedFrom(q[i], CitizenComponent)) q[i] = 0 }
//
// The IsDerivedFrom filter is a load-bearing part of load, not a sanity check: a saved
// queue entry whose citizen no longer exists deserializes to some other object, and
// retail nulls the slot rather than letting ProcessQueue dispatch on it.

// ============================================================================
// ENDGAME B.3.8 MI-A — AttendanceComponent as a REAL class (AttendanceComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES (a 48-slot table re-filled at every
// create, slots 3/7/9/13/14/18-21 re-installed from PopulateAndWireSlots), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<AttendanceCtrlTag> table stamped into raw bytes at +0x38.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/AttendanceComponent.h"

// The controller drives the OPEN (2) / CLOSED (3) states (attendancecomponent_states.cpp);
// slots 16/17 are what let the StateMachineComponent link the member. Default state 2
// (0x1400026c0), member at +0x38 (0x1400026b0) — NOT the ResourceProduce family's
// +0x28. The state table is built ONCE at static-init time, as retail's static
// initializer does (B38 D6/D7).
//
// The tree's slots 18-21 (ControllerLive_ScalarDeletingDtor, the description type
// info / type id, `return 1`) were AttendanceDescription's table 0x140158d70 read past
// retail's 18-slot end (B38 D3.5); no dispatch past slot 17 exists in the tree. Gone.
extern "C" void AttendanceCtrl_PopulateTable();
static const bool s_attendanceStateTableBuilt = (AttendanceCtrl_PopulateTable(), true);

// 0x140044580. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28 = +0x30 = 0 (queue's Array ctor); `_controller(this)` is the member's back-pointer at +0x50; then
// `_flags |= 0xa` (0x8a).
AttendanceComponent::AttendanceComponent()
    : _controller(this) {
    OrFlagsInCtor(0xa);
}

// 0x140044510: the body is empty. ~_controller restamps +0x38 (0x140044540), THEN
// ~queue frees and zeroes the waiting queue (+0x28 data, +0x30 count/capacity,
// 0x140044543..0x140044553; kSpec_PtrZero has no element destroy), then the bases'
// restamps and ~Internal: reverse declaration order, the compiler's chain.
AttendanceComponent::~AttendanceComponent() {
}

// create 0x140002670: MemAlloc(0x58, 8), then the ctor.
::Object::Base* AttendanceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AttendanceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AttendanceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AttendanceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AttendanceComponent());
}

// [3] 0x140044410.
void AttendanceComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Stream& io = stream;

    // Empty block — retail opens and closes a Serializer with nothing in it.
    { IO::Serializer empty(io, 0); }

    IO::Serializer ser(io, 0);

    int32_t count = queue._size;
    ser.Serialize(count, nullptr);

    // The queue array has to be grown to the deserialized count before the
    // per-entry loop writes into it. The loading flag is on the STREAM (+0x08).
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // exe 0x1400032e0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
        queue.SetSize(count);
    }

    Object::InternalPtr<CitizenComponent>* q = queue._data;
    for (int32_t i = 0; i < count && q; ++i, ++q) {
        q->InternalBasePtr::Serialize(ser);
        // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
        // downcast to CitizenComponent*; the answer is the same.
        if (q->IsNull()) continue;
        if (!::Object::IsDerivedFromNullSafe(*q, CitizenComponent::TypeId()))
            *q = nullptr;
    }
}

// [7] 0x1400443d0 — leave the town attendance registry (state 3's enter also calls
// it through the vtable).
void AttendanceComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* const town = GameUpdateLoop::_singleton->m_citizenList;
    if (!town) return;
    Object::InternalPtr<AttendanceComponent> selfIbp(this);
    town->attendanceComponents.Remove(selfIbp);   // 0x140002cc0 (CitizenBehavior +0x60)
}

// [9] 0x140044370 — the elapsed-time argument is not read by retail's body.
// ============================================================
// AttendanceComponent::Update (vtable slot [9], 0x140044370)
// ============================================================
void AttendanceComponent::Update(const System::Time* /*time*/) {
    // Check if queue count at +0x30 (as int32) is nonzero
    int32_t count = queue._size;
    if (count == 0)
        return;

    ComponentEntity* entity = _entity.Get();

    // Get the WorkPlaceComponent from the entity (0x140002e10)
    WorkPlaceComponent* building = entity->FindComponent<WorkPlaceComponent>();

    // DIAGNOSTIC ONLY (the deref below stays faithful): retail relies on every attendance
    // building carrying a WorkPlaceComponent, so a null here means something upstream
    // handed this component an entity without one. Say WHICH entity before faulting,
    // because the crash log alone cannot (first seen in the BANISHED_STRESS town).
    if (building == nullptr) {
        AttendanceComponent_ReportMissingWorkplace(this, entity, count);
    }

    // Check if building is idle (the roster count, +0x60, == 0)
    int32_t busyState = building->workers._size;
    if (busyState == 0) {
        // Building is idle -- process the attendance queue
        ProcessQueue();   // 0x140043f60
    }
}

// [13] 0x140043f10 — `accepting ? 3 : 1`; the command id is ignored, as in retail.
uint32_t AttendanceComponent::CommandMask(int /*command*/) {
    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();
    return wpc->isAccepting ? 3 : 1;
}

// [14] 0x140043e80 — flip the workplace's accepting flag; drain on the OFF edge.
void AttendanceComponent::Command(int /*command*/) {
    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();
    const bool cur = wpc->isAccepting;
    wpc->SetAccepting(cur == 0);   // WorkPlaceComponent_SetAccepting's member
    if (wpc->isAccepting == 0)
        ProcessQueue();
}

DEFINE_EXE_TYPE(AttendanceComponent, "ControllerComponent")
