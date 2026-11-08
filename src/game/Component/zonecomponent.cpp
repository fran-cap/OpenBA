// src/game/Component/zonecomponent.cpp
// Clean reimplementation of ZoneComponent lifecycle methods from Application-x64.exe.
//
// ZoneComponent (0x28 bytes) — minimal component, manages zone rendering.
// No additional fields beyond Component base.
//
// Vtable at 0x140164e30. Three non-trivial overrides:
//   Shutdown (0x1401302F0) — Serialize + conditional zone map registration
//   Draw     (0x1401301F0) — Zone rendering (complex, many deps)
//   OnCreate (0x140130080) — Zone creation + tree walk (complex)
//
// Draw and OnCreate are deeply coupled to zone rendering subsystem.
// We call the original exe functions for these.
//

#include "Component/ZoneComponent.h"
#include "Game/Map.h"   // Map::StampFootprint (0x14009d360)
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <new>
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"   // ZoneComponent_sub_d360 (0x14009d360)
#include "system/MemoryAllocator.h"
#include "Component/MapComponent.h"
#include "Component/MapDescription.h"   // MapDescription::_footprint (+0x28)
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>

// ============================================================
// Helpers
// ============================================================
// FUN_140088e20 CitizenBehavior::AddZoneEntity — zone map registration (below).
// FUN_140004680 FindComponent<MapComponent> — the tile component of an entity.
// FUN_140017230 ResidenceComponent_impl_7230 — red-black tree node insert/lookup;
//   returns ptr-to-(ptr-to-node).
// FUN_140003110 — append an entity ref to a tree node's child list.
// FUN_14009d360 ZoneComponent_sub_d360 — rasterize zone tiles into the world grid.
// FUN_140002cc0 Array<T>::Remove — finalize/cleanup tree node during OnCreate.
// The GameWorld cell 0x1401c3dd8 is read through g_pGameWorldSingleton.


// ============================================================
// CitizenBehavior::AddZoneEntity (0x140088e20): register an entity in the zone map.
// ============================================================
// `entity` is an InternalBasePtr*; key = the entity's description (+0x28). Walk the
// red-black tree at +0x160 for that key; if no node matches, insert one via TreeInsert;
// then append the entity ref to the node's child list (node+8, count = node[2]+1).
// The +0x160 tree's nodes are TemplateBucket (Entity/CitizenBehavior.h), the key
// compared by address.
void CitizenBehavior::AddZoneEntity(Object::InternalPtr<ComponentEntity>* entity) {
    void* entityPtr = entity;

    ComponentDescription* key = entity->Get()->_description;

    // Retail (0x140088e20) is `while (puVar3 = 0, puVar1 != 0)` — the reset is part
    // of the loop CONDITION, so it runs before every test including the one that
    // ends the walk. `found` is therefore non-null ONLY on the break path, i.e.
    // only on an exact key match.
    //
    // This was written as a `while (node)` with `found = nullptr` as the first
    // statement of the BODY, which is not the same program: the body does not run
    // on the iteration that exits, so `found` kept the last node the walk descended
    // RIGHT from — a lower bound, not a match. A non-null `found` then skips the
    // insert below and appends the entity to that other node's list, so the zone
    // was filed under a different description entirely and its own key never
    // appeared in the tree.
    //
    // Symptom: TutorialGoalCount's Zone mode counts this registry, so a drag-placed
    // StorageYard was placed and solid while stepFirstPile2's goal counted 0
    // forever. Intermittent, because whether the final descent goes left or right
    // depends on the ordering of heap description pointers — which differs run to
    // run. Reached step 18 by a jump and the tree was small enough to work; reached
    // it organically, with WoodHouse and Clear already registered, and it did not.
    TemplateBucket* node = entitiesByTemplate._root;
    TemplateBucket* found = nullptr;
    for (;;) {
        found = nullptr;
        if (node == nullptr) break;
        if (key < node->element.key) {
            node = node->left;    // +0x20
        } else {
            found = node;
            if (key <= node->element.key) break;   // equal key => exact match, stop
            node = node->right;   // +0x28
        }
    }

    if (found == nullptr) {
        TemplateBucket* outNode[2] = {nullptr, nullptr};
        CitizenBucketEntities empty = {};   // the 0x10 array record: retail zeroes two qwords (0x140088e89 / e8e)
        TemplateBucket** ret = InsertCitizenBucket(&entitiesByTemplate, outNode, &key, &empty);   // 0x140017230
        if (ret) found = *ret;
        System::MemoryAllocator::MemFree(nullptr);
    }

    // FUN_140003110 is Collection::Array<void*>::SetSize(n, fill): the bucket's own
    // typed Array (+0x08, CitizenPtrList over kSpec_PtrZero).
    if (found)
        found->element.entities.SetSizeFill(found->element.entities._size + 1, entityPtr);
}

