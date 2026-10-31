// src/game/Component/orchardcomponent_states.cpp
// OrchardComponent controller state table — the standalone replica of retail's
// OrchardDescription_helper_20f0 (0x1400520f0), which is what fills the orchard's
// state-type table at DAT_1401cfe50 (16 entries x 0xf8, count DAT_1401d0dd0).
//
// WHY THIS FILE EXISTS: OrchardComponent was a COMPONENT_INERT_FACTORY stub, so its
// embedded state node at comp+0x28 was never installed and never linked into the
// entity's StateMachineComponent chain. Nothing ticked, so nothing ever built the
// per-tree slot array at +0x50/+0x58, nothing posted PlantOrchard work, no trees grew
// and no HarvestOrchard work was ever scheduled — the "orchard produces no yield"
// symptom. Measured baseline before this file existed (BANISHED_ORCHARD_DIAG=120):
//   node(+0x28).vtable=0  backptr(+0x40)=0  linked=NO  count(+0x58)=0
//   PlantOrchard constructed=0 executed=0 | HarvestOrchard constructed=0 executed=0
//
// ── The retail registration API (0x140009aa0 / 9bc0 / 9c70) ────────────────────────
//   AddState(int id, void* enter /*RDX*/, void* update /*R8*/, <R9 unused>,
//            void* trans38 /*[RSP+0x20]*/)
//   AddTransition(int targetId)              -> appends {targetId, 0} to the last
//                                               state's transition list (+0x28)
//   AddEvent(int eventId, void* handler)     -> appends to the last state's EVENT
//                                               list (+0xb0) — see the gap note below
//
// ⚠ TRAP: AddState's argument order is NOT its field order. The `update` argument
// lands at entry+0x20 and `trans38` at entry+0x18, and trans30 (+0x10) is never set
// by any orchard state. Wiring these positionally would put the per-frame handler in
// a transition slot. Verified by reading 0x140009aa0's body, not by argument names.
//
// ⚠ TRAP: AddState's 4th register argument (R9) is genuinely uninitialised at every
// call site — it shows up as `in_R9` in the decompile. It is not a field; do not
// invent one for it.
//
// ── Node vtable slot mapping (READ FROM VTABLE BYTES, not Ghidra labels) ───────────
// The node vtable is 0x14015a130 (10 slots), sitting immediately before the primary
// component vtable at 0x14015a180. Ghidra names those 10 bodies with generic
// component-vtable names (ScalarDtor/Slot1/Slot2/Shutdown/Serialize/Slot5/Draw/
// OnCreate/OnRemove) but they implement the STATE-NODE interface instead. Proof:
// 0x140009df0, which Ghidra calls "Controller_Shutdown", is the Enter dispatcher —
//     MOVSXD RAX,R8D / LEA R8,[0x1401cfe50] / IMUL RAX,RAX,0xf8 / ADD RAX,R8
//     MOV R8,[RAX+0x8] / TEST R8,R8 / JZ ret0 / MOV RCX,[RCX+0x18] / JMP R8
// — 0xf8 stride, enterHandler at +0x08, and the documented `MOV RCX,[node+0x18]`
// backptr contract. So: [0]=Step [3]=Enter [4]=Update [5]=OnState [6]=Trans30
// [7]=Trans38(per-frame) [9]=NULL. Trusting the Ghidra names would have wired
// Serialize into the per-tick slot: a node that links and ticks and does nothing.
//
// ── GAP (deliberate, increment 1) ─────────────────────────────────────────────────
// Every handler below is registered as nullptr. ControllerLive::Dispatcher treats a
// null handler as a graceful no-op, so the node still runs the REAL table-driven
// structure — Step returns real indices and transitions are validated against the
// real graph — it just performs no per-state work yet. That makes this increment
// verifiable on its own (linked=NO -> linked=YES) without any behaviour risk, and
// the per-state bodies drop in one at a time afterwards.
//
// The 18 handler addresses are recorded per state below so that filling them in is a
// lookup, not another decomp pass. All 18 are UNPORTED as of this increment.
//
// ── SECOND GAP: the event list ────────────────────────────────────────────────────
// controller_state_table.h's StateEntry models id/enter/trans30/trans38/update and
// the transition list, but NOT the event list retail keeps at entry+0xb0 (filled by
// AddEvent 0x140009c70). The orchard needs it: the panel's Command (idx14) posts node
// events 3/4/5, and event 3 on state 2 is the "start planting" kick. Until StateEntry
// carries events, the command path cannot drive the machine. Noted here rather than
// silently omitted — it is required work, not an optional extra.

#include "Component/controller_state_table.h"
#include "Component/ClearAreaDescription.h"
#include "Component/OrchardDescription.h"
#include "Component/OrchardComponent.h"
#include "Component/StateMachineComponent.h"   // StateMachine (the handlers sm)
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Component/exe_bodies.h"   // the Component leaves named below
#include "Work/exe_bodies.h"        // the work-item ctors
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"   // the entity description's sub-descriptions
#include "Component/ClearAreaComponent.h"
#include "Component/MapComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Game/Map.h"                // Map::size (GameUpdateLoop::m_game)
#include "Game/Calendar.h"           // GameUpdateLoop::m_dayCounter
#include "Game/WeatherSimulation.h"  // GameUpdateLoop::m_weather
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include <cstdint>
#include "Component/ControllerComponent.h"
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Work/ClearArea.h"
#include "Work/TendArea.h"   // B8: the (entity, kind) constructor
#include "Work/PlantOrchard.h"     // B9: the (entity, tree type) constructor
#include "Work/HarvestOrchard.h"   // B9: the (entity) constructor
#include "Component/FruitComponent.h"   // B4 S4 RT2: InternalPtr<FruitComponent>::Get() needs the class complete
#include "Component/NaturalResourceComponent.h"   // B4 S4 RT2: InternalPtr<NaturalResourceComponent>::Get() needs the class complete

