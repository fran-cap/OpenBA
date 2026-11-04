// src/game/Component/orchardcomponent.cpp
// Clean reimplementation of OrchardComponent lifecycle methods from Application-x64.exe.
//
// OrchardComponent (0x68 bytes) extends ControllerComponent with:
//   +0x48 bool enabled, +0x4C int32 lastHarvestYear, +0x50 Array<OrchardSlot>,
//   +0x60 ExternalPtr treeType
//
// Vtable at 0x14015a180. Persist (0x140051de0) serializes the slot array.
//
// LEFT (B.4 COMP-d): GameUpdateLoop m_game / m_dayCounter are void* (W0 header) and
// the Map's 0x30-byte cell record (array at +0x80, +0x08 = the entity on the cell)
// has no type yet (GAME-b); the StateMachine at StateMachineComponent+0x48 is a void*
// (COMP-e's header); WorkPlaceComponent_SetAccepting takes an int64_t (COMP-f).

#include "Component/OrchardComponent.h"
#include <cstdint>
#include "Entity/ComponentEntity.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/FruitComponent.h"
#include "Component/FruitDescription.h"
#include "Component/OrchardDescription.h"
#include "Component/StateMachineComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::cells (GameUpdateLoop::m_game)
#include "Game/Calendar.h"         // GameUpdateLoop::m_dayCounter
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"     // Reimpl_ExternalPtr_Serialize_StringTable
#include "Game/exe_constants.h"  // kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <cstdio>
#include <new>
#include <atomic>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"

// ============================================================
// Helpers, called by name (B3R)
// ============================================================
//   0x140051830 OrchardComponent::ValidateRefs  -- validate entity references in orchard
//   0x140009f80 OrchardComponent::SetSlotCount  -- resize the slot array at +0x50
//   0x14000a230 SerializeSlots                  -- serialize the slot array entries
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable -- serialize the ExternalPtr at +0x60


// ============================================================
// Reimplemented exe helpers (callee bodies for the guarded call sites above)
// ============================================================

// Object::Base::IsA(self, typeId) -> bool — retail calls it through the IAT dispatch slot
// at 0x140151410 (`CALL qword ptr [0x140151410]` at 0x1400518bd). It was once bound to exe
// 0x140002d40, which is not IsA at all but DynArray8_EraseByValue, and has no
// reimplementation, so the resolve returned 0 and ValidateRefs' guard failed every time —
// silently demoting every correctly-bound slot to "unusable" (measured: msg 0 bound
// entity+natRes+fruit, the very next validate pass tore it back down, and the orchardist
// replanted the same cell forever).
// 0x1400052c0 is FindComponent<NaturalResourceComponent> -- used by ref validation.
// Type-id globals referenced by the serializer (entity-ref kind filters).
// ⚠ This was bound to 0x1401c79e0 (MapComponent) and named accordingly. Both retail sites
// that use it — the ref serializer (0x14000a230) and ValidateRefs (0x140051830) — read
// `dword [0x1401c5d70]` = ComponentEntity, verified from the disassembly. The old global is
// a real, registered, NON-ZERO id of the wrong class, so every null check passed and the
// filter simply rejected everything: reference_wrong_but_nonzero_typeid. The slot the two
// sites test is a tile record's +0x08, which holds the ENTITY standing on the tile (dc20
// stores the owner IBP from MapComponent+0x10) — an entity is never a MapComponent.

// 0x140009f80: Array<OrchardSlot>::SetSize on +0x50 (grow: copy old, zero the new
// entries' three pointers, free old).
void OrchardComponent::SetSlotCount(int newCount) {
    const int oldCount = slotCount;
    if (newCount == oldCount) return;
    OrchardSlot* oldBuf = slots;
    slotCount = newCount;
    if (slotCapacity < newCount) {
        slotCapacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) slotCapacity = newCount * 3;
        slots = (System::MemoryAllocator::_singleton)
                    ? static_cast<OrchardSlot*>(System::MemoryAllocator::MemAlloc(
                          (uint64_t)slotCapacity * sizeof(OrchardSlot), 8))
                    : nullptr;
    }
    const int keep = (oldBuf == slots) ? oldCount : 0;
    for (int idx = keep; idx < newCount; ++idx) {
        OrchardSlot& dst = slots[idx];
        if (idx < oldCount) {
            const OrchardSlot& src = oldBuf[idx];
            dst.tile     = src.tile;
            dst.state    = src.state;
            dst.tree     = src.tree;
            dst.resource = src.resource;
            dst.fruit    = src.fruit;
            dst.picked   = src.picked;
        } else {
            dst.tree     = nullptr;
            dst.resource = nullptr;
            dst.fruit    = nullptr;
        }
    }
    if (oldBuf != slots) System::MemoryAllocator::MemFree(oldBuf);
}