// ============================================================================
// ENDGAME B.3.8 VIS — ZoneComponent as a REAL class (ZoneComponent.h).
//
// Before: a 48-slot ZoneComponent_InertVtable filled per create, Inert_ScalarDtor in
// slot 0 (only ~Internal).
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/ZoneComponent.h"

// Retail dispatch: EntityList::RegisterEntity -> ComponentEntity_OnAddedPass ->
// ZoneComponent slot 6 = Draw (0x1401301f0) -> sub_d360 (0x14009d360), which
// writes the owning entity into **cell + 0x00** for every non-'.' footprint
// cell, notifies the pathfinder mirror per cell (a6f00 cmd 3), and bumps the
// '.'-cell reserve counter at cell+0x28. Slot 7 = OnCreate (0x140130080),
// dispatched by MarkForRemoval's vtable+0x38 walk, rasterizes the same rect
// with a NULL ref — the symmetric teardown.
//
// cell+0x00 is the slot EditTool's cell pick (FUN_1400a0280) resolves FIRST —
// before the cell+0x08 solid layer buildings only claim when their clearing
// finishes (MapComponent slot 12). This is what makes a just-placed foundation
// clickable in retail while the trees still hold +0x08.

// 0x1401303c0: Component() is the CALL 0x1401188b0; the 0x140164e30 stamp is
// the compiler's; flags |= 3 (0x83 with Component's 0x80 — bit 0 puts every
// ZoneComponent in the OnAddedPass dispatch, exactly as retail).
ZoneComponent::ZoneComponent() {
    OrFlagsInCtor(0x3);
}

// ============================================================
// ZoneComponent::Persist (0x1401302F0)
// ============================================================
// Serializes component, then conditionally adds entity to zone map.
// If serializer version==0 and stream is reading (mode==0),
// registers the owning entity into the zone map at GameState+0xF0.

void ZoneComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream& str = stream;
    IO::Serializer ser(str, 1);

    int version = ser.GetVersion();
    if (version == 0 && static_cast<int>(str.GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        Object::InternalPtr<ComponentEntity> entityPtr(_entity);
        if (GameUpdateLoop::_singleton != nullptr) {
            CitizenBehavior* zoneMap = GameUpdateLoop::_singleton->m_citizenList;
            zoneMap->AddZoneEntity(&entityPtr);   // 0x140088e20
        }
    }
}
// ============================================================
// ZoneComponent::OnAdded (0x1401301F0)
// ============================================================
// Reimpl of ZoneComponent::Draw (0x1401301F0): find the owner's map component,
// pull its tile rect (x,y,w,h at +0x28..+0x2e) and kind (+0x30), resolve the zone
// description via IsDerivedFrom(map+0x18, zoneDescTypeId), rasterize the zone tiles
// into the world grid, then (re)register the entity in the zone map tree.

