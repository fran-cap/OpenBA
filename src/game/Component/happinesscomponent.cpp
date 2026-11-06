// src/game/Component/happinesscomponent.cpp
// Clean reimplementation of HappinessComponent lifecycle methods from Application-x64.exe.
//
// HappinessComponent (0x50 bytes) -- ControllerComponent subclass with shifted
// controller sub-object at +0x30 (own data byte at +0x28).
// Tracks citizen happiness state.
//
// Vtable at 0x1401595d0:
//   [3] Shutdown  = 0x14004aa80 -- serialize base + registered
//   [7] OnRemove  = 0x14004aa20 -- ERASE from the happiness list (Ghidra names this
//                                  slot OnCreate on several components; it is the remove side)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/HappinessComponent.h"
#include <cstdint>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Work/exe_bodies.h"   // WorkBase_FindComponent_6270
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/HappinessDescription.h"     // ENDGAME B.3.9 C1: 0x1401c42f8 cell class
#include "Component/RadiusDescription.h"        // ENDGAME B.3.9 C1: 0x1401c4870 cell class
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7cb8 cell class
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"      // ResourceCategory 0x140088f90 (OnMessage)
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "ext/registry.h"             // Ext::Fix().tavern_counter
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays

// ============================================================
// Helpers, called by name (B3R)
// ============================================================
// 0x140002cc0 Array<T>::Remove = DynArray8_EraseByValue -- REMOVES every element
// equal to *value, shifting the tail down and decrementing the count. It is NOT a push; the
// append counterpart is DynArray8_ResizeFill (0x140003110), which is what Register uses.
// Ghidra's own plate comment on the callee records that leaving it unnamed already caused
// two component OnCreate/OnRemove vtable entries to be mislabelled as the create side.
// 0x14004a300 HappinessComponent::UpdateInfluence (below) -- stamps / lifts the influence disk.
// FUN_140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised into the exe
// (ENDGAME B.3.2 step 2) — converted at its call site below.
// 0x140002e10 AttendanceComponent_GetJobHostComponent(entity, outIBP) -- the owner's
// workplace, used by the "needs staff" arm of IsActive.
// 0x140006270 WorkBase_FindComponent_6270(entity, outIBP) -- the storage, "needs goods" arm.
// IsDerivedFrom (slot 0x140152a98) is Object::Base::IsDerivedFrom.
// DAT_1401c3dd8 (the game world) is read through g_pGameWorldSingleton, the engine_funcs
// global bound to the same cell.
//
// ENDGAME B.3.9 C1 (D5): 0x1401c42f8 (HappinessDescription) and 0x1401c7cb8
// (RawMaterialDescription, whose +0x5c flags the goods test masks) are read
// BY CELL below, as the class's static TypeId().

// ============================================================
// HappinessComponent::Shutdown (vtable slot [3], 0x14004aa80) — the Persist member below
// ============================================================
// Ghidra decompilation:
//   Component_Serialize(param_1, param_2)
//   Serializer(local_20, param_2, 0); ~Serializer(local_20)  // empty controller block
//   Serializer(local_30, param_2, 0)
//   Serialize(local_30, param_1+0x28, nullptr)  // registered as bool
//   ~Serializer(local_30)

// ============================================================
// HappinessComponent::OnRemove (vtable slot [7], 0x14004aa20) — the member below
// ============================================================
// Ghidra decompilation:
//   if (param_1[0x28] != 0) {
//     InternalBasePtr::Ctor(local, param_1)
//     FUN_140002cc0( *(gameWorld + 0xF0) + 0x50, local )
//     FishingComponent_impl_a300(param_1, -1)
//     param_1[0x28] = 0
//   }

// ============================================================
// Reimpl of FUN_14004a300 (FishingComponent_impl_a300: notify the population
//   manager of a fishing tile, mapping the description's mode to a state id)
// ============================================================
// Despite the local typedef/function names (borrowed from the fishing lane's
// near-identical shape), this reads a HappinessDescription off owner+0x18 (type
// 0x1401c42f8 -- INVENTORY_C Table 1 says HappinessDescription, not
// "FishingDescription" as the old comment guessed), locates the RadiusDescription
// (type 0x1401c4870 -- Table 1, not a generic "fishing-target component") on the
// host, the MapComponent (FUN_140004680) for tile coords, maps desc+0x20 (0..4)
// to a state id, and calls the helper below (0x14009f990).
// 0x140004680 is FloorComponent_FindResData, 0x14009f990 FishingComponent_BrushTile (below).

