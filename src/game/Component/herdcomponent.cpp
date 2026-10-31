// src/game/Component/herdcomponent.cpp
// Clean reimplementation of HerdComponent::Update from Application-x64.exe.
//
// HerdComponent manages wild animal herds. Two phases:
//   Phase 0 (grazing): countdown timer at +0x50, check terrain validity,
//                       then transition to migration when timer expires or invalid terrain.
//   Phase 1 (migrating): check migration done, then transition back to grazing
//                         with randomized timer.
//
// On each timer tick (interval from config+0x3C): checks food via helper,
// computes target herd size, calls AdjustSize.
//
// Update address: 0x140121bf0
// Struct size: 0x60, vtable at 0x140163680. Verified slot indices (from the
// .rdata table at 0x140163680): [3] Serialize 0x140121f90, [4] Startup (base
// stub 0x140118a50), [6] OnAdd 0x140121f40, [7] OnRemove 0x140121e20,
// [9] Update 0x140121bf0.
//
// Herds are created by PopulationComponent's Startup (vtable slot 4,
// 0x140054220 — see populationcomponent.cpp), which instantiates
// (mapDim/popDesc+0x2c)^2 herd entities and seeds each one through
// HerdComponent::Populate (0x140120f50).
//
// Decompiled from Application-x64.exe using Ghidra.

// graphics/Mesh.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Mesh.h"
#include "err_line.h"   // HITCH_POSTDRAIN: one write per diagnostic line
#include "Component/HerdComponent.h"
#include <cstdint>
#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::size (GameUpdateLoop::m_game)
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Action/exe_bodies.h"            // Pf_HerdStartMigration_122340
#include "Game/exe_bodies.h"              // Reimpl_MapGetTerrainPos
#include "Player/exe_bodies.h"            // ToolTile_SetTransformFromYaw_3870
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"       // CitizenBehavior::herds (GameUpdateLoop::m_citizenList)
#include "Game/exe_constants.h"           // kRdata_*
#include "math/Math.h"                    // Math::Vector3
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/HerdDescription.h"        // ENDGAME B.3.9 C1: 0x1401c7858 cell class
#include "Component/WildAnimalComponent.h"    // ENDGAME B.3.9 C1: 0x1401c8000 cell class
#include "Component/AnimalComponent.h"        // ENDGAME B.3.9 C1: 0x1401c6f98 cell class
#include "Component/CharacterComponent.h"     // ENDGAME B.3.9 C1: 0x1401c7078 cell class
#include "Component/AnimalDescription.h"      // ENDGAME B.3.9 C1: 0x1401c6fd0 cell class
#include "Component/ModelComponent.h"         // 0x1401c7a50 cell class
#include "Component/ModelDescription.h"       // ENDGAME B.3.9 C1: 0x1401c7a88 cell class
#include "Component/PopulationComponent.h"    // 0x1401c4790 cell class
#include "Component/PopulationDescription.h"  // PopulationDescription::_habitatBlock
#include "Component/ComponentDescription.h"   // ComponentDescription::_category
#include "Component/exe_bodies.h"

// ============================================================
// Helpers, called by name (B3R)
// ============================================================
//   0x140053f20 PopulationComponent::HerdCellFood     -- food availability check (below)
//   0x140121450 HerdComponent::AdjustSize             -- adjusts herd population (below)
//   0x140122340 Pf_HerdStartMigration_122340          -- starts herd migration
//   0x140121980 HerdComponent::CheckMigrationDone     -- checks if migration complete (below)
// .rdata (Game/exe_constants.h):
//   0x140172234 = 300.0f — the minutes->seconds scale every herd/growth timer multiplies its
//     description interval by. (Old comments here said 60.0f; the .rdata word is 0x43960000
//     = 300.0f and PopulationComponent_Update uses the same constant as a literal 300-second
//     smoothing period.)
//   0x14017214c = 1.0f   0x140172114 = 0.5f (food threshold)   0x140172174 = 2.0f (food scale)
//
// Sub-helpers used by AdjustSize (deep spawn path), by name:
//   0x1400c3720 ComponentEntity(ComponentDescription*)   import slot 0x140152a30 Math::Vector3(x,y,z)
//   0x14009f3e0 Reimpl_MapGetTerrainPos                  0x14012e510 WildAnimalComponent::SetHerd (below)
//   0x1400c48a0 EntityList_RegisterEntity_Impl           0x140106b10 AnimalComponent::IsInWanderBox (below)
//   0x140107400 is AnimalComponent::Place in this file.
// .rdata: 0x140172220 = 180.0f, spawn yaw scale; 0x140172110 = 0.4f, populate jitter.
// Deps of AnimalComponent::Place (0x140107400), by name:
//   0x140003f60 FindComponent<ModelComponent>
//   0x140003870 ToolTile_SetTransformFromYaw_3870    0x1400c2b70 ComponentEntity::SendEvent
//   0x1400c3470 ComponentEntity::RefreshEntityFlags  0x140107320 AnimalComponent::UpdateBehavior
// DynArray<InternalBasePtr> ops used by OnAdd/OnRemove: 0x140003110 was
// Collection::Array<void*>::SetSize(n, fill) monomorphised into the exe (ENDGAME B.3.2
// step 2) — converted at its call sites below; 0x140002cc0 is Array<T>::Remove
// (DynArray8_EraseByValue).
//
// LEFT (B.4 P2 COMP-45): 0x140003870 (ModelComponent's transform-from-yaw) is PLAYER's
// free body taking an integer receiver (wave X).

