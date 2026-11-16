// src/game/Component/mapcomponent.cpp
// Clean reimplementation of MapComponent lifecycle methods from Application-x64.exe.
//
// MapComponent (0x38 bytes) manages tile map coordinates for entities.
// Fields: +0x28..+0x2E = four int16 tile coords (x, y, w, h)
//         +0x30 = int32 rotation (serialized)
//
// Vtable at 0x140163aa0. Four non-trivial overrides:
//   Persist   (0x1401243e0) — Serializes tile coords as shorts + rotation int
//   OnAdded   (0x140124370) — AddToMap when MapDescription::_claimOnAdd (+0x30)
//   OnEvent   (0x1401243c0) — AddToMap on message 1 (ClearArea's state-2 enter)
//   OnRemove  (0x1401242f0) — Clears the minimap rect + the map occupancy
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/MapComponent.h"
#include "Component/MapDescription.h"
#include "Entity/ComponentEntity.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Minimap.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "Component/exe_bodies.h"

// Helpers, called by name (B3R): Object::Base::IsDerivedFrom (import slot 0x140152a98),
// MapComponent::AddToMap (0x1401241b0, keystone_a_map.cpp), MapComponent::RemoveFromMap
// (0x140124750 — the tree's body; keystone_a_map.cpp's CreateNaturalResource_impl_4750 is
// the second one), Minimap::ClearTerrain (0x1400a4d30, below).
//
// Bit 0x100000 of Component::_flags (+0x20) is "this footprint is written into the map":
// AddToMap skips everything when it is set and sets it after the occupancy write
// (0x140124297 BTS); RemoveFromMap runs only when it is set and clears it. Nothing else
// in the tree writes it for a MapComponent (Phase D SLOTS, row 1203).

// ============================================================
// Reimpl of FUN_1400a4d30 (MapComponent terrain-clear / rect fill)
// ============================================================
// Marks the grid dirty (*grid = 1) and writes `value` into the 32-bit cell grid at
// grid+0x28 over a (w x h) rectangle anchored at (x, y). Row index is flipped:
//   cell = (((width - y) - row - 1) * width + x + col), width @ grid+4.
// Pure data write, no external deps. 1:1 port of the exe helper.
// The grid is the Minimap (GameUpdateLoop +0x148): +0x00 overlayDirty, +0x04 size,
// +0x28 overlayPixels.
void Minimap::ClearTerrain(int value, int x, int y,
                                       int w, int h) {
    Minimap* grid = this;
    grid->overlayDirty = 1;
    int width = grid->size;
    uint32_t* cells = grid->overlayPixels._data;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int64_t idx = (int64_t)(((int64_t)width - y - row - 1) * width + x + col);
            cells[idx] = (uint32_t)value;
        }
    }
}

// ============================================================
// Helper: the component's MapDescription
// ============================================================
// Pattern: IsDerivedFrom(self->_description, MapDescription typeId)
// Returns the description if the type matches, else nullptr. Retail calls IsDerivedFrom
// on [RCX+0x18] with no test (0x14012438a / 0x14012430a).
static MapDescription* GetMapDescription(MapComponent* self) {

    Description* obj = self->_description;

    bool derived = obj->IsDerivedFrom(MapDescription::TypeId());   // slot 0x140152a98
    return derived ? static_cast<MapDescription*>(obj) : nullptr;
}


// ===== Standalone factory (Foundation D — component instantiation) =====
#include "Object/factory_registration.h"
#include "Component/MapComponent.h"
#include <cstdlib>

// ============================================================
// Economy brick M-B — grid occupancy UN-mark on entity removal
// ============================================================
// Standalone body for 0x140124750, the removal-side mirror of the occupancy write.
// Retail dispatch: entity mark-removal FUN_1400c2ea0 walks the component list and
// calls vtable+0x38 on removable components -> MapComponent::OnRemove (0x1401242f0)
// -> tail-calls 0x140124750 -> dee0 (clear the footprint's cell entity-links +
// reclassify terrain). Gated on comp+0x20 bit 0x100000 ("occupancy currently
// written"), set by AddToMap exactly when dc20 wrote cell+0x08. Without
// this, removing a produce-harvested tree leaves the cell grid pointing at the
// FREED entity (CheckPtr is a release no-op) and any grid reader — the
// CitizenAI leisure box scan, the forester scan — faults on it.
//
// dee0 body: StartTown_UnmarkResourceCellGrid (starttown.cpp), dee0's shape / classify /
// write logic.
// LEFT: a second body of 0x140124750 (keystone_a_map.cpp's CreateNaturalResource_impl_4750,
// the faithful one) exists; this is the one the tree runs.
extern "C" void StartTown_UnmarkResourceCellGrid(Map* game, ComponentEntity* ownerEntity, int tileX,
                                                 int tileY, int w, int h, const char* shape, int rot);