// ============================================================
// Reimpl of FishingComponent_helper_f990 (0x14009f990) — Component reg wave B.
// Stamps a circular brush of radius `r` (param5) onto a population grid: for every
// cell within the disk it adds `delta` (param6) to a per-cell byte, then bumps a
// per-state accumulator short. mgr (param1): +0 = grid width (int), +0x80 = grid
// base ptr; cell stride 0x30, the touched byte sits at cell+0x1c. The state-totals
// short array starts at mgr+0x90, indexed by `state` (param2). Pure leaf (no callees).
// Faithful 1:1 port of the Ghidra decompile.
// ============================================================
void Map::StampInfluence(int state, int cx, int cy, int r, short delta) {
    MapCell* grid = cells.GetData();   // +0x80
    int yy = cy - r;
    if (yy <= cy + r) {
        int dy = cy - yy;
        do {
            int xx = cx - r;
            if (xx <= r + cx) {
                int dx = cx - xx;
                int ix = xx;
                do {
                    int width = size;
                    if (ix >= 0 && yy >= 0 && ix < width && yy < width &&
                        dy * dy + dx * dx <= r * r) {
                        // MapCell +0x1C, the nine per-state bytes, indexed by `state`
                        uint8_t* cell = &grid[(int64_t)(width * yy + ix)].field_0x1C[(int64_t)state];
                        *cell = (uint8_t)(*cell + (uint8_t)delta);
                    }
                    ix++;
                    dx--;
                } while (ix <= r + cx);
            }
            yy++;
            dy--;
        } while (yy <= cy + r);
    }
    field_0x90[state] = (short)(field_0x90[state] + delta);
}

// Retail's `IsDerivedFrom(p, tid) ? p : nullptr` idiom on a component's description.
template <class T>
static T* AsType(Description* obj) {
    if (!obj || !T::TypeId()) return nullptr;
    return ::Object::IsDerivedFromNullSafe(obj, T::TypeId()) ? static_cast<T*>(obj) : nullptr;
}

// The first sub-description of `desc` whose Type() is T's, if it IsDerivedFrom T —
// retail's inlined description walk (Type match, then IsDerivedFrom, then stop).
template <class T>
static T* FindSubDescription(const ComponentDescription* desc) {
    const uint32_t tid = T::TypeId();
    for (int i = 0; i < desc->_subDescCount; ++i) {
        Description* sub = desc->_subDescs[i];
        if (sub->Type() == tid)
            return ::Object::IsDerivedFromNullSafe(sub, tid) ? static_cast<T*>(sub) : nullptr;
    }
    return nullptr;
}

// ============================================================
// HappinessComponent::UpdateInfluence — 0x14004a300 (Ghidra FishingComponent_impl_a300;
// every retail caller is a HappinessComponent body: 0x14004a4a0, 0x14004a4f0,
// 0x14004a940, 0x14004aa20). Stamps (delta +1) or lifts (delta -1) this building's
// influence disk on the map: the kind from HappinessDescription::_happinessType, the
// radius from the entity's RadiusDescription, the centre from its MapComponent.
// ============================================================
void HappinessComponent::UpdateInfluence(int16_t delta) {
    if (GameUpdateLoop::_singleton == nullptr) return;

    HappinessDescription* desc = AsType<HappinessDescription>(_description);

    // The RadiusDescription (0x1401c4870) among the entity description's sub-descriptions.
    RadiusDescription* radius = FindSubDescription<RadiusDescription>(_entity->_description);

    // The MapComponent for tile coords (0x140004680).
    MapComponent* mc = _entity->FindComponent<MapComponent>();

    // Map the description's kind (0..4) to a state id (default 5).
    int state = 5;
    switch (desc ? static_cast<int>(desc->_happinessType) : -1) {
        case 0: state = 0; break;
        case 1: state = 1; break;
        case 2: state = 2; break;
        case 3: state = 3; break;
        case 4: state = 4; break;
        default: break;
    }

    int x = (static_cast<int16_t>(mc->tileX1) >> 1) + static_cast<int16_t>(mc->tileX0);
    int y = (static_cast<int16_t>(mc->tileY1) >> 1) + static_cast<int16_t>(mc->tileY0);
    int r = radius ? radius->_radius : 0;

    GameUpdateLoop::_singleton->m_game->StampInfluence(state, x, y,   // +0xf8
                               r, delta);   // 0x14009f990
}

