// src/game/Component/tradecomponent_states.cpp
// TradeComponent's controller state machine — the thing that makes a trading post
// drive itself instead of waiting for the player to poke the work toggle.
//
// tradecomponent.cpp deliberately left the controller sub-object inert:
//   "DELIBERATELY NOT applied: +0x88 (the controller sub-object's own vtable) and
//    +0xa0 (its self back-pointer). ... They stay inert until someone ports the
//    controller."
// This file is that port. Node offset 0x88 and default state 2 are not guesses —
// TradeComponent's retail vtable 0x14015acd0 holds slot 16 = 0x1400026c0
// (`MOV EAX,2; RET`) and slot 17 = 0x14000dba0 (`LEA RAX,[RCX+0x88]; RET`).
//
// ── The retail table (TradeDescription_Setup -> 0x14005c650) ──────────────────────
// c650 builds the table with the same three primitives controller_state_table.h
// already models: 0x14000dd70 = register state, 0x14000de90 = add transition,
// 0x14000df40 = add event. Argument mapping read off c650's disassembly
// (dd70's 5th argument arrives on the stack at [RSP+0x20], which the decompiler
// shows as `in_R9` noise — it is the trans38/per-frame handler, not R9):
//
//   dd70(2,    0x14005abd0, 0,           _, 0x14005aa60)
//     de90(3, 0x14005b160)   de90(0x12, 0)
//   dd70(0x12, 0x14005aa50, 0,           _, 0)
//     de90(3, 0x14005b160)   de90(0x13, 0)   de90(2, 0)
//     df40(9, 0x14005a850)   df40(0xb, 0x14005a9c0)
//   dd70(0x13, 0x14005a800, 0x14005a6d0, _, 0x14005a780)
//     de90(3, 0x14005b160)   de90(2, 0)
//     df40(0xa, 0x14005b240)
//
// so, in StateEntry terms (+0x08 enter, +0x18 trans38, +0x20 update):
//
//   state 2    IDLE / waiting for the next merchant
//     enter   abd0  post a TradeWork if the post is accepting, then arm the timer
//     trans38 aa60  per-frame: count the timer down, spawn the merchant, -> 0x12
//   state 0x12 MERCHANT INBOUND (boat en route)
//     enter   aa50  `MOV AL,1; RET` — nothing to do
//     event 9    a850  boat docked: write the event-log line, -> 0x13
//     event 0xb  a9c0  boat lost/despawned: drop the merchant ref, -> 2
//   state 0x13 MERCHANT DOCKED (the panel can trade)
//     enter   a800  build the merchant's manifest, arm the stay timer, auto-trade
//     trans38 a780  per-frame: stay timer expired and not mid-trade -> 2
//     update  a6d0  leaving: auto-trade, release the boat, clear the manifest
//                   (both in tradecomponent_dock.cpp — layer 3)
//     event 0xa  b240  player dismissed the merchant -> 2 (inline SM tick)
//   every state carries transition 3 -> b160, the shared teardown.
//
// ── WHAT IS PORTED HERE, AND WHAT IS NOT ─────────────────────────────────────────
// This is layer 1 of the wave: the table, the node wiring, and the state-2 self-drive
// path. Ported and wired: abd0, aa50, a780, a9c0, plus the leaves abd0 needs
// (0x14005b450 timer arm, 0x14005b4f0 tier pick, 0x1400f6ea0 TradeWork ctor).
//
// NOT ported, and therefore registered as NULL handlers (which the dispatcher treats
// as a graceful no-op, exactly as retail treats an unregistered slot):
//   0x14005aa60  state-2 per-frame merchant spawn — blocked on 0x1400456e0, the river
//                route builder (~1.5 KB, two 4096-int stack maps). Without it a boat
//                can be created but not routed, so the whole spawn is held back rather
//                than half-done.
//   0x14005b160  shared teardown — also calls 0x1400456e0 (with a null ref, its
//                "release the boat" branch).
//   0x14005b240  dismiss event — retail inlines the whole of StateMachine::Tick here.
//   (0x14005a850, the dock event-log line, WAS in this list — ported 2026-08-22 once the
//    boat state machine gave it a caller; it reuses eventlog.cpp's AddEvent.)
//
// So with this layer alone the machine runs, sits in state 2, and re-posts trade work
// on its own; it does not yet advance to 0x12/0x13. That is a real behaviour change
// (today NOTHING posts trade work except the player clicking the toggle) and a
// deliberately visible stopping point, not a silent partial.
//
// GATED: BANISHED_LIVE_TRADE_CTRL. GRADUATED 2026-08-22 into the default-on table, so it is
// now a KILL-SWITCH (=0), not an opt-in. Proven end to end: timer -> merchant spawn ->
// route -> dock -> manifest -> auto-trade delivering goods.