void MapComponent::RemoveFromMap() {
    MapComponent* self = this;
    uint32_t flags = self->_flags;
    if (((flags >> 20) & 1) == 0) return;          // occupancy not written
    if (GameUpdateLoop::_singleton == nullptr) return;
    Map* game = GameUpdateLoop::_singleton->m_game;
    if (!game) return;

    // Owner entity (comp+0x10) — dee0 compares it against cell+0x00 for
    // the buildable(2) override.
    ComponentEntity* ownerEntity = self->_entity.Get();

    // MapDescription shape (desc+0x28 String buffer). The original IsDerivedFrom
    // guard rejects valid descriptions in standalone's partial factory tree (the
    // documented FUN_1401253d0-class landmine), so trust the linked description —
    // it comes straight from the entity template. Null desc => no shape (1x1).
    Description* linked = self->_description;
    MapDescription* desc = static_cast<MapDescription*>(linked);
    const char* shape = desc ? desc->_footprint._data : nullptr;

    StartTown_UnmarkResourceCellGrid(game, ownerEntity,
                                     (int)static_cast<int16_t>(self->tileX0),
                                     (int)static_cast<int16_t>(self->tileY0),
                                     (int)static_cast<int16_t>(self->tileX1),
                                     (int)static_cast<int16_t>(self->tileY1),
                                     shape, self->extraField);

    // Clear the occupancy-written bit (4750: flags &= ~0x100000). The original's
    // low-16 changed -> RefreshEntityFlags notify can never fire from this bit,
    // so it is faithfully omitted.
    self->_flags = flags & ~0x100000u;
}


// ============================================================================
// ENDGAME B.3.8 VIS — MapComponent as a REAL class (MapComponent.h).
//
// Before: a 48-slot MapComponent_InertVtable filled per create, Inert_ScalarDtor
// in slot 0 (only ~Internal). After: the compiler's 16-slot vtable and a real
// ctor/dtor chain (B38_RULES D1, D2); slot bodies are the ones the old table
// installed. Slot 7 (the slot FUN_1400c2ea0 dispatches at entity removal)
// routes to the removal body below, whose 0x140124750 tail is the grid
// un-mark; the bit-0x100000 gate means it no-ops unless occupancy was written.
// ============================================================================

// 0x1401244d0: Component() is the CALL 0x1401188b0; the 0x140163aa0 stamp is
// the compiler's; `OR EAX,0xffffffff` then four word stores of 0xFFFF into the
// rect (0x1401244fb-0x14012450a, B6 row 28); flags |= 3 (0x83). Retail does not
// write +0x30 / +0x34 (the next store after the rect is the flags OR at 0x140124516):
// +0x30 keeps the allocation's bytes until a placer writes the rotation (row 920,
// B7 RES). Every rect writer also writes +0x30 before the first reader (AddToMap /
// RemoveFromMap here, decal, zone, cemetery, destroy, keystone_a_map, lifeevent):
// the placement tools (tool_base_tile, dragplacementtool, bridgetool / fencedtool
// calcpath), start town (starttown.cpp's spawn), the upgrade clone
// (destroycomponent_states), the resource / tombstone spawners and Persist.
MapComponent::MapComponent()
    : tileX0(-1), tileY0(-1), tileX1(-1), tileY1(-1) {
    OrFlagsInCtor(0x3);
}

// ============================================================
// MapComponent::Persist (vtable slot [3], 0x1401243e0)
// ============================================================
// Serializes tile coordinates and the rotation int.
//
// Original disassembly:
//   CALL Component_Serialize (0x1401189b0)
//   CALL Serializer_Ctor(local, stream, 0)
//   CALL Serialize_Short(&tileX0, nullptr) x4
//   CALL Serialize_Int(&extraField, nullptr)
//   CALL Serializer_Dtor(local)

