// src/game/Action/huntanimal.cpp
// Clean C++ reimplementation of HuntAnimal action methods.
// Source: Game/Action/HuntAnimal.cpp (from profile build debug strings)
//
// HuntAnimal -- 0x40 bytes. Handles hunting behavior: finding nearby animal,
// spawning a corpse entity, starting herd migration.
// Inherits: Object::Internal -> Action (0x20) -> HuntAnimal
// Factory: 0x140022700, Vtable: 0x14015fde0
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = 0x140022770
//   [1] +0x08 VectorDtor    = 0x1400226f0 (returns type ptr, unhookable)
//   [2] +0x10 BaseDtor      = 0x1400226e0 (returns TypeInfo, unhookable)
//   [3] +0x18 Serialize     = 0x1400c8260
//   [4] +0x20 AbstractImpl  = 0x1400c8250 (9 bytes, unhookable)
//   [5] +0x28 Execute       = 0x1400c8240 (9 bytes, unhookable)
//   [6] +0x30 Enter         = 0x1400c7de0 (~250 instructions, grid math + entity queries)
//   [7] +0x38 Exit          = 0x1400c4ba0 (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400c4a30 (base Action)
//   [9] +0x48 GetTarget     = 0x1400c7d60
//
// Field layout (0x40 bytes):
//   +0x00-0x1F: Action base (vtable, Internal base, entity ptr, status byte)
//   +0x20: void*    animalEntity   (InternalBasePtr -- the animal being hunted)
//   +0x28: void*    herdEntity     (InternalBasePtr -- the herd the animal belongs to)
//   +0x30: void*    corpseEntity   (InternalBasePtr -- unused/zeroed field)
//   +0x38: uint8_t  huntStarted    (flag: 0=not started, 1=hunt completed)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "HuntAnimal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // HuntIsReady, QueryNearestCharacter, RemoveAnimalActions, HerdStartMigration
#include "Game/Map.h"
#include "Component/AnimalComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterGrid.h"     // CharacterGrid::QueryNearestCharacter
#include "Component/ModelComponent.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Work/exe_bodies.h"     // WorkGrid_FindCell_9ccf0, WorkBase_ValidateCell_a00f0
#include "Game/exe_bodies.h"     // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"  // kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "math/Math.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/HuntComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/AnimalDescription.h"
#include "Component/WildAnimalComponent.h"
#include "Component/HerdComponent.h"   // B4 S4 RT2: InternalPtr<HerdComponent>::Get() needs the class complete

// ============================================================
// Helpers, called by name (B3R)
//   0x140122a80 ActHunt_IsAnimalReady_2a80 (HuntAnimal_helper_2a80) -- checks if animal has
//               been held long enough: entity's time at +0x50 >= description's holdTime * 300.0
//   0x140092260 Pf_SpatialProximityQuery_92260 -- spatial entity query (finds nearby entity
//               matching criteria): gridPtr, outResult, position, radius, typeId, outIBP
//   0x140005500 ActHunt_FindComponent_5500 -- get model component from entity
//   0x14009ccf0 WorkGrid_FindCell_9ccf0 -- find path cost between two grid cells
//               (mapPtr, fromCell, toCell, flags 0xe1f)
//   0x1400a00f0 WorkBase_ValidateCell_a00f0 -- check path validity (mapPtr, flags 0xe1f, cell,
//               destCell)
//   0x14009f3e0 Reimpl_MapGetTerrainPos (CharacterComponent_impl_f3e0) -- grid pos to world pos
//   0x1400bbea0 Pf_RemoveAnimalCtor_bbea0 (RemoveAnimal_Action_Ctor) -- creates the corpse /
//               removal action: herdIBP, animIndex(-1), worldPos, fromCell, corpseIBP, descPtr, flags
//   0x1400227e0 ActHunt_FindComponent_27e0 -- finds HerdComponent from entity's component list
//   0x140122340 Pf_HerdStartMigration_122340 -- triggers herd migration after kill
//
// ENDGAME B.3.9 CB: the two "TypeInfo for slots 1/2" reads that sat here are gone.
// Retail's only readers of HuntAnimal's record are its own slots -- the type-id cell
// 0x1401c5f68 is read by HuntAnimal_Slot2 (0x1400226e0) and the NAME field +8
// (0x1401c5f70) by HuntAnimal_Slot1 (0x1400226f0) -- and DECLARE_EXE_TYPE answers
// both slots (HuntAnimal::TypeId() / TypeName()).
//
// Float constants (Game/exe_constants.h): 0x1401721a8 = 8.0f (search radius),
// 0x140172114 = 0.5f (half-cell offset).
// ============================================================