#include "Component/controller_state_table.h"
#include "Component/TradeComponent.h"
#include <new>
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/TradeDescription.h"   // ENDGAME B.3.9 TR: TradeDescription::TypeId()
#include "Component/ComponentDescription.h"
#include "Component/BoatComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/CitizenBehavior.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "Work/TradeWork.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/MemoryAllocator.h"
#include "system/Value.h"               // RtSystem_Time_Ctor (import slot 0x140152b00)
#include "Component/exe_bodies.h"       // 0x14012ec60 / 0x140002cc0
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete
#include "Game/EventLog.h"   // EventLog::AddEvent (0x140095580)

// TradeCtrlTag lives in TradeComponent.h (the member type needs it).
using TradeDispatch = ::ControllerLive::Dispatcher<TradeCtrlTag>;

namespace {

// ── Leaves, by retail address ────────────────────────────────────────────────────
//   0x140002e10 FindComponent<WorkPlaceComponent>   0x140003050 FindComponent<WorkComponent>
//   0x14012ead0 WorkComponent::IsWorkPosted         0x14012ec60 CitizenAI_WorkScheduler_Post
//   import slot 0x140152b00 RtSystem_Time_Ctor
// Layer 2 (merchant spawn / teardown) leaves:
//   0x1400c3720 ComponentEntity_CtorFromDescription  0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x14000eb20 FindComponent<BoatComponent>         0x1400456e0 BoatComponent::SetDestination
//   0x1400a9660 StateMachine::Tick                   0x140002cc0 Array<T>::Remove

// The TradeDescription off +0x18, cast exactly as retail does it.
// The deref of the result is left UNGUARDED below, as retail leaves it: retail
// relies on the cast succeeding, and a guard here would skip the behaviour rather
// than restore it. If this ever faults, the description on +0x18 is the bug.
TradeDescription* TradeDesc(const TradeComponent* comp) {
    Description* raw = comp->_description;
    return Object::IsDerivedFromNullSafe(raw, TradeDescription::TypeId())
        ? static_cast<TradeDescription*>(raw) : nullptr;
}


// Rate-limited outcome trace for the state-2 enter. Prints on EVERY branch, including
// the ones that post nothing, so a silent log means the handler never ran rather than
// that it ran and succeeded.
void TradeCtrlReport(const TradeComponent* comp, bool tried, const void* work, bool schedHasWork) {
    static int n = 0;
    if (n >= 16) return;
    ++n;
    std::fprintf(stderr,
        "[TradeCtrl] state2 enter comp=%p accepting=%s work=%p schedHasWork=%s "
        "timer(+0x78)=%lld\n",
        static_cast<const void*>(comp), tried ? "yes" : "no", work,
        schedHasWork ? "yes" : "no",
        static_cast<long long>(comp->merchantTimer._ticks));
    std::fflush(stderr);
}

} // namespace

// ============================================================
// 0x14005b4f0 — pick the merchant tier for the town's current size
// ============================================================
// Walks the description's tier array (desc+0x58, stride 0x14) and returns the index
// of the first tier whose threshold (+0x00) is above the town's current population
// figure at *(gameWorld+0xf0)+0xa8; if none is, the last tier. A negative population
// short-circuits to the current index, which is retail's behaviour on an unbuilt town.
// LEFT: the town manager (GameUpdateLoop::m_citizenList) is void* (GAME-a).
int TradeComponent::PickMerchantTier() {
    TradeDescription* desc = TradeDesc(this);

    const int count = desc->_priceCount;
    const TradeTier* tiers = desc->_prices;
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        const CitizenBehavior* townState = GameUpdateLoop::_singleton->m_citizenList;
        const int pop = townState->citizens._size;   // +0xa8
        if (pop >= 0 && pop < tiers[i].populationLimit)
            return idx;
        ++idx;
    }
    return count - 1;
}