// ============================================================
// HappinessComponent::IsActive -- 0x14004a550
// ============================================================
// Decides whether this happiness source is currently DOING anything. Read off the
// disassembly at 0x14004a550; the three arms, in retail's order:
//
//   1. _happinessType == 5  -> unconditionally active. This is the plain radius source
//      (the chapel / well / town square kind): being built is enough.
//   2. _needsWorkers        -> needs staff. Find the owner's WorkPlaceComponent
//      (0x140002e10); with no workplace the staffing test is skipped entirely,
//      otherwise a workplace with no worker (workerCount == 0), or one that is closed
//      (isAccepting == 0), makes it inactive.
//   3. _needsGoods          -> needs goods. Walk the owner's StorageComponent slots
//      (0x140006270; slot records are 0x10 bytes, first qword = the stored good's
//      ComponentDescription) and AND that good's RawMaterialDescription flags (+0x5c)
//      against the happiness description's accepted-goods mask (_goodsFlags). Active
//      if any slot matches. This is the tavern: it cheers nobody up until there is ale.
//
// NOTE the inner RawMaterialDescription lookup is deliberately left unguarded. Retail
// falls out of that loop with RCX == 0 and reads [RCX+0x5c] regardless -- it relies on
// every stored good carrying a RawMaterialDescription. Guarding it here would silently
// skip the mask test rather than restore the behaviour, so the deref stays faithful; if
// it ever faults, the good in storage is what is wrong.
// The tree's null tests on the storage, on a slot's good and on a null sub-description
// are its own (retail has none); kept (B9).
bool HappinessComponent::IsActive() {
    Description* d = _description;
    const HappinessDescription* desc =
        (d && d->IsDerivedFrom(HappinessDescription::TypeId()))
            ? static_cast<HappinessDescription*>(d) : nullptr;

    if (static_cast<int>(desc->_happinessType) == 5) return true;

    if (desc->_needsWorkers) {
        WorkPlaceComponent* wp = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        if (wp) {
            if (wp->workers._size == 0) return false;
            if (!wp->isAccepting) return false;
        }
    }

    if (!desc->_needsGoods) return true;

    StorageComponent* store = _entity->FindComponent<StorageComponent>();   // 0x140006270

    const uint32_t wantMask = desc->_goodsFlags;
    bool matched = false;

    if (!store) return false;

    // The 0x10-byte item's first qword is the good's ComponentDescription.
    const StorageComponent::Item* items = store->storageSlots._data;   // +0x28
    for (int i = 0; i < store->storageSlots._size; ++i) {
        const ComponentDescription* good = items[i]._resource;   // Item +0x00

        const RawMaterialDescription* rawMat = nullptr;
        if (good) {
            const uint32_t tidRawMat = RawMaterialDescription::TypeId();
            for (int j = 0; j < good->_subDescCount; ++j) {
                Description* c = good->_subDescs[j];
                if (!c) continue;
                if (c->Type() == tidRawMat) {
                    if (c->IsDerivedFrom(tidRawMat))
                        rawMat = static_cast<RawMaterialDescription*>(c);
                    break;
                }
            }
        }
        // Faithful unguarded read -- see the note above.
        matched |= (wantMask & rawMat->_flags) != 0;
    }
    return matched;
}

// GameUpdateLoop::m_citizenList (+0xF0): the town manager, the CitizenBehavior, whose
// happiness list is read by name.
static CitizenBehavior* TownManager() {
    return GameUpdateLoop::_singleton->m_citizenList;
}

// ============================================================
// HappinessComponent::Register -- 0x14004a4f0
// ============================================================
// Stamp the happiness radius onto the map (UpdateInfluence +1), append the component
// to the town manager's happiness list (CitizenBehavior::happinessComponents), and
// latch `registered` so it is not registered twice.
void HappinessComponent::Register() {
    UpdateInfluence(1);   // 0x14004a300

    ::Object::InternalPtr<HappinessComponent> self(this);

    if (GameUpdateLoop::_singleton != nullptr) {
        CitizenBehavior* town = TownManager();
        if (town) {
            const int count = town->happinessComponents._size;
            town->happinessComponents
                .SetSizeFill(count + 1, &self);

            static int n = 0;
            if (n < 8) { ++n;
                fprintf(stderr, "[Happiness] source REGISTERED: comp=%p townMgr=%p "
                                "happinessCount=%d\n", static_cast<void*>(this),
                        static_cast<void*>(town), town->happinessComponents._size);
                fflush(stderr);
            }
        }
    }
    registered = true;
}

// ============================================================
// HappinessComponent::Unregister -- 0x14004a4a0
// ============================================================
// The mirror of Register, and byte-identical to the body of slot 7 / the state-3 enter
// minus their `if (registered)` guard.
void HappinessComponent::Unregister() {
    ::Object::InternalPtr<HappinessComponent> self(this);

    if (GameUpdateLoop::_singleton != nullptr) {
        CitizenBehavior* town = TownManager();
        if (town)
            town->happinessComponents.Remove(self);   // 0x140002cc0
    }
    UpdateInfluence(-1);   // 0x14004a300
    registered = false;
}

