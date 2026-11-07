// src/game/Component/wellcomponent.cpp
// Clean reimplementation of WellComponent lifecycle methods from Application-x64.exe.
//
// WellComponent (0x48 bytes) extends ControllerComponent. No additional fields.
// Vtable at 0x14015ae90, read from the BYTES:
//   [ 3] 0x14005cfa0  Shutdown/Serialize — two empty serializer blocks
//   [ 7] 0x14005cf60  OnRemove           — UNregister the water source
//   [16] 0x1400026c0  GetDefaultState    — return 2 (BuildingState::Built)
//   [17] 0x140004080  CreateState        — self + 0x28
// Slots 16/17 are the same two functions ResidenceComponent uses, i.e. this is a
// state-machine controller whose default state is 2.
//
// ⚠ SLOT 7 IS OnRemove, NOT OnCreate. It calls 0x140091ba0, which SCANS FOR AND
// CLEARS the entity from the water-source array — a removal, not a registration.
// This file used to call it OnCreate (the standing Ghidra OnCreate/OnRemove
// off-by-one, see reference_ghidra_oncreate_onremove_mislabel), which is part of why
// the missing REGISTER half went unnoticed for so long: the file looked complete.
//

#include "Component/WellComponent.h"
#include <cstdint>
#include "Component/MapComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::size
#include "Object/factory_registration.h"
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <new>
#include "Component/exe_bodies.h"
#include "system/MemoryAllocator.h"

// ============================================================
// Helpers
// ============================================================
// FUN_140091ba0 CitizenBehavior::UnregisterWell (below) — on unregister, scan the
//   town's well array for the matching IBP and, if found, clear its coarse-cell mark
//   and null the slot (a remove-by-IBP, not a push).
// The GameWorld cell 0x1401c3dd8 is read through GameUpdateLoop::_singleton.
// FUN_140003110 — dynamic 8-byte-element array set-count + insert (GameColl
//   SetSizeFill). Used here for the IBP-array append.
// System::MemoryAllocator::MemAlloc/MemFree, the STATIC (size, align) pair.
// Import slot 0x140152a98 — Object::Base::IsDerivedFrom.
// FUN_140004680 FindComponent<MapComponent> — the footprint finder of the remove scan.
//
// The map width is Map::size (GameUpdateLoop::m_game),
// and the town manager comes from GameUpdateLoop::m_citizenList.

// ============================================================
// Reimpl of FUN_1400889e0 (Component reg wave B — WELL cluster).
// Pure leaf (no exe callees). Compacts the int array of one coarse terrain cell,
// deleting every entry equal to `value`. The cell is located from the world map width
// (*(int*)(*(int**)(world+0xf8)) >> 3 = cells-per-row) and the 8-px-quantised (x,y):
// cellIndex = (width>>3)*(y>>3)+(x>>3); the cell record (+0x140, stride 0x10) is an
// Array<int>. Faithful 1:1 port of the Ghidra decompile (same quirky shift-and-overwrite
// compaction).
// ============================================================
void CitizenBehavior::RemoveCellEntry(int x, int y, int value) {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (!world) return;
    int width = world->m_game->size >> 3;   // Map +0x00
    Collection::Array<int32_t>& cell = field_0x140._data[(width * (y >> 3)) + (x >> 3)];
    int count = cell._size;
    if (count <= 0) return;

    int i = 0;
    int64_t idx = 0;
    do {
        if (cell._data[idx] == value) {
            int64_t shiftIdx = idx;
            int j = i;
            if (i < count) {
                do {
                    j++;
                    if (j < cell._size) {
                        int* p = cell._data + shiftIdx;
                        if (p) *p = p[1];
                    }
                    shiftIdx += 1;
                } while (j < cell._size);
            }
            cell._size = cell._size - 1;
            i--;
            idx -= 1;
        }
        count = cell._size;
        i++;
        idx += 1;
    } while (i < count);
}

// ============================================================
// Reimpl of FUN_140091ba0 (Component reg wave B — WELL cluster).
// On unregister, scan the town's well array {ptr +0xe0, count +0xe8} for the entry
// equal to *well (the component's IBP). When found: find the entry's MapComponent
// (FUN_140004680) for its tile (short x +0x28, y +0x2a), clear the coarse-cell mark
// via RemoveCellEntry(x, y, -1-index), then null the slot with a fresh InternalBasePtr.
// 1:1 port.
// ============================================================
void CitizenBehavior::UnregisterWell(Object::InternalPtr<WellComponent>* well) {
    if (wells._size <= 0) return;
    int idx = 0;
    do {
        if (wells._data[idx] == well->Get()) {
            if (idx < 0) return;
            ComponentEntity* entry = wells._data[idx]->_entity.Get();
            MapComponent* resData = entry->FindComponent<MapComponent>();   // 0x140004680
            int rx = (int)static_cast<int16_t>(resData->tileX0);
            int ry = (int)static_cast<int16_t>(resData->tileY0);
            RemoveCellEntry(rx, ry, -1 - idx);
            wells._data[idx] = nullptr;   // a fresh InternalBasePtr(nullptr)
            return;
        }
        idx++;
    } while (idx < wells._size);
}