// ============================================================
// 0x14005b450 — arm the countdown to the next merchant
// ============================================================
// +0x78 = Time( (RandFloatTol() * tier.tolerance + tier.base) * 300.0f )
// The tier record is 0x14 bytes: +0x00 population threshold, +0x04 base delay,
// +0x08 tolerance. DAT_140172234 is the literal 300.0f (0x43960000).
void TradeComponent::ArmMerchantTimer() {
    TradeDescription* desc = TradeDesc(this);

    const int tier = PickMerchantTier();
    const TradeTier& rec = desc->_prices[tier];

    System::Random* rng = GameUpdateLoop::_singleton->m_random;
    const float tol = rng->RandFloatTol();
    float seconds = (tol * rec.delayTolerance + rec.delayBase) * 300.0f;

    System::Time t = {0};
    merchantTimer = *RtSystem_Time_Ctor(&t, seconds);

    // OUTCOME: the armed countdown read back off the component, not the seconds we
    // computed. A zero here means the arm silently did nothing, which is exactly the
    // state that would make a post sit idle forever while looking configured.
    static int reported = 0;
    if (reported < 8) {
        ++reported;
        std::fprintf(stderr,
            "[TradeCtrl] arm timer comp=%p tier=%d base=%.2f tol=%.2f -> %.1fs "
            "timer(+0x78)=%lld\n",
            static_cast<void*>(this), tier, rec.delayBase, rec.delayTolerance,
            seconds, static_cast<long long>(merchantTimer._ticks));
        std::fflush(stderr);
    }
}

// ============================================================
// state 2 ENTER — 0x14005abd0
// ============================================================
// THE self-drive step. On entering idle: if the scheduler is not already holding a
// work slot and the workplace is accepting, allocate a TradeWork and post it. Then
// arm the merchant countdown. Today nothing does this without the player clicking
// the work toggle (TradeComponent::Command), which is why a post that has never been
// toggled never fetches anything.
//
// Returns 0 in AL — retail's tail is `CALL 0x14005b450; XOR AL,AL; RET`.
char TradeComponent::EnterIdle() {
    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10

    WorkComponent* sched = _entity->FindComponent<WorkComponent>();          // 0x140003050

    if (sched->IsWorkPosted()) {          // already holds work — nothing to post
        TradeCtrlReport(this, false, nullptr, true);
        ArmMerchantTimer();
        return 0;
    }

    if (wpc->isAccepting) {   // workplace accepting
        TradeWork* work = nullptr;
        // MemAlloc(0x58, 8) + the from-entity TradeWork ctor 0x1400f6ea0: the default
        // ctor (Work/TradeWork.h) and Work::InitFromEntity (0x1400f72e0's body).
        void* mem = System::MemoryAllocator::MemAlloc(0x58, 8);
        if (mem) {
            ComponentEntity* entityIBP[2] = {_entity.Get(), nullptr};
            work = new (mem) TradeWork();
            work->InitFromEntity(entityIBP);
        }
        Object::InternalPtr<Work> workIBP(work);

        WorkComponent* sched2 = _entity->FindComponent<WorkComponent>();
        sched2->PostWork(1, '\0', &workIBP);

        // OUTCOME, not intent: report whether a TradeWork was really constructed and
        // whether the scheduler is holding one AFTER the post. "accepting was set" and
        // "a work object exists" are different claims, and only the readback proves the
        // second. See reference_diagnostics_must_report_outcomes.
        TradeCtrlReport(this, true, work, sched2->IsWorkPosted());
    } else {
        TradeCtrlReport(this, false, nullptr, false);
    }

    ArmMerchantTimer();
    return 0;
}

// ============================================================
// state 0x12 ENTER — 0x14005aa50
// ============================================================
// Retail is literally `MOV AL,0x1; RET` (two bytes, which is why Ghidra never made it
// a function and c650 refers to it as LAB_14005aa50). Nothing to do on entry to
// "merchant inbound" — the work happens in the events.
char TradeComponent::EnterInbound() { return 1; }

// ============================================================
// state 0x13 trans38 (per frame) — 0x14005a780
// ============================================================
// Counts the merchant's stay down. When it expires and the post is not mid-trade
// (+0x71), request state 2. +0x71 is what stops a merchant vanishing out from under an
// open checkout. The null delta test is the tree's.
char TradeComponent::TickDocked(StateMachine* sm, const System::Time* deltaTime) {
    const int64_t dt = deltaTime ? deltaTime->_ticks : 0;
    merchantTimer._ticks -= dt;
    if (merchantTimer._ticks < 1 && !tradeFlag) {
        ::ControllerLive::RequestStateTransition(sm, 2);
    }
    return 1;
}