// 0x14000a230: serialize the slot array entries (2 ints, 3 InternalPtrs, 1 bool).
static void SerializeSlots(IO::Serializer& ser, int count, OrchardSlot* slot) {
    if (count <= 0) return;
    for (int i = 0; i < count; ++i, ++slot) {
        ser.Serialize(slot->tile, nullptr);
        ser.Serialize(slot->state, nullptr);

        // LEFT: InternalBasePtr has no typed view of a bare pointer member.
        slot->tree.InternalBasePtr::Serialize(ser);
        if (slot->tree != nullptr &&
            !(::Object::IsDerivedFromNullSafe(slot->tree, ComponentEntity::TypeId())))
            slot->tree = nullptr;

        // LEFT: as above.
        slot->resource.InternalBasePtr::Serialize(ser);
        if (slot->resource != nullptr &&
            !(::Object::IsDerivedFromNullSafe(slot->resource, NaturalResourceComponent::TypeId())))
            slot->resource = nullptr;

        // LEFT: as above.
        slot->fruit.InternalBasePtr::Serialize(ser);
        if (slot->fruit != nullptr &&
            !(::Object::IsDerivedFromNullSafe(slot->fruit, FruitComponent::TypeId())))
            slot->fruit = nullptr;

        ser.Serialize(slot->picked, nullptr);
    }
}

// 0x140051830: validate orchard entity refs against live world entities; clear stale.
void OrchardComponent::ValidateRefs() {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (!world) return;
    const MapCell* cells = world->m_game->cells.GetData();
    const int count = slotCount;
    for (int i = 0; i < count; ++i) {
        OrchardSlot& slot = slots[i];
        if (slot.state != 1) continue;

        const int32_t tile = slot.tile;
        ComponentEntity* live = cells[tile].occupant.Get();   // the entity standing on the cell
        bool ok = false;
        if (live) {
            if (live->IsA(ComponentEntity::TypeId())) {   // slot 0x140151410
                if (live->FindComponent<NaturalResourceComponent>() != nullptr) {   // 0x1400052c0
                    if (live == slot.tree.Get()) ok = true;
                }
            }
        }
        if (ok) continue;
        {   // This is the ONLY demotion out of state 1, and it silently undoes a successful
            // msg-0 bind — the orchard planted a tree, then reported the slot as unusable and
            // replanted the same cell forever. Four independent conditions decide it; say which
            // one said no.
            static int s_n = 0;
            if (s_n++ < 8) {
                ComponentEntity* bound = slot.tree.Get();
                std::fprintf(stderr, "[OrchardValidate] slot=%d tile=%d DEMOTING 1->2: live=%p "
                             "bound=%p typeIdCompEntity=%u isA=%d liveEqBound=%d\n",
                             i, tile, static_cast<void*>(live), static_cast<void*>(bound),
                             ComponentEntity::TypeId(),
                             live ? (int)live->IsA(ComponentEntity::TypeId()) : -1,
                             (int)(live == bound));
                std::fflush(stderr);
            }
        }
        // Stale -> mark removed (kind=2) and null the 3 IBP slots.
        slot.state    = 2;
        slot.tree     = nullptr;
        slot.resource = nullptr;
        slot.fruit    = nullptr;
    }
}