// OrchardCtrlTag lives in OrchardComponent.h (the member type needs it).
using OrchardDispatch = ::ControllerLive::Dispatcher<OrchardCtrlTag>;


// Declared by their headers: MapGrid_StampCellRefs_9f770 (0x14009f770,
// pasturecomponent_states.cpp) in Component/exe_bodies.h; the PlantOrchard / TendArea /
// HarvestOrchard constructors in their Work/ headers.
//
// Finders: 0x140002e10 FindComponent<WorkPlaceComponent>, 0x140003050
// FindComponent<WorkComponent>, 0x140004680 FindComponent<MapComponent>, 0x14000a170
// FindComponent<ClearAreaComponent>. Leaves, called by name (B3R):
//   0x14012ec60 CitizenAI_WorkScheduler_Post   0x14012fc00 WorkPlaceComponent_SetAccepting
//   0x14012eb50 WorkComponent_RegisterWorkSlot 0x1400a9660 StateMachine::Tick
//   0x14012ead0 WorkComponent_CheckWorkSlot
//     (FUN_14012ead0 — "does the work component currently hold a work slot?"; the companion
//     of 0x14012eb50 (release). State 10 gates both its update and its seasonal re-post on it.)
//   import slot 0x140152b00 ??0Time@System@@QEAA@M@Z = RtSystem_Time_Ctor
// LEFT (B.4 P2 COMP-6): the handlers receive the StateMachine as void* (controller_state_table.h,
// COMP-2+3); the leaves above take their components as uintptr_t / int64_t (wave X).
// ============================================================
namespace {

// DAT_1401c46e8 = BaseTypeInfo("OrchardDescription"), read BY CELL as the class's static.
uint32_t OrchardDescTypeId() { return OrchardDescription::TypeId(); }   // ti cell 0x1401c46e8

// The "request a state" idiom, shared by ten of the eighteen handlers.
// Retail inlines this everywhere:
//     want = <id>;
//     if ((int8_t)sm[9] < 0) { Time t(0.0f); StateMachine_Tick(sm, t, 0, 0); }
//     idx = (*(void**)sm)->vtable[+0x08]((void*)*sm, (int)(int8_t)sm[9], &want);
//     if (idx >= 0) { sm[0xb] = idx; sm[0xa] = want; }
// `want` is an in/out — node vtable idx1 writes -1 into it on a miss, so it must be a
// real local, never a constant. sm+0x09 is the CURRENT state id (signed; < 0 means the
// machine has not started, hence the priming Tick).
// `sm` is the StateMachine (StateMachineComponent+0x48) the handler received.
void OrchardRequestState(StateMachine* machine, int wantId) {
    if (!machine) return;
    int want = wantId;
    if (machine->activeState < 0) {
        System::Time timeBuf[2] = {};      // Time is 8 bytes; 16 is deliberate headroom
        RtSystem_Time_Ctor(&timeBuf[0], 0.0f);   // (see the Time-ctor 8-byte-clobber lesson)
        // The ADDRESS of the stack Time(0), as every retail caller passes it (B6 row 510).
        machine->Tick(timeBuf, 0, 0);   // 0x1400a9660
    }
    ControllerComponent::Controller* chainHead = machine->current;
    if (!chainHead) return;
    // chainHead = the first controller member the SM linked (D6); slot 1 FindTransition.
    int idx = chainHead->FindTransition(static_cast<int>(machine->activeState), &want);
    if (idx >= 0) {
        machine->transitionState = static_cast<int8_t>(idx);
        machine->pendingState    = static_cast<int8_t>(want);
    }
}

}  // namespace

// ============================================================
// state 2 ENTER — retail 0x140051530
// Deactivate the workplace. `sm` is unused by retail. Returns AL = 0: an enter
// handler that returns non-zero starves every sibling node behind this one.
// ============================================================
char OrchardComponent::EnterIdle() {
    WorkPlaceComponent* wp = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    wp->SetAccepting(false);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)
    return 0;
}

// ============================================================
// state 2 TRANS38 (per-frame) — retail 0x1400514c0
// THE TRIGGER FOR THE WHOLE FEATURE: an orchard whose `enabled` switch is 0 never
// leaves state 2. The ctor clears it, and the only two ways to set it are the
// panel's Command(2) toggle and Command(0)'s node event 3 — both ANDed with
// hasType (treeType != 0) by CanCommand. So an untyped orchard is correctly and
// permanently idle, which is exactly what the runtime diagnostic shows.
// ============================================================
char OrchardComponent::TickIdle(StateMachine* sm) {
    if (enabled)
        OrchardRequestState(sm, 9);
    return 1;
}