// ============================================================
// state 0x12 event 9 — 0x14005a850   THE MERCHANT HAS DOCKED
// ============================================================
// Raised by the BOAT, not by anything on this side: BoatComponent's state-2 enter
// (0x140044f70, boatcomponent_states.cpp) posts event 9 to this component's
// StateMachineComponent the moment the hull is within 0.2 of the pier. State 0x12 has no
// per-frame handler at all, so until something raised this event the post could never
// leave "merchant inbound" and the whole of layer 3 was unreachable.
//
// Retail INLINES the event-log writer here rather than calling it, but the entry it
// builds is field-for-field the 4-arg AddEvent overload (0x140095580) that eventlog.cpp
// already ports, with eventType 0xd and severity 2 — checked, not assumed:
//     color   0xff8080ff == EventLog_SeverityColor(2)
//     +0x10   0xd        == eventType
//     +0x14   2          == severity
//     +0x18   tileIdx    == EventLog_TileIndex(&entityIBP)   (comp+0x10)
//     +0x1c   5          == EventLog_IconIndex(0xd)
//     +0x20   -1         == EventLog_PopupIndex(0xd)
// so reusing AddEvent reproduces the inlined body exactly instead of duplicating it.
//
// The event log lives at gameWorld+0x138 (GameUpdateLoop::m_eventNotifier).
char TradeComponent::OnMerchantDocked(StateMachine* sm) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (gw) {
        // Retail copies the owner IBP to a local and passes its ADDRESS (the tile-index
        // helper takes an InternalBasePtr*, not the entity).
        Object::InternalPtr<ComponentEntity> entityIBP(_entity);
        gw->m_eventNotifier->AddEvent(0xd, 2, &entityIBP);
    }

    ::ControllerLive::RequestStateTransition(sm, 0x13);
    std::fprintf(stderr, "[TradeCtrl] event 9: MERCHANT DOCKED comp=%p -> state 0x13\n",
                 static_cast<void*>(this));
    std::fflush(stderr);
    return 1;
}

// ============================================================
// state 0x12 event 0xb — 0x14005a9c0
// ============================================================
// The inbound boat went away without docking: clear the merchant reference at +0x80
// and go back to idle.
char TradeComponent::OnMerchantLost(StateMachine* sm) {
    merchant = nullptr;
    ::ControllerLive::RequestStateTransition(sm, 2);
    return 1;
}

// ============================================================
// state 0x13 event 0xa — 0x14005b240   PLAYER DISMISSED THE MERCHANT
// ============================================================
// Retail inlines the whole of StateMachine::Tick after the request, which is why
// the body is ~90 decompiled lines for what is a two-step handler. Read against
// our own StateMachine::Tick (statemachinecomponent.cpp) the inlined
// loop matches EXACTLY, with time = System::Time(0.0), param3 = 0 (it dispatches
// node slot +0x30, not +0x38) and param4 = 1 (the `bVar2 = true` that is cleared
// after one pass). So retail 0x14005b240 is, in full:
//
//   local = 2;                                   // the desired state
//   if (sm[+9] < 0) StateMachine_Tick(sm, Time(0), 0, 0);   // drain a pending enter
//   i = node->slot08(node, sm[+9], &local);
//   if (i >= 0) { sm[+0xb] = i; sm[+0xa] = local; }         // == RequestStateTransition(sm, 2)
//   StateMachine_Tick(sm, Time(0), 0, 1);                   // run it NOW, not next frame
//   return 1;
//
// RequestStateTransition (controller_state_table.h:317) already replicates the
// first four lines including the nested drain tick and the node re-read, so the
// only thing this handler adds over its 0xb sibling is the immediate tick — which
// is the point: dismissing a merchant has to take effect on the click, not on the
// next simulation frame. Retail ignores param_1 entirely, so the handler stays free.
// Read in B6 (0x14005b240-0x14005b416): no test of `sm` (0x14005b24f `CMP [RDX+0x9],0` first);
// the drain tick hands &Time(0.0f) at [RSP+0x40] (0x14005b263-0x14005b27c); the inlined tail
// builds Time(0.0f) at [RSP+0x38] (0x14005b2a5) and passes it in R8 to node slot +0x30
// (0x14005b3cc-0x14005b3d7), run flag SIL = 1 (0x14005b2b4) cleared after one pass.
static char TradeCtrl_Event0A_Dismissed(TradeComponent* /*self*/, StateMachine* smV) {
    ::ControllerLive::RequestStateTransition(smV, 2);
    System::Time t0 = {0};
    RtSystem_Time_Ctor(&t0, 0.0f);
    smV->Tick(&t0, 0, 1);
    return 1;
}

