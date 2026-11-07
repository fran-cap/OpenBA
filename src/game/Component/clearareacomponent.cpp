// src/game/Component/clearareacomponent.cpp
// Clean reimplementation of ClearAreaComponent lifecycle methods.
//
// ClearAreaComponent (0x50 bytes) -- ControllerComponent subclass.
// Has one extra field: clearState (int32) at +0x48.
// Handles clearing terrain areas during construction/placement.
//
// Vtable at 0x1401626d0. Non-trivial override:
//   Persist (0x140117EC0) -- serialize with clearState
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ClearAreaComponent.h"
#include <cstdint>
#include "Component/MapComponent.h"
#include "Game/Map.h"
#include "Entity/EntityList.h"
#include "Entity/ComponentEntity.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <new>
#include "Action/exe_bodies.h"

// ============================================================
// ClearAreaComponent::FootprintSweep  (ClearAreaDescription_helper_7d60, 0x140117d60)
// ============================================================
// The heart of "placing a building marks trees/rocks/iron for removal". Reads the
// building's footprint rectangle off its MapComponent (FUN_140004680 =
// FindComponent<MapComponent>), walks every footprint cell, and for each occupying
// entity (tree/rock/iron) that isn't the building itself and isn't already flagged
// (+0x40 bit 0x400), marks it for removal (FUN_1400c2ea0) and pushes it onto the
// EntityList removal-pending list (FUN_140003110 → gw+0xe8,+0xD0). EntityList::Update
// Step 5 then destroys them.
//
// Map/tile layout (verified live + vs the markremoval backend harness):
//   gw+0xf8 -> map (int*);  *map = grid width;  *(map+0x80 bytes) = tile array base.
//   tile stride 0x30 bytes; entity ptr slot at +0x08.  gw+0xe8 -> EntityList.

// The mark-for-removal leaf 0x1400c2ea0 is ActHunt_MarkEntityRemoval_c2ea0, called directly.
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call site below.

// Returns the number of footprint entities newly marked for removal. The tree's null
// tests (no game loop / owner / MapComponent / map / entity list) are its own; retail
// reads through each unguarded. Kept (B9).
int ClearAreaComponent::FootprintSweep() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return 0;

    ComponentEntity* entity = _entity.Get();
    if (!entity) return 0;

    // The MapComponent holds the footprint rect (retail MOVSX: the fields are signed).
    MapComponent* mc = entity->FindComponent<MapComponent>();   // 0x140004680
    if (!mc) return 0;

    int16_t startCol = static_cast<int16_t>(mc->tileX0);
    int16_t startRow = static_cast<int16_t>(mc->tileY0);
    int16_t width    = static_cast<int16_t>(mc->tileX1);
    int16_t height   = static_cast<int16_t>(mc->tileY1);

    // GameUpdateLoop::m_game (+0xf8) and m_entityList (+0xe8): the cells and the
    // removal array (EntityList::removalList, +0xD0) by name.
    const Map* map = gw->m_game;
    if (!map) return 0;
    int gridWidth = map->size;
    const MapCell* tileArray = map->cells.GetData();
    EntityList* entityList = gw->m_entityList;
    if (!tileArray || !entityList) return 0;

    int marked = 0;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int cell = (startRow + row) * gridWidth + (startCol + col);
            // entity ptr slot at cell*0x30 + 0x08 (matches 7d60's "+ 8 + (...)*0x30")
            ComponentEntity* occ = tileArray[static_cast<uintptr_t>(cell)].occupant.Get();

            // Skip empty cells and the building's own entity.
            if (occ == nullptr || occ == _entity.Get()) continue;
            if (occ->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) continue;

            // Mark + push (the proven backend).
            occ->MarkForRemoval();
            IBPArray* rl = &entityList->removalList;
            int cnt = rl->_size;
            rl->SetSize(cnt + 1, occ);
            ++marked;
        }
    }
    return marked;
}

// ============================================================================
// ENDGAME B.3.8 MI-A — ClearAreaComponent as a REAL class (ClearAreaComponent.h).
//
// Before: a 48-slot hand-built `ClearAreaComponent_LiveVtable` filled at every
// create (FillTypedVtable + FinishLiveVtable), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<ClearAreaCtrlTag> table stamped into raw bytes at +0x28.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include <cstring>
#include <cstdio>