// ============================================================
// state 9 ENTER — retail 0x140051070 (Ghidra: PlantOrchard_impl_1070)
// THIS IS WHAT BUILDS THE SLOT ARRAY. Until this ran, slotCount stayed 0,
// which is exactly what the runtime diagnostic reported.
//
// Steps, in retail's order:
//   1. push the master switch to the workplace: SetWorkplaceActive(wp, enabled)
//      — note it forwards the switch, NOT a literal 0 as state 2's enter does
//   2. footprint rect from the MapComponent: x,y,w,h as int16 at +0x28/+0x2a/+0x2c/+0x2e
//   3. spacing from the OrchardDescription
//   4. cols = (w-1+spacingX)/spacingX, rows = (h-1+spacingY)/spacingY
//   5. resize the slot array to rows*cols and fill each entry:
//      tile = curY*mapStride + colOffset + x, three null IBPs, state = 0
//   6. RemoveFromMap on the MapComponent (0x140124750)
//   7. allocate 0x70 and build a PlantOrchard carrying treeType
//   8. post it to the entity's work component
// ============================================================
char OrchardComponent::EnterPlanting() {
    // 1. workplace <- master switch (0x140002e10, 0x14012fc00)
    WorkPlaceComponent* wp = _entity->FindComponent<WorkPlaceComponent>();
    wp->SetAccepting(enabled);

    // 2. footprint rect (0x140004680), read unguarded
    MapComponent* pos = _entity->FindComponent<MapComponent>();
    const int rectX = static_cast<int16_t>(pos->tileX0);
    int curY        = static_cast<int16_t>(pos->tileY0);
    const int rectW = static_cast<int16_t>(pos->tileX1);
    const int rectH = static_cast<int16_t>(pos->tileY1);

    // 3. spacing: IsDerivedFrom OrchardDescription (cell 0x1401c46e8; CMOVNZ, null on
    // a miss), +0x24 / +0x28 read unguarded and divided by as they are.
    Description* desc = _description;
    const OrchardDescription* od = desc->IsDerivedFrom(OrchardDescTypeId())   // import 0x140152a98
                                       ? static_cast<const OrchardDescription*>(desc) : nullptr;
    const int spacingX = od->_spacingX;
    const int spacingY = od->_spacingY;

    // 4/5. lay out the grid: SetSlotCount 0x140009f80, then per slot the tile
    // (curY * map edge + column offset + x; the edge re-read per slot, 0x1400511e4),
    // the three IBPs in retail's order (+0x10, +0x18, +0x08) and state 0.
    const int cols = (rectW - 1 + spacingX) / spacingX;
    const int rows = (rectH - 1 + spacingY) / spacingY;
    SetSlotCount(rows * cols);
    const Map* map = GameUpdateLoop::_singleton->m_game;   // [0x1401c3dd8] +0xf8
    for (int r = 0; r < rows; ++r) {
        int colOff = 0;
        for (int c = 0; c < cols; ++c) {
            OrchardSlot& slot = slots[r * cols + c];
            slot.tile = curY * map->size + colOff + rectX;
            slot.resource = nullptr;
            slot.fruit    = nullptr;
            slot.tree     = nullptr;
            slot.state    = 0;
            colOff += spacingX;
        }
        curY += spacingY;
    }

    // 6. the MapComponent's 0x140124750 over the footprint
    pos->RemoveFromMap();

    // 7. the PlantOrchard work item (MemAlloc 0x70; the ctor 0x1400f5140 runs only
    // on a non-null block, as retail tests it) carrying the chosen tree type (+0x60)
    Work* work = nullptr;
    if (void* mem = System::MemoryAllocator::MemAlloc(0x70, 8)) {
        ComponentEntity* entityLocal = _entity.Get();
        work = new (mem) PlantOrchard(&entityLocal, treeType);
    }
    Object::InternalPtr<Work> workIBP = work;

    // 8. post it (PostWork 0x14012ec60 with 1, 0). Retail hand-rolls the scan for the
    // WorkComponent cell 0x1401c8070: Type() compare, then IsDerivedFrom, which is
    // FindComponent<WorkComponent>.
    WorkComponent* target = _entity->FindComponent<WorkComponent>();
    target->PostWork(1, 0, &workIBP);
    return 1;
}

// ============================================================
// state 9 TRANS38 (per-frame) — retail 0x140050f90
// When every slot has reached state 1 (planted), deactivate the workplace and move to
// state 10.
//
// ⚠ FAITHFUL LANDMINE, deliberately preserved: the test is `planted == slotCount`,
// evaluated before any planting has happened. A footprint yielding cols*rows == 0 gives
// 0 == 0, so a zero-area orchard falls straight through to state 10 on its first frame
// having planted nothing, and looks healthy. Retail does this; do not "fix" it here.
// ============================================================
char OrchardComponent::TickPlanting(StateMachine* sm) {
    ValidateRefs();

    const int count = slotCount;
    int planted = 0;
    for (int i = 0; i < count; ++i) {
        const int st = slots[i].state;
        if (st != 0 && st == 1) ++planted;   // retail's redundant pair, kept verbatim
    }
    if (planted != count) return 1;

    WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
    wc->CancelWork();   // 0x14012eb50
    OrchardRequestState(sm, 10);
    return 1;
}

// ============================================================
// State 10's shared leaves
// ============================================================
// The calendar/weather constants are the exe's own .rdata floats, read as BYTES
// (read_memory, 2026-08-10) rather than taken from a Ghidra label:
//   DAT_14017225c = 0x45610000 = 3600.0f   seconds per game year
//   DAT_1401721b8 = 0x41400000 =   12.0f   months per year
//   DAT_1401721d4 = 0x42040000 =   33.0f   weather threshold (gw+0x130, +0x18)
//   DAT_140172144 = 0x3f7d70a4 =    0.99f  ripeness threshold on GetGrowth
static constexpr float kSecondsPerYear   = 3600.0f;
static constexpr float kMonthsPerYear    = 12.0f;
static constexpr float kWeatherThreshold = 33.0f;
static constexpr float kRipeGrowth       = 0.99f;