// ============================================================
// state 2 trans38 (per frame) - 0x14005aa60   THE MERCHANT SPAWN
// ============================================================
// Counts the merchant timer at +0x78 down; when it reaches zero it CLONES the
// TraderBoat entity out of the TradeDescription, hands the clone to the route builder,
// registers it with the world entity list and asks for state 0x12 (inbound).
//
// RETURN VALUE: retail always returns a value whose LOW BYTE is zero - the not-yet
// branch returns the delta-time with the low byte masked off, and the fired branch masks
// the transition result the same way. So: 0, on every path. The delta is read
// unguarded (0x14005aa82).
char TradeComponent::TickIdle(StateMachine* smV, const System::Time* deltaTime) {
    merchantTimer._ticks -= deltaTime->_ticks;
    if (merchantTimer._ticks >= 1) return 0;

    TradeDescription* desc = TradeDesc(this);

    // Clone the merchant entity from the description's template (desc+0x30).
    ComponentEntity* boat = nullptr;
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    if (mem)
        boat = ::new (mem) ComponentEntity(desc->_merchantTemplate);

    merchant = boat;

    // Route it to THIS post. The destination is the trading post's own entity (+0x10).
    Object::InternalPtr<ComponentEntity> destIBP(_entity);
    BoatComponent* boatComp = merchant->FindComponent<BoatComponent>();   // 0x14000eb20
    // Unguarded (0x14005ab29): a merchant template always carries a BoatComponent.
    boatComp->SetDestination(&destIBP);   // 0x1400456e0

    // Publish the entity. The category is the template's own (template+0x20).
    Object::InternalPtr<ComponentEntity> regIBP = merchant;
    // The description and the world are read unguarded (0x14005ab31..0x14005ab58).
    ComponentDescription* tmpl = desc->_merchantTemplate;
    GameUpdateLoop::_singleton->m_entityList->RegisterEntity(&regIBP,
                                                            static_cast<int>(tmpl->_category));

    // OUTCOME, not intent: the route point count is the only thing that says the boat can
    // actually sail. "A boat was allocated" and "a boat has a route" are different claims.

    // Retail ticks the machine once first when it has no current state yet
    // (`if (*(char*)(sm+9) < 0)` at 14005ab6a), then requests 0x12.
    if (smV && smV->activeState < 0) {   // +0x09, signed
        System::Time zeroTime = {0};
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        // 0x14005ab6b-0x14005ab87: `LEA RDX,[RSP+0x60]` -- the ADDRESS of the stack Time(0).
        smV->Tick(&zeroTime, 0, 0);   // 0x1400a9660
    }
    ::ControllerLive::RequestStateTransition(smV, 0x12);
    return 0;
}

// ============================================================
// shared transition 3 - 0x14005b160   TEARDOWN
// ============================================================
// Carried by every state. Unregisters the post from the world's trading-post list, and if
// a merchant is present hands the boat back (a NULL destination is what tells the route
// builder to release it), frees the post's manifest and clears the docked merchant
// index.
//
// +0x50 / +0x58 here are the TradeComponent's MANIFEST base and count - NOT the
// BoatComponent's route head, which occupies the same offsets on a different object. The
// two are easy to confuse when reading b160 and 56e0 side by side.
char TradeComponent::Teardown() {
    ComponentEntity* entityIBP = _entity.Get();
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (gw) {
        // The town manager's trading-post list at +0x30.
        gw->m_citizenList->field_0x30.Remove(entityIBP);
    }

    if (merchant.Get() != nullptr) {
        Object::InternalPtr<ComponentEntity> nullDest(nullptr);
        BoatComponent* boatComp = merchant->FindComponent<BoatComponent>();   // 0x14000eb20
        if (boatComp) boatComp->SetDestination(&nullDest);   // the tree's null test

        System::MemoryAllocator::MemFree(manifest._data);
        manifest._data = nullptr;
        manifest._size = 0;
        manifest._capacity = 0;
        tradeId = -1;      // no merchant docked

        merchant = nullptr;
    }
    return 1;
}