// ============================================================
// OrchardComponent::GetGrowth (0x140051640) -> float
// ============================================================
// Average growth ratio (0.0-1.0) across the orchard's planted tree slots.
// This is what OrchardUI::Slot7 renders as the panel's yield bar + "NN%" text;
// with it unresolved the percentage rendered blank (and, once the component
// finder started working, calling the null pointer crashed the frame).
//
// The tile lookup is the entity pointer inside a 0x30-byte Map cell (+0x08).
//
// The `entity == slot.tree` re-check is retail's staleness test: a slot whose
// tile now holds a DIFFERENT entity (tree chopped, something else built) is
// skipped rather than counted. Note the divisor is the FULL slot count, not the
// number of slots that scored — an orchard of saplings correctly reads low.
//
// The per-slot clamp is against the 1.0f constant at 0x14017214c (Ghidra names
// that float g_fUVScale; it is a plain 1.0f used here as the growth ceiling).
float OrchardComponent::GetGrowth() const {
    const int count = slotCount;
    if (count <= 0) return 0.0f;

    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (!world) return 0.0f;
    const Map* map = world->m_game;
    if (!map) return 0.0f;
    const MapCell* cells = map->cells.GetData();
    if (!cells) return 0.0f;

    const uint32_t tidEntity = ComponentEntity::TypeId();   // cell 0x1401c5d70 (D5)
    const float maxGrowth = kRdata_14017214c;   // 0x14017214c

    if (!slots) return 0.0f;

    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        const OrchardSlot& slot = slots[i];

        ComponentEntity* entity = cells[slot.tile].occupant.Get();   // the entity standing on the cell
        if (!entity) continue;
        if (!entity->IsA(tidEntity)) continue;

        if (entity->FindComponent<NaturalResourceComponent>() == nullptr) continue;   // 0x1400052c0

        if (entity != slot.tree.Get()) continue;
        if (slot.state != 1) continue;

        if (!slot.fruit) continue;   // SA: retail derefs unguarded; state 1 implies it
        float g = slot.fruit->ripeness;
        if (g > maxGrowth) g = maxGrowth;
        total += g;
    }

    return total / static_cast<float>(count);
}


// ============================================================
// OrchardComponent::OnMessage — primary vtable idx15 (+0x78), retail 0x140051990.
// ABI confirmed by src/game/Action/sendcomponentmessage.cpp:231, which dispatches
// vtable+0x78 as (comp, msgId, srcEntityRef, p1, p2). p1 is the SLOT INDEX.
//
// This is the writer the whole planting loop was missing. PlantOrchard::Slot4 skips any
// slot whose tree already holds an entity and marks its own validity byte, and
// PlantOrchard::OnComplete clears that byte again on completion — so with nothing ever
// writing slot.tree the orchardist replanted the same cell forever (measured: 29 executed
// plant actions, slot histogram still 12x state 0, nonNullEntity=0).
//
//   msg 0 "planted"  — read the tile the slot points at, take the entity standing on it,
//                      and bind {entity, NaturalResourceComponent, FruitComponent} into the
//                      slot with state 1. If ANY of the three is missing the slot goes to
//                      state 2 (removed/unusable) instead — retail's own failure branch.
//   msg 1 "harvested" — flag the slot (picked) and reset the fruit component's yield
//                      (+0x30 = 0, +0x34 = 1).
//   msg 2 "boost"    — advance the tree's fruit growth by the orchard description's rate.
//
// ⚠ Two faithful landmines kept verbatim: msg 2 reads the boost rate through a pointer that
// retail nulls when IsDerivedFrom fails, and BoostFruit does the same with the fruit
// description's rate. Both are load-bearing — they only fire if a type-id global is unfilled,
// which is a bug to fix at the registration, not to paper over here (see
// reg_orchard_typeid.cpp / reg_component_fruit.cpp).
// ============================================================

// 0x14000a0b0 is FindComponent<FruitComponent>. 0x140172234 = 300.0f (kRdata_140172234).

// 0x140049fc0 — advance a FruitComponent's growth by `scale` worth of its description's
// rate, by synthesising a Time and calling the component's own Update (slot 9).
static void BoostFruit(FruitComponent* fruit, float scale) {
    Description* desc = fruit->_description;
    FruitDescription* validated = nullptr;
    if (::Object::IsDerivedFromNullSafe(desc, FruitDescription::TypeId()))
        validated = static_cast<FruitDescription*>(desc);
    const float rate  = validated->_ripenSeasons;
    const float secs  = rate * kRdata_140172234 * scale;
    // System::Time is 8 bytes — a 4-byte target here is the documented clobber trap.
    System::Time timeVal{0};
    RtSystem_Time_Ctor(&timeVal, secs);
    fruit->Update(&timeVal);   // slot 9
}


