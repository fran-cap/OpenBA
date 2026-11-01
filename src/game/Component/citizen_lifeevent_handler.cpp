// src/game/Component/citizen_lifeevent_handler.cpp
// CitizenComponent_HandleLifeEvent (0x140116dd0) + its five previously-unported
// helpers — the citizen-DEATH path, formerly a "Level-2 deep closure" defer.
// Closure audit 2026-07-19: every sub-callee is now registered, so the whole
// chain ports cleanly:
//
//   0x140116dd0 HandleLifeEvent        (this file)  — orchestrator, returns bool
//   0x140113a70 FamilyRingOrFlag_3a70  (this file)  — OR life-event flag around the
//                                        family ring (+0x260 head / +0x268 next)
//   0x140089740 FindNearestWithSlot    (this file)  — nearest cemetery (path-dist,
//                                        reachability 0xe1f, has free grave slot)
//   0x140046c60 PickFreeSlot_6c60      (this file)  — random-start scan of the
//                                        grave-cell array (+0x48/+0x50) for a cell
//                                        not occupied by a TombstoneComponent
//   0x140046a90 SpawnTombstone_6a90    (this file)  — claim map cell (state 2),
//                                        pathfinder dirty+enqueue, ctor tombstone
//                                        entity from CemeteryDescription+0x20,
//                                        place + register in the entity list
//   0x140004800 FindTombstoneComp_4800 (this file)  — component scan by type-id
//   0x140057b20 PlaceAtCell_7b20       (this file)  — stamp position component
//                                        (x,y,0x10001,yaw) + model transform via
//                                        Transform::FromYawPitchOffset at terrain h
//
// Death flags OR'd into +0x88 of self/partner(+0x258)/parent(+0x250)+family ring:
//   0x40 = buried (grave found), 0x02 = eventType 2 with no grave, 0x80 = other
//   death with no grave. Then PostLifeEvent(type, severity 1) posts the event-log
//   message and the citizen entity is queued on GameUpdateLoop+0xd0 for removal
//   (unless entity flag bit 10 at +0x40 already marks it).
//
// Type-id .rdata globals (storage + fill in standalone/reg_citizen_lifeevent_handler.cpp):
//   0x1401c7900 InteractComponent (already registered by reg_work_tail_fire)
//   0x1401c4a30 TombstoneComponent   (NEW storage)
//   0x1401c40c8 CemeteryDescription  (NEW storage)
//
// Retail callers of 0x140116dd0: CitizenComponent_Update (6 CALL sites) — already
// resolving through the registry (citizencomponent.cpp s_fn_HandleLifeEvent).

// graphics/Geometry.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Geometry.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C)
#include "ext/family_tree/family_tree.h"
#include <new>
#include "err_line.h"   // HITCH_POSTDRAIN: one write per diagnostic line
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/InteractComponent.h"     // ENDGAME B.3.9 C1: 0x1401c7900 cell class
#include "Component/TombstoneComponent.h"    // ENDGAME B.3.9 C1: 0x1401c4a30 cell class
#include "Component/ComponentDescription.h"   // _category (+0x20)
#include "Component/CemeteryDescription.h"   // ENDGAME B.3.9 C1: 0x1401c40c8 cell class
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Component/CitizenComponent.h"      // B.4 W1 COMP-b: HandleLifeEvent is a member
#include "Component/CharacterComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"          // cemeteries (+0x40 / +0x48)
#include "Entity/EntityList.h"               // removalList (+0xD0)
#include "Component/CemeteryComponent.h"     // plots (+0x48 / +0x50)
#include "Component/MapComponent.h"          // the footprint rect / rotation
#include "Component/ModelComponent.h"        // transform / yaw / meshInstance
#include "Game/Map.h"                        // size / cells / pathfinder / regionGraph
#include "Game/PathfinderThread.h"           // SetCellTerrain (0x1400a6fe0)
#include "Game/terrain_sidecar.h"            // [terrain sidecar] NoteWrite after the +0x29 store
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store

// Row 310 G1 (B7 RES): 4800 / 6c60 / 9740's inner component scans are
// ComponentEntity::FindComponent<T>()'s shape (`[[slot]+0x10]` with no null-slot test,
// IsDerivedFrom + CMOVNZ on the first type match); the tree's FindComponentByTypeId with
// its null-slot skip is gone, and so are the bodies' tree-only null returns below.