// Reimpl of FUN_140106b10: per-animal "has this animal reached its migration target
// tile?" test. Finds the CharacterComponent on the owning entity (0x1400012d0), reads
// its model's world position (the transform translation, ModelComponent +0x158/+0x15c),
// converts it to a clamped tile index, then returns true iff that tile falls inside
// the animal's wander box (+0x38..+0x3e).
bool AnimalComponent::IsInWanderBox() const {
    const Map* map = GameUpdateLoop::_singleton->m_game;

    CharacterComponent* character = _entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    const Math::Vector3& pos =
        character->modelComponent->transform.translation;

    int mapDim = map->size;
    int x = (int)(pos.x + (float)(mapDim >> 1));
    int y = (int)(pos.y + (float)(mapDim >> 1));
    if (x < 0) x = 0;
    if (mapDim <= x) x = mapDim - 1;
    if (y < 0) y = 0;
    if (mapDim <= y) y = mapDim - 1;

    // Original folds (x,y) into mapDim*y+x then re-splits it; net effect recovers
    // the same clamped x,y. Keep that for byte-faithfulness.
    int idx = mapDim * y + x;
    int ry = idx / mapDim;
    int rx = idx % mapDim;

    if (rx < wanderX || ry < wanderY ||
        (int)wanderWidth + (int)wanderX <= rx ||
        (int)wanderHeight + (int)wanderY <= ry) {
        return false;
    }
    return true;
}

// Reimpl of FUN_140053f20 (count fertile/food tiles in the herd's grazing cell)
// Sub-samples the `cell` macro-cell into a (gridDim x gridDim) block of map tiles
// (gridDim = map edge / cells per edge), counts tiles whose first int (food value)
// is > 0, and returns the fraction (countPositive / gridDim^2). The grid is
// 0xC-byte records.
float PopulationComponent::HerdCellFood(int cell) const {
    int cellDim = herdDim;
    int mapStride = habitatDim;
    int gridDim = mapStride / cellDim;

    int positive = 0;
    for (int64_t row = 0; row < gridDim; row++) {
        const Cell* pTile = cells._data +
            (((cell / cellDim) * gridDim + row) * (int64_t)mapStride +
             (int64_t)((cell % cellDim) * gridDim));
        for (int64_t col = gridDim; col != 0; col--) {
            positive += (pTile->density > 0) ? 1 : 0;
            pTile++;
        }
    }
    return (float)positive / (float)(gridDim * gridDim);
}

