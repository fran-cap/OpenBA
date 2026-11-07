#pragma once
// src/game/Component/controller_state_table.h
// Phase 3 / Keystone D step 3 — REAL per-controller state-type table + the real
// generic state-node dispatcher virtuals, for the standalone exe.
//
// Each building ControllerComponent (Build, StorageLocation, …) owns a private
// "state-type table": an array of 0xf8-byte entries describing the states its
// embedded state node can be in. In the original exe this table lives in the
// component's .data (e.g. BuildComponent: array DAT_1401d3ca0, count DAT_1401d4c20)
// and is filled at startup by `<X>Description_Setup -> helper -> sub_f500/sub_f620`.
// The node's *secondary* vtable methods (Step/Enter/Update/OnState/Transition) are
// tiny GENERIC dispatchers that index this table — they are byte-identical across
// controllers except for the hard-coded table base.
//
// In standalone the exe .data table is not mapped, so we allocate our own table
// (one StateTable per controller-type, via the Dispatcher<Tag> template) and a set
// of ported generic dispatchers that read it. This replaces the crash-safe stub
// node vtable (controller_live_base.cpp) with the REAL mechanism: Step performs the
// real state-id->index search, and Enter/Update/OnState/Transition dispatch through
// the table to per-state handlers.
//
// ── Table entry layout (0xf8 bytes, recovered from BuildComponent_Controller_*
//    dispatchers 0x14002f6d0 / 0x140002b60 / 0x140002b90 / 0x140002bc0 /
//    0x140002bf0 / 0x140002c20 and the populators sub_f500/sub_f620): ────────────
//   +0x00 int    id              (the state-id Step matches against)
//   +0x08 void*  enterHandler    (node vtable slot +0x18 dispatcher tail-calls it)
//   +0x10 void*  trans30Handler  (node vtable slot +0x30 dispatcher)
//   +0x18 void*  trans38Handler  (node vtable slot +0x38 dispatcher; = setup fn)
//   +0x20 void*  updateHandler   (node vtable slot +0x20 dispatcher)
//   +0x28 Trans  transitions[8]  (per-state transition list, {targetId, handler})
//   +0xa8 int    transitionCount
//   …pad to 0xf8
//
// ── Node-interface ABI (recovered from StateMachine::Tick 0x1400a9660 disasm; the
//    state index is carried in R8D / R9D — Ghidra DROPS these in the decompile): ─
//   slot 0 (+0x00) Step(node,     int input)              -> int index / -1
//   slot 3 (+0x18) Enter(node, sm, int idx)              -> char
//   slot 4 (+0x20) Update(node, sm, int idx)             -> char
//   slot 5 (+0x28) OnState(node, sm, int actIdx, int trIdx) -> void
//   slot 6 (+0x30) Transition(node, sm, const Time* time, int idx) -> char
//   slot 7 (+0x38) Transition(node, sm, const Time* time, int idx) -> char
// Each handler is tail-called as `handler(*(node+0x18))` (the dispatcher does
// `MOV RCX,[RCX+0x18]; JMP handler`); a null handler is a graceful no-op (return 0).
//
// Handlers are null in standalone until the per-state behaviour functions are
// ported (the deep next layer — e.g. BuildDescription_helper_8290 is a large
// GameWorld/ResourceGather routine). With null handlers the SM still runs the REAL
// table-driven structure (Step returns real indices, transitions are validated
// against the real graph), it just performs no per-state work yet.

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/ControllerComponent.h"   // ControllerComponent::Controller (the node)
#include "Component/StateMachineComponent.h" // StateMachine (the machine the handlers receive)
#include "Component/sm_memo.h"               // SMMEMO: the trans30 handler count
#include "Component/BuildingState.h"         // BuildingState / BuildingEvent (the table ids)
#include <type_traits>