// ============================================================
// THE WATER-SOURCE REGISTRATION HALF (added 2026-08-22, fire lane)
// ============================================================
// Until now only the REMOVE half of this pair existed, which is why the FireWork
// spiral search returned found=0 on every run even with a well standing in the town:
// townMgr+0xE0 (the water-source IBP array) and townMgr+0x140 (the coarse per-cell
// index the search actually walks) were never written by anyone.
//
// HOW RETAIL GETS HERE. WellComponent extends ControllerComponent and owns a private
// state-type table, built at startup by 0x14014bc20 (a byte-identical copy sits
// unreferenced at 0x14005d110):
//     0x14000fed0(2, 0x14005cf20)   state 2 "built",    enter = REGISTER
//     0x14000ffe0(3)                  transitions -> {3}
//     0x14000fed0(3, 0x14005cee0)   state 3 "demolish", enter = UNREGISTER
//     0x14000ffe0(6) (4) (5)          transitions -> {6,4,5}
// 0x14000fed0 appends a 0xf8-byte state entry {id@+0x00, enterHandler@+0x08} to the
// table at 0x1401d1d70; 0x14000ffe0 appends a {targetId,null} pair to that entry's
// transition list at +0x28 (count +0xa8). That is EXACTLY the layout
// controller_state_table.h already models, and exactly the graph ResidenceComponent
// has -- state 2 registers with the TownManager, state 3 unregisters, same transition
// targets. WellComponent's vtable slots 16/17 are the same two functions as
// ResidenceComponent's (0x1400026c0 "return 2" and 0x140004080 "self+0x28"), so the
// bring-up below is the residence pattern with the two handlers swapped in.

// ============================================================
// Reimpl of FUN_140088a80 — the coarse-cell index push_back.
// The exact mirror of RemoveCellEntry above: same addressing
//     cellIdx = (mapWidth >> 3) * (y >> 3) + (x >> 3)
//     record  = field_0x140[cellIdx] = { int* data; int count; int cap }
// but it APPENDS `value` instead of compacting it out. This is the array the FireWork
// spiral search reads, so without this call the buckets stay empty however many wells
// the town has.
// ============================================================
void CitizenBehavior::AddCellEntry(int x, int y, int value) {
    // This helper is shared: CitizenList_PostLoad_8f580 (0x14008f580) calls it for
    // every residence cell on a LOAD, long before any well reaches its state-2 enter.
    // The singleton read unguarded, as retail 0x140088a95 ([0x1401c3dd8] +0xf8):
    // every caller runs inside a game (row 1310; the port's `if (!world) return;`
    // stood here until 2026-10-11).
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    const int mapW = world->m_game->size;   // Map +0x00
    const int64_t cellIdx = static_cast<int64_t>((mapW >> 3) * (y >> 3) + (x >> 3));
    Collection::Array<int32_t>& rec = field_0x140._data[cellIdx];

    int* oldData = rec._data;
    const int count = rec._size;
    const int newCount = count + 1;
    rec._size = newCount;

    int* data = oldData;
    if (newCount > rec._capacity) {
        // Retail's growth rule: first growth of an EMPTY array takes exactly one slot;
        // any later growth-by-one triples. (`count == 0` is the only way the second
        // test fails, since newCount is always count+1 here.)
        int newCap = newCount;
        if (count != 0) newCap = newCount * 3;
        rec._capacity = newCap;
        data = static_cast<int*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(newCap) * 4, 4));
        rec._data = data;
    }
    // Rows below the old count come from the OLD buffer; the new row takes `value`.
    // When the buffer did not move, retail starts at oldCount and writes only that one.
    for (int i = (oldData == data) ? count : 0; i < newCount; ++i)
        data[i] = (i >= count) ? value : oldData[i];

    if (oldData != data) System::MemoryAllocator::MemFree(oldData);
}


