// src/game/Component/statemachinecomponent.cpp
// Clean reimplementation of StateMachineComponent vtable methods.
//
// StateMachineComponent (0x58 bytes) -- state machine tick dispatcher.
// Vtable: 0x140164520
//
// The component embeds the ROOT node of the controller chain at +0x28 and the
// StateMachine at +0x48. Most vtable slots run StateMachine::Tick with different
// parameter combinations. Startup / slot 11 iterate the owning entity's
// components to find and link ControllerComponents.
//
// Layout:
//   +0x00: Component base (0x28 bytes: vtable, next, owner, unk18, flags)
//   +0x28: the root node (Controller interface vptr + parent / child + owner)
//   +0x48: StateMachine: current node, the four state bytes (+0x50..+0x53)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"   // ControllerLive (the root node table)
#include "Entity/ComponentEntity.h"
#include "Object/factory_registration.h"
#include "system/Value.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "Component/sm_memo.h"   // SMMEMO: the slot-8 no-op memo
#include "stress_harness.h"      // g_verifyOn / StressHarness_Verify (the SmTickNoop shadow)
#include <new>

using Controller = ControllerComponent::Controller;

// The chain links of a node: +0x08 parent, +0x10 child.
static inline Controller* NodeParent(const Controller* node) {
    return node->_parent;
}
static inline Controller* NodeChild(const Controller* node) {
    return node->_child;
}

// ============================================================
// StateMachineComponent::RelinkAndResume — the body of slot [11] (0x14012b7d0),
// the post-load fixup (EntityList 0x1400c4730). The chain links are runtime-only,
// so a loaded machine has only its four state bytes: link every
// ControllerComponent's node under the root in component order (IsDerivedFrom
// cell 0x1401c73c0, GetController = slot 17 at +0x88; node parent +0x08 = the
// tail, tail child +0x10 = the node), then, when the input byte is >= 0, move
// `current` to the chain's tail and climb (parent +0x08) until a node's FindState
// (slot 0) owns the input. The owner, every node and every parent are read
// unguarded, as retail reads them.
// ============================================================
void StateMachineComponent::RelinkAndResume() {
    if constexpr (SmMemo::kEnabled) stateMachine.tickMemo = SmMemo::kNone;   // NOT RETAIL: SMMEMO, the chain is relinked
    Controller* tail = &_controller;
    for (int i = 0; i < _entity->_components._size; i++) {
        Component* comp = _entity->_components[i].Get();
        if (comp->IsDerivedFrom(ControllerComponent::TypeId())) {   // import 0x140152a98
            Controller* node = static_cast<ControllerComponent*>(comp)->GetController();
            node->_parent = tail;
            tail->_child = node;
            tail = node;
        }
    }

    if (stateMachine.currentInput < 0) return;   // 0x14012b8df JS

    Controller* node = stateMachine.current;
    while (NodeChild(node) != nullptr) node = NodeChild(node);
    stateMachine.current = node;
    while (node->FindState(stateMachine.currentInput) < 0) {
        node = NodeParent(stateMachine.current);
        stateMachine.current = node;
    }
}

// ============================================================
// StateMachine::Tick (0x1400a9660) lives in statemachine_tick.cpp (its own
// translation unit, so tests/unit/sm_memo_test.cpp links the real body).
// ============================================================

// ============================================================================
// ENDGAME B.3.8 VIS -- StateMachineComponent as a REAL class
// (StateMachineComponent.h): the compiler's 16-slot vtable, a real ctor/dtor chain
// (B38_RULES D1, D2) and the root node as a real member (D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/StateMachineComponent.h"
#include <cstdlib>
#include <cstring>

// ============================================================================
// The members.
// ============================================================================

// 0x14012ba30: Component() is the CALL 0x1401188b0; the 0x140164520 stamp and
// the member's 0x14015f070 / 0x1401644d0 stamps are the compiler's; the member
// gets +0x30 = +0x38 = 0 and +0x40 = this (retail); +0x48 = this+0x28, the root
// (0x14012ba86), and the state quad +0x50 = 0xff00ffff (0x14012ba8a: input,
// active, transition = -1, pending = 0); flags |= 0xcd. A created machine is then
// linked by Startup, a loaded one by Slot11.
StateMachineComponent::StateMachineComponent()
    : _controller(this), stateMachine{&_controller, -1, -1, 0, -1, {}} {
    OrFlagsInCtor(0xcd);
}