// ============================================================
// HappinessComponent state 2 / 13 "built" enter -- 0x14004a9f0
// ============================================================
// Register on entry, but only if not already registered and currently active.
char HappinessComponent::StateBuiltEnter() {
    const bool active = IsActive();
    if (!registered && active)
        Register();
    return 0;
}

// ============================================================
// HappinessComponent state 2 / 13 trans38 tick -- 0x14004a9a0
// ============================================================
// The per-frame column (StateMachine::Tick's +0x38 handler). It re-evaluates IsActive
// every tick and moves the source in and out of the town list as the conditions change
// -- a tavern that runs out of ale drops out here and re-registers when it is restocked.
// This is the only state-table column HappinessComponent populates, which is why its
// RegisterState instantiation (0x140005e30) is the 3-argument one: the extra stack
// argument lands in the trans38 slot at table entry +0x18.
char HappinessComponent::StateBuiltPerFrame() {
    const bool active = IsActive();

    if (!registered && active) {
        Register();
    } else if (registered && !active) {
        Unregister();
    }
    return 0;
}

// HappinessComponent state-3 "demolish" enter -- 0x14004a940. Byte-identical to the
// slot-7 removal hook (0x14004aa20), guard included.
char HappinessComponent::StateDemolishEnter() {
    HappinessComponent::OnRemove();
    return 0;
}

// The state table's handler adapters: the controller hands each one its owner
// (node+0x18), the HappinessComponent.
static char Happiness_StateBuiltEnter(HappinessComponent* c, StateMachine* /*sm*/) { return c->StateBuiltEnter(); }      // 0x14004a9f0
static char Happiness_StateBuiltPerFrame(HappinessComponent* c, StateMachine* /*sm*/, const System::Time* /*time*/) {
    return c->StateBuiltPerFrame();                                                                             // 0x14004a9a0
}
static char Happiness_StateDemolishEnter(HappinessComponent* c, StateMachine* /*sm*/) { return c->StateDemolishEnter(); } // 0x14004a940

// ============================================================================
// ENDGAME B.3.8 MI-A — HappinessComponent as a REAL class (HappinessComponent.h).
//
// Before: a 48-slot hand-built `HappinessComponent_LiveVtable` filled at every
// create (FillTypedVtable + FinishLiveVtable), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<HappinessCtrlTag> table stamped into raw bytes at +0x30.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"

using HappinessCtrlDispatch = ::ControllerLive::Dispatcher<HappinessCtrlTag>;

// The retail table, transcribed from 0x14004ac10 call for call. Its RegisterState
// instantiation (0x140005e30) copies the caller's 5th stack argument into the entry's
// +0x18 field -- verified in the callee (it reads [rsp+0x170] into [rsp+0x48]), which is
// the trans38 column. States 2 and 13 pass 0x14004a9a0 there; state 3 passes null.
// Built ONCE at static-init time, as retail's static initializer does (B38 D6/D7).
static void HappinessComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = HappinessCtrlDispatch::table;
    if (t.count != 0) return;                      // idempotent
    TableRegisterState(t, 2, &Happiness_StateBuiltEnter,
                       nullptr, &Happiness_StateBuiltPerFrame, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableRegisterState(t, 13, &Happiness_StateBuiltEnter,
                       nullptr, &Happiness_StateBuiltPerFrame, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableRegisterState(t, 3, &Happiness_StateDemolishEnter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, 6, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddTransition(t, 5, nullptr);
}
static const bool s_happinessStateTableBuilt = (HappinessComponent_PopulateStateTable(), true);

// 0x14004ab60. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28 = 0 (byte); `_controller(this)` is the member's back-pointer at +0x48; then
// `_flags |= 2` (0x82).
HappinessComponent::HappinessComponent() : registered(false), _pad29{}, _controller(this) {
    OrFlagsInCtor(0x2);
}

// 0x14004ab00: restamp + ~Internal, no own work.
HappinessComponent::~HappinessComponent() = default;

// create 0x140005ba0: MemAlloc(0x50, 8), then the ctor.
::Object::Base* HappinessComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HappinessComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HappinessComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* HappinessComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HappinessComponent());
}

// [3] 0x14004aa80 — registered round-trips (save/load wave 2026-08-28):
// Component::Persist (a direct CALL 0x1401189b0), an empty controller scope,
// then a scope carrying registered (+0x28) as a bool.
void HappinessComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    { IO::Serializer scope(stream, 0); }
    IO::Serializer ser(stream, 0);
    ser.Serialize(registered, nullptr);
}