// ============================================================
// Reimpl of FUN_140107400: place one animal.
// ============================================================
// Called once per spawned animal from AdjustSize and Populate:
//   * find the ModelComponent on the animal's entity and write its world
//     transform (position + yaw) via 0x140003870
//   * record the pose flag at +0x40; when set, and the model's mesh group
//     has more than one mesh, swap the MeshInstance to mesh index 1 (the
//     alternate "grazing" pose) and re-register the model with the scene
//   * reset the graze timer at +0x50 to 0 and OR in capability bit 0x400000
//   * tail-call AnimalComponent::UpdateBehavior (0x140107320)
//
// Note the retail dirty-flag test `((new ^ old) & 0xffff) != 0` can never be
// true here (the only changed bit is 0x400000, above the mask), so the
// 0x1400c3470 re-register is dead on this path. Kept for fidelity.
void AnimalComponent::Place(const Math::Vector3* position, float yaw, bool pose) {
    ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60
    if (!model) return;   // the tree's guard (retail has none)

    model->SetTransformFromYaw(position, yaw);   // 0x140003870
    canGraze = pose;

    if (pose) {
        // Re-resolve the model exactly as retail does (a second find call).
        model = _entity->FindComponent<ModelComponent>();   // 0x140003f60

        Description* modelDescRaw = model->_description;
        ModelDescription* modelDesc = ::Object::IsDerivedFromNullSafe(modelDescRaw, ModelDescription::TypeId())
            ? static_cast<ModelDescription*>(modelDescRaw) : nullptr;
        const ModelMeshGroup* groups = modelDesc ? modelDesc->_meshGroups : nullptr;
        int meshCount = groups ? groups[model->meshGroup].meshCount : 0;

        if (meshCount > 1) {
            model = _entity->FindComponent<ModelComponent>();   // 0x140003f60

            if (model->meshIndex != 1) {
                // Detach from the scene while the mesh is swapped (slot 7),
                // then re-add (slot 6).
                if (model->meshIndex >= 0) {
                    model->OnRemove();   // slot 7
                }
                model->meshIndex = 1;

                Description* md2Raw = model->_description;
                ModelDescription* md2 = ::Object::IsDerivedFromNullSafe(md2Raw, ModelDescription::TypeId())
                    ? static_cast<ModelDescription*>(md2Raw) : nullptr;
                const ModelMeshGroup* groups2 = md2 ? md2->_meshGroups : nullptr;
                if (groups2) {
                    Graphics::GraphicsMesh** meshList = groups2[model->meshGroup].meshes;
                    Graphics::GraphicsMesh* mesh = meshList ? meshList[model->meshIndex] : nullptr;
                    model->meshInstance
                        .SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
                }

                model->OnAdded();   // slot 6
            }
            model->_entity->SendEvent(0);   // 0x1400c2b70
        }
    }

    System::Time zeroTime;
    RtSystem_Time_Ctor(&zeroTime, 0.0f);
    uint32_t oldFlags = _flags;
    grazeTimer = zeroTime;
    uint32_t newFlags = oldFlags | 0x400000u;
    _flags = newFlags;
    if (((newFlags ^ oldFlags) & 0xffff) != 0 && _entity != nullptr) {
        _entity->RefreshEntityFlags();   // 0x1400c3470
    }

    UpdateBehavior();   // 0x140107320
}

// Reimpl of FUN_140121980 (is the herd's migration complete?)
// For each member's entity, finds its AnimalComponent (0x1401c6f98) and asks
// IsInWanderBox (0x140106b10) whether that animal has reached its target.
// Returns 0 as soon as any animal is still en route; 1 if all are done.
char HerdComponent::CheckMigrationDone() {
    (void)::Object::IsDerivedFromNullSafe(_description, HerdDescription::TypeId());

    for (int i = 0; i < members._size; i++) {
        ComponentEntity* entity = members[i]->_entity.Get();
        AnimalComponent* animal = entity->FindComponent<AnimalComponent>();
        if (!animal->IsInWanderBox()) return 0;   // 0x140106b10
    }
    return 1;
}

