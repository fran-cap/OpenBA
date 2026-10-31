// src/game/Component/pasturecomponent.cpp
// Clean reimplementation of PastureComponent lifecycle methods from Application-x64.exe.
//
// PastureComponent (0x68 bytes) has a SHIFTED controller sub-object at +0x48.
// Own data at +0x28..+0x44, controller vtable at +0x48.
//
// Vtable at 0x140163f60. Three non-trivial overrides:
//   Persist   (0x140127f40) — Component_Serialize + the animal array serialization
//   OnAdded   (0x140127a30) — Compute pasture capacity
//   OnRemove  (0x1401277e0) — release every animal
//
// LEFT (B.4 COMP-d): GameUpdateLoop m_game is void* (W0 header) and the Map's 0x30-byte
// cell (array at +0x80) has no type yet (GAME-b); the Pf_* map
// helpers take their objects as int64_t (Action's bodies).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/PastureComponent.h"
#include <cstdint>
#include "Component/AnimalComponent.h"
#include "Component/AnimalDescription.h"
#include "Component/CharacterComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/InteractComponent.h"
#include "Component/LivestockComponent.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"              // Map (GameUpdateLoop::m_game)
#include "math/Math.h"             // Math::Transform / Vector3
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"   // InteractNodeCount
#include "Action/PathSearch.h"    // RegionGraph::MarkCellDirty
#include "Game/PathfinderThread.h"   // PathfinderThread::SetCellTerrain
#include "Game/terrain_sidecar.h"   // [terrain sidecar] NoteWrite after the +0x29 store (OnEvent)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store

// The exe helpers this file calls, by retail address:
//   0x140004680 FindComponent<MapComponent>       0x140005500 FindComponent<AnimalComponent>
//   0x1400012d0 FindComponent<CharacterComponent> 0x140002ed0 FindComponent<InteractComponent>
//   0x1400c3470 ComponentEntity::RefreshEntityFlags
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable  serialize the ExternalPtr at +0x28
//   0x140152a98 ?IsDerivedFrom@Base@Object@@    import slot
//   0x140152b00 ??0Time@System@@QEAA@M@Z        import slot (RtSystem_Time_Ctor)


// 0x140105e20: the wander box of `size` cells around the habitat cell, clamped to the
// map. Pure leaf.
void AnimalComponent::GetWanderBox(int16_t* outX, int16_t* outY, int16_t* outW, int16_t* outH,
                                   int size) const {
    const Map* map = GameUpdateLoop::_singleton->m_game;
    int cell = habitatCell;
    int mapDim = map->size;
    short half = static_cast<short>(size / 2);
    *outX = static_cast<short>(cell % mapDim) - half;
    *outY = static_cast<short>(cell / mapDim) - half;
    *outW = static_cast<short>(size);
    *outH = static_cast<short>(size);
    if (*outX < 0) *outX = 0;
    if (*outY < 0) *outY = 0;
    if (mapDim <= (int)*outW + (int)*outX)
        *outW = static_cast<short>((short)mapDim - *outX) - 1;
    if (mapDim <= (int)*outH + (int)*outY)
        *outH = static_cast<short>((short)mapDim - *outY) - 1;
}

// B4 FORWARD: 1 outside caller file (animalcomponent.cpp).

// 0x140128570: pasture capacity = footprint area / the per-animal area of the
// livestock template's AnimalDescription (+0x24). 0 with no livestock type.
int PastureComponent::ComputeCapacity() const {
    if (livestockType == nullptr) return 0;

    MapComponent* mapComp = _entity->FindComponent<MapComponent>();   // 0x140004680

    const ComponentDescription* animalRef = livestockType;
    short w = static_cast<int16_t>(mapComp->tileX1);
    short h = static_cast<int16_t>(mapComp->tileY1);

    // Scan the template's sub-descriptions for its AnimalDescription.
    const AnimalDescription* found = nullptr;
    int n = animalRef->_subDescCount;
    for (int i = 0; i < n; i++) {
        Object::Base* comp = animalRef->_subDescs[i];
        uint32_t t = comp->Type();   // slot 2
        if (t == AnimalDescription::TypeId()) {
            if (Object::IsDerivedFromNullSafe(comp, AnimalDescription::TypeId()))
                found = static_cast<const AnimalDescription*>(comp);
            break;
        }
    }
    // No test of the desc or the divisor: 0x140128644 `IMUL R15D,R12D; CDQ;
    // IDIV dword ptr [RBX+0x24]`, RBX the CMOVNZ scan result or 0.
    return (int)(w * h) / found->_i24;
}