// [7] 0x14004aa20 — if registered: ERASE this component from the happiness list
// at (gameWorld+0xF0)+0x50, set the entity index to -1, clear the flag.
//
// This said "Push" until 2026-08-25 and the function was called OnCreate. Both
// were wrong and they were the SAME wrong: 0x140002cc0 is DynArray8_EraseByValue,
// so a slot that erases, sets the entity index to -1 and clears the registered
// flag is the REMOVE side.
void HappinessComponent::OnRemove() {
    if (!registered) return;
    ::Object::InternalPtr<HappinessComponent> self(this);
    if (GameUpdateLoop::_singleton != nullptr)
        TownManager()->happinessComponents.Remove(self);   // 0x140002cc0
    UpdateInfluence(-1);                                      // 0x14004a300
    registered = false;
}


// [15] 0x14004a780 (437 B). Message 0 is "a citizen was served here": the
// PlayAnimation_Action_Ctor4 (0x1400ba6b0) visit sends it to the building's
// HappinessComponent. A source that needs goods (the tavern) spends ONE stored unit
// whose RawMaterialDescription flags meet its _goodsFlags, and the town counts it
// as consumed. Any other message, and a source that needs no goods, does nothing.
//   0x14004a780  message != 0 -> RET
//   0x14004a78d  desc = IsDerivedFrom(+0x18, [0x1401c42f8]) ? +0x18 : 0; +0x25 test, no null test
//   0x14004a7d9  storage = 0x140006270 (FindComponent<StorageComponent>), no null test
//   0x14004a812  per item (stride 0x10, count +0x30 re-read per pass): the good's
//                RawMaterialDescription (the FindDescription<T> shape, inlined); with
//                none, 0x14004a862 reads [0 + 0x5c] (the same unguarded read IsActive has)
//   0x14004a864  (_goodsFlags & flags) && count - reserve >= 1:
//   0x14004a8da    SubtractItem(item._resource, 1)                       (0x14012d150)
//   0x14004a8e6    category = ResourceCategory(item._resource)          (0x140088f90)
//   0x14004a907    if (category >= 0) town +0x170[category].+0xc += 1; RET
// Retail re-reads the item's material through the same item pointer AFTER the
// subtract (0x14004a8e6 MOV RDX,[R14]). When that unit was the last one, SubtractItem
// has compacted the array, so the category bumped is the NEXT item's (or the removed
// one's, left in place when it was the last slot). Kept in the faithful arm.
// FIX tavern_counter (docs/retail_bugs.md section 3): retail 0x14004a780 re-reads the row's
// material after SubtractItem (0x14004a8e6 MOV RDX,[R14]), so a spent last unit counts the
// NEXT item's category; the corrected arm loads the material before the call ("load the
// material before the call"), so the spent good's category is counted.
void HappinessComponent::OnMessage(int message, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    const bool fixTavernCounter = Ext::Fix().tavern_counter;
    if (message != 0) return;

    const HappinessDescription* desc = _description->IsDerivedFrom(HappinessDescription::TypeId())
                                           ? static_cast<HappinessDescription*>(_description) : nullptr;
    if (!desc->_needsGoods) return;

    StorageComponent* store = _entity->FindComponent<StorageComponent>();   // 0x140006270
    for (int i = 0; i < store->storageSlots._size; ++i) {
        StorageComponent::Item* item = &store->storageSlots._data[i];
        const RawMaterialDescription* rawMat = item->_resource->FindDescription<RawMaterialDescription>();
        if ((desc->_goodsFlags & rawMat->_flags) != 0 && item->_count - item->_reserve >= 1) {
            // FIX tavern_counter: the corrected arm loads the material before the subtract;
            // the faithful arm re-reads the row after it, as retail does (0x14004a8e6).
            if (Ext::MapOverlays::g_on) {
                // ext.map_overlays: one good spent (ale and the like), at the building; read before the subtract
                Ext::MapOverlays::OnConsumedAt(item->_resource, _entity.Get(), 1);
            }
            const ComponentDescription* spent;
            if (fixTavernCounter) {
                spent = item->_resource;
                store->SubtractItem(spent, 1);                       // 0x14012d150
            } else {
                store->SubtractItem(item->_resource, 1);             // 0x14012d150
                spent = item->_resource;                             // retail: the (maybe compacted) row
            }
            CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
            int category = ResourceCategory(spent);                 // 0x140088f90
            if (category >= 0)
                town->professionResources._data[category].field_0x0C += 1;
            return;
        }
    }
}

DEFINE_EXE_TYPE(HappinessComponent, "ControllerComponent")