namespace {

// The month index retail derives in three separate places:
//   (int)(Time::AsSeconds(*(gw+0x128) + 8) / 3600.0f * 12.0f)
// Note the Time lives at calendar+8 and the YEAR is the int at calendar+0 (see the
// event-4 handler, which stamps it into lastHarvestYear).
int OrchardCurrentMonth() {
    if (GameUpdateLoop::_singleton == nullptr) return 0;
    Calendar* cal = GameUpdateLoop::_singleton->m_dayCounter;
    if (!cal) return 0;
    const float secs = cal->yearTime.AsSeconds();
    return static_cast<int>(secs / kSecondsPerYear * kMonthsPerYear);
}

// The year is the calendar's first int.
int OrchardCurrentYear() {
    if (GameUpdateLoop::_singleton == nullptr) return 0;
    Calendar* cal = GameUpdateLoop::_singleton->m_dayCounter;
    return cal ? cal->year : 0;
}

// Slots whose state is 2 are HOLES — a tile the planter could not use (msg 0 failed to
// bind, or the tree was later removed). Retail writes the scan as
// `if ((v != 0) && (v != 1) && (v == 2))`, a switch artefact that means simply `v == 2`.
int OrchardHoleCount(const OrchardComponent* s) {
    const int count = s->slotCount;
    const OrchardSlot* slots = s->slots;
    if (!slots) return 0;
    int holes = 0;
    for (int i = 0; i < count; ++i)
        if (slots[i].state == 2)
            ++holes;
    return holes;
}

// The TendArea post shared by state 10's enter and its per-frame seasonal branch.
void OrchardPostTendArea(ComponentEntity* entity) {
    // Retail runs a finder into a throwaway out-param here before allocating and never
    // reads the result — kept for shape.
    (void)entity->FindComponent<WorkComponent>();   // 0x140003050

    Work* work = nullptr;
    {
        void* mem = System::MemoryAllocator::MemAlloc(0x60, 8);
        if (mem) {
            ComponentEntity* entityLocal = entity;
            work = new (mem) TendArea(&entityLocal, 1);   // 0x1400f5a80
        }
    }
    Object::InternalPtr<Work> workIBP = work;   // the InternalPtr ctor is a plain store

    WorkComponent* target = entity->FindComponent<WorkComponent>();   // 0x140003050
    target->PostWork(1, 0, &workIBP);
}

// Is the work component currently holding a work slot for us? (0x14012ead0)
bool OrchardHasWork(ComponentEntity* entity) {
    WorkComponent* wc = entity->FindComponent<WorkComponent>();   // 0x140003050
    return wc->IsWorkPosted() != 0;   // 0x14012ead0
}

}  // namespace

// ============================================================
// event-4 handler — retail 0x140050900 (wired on states 10 and 12)
// Request state 11 (harvest), clear every slot's picked byte, and stamp the
// current year into lastHarvestYear so TryHarvest cannot fire twice in one year.
// TryHarvest calls it directly as well, as retail's per-frame does.
// ============================================================
char OrchardComponent::OnHarvestEvent(StateMachine* sm) {
    OrchardRequestState(sm, 11);

    const int count = slotCount;
    for (int i = 0; slots && i < count; ++i)
        slots[i].picked = false;

    lastHarvestYear = OrchardCurrentYear();
    return 1;
}

// ============================================================
// event-3 handler — retail 0x140051450 (wired on state 2)
// Unconditionally request state 9. This is the panel's "start planting" kick: the
// Command slot (idx14) maps button id 0 to node event 3, and this bypasses the
// enabled-switch gate that state 2's per-frame tests.
// ============================================================
char OrchardComponent::OnPlantEvent(StateMachine* sm) {
    OrchardRequestState(sm, 9);
    return 1;
}

// ============================================================
// event-5 handler — retail 0x1400513e0 (wired on states 9, 10, 11 and 12)
// Unconditionally request state 20 (0x14) — the clear/replace path, button id 3.
// ============================================================
char OrchardComponent::OnClearEvent(StateMachine* sm) {
    {   // The panel proved it POSTS event 5 (the [OrchardCmd] Command(3) line), but nothing
        // showed whether the event reached a handler — and with state 20's handlers null,
        // "the event never arrived" and "it arrived and did nothing" looked identical.
        // sm+0x0a is the requested state id after the request, sm+0x09 the current one.
        // ⚠ sm+0x09 is the table INDEX, not a state id (registration order is
        // [0]=2 [1]=9 [2]=10 [3]=12 [4]=11 [5]=20), so it is labelled as such. sm+0x0a is
        // the requested STATE id — the two must not be printed under the same word.
        static int s_n = 0;
        if (s_n++ < 8) {
            StateMachine* machine = sm;
            const int beforeIdx = sm ? static_cast<int>(machine->activeState) : -99;
            OrchardRequestState(sm, 20);
            std::fprintf(stderr, "[Orchard20] event 5 (cut): from table idx %d -> requested "
                         "state %d (accepted=%d)\n", beforeIdx,
                         sm ? static_cast<int>(static_cast<uint8_t>(machine->pendingState)) : -99,
                         sm ? static_cast<int>(static_cast<uint8_t>(machine->pendingState) == 20) : 0);
            std::fflush(stderr);
            return 1;
        }
    }
    OrchardRequestState(sm, 20);
    return 1;
}

// ============================================================
// TryHarvest — retail 0x140051580 (called by the state 10 and 12 per-frame bodies)
// The growth -> harvest trigger, and the only reader of GetGrowth outside the UI.
//
//   if (!enabled)                      return 0;   // master switch off
//   if (year == lastHarvestYear)       return 0;   // already harvested this year
//   if (weather >= 33.0f && month in [7..11] && GetGrowth > 0.0f)  -> harvest
//   if (GetGrowth > 0.99f)                                         -> harvest
//
// The first arm is the "frost is coming, take what is there" rule; the second is the
// ordinary ripeness one. Returning 1 makes the per-frame body return immediately.
// ============================================================
char OrchardComponent::TryHarvest(StateMachine* sm) {
    if (!enabled) return 0;
    if (OrchardCurrentYear() == lastHarvestYear) return 0;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    WeatherSimulation* weather = gw ? gw->m_weather : nullptr;
    if (weather && weather->temperature >= kWeatherThreshold) {
        const unsigned m = static_cast<unsigned>(OrchardCurrentMonth() - 7);
        if (m < 5 && GetGrowth() > 0.0f)
            return OnHarvestEvent(sm);
    }
    if (GetGrowth() > kRipeGrowth)
        return OnHarvestEvent(sm);
    return 0;
}