// Reimpl of FUN_140121450 (grow the herd toward targetSize by spawning animals)
// While the member count (+0x40) < target: build an animal entity from the herd
// description's template, locate its components, pick a randomized spawn position
// around the herd centre tile (+0x4c), place the new animal (Place), link it to the
// herd (SetHerd), and register the entity with the world entity list (+0xe8).
// The spawn pose alternates each iteration.
void HerdComponent::AdjustSize(int targetSize) {
    Description* cfgRaw = _description;
    HerdDescription* cfg = ::Object::IsDerivedFromNullSafe(cfgRaw, HerdDescription::TypeId())
        ? static_cast<HerdDescription*>(cfgRaw) : nullptr;
    if (!cfg) return;   // the tree's guard (retail has none)
    ComponentDescription* descr = cfg->_animal;
    if (GameUpdateLoop::_singleton == nullptr) return;   // the tree's guard
    System::Random* rng = GameUpdateLoop::_singleton->m_random;
    Map* game = GameUpdateLoop::_singleton->m_game;
    const Map* map = game;
    bool altFlag = rng->Rand(2u) == 0;
    const float spread = kRdata_140172220;   // 0x140172220
    const float half = kRdata_140172114;     // 0x140172114

    while (members._size < targetSize) {
        // The loop's only exit is the member count at +0x40 growing, which
        // happens inside SetHerd -> AddMember. If that link is missing the
        // retail shape spins forever while allocating an entity per pass, so
        // bail on a pass that made no progress rather than hanging the world.
        const int countBefore = members._size;

        // Build the animal entity from the template.
        void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
        Object::InternalPtr<ComponentEntity> entity = mem ? new (mem) ComponentEntity(descr) : nullptr;   // 0x1400c3720

        // Retail looks up three components in this order and uses two of them:
        // the WildAnimalComponent is what gets linked back to the herd, and the
        // AnimalComponent is what gets placed. The CharacterComponent lookup is
        // performed and discarded — kept so the search order matches.
        WildAnimalComponent* wild = entity->FindComponent<WildAnimalComponent>();
        AnimalComponent* animal = entity->FindComponent<AnimalComponent>();
        (void)entity->FindComponent<CharacterComponent>();

        // Spread radius from the herd config.
        Description* cfg2Raw = _description;
        HerdDescription* cfg2 = ::Object::IsDerivedFromNullSafe(cfg2Raw, HerdDescription::TypeId())
            ? static_cast<HerdDescription*>(cfg2Raw) : nullptr;
        int animalArea = cfg2->_spread;
        int mapDim = map->size;
        float origin = (float)(mapDim >> 1) - half;

        Math::Vector3 centre((float)(tile % mapDim) - origin,
                             (float)(tile / mapDim) - origin, 0.0f);   // slot 0x140152a30

        Math::Vector3 worldPos;   // local_e8/e4/e0
        game->GetTerrainPos(&worldPos.x, &centre.x, 0, nullptr);   // 0x14009f3e0

        float rx = rng->RandFloatTol();
        float ry = rng->RandFloatTol();
        Math::Vector3 offset(ry * (float)animalArea, rx * (float)animalArea, 0.0f);   // slot 0x140152a30

        Math::Vector3 spawn(worldPos.x + offset.x,
                            worldPos.y + offset.y,
                            worldPos.z + offset.z);   // slot 0x140152a30

        float rz = rng->RandFloatTol();
        animal->Place(&spawn, rz * spread, altFlag);   // 0x140107400
        altFlag = !altFlag;

        (void)::Object::IsDerivedFromNullSafe(_description, HerdDescription::TypeId());
        HerdComponent* self = this;
        wild->SetHerd(&self);   // 0x14012e510

        // LEFT: GameUpdateLoop::m_entityList is void* (W0 header); the forwarder takes it.
        GameUpdateLoop::_singleton->m_entityList->RegisterEntity(&entity,
                                       descr->_category);   // 0x1400c48a0
        if (members._size <= countBefore) {
            ErrLine("[Herd] AdjustSize aborted: member count stuck at %d "
                    "(target %d) — the herd back-link never ran\n",
                    countBefore, targetSize);
            fflush(stderr);
            return;
        }
    }
}

// Reimpl of FUN_140121ad0: register a new animal into the herd. Append its IBP to
// the member list (+0x38, count +0x40), then compute a clamped wander box (from the
// herd centre tile +0x4c and the description's spread +0x38) and write it into the
// animal's AnimalComponent (+0x38..+0x3e, found through 0x140005500).
void HerdComponent::AddMember(WildAnimalComponent* const* member) {
    // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill) (kSpec_PtrZero).
    members.SetSizeFill(members._size + 1, member);

    Description* cfgRaw = _description;
    HerdDescription* cfg = ::Object::IsDerivedFromNullSafe(cfgRaw, HerdDescription::TypeId())
        ? static_cast<HerdDescription*>(cfgRaw) : nullptr;
    int spread = cfg->_spread;
    if (GameUpdateLoop::_singleton == nullptr) return;   // the tree's guard
    int mapDim = GameUpdateLoop::_singleton->m_game->size;

    int bx = tile % mapDim - (spread >> 1);
    int by = tile / mapDim - (spread >> 1);
    if (bx < 1) bx = 1;
    if (by < 1) by = 1;
    int bw = spread;
    int bh = spread;
    if (mapDim - 1 <= bx + spread) bw = (mapDim - bx) - 2;
    if (mapDim - 1 <= by + bw)     bh = (mapDim - by) - 2;

    AnimalComponent* animal = (*member)->_entity->FindComponent<AnimalComponent>();   // 0x140005500
    animal->wanderX = (int16_t)bx;
    animal->wanderY = (int16_t)by;
    animal->wanderWidth = (int16_t)bw;
    animal->wanderHeight = (int16_t)bh;
}