// ---------------------------------------------------------------- 0x140113a70
// CitizenComponent::OrFamilyFlags: walk the circular children ring (+0x260 head,
// +0x268 next) OR-ing the life-event flag into each child's +0x88 status word.
void CitizenComponent::OrFamilyFlags(uint32_t flags) {
    Object::InternalPtr<CitizenComponent>* head = &_firstChild;
    CitizenComponent* cur = head->Get();
    if (!cur) return;
    do {
        cur->happinessFlags |= flags;
        cur = cur->_nextSibling.Get();   // 0x140113ab6: [cur+0x268], no test before the compare
    } while (cur != head->Get());
}

// ---------------------------------------------------------------- 0x140046c60
// Pick a grave slot: random start index, walk the cemetery's grave-cell array
// (+0x48 int cells, +0x50 count) with wrap. A slot is usable if its map cell is
// empty OR its occupant entity has no TombstoneComponent. Returns slot index or
// 0xffffffff when every cell already holds a tombstone.
// Row 310 (B7 RES), 0x140046c60: Rand(count) is drawn FIRST, before the count test (the
// tree returned on count <= 0 without drawing); no world test.
extern "C" uint32_t Cemetery_PickFreeSlot_6c60(CemeteryComponent* self) {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    System::Random* rng = world->m_random;                       // +0xd8
    int idx = static_cast<int>(rng->Rand(static_cast<uint32_t>(self->plots._size)));   // +0x50
    int count = self->plots._size;
    Map* map = world->m_game;                                    // +0xf8
    for (int tries = 0; tries < count; tries++) {
        int cell = self->plots[idx];                             // +0x48
        ComponentEntity* occupant = map->cells.GetData()[cell].occupant.Get();   // +0x80 -> +0x08, stride 0x30
        if (!occupant) return static_cast<uint32_t>(idx);      // empty cell — free slot
        Component* ts = occupant->FindComponent<TombstoneComponent>();   // cell 0x1401c4a30
        Component* tsIBP = ts;
        if (!tsIBP) return static_cast<uint32_t>(idx);         // occupant is not a tombstone
        idx = (idx + 1 >= count) ? 0 : idx + 1;
    }
    return 0xffffffffu;
}