// B4 FORWARD: 1 outside caller file (Dialog/debugui.cpp).

// 0x140105ed0: let the animal wander from where it stands. Sets flag bit 0x400000
// (notifying the entity on a change), records the tile under its model as the habitat
// cell, sets the wander box from its AnimalDescription's size (+0x20), and kicks an
// immediate Update (slot 9 with t = 0).
void AnimalComponent::ReleaseToWander() {
    uint32_t oldFlags = _flags;
    uint32_t newFlags = oldFlags | 0x400000;
    _flags = newFlags;
    if (((newFlags ^ oldFlags) & 0xffff) != 0 && _entity != nullptr) {
        _entity->RefreshEntityFlags();   // 0x1400c3470
    }

    CharacterComponent* character = _entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    ModelComponent* model = character->modelComponent.Get();

    if (GameUpdateLoop::_singleton == nullptr) return;
    int mapW = GameUpdateLoop::_singleton->m_game->size;
    const Math::Vector3& pos = model->transform.translation;
    int x = (int)(pos.x + (float)(mapW >> 1));
    int y = (int)(pos.y + (float)(mapW >> 1));
    if (x < 0) x = 0;
    if (mapW <= x) x = mapW - 1;
    if (y < 0) y = 0;
    if (mapW <= y) y = mapW - 1;

    Description* tmpl = _description;
    habitatCell = mapW * y + x;
    const AnimalDescription* animalTmpl = tmpl->IsDerivedFrom(AnimalDescription::TypeId())
                                              ? static_cast<const AnimalDescription*>(tmpl) : nullptr;
    GetWanderBox(&wanderX, &wanderY, &wanderWidth, &wanderHeight, animalTmpl->_i20);   // 0x140105e20

    System::Time t{0};
    RtSystem_Time_Ctor(&t, 0.0f);
    Update(&t);   // slot 9, +0x48
}

// 0x1401240a0: pen the livestock in its pasture — clear the animal's flag bit
// 0x400000 (notifying on a change) and copy the pasture's footprint rect into the
// animal's wander box.
void LivestockComponent::PenInPasture() {
    if (pasture == nullptr) return;

    ComponentEntity* link = pasture->_entity.Get();
    MapComponent* mapComp = link->FindComponent<MapComponent>();   // 0x140004680
    int16_t r0 = mapComp->tileX0;
    int16_t r1 = mapComp->tileY0;
    int16_t r2 = mapComp->tileX1;
    int16_t r3 = mapComp->tileY1;

    AnimalComponent* animal = _entity->FindComponent<AnimalComponent>();   // 0x140005500

    uint32_t oldFlags = animal->_flags;
    uint32_t newFlags = oldFlags & 0xffbfffff;
    animal->_flags = newFlags;
    if (((newFlags ^ oldFlags) & 0xffff) != 0 && animal->_entity != nullptr) {
        animal->_entity->RefreshEntityFlags();   // 0x1400c3470
    }
    animal->wanderX      = r0;
    animal->wanderY      = r1;
    animal->wanderWidth  = r2;
    animal->wanderHeight = r3;
}


// ============================================================
// PastureComponent vtable slot [7] (0x1401277e0) — RELEASE, not "OnCreate"
// ============================================================
// Ghidra's "OnCreate" label is the usual off-by-one (slot 6 is OnAdded, slot 7 is
// the remove side). The body is byte-identical to PastureDescription_helper_6e40,
// the controller's state-3 (teardown) ENTER: unassign every animal back to the
// world's free-roaming pool and erase the pasture from the world pasture registry
// (CitizenBehavior::pastures).
//
// The previous port of this function spun forever. Two labelling mistakes caused it:
// FUN_140002cc0 is DynArray8_EraseByValue and FUN_140003110 is DynArray8_ResizeFill,
// not "AddToList"/"InsertInList" — and it is the ERASE from `animals` that shrinks
// `animalCount` and ends the `while (count >= 1)` loop. The single shared body lives
// in pasturecomponent_states.cpp (PastureComponent::ReleaseAllAnimals), so the vtable
// slot and the state-3 handler cannot drift apart again.