// ============================================================
// state 10 ENTER — retail 0x140050e30
// ValidateRefs; if ANY slot is a hole, go replant (state 12) and post nothing.
// Otherwise post one TendArea work item. Returns AL = 1 on both paths.
// ============================================================
char OrchardComponent::EnterTending(StateMachine* sm) {
    ValidateRefs();
    const int holes = OrchardHoleCount(this);
    {   // both arms are silent otherwise, and "posted a TendArea" vs "bounced to replant"
        // is the whole meaning of this handler.
        static int s_n = 0;
        if (s_n++ < 4) {
            std::fprintf(stderr, "[Orchard10] enter: holes=%d -> %s\n", holes,
                         holes > 0 ? "request state 12 (replant)" : "post TendArea");
            std::fflush(stderr);
        }
    }
    if (holes > 0) {
        OrchardRequestState(sm, 12);
        return 1;
    }
    OrchardPostTendArea(_entity.Get());
    return 1;
}

// ============================================================
// state 10 UPDATE — retail 0x140050ba0
// Find the work component; if it is holding a work slot, release it. `sm` unused.
// ============================================================
char OrchardComponent::UpdateTending() {
    if (OrchardHasWork(_entity.Get())) {
        WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
        wc->CancelWork();   // 0x14012eb50
    }
    return 1;
}

// ============================================================
// state 10 TRANS38 (per-frame) — retail 0x140050c00
// ValidateRefs; TryHarvest (returns straight out if it fired); holes -> request 12;
// then the seasonal tending gate.
//
// ⚠ FAITHFUL: the state-12 request is NOT followed by a return — the seasonal block
// still runs in the same frame. Do not "tidy" it into an early return.
//
// inSeason = month in [1..8]: retail spells it `(unsigned)(m-1) < 11 && !((unsigned)(m-9) < 3)`,
// i.e. [1..11] minus [9..11]. Out of season the tending work is dropped rather than
// left queued; in season a fresh TendArea is posted only when none is outstanding.
// ============================================================
char OrchardComponent::TickTending(StateMachine* sm) {
    ValidateRefs();
    if (TryHarvest(sm) != 0) return 1;

    if (OrchardHoleCount(this) > 0) OrchardRequestState(sm, 12);   // no return — faithful

    const int m = OrchardCurrentMonth();
    const bool inSeason = (static_cast<unsigned>(m - 1) < 11u) &&
                          !(static_cast<unsigned>(m - 9) < 3u);
    const bool hasWork = OrchardHasWork(_entity.Get());

    {   // This handler runs every frame and its four branch combinations look identical
        // from outside. Report the deciding inputs the first four times only: a periodic
        // line would print for the whole session in a shipped build.
        static int s_n = 0;
        if (s_n < 4) {
            ++s_n;
            std::fprintf(stderr, "[Orchard10] perFrame #%d: month=%d inSeason=%d hasWork=%d "
                         "holes=%d growth=%.6f year=%d lastHarvestYear(+0x4c)=%d -> %s\n",
                         s_n, m, (int)inSeason, (int)hasWork, OrchardHoleCount(this),
                         GetGrowth(), OrchardCurrentYear(), lastHarvestYear,
                         (hasWork && !inSeason) ? "drop the tending work"
                             : (!hasWork && inSeason) ? "post TendArea" : "nothing");
            std::fflush(stderr);
        }
    }
    if (hasWork && !inSeason) {
        WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
        wc->CancelWork();   // 0x14012eb50
    } else if (!hasWork && inSeason) {
        OrchardPostTendArea(_entity.Get());
    }
    return 1;
}

// ============================================================
// state 12 ENTER — retail 0x140050ae0 (Ghidra: PlantOrchard_impl_0ae0)
// The replant-the-gaps state. Post a fresh PlantOrchard; unlike state 9 it does NOT
// touch the slot array — PlantOrchard_CtorFromEntity re-sizes its own byte array from
// the orchard's existing slot count, so the holes it finds are whatever ValidateRefs
// last marked as state 2.
//
// Retail runs a finder into a throwaway out-param before allocating and never reads the
// result; kept for shape.
// ============================================================
char OrchardComponent::EnterReplanting() {
    (void)_entity->FindComponent<WorkComponent>();   // 0x140003050

    Work* work = nullptr;
    {
        void* mem = System::MemoryAllocator::MemAlloc(0x70, 8);
        if (mem) {
            ComponentEntity* entityLocal = _entity.Get();
            work = new (mem) PlantOrchard(&entityLocal, treeType);
        }
    }
    Object::InternalPtr<Work> workIBP = work;   // the InternalPtr ctor is a plain store

    WorkComponent* target = _entity->FindComponent<WorkComponent>();   // 0x140003050
    {   // state 12 is only reachable when ValidateRefs found a hole, so "we got here at
        // all" is the interesting fact; the hole count says how much replanting is due.
        static int s_n = 0;
        if (s_n++ < 4) {
            std::fprintf(stderr, "[Orchard12] enter: holes=%d work=%p -> post PlantOrchard\n",
                         OrchardHoleCount(this), work);
            std::fflush(stderr);
        }
    }
    target->PostWork(1, 0, &workIBP);
    return 1;
}

// state 12 UPDATE — retail 0x1400509b0: byte-for-byte the same body as state 10's
// update (separate retail function, identical logic), so the table names UpdateTending.

// ============================================================
// state 12 TRANS38 (per-frame) — retail 0x140050a10
// ValidateRefs; TryHarvest (short-circuits the frame if it fired); if any hole remains
// stay in 12, otherwise go back to tending.
//
// ⚠ FAITHFUL zero-degenerate, third instance: when slotCount is 0 retail skips the scan
// loop entirely and falls straight through to requesting state 10 — a slot-less orchard
// leaves replant on its first frame. Do not "fix" it.
// ============================================================
char OrchardComponent::TickReplanting(StateMachine* sm) {
    ValidateRefs();
    if (TryHarvest(sm) != 0) return 1;

    if (slotCount > 0 && OrchardHoleCount(this) != 0)
        return 1;                                   // holes remain — stay in 12

    OrchardRequestState(sm, 10);
    return 1;
}