namespace ControllerLive {

// ── The handler types. `Owner` is the class that embeds the controller (its
//    `Tag::Owner`): every dispatcher tail-calls with `MOV RCX,[node+0x18]`, and the
//    node's +0x18 is the owning component (ControllerComponent.h). What else
//    survives the tail-JMP is read from BuildComponent's dispatchers:
//      enter   0x140002b60  R8 <- handler, JMP R8          -> (owner, sm)        char
//      update  0x140002b90  R8 <- handler, JMP R8          -> (owner, sm)        char
//      trans30 0x140002bf0  R9 <- handler, JMP R9          -> (owner, sm, time)  char
//      trans38 0x140002c20  R9 <- handler, JMP R9          -> (owner, sm, time)  char
//      event   0x140009da0  JMP [RAX+8]                    -> (owner, sm)        char
//      transition 0x140002bc0: the null arm is a bare RET (no XOR AL), the slot is
//        `void OnTransition` -> void; the tree calls it with the owner alone.
//    `time` is the System::Time* StateMachine::Tick forwards (RDX = &time, 0x14012b67b).
template <class Owner>
struct StateHandlers {
    using Enter      = char (*)(Owner* owner, StateMachine* sm);
    using Update     = char (*)(Owner* owner, StateMachine* sm);
    using Tick       = char (*)(Owner* owner, StateMachine* sm, const System::Time* time);
    using Transition = void (*)(Owner* owner);
    using Event      = char (*)(Owner* owner, StateMachine* sm);
};

#pragma pack(push, 1)
template <class Owner>
struct TransEntry {        // 0x10 bytes
    int   targetId;        // +0x00
    int   _pad04;          // +0x04
    typename StateHandlers<Owner>::Transition handler;   // +0x08
};
template <class Owner>
struct EventEntry {        // 0x10 bytes — same shape as TransEntry
    int   eventId;         // +0x00
    int   _pad04;          // +0x04
    typename StateHandlers<Owner>::Event handler;        // +0x08
};
template <class Owner>
struct StateEntry {        // 0xf8 bytes
    int        id;             // +0x00
    int        _pad04;         // +0x04
    typename StateHandlers<Owner>::Enter  enterHandler;   // +0x08
    typename StateHandlers<Owner>::Tick   trans30Handler; // +0x10
    typename StateHandlers<Owner>::Tick   trans38Handler; // +0x18
    typename StateHandlers<Owner>::Update updateHandler;  // +0x20
    TransEntry<Owner> transitions[8]; // +0x28 .. +0xa8
    int        transitionCount;// +0xa8
    uint8_t    _pad_ac[4];     // +0xac
    EventEntry<Owner> events[4];      // +0xb0 .. +0xf0   (see the PostEvent note below)
    int        eventCount;     // +0xf0
    uint8_t    _tail[0xf8 - 0xf4];
};
#pragma pack(pop)
// The layout does not depend on Owner (every handler is one pointer); asserted on
// the base class.
static_assert(sizeof(TransEntry<::Component>) == 0x10, "TransEntry must be 0x10");
static_assert(offsetof(TransEntry<::Component>, handler) == 0x08, "transition handler at +0x08");
static_assert(sizeof(EventEntry<::Component>) == 0x10, "EventEntry must be 0x10");
static_assert(offsetof(EventEntry<::Component>, handler) == 0x08, "event handler at +0x08");
static_assert(sizeof(StateEntry<::Component>) == 0xf8, "StateEntry must be 0xf8");
static_assert(offsetof(StateEntry<::Component>, id) == 0x00, "id must sit at +0x00");
static_assert(offsetof(StateEntry<::Component>, enterHandler) == 0x08, "enter at +0x08");
static_assert(offsetof(StateEntry<::Component>, trans30Handler) == 0x10, "trans30 at +0x10");
static_assert(offsetof(StateEntry<::Component>, trans38Handler) == 0x18, "trans38 at +0x18");
static_assert(offsetof(StateEntry<::Component>, updateHandler) == 0x20, "update at +0x20");
static_assert(offsetof(StateEntry<::Component>, transitions) == 0x28, "transitions must sit at +0x28");
static_assert(offsetof(StateEntry<::Component>, transitionCount) == 0xa8, "transitionCount must sit at +0xa8");
static_assert(offsetof(StateEntry<::Component>, events) == 0xb0, "events must sit at +0xb0");
static_assert(offsetof(StateEntry<::Component>, eventCount) == 0xf0, "eventCount must sit at +0xf0");

template <class Owner>
struct StateTable {
    StateEntry<Owner> entries[16];
    int        count;
};

// ── Populate API — replicates sub_f500 (register state) / sub_f620 (add a
//    transition to the most-recently-registered state). ──────────────────────────
template <class Owner>
inline void TableRegisterState(StateTable<Owner>& t, int id,
                               typename StateHandlers<Owner>::Enter enterH,
                               typename StateHandlers<Owner>::Tick trans30H,
                               typename StateHandlers<Owner>::Tick trans38H,
                               typename StateHandlers<Owner>::Update updateH) {
    if (t.count >= 16) return;
    StateEntry<Owner>& e = t.entries[t.count];
    e.id = id;
    e.enterHandler = enterH;
    e.trans30Handler = trans30H;
    e.trans38Handler = trans38H;
    e.updateHandler = updateH;
    e.transitionCount = 0;
    ++t.count;
}
template <class Owner>
inline void TableAddTransition(StateTable<Owner>& t, int targetId,
                               typename StateHandlers<Owner>::Transition handler) {
    if (t.count <= 0) return;
    StateEntry<Owner>& e = t.entries[t.count - 1];
    if (e.transitionCount >= 8) return;
    TransEntry<Owner>& tr = e.transitions[e.transitionCount];
    tr.targetId = targetId;
    tr.handler = handler;
    ++e.transitionCount;
}
// Replicates retail's AddEvent (0x140009c70): append {eventId, handler} to the
// most-recently-registered state's event list. The panel's Command (component vtable
// idx14) posts node events 3/4/5 through node slot 2, and without this the post finds
// an empty list and the button click dies silently.
template <class Owner>
inline void TableAddEvent(StateTable<Owner>& t, int eventId,
                          typename StateHandlers<Owner>::Event handler) {
    if (t.count <= 0) return;
    StateEntry<Owner>& e = t.entries[t.count - 1];
    if (e.eventCount >= 4) return;   // retail's capacity too: (0xf0-0xb0)/0x10 == 4
    EventEntry<Owner>& ev = e.events[e.eventCount];
    ev.eventId = eventId;
    ev.handler = handler;
    ++e.eventCount;
}
// Enum spellings of the three populators (BuildingState / BuildingEvent, or a Boat /
// Nomad enum): forward the retail value to the int versions above. A plain int argument
// still picks the int version (the template drops out for a non-enum E).
template <class Owner, class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
inline void TableRegisterState(StateTable<Owner>& t, E id,
                               typename StateHandlers<Owner>::Enter enterH,
                               typename StateHandlers<Owner>::Tick trans30H,
                               typename StateHandlers<Owner>::Tick trans38H,
                               typename StateHandlers<Owner>::Update updateH) {
    TableRegisterState(t, static_cast<int>(id), enterH, trans30H, trans38H, updateH);
}
template <class Owner, class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
inline void TableAddTransition(StateTable<Owner>& t, E targetId,
                               typename StateHandlers<Owner>::Transition handler) {
    TableAddTransition(t, static_cast<int>(targetId), handler);
}
template <class Owner, class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
inline void TableAddEvent(StateTable<Owner>& t, E eventId,
                          typename StateHandlers<Owner>::Event handler) {
    TableAddEvent(t, static_cast<int>(eventId), handler);
}
// The real Step search: first entry whose id == input, else -1 (orig 0x14002f6d0).
template <class Owner>
inline int TableFindIndex(const StateTable<Owner>& t, int input) {
    for (int i = 0; i < t.count; ++i)
        if (t.entries[i].id == input) return i;
    return -1;
}

// ── Per-controller dispatcher + table, generated once per Tag type. Mirrors the
//    exe's per-controller dispatcher functions (each hard-codes its own table). ───
template <class Tag>
struct Dispatcher {
    // The class that embeds this controller (the node's +0x18 points at it).
    using Owner = typename Tag::Owner;
    static StateTable<Owner> table;
    // (The hand-built 16-slot `vtbl` + `Vtable()` + `Stub` went at ENDGAME B.3.9
    // EXA: no reader — every controller member is a TableController<Tag> with a
    // compiler table whose slots call the static handlers below.)