// ============================================================
// The PANEL COMMAND PATH — primary vtable idx13 (CanCommand) and idx14 (Command)
// ============================================================
// Slot indices verified from the vtable BYTES at 0x14015a180: +13*8 = 0x14015a1e8 reads
// {0x140051c90, 0x140051ba0, 0x140051990, 0x1400026c0} — and the last two are the
// addresses this file already cites for OnMessage (idx15) and GetDefaultState (idx16),
// which is what pins the indexing.
//
// WHY THEY WERE MISSING: neither slot was ever assigned, so both kept the shared inert
// stub MakeInertTyped installs — "returns 0 for every non-dtor slot". CanCommand
// returning 0 unconditionally is exactly the reported symptom: every orchard panel
// button renders disabled regardless of state, and a click dies at the first hop. This
// is the same trap the slot-15 comment below documents, so the wiring hangs off the
// same PopulateFn hook.
namespace {

// Finders called here: 0x140003ea0 FindComponent<StateMachineComponent>, 0x140003050
// FindComponent<WorkComponent>, 0x140002e10 FindComponent<WorkPlaceComponent>; leaves
// 0x14012ead0 WorkComponent_CheckWorkSlot, 0x14012fc00 WorkPlaceComponent_SetAccepting.

// The SM's current state id: +0x52 (pending), falling back to +0x50 when negative.
// NB these are offsets on the StateMachineComponent, and the `sm` the state handlers
// use is that component + 0x48.
int OrchardCurrentStateId(const StateMachineComponent* smc) {
    int8_t v = smc->stateMachine.pendingState;
    if (v < 0) v = smc->stateMachine.currentInput;
    return v;
}

int OrchardPlantedCount(const OrchardComponent* s) {
    const int count = s->slotCount;
    const OrchardSlot* slots = s->slots;
    int n = 0;
    for (int i = 0; slots && i < count; ++i) {
        const int st = slots[i].state;
        if (st != 0 && st == 1) ++n;      // retail's redundant pair, kept verbatim
    }
    return n;
}

// The in-game year: the first int of the day counter at GameUpdateLoop+0x128.
int OrchardYearOrZero() {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Calendar* cal = world ? world->m_dayCounter : nullptr;
    return cal ? cal->year : 0;
}

// Reports what the panel was told, and WHY, one line per change of the deciding inputs.
//
// ⚠ Budget PER COMMAND ID, and fire ON CHANGE rather than first-N. A first-N budget CANNOT
// answer "why is the harvest button greyed out": the panel polls CanCommand every frame from
// the moment the orchard exists, so the first three polls are spent hundreds of frames before
// the player ever opens the panel, and the values at the moment they actually looked are
// never logged. That is exactly what happened on 2026-08-10 — the trace reported state=9
// (still planting) for a question asked in state 10. Third instance of the saturating-counter
// trap in this wave. It also prints the RETURN, not just the inputs: a diagnostic that makes
// the reader re-derive the predicate is one the reader can get wrong.
void TraceCanCommand(int cmdId, int curState, int planted, int slotCount, bool hasType,
                     char masterSwitch, int lastHarvestYear, uint8_t result) {
    struct Key { int state, planted, slots, hasType, sw, year, result; bool valid; };
    static Key s_last[8] = {};
    const int bucket = (cmdId >= 0 && cmdId < 8) ? cmdId : 7;
    Key k{curState, planted, slotCount, (int)hasType, (int)masterSwitch, lastHarvestYear,
          (int)result, true};
    Key& prev = s_last[bucket];
    if (prev.valid && prev.state == k.state && prev.planted == k.planted &&
        prev.slots == k.slots && prev.hasType == k.hasType && prev.sw == k.sw &&
        prev.year == k.year && prev.result == k.result)
        return;
    prev = k;

    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Calendar* cal = world ? world->m_dayCounter : nullptr;
    const int curYear = cal ? cal->year : -1;

    // Name the gate that actually decided it, so a greyed button reports its own reason.
    const char* why = "enabled";
    if (result == 0) {
        if (!hasType)                    why = "no tree type selected";
        else if (cmdId == 1 && curState != 10) why = "not in state 10 (tend) — harvest only "
                                                     "offered on a grown orchard";
        else if (cmdId == 1)             why = "already harvested this year (+0x4c == curYear)";
        else if (cmdId == 3 && planted == 0)   why = "nothing planted";
        else if (cmdId == 3)             why = "already clearing (state 20)";
        else                             why = "busy or fully planted";
    }
    std::fprintf(stderr, "[OrchardCmd] CanCommand(id=%d) -> %u (%s): state=%d planted=%d/%d "
                 "hasType=%d switch=%d lastHarvestYear=%d curYear=%d\n",
                 cmdId, result, why, curState, planted, slotCount, (int)hasType,
                 (int)masterSwitch, lastHarvestYear, curYear);
    std::fflush(stderr);
}

}  // namespace