// ============================================================
// 0x140088b80 -- record one shore water-access point: append {cell, waterCell} to the
// +0x130 pair array (the inlined Array push: count + 1; on growth the capacity is the
// new count, tripled unless the array was empty; MemAlloc(cap * 8, 4), copy, free the
// old buffer), then file the new index in the coarse cell bucket of `cell`
// (`JMP 0x140088a80` = AddCellEntry(cell % size, cell / size, index); the map edge read
// from [0x1401c3dd8] +0xf8 unguarded). FindWater 0x1400881d0 and the FireWork search
// read the pairs. The only caller is the worldgen map create 0x14009ef20's shore loop,
// which passes the first shore / deep-water neighbour of a buildable cell in R8
// (0x14009f029-0x14009f030; Ghidra drops it). A loaded town gets the array from its
// save (Serialize) and the buckets from PostLoad 0x14008f580.
// ============================================================
void CitizenBehavior::AddWaterAccess(int cell, int waterCell) {
    const int index = field_0x130._size;
    const WaterCellPair pair{cell, waterCell};
    field_0x130.SetSize(index + 1, pair);
    const int mapW = GameUpdateLoop::_singleton->m_game->size;
    AddCellEntry(cell % mapW, cell / mapW, index);   // 0x140088a80
}

// ============================================================
// Reimpl of FUN_140091cb0 — TownManager::RegisterWaterSource.
// 1. Park the water source's IBP in the array {ptr +0xe0, count +0xe8}, reusing the
//    first NULLED slot (the unregister leaves holes) and appending only if none.
// 2. Find the owner entity's MapComponent, and stamp its tile in the coarse cell index
//    with `-1 - slotIndex`. The negative encoding is what RemoveCellEntry searches for
//    on removal, and what the FireWork search decodes back to a slot.
// ============================================================
void CitizenBehavior::RegisterWell(Object::InternalPtr<WellComponent>* well) {
    int index = -1;
    const int count = wells._size;
    if (count > 0) {
        Object::InternalPtr<WellComponent>* arr = wells._data;
        for (int i = 0; i < count; ++i) {
            if (arr[i] != nullptr) continue;
            arr[i] = well->Get();
            index = i;
            break;
        }
    }
    if (index < 0) {
        index = wells._size;      // append at the current end
        wells.SetSizeFill(index + 1, well);
    }

    ComponentEntity* entity = well->Get()->_entity.Get();

    // The tile-position component (ti cell 0x1401c79e0, MapComponent; the int16 x/y at
    // +0x28/+0x2a this reads are MapComponent's, as FUN_140004680 agrees). The
    // FindComponent<T> shape, inline; the null-slot test is the tree's.
    const uint32_t tidMap = MapComponent::TypeId();
    MapComponent* mapComp = nullptr;
    const int compCount = entity->_components._size;
    Object::InternalPtr<Component>* comps = entity->_components._data;
    for (int i = 0; i < compCount; ++i) {
        Component* comp = comps[i].Get();
        if (!comp) continue;
        if (comp->Type() != tidMap) continue;
        // Retail re-reads the slot and confirms with IsDerivedFrom before accepting.
        if (comp->IsDerivedFrom(tidMap)) mapComp = static_cast<MapComponent*>(comp);
        break;
    }

    if (mapComp == nullptr) {
        // Retail derefs this unguarded -- it relies on every registered water source
        // owning a MapComponent. Ours can legitimately reach here while the component
        // set is still being brought up, and a fault would take the whole boot with it,
        // so say which half failed instead of guessing.
        fprintf(stderr, "[Well] register INCOMPLETE: slot %d holds the IBP but the owner "
                        "entity has no MapComponent (tid=%u, %d components scanned) -- no "
                        "cell stamp, so the FireWork search still cannot find this well\n",
                index, tidMap, compCount);
        fflush(stderr);
        return;
    }
    const int tx = static_cast<int16_t>(mapComp->tileX0);
    const int ty = static_cast<int16_t>(mapComp->tileY0);
    AddCellEntry(tx, ty, -1 - index);

    {
        static int n = 0;
        if (n < 8) { ++n;
            fprintf(stderr, "[Well] water source REGISTERED: slot=%d tile=(%d,%d) "
                            "cellValue=%d townMgr=%p arrayCount=%d\n",
                    index, tx, ty, -1 - index, static_cast<void*>(this), wells._size);
            fflush(stderr);
        }
    }
}

// ============================================================
// WellComponent state-2 "built" enter — 0x14005cf20.
// Wraps the COMPONENT (not the owner entity) in an InternalBasePtr and hands it to the
// registrar with TownManager = *(GameWorld+0xf0). Retail returns 0 in AL so the SM's
// Enter walk continues past this node.
// ============================================================
char WellComponent::EnterBuilt() {
    if (GameUpdateLoop::_singleton == nullptr) return 0;
    Object::InternalPtr<WellComponent> ibp(this);
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (!townMgr) return 0;
    townMgr->RegisterWell(&ibp);   // 0x140091cb0
    return 0;
}