// Reimpl of FUN_14012e510: assign the animal's owning herd (+0x28). If it changes:
// detach the animal from the old herd's member list (FUN_140002cc0 on oldHerd+0x38),
// store the new herd, and if non-null attach it (HerdComponent::AddMember).
void WildAnimalComponent::SetHerd(HerdComponent* const* herd) {
    HerdComponent* current = this->herd.Get();
    if (current == *herd) return;

    if (current != nullptr) {
        WildAnimalComponent* self = this;
        current->members.Remove(self);   // 0x140002cc0
    }

    this->herd = *herd;
    if (*herd != nullptr) {
        WildAnimalComponent* self = this;
        (*herd)->AddMember(&self);   // 0x140121ad0
    }
}

// ============================================================
// Reimpl of FUN_140120f50 — HerdComponent::Populate(count).
// ============================================================
// Ghidra names this PopulationComponent_helper_0f50, but param_1 is the
// HerdComponent: it reads the herd's description through +0x18 against
// HerdDescription (0x1401c7858) and writes the herd's own +0x4c/+0x50/+0x48.
//
// This is the herd's initial seeding pass, called once per herd from
// PopulationComponent's Startup. It picks the grazing cell (StartMigration),
// derives the clamped wander box the same way AddMember does, then spawns
// `count` animals at random tiles inside that box, and finally arms the graze
// timer and drops the herd into the grazing phase.
//
// Unlike AdjustSize (which scatters around the herd centre with a radius from
// the description's spread), each animal here gets its own random tile in the box
// plus a sub-tile jitter of DAT_140172110 (0.4f).
void HerdComponent::Populate(int count) {
    Description* herdDescRaw = _description;
    HerdDescription* herdDesc = ::Object::IsDerivedFromNullSafe(herdDescRaw, HerdDescription::TypeId())
        ? static_cast<HerdDescription*>(herdDescRaw) : nullptr;

    StartMigration();   // 0x140122340

    if (GameUpdateLoop::_singleton == nullptr || !herdDesc) return;   // the tree's guards
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* game = gw->m_game;
    const Map* map = game;
    System::Random* rng = gw->m_random;

    // StartMigration picks the grazing tile into +0x4c, or leaves -1 when the
    // herd's cell has no habitable ground at all (a water/mountain cell — a
    // normal outcome, and also what an unregistered StartMigration looks like).
    // Retail never guards this because its StartMigration always runs; without
    // the check every tile computed below would be negative.
    int mapDim = map ? map->size : 0;
    if (mapDim <= 0 || tile < 0) {
        return;
    }

    // Clamped wander box around the herd centre tile (+0x4c).
    int spread = herdDesc->_spread;
    int bx = tile % mapDim - (spread >> 1);
    int by = tile / mapDim - (spread >> 1);
    if (bx < 1) bx = 1;
    if (by < 1) by = 1;
    int bw = spread;
    int bh = spread;
    if (mapDim - 1 <= bx + spread) bw = (mapDim - bx) - 2;
    if (mapDim - 1 <= by + bw)     bh = (mapDim - by) - 2;

    // Retail feeds bw/bh straight to Random::Rand. A herd whose centre lands
    // within two tiles of the map edge makes them <= 0, and Rand's modulo would
    // fault, so floor them at one tile.
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    bool altPose = rng->Rand(2u) == 0;
    const float scatter = kRdata_140172110;    // 0x140172110
    const float yawScale = kRdata_140172220;   // 0x140172220
    const float half = kRdata_140172114;       // 0x140172114
    ComponentDescription* descr = herdDesc->_animal;
    if (!descr) {
        // Skip the spawn loop but still arm the graze timer below, exactly as a
        // count of zero would.
        ErrLine("[Herd] Populate: herd=%p has no animal template "
                "(HerdDescription+0x20 is null) — no deer will spawn\n", static_cast<void*>(this));
        fflush(stderr);
        count = 0;
    }

    int spawned = 0;
    for (int n = 0; n < count; n++) {
        int rndX = rng->Rand(static_cast<uint32_t>(bw));
        int rndY = rng->Rand(static_cast<uint32_t>(bh));

        void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
        Object::InternalPtr<ComponentEntity> entity = mem ? new (mem) ComponentEntity(descr) : nullptr;   // 0x1400c3720
        if (!entity) continue;

        AnimalComponent* animal = entity->FindComponent<AnimalComponent>();
        WildAnimalComponent* wild = entity->FindComponent<WildAnimalComponent>();

        mapDim = map->size;
        int spawnTile = mapDim * (by + rndY) + bx + rndX;
        float origin = (float)(mapDim >> 1) - half;

        Math::Vector3 cell((float)(spawnTile % mapDim) - origin,
                           (float)(spawnTile / mapDim) - origin, 0.0f);   // slot 0x140152a30

        Math::Vector3 worldPos;
        game->GetTerrainPos(&worldPos.x, &cell.x, 0, nullptr);   // 0x14009f3e0

        float j1 = rng->RandFloatTol();
        float j2 = rng->RandFloatTol();
        Math::Vector3 offset(j2 * scatter, j1 * scatter, 0.0f);   // slot 0x140152a30

        Math::Vector3 spawn(worldPos.x + offset.x,
                            worldPos.y + offset.y,
                            worldPos.z + offset.z);   // slot 0x140152a30

        float yaw = rng->RandFloatTol();
        animal->Place(&spawn, yaw * yawScale, altPose);   // 0x140107400
        altPose = !altPose;

        // Randomize the animal's starting age from AnimalDescription+0x28.
        if (animal) {
            Description* animalDescRaw = animal->_description;
            AnimalDescription* animalDesc = ::Object::IsDerivedFromNullSafe(animalDescRaw, AnimalDescription::TypeId())
                ? static_cast<AnimalDescription*>(animalDescRaw) : nullptr;
            float r = rng->RandFloat();
            if (animalDesc)
                animal->age = r * animalDesc->_f28;
            animal->UpdateBehavior();   // 0x140107320
        }

        HerdComponent* self = this;
        wild->SetHerd(&self);   // 0x14012e510

        // LEFT: GameUpdateLoop::m_entityList is void* (W0 header); the forwarder takes it.
        gw->m_entityList->RegisterEntity(&entity, descr->_category);   // 0x1400c48a0
        spawned++;
    }

    // Arm the graze timer and enter the grazing phase.
    float r = gw->m_random->RandFloat();
    System::Time grazeTicks;
    RtSystem_Time_Ctor(&grazeTicks,
        r * (herdDesc->_grazeRandomMinutes + herdDesc->_grazeMinutes) * kRdata_140172234);
    grazeTimer = grazeTicks;
    migrating = false;
}