// ============================================================
// Slot 3: Serialize (0x1400c8260)
// Serializes: status(+0x18), animal entity(+0x20) with type check,
//             herd entity(+0x28) with type check, huntStarted flag(+0x38).
// ============================================================
void HuntAnimal::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs and hunt flag
    IO::Serializer ser(stream, 0);

    // The HuntComponent (+0x20)
    _field20.InternalBasePtr::Serialize(ser);
    if (!_field20.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_field20, HuntComponent::TypeId())) {
            _field20 = nullptr;
        }
    }

    // The hunting citizen (+0x28)
    _field28.InternalBasePtr::Serialize(ser);
    if (!_field28.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_field28, CitizenComponent::TypeId())) {
            _field28 = nullptr;
        }
    }

    // Serialize hunt started flag (+0x38)
    ser.Serialize(_field38, nullptr);
}

// ============================================================
// Slot 4: AbstractImpl (0x1400c8250) -- unhookable (9 bytes)
// Returns 1 if hunt not started (keep running), 0 if hunt completed.
// ============================================================
int  HuntAnimal::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    return (!_field38) ? 1 : 0;
}

// ============================================================
// Slot 5: Execute (0x1400c8240) -- unhookable (9 bytes)
// Same logic as AbstractImpl: returns 1 if hunt not started, 0 if completed.
// ============================================================
int  HuntAnimal::Execute(ActionStack* actionStack, const System::Time* time) {
    (void)actionStack;
    (void)time;
    return (!_field38) ? 1 : 0;
}

// ============================================================
// Slot 6: Enter (0x1400c7de0) -- complex hunting logic
//
// Algorithm:
// 1. If huntStarted is already set, return early
// 2. Check if the HuntComponent at +0x20 is ready (held long enough)
// 3. The hunter's position: +0x28 citizen -> character (+0x278) -> modelComponent (+0x68)
//    -> transform.translation (+0x158)
// 4. Query the CharacterGrid for the nearest WildAnimalComponent character
// 5. Verify the found animal's AnimalComponent doesn't have bit 20 set (not in use)
// 6. Calculate grid cell positions for path finding
// 7. Check path cost and validity between hunter and animal
// 8. If path is valid:
//    a. The AnimalComponent's description (an AnimalDescription)
//    b. Convert grid position to world position
//    c. Build the removal actions (0x1400bbea0)
//    d. Find the WildAnimalComponent and start its herd's migration