// ===== ENDGAME B.3.8 MI-B — PastureComponent's own slot bodies =====
// Slots 13/14/15 live in pasturecomponent_states.cpp; the slot-12 body is below.
#include "Object/factory_registration.h"



// ============================================================================
// ENDGAME B.3.8 MI-B — PastureComponent as a REAL class (PastureComponent.h).
//
// Before: a hand-rolled InstallLive over a 48-slot table (slots 1/3/4/6/7/12-21
// patched per create, Inert_ScalarDtor in slot 0, Dispatcher<PastureCtrlTag>
// stamped into raw bytes at +0x48 with its back-pointer at +0x60). Slots 18-21 held
// ControllerLive_ScalarDeletingDtor, the class's type info / id and `return 2`: that
// is PastureDescription's table 0x140163ff0 read past this 18-slot table's end
// (B38 D3.5) — gone; nothing in the tree dispatches a component past slot 17.
// After: the compiler's 18-slot vtable, a real ctor/dtor chain, the controller a
// member object at retail's +0x48 (B38_RULES D1, D2, D6).
// ============================================================================

// 0x1401280e0. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28 = +0x30 = +0x38 = 0; +0x44 = 0; the member at +0x48 with its back-pointer
// (+0x60); then `_flags |= 0x8b`. Retail leaves +0x40 unwritten; the tree's factory
// zeroed it and that is kept.
PastureComponent::PastureComponent()
    : livestockType(nullptr), capacity(0), production(0), _controller(this) {
    OrFlagsInCtor(0x8b);
}

// 0x140128070: the controller's restamp (~_controller), then MemFree(+0x30), +0x30 = +0x38
// = 0 (~animals: kSpec_PtrZero has no element destroy), then the restamps and ~Internal
// (the compiler chain, reverse declaration order). The body is empty.
PastureComponent::~PastureComponent() {
}

// create 0x140036730: MemAlloc(0x68, 8), then the ctor.
::Object::Base* PastureComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PastureComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PastureComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* PastureComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PastureComponent());
}

// ============================================================
// PastureComponent::Persist (vtable slot [3], 0x140127f40)
// ============================================================
// Component_Serialize + empty serializer(v0) + main serializer(v0) with:
//   - the livestock type ExternalPtr
//   - the animal count and array, each validated against LivestockComponent
//   - capacity and production
void PastureComponent::Persist(IO::Stream& stream) {
    // Base Component serialization
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;

    // Empty serializer block (version 0, immediately destroyed)
    { IO::Serializer scope(*st, 0); }

    // Main serializer block (version 0)
    IO::Serializer ser(*st, 0);

    ::Object::SerializeExternal<ComponentDescription>(ser, livestockType);   // 0x140002340

    int countBuf = animals._size;
    ser.Serialize(countBuf, nullptr);

    // On load, resize the animal array. Retail reads the direction flag from the
    // STREAM (+8) through the serializer's first field — `*(int*)(ser[0] + 8)`
    // — not from serializer+8 (that's the version int, 0 here, which made the
    // old check pass on save too; benign only because resize-to-same-count is
    // a no-op).
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        animals.SetSize(countBuf);
    }

    // Serialize each InternalPtr and validate its type
    Object::InternalPtr<LivestockComponent>* entry = animals._data;
    int64_t count = (int64_t)countBuf;
    if (count > 0 && entry) {
        for (int64_t i = 0; i < count; i++, entry++) {
            entry->InternalBasePtr::Serialize(ser);
            LivestockComponent* obj = entry->Get();
            if (obj != nullptr) {
                if (!obj->IsDerivedFrom(LivestockComponent::TypeId())) {
                    *entry = nullptr;
                }
            }
        }
    }

    ser.Serialize(capacity, nullptr);
    ser.Serialize(production, nullptr);
}
// [4] 0x140127a50: `MOV qword [RCX+0x28],0` — one store.
void PastureComponent::Startup() { livestockType = nullptr; }
// ============================================================
// PastureComponent::OnAdded (vtable slot [6], 0x140127a30; Ghidra "Draw")
// ============================================================
void PastureComponent::OnAdded() {
    capacity = ComputeCapacity();   // 0x140128570
}
void PastureComponent::OnRemove() {
    ReleaseAllAnimals();   // 0x140126e40's body
}
// ── slot 12 — retail 0x140127540 — claim the pasture's gate cells ─────────────────
// Only acts on message id 4. For each kind-2 child attach point of the pasture's
// InteractComponent it converts the point to a map cell, forces that cell's terrain
// byte (+0x29) to 2, clears the cell's entity slot (+0x08) to a null IBP, and notifies
// the two map subsystems:
//     if (1 << terrain & 0x1e0) map+0xa2 |= 1, and FUN_1400a8c90(map+0x70, x, y)
//     FUN_1400a6fe0(map+0x68, cell, 2, index)
// The 0x1e0 mask is the "was this a resource/rough tile" test the orchard and
// ResourceProduce paths use too; the |= records that at least one such tile changed so
// the caller can refresh.
//
// This is the pasture's gate/entrance claim — the cells a herder walks through. With
// the slot stubbed the terrain there was never converted, which is the same class of
// omission as the MapComponent occupancy gap already recorded for this wave.
// The slot-12 helpers: 0x140002ed0 FindComponent<InteractComponent>, 0x140123070
// Pf_RecursiveChildSum_123070, 0x140122e20 Pf_EntityResolvePos_2e20, 0x1400a8c90
// Pf_FineCellDirty_a8c90, 0x1400a6fe0 Pf_EnqueueCmd2_a6fe0.