// ============================================================
// Reimpl of FUN_140121e20 — HerdComponent::OnRemove (vtable slot 7).
// Detaches every member (the inlined SetHerd(member, null) — erasing the
// member from this herd's own +0x38 list is what drains the +0x40 count and
// terminates the loop), then removes the herd from the world herd list.
// ============================================================
void HerdComponent::OnRemove() {
    // The two `break`s below are the tree's guards (retail has neither: a member with
    // no herd would spin, a null member would fault).
    while (members._size > 0) {
        WildAnimalComponent* member = members[members._size - 1].Get();
        if (!member) break;

        HerdComponent* current = member->herd.Get();
        if (current == nullptr) break;  // no progress possible

        WildAnimalComponent* memberRef = member;
        current->members.Remove(memberRef);   // 0x140002cc0
        member->herd = nullptr;
    }

    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (!townMgr) return;
    HerdComponent* selfRef = this;
    townMgr->herds.Remove(selfRef);   // 0x140002cc0
}

// ============================================================
// Helper: is the herd's centre tile habitable? (inlined twice into Update)
// Returns true if the population grid's cell holding the tile has a nonzero
// first int.
// ============================================================
static bool IsTileValid(HerdComponent* self) {
    PopulationComponent* population = self->population.Get();
    if (population == nullptr) return false;

    int mapWidth = GameUpdateLoop::_singleton->m_game->size;

    int tileIdx = self->tile;

    // Signed division for row/col
    int64_t quotient = static_cast<int64_t>(tileIdx) / static_cast<int64_t>(mapWidth);
    int64_t remainder = static_cast<int64_t>(tileIdx) % static_cast<int64_t>(mapWidth);

    int cellSize = population->description->_habitatBlock;
    int stride = population->habitatDim;

    // Compute grid row/col (matching decompiler's division pattern)
    int row = static_cast<int>(
        static_cast<int64_t>(
            (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int>(quotient) >> 31)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(quotient))
        ) / static_cast<int64_t>(cellSize));
    int col = static_cast<int>(
        static_cast<int64_t>(
            (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int>(remainder) >> 31)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(remainder))
        ) / static_cast<int64_t>(cellSize));

    // The habitability cell's density (0: uninhabitable).
    int64_t cellIndex = static_cast<int64_t>(row) * static_cast<int64_t>(stride) +
                        static_cast<int64_t>(col);
    int terrainType = population->cells[cellIndex].density;

    return terrainType != 0;
}