void MapComponent::Persist(IO::Stream& stream) {
    // Call base Component_Serialize
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    // The 4 tile coords, Serialize(short&) as retail.
    ser.Serialize(tileX0, nullptr);
    ser.Serialize(tileY0, nullptr);
    ser.Serialize(tileX1, nullptr);
    ser.Serialize(tileY1, nullptr);

    // Serialize the rotation at +0x30
    ser.Serialize(extraField, nullptr);
}

// [6] 0x140124370-0x1401243b0: IsDerivedFrom on [RCX+0x18] (cell 0x1401c7a18), CMOVNZ,
// `CMP byte [RDX+0x30],0` with no test of the cast, then CALL 0x1401241b0 (AddToMap).
// Dispatched by ComponentEntity::OnAddedPass at EntityList::RegisterEntity (flags 0x83
// carry bit 0). _claimOnAdd is the "claim at spawn" switch: natural resources and raw
// material piles set it and claim their cell at registration (the orchard / forester /
// pile-scan readers of MapCell::occupant depend on it); a building leaves it clear and
// claims its footprint at message 1 (OnEvent below).
void MapComponent::OnAdded() {
    if (GetMapDescription(this)->_claimOnAdd) {
        AddToMap();   // 0x1401241b0
    }
}
// ============================================================
// MapComponent::OnRemove (vtable slot [7], 0x1401242f0)
// ============================================================
// If the description's +0x32 is set, clears the minimap rect.
// Always calls the 0x140124750 map un-mark at the end.
//
// Original disassembly:
//   Get the description via IsDerivedFrom
//   CMP byte [RDX+0x32],0
//   JZ skip_clear
//   MOVSX sign-extend tile coords from int16 to int32
//   MOV RCX,[g_pGameWorldSingleton]
//   MOV RCX,[RCX+0x148]            ; the minimap overlay
//   XOR EDX,EDX                    ; param2 = 0
//   CALL 0x1400a4d30               ; rect fill
// skip_clear:
//   JMP 0x140124750                ; tail call to the map un-mark

// [7] 0x1401242f0
void MapComponent::OnRemove() {
    // B.3.8 CB1b: the ported retail removal body (0x1401242f0).
    MapDescription* building = GetMapDescription(this);
    if (building) {
        if (building->_showOnMap) {
            if (GameUpdateLoop::_singleton != nullptr) {
                Minimap* terrainPtr = GameUpdateLoop::_singleton->m_imageCapture;   // +0x148
                if (terrainPtr) {
                    terrainPtr->ClearTerrain(0,   // 0x1400a4d30
                        (int)(int16_t)tileX0,
                        (int)(int16_t)tileY0,
                        (int)(int16_t)tileX1,
                        (int)(int16_t)tileY1);
                }
            }
        }
    }

    // Always the map un-mark (tail call in original): 0x140124750
    RemoveFromMap();
}
// Slot 12 (+0x60) — the per-component MESSAGE slot, retail `MapComponent_Slot12`
// (0x1401243c0), which is only three instructions:
//
//     CMP EDX,1 ; JZ 0x1401241b0 ; RET
//
// i.e. "on message 1, run the occupancy write". The broadcaster is
// ComponentEntity::SendEvent (0x1400c2b70), which walks the entity's
// components and calls each one's +0x60 with the id — and the caller that passes **1** is
// `ClearAreaDescription_helper_77f0`, the ClearArea **state-2 ENTER**
// (`MOV EDX,1; CALL c2b70` at 0x140117807). Every other +0x60 dispatch in the exe passes
// 0 (verified: all three BuildComponent sites and both sub_8720 sites are `XOR EDX,EDX`).
//
// So retail's answer to "when does a building claim its footprint cells" is: **when its
// ClearArea finishes**, not when it is placed. That is why a fresh foundation is pickable
// only by its decal mesh (fiddly), while a cleared/among-completed building is cell-picked
// and blocks navigation — and it is what makes dc20's stale-occupant eviction safe: by the
// time it runs, the trees have been harvested by citizens, so it only sweeps stragglers.
//
// Retail 0x1401243c3 JZ's straight into AddToMap, paint included: a building's minimap
// dot and its cell claim both appear when its ClearArea finishes.
void MapComponent::OnEvent(int msgId) {
    if (msgId == 1) {
        AddToMap();   // 0x1401241b0
    }
}

// create 0x140035700: MemAlloc(0x38, 8), then the ctor.
::Object::Base* MapComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MapComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MapComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* MapComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MapComponent());
}

DEFINE_EXE_TYPE(MapComponent, "Component")