// ============================================================
// state 11 ENTER — retail 0x140050800 (Ghidra: HarvestOrchard_impl_0800)
// Post one HarvestOrchard work item, then stamp the footprint cells with the orchard's
// tree type. Note the ctor takes NO tree-type argument, unlike PlantOrchard's — the work
// item derives its worklist from the component's slot array instead.
// ============================================================
char OrchardComponent::EnterHarvesting() {
    Work* work = nullptr;
    {
        void* mem = System::MemoryAllocator::MemAlloc(0x68, 8);
        if (mem) {
            ComponentEntity* entityLocal = _entity.Get();
            work = new (mem) HarvestOrchard(&entityLocal);
        }
    }
    Object::InternalPtr<Work> workIBP = work;   // the InternalPtr ctor is a plain store

    WorkComponent* target = _entity->FindComponent<WorkComponent>();   // 0x140003050
    target->PostWork(1, 0, &workIBP);

    // Footprint stamp. Retail passes the Map (GameUpdateLoop::m_game) and the rect as
    // four int16s off the MapComponent.
    // 0x1400508a8-0x1400508ea: the MapComponent and the map ([0x1401c3dd8]+0xf8) read
    // with no test (row 1241).
    MapComponent* pos = _entity->FindComponent<MapComponent>();   // 0x140004680
    Map* grid = GameUpdateLoop::_singleton->m_game;
    {   // The single line that says "the harvest actually started".
        static int s_n = 0;
        if (s_n++ < 4) {
            std::fprintf(stderr, "[Orchard11] enter: work=%p slots=%d grid=%p treeType=%p\n",
                         work, slotCount, static_cast<void*>(grid), static_cast<void*>(treeType));
            std::fflush(stderr);
        }
    }
    grid->StampCellRefs(static_cast<int16_t>(pos->tileX0), static_cast<int16_t>(pos->tileY0),
            static_cast<int16_t>(pos->tileX1), static_cast<int16_t>(pos->tileY1),
            treeType);
    return 1;
}

// ============================================================
// state 11 TRANS38 (per-frame) — retail 0x140050650
// The harvest-complete latch: count slots that still hold a tree (state == 1) against
// slots already picked. Once picked >= live, release the work slot and go back to
// tending.
//
// The event-4 handler clears every slot's picked byte on the way IN to state 11, which is
// what makes this latch meaningful — HarvestOrchard sets the byte per slot as citizens pick.
//
// ⚠ FAITHFUL zero-degenerate: with no slots 0 <= 0 holds, so a slot-less orchard exits to
// state 10 on its first frame.
// ============================================================
char OrchardComponent::TickHarvesting(StateMachine* sm) {
    const int count = slotCount;
    int liveCount = 0, doneCount = 0;
    for (int i = 0; slots && i < count; ++i) {
        const int st = slots[i].state;
        if (st != 0 && st == 1) ++liveCount;        // retail's redundant pair, kept verbatim
    }
    for (int i = 0; slots && i < count; ++i)
        if (slots[i].picked)
            ++doneCount;
    {
        static int s_n = 0;
        if (s_n < 4) {
            ++s_n;
            std::fprintf(stderr, "[Orchard11] perFrame #%d: live=%d picked=%d -> %s\n",
                         s_n, liveCount, doneCount,
                         liveCount <= doneCount ? "harvest done, request state 10" : "keep picking");
            std::fflush(stderr);
        }
    }
    if (liveCount > doneCount) return 1;

    // Retail inlines the WorkComponent scan here; FindComponent<WorkComponent> is the same
    // scan. Note the release is unconditional — no HasWorkSlot test.
    WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
    wc->CancelWork();   // 0x14012eb50
    OrchardRequestState(sm, 10);
    return 1;
}

// ============================================================
// state 20 (0x14) — CLEAR / "cut the orchard down"
//
// This is the state the panel's CLEAR button (cmd id 3 -> node event 5) drives, and until
// now it was registered with three nullptr handlers. ControllerLive::Dispatcher treats a
// null handler as a graceful no-op, so the machine DID transition into state 20 and then
// sat there doing nothing — which from the panel is indistinguishable from a dead button.
// That is the user-reported "cut is clickable but does nothing".
//
// The two real leaves are already ported in clearareacomponent_states.cpp (retail's state
// 20 calls the same two functions the ClearAreaComponent controller does), so they are
// used through the exports rather than forked.
// ============================================================

namespace {

// The entity's description carries a list of sub-descriptions. State 20's enter scans it
// for the ClearAreaDescription and passes its +0x20 (the resource filter) into the work
// item.
uint32_t ClearAreaDescTypeId() { return ClearAreaDescription::TypeId(); }   // ti cell 0x1401c7200

ClearAreaDescription* FindClearAreaDescription(ComponentEntity* entity) {
    const uint32_t want = ClearAreaDescTypeId();
    ComponentDescription* descList = entity ? entity->_description : nullptr;
    if (!descList || !want) return nullptr;
    const int count = descList->_subDescCount;
    Description** arr = descList->_subDescs;
    for (int i = 0; arr && i < count; ++i) {
        Description* elem = arr[i];
        if (!elem) continue;
        Object::Base* sub = elem;
        if (sub->Type() != want) continue;   // slot 2
        // Retail confirms the hit with IsDerivedFrom before accepting it, and BREAKS out of
        // the loop either way — a type-id match that fails the derived test yields null.
        if (!::Object::IsDerivedFromNullSafe(sub, want)) return nullptr;
        return static_cast<ClearAreaDescription*>(sub);
    }
    return nullptr;
}

}  // namespace