// ===== ENDGAME B.3.8 CIT — HerdComponent as a REAL class (HerdComponent.h) =====
// Before: a 48-slot hand-built HerdComponent_LiveVtable filled at every create
// (slots 3 Serialize, 4 Startup, 6 OnAdd, 9 Update), Inert_ScalarDtor in slot 0.
// After: the compiler's 16-slot vtable and a real ctor/dtor chain (B38_RULES D1,
// D2). A live herd does nothing on its own — PopulationComponent's Startup is
// what creates herd entities and calls Populate.
#include "Object/factory_registration.h"


// 0x140122150. Component() is the CALL 0x1401188b0; the 0x140163680 stamp is
// the compiler's; +0x28, +0x38, +0x40, +0x48 (byte), +0x50, +0x58 = 0; flags |=
// 0x0b (0x8b). Retail leaves +0x30 unwritten (zeroed here, as the tree did).
// +0x4c = -1 is the ctor's own store (0x14012218c). The tree used to write it from
// the Startup slot (HerdComponent_Startup) because the inert factory could not
// express it; retail's slot 4 is Component's `RET`, which the class now inherits.
HerdComponent::HerdComponent()
    : population(nullptr), populationCell(0), _pad34{}, migrating(false), _pad49{}, tile(-1), grazeTimer{0}, spawnTimer{0} {
    OrFlagsInCtor(0x0b);
}

// [6] 0x140121f40. Appends this herd to the town manager's herd list.
void HerdComponent::OnAdded() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (!townMgr) return;

    HerdComponent* selfRef = this;
    townMgr->herds.SetSizeFill(townMgr->herds._size + 1, &selfRef);
}

// [7] 0x140121e20: HerdComponent::OnRemove above.

// [9] 0x140121bf0. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af).
// HerdComponent::Update's diagnostic counters (main thread only: the update walk),
// at namespace scope rather than function-local statics (B6-5).
static long long s_herdUpdCalls = 0, s_herdUpdNoPop = 0, s_herdUpdNoCfg = 0, s_herdUpdFired = 0;