// ============================================================
// Table build — replicates 0x14005c650
// ============================================================
// The handlers the dispatcher calls with (owner, sm[, delta]); RCX is the component.
static char TradeCtrl_State2_Enter(TradeComponent* c, StateMachine* /*sm*/)       { return c->EnterIdle(); }         // 0x14005abd0
static char TradeCtrl_State2_Tick(TradeComponent* c, StateMachine* sm, const System::Time* dt) {                    // 0x14005aa60
    return c->TickIdle(sm, dt);
}
static char TradeCtrl_State12_Enter(TradeComponent* c, StateMachine* /*sm*/)      { return c->EnterInbound(); }      // 0x14005aa50
static char TradeCtrl_Event09_MerchantDocked(TradeComponent* c, StateMachine* sm) { return c->OnMerchantDocked(sm); }   // 0x14005a850
static char TradeCtrl_Event0B_MerchantLost(TradeComponent* c, StateMachine* sm)   { return c->OnMerchantLost(sm); }     // 0x14005a9c0
static char TradeCtrl_State13_Enter(TradeComponent* c, StateMachine* /*sm*/)      { return c->EnterDocked(); }       // 0x14005a800
static char TradeCtrl_State13_Tick(TradeComponent* c, StateMachine* sm, const System::Time* dt) {                   // 0x14005a780
    return c->TickDocked(sm, dt);
}
static char TradeCtrl_State13_Update(TradeComponent* c, StateMachine* /*sm*/)     { return c->LeaveDocked(); }       // 0x14005a6d0
static void TradeCtrl_Teardown(TradeComponent* c)           { c->Teardown(); }          // 0x14005b160

// Built ONCE at static-init time, as retail's 0x14005c650 does (B38 D6/D7).
static void TradeCtrl_PopulateTable() {
    auto& t = TradeDispatch::table;
    if (t.count != 0) return;
    using namespace ::ControllerLive;

    // state 2 — idle / waiting for the next merchant
    TableRegisterState(t, 2,
        &TradeCtrl_State2_Enter,   // +0x08 enter  (abd0)
        nullptr,                                            // +0x10 trans30 (retail: 0)
        &TradeCtrl_State2_Tick,    // +0x18 trans38 (aa60)
        nullptr);                                           // +0x20 update  (retail: 0)
    TableAddTransition(t, 3, &TradeCtrl_Teardown);   // b160
    TableAddTransition(t, 0x12, nullptr);

    // state 0x12 — merchant inbound
    TableRegisterState(t, 0x12,
        &TradeCtrl_State12_Enter,  // aa50
        nullptr, nullptr, nullptr);
    TableAddTransition(t, 3, &TradeCtrl_Teardown);   // b160
    TableAddTransition(t, 0x13, nullptr);
    TableAddTransition(t, 2, nullptr);
    TableAddEvent(t, 9, &TradeCtrl_Event09_MerchantDocked);  // a850
    TableAddEvent(t, 0xb, &TradeCtrl_Event0B_MerchantLost);

    // state 0x13 — merchant docked
    TableRegisterState(t, 0x13,
        &TradeCtrl_State13_Enter,  // a800  (layer 3)
        nullptr,                                            // retail: 0
        &TradeCtrl_State13_Tick,   // a780
        &TradeCtrl_State13_Update);// a6d0  (layer 3)
    TableAddTransition(t, 3, &TradeCtrl_Teardown);   // b160
    TableAddTransition(t, 2, nullptr);
    TableAddEvent(t, 0xa, &TradeCtrl_Event0A_Dismissed);  // 0x14005b240

    std::fprintf(stderr, "[TradeCtrl] table built: %d states (2, 0x12, 0x13)\n", t.count);
    std::fflush(stderr);
}

// Built ONCE at static-init time (B38 D6/D7). Before, TradeComponent_InstallController
// re-ran it from every factory call on both threads. The "[TradeCtrl] table built"
// line above now prints at static init, before standalone_error.log is opened.
static const bool s_tradeStateTableBuilt = (TradeCtrl_PopulateTable(), true);