// ============================================================================
// ENDGAME B.3.8 MI-B — OrchardComponent as a REAL class (OrchardComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES (48-slot table filled per create, slots
// 3/4/13/14/15/18-21 written from InstallLive, Inert_ScalarDtor in slot 0,
// Dispatcher<OrchardCtrlTag> stamped into raw bytes at +0x28). Slots 18-21 held
// ControllerLive_ScalarDeletingDtor, the class's type info / id and `return 3`: that
// is OrchardDescription's table 0x14015a210 read past this 18-slot table's end
// (B38 D3.5) — gone; nothing in the tree dispatches a component past slot 17.
// After: the compiler's 18-slot vtable, a real ctor/dtor chain, the controller a
// member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// 0x140051f40. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48 = 0; +0x4c = -1; +0x50 =
// +0x58 = +0x60 = 0; `_flags |= 0x80` (already set by Component()).
// +0x4c is the last-harvest year the state-10 harvest gate compares with the current
// year: -1 means "never harvested". The tree's factory zero-filled it, which read as
// "already harvested in year 0".
OrchardComponent::OrchardComponent()
    : _controller(this), enabled(false), _pad49{}, lastHarvestYear(-1), slots(nullptr),
      slotCount(0), slotCapacity(0), treeType(nullptr) {
    OrFlagsInCtor(0x80);
}

// 0x140051ed0: MemFree(+0x50), +0x50 = +0x58 = 0, then the restamps and ~Internal
// (the compiler chain).
OrchardComponent::~OrchardComponent() {
    Object::detail::InternalMemFree(slots);
    slots = nullptr;
    slotCount = 0;
    slotCapacity = 0;
}

// create 0x140009870: MemAlloc(0x68, 8), then the ctor.
::Object::Base* OrchardComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(OrchardComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) OrchardComponent());
}

// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* OrchardComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) OrchardComponent());
}

// ============================================================
// OrchardComponent::Persist (vtable slot [3], 0x140051de0)
// ============================================================
// Component_Serialize + empty controller serializer + second serializer:
//   validate refs, serialize enabled, lastHarvestYear, the slot array, the tree type
void OrchardComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;

    // Empty controller serializer block
    { IO::Serializer scope(*st, 0); }

    // Main data serializer
    IO::Serializer ser(*st, 0);

    // Validate entity references
    ValidateRefs();   // 0x140051830

    ser.Serialize(enabled, nullptr);
    ser.Serialize(lastHarvestYear, nullptr);

    // Serialize the slot count, resize the array if loading
    int localCount = slotCount;
    ser.Serialize(localCount, nullptr);

    if (static_cast<int>(st->GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        SetSlotCount(localCount);   // 0x140009f80
    }

    SerializeSlots(ser, localCount, slots);   // 0x14000a230

    // The tree type ExternalPtr at +0x60
    ::Object::SerializeExternal<ComponentDescription>(ser, treeType);   // 0x140002340
}

// [4] 0x140051dd0: `MOV qword [RCX+0x60],0` — one store, nothing else.
void OrchardComponent::Startup() { treeType = nullptr; }