// ---------------------------------------------------------------- 0x140089740
// Find the nearest cemetery with a reachable free grave slot. listMgr =
// *(world+0xf0): +0x40 component-ptr array, +0x48 count. Distance = path
// distance (f810) or euclidean + mapSize when unreachable-by-path; candidate
// must pass ValidateCell(0xe1f) reachability and have PickFreeSlot >= 0.
extern "C" CemeteryComponent** Cemetery_FindNearestWithSlot_9740(CitizenBehavior* listMgr, CemeteryComponent** out,
                                                                  uint32_t citizenCell) {
    *out = nullptr;
    // Row 310 (B7 RES), 0x140089740: no world / list / slot test.
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Map* map = world->m_game;                                    // +0xf8
    Map* mapGrid = map;
    int best = 0x7fffffff;
    int count = listMgr->cemeteries._size;                          // +0x48
    for (int i = 0; i < count; i++) {
        Object::InternalPtr<CemeteryComponent>* slot = &listMgr->cemeteries._data[i];      // +0x40
        CemeteryComponent* comp = slot->Get();
        ComponentEntity* entity = comp->_entity.Get();                 // +0x10
        // The cemetery's map position = its InteractComponent's resolved position.
        InteractComponent* icIBP = entity->FindComponent<InteractComponent>();   // cell 0x1401c7900
        Math::Vector3 pos;
        icIBP->NodePosition(&pos, 0, 0, nullptr);         // 0x140122e20
        int size = map->size;
        int cx = static_cast<int>(pos.x + static_cast<float>(size >> 1));
        int cy = static_cast<int>(pos.y + static_cast<float>(size >> 1));
        if (cx < 0) cx = 0;
        if (size <= cx) cx = size - 1;
        if (cy < 0) cy = 0;
        if (size <= cy) cy = size - 1;
        uint32_t cemCell = static_cast<uint32_t>(size * cy + cx);
        int dist = map->PathDistance(citizenCell, cemCell, 1);   // 0x14009f810
        if (dist < 0) {
            int dy = static_cast<int>(citizenCell) / size - static_cast<int>(cemCell) / size;
            int dx = static_cast<int>(citizenCell) % size - static_cast<int>(cemCell) % size;
            dist = static_cast<int>(sqrtf(static_cast<float>(dy * dy + dx * dx))) + size;
        }
        if (dist < best
            && (mapGrid->ValidateCell(0xe1f, citizenCell, static_cast<int>(cemCell)) & 0xff) != 0) {
            if (static_cast<int>(Cemetery_PickFreeSlot_6c60(slot->Get())) >= 0) {
                *out = slot->Get();
                best = dist;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- 0x140004800
// Wrap the entity's TombstoneComponent (or null) in the out InternalBasePtr.
extern "C" TombstoneComponent** ComponentEntity_FindTombstoneComp_4800(ComponentEntity* entity,
                                                                       TombstoneComponent** outIBP) {
    *outIBP = entity->FindComponent<TombstoneComponent>();   // the InternalBasePtr ctor's one store
    return outIBP;
}

// ---------------------------------------------------------------- 0x140057b20
// Place the tombstone at its grave cell: stamp the position component
// (x,y at +0x28/+0x2a, extent 0x10001 at +0x2c, yaw degrees normalized to
// [0,360) at +0x30) and set the model component's transform (+0x148 quat+pos,
// +0x168 yaw) from Transform::FromYawPitchOffset at terrain height.
// Row 994 (0x140057b20 part, B7 RES): 0x140057b2e-0x140057ba0 reads [gw+0xf8], the
// receiver's +0x10 and both finds' results (MOV word [RCX+0x28] at 0x140057ba0) with no
// test; neither D4(a) nor (b) applies (no harness route, no shared template, no PORT GAP),
// so the tree's world / receiver / component returns are gone.
extern "C" void Entity_PlaceAtCell_7b20(TombstoneComponent* self, int cell, float yawDeg) {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Map* map = world->m_game;                                    // +0xf8
    int size = map->size;
    ComponentEntity* entity = self->_entity.Get();                     // +0x10
    MapComponent* posIBP = entity->FindComponent<MapComponent>();        // 0x140004680
    ModelComponent* modelIBP = entity->FindComponent<ModelComponent>();  // 0x140003f60
    int yaw = static_cast<int>(yawDeg);
    posIBP->tileX0 = static_cast<int16_t>(cell % size);         // +0x28
    posIBP->tileY0 = static_cast<int16_t>(cell / size);         // +0x2a
    posIBP->tileX1 = 1;                                          // +0x2c / +0x2e: the dword 0x10001 (H7)
    posIBP->tileY1 = 1;
    posIBP->extraField = yaw;                                    // +0x30
    if (yaw < 0) {
        do { yaw += 360; } while (yaw < 0);
        posIBP->extraField = yaw;
    }
    // World-space cell centre: cell - (halfMap - 0.5f). 0.5f = exe DAT_140172114.
    float half = static_cast<float>(size >> 1) - 0.5f;
    float inXY[3] = { static_cast<float>(cell % size) - half,
                      static_cast<float>(cell / size) - half, 0.0f };
    Math::Vector3 terr(0.0f, 0.0f, 0.0f);
    map->GetTerrainPos(&terr.x, inXY, '\0', nullptr);
    Math::Scalar pitch(0.0f);
    Math::Scalar yawF(yawDeg);
    Math::Transform xform = Math::Transform::FromYawPitchOffset(yawF, pitch, terr);
    modelIBP->transform = xform;                                 // +0x148 (7-dword copy)
    modelIBP->yaw = yawDeg;                                      // +0x168
    modelIBP->meshInstance.SetTransform(modelIBP->transform);    // +0x28 Geometry::SetTransform
}

// ---------------------------------------------------------------- 0x140046a90
// Claim the grave cell and spawn the tombstone entity there.
// cemComp = cemetery component, cell = grave cell index, citizenCell unused
// (matches retail), worldArg = game-world singleton (passed to the pathfinder cmd).
extern "C" void Cemetery_SpawnTombstone_6a90(CemeteryComponent* cem, int cell,
                                             uint64_t /*citizenCell*/, void* /*worldArg*/) {
    // No test of the world, the cemetery, the MapComponent, the description, the new
    // entity or its TombstoneComponent: 0x140046a90 reads each unguarded.
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    ComponentEntity* nullIBP = nullptr;                          // the InternalBasePtr(nullptr) ctor
    Map* map = world->m_game;                                    // +0xf8
    MapCell* cellRec = map->cells.GetData();                               // +0x80, stride 0x30
    uint8_t oldState = cellRec[cell].terrainType;                // +0x29
    bool stateWasWalk = ((1u << (oldState & 0x1f)) & 0x1e0u) != 0;   // old state 5..8
    map->field_0xA2 |= static_cast<uint8_t>(stateWasWalk);   // SETNZ AL; OR byte [map+0xa2],AL (0x140046afd)
    cellRec[cell].terrainType = 2;
    TerrainSidecar::NoteWrite(&cellRec[cell]);   // [terrain sidecar] mirror (missing until PERFXEXT)
    CellStamp::NoteWrite(&cellRec[cell]);   // [AMBMEMO] cell write clock
    int size = map->size;
    if (stateWasWalk)
        map->regionGraph->MarkCellDirty(cell % size, cell / size);   // +0x70, 0x1400a8c90
    map->pathfinder->SetCellTerrain(cell, 2);                        // +0x68, 0x1400a6fe0
    cellRec[cell].occupant = nullIBP;  // = null (+0x08)
    CellStamp::NoteWrite(&cellRec[cell]);   // [AMBMEMO] cell write clock
    // Tombstone yaw = the cemetery's own placed rotation (position component +0x30).
    MapComponent* resIBP = cem->_entity->FindComponent<MapComponent>();   // 0x140004680
    int rot = resIBP->extraField;
    // Tombstone description lives at CemeteryDescription+0x20.
    Description* desc = cem->_description;                       // +0x18
    // Narrowed to CemeteryDescription (cell 0x1401c40c8), null on a miss, +0x20 read unguarded;
    // the description itself is not tested (0x140046b74-0x140046b8c: CALL IsDerivedFrom, CMOVNZ).
    CemeteryDescription* cemDesc = desc->IsDerivedFrom(CemeteryDescription::TypeId())
                                   ? static_cast<CemeteryDescription*>(desc) : nullptr;
    ComponentDescription* tombDesc = cemDesc->_ref20;            // +0x20
    ComponentEntity* built = nullptr;
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    if (mem) built = ::new (mem) ComponentEntity(tombDesc);
    ComponentEntity* entIBP = built;
    TombstoneComponent* tsIBP = nullptr;
    ComponentEntity_FindTombstoneComp_4800(entIBP, &tsIBP);
    Entity_PlaceAtCell_7b20(tsIBP, cell, static_cast<float>(rot));
    Object::InternalPtr<ComponentEntity> regIBP = entIBP;
    world->m_entityList->RegisterEntity(&regIBP,   // +0xe8
                                   static_cast<int>(tombDesc->_category));   // +0x20
}

// ---------------------------------------------------------------- 0x140116dd0
// Citizen death handler. Returns 1 (handled) or 0 when the world is in the
// bit-8 state at +0xd0 (loading/shutdown) — retail early-outs with AL=0 there.
uint64_t CitizenComponent::HandleLifeEvent(int eventType, const char* strParam) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C), the cause a
    // death will be recorded with
    Ext::FamilyTree::OnLifeEvent(this, eventType);
    void* selfV = this;
    // Row 310 (B7 RES), 0x140116dd0: no world test, the character / model read straight,
    // the second pick's slot used as is, the entity list read straight. Its tests are the
    // +0xd0 bit 8, the home, the cemetery, parent, spouse and the entity pointer only.
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if ((world->m_gameFlags >> 8 & 1) != 0) return 0;

    // --- Death position: the home's InteractComponent (+0x238) if housed, else the
    // citizen's own model (+0x278 -> +0x68, raw transform storage) ---
    Math::Vector3 pos;
    if (home == nullptr) {
        CharacterComponent* ch = character.Get();
        ModelComponent* homeEnt = ch->modelComponent.Get();
        pos.x = homeEnt->transform.translation.x;   // +0x158
        pos.y = homeEnt->transform.translation.y;   // +0x15c
        pos.z = homeEnt->transform.translation.z;   // +0x160
    } else {
        home->FindComponent<InteractComponent>()->NodePosition(&pos, 0, 0, nullptr);   // 0x140122e20
    }

    // --- Clamp to a map cell ---
    int size = world->m_game->size;
    int cx = static_cast<int>(pos.x + static_cast<float>(size >> 1));
    int cy = static_cast<int>(pos.y + static_cast<float>(size >> 1));
    if (cx < 0) cx = 0;
    if (size <= cx) cx = size - 1;
    if (cy < 0) cy = 0;
    if (size <= cy) cy = size - 1;
    uint32_t cell = static_cast<uint32_t>(size * cy + cx);

    // --- Find a cemetery; spawn the tombstone; pick the family flag ---
    // BURIAL OUTCOME TRACE. Every arm below used to be silent, so "nobody was buried"
    // and "everybody was buried" produced identical logs -- and for the whole life of
    // this port the answer was always the first one, because the cemetery list at
    // *(gw+0xf0)+0x40 had no writer at all. Report the town-level count, the per-cemetery
    // plot count, and which branch was taken, so a regression names itself.
    CitizenBehavior* const townMgrForTrace = world->m_citizenList;
    const int cemCountForTrace =
        townMgrForTrace ? townMgrForTrace->cemeteries._size : -1;   // +0x48

    CemeteryComponent* cem = nullptr;
    Cemetery_FindNearestWithSlot_9740(world->m_citizenList, &cem, cell);
    uint32_t flags;
    if (!cem) {
        flags = (eventType != 2) ? 0x80u : 0x2u;
        // eventType names the CAUSE; without it every death in the log looks alike:
        // 0 starvation, 1 thirst, 2 old age, 3 health, 6 disease, 7 other.
        static const char* kCause[] = {"starvation", "thirst", "old-age", "health",
                                       "?4", "?5", "disease", "other"};
        ErrLine("[Burial] NO GRAVE: citizen=%p cause=%d(%s) cell=%u "
                        "townCemeteries=%d (0 => the state-2 registrar never ran; "
                        ">0 => every cemetery is full, unreachable, or has an empty "
                        "plot array) flags=0x%x\n",
                selfV, eventType,
                (eventType >= 0 && eventType < 8) ? kCause[eventType] : "?",
                cell, cemCountForTrace, flags);
        fflush(stderr);
    } else {
        flags = 0x40u;
        const int plotsForTrace = cem->plots._size;   // +0x50
        int slot = static_cast<int>(Cemetery_PickFreeSlot_6c60(cem));
        int graveCell = cem->plots[slot];       // +0x48, indexed with no test (9740 just found a slot)
        Cemetery_SpawnTombstone_6a90(cem, graveCell, cell, world);
        ErrLine("[Burial] BURIED: citizen=%p cem=%p townCemeteries=%d "
                        "plots=%d slot=%d graveCell=%d\n",
                selfV, cem, cemCountForTrace, plotsForTrace, slot, graveCell);
        fflush(stderr);
    }

    // --- OR the flag into the parent (+0x258), the spouse (+0x250) + the spouse's
    //     children ring, and our own children ring ---
    if (_parent != nullptr) {
        _parent->happinessFlags |= flags;
    }
    if (spouse != nullptr) {
        spouse->happinessFlags |= flags;
        spouse->OrFamilyFlags(flags);
    }
    OrFamilyFlags(flags);

    // --- Post the event-log message (severity 1) ---
    PostLifeEvent(eventType, 1, strParam);

    // --- Queue the citizen entity for removal (the EntityList's removal list at
    //     +0xd0) ---
    ComponentEntity* entIBP = _entity.Get();
    EntityList* loop = world->m_entityList;   // [gw+0xe8], no test
    if (entIBP != nullptr) {
        if ((entIBP->_entityFlags >> 10 & 1) == 0) {
            entIBP->MarkForRemoval();   // 0x1400c2ea0
            loop->removalList
                .SetSize(loop->removalList._size + 1, entIBP);   // +0xd0 / +0xd8
        }
    }
    return 1;
}