void ZoneComponent::OnAdded() {
    MapComponent* mapIBP = _entity->FindComponent<MapComponent>();   // 0x140004680
    if (!mapIBP) {
        return;   // standalone-defensive: retail data guarantees the map comp
    }

    int16_t x = static_cast<int16_t>(mapIBP->tileX0);
    int16_t y = static_cast<int16_t>(mapIBP->tileY0);
    int16_t w = static_cast<int16_t>(mapIBP->tileX1);
    int16_t h = static_cast<int16_t>(mapIBP->tileY1);
    int kind = mapIBP->extraField;

    // Retail gates the description through IsDerivedFrom(DAT_1401c7a18); in
    // standalone's partial factory tree that check rejects valid descriptions
    // (the documented FUN_1401253d0-class landmine, same accommodation as
    // the deleted MapComponent_PaintMinimapRect; see row 991) and d360 would then deref (null+0x28).
    // Trust the linked description — it comes straight from the entity template.
    Description* desc = mapIBP->_description;
    if (!desc) {
        return;
    }

    if (GameUpdateLoop::_singleton == nullptr) {
        return;
    }
    GameUpdateLoop* world = GameUpdateLoop::_singleton;

    // The MapComponent's description is a MapDescription (retail's IsDerivedFrom
    // 0x1401c7a18 above, skipped by the tree).
    MapDescription* mapDesc = static_cast<MapDescription*>(desc);
    Object::InternalPtr<ComponentEntity> entityRef(_entity);
    world->m_game->StampFootprint(&entityRef,
        (int)x, (int)y, (unsigned int)(int)w, (int)h,
        &mapDesc->_footprint, kind);   // 0x14009d360

    entityRef = _entity;
    world->m_citizenList->AddZoneEntity(&entityRef);   // 0x140088e20

    // BANISHED_REGENTITY_TRACE — this call is the ONLY writer of the
    // per-description zone registry at (world+0xf0)+0x160, which is what
    // TutorialGoalCount's Zone mode counts. A drag-placed StorageYard reaches
    // OnAdded with the same armed component set as a Clear zone (Map, Zone,
    // Decal, Work, StateMachine) and the correct description, yet never gains a
    // node — so either one of the two early returns above fires for it, or this
    // add runs and does not insert. Say which.
}
// ============================================================
// ZoneComponent::OnRemove (0x140130080)
// ============================================================
// Reimpl of ZoneComponent::OnCreate (0x140130080): like Draw, find the map
// component and zone description, rasterize the zone tiles, then walk the zone-map
// red-black tree for the owner's entity id and finalize the matching node.

void ZoneComponent::OnRemove() {
    MapComponent* mapIBP = _entity->FindComponent<MapComponent>();   // 0x140004680
    if (!mapIBP) return;   // standalone-defensive: retail data guarantees the map comp

    int16_t x = static_cast<int16_t>(mapIBP->tileX0);
    int16_t y = static_cast<int16_t>(mapIBP->tileY0);
    int16_t w = static_cast<int16_t>(mapIBP->tileX1);
    int16_t h = static_cast<int16_t>(mapIBP->tileY1);
    int kind = mapIBP->extraField;

    // Same IsDerivedFrom landmine accommodation as Draw above: trust the linked
    // description or d360 derefs (null+0x28).
    Description* desc = mapIBP->_description;
    if (!desc) return;

    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* world = GameUpdateLoop::_singleton;

    MapDescription* mapDesc = static_cast<MapDescription*>(desc);
    Object::InternalPtr<ComponentEntity> entityRef(nullptr);
    world->m_game->StampFootprint(&entityRef,
        (int)x, (int)y, (unsigned int)(int)w, (int)h,
        &mapDesc->_footprint, kind);   // 0x14009d360

    // Walk the zone-map tree (CitizenBehavior +0x160, TemplateBucket nodes) for the
    // owner's description (the entity's +0x28).
    Object::InternalPtr<ComponentEntity> ownerRef(_entity);
    CitizenBehavior* zoneMap = world->m_citizenList;
    ComponentDescription* key = _entity->_description;

    TemplateBucket* node = zoneMap->entitiesByTemplate._root;
    TemplateBucket* found = nullptr;
    while (node != nullptr) {
        if (key < node->element.key) {
            node = node->left;    // +0x20
        } else {
            if (key <= node->element.key) { found = node; break; }
            node = node->right;   // +0x28
        }
    }
    if (found)
        found->element.entities.Remove(ownerRef);   // 0x140002cc0 (the bucket's Array, +0x08)
}

// create 0x14003a6a0: MemAlloc(0x28, 8), then the ctor.
::Object::Base* ZoneComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ZoneComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ZoneComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ZoneComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ZoneComponent());
}

DEFINE_EXE_TYPE(ZoneComponent, "Component")