// [13] 0x140051c90 / [14] 0x140051ba0 / [15] 0x140051990 below; the extern "C" doors
// of [13]/[14] stay above (src/diag/orchard_cut_harness.cpp calls them).
// idx13 CanCommand — retail 0x140051c90. Returns the button's enabled state.
//   id 0 WORK    : (no work slot held && planted < slots) || slots == 0, ANDed with hasType
//   id 1 HARVEST : state == 10 && lastHarvestYear != current year, ANDed with hasType
//   id 2 TOGGLE  : bit0 = hasType, bit1 = the enabled switch (a BITFIELD, not a bool —
//                  the panel reads bit 1 to draw the on/off state)
//   id 3 CLEAR   : planted > 0 && state != 20
// `hasType = (treeType != 0)` is ANDed into ids 0/1/2 — an untyped orchard is
// correctly inert in retail, which is why an unconfigured orchard shows dead buttons.
uint32_t OrchardComponent::CommandMask(int cmdId) {
    StateMachineComponent* smc = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    WorkComponent* wc = _entity->FindComponent<WorkComponent>();   // 0x140003050
    if (!smc) return 0;         // SA: retail derefs unguarded; every orchard entity has a
                                // StateMachineComponent, so this can only fire if the
                                // component was asked before its entity finished building.
    const int  curState   = OrchardCurrentStateId(smc);
    const int  planted    = OrchardPlantedCount(this);
    const bool hasType    = (treeType != nullptr);

    uint8_t result = 0;
    switch (cmdId) {
    case 0: {
        const bool busy = wc->IsWorkPosted() != 0;   // 0x14012ead0
        if ((!busy && planted < slotCount) || slotCount == 0) result = hasType ? 1 : 0;
        break;
    }
    case 1: {
        if (curState != 10) break;
        if (lastHarvestYear == OrchardYearOrZero()) break;
        result = hasType ? 1 : 0;
        break;
    }
    case 2:
        result = static_cast<uint8_t>(
            (static_cast<uint8_t>(-(enabled != 0)) & 2u) |
            (hasType ? 1u : 0u));
        break;
    case 3:
        result = (planted > 0 && curState != 20) ? 1 : 0;
        break;
    default:
        break;
    }
    TraceCanCommand(cmdId, curState, planted, slotCount, hasType,
                    static_cast<char>(enabled), lastHarvestYear, result);
    return result;
}

// idx14 Command — retail 0x140051ba0. Maps a button id to a NODE EVENT and posts it.
//   0 -> event 3 (plant/work)   1 -> event 4 (harvest)   3 -> event 5 (clear)
//   2 -> no event at all: flip the enabled switch and push it to the workplace.
// The post itself is node vtable slot 2 (+0x10):
//   (**(code**)(**(longlong**)(smc+0x48) + 0x10))(node, smc+0x48, (int)*(char*)(smc+0x51), eventId)
// i.e. curIdx comes from smc+0x51, which is (smc+0x48)+0x09 — the same sm+0x09 field
// OrchardRequestState reads. ControllerLive::Dispatcher::PostEvent implements that slot.
void OrchardComponent::Command(int cmdId) {
    StateMachineComponent* smc = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0

    int eventId;
    switch (cmdId) {
    case 0: eventId = 3; break;
    case 1: eventId = 4; break;
    case 3: eventId = 5; break;
    case 2: {
        // Toggle the master switch and mirror it onto the workplace. Retail returns here:
        // no event is posted for this command.
        enabled = !enabled;
        WorkPlaceComponent* wp = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        wp->SetAccepting(enabled);   // 0x14012fc00 (WorkPlaceComponent_SetAccepting's member)
        std::fprintf(stderr, "[OrchardCmd] Command(2) toggle -> enabled=%d\n", (int)enabled);
        std::fflush(stderr);
        return;
    }
    default: return;
    }

    if (!smc) return;
    ControllerComponent::Controller* node = smc->stateMachine.current;
    const int curIdx = smc->stateMachine.activeState;   // +0x51
    std::fprintf(stderr, "[OrchardCmd] Command(%d) -> post node event %d (node=%p curIdx=%d)\n",
                 cmdId, eventId, static_cast<void*>(node), curIdx);
    std::fflush(stderr);
    if (!node) return;
    // node = the controller member the StateMachineComponent linked (D6); slot 2 PostEvent.
    node->PostEvent(
        &smc->stateMachine, curIdx, eventId);
}