// ============================================================
// state 20 ENTER — retail 0x1400504e0 (Ghidra: ClearArea_impl_04e0)
// Release any work slot the orchard is holding, then post ONE ClearArea work item at
// priority 0x401 carrying the ClearAreaDescription's filter. The three literal flags
// (1, 0, 1) are retail's — clear trees, leave the ground, clear buildings — and are NOT
// read off the description the way ClearAreaComponent's own state 0 reads them.
// ============================================================
char OrchardComponent::EnterClearing() {
    WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
    if (wc->IsWorkPosted()) {   // 0x14012ead0
        wc->CancelWork();   // 0x14012eb50
    }

    ComponentEntity* entity = _entity.Get();
    ClearAreaDescription* caDesc = FindClearAreaDescription(entity);

    ClearArea* work = nullptr;
    if (caDesc) {
        void* mem = System::MemoryAllocator::MemAlloc(0x78, 8);
        if (mem) {
            work = new (mem) ClearArea(&entity, caDesc->_target, 1, 0, 1);   // 0x1400e6960
        }
    }
    {   // The whole point of porting this state is to make "cut" observable. Retail derefs
        // caDesc+0x20 unguarded, so a miss here would be a FAULT in retail — meaning the
        // orchard template is expected to carry a ClearAreaDescription. Report it loudly
        // instead of faulting: if this line ever prints, the description list is the bug,
        // not this handler.
        static int s_n = 0;
        if (s_n++ < 4) {
            std::fprintf(stderr, "[Orchard20] enter: caDesc=%p work=%p slots=%d -> %s\n",
                         static_cast<void*>(caDesc), static_cast<void*>(work), slotCount,
                         caDesc ? "post ClearArea (prio 0x401)"
                                : "NO ClearAreaDescription on this entity — retail would "
                                  "fault here; nothing posted, the cut will not progress");
            std::fflush(stderr);
        }
    }
    if (!work) return 1;

    Object::InternalPtr<Work> workIBP(work);   // the ctor is a plain store
    wc->PostWork(0x401, 0, &workIBP);
    return 1;
}

// ============================================================
// state 20 UPDATE — retail 0x1400503e0
// Unconditional release of the work slot (no HasWorkSlot test, unlike state 10's update).
// ============================================================
char OrchardComponent::UpdateClearing() {
    WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
    wc->CancelWork();   // 0x14012eb50
    return 1;
}

// ============================================================
// state 20 TRANS38 (per-frame) — retail 0x140050430
// Find the entity's ClearAreaComponent (FUN_14000a170) and ask FUN_140118440 whether the
// footprint is clear yet. When it is, request state 9 — i.e. a cut orchard re-plants
// itself with whatever tree type is now selected. That round trip through state 9 is
// what makes "cut" the REPLACE command in the panel.
// ============================================================
char OrchardComponent::TickClearing(StateMachine* sm) {
    ClearAreaComponent* ca = _entity->FindComponent<ClearAreaComponent>();   // 0x14000a170

    // SA: retail derefs the finder result unguarded (caComp+0x18 is the description). A
    // null here is NOT benign — ClearAreaCtrl_IsDone answers `true` for a null component,
    // which would bounce the orchard straight back to state 9 and read exactly like the
    // "cut does nothing" symptom it is meant to fix. Report and stay put instead.
    if (!ca) {
        static int s_n = 0;
        if (s_n++ < 4) {
            std::fprintf(stderr, "[Orchard20] perFrame: entity has NO ClearAreaComponent — "
                         "the cut cannot complete (retail would fault here)\n");
            std::fflush(stderr);
        }
        return 1;
    }

    const bool done = ca->IsDone();
    {   // "waiting on the woodcutters" and "finished" are the only two outcomes, and they
        // look identical from outside; print the first four only.
        static int s_n = 0;
        if (s_n < 4) {
            ++s_n;
            std::fprintf(stderr, "[Orchard20] perFrame #%d: cleared=%d -> %s\n",
                         s_n, (int)done, done ? "request state 9 (replant)" : "still clearing");
            std::fflush(stderr);
        }
    }
    if (done) OrchardRequestState(sm, 9);
    return 1;
}

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm[, time]): [node+0x18] is the controller's back-pointer, this
// component, so each entry is a one-line adapter that calls the member. The tree's
// null-component guards stay in the adapters.
namespace {
char OrchEnterIdle(OrchardComponent* c, StateMachine*)                { return c ? c->EnterIdle() : 0; }         // 0x140051530
char OrchTickIdle(OrchardComponent* c, StateMachine* sm, const System::Time*)       { return c ? c->TickIdle(sm) : 1; }        // 0x1400514c0
char OrchOnPlant(OrchardComponent* c, StateMachine* sm)               { return c ? c->OnPlantEvent(sm) : 1; }    // 0x140051450
char OrchEnterPlanting(OrchardComponent* c, StateMachine*)            { return c ? c->EnterPlanting() : 1; }     // 0x140051070
char OrchTickPlanting(OrchardComponent* c, StateMachine* sm, const System::Time*)   { return c ? c->TickPlanting(sm) : 1; }    // 0x140050f90
char OrchOnHarvest(OrchardComponent* c, StateMachine* sm)             { return c ? c->OnHarvestEvent(sm) : 1; }  // 0x140050900
char OrchOnClear(OrchardComponent* c, StateMachine* sm)               { return c ? c->OnClearEvent(sm) : 1; }    // 0x1400513e0
char OrchEnterTending(OrchardComponent* c, StateMachine* sm)          { return c ? c->EnterTending(sm) : 1; }    // 0x140050e30
char OrchUpdateTending(OrchardComponent* c, StateMachine*)     { return c ? c->UpdateTending() : 1; }     // 0x140050ba0 / 0x1400509b0
char OrchTickTending(OrchardComponent* c, StateMachine* sm, const System::Time*)    { return c ? c->TickTending(sm) : 1; }     // 0x140050c00
char OrchEnterReplanting(OrchardComponent* c, StateMachine*)          { return c ? c->EnterReplanting() : 1; }   // 0x140050ae0
char OrchTickReplanting(OrchardComponent* c, StateMachine* sm, const System::Time*) { return c ? c->TickReplanting(sm) : 1; }  // 0x140050a10
char OrchEnterHarvesting(OrchardComponent* c, StateMachine*)          { return c ? c->EnterHarvesting() : 1; }   // 0x140050800
char OrchTickHarvesting(OrchardComponent* c, StateMachine* sm, const System::Time*) { return c ? c->TickHarvesting(sm) : 1; }  // 0x140050650
char OrchEnterClearing(OrchardComponent* c, StateMachine*)            { return c ? c->EnterClearing() : 1; }     // 0x1400504e0
char OrchUpdateClearing(OrchardComponent* c, StateMachine*)    { return c ? c->UpdateClearing() : 1; }    // 0x1400503e0
char OrchTickClearing(OrchardComponent* c, StateMachine* sm, const System::Time*)   { return c ? c->TickClearing(sm) : 1; }    // 0x140050430
}  // namespace