void PastureComponent::OnEvent(int msgId) {
    if (msgId != 4) return;
    if (GameUpdateLoop::_singleton == nullptr) return;

    InteractComponent* pos = _entity->FindComponent<InteractComponent>();   // 0x140002ed0
    if (!pos) return;

    Map* map = GameUpdateLoop::_singleton->m_game;
    if (!map) return;
    const int width = map->size;
    MapCell* grid = map->cells.GetData();
    if (!grid || width <= 0) return;

    for (uint32_t i = 0; (int)i < pos->NodeCount(2); ++i) {   // 0x140123070 (Pf_RecursiveChildSum_123070's body)
        Math::Vector3 p;
        pos->NodePosition(&p, 2, (int)i, nullptr);
        int x = (int)(p.x + (float)(width >> 1));
        int y = (int)(p.y + (float)(width >> 1));
        if (x < 0) x = 0; if (x >= width) x = width - 1;
        if (y < 0) y = 0; if (y >= width) y = width - 1;
        const int cell = width * y + x;

        uint8_t* terrain = &grid[cell].terrainType;   // +0x29
        const uint8_t was = *terrain;
        const bool wasRough = ((1u << (was & 0x1f)) & 0x1e0u) != 0;
        map->field_0xA2 = map->field_0xA2 | wasRough;
        *terrain = 2;
        TerrainSidecar::NoteWrite(&grid[cell]);   // [terrain sidecar] mirror (missing until AMBMEMO)
        CellStamp::NoteWrite(&grid[cell]);   // [AMBMEMO] cell write clock
        if (wasRough)
            map->regionGraph->MarkCellDirty(cell % width, cell / width);   // 0x1400a8c90 (Pf_FineCellDirty_a8c90's body)
        // 0x1400a6fe0 reads the cell and the type (EDX, R8D) only.
        map->pathfinder->SetCellTerrain(cell, 2);   // 0x1400a6fe0 (Pf_EnqueueCmd2_a6fe0's body)
        grid[cell].occupant = nullptr;   // +0x08 the entity on the cell
        CellStamp::NoteWrite(&grid[cell]);   // [AMBMEMO] cell write clock
    }
}

// [13] 0x140127dd0 CommandMask / [14] 0x140127a60 Command / [15] 0x140127720 OnMessage
// live in pasturecomponent_states.cpp next to their helpers.

DEFINE_EXE_TYPE(PastureComponent, "ControllerComponent")