// WellComponent state-3 "demolish" enter — 0x14005cee0. Byte-identical to the slot-7
// removal hook (0x14005cf60): wrap the component, call the unregister.
char WellComponent::EnterDemolish() {
    WellComponent::OnRemove();   // retail: an identical body, not a dispatch
    return 0;
}

// ============================================================================
// ENDGAME B.3.8 pilot — WellComponent as a REAL class (WellComponent.h).
//
// Before: a 48-slot hand-built `WellComponent_LiveVtable` filled at every
// create (FillTypedVtable + FinishLiveVtable), `Inert_ScalarDtor` in slot 0
// (only `~Internal`), `Dispatcher<WellCtrlTag>::Vtable()` stamped into raw
// bytes at +0x28. After: the compiler's 18-slot vtable, a real constructor and
// destructor chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"

using WellCtrlDispatch = ::ControllerLive::Dispatcher<WellCtrlTag>;

static char WellComponent_EnterBuiltState(WellComponent* c, StateMachine* /*sm*/)    { return c->EnterBuilt(); }      // 0x14005cf20
static char WellComponent_EnterDemolishState(WellComponent* c, StateMachine* /*sm*/) { return c->EnterDemolish(); }   // 0x14005cee0

// The retail table, transcribed from 0x14014bc20 call for call. Retail runs it
// ONCE from a static initializer; the tree ran it (idempotently, unsynchronised)
// on every create, from both the loader and the main thread. It now runs at
// static-init time like retail's (B6-5: no first-use race). The table is
// zero-initialised storage (constant init), so the order against other static
// initialisers does not matter.
static void WellComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = WellCtrlDispatch::table;
    if (t.count != 0) return;                      // idempotent
    TableRegisterState(t, BuildingState::Built, &WellComponent_EnterBuiltState,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
    TableRegisterState(t, BuildingState::Demolish, &WellComponent_EnterDemolishState,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Collapse, nullptr);
    TableAddTransition(t, BuildingState::HaulAway, nullptr);
    TableAddTransition(t, BuildingState::Deconstruct, nullptr);
}
static const bool s_wellStateTableBuilt = (WellComponent_PopulateStateTable(), true);

// 0x14005d060. Component() is the CALL 0x1401188b0; the 0x140162b80 and
// 0x14015ae90 stamps and the member's 0x14015f070 / 0x14015ae40 stamps are the
// compiler's; `_controller(this)` is the +0x40 back-pointer store and the +0x30
// / +0x38 zeroes; then `_flags |= 2` (0x82 with Component's 0x80).
WellComponent::WellComponent() : _controller(this) {
    OrFlagsInCtor(0x2);
}

// ============================================================
// WellComponent::Persist (vtable slot [3], 0x14005cfa0)
// ============================================================
// Component_Serialize + two empty serializer blocks (controller compat).

void WellComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    // First empty serializer block (component section)
    { IO::Serializer scope1(stream, 0); }

    // Second empty serializer block (controller section)
    { IO::Serializer scope2(stream, 0); }
}

// ============================================================
// WellComponent::OnRemove (vtable slot [7], 0x14005cf60)
// ============================================================
// UNregisters this component from the TownManager water-source list. The matching
// REGISTER is the state-2 enter above.

void WellComponent::OnRemove() {
    // ⚠ FIXED 2026-08-22 (fire lane): retail 0x14005cf60 is
    //     InternalBasePtr::InternalBasePtr(&local, param_1)   // param_1 == THIS
    //     FUN_140091ba0(*(gw + 0xf0), &local)
    // — it wraps the COMPONENT, not the owner entity. This passed `owner`,
    // so the IBP handed to 0x140091ba0 identified the wrong object and its
    // scan-for-matching-IBP could never hit. Latent while WellComponent was inert
    // (slot 7 was never installed); it matters now that the FireWork spiral search
    // depends on wells being findable in the gw+0xf0 entity index.
    Object::InternalPtr<WellComponent> basePtr(this);

    // *(GameWorld + 0xF0) is the TownManager itself -- not a "water source array".
    // 0x140091ba0 takes the manager and indexes its own +0xe0/+0xe8 pair.
    if (GameUpdateLoop::_singleton != nullptr) {
        CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
        townMgr->UnregisterWell(&basePtr);   // 0x140091ba0
    }
}

// create 0x14000fc80: MemAlloc(0x48, 8), then the ctor.
::Object::Base* WellComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(WellComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) WellComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (WellComponent.h).
::Object::Base* WellComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) WellComponent());
}

DEFINE_EXE_TYPE(WellComponent, "ControllerComponent")