    // slot 0 (+0x00): Step(node /*ignored*/, input) -> index / -1
    static int Step(::ControllerComponent::Controller* /*node*/, int input) {
        int idx = TableFindIndex(table, input);
        return idx;
    }
    // node+0x18 (Controller::_owner) holds the component the handler expects: the
    // Owner whose ctor built this controller with `this` (a downcast in the
    // Component hierarchy, at offset 0).
    static Owner* NodeData(::ControllerComponent::Controller* node) {
        ::Component* const owningComponent = node->_owner;
        return static_cast<Owner*>(owningComponent);
    }
    // slot 3 (+0x18): Enter — dispatch table[idx].enterHandler(node+0x18, sm)
    // The exe enter dispatcher tail-calls `MOV RCX,[node+0x18]; JMP handler`, so a real
    // handler receives RCX=controller-data AND RDX=sm (preserved through the tail-JMP,
    // bug-class #6). Forward sm as the 2nd arg; 1-arg handlers ignore the extra RDX.
    static char Enter(::ControllerComponent::Controller* node, StateMachine* sm, int idx) {
        if (idx < 0 || idx >= table.count) return 0;
        typename StateHandlers<Owner>::Enter h = table.entries[idx].enterHandler;
        if (!h) return 0;
        return h(NodeData(node), sm);
    }
    // slot 1 (+0x08): RequestTransition(node, curIdx, int* pDesiredStateId) -> transIdx / -1
    // The state handlers call this to request a transition: it searches the CURRENT
    // state's registered transition list for one whose targetId == *pDesired and returns
    // that transition's index (which StateMachine::Tick later feeds to OnState/slot28 to
    // run the transition action). Mirrors the exe node slot +0x08 dispatcher (Well's
    // 0x1400100c0, read 2026-09-26): on a MISS retail stores -1 to *pDesired
    // (0x140010103 `MOV dword [R10],-1; OR EAX,-1; RET`) — B38_RULES D9 #9. A hit
    // leaves *pDesired alone. The idx/null bounds rejection is the tree's (retail has
    // none); it returns -1 without touching *pDesired, as before.
    static int RequestTransition(::ControllerComponent::Controller* /*node*/, int curIdx, int* pDesired) {
        if (!pDesired || curIdx < 0 || curIdx >= table.count) return -1;
        const StateEntry<Owner>& e = table.entries[curIdx];
        int desired = *pDesired;
        for (int i = 0; i < e.transitionCount; ++i)
            if (e.transitions[i].targetId == desired) return i;
        *pDesired = -1;
        return -1;
    }
    // slot 8 (+0x40): retail (Well 0x140010260) `XOR EAX,EAX; CMP [count],EAX;
    // SETNZ AL; RET` — true iff the class's state table has any state (D9 #9).
    static char HasStates(::ControllerComponent::Controller* /*node*/) { return table.count != 0 ? 1 : 0; }
    // slot 4 (+0x20): Update — dispatch table[idx].updateHandler(node+0x18, sm)
    // Like Enter, the exe update dispatcher tail-calls `MOV RCX,[node+0x18]; JMP handler`
    // preserving RDX=sm (bug-class #6), so real update handlers (e.g. sub_81e0/sub_76e0)
    // receive (controller-data, sm) and use sm to request state transitions.
    static char Update(::ControllerComponent::Controller* node, StateMachine* sm, int idx) {
        if (idx < 0 || idx >= table.count) return 0;
        typename StateHandlers<Owner>::Update h = table.entries[idx].updateHandler;
        if (!h) return 0;
        return h(NodeData(node), sm);
    }
    // slot 5 (+0x28): OnState — table[actIdx].transitions[trIdx].handler(node+0x18)
    static void OnState(::ControllerComponent::Controller* node, StateMachine* /*sm*/, int actIdx, int trIdx) {
        if (actIdx < 0 || actIdx >= table.count) return;
        if (trIdx < 0 || trIdx >= 8) return;
        typename StateHandlers<Owner>::Transition h = table.entries[actIdx].transitions[trIdx].handler;
        if (!h) return;
        h(NodeData(node));
    }
    // slot 6 (+0x30): Transition — dispatch table[idx].trans30Handler(node+0x18, sm, time)
    // ⚠ THIS IS ALSO A PER-FRAME HANDLER, and it is the ONLY per-frame column some
    // controllers use. StateMachine::Tick's run phase picks +0x38 when its `useAlt`
    // argument is 1 and +0x30 when it is 0 (0x1400a97b1 vs 0x1400a97e3), and retail
    // drives BOTH every frame through two different StateMachineComponent vtable slots:
    // idx 9 = 0x14012b620 Tick(...,1,1) from the EntityList bucket round-robin, and
    // idx 8 = 0x14012b640 Tick(...,0,1) from EntityList::TreeNodeHandler. BoatComponent's
    // state table (0x1400468a0) registers NO trans38 handler at all — every per-frame
    // handler it owns is in this column.
    //
    // Same tail-JMP ABI as Trans38: the exe dispatcher does `MOV RCX,[RCX+0x18]; JMP h`
    // with RDX=sm and R8=time passing through, so handlers receive (data, sm, time).
    // Forwarding them is required, not cosmetic — 0x140045250 and 0x140044ff0 both read
    // sm to request their state transition. Handlers that take fewer arguments simply
    // ignore the extra registers.
    static char Trans30(::ControllerComponent::Controller* node, StateMachine* sm, const System::Time* time, int idx) {
        if (idx < 0 || idx >= table.count) return 0;
        typename StateHandlers<Owner>::Tick h = table.entries[idx].trans30Handler;
        if (!h) return 0;
        // NOT RETAIL: SMMEMO (Component/sm_memo.h) counts the handlers this column calls;
        // a slot-8 tick that leaves the count unchanged called none. Only reached by a
        // table with a trans30 handler (BoatComponent's).
        if constexpr (SmMemo::kEnabled) ++SmMemo::g_trans30Calls;
        return h(NodeData(node), sm, time);
    }
    // slot 7 (+0x38): DrawAlt — dispatch table[idx].trans38Handler(node+0x18, sm, time)
    // ⚠ This is the PER-FRAME handler. StateMachineComponent::Update calls
    // StateMachine::Tick(sm, time, useAltDraw=1, doDraw=1) every frame, and Tick's
    // DRAW phase (useAltDraw=1) invokes node slot +0x38 every frame — whereas the
    // UPDATE phase (slot +0x20) only runs when a transition is pending. So the real
    // per-tick state work (BuildDescription sub_76e0/sub_81e0 proportional build +
    // transition-request) lives in the trans38Handler.
    // The exe slot-7 dispatcher (e.g. ResidenceComponent's 0x140037a00) tail-JMPs
    // with RCX=[node+0x18] and RDX/R8 passing through — so handlers receive
    // (data, sm, Time* dt). Handlers that don't need dt (2-arg ports like the
    // BuildComponent state work) simply ignore R8; the ResidenceComponent heat
    // tick (0x140129970) dereferences it for its heat countdown.
    static char Trans38(::ControllerComponent::Controller* node, StateMachine* sm, const System::Time* time, int idx) {
        if (idx < 0 || idx >= table.count) return 0;
        typename StateHandlers<Owner>::Tick h = table.entries[idx].trans38Handler;
        if (!h) return 0;
        return h(NodeData(node), sm, time);
    }
    // slot 2 (+0x10): PostEvent(node, sm, int curIdx, int eventId) -> char
    // Ported 1:1 from the retail dispatcher at 0x140009da0 (orchard node vtable
    // 0x14015a130 index 2), read as disassembly rather than trusting the vtable's
    // Ghidra names, which are wrong by position for this whole family:
    //     MOVSXD RAX,R8D ; LEA R8,[table] ; IMUL RAX,RAX,0xf8 ; ADD RAX,R8
    //     MOVSXD R10,[RAX+0xf0]        <- eventCount, INLINE at +0xf0
    //     ADD RAX,0xb0                 <- event array base
    //     CMP [RAX],R9D / ADD RAX,0x10 <- linear scan, stride 0x10, id at +0x00
    //     hit:  MOV RCX,[RCX+0x18] ; JMP [RAX+0x8]   <- tail-call, RDX=sm survives
    //     miss: XOR AL,AL ; RET
    // So a handler is called as handler(NodeData(node), sm) and returns char — the
    // same shape as Enter/Update. A miss is a graceful 0, exactly as retail.
    static char PostEvent(::ControllerComponent::Controller* node, StateMachine* sm, int curIdx, int eventId) {
        if (curIdx < 0 || curIdx >= table.count) return 0;
        const StateEntry<Owner>& e = table.entries[curIdx];
        for (int i = 0; i < e.eventCount; ++i) {
            if (e.events[i].eventId != eventId) continue;
            typename StateHandlers<Owner>::Event h = e.events[i].handler;
            if (!h) return 0;
            return h(NodeData(node), sm);
        }
        return 0;
    }
};
template <class Tag> StateTable<typename Dispatcher<Tag>::Owner> Dispatcher<Tag>::table = {};

// Request a state transition from a ported state handler, replicating the exe idiom:
//   int d=desiredStateId; int i = node->slot08(node, sm->index, &d);
//   if (i>=0) { sm[+0xb]=i (transition index); sm[+0xa]=d (pending state id); }
// The SM object layout: +0x00 cur-node, +0x09 current node-local index (int8),
// +0x0a pending state id (int8), +0x0b transition-target index (int8). StateMachine::Tick
// picks up +0x0a/+0x0b on its next pass to run the transition action and enter the target.
//
// When sm->index (+0x09) is < 0 the SM has never entered a state (a pending default is
// still undrained), so the slot-8 lookup has no current state to search from. Retail
// runs a nested StateMachine_Tick(sm, Time(0), 0, 0) first to drain the pending enter,
// then re-reads BOTH the node pointer and +0x09 before requesting — the tick can change
// the active node. Verified at two retail call sites: 0x14012ae60 (the RP pause path)
// and 0x14012b310 (ConsumeProduceAction_helper_b310). We do the same. With both flag
// args 0 the nested Tick skips its action phase — the only consumer of the time arg —
// so a plain 0 stands in for retail's Time(0.0) byte-for-byte. If the SM has nothing
// pending either, the nested Tick is a no-op, +0x09 stays negative, and the slot-8
// dispatcher bounds-rejects the request exactly as before.
//
// Row 922 (B7 RES): no null-machine and no null-node return. The inlined copies read
// [m+0x9] (0x14005a79d) and [[m]] (0x14005a7cc) with no test; the same at 0x14005a9f1 /
// 0x14005ab65 / 0x14005b24f.
inline void RequestStateTransition(::StateMachine* m, int desiredStateId) {
    if (m->activeState < 0) {
        // Retail's inlined copies (0x14005a7ab-0x14005a7c7, 0x14005a9f7-0x14005aa13): a stack
        // Time(0.0f) through the import 0x140152b00, `LEA RDX,[RSP+..]`, R8D = R9D = 0.
        System::Time t0 = {0};
        RtSystem_Time_Ctor(&t0, 0.0f);
        m->Tick(&t0, 0, 0);       // 0x1400a9660
    }
    ::ControllerComponent::Controller* node = m->current;   // re-read after the nested tick
    int curIdx = m->activeState;  // re-read after the nested tick
    int desired = desiredStateId;
    int idx = node->FindTransition(curIdx, &desired);   // node slot 1 (+0x08)
    if (idx >= 0) {
        m->transitionState = static_cast<int8_t>(idx);
        m->pendingState = static_cast<int8_t>(desired);
    }
}

// Enum spelling of RequestStateTransition (BuildingState, or a Boat / Nomad enum).
template <class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
inline void RequestStateTransition(::StateMachine* m, E desiredState) {
    RequestStateTransition(m, static_cast<int>(desiredState));
}

}  // namespace ControllerLive