// Harvest-over-time path (faithful): the entity's StateMachineComponent links this
// component's embedded controller (at +0x28) and ticks its clearing state machine
// (clearareacomponent_states.cpp), which posts a ClearArea work-action to a citizen
// so the footprint resources are chopped over time. The state table is built ONCE
// at static-init time, as retail's static initializer does (B38 D6/D7).
extern "C" void ClearAreaCtrl_PopulateTable();
static const bool s_clearAreaStateTableBuilt = (ClearAreaCtrl_PopulateTable(), true);

// 0x140117fa0. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48 = 0. No flag OR: the SM
// ticks the node, so the component needs no self-tick bit (0x80).
ClearAreaComponent::ClearAreaComponent() : _controller(this), clearState(0), _pad4C{} {}

// 0x140117f40: restamp + ~Internal, no own work.
ClearAreaComponent::~ClearAreaComponent() = default;

// create 0x140030ea0: MemAlloc(0x50, 8), then the ctor.
::Object::Base* ClearAreaComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ClearAreaComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ClearAreaComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ClearAreaComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ClearAreaComponent());
}

// [3] 0x140117ec0 — clearState round-trips (save/load wave 2026-08-28):
// Component::Persist (a direct CALL 0x1401189b0), an empty
// Serializer scope (format stub), then a scope carrying clearState (+0x48).
void ClearAreaComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    { IO::Serializer scope(stream, 0); }
    IO::Serializer scope(stream, 0);
    scope.Serialize(clearState, nullptr);
}

DEFINE_EXE_TYPE(ClearAreaComponent, "ControllerComponent")

// Harness probe: does this building entity carry a live ClearAreaComponent? Scans the
// entity's component list for the ClearAreaComponent type-id and returns the component
// ptr (or null). outFlags gets the component's +0x20 capability flags (bit 0x4 = ticked).
extern "C" ClearAreaComponent* ClearAreaComponent_HarnessFind(ComponentEntity* entity, uint32_t* outFlags) {
    if (outFlags) *outFlags = 0;
    if (!entity) return nullptr;
    uint32_t wantTid = ClearAreaComponent::TypeId();
    if (!wantTid) return nullptr;
    if (!entity->_components._data) return nullptr;
    for (int i = 0; i < entity->_components._size; ++i) {
        Component* c = entity->_components[i].Get();
        if (!c) continue;
        if (c->Type() == wantTid) {
            if (outFlags) *outFlags = c->_flags;
            return static_cast<ClearAreaComponent*>(c);   // the exact type id matched
        }
    }
    return nullptr;
}

// Harness entry: run the footprint sweep on a REAL live ClearAreaComponent (reads its
// own +0x10 owner) — exactly what a de-inerted component's Update tick would do.
extern "C" int ClearAreaComponent_HarnessSweepComp(ClearAreaComponent* realComp) {
    return realComp->FootprintSweep();
}

// Read a live component's +0x10 owner field (to confirm it's wired to its entity).
extern "C" ComponentEntity* ClearAreaComponent_HarnessOwner(ClearAreaComponent* realComp) {
    if (!realComp) return nullptr;
    return realComp->_entity.Get();
}

// Harness entry: run the footprint sweep for a given building entity. Builds a
// synthetic ClearAreaComponent whose owner (+0x10) is the building, then sweeps —
// exactly what the live component's driver does on placement. Returns entities marked.
// B7 LX (B7_RULES D8b): a REAL ClearAreaComponent local (it was a zeroed raw buffer
// viewed as one). Its constructor chain (0x140117fa0 -> Component 0x1401188b0 ->
// Object::Internal 0x1800bfd10) registers it with the InternalFactory (AddObject) and
// its destructor chain (~Internal 0x1800bfdb0) unregisters it (RemoveObject) at the
// scope's end, so the harness leaves no factory entry behind. The fields the zeroed
// buffer differed in (`_flags` 0x80, the controller's owner back-pointer, the vptrs)
// are not read by FootprintSweep.
extern "C" int ClearAreaComponent_HarnessSweep(ComponentEntity* ownerEntity) {
    ClearAreaComponent comp;
    comp._entity = ownerEntity;
    return comp.FootprintSweep();
}