void HuntAnimal::Enter(ActionStack*, const System::Time*) {

    // If hunt already started, return early
    if (_field38) {
        return;
    }

    // Check if the hunt component at +0x20 is ready
    if (!_field20.Get()->IsReady()) {   // 0x140122a80
        return;
    }

    // Set up search radius
    Math::Scalar searchRadius(kRdata_1401721a8);  // 8.0f

    // Initialize the excluded character (an InternalBasePtr in retail) and the result
    CharacterComponent* queryOutIBP = nullptr;

    // Get the search type ID
    int searchTypeId = static_cast<int>(WildAnimalComponent::TypeId());

    // The hunter's position
    const Math::Vector3& hunterPos = _field28->character->modelComponent->transform.translation;
    float searchPos[3];
    searchPos[0] = hunterPos.x;
    searchPos[1] = hunterPos.y;
    searchPos[2] = hunterPos.z;

    // Spatial query: find the nearest wild animal
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    CharacterComponent* queryResult = nullptr;

    gameWorld->m_characterGrid->QueryNearestCharacter(&queryResult,   // 0x140092260
                          searchPos, &searchRadius, searchTypeId, &queryOutIBP);

    // If no result found, bail
    if (queryResult == nullptr) {
        return;
    }

    // Check if the found animal's AnimalComponent has flag bit 20 set (in use)
    AnimalComponent* animal1 = queryResult->_entity->FindComponent<AnimalComponent>();   // 0x140005500
    uint32_t modelFlags = animal1->_flags;

    if ((modelFlags >> 20) & 1) {
        // Entity already in use, bail
        return;
    }

    // The AnimalComponent again (second call for later use)
    AnimalComponent* animal2 = queryResult->_entity->FindComponent<AnimalComponent>();   // 0x140005500

    // The map, for grid calculations
    Map* map = gameWorld->m_game;

    // The animal's position for grid cell calculation
    const Math::Vector3& foundPos = queryResult->modelComponent->transform.translation;
    float foundPosX = foundPos.x;
    float foundPosY = foundPos.y;

    // The hunter's position again for its grid cell
    const Math::Vector3& hunterPos2 = _field28->character->modelComponent->transform.translation;

    int mapSize = map->size;
    float herdPosX2 = hunterPos2.x;
    float herdPosY2 = hunterPos2.y;

    // Convert world positions to grid cell indices
    float halfSize = (float)(mapSize >> 1);

    // Hunter position -> grid cell (toCell)
    int herdCellX = (int)(herdPosX2 + halfSize);
    int herdCellY = (int)(herdPosY2 + halfSize);
    if (herdCellX < 0) herdCellX = 0;
    if (herdCellX >= mapSize) herdCellX = mapSize - 1;
    if (herdCellY < 0) herdCellY = 0;
    if (herdCellY >= mapSize) herdCellY = mapSize - 1;
    int toCell = mapSize * herdCellY + herdCellX;

    // Animal position -> grid cell (fromCell)
    int foundCellX = (int)(foundPosX + halfSize);
    int foundCellY = (int)(foundPosY + halfSize);
    if (foundCellX < 0) foundCellX = 0;
    if (foundCellX >= mapSize) foundCellX = mapSize - 1;
    if (foundCellY < 0) foundCellY = 0;
    if (foundCellY >= mapSize) foundCellY = mapSize - 1;
    int fromCell = mapSize * foundCellY + foundCellX;

    // LEFT: 0x14009ccf0 / 0x1400a00f0 are declared in Work/exe_bodies.h (WORK) taking int*.
    Map* mapGrid = map;

    // Check path cost between the two cells
    uint32_t pathCost = (uint32_t)mapGrid->FindCell(fromCell, toCell, 0xe1f);   // 0x14009ccf0
    if ((int)pathCost < 0) {
        // No valid path, bail
        return;
    }

    // Check path validity
    uint64_t pathValid = mapGrid->ValidateCell(0xe1f, pathCost, toCell);   // 0x1400a00f0
    if ((char)pathValid == 0) {
        // Path not valid, bail
        return;
    }

    // Path is valid -- proceed with corpse spawning

    // The animal's description, when it is an AnimalDescription
    Description* descObj = animal2->_description;
    bool isModelDesc = ::Object::IsDerivedFromNullSafe(descObj, AnimalDescription::TypeId());
    const AnimalDescription* modelDesc = isModelDesc ? static_cast<const AnimalDescription*>(descObj) : nullptr;

    // The animal reference for the removal actions
    Component* corpseModelIBP = animal2;

    // Convert grid cell to world position
    float worldPos[3];
    float halfFloat = (float)(mapSize >> 1) - kRdata_140172114;
    int cellIdx = (int)pathCost;
    // Math::Vector3 ctor (was the 0x140152a30 slot): x = col, y = row, z = 0
    Math::Vector3 gridPos((float)(cellIdx % mapSize) - halfFloat,
                          (float)(cellIdx / mapSize) - halfFloat,
                          0.0f);

    map->GetTerrainPos(worldPos, &gridPos.x, 0, nullptr);   // 0x14009f3e0 reads x, y

    // Build the removal actions on the hunter: citizen, task -1, worldPos, fromCell, the
    // animal, its description, gate 0
    CitizenComponent* herdIBP_local = _field28.Get();

    RemoveAnimalActions(&herdIBP_local, (uint32_t)-1, worldPos, fromCell,   // 0x1400bbea0
                        &corpseModelIBP, modelDesc, 0);

    // The animal's WildAnimalComponent; start its herd's migration
    WildAnimalComponent* wild = queryResult->_entity->FindComponent<WildAnimalComponent>();   // 0x1400227e0

    wild->herd.Get()->StartMigration();   // 0x140122340

    // Clear the hunt component's progress and set hunt started flag
    _field20->huntProgress._ticks = 0;

    _field38 = true;
}

// ============================================================
// Slot 9: GetTarget (0x1400c7d60)
// Returns true if param matches the hunter's entity (+0x28 -> +0x10)
// or the hunt component's entity (+0x20 -> +0x10).
// ============================================================
bool HuntAnimal::GetTarget(ComponentEntity** param) {
    void* target = *param;

    if (_field28->_entity.Get() == target) {
        return true;
    }

    if (_field20->_entity.Get() == target) {
        return true;
    }

    return false;
}

// ============================================================
// class HuntAnimal (HuntAnimal.h) — ENDGAME B.3.8 ACT.
// The hand-built g_HuntAnimal_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022700 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
HuntAnimal::HuntAnimal()
    : _field20(nullptr),
      _field28(nullptr),
      _field30(nullptr),
      _field38(false),
      _pad39{} {}

// [3] 0x1400c8260
// [4] 0x1400c8250
// [5] 0x1400c8240
// [6] 0x1400c7de0
// [9] 0x1400c7d60

::Object::Base* HuntAnimal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HuntAnimal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HuntAnimal());
}
::Object::Base* HuntAnimal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HuntAnimal());
}


// ============================================================
// FUN_1400c8400 — HuntAnimal_Action_CopyCtor
//   RCX=self, RDX=p2, R8=p3.  +0x20 = *p3 ; +0x28 = *p2 ; +0x30 = 0 ; +0x38 = 0(byte).
// ============================================================
// 0x1400c8400: +0x20 = *p3 ; +0x28 = *p2 ; +0x30 = 0 ; +0x38 = 0 (byte).
HuntAnimal::HuntAnimal(CitizenComponent* const* hunter, HuntComponent* const* hunt)
    : _field20(*hunt),
      _field28(*hunter),
      _field30(nullptr),
      _field38(false) {}

DEFINE_EXE_TYPE(HuntAnimal, "Action")