// ============================================================================
// ENDGAME B.3.8 (B38_RULES.md D6) — the table-driven controller as a REAL
// member class. A converted ControllerComponent leaf embeds one of these BY
// VALUE at its retail offset instead of stamping `Dispatcher<Tag>::Vtable()`
// into raw bytes; its compiler vtable has retail's 9 slots (the hand-built
// `Dispatcher<Tag>::vtbl` has 16, slots 9-15 `Stub`, which retail does not
// have). Every override forwards to the SAME Dispatcher<Tag> static the old
// table held, so the behaviour is byte-for-byte the old table's.
// ============================================================================
namespace ControllerLive {

template <class Tag>
class TableController final : public ::ControllerComponent::Controller {
public:
    explicit TableController(::Component* owner) { _owner = owner; }
    ~TableController() = default;

    int  FindState(int stateId) override { return Dispatcher<Tag>::Step(this, stateId); }
    int  FindTransition(int stateIndex, int* desired) override {
        return Dispatcher<Tag>::RequestTransition(this, stateIndex, desired);
    }
    char PostEvent(StateMachine* sm, int stateIndex, int eventId) override {
        return Dispatcher<Tag>::PostEvent(this, sm, stateIndex, eventId);
    }
    char Enter(StateMachine* sm, int stateIndex) override { return Dispatcher<Tag>::Enter(this, sm, stateIndex); }
    char Update(StateMachine* sm, int stateIndex) override { return Dispatcher<Tag>::Update(this, sm, stateIndex); }
    void OnTransition(StateMachine* sm, int stateIndex, int transitionIndex) override {
        Dispatcher<Tag>::OnState(this, sm, stateIndex, transitionIndex);
    }
    char Tick(StateMachine* sm, const System::Time* time, int stateIndex) override {
        return Dispatcher<Tag>::Trans30(this, sm, time, stateIndex);
    }
    char TickAlt(StateMachine* sm, const System::Time* time, int stateIndex) override {
        return Dispatcher<Tag>::Trans38(this, sm, time, stateIndex);
    }
    // [8] retail (Well 0x140010260): `table count != 0` (B38_RULES D9 #9).
    char HasStates() override { return Dispatcher<Tag>::HasStates(this); }
};

}  // namespace ControllerLive