// Diagnostic counters of the [OrchardMsg] lines (B6-5: namespace-scope atomics,
// not function-local statics).
static std::atomic<int> s_orchardMsgSeen[8] = {};
static std::atomic<int> s_orchardMsg0Traced{0};

void OrchardComponent::OnMessage(int msgId, void* /*srcEntityRef*/, uint32_t slotIdx, uint32_t /*p2*/) {
    {   // first few deliveries only — this is the handler the slot array depends on, and
        // "was it ever called at all" is the question its silence raises.
        // Per-message-id budget: a single shared counter let the 12 planting deliveries
        // (msg 0) hide every harvest delivery (msg 1) behind an exhausted budget.
        const int bucket = (msgId >= 0 && msgId < 8) ? msgId : 7;
        if (s_orchardMsgSeen[bucket].fetch_add(1, std::memory_order_relaxed) < 6) {
            std::fprintf(stderr, "[OrchardMsg] OnMessage(comp=%p msg=%d slot=%u)\n",
                         static_cast<void*>(this), msgId, slotIdx);
            std::fflush(stderr);
        }
    }
    const uintptr_t idx = static_cast<uintptr_t>(slotIdx);
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (!world) return;

    if (msgId == 0) {
        if (!slots) return;
        OrchardSlot& slot = slots[idx];
        const MapCell* cells = world->m_game->cells.GetData();
        // The cell's occupant (+0x08) is the entity standing on it — the tree the plant
        // action just created.
        ComponentEntity* entity = cells[slot.tile].occupant.Get();
        NaturalResourceComponent* natRes = entity->FindComponent<NaturalResourceComponent>();   // 0x1400052c0
        FruitComponent* fruit = entity->FindComponent<FruitComponent>();   // 0x14000a0b0

        {   // msg 0 is the ONLY writer of the slot's {entity, natRes, fruit} triple, and its
            // two outcomes — bound (state 1) and unusable (state 2) — are indistinguishable
            // from the slot histogram alone. Say which of the three came back null.
            if (s_orchardMsg0Traced.fetch_add(1, std::memory_order_relaxed) < 8) {
                std::fprintf(stderr, "[OrchardMsg]   msg0 slot=%u tile=%d -> entity=%p "
                             "natRes=%p fruit=%p => state %d\n", slotIdx, (int)slot.tile,
                             static_cast<void*>(entity), static_cast<void*>(natRes),
                             static_cast<void*>(fruit),
                             (entity && natRes && fruit) ? 1 : 2);
                std::fflush(stderr);
            }
        }
        if (entity != nullptr && natRes != nullptr && fruit != nullptr) {
            slot.state    = 1;          // planted
            slot.tree     = entity;
            slot.resource = natRes;
            slot.fruit    = fruit;
            return;
        }
        slot.state = 2;                 // unusable
        return;
    }

    if (msgId == 1) {
        ValidateRefs();
        if (!slots) return;
        OrchardSlot& slot = slots[idx];
        slot.picked = true;
        if (slot.fruit != nullptr && slot.tree != nullptr) {
            slot.fruit->ripeness  = 0.0f;
            slot.fruit->fruitFlag = 1;
        }
        return;
    }

    if (msgId == 2) {
        const Map* mapGrid = world->m_game;
        ValidateRefs();
        if (!slots) return;
        ComponentEntity* entity = mapGrid->cells.GetData()[slots[idx].tile].occupant.Get();
        if (entity == nullptr) return;
        FruitComponent* fruit = entity->FindComponent<FruitComponent>();   // 0x14000a0b0
        if (fruit == nullptr) return;
        Description* desc = _description;
        OrchardDescription* validated = nullptr;
        if (::Object::IsDerivedFromNullSafe(desc, OrchardDescription::TypeId()))
            validated = static_cast<OrchardDescription*>(desc);
        BoostFruit(fruit, validated->_boostRate);   // 0x140049fc0
        return;
    }
}

DEFINE_EXE_TYPE(OrchardComponent, "ControllerComponent")