// ============================================================
// Persist (vtable slot [3], 0x14012b940)
// ============================================================
void StateMachineComponent::Persist(IO::Stream& stream) {
    // Component::Persist first (0x1401189b0, a direct call in retail too). It
    // used to go through a lazily resolved pointer whose null state desynced the
    // stream on the load path (save/load wave 2026-08-28); the qualified member
    // call has no such state.
    Component::Persist(stream);

    IO::Serializer ser(stream, 0);

    // The four state bytes +0x50..+0x53 through Serialize(signed char&) (import
    // 0x140152418, 0x14012b97d..0x14012b9b3), the fields' own type.
    ser.Serialize(stateMachine.currentInput, nullptr);
    ser.Serialize(stateMachine.activeState, nullptr);
    ser.Serialize(stateMachine.pendingState, nullptr);
    ser.Serialize(stateMachine.transitionState, nullptr);
}
// ------------------------------------------------------------------
// [4] Startup 0x14012b6a0 -- link the controllers. StartupComponents runs it at
// create (flag bit 7), after every component of the entity exists and before
// RegisterEntity's OnAdded pass, so the chain is linked when slot 6 ticks it. In
// component order: every component IsDerivedFrom ControllerComponent (cell
// 0x1401c73c0; the InternalPtr ctor 0x140152a90 on the CMOVNZ result) has its node
// (slot 17, +0x88) hung under the tail (node parent +0x08 = tail, tail child +0x10 =
// node); the FIRST one's slot 16 (+0x80, GetDefaultState) goes to pendingState
// (+0x52) as it is. The owner and every component are read unguarded, and the count
// is re-read after each component (0x14012b796).
//
// The templates order their controllers for this rule: WoodHouse / TownHall /
// TradingPost list `cleararea` first (default 0, which ClearArea owns: 0 -> {1, 3}
// -> 2), StorageYard lists `absorbresource` first. A controller whose default no
// node owns lands on the root's empty state 0 (RootController below).
//
// (D.4 RESIDUE, row 1291: this replaced the tree's lazy linker, which ran from
// Update / Tick / OnAdded as well and kept looking past a zero default and past an
// out-of-order DestroyComponent. Those rules were measured while controllers were
// inert and the link ran on a LOADED machine; every controller is live now (all
// 23 answer GetController with their member node), Startup never runs on a load
// (ComponentEntity::Persist builds the components; Slot11 relinks), and the
// start-town +0x52 stamp is a post-RegisterEntity fallback in starttown.cpp.)
void StateMachineComponent::Startup() {
    if constexpr (SmMemo::kEnabled) stateMachine.tickMemo = SmMemo::kNone;   // NOT RETAIL: SMMEMO, the chain is linked
    Controller* tail = &_controller;
    bool gotDefault = false;
    for (int i = 0; i < _entity->_components._size; i++) {
        Component* comp = _entity->_components[i].Get();
        if (!comp->IsDerivedFrom(ControllerComponent::TypeId())) continue;   // import 0x140152a98
        ControllerComponent* controller = static_cast<ControllerComponent*>(comp);
        if (!gotDefault) {
            stateMachine.pendingState = static_cast<int8_t>(controller->GetDefaultState());
            gotDefault = true;
        }
        Controller* node = controller->GetController();
        node->_parent = tail;
        tail->_child = node;
        tail = node;
    }
}
// ------------------------------------------------------------------
// [6] OnAdded 0x14012b660: StateMachine::Tick(sm, &Time(0.0f), 0, 0). The one-shot
// register-time tick: Tick's outer loop runs while a pending state is set, so an
// enter handler that requests a transition is serviced inside the same call (the
// start town's instant build: 1 -> 8 -> 2 before RegisterEntity returns).
void StateMachineComponent::OnAdded() {
    System::Time zeroTime{0};
    RtSystem_Time_Ctor(&zeroTime, 0.0f);   // import 0x140152b00
    stateMachine.Tick(&zeroTime, 0, 0);
}
// ------------------------------------------------------------------
// [8] Tick 0x14012b640: StateMachine::Tick(sm, time, 0, 1), from
// EntityList::TreeNodeHandler; useAlt 0 runs the node's slot 6 (+0x30) column.
//
// NOT RETAIL, IDENTICAL effects: SMMEMO (Component/sm_memo.h, docs/extensions/handins/
// SMMEMO.md). When the four state bytes equal the memo the last full tick of this machine
// left (a tick whose run phase called no trans30 handler), the full tick would walk the
// same nodes to the same null handlers and write nothing: it is skipped. One compare of a
// dword on the line the full tick reads first. A miss runs the full tick and re-records.
// COMPPROF=4 (g_verifyOn): a hit runs the full tick anyway and the SmTickNoop shadow
// checks that it called no handler, kept the four bytes and kept `current`.
// The two full-tick arms are out of line so the hit path stays a leaf (no frame).
static inline uint32_t SmStateWord(const StateMachine& m) {
    return SmMemo::StateWord(m.currentInput, m.activeState, m.pendingState, m.transitionState);
}
// A miss: the full tick, then the memo it leaves (kNone when it called a trans30 handler).
static __attribute__((noinline)) void SmMemoFullTick(StateMachine& m, const System::Time* t) {
    const uint32_t calls0 = SmMemo::g_trans30Calls;
    m.Tick(t, 0, 1);
    m.tickMemo = SmMemo::Record(calls0, SmMemo::g_trans30Calls, SmStateWord(m));
}
// A hit under COMPPROF=4: the full tick anyway, and the SmTickNoop check of the claim.
static __attribute__((noinline)) void SmMemoShadowTick(StateMachine& m, const System::Time* t, uint32_t word) {
    const uint32_t calls0 = SmMemo::g_trans30Calls;
    const ControllerComponent::Controller* current0 = m.current;
    m.Tick(t, 0, 1);
    const uint32_t after = SmStateWord(m);
    const long long diff = SmMemo::ShadowDiff(calls0, SmMemo::g_trans30Calls, word, after, current0, m.current);
    StressHarness_Verify("SmTickNoop", diff == 0, diff);
    m.tickMemo = SmMemo::Record(calls0, SmMemo::g_trans30Calls, after);   // the same memo when the claim held
}
void StateMachineComponent::Tick(const System::Time* t) {
    if constexpr (SmMemo::kEnabled) {
        const uint32_t word = SmStateWord(stateMachine);
        if (SmMemo::Hit(word, stateMachine.tickMemo)) {
            ++SmMemo::g_skipped;
            if (g_verifyOn) SmMemoShadowTick(stateMachine, t, word);
            return;
        }
        SmMemoFullTick(stateMachine, t);
        return;
    }
    stateMachine.Tick(t, 0, 1);
}
// [9] Update 0x14012b620: StateMachine::Tick(sm, time, 1, 1), from the EntityList
// bucket round-robin; useAlt 1 runs the node's slot 7 (+0x38) column.
void StateMachineComponent::Update(const System::Time* time) {
    stateMachine.Tick(time, 1, 1);
}
// [11] 0x14012b7d0 — the post-load relink and resume (RelinkAndResume above).
// It needs the ctor's `current` = the root (0x14012ba86).
void StateMachineComponent::Slot11() { RelinkAndResume(); }