void HerdComponent::Update(const System::Time* time) {
    const System::Time* deltaTime = time;

    // Every branch below reaches the owning PopulationComponent through +0x28
    // (food sampling, tile validity, migration retarget), which retail derefs
    // unconditionally. A herd whose Populate never ran has +0x28 == 0, so bail
    // rather than fault; the herd simply stays idle.
    // Update-entry census. The [Herd] census line below only prints once the
    // respawn timer fires, so its absence cannot distinguish "Update never ran"
    // from "ran but the timer never expired" from "bailed on a null +0x28".
    auto& g_hu_calls = s_herdUpdCalls; auto& g_hu_noPop = s_herdUpdNoPop;
    auto& g_hu_noCfg = s_herdUpdNoCfg; auto& g_hu_fired = s_herdUpdFired;
    ++g_hu_calls;
    if (population == nullptr) {
        ++g_hu_noPop;
        if ((g_hu_noPop % 2000) == 1) {
            ErrLine("[HerdUpd] calls=%lld noPop=%lld noCfg=%lld fired=%lld\n",
                    g_hu_calls, g_hu_noPop, g_hu_noCfg, g_hu_fired);
            fflush(stderr);
        }
        return;
    }

    // Get config object via IsDerivedFrom check
    Description* configRaw = _description;
    HerdDescription* config = ::Object::IsDerivedFromNullSafe(configRaw, HerdDescription::TypeId())
        ? static_cast<HerdDescription*>(configRaw) : nullptr;
    if (!config) ++g_hu_noCfg;

    // === Timer tick: accumulate time at +0x58 ===
    spawnTimer._ticks += deltaTime->_ticks;

    // Compute interval from config+0x3C (in game minutes, convert to ticks)
    float intervalMinutes = config->_growMinutes;
    System::Time interval;
    RtSystem_Time_Ctor(&interval, intervalMinutes * kRdata_140172234);
    int64_t intervalTicks = interval._ticks;

    // One line per 100000 updates (it was one per 25: 129 million updates in a 2-hour town wrote a 1 GB standalone_error.log).
    // The FIRED lines below are rare and stay.
    if ((g_hu_calls % 100000) == 1) {
        ErrLine("[HerdUpd] calls=%lld noPop=%lld noCfg=%lld fired=%lld | "
                "herd=%p members=%d timer=%lld interval=%lld cfgMin=%.3f\n",
                g_hu_calls, g_hu_noPop, g_hu_noCfg, g_hu_fired, static_cast<void*>(this),
                members._size,
                (long long)spawnTimer._ticks, (long long)intervalTicks, intervalMinutes);
        fflush(stderr);
    }

    if (intervalTicks < spawnTimer._ticks) {
        ++g_hu_fired;
        ErrLine("[HerdUpd] FIRED #%lld herd=%p members=%d (calls=%lld)\n",
                g_hu_fired, static_cast<void*>(this),
                members._size, g_hu_calls);
        fflush(stderr);
        // Timer fired -- subtract interval
        spawnTimer._ticks -= intervalTicks;

        // Check food availability
        float foodScore = population->HerdCellFood(populationCell);   // 0x140053f20

        // Compute food modifier: if food < threshold, scale it
        float foodModifier = kRdata_14017214c;
        if (foodScore < kRdata_140172114) {
            foodModifier = foodScore * kRdata_140172174;
        }

        // Target size: current * 2 (minimum 2)
        int currentTarget = members._size * 2;
        if (currentTarget == 0) {
            currentTarget = 2;
        }

        // Max size from config+0x28, scaled by food modifier
        int maxSize = static_cast<int>(
            static_cast<float>(config->_maxSize) *
            foodModifier + kRdata_140172114);
        if (maxSize < currentTarget) {
            currentTarget = maxSize;
        }

        AdjustSize(currentTarget);   // 0x140121450
    }

    // === Phase logic ===
    if (!migrating) {
        // Grazing phase: countdown timer at +0x50
        grazeTimer._ticks -= deltaTime->_ticks;

        if (grazeTimer._ticks > 0) {
            // Timer still running -- check terrain validity
            if (IsTileValid(this)) {
                return;  // Valid terrain, stay grazing
            }
        }

        // Timer expired or invalid terrain -- start migration
        StartMigration();   // 0x140122340
        grazeTimer._ticks = 0;
        migrating = true;  // Switch to migrating phase
    } else {
        // Migrating phase: check if done
        char done = CheckMigrationDone();   // 0x140121980

        if (done != 0) {
            // Migration complete -- randomize graze timer and switch back
            float randVal = GameUpdateLoop::_singleton->m_random->RandFloat();
            float baseTime = config->_grazeMinutes;
            float randRange = config->_grazeRandomMinutes;
            float grazeSeconds = (randVal * randRange + baseTime) * kRdata_140172234;

            System::Time grazeTicks;
            RtSystem_Time_Ctor(&grazeTicks, grazeSeconds);

            migrating = false;  // Back to grazing
            grazeTimer = grazeTicks;
            return;
        }

        // Still migrating -- check terrain validity at current position
        if (!IsTileValid(this)) {
            // Tile became invalid -- restart migration
            StartMigration();   // 0x140122340
        }
    }
}

// create 0x140034cd0: MemAlloc(0x60, 8), then the ctor.
::Object::Base* HerdComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HerdComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HerdComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* HerdComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HerdComponent());
}

DEFINE_EXE_TYPE(HerdComponent, "Component")