// Replica of OrchardDescription_helper_20f0 (0x1400520f0), in registration order.
// State ids are literal; the names in comments are inferred from the bodies.
static void OrchardCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = OrchardDispatch::table;
    if (t.count != 0) return;                       // idempotent, as pasture's is

    // ── state 2 — DEFAULT (idle; GetDefaultState 0x1400026c0 is `MOV EAX,2; RET`) ──
    // enter    0x140051530  deactivate the workplace
    // trans38  0x1400514c0  if enabled -> request state 9   (the idle->plant trigger)
    // event 3  0x140051450  unconditional request of state 9  (the panel's plant cmd)
    TableRegisterState(t, 2, &OrchEnterIdle, nullptr,
                       &OrchTickIdle, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 9, nullptr);
    TableAddEvent(t, 3, &OrchOnPlant);

    // ── state 9 — PLANT GRID ──────────────────────────────────────────────────────
    // enter    0x140051070  PlantOrchard_impl_1070: activates the workplace, reads the
    //                       footprint rect + desc spacing, RESIZES THE SLOT ARRAY to
    //                       cols*rows (this is what makes slotCount non-zero), then posts
    //                       a PlantOrchard work carrying treeType
    // trans38  0x140050f90  when every slot reaches state 1 -> request state 10
    // event 5  0x1400513e0  -> request state 20
    TableRegisterState(t, 9, &OrchEnterPlanting, nullptr,
                       &OrchTickPlanting, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 10, nullptr);
    TableAddTransition(t, 20, nullptr);
    TableAddEvent(t, 5, &OrchOnClear);

    // ── state 10 — TEND / IDLE-GROWN ──────────────────────────────────────────────
    // enter    0x140050e30  hole -> state 12, else post a TendArea work
    // update   0x140050ba0  deactivate the workplace if active
    // trans38  0x140050c00  TryHarvest; hole -> 12; seasonal TendArea re-post
    // event 5  0x1400513e0 -> state 20 ; event 4  0x140050900 -> state 11
    TableRegisterState(t, 10, &OrchEnterTending, nullptr,
                       &OrchTickTending,
                       &OrchUpdateTending);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 12, nullptr);
    TableAddTransition(t, 11, nullptr);
    TableAddTransition(t, 20, nullptr);
    TableAddEvent(t, 5, &OrchOnClear);
    TableAddEvent(t, 4, &OrchOnHarvest);

    // ── state 12 — REPLANT GAPS ───────────────────────────────────────────────────
    // enter    0x140050ae0  PlantOrchard_impl_0ae0: post a PlantOrchard for the holes
    // update   0x1400509b0  (state 10's update body)
    // trans38  0x140050a10  TryHarvest; no holes left -> request state 10
    // event 5  0x1400513e0 -> state 20 ; event 4  0x140050900 -> state 11
    TableRegisterState(t, 12, &OrchEnterReplanting, nullptr,
                       &OrchTickReplanting,
                       &OrchUpdateTending);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 10, nullptr);
    TableAddTransition(t, 11, nullptr);
    TableAddTransition(t, 20, nullptr);
    TableAddEvent(t, 5, &OrchOnClear);
    TableAddEvent(t, 4, &OrchOnHarvest);

    // ── state 11 — HARVEST ────────────────────────────────────────────────────────
    // enter    0x140050800  HarvestOrchard_impl_0800: post a HarvestOrchard work, then
    //                       stamp the footprint cells with treeType
    // trans38  0x140050650  harvest-complete latch -> request state 10
    // event 5  0x1400513e0 -> state 20
    TableRegisterState(t, 11, &OrchEnterHarvesting, nullptr,
                       &OrchTickHarvesting, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 10, nullptr);
    TableAddTransition(t, 20, nullptr);
    TableAddEvent(t, 5, &OrchOnClear);

    // ── state 20 (0x14) — CLEAR ───────────────────────────────────────────────────
    // enter    0x1400504e0  ClearArea_impl_04e0: post a ClearArea work at prio 0x401
    // update   0x1400503e0  deactivate the workplace
    // trans38  0x140050430  when the footprint tests clear -> request state 9
    TableRegisterState(t, 20, &OrchEnterClearing, nullptr,
                       &OrchTickClearing,
                       &OrchUpdateClearing);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 9, nullptr);

    // State 3 is the shared teardown/"stop" id every state can transition to. Retail
    // registers NO body for it in this table — it is a transition target only, so it
    // is deliberately absent here too rather than added as an empty state.
}

// Built ONCE at static-init time, as retail's OrchardDescription_helper_20f0 does
// (B38 D6/D7). Before, every OrchardComponent factory call re-ran it from both threads.
static const bool s_orchardStateTableBuilt = (OrchardCtrl_PopulateTable(), true);