// The root node's 9 slots (member table 0x1401644d0, bodies 0x140038630..0x140038800;
// no Ghidra function is defined there, read from the bytes). They are the
// state-table template instance over StateMachineComponent's own static table
// (0x1401dc8b0: 16 entries of 0xf8, count 0x1401dd830, "set up" flag 0x1401dd838).
// The static initializer 0x14014fa70 (CRT init table 0x1401536b0) appends ONE
// zeroed entry (0x140038510) and sets the flag, so the table always holds exactly
// one state: id 0, no Enter / Update / Tick / TickAlt handler (+0x08 / +0x20 /
// +0x10 / +0x18 null), no transition (+0xa8 count 0), no event (+0xf0 count 0).
// Each slot below is what the template body answers for that one entry.

// [0] 0x140038630: scan the ids (+0x00) for `stateId`; the index, or -1.
int StateMachineComponent::RootController::FindState(int stateId) {
    return stateId == 0 ? 0 : -1;
}
// [1] 0x140038660: scan the state's transitions for *desired; none: *desired = -1,
// return -1 (0x1400386a3 / 0x1400386aa).
int StateMachineComponent::RootController::FindTransition(int, int* desiredStateId) {
    *desiredStateId = -1;
    return -1;
}
// [2] 0x1400386c0: no event entry matches -> 0.
char StateMachineComponent::RootController::PostEvent(StateMachine*, int, int) { return 0; }
// [3] 0x140038710 / [4] 0x140038740 / [6] 0x1400387a0 / [7] 0x1400387d0: a null
// handler -> 0 (`XOR AL,AL`).
char StateMachineComponent::RootController::Enter(StateMachine*, int) { return 0; }
char StateMachineComponent::RootController::Update(StateMachine*, int) { return 0; }
// [5] 0x140038770: a null transition handler -> nothing.
void StateMachineComponent::RootController::OnTransition(StateMachine*, int, int) {}
char StateMachineComponent::RootController::Tick(StateMachine*, const System::Time*, int) { return 0; }
char StateMachineComponent::RootController::TickAlt(StateMachine*, const System::Time*, int) { return 0; }
// [8] 0x140038800: the "set up" flag != 0 -> 1.
char StateMachineComponent::RootController::HasStates() { return 1; }

// create 0x1400382d0: MemAlloc(0x58, 8), then the ctor.
::Object::Base* StateMachineComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StateMachineComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StateMachineComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* StateMachineComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StateMachineComponent());
}

DEFINE_EXE_TYPE(StateMachineComponent, "Component")
