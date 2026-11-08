// src/game/Work/herbalistwork.cpp
// Clean C++ reimplementation of HerbalistWork — all methods.
// Source: Game/Work/HerbalistWork.cpp (from profile build debug strings)
//
// HerbalistWork (0x60 bytes) — Herbalist gathering work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> HerbalistWork
// Vtable: 0x1401615a0
// Same layout as ForesterWork (different vtable, different work logic).
//
// Key difference from ForesterWork: no plant/cut branching. Herbalist only
// searches for existing natural resources (herbs) that match the description
// list, checks growth threshold, then creates a PerformWork action to harvest.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/HerbalistWork.h"
#include "Work/work_rules.h"   // review F-199..F-206: the shared Execute decisions
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Tutorial/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "math/Math.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/HerbalistComponent.h"    // ENDGAME B.3.9 D5
#include "Component/HerbalistDescription.h"  // ENDGAME B.3.9 D5
#include "Component/RadiusDescription.h"     // ENDGAME B.3.9 D5
#include "Component/NaturalResourceComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x1400e3db0  Work::TryAutoPickup
//   0x140003050  WorkBase_FindComponent_3050
//   0x140002f90  WorkBase_FindComponent_2f90
//   0x14012c3f0  CitizenComp_FUN_14012c3f0
//   0x14012e5c0 WorkComponent::CalcMaxWorkers
//   0x140132010  WorkImpl_Sqrtf_132010
//   0x140087fb0  Pf_EntityAnchorCell_7fb0
//   0x140008830  WorkBase_CalcDistance_8830
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x1400052c0  WorkBase_FindComponent_52c0
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x1400c1eb0  WorkAct_PerformWorkCtor_c1eb0
//   0x1400c2490  WorkAct_PlayAnimationCtor_c2490
//   0x140021a30  WorkBST_RegisterWork_1a30
//   0x140115e60  CitizenAI_State_LongTermPlan
//   0x140006270  WorkBase_FindComponent_6270
//   0x14012cf10  StorageComponent::QueryCount
//   0x1400296b0  WorkBST_BST_Serialize_296b0
//   0x1400f7100  Work::ValidateTask
//   0x14001a6e0  WorkBST_BSTRemove_a6e0
//   0x140025520  WorkBST_BSTFindRemove_25520
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  AttendanceComponent_GetJobHostComponent
//   0x140014c30  WorkBase_TreeNodeFree_14c30
//   0x140172184  kRdata_140172184 (.rdata)
//   0x140172114  kRdata_140172114 (.rdata)
//   0x14017214c  kRdata_14017214c (.rdata)
//
// Vtable pointers for dtor chain
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: HerbalistWork_Ctor
// disassembles to `0x14004b1a9: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// The AutoPickupWork vtable it then overwrites with is 0x140160fa0 (the next LEA
// in the same ctor); only after that comes this class's own vtable.
// These were previously 0x1401612e0 / 0x140161340, neither of which is a vtable:
// both are INTERIOR addresses of the FireWork (0x1401612a0) and FishingWork
// (0x140161320) vtables, and Ghidra reports ZERO xrefs to either.


// ============================================================
// Helper: the RadiusDescription among a ComponentDescription's sub-descriptions (+0x28
// data / +0x30 count) -- the inlined scan retail repeats (no CheckPtr).
// ============================================================
static RadiusDescription* FindSubComponentInList(ComponentDescription* subObj) {
    const uint32_t typeId = RadiusDescription::TypeId();
    int count = subObj->_subDescCount;
    for (int i = 0; i < count; i++) {
        Description* obj = subObj->_subDescs[i];
        if (obj->Type() == typeId) {
            Description* comp = subObj->_subDescs[i];
            if (Object::IsDerivedFromNullSafe(comp, typeId)) {
                return static_cast<RadiusDescription*>(comp);
            }
            return nullptr;
        }
    }
    return nullptr;
}


// ============================================================
// Helper: Assign work and register with AI
// ============================================================
static void AssignWorkToEntity(HerbalistWork* self, CitizenComponent** citizenIBP, int priority) {
    CitizenComponent* citizen = *citizenIBP;

    // Call vtable slot +0x50 (GetTarget) on self
    Profession* workEntity = nullptr;
    self->GetTarget(&workEntity);

    // Set citizen flags
    citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
    citizen->lastErrand = CitizenErrand::None;
    citizen->lastWorkId = priority;

    // If the work's profession matches the citizen's (+0x1f0) and priority >= 0,
    // set the "owned work" flag
    if (workEntity == citizen->profession && priority >= 0) {
        citizen->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
    }

    // Register work with citizen AI subsystem at self+0x48 (the tree helper takes the
    // container as uintptr_t*: LEFT)
    WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEntity's)
    WorkBST_RegisterWork_1a30(&self->_citizens, &inserted, citizenIBP);
}


// ============================================================
// HerbalistWork::Slot4 — Main work-assignment logic
// ============================================================
//
// Algorithm (differs from ForesterWork — no plant/cut branching):
// 1. Check if citizen already has auto-pickup assignments
// 2. Find WorkComponent, HerbalistComponent / HerbalistDescription, RadiusDescription
// 3. Check if material count has reached capacity (return 1 = done if so)
// 4. Calculate search radius from workplace radius
// 5. Random iteration over grid cells in radius
// 6. For each valid cell:
//    a. Skip if terrain is forest/resource terrain (0x1e0 mask)
//    b. Check distance within 2x radius
//    c. If cell has a resource entity, find NatRes component
//    d. For each herb type in the description list:
//       - Check growth >= threshold
//       - Check resource description matches
//       - Check not flagged for removal
//       - Find nearest passable cell, check pathfinding
//       - Create PerformWork action to harvest
// 7. If no work found, handle failure (idle animation or replan)
//
int HerbalistWork::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // Save citizen ref for later use
    CitizenComponent* citizenRef = *citizenIBP;

    // -------------------------------------------------------
    // Step 1: Check auto-pickup assignments (0x1400e3db0,
    // Work::TryAutoPickup)
    // -------------------------------------------------------
    int checkResult = TryAutoPickup(&citizenRef);
    if (checkResult == 2) {
        return 2;
    }

    // -------------------------------------------------------
    // Step 2: Find components on the workplace entity
    // -------------------------------------------------------

    // The WorkComponent of the workplace entity (self+0x10) (0x140003050)
    WorkComponent* matDesc = _entity->FindComponent<WorkComponent>();

    // DAT_1401c43a0 = HerbalistComponent (ENDGAME B.3.9 D5: by cell, not the legacy
    // variable name "WorkplaceComponent").
    HerbalistComponent* workplaceComp = _entity->FindComponent<HerbalistComponent>();

    // Its description (+0x18)
    Description* workplaceDesc = workplaceComp->_description;
    HerbalistDescription* radiusComp = nullptr;
    // DAT_1401c43d8 = HerbalistDescription (ENDGAME B.3.9 D5: by cell, not the
    // legacy variable name "RadiusComponent").
    if (Object::IsDerivedFromNullSafe(workplaceDesc, HerbalistDescription::TypeId())) {
        radiusComp = static_cast<HerbalistDescription*>(workplaceDesc);
    }

    // DAT_1401c4870 = RadiusDescription on the workplace's ComponentDescription (+0x28)
    // (ENDGAME B.3.9 D5: by cell, not the legacy variable name "MaterialComponent").
    RadiusDescription* materialComp = FindSubComponentInList(_entity->_description);

    // -------------------------------------------------------
    // Step 3: Check capacity — is material count at limit?
    // (the town's per-resource counts, CitizenBehavior +0x170)
    // -------------------------------------------------------
    int materialIdx = (int)radiusComp->_resourceLimit;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    const ProfessionResourceEntry& resEntry = gameWorld->m_citizenList->professionResources._data[materialIdx];
    bool atCapacity = resEntry.maxCount <= resEntry.currentCount;

    // Set the status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
    StatusIconComponent* statusIcon = _entity->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);

    // Get herb count (the assigned count, self+0x50)
    int herbCount = _citizens._count;

    // If the worker capacity cache is 0, calculate it (the tree's port of 0x14012e5c0
    // called here takes the component as an integer: LEFT)
    if (matDesc->workState == 0) {
        int maxWorkers = matDesc->CalcMaxWorkers();
        matDesc->workState = maxWorkers;
    }

    // If herbCount exceeds max workers OR at capacity, we're done (return 1)
    if (matDesc->workState < herbCount || atCapacity) {
        return 1;
    }

    // -------------------------------------------------------
    // Step 4: Calculate search parameters
    // -------------------------------------------------------
    System::Random* rng = gameWorld->m_random;
    Map* map = gameWorld->m_game;
    // The map helpers (0x140008830, 0x14009ccf0, 0x1400a00f0, 0x14009f3e0) take the map
    // as int* / void*: LEFT.
    Map* mapGrid = map;

    int workplaceRadius = materialComp->_radius;
    int radiusSq = workplaceRadius * workplaceRadius;
    float searchArea = WorkImpl_Sqrtf_132010((float)radiusSq * (kRdata_140172184));
    int mapSize = map->size;
    int maxAttempts = (int)searchArea;

    // Workplace grid cell position
    int workplaceCell = _cell;
    int cellRow = workplaceCell / mapSize;
    int cellCol = workplaceCell % mapSize;

    // Get citizen's current position: CharacterComponent (+0x278) -> ModelComponent
    // (+0x68) -> the transform's translation
    const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

    int mapSz = map->size;
    int citizenCellX = (int)(citizenPos.x + (float)(mapSz >> 1));
    int citizenCellY = (int)(citizenPos.y + (float)(mapSz >> 1));

    // Clamp citizen cell to map bounds
    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSz) citizenCellX = mapSz - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSz) citizenCellY = mapSz - 1;

    int citizenGridCell = mapSz * citizenCellY + citizenCellX;

    // Get entity's grid cell for distance calculations
    ComponentEntity* entityIBPBuf[2] = { _entity.Get(), nullptr };
    uint32_t entityGridCell = EntityAnchorCell(entityIBPBuf, nullptr);

    float fGrowthVal = kRdata_14017214c;



    // -------------------------------------------------------
    // Step 5: Random search for herb spots
    // -------------------------------------------------------
    if (maxAttempts > 0) {
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            int randX, randY;
            int testCellX, testCellY;

            // Generate random position within radius (rejection sampling)
            do {
                do {
                    randX = rng->RandTol(workplaceRadius);
                    randY = rng->RandTol(workplaceRadius);
                    testCellX = cellCol + randX;
                    testCellY = cellRow + randY;
                } while (randY * randY + randX * randX > radiusSq);
            } while (WorkRules::SampleOffMap(testCellX, testCellY, mapSize));   // Y only (0x1400ee755)

            // NOT RETAIL: retail reads the wrapped cell before cell 0 / past the last one (UB).
            if (WorkRules::SampleCellOutsideMap(testCellX, testCellY, mapSize)) {
                continue;
            }

            uint32_t testCell = (uint32_t)(map->size) * testCellY + testCellX;
            MapCell* gridData = map->cells.GetData();

            // Check terrain type bitmask — skip "forest/resource" terrain (0x1e0 mask)
            uint8_t terrainByte = static_cast<uint8_t>(gridData[testCell].terrainType);
            uint32_t terrainBit = 1u << (terrainByte & 0x1f);
            if ((terrainBit & Bits(PathBits::Zoneless)) != 0) {
                continue;
            }

            // Check distance is within 2x workplace radius
            int dist = mapGrid->CalcDistance(entityGridCell, testCell);
            if (dist > workplaceRadius * 2) {
                continue;
            }

            // Get resource entity at this cell (the cell's occupant, +8)
            ComponentEntity* resourceEntity = gridData[testCell].occupant.Get();
            if (resourceEntity == nullptr) {
                continue;
            }

            // -------------------------------------------------------
            // Found a resource entity — check if it's a valid herb (0x1400052c0)
            // -------------------------------------------------------
            NaturalResourceComponent* natResComp = resourceEntity->FindComponent<NaturalResourceComponent>();

            if (natResComp == nullptr) {
                continue;
            }

            // Check NatRes component against the description list
            NaturalResourceComponent* natResComp2 = resourceEntity->FindComponent<NaturalResourceComponent>();

            // Iterate through herb types in the description list
            int herbTypeCount = radiusComp->_herbCount;
            ComponentDescription** herbTypeList = radiusComp->_herbs;

            for (int herbIdx = 0; herbIdx < herbTypeCount; herbIdx++) {
                // Check growth >= threshold
                float growth = natResComp2->resourceScale;
                if (growth < fGrowthVal) {
                    continue;
                }

                // Check the resource entity's description matches this herb type
                Object::External* resDescRef = resourceEntity->_description;
                Object::External* herbTypeRef = herbTypeList[herbIdx];
                if (resDescRef != herbTypeRef) {
                    continue;
                }

                // Check not flagged for removal (bit 20 of the component flags, +0x20)
                uint32_t flags20 = natResComp2->_flags;
                if ((flags20 >> 20) & 1) {
                    continue;
                }

                // Find nearest passable cell for pathfinding
                int nearestCell = mapGrid->FindCell(testCell, citizenGridCell, PathBits::Access);
                if (nearestCell < 0) {
                    continue;
                }

                // Check path to the nearest passable cell
                bool canPath = static_cast<uint8_t>(mapGrid->ValidateCell(PathBits::Walk, nearestCell, citizenGridCell)) != 0;   // the bool door read AL
                if (!canPath) {
                    continue;
                }

                // -------------------------------------------------------
                // Valid herb found — create PerformWork action
                // -------------------------------------------------------
                float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
                Math::Vector3 cutPos(
                    (float)(nearestCell % map->size) - halfOffset,
                    (float)(nearestCell / map->size) - halfOffset,
                    0.0f);

                float pathPos[3];
                map->GetTerrainPos(pathPos, &cutPos.x, 0, nullptr);

                CitizenComponent* harvestBuf[2] = { *citizenIBP, nullptr };
                NaturalResourceComponent* harvestTargetBuf[2] = { natResComp2, nullptr };
                WorkAct_PerformWorkCtor_c1eb0(
                    harvestBuf,
                    _workId,
                    harvestTargetBuf,
                    pathPos,
                    0,
                    1);

                _failedSearchCount = 0;
                AssignWorkToEntity(this, citizenIBP, _workId);
                return 2;
            }
        }
    }

    // -------------------------------------------------------
    // Step 7: No work found — handle failure
    // -------------------------------------------------------
    int failCount = _failedSearchCount;

    if (failCount > maxAttempts) {
        // Too many failures — check if citizen's storage has items, then replan
        StorageComponent* storage = (*citizenIBP)->_entity->FindComponent<StorageComponent>();

        int storageCount = storage->QueryCount();
        if (storageCount < 1) {
            return 0;
        }

        // Citizen has items in storage — replan long term
        (*citizenIBP)->LongTermPlan();
    }
    else {
        // Play idle animation and increment fail counter
        CitizenComponent* idleBuf[2] = { *citizenIBP, nullptr };
        WorkAct_PlayAnimationCtor_c2490(idleBuf, _workId, 0);

        // Assign work (idle animation) and register with AI
        int priority = _workId;
        CitizenComponent* citizenEnt2 = *citizenIBP;

        // Call vtable slot +0x50 (GetTarget)
        Profession* workEnt = nullptr;
        GetTarget(&workEnt);

        citizenEnt2->statusFlags |= Bits(CitizenStatus::TaskDone);
        citizenEnt2->lastWorkId = priority;
        citizenEnt2->lastErrand = CitizenErrand::None;

        if (workEnt == citizenEnt2->profession && priority >= 0) {
            citizenEnt2->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
        }

        // (the tree helper takes the container as uintptr_t*: LEFT)
        WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEnt's)
        WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenIBP);

        // Increment fail counter
        _failedSearchCount = failCount + 1;
    }

    return 2;
}


// ============================================================
// HerbalistWork::Slot6 — NOT HOOKABLE (RIP-relative CALL)
// Thunk: sub rsp,0x28; add rcx,0x48; call CitizenAI_impl_5520; xor al,al; add rsp,0x28; ret
// Calls BSTFindRemove on the BST at self+0x48, returns false.
// (The tree helper takes the container as uintptr_t*: LEFT.)
// ============================================================
int HerbalistWork::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return false;
}


// ============================================================
// HerbalistWork::Slot7 — 9-byte thunk, too small to hook.
// Thunk: add rcx,0x48; jmp CitizenAI_impl_5520
// Calls BSTFindRemove on the BST at self+0x48.
// ============================================================
int HerbalistWork::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}


// ============================================================
// HerbalistWork::Slot8 / CancelAll
// Loops through tree at +0x48 while count (+0x50) > 0.
// For each leftmost node (the walk follows the LEFT child, +0x10; the old comments
// here said "rightmost"), cancels the work item via engine call.
// ============================================================
int HerbalistWork::Cleanup() {
    int count = _citizens._count;

    while (count > 0) {
        // Find leftmost node in BST
        AssignmentNode* node = _citizens._root;
        AssignmentNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        // Cancel the work item: the citizen's action list (CharacterComponent +0x48)
        CitizenComponent* citizenEntity = leftmost->element;
        citizenEntity->character->actionStack.Clear();

        count = _citizens._count;
    }
    return 0;
}


// ============================================================
// HerbalistWork::Slot9 / RemoveInvalid (164 instructions)
// Iterates BST, validates each entry via FUN_1400f7100,
// collects invalid ones, removes them from tree.
// Returns true if tree becomes empty, false otherwise.
// ============================================================
int HerbalistWork::Validate() {
    AssignmentNode* root = _citizens._root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root == nullptr) {
        return true;  // Empty tree — nothing to validate
    }

    // Find leftmost node (minimum)
    AssignmentNode* node = root;
    while (node->left != nullptr) {
        node = node->left;
    }

    // In-order traversal
    while (node != nullptr) {
        CitizenComponent* entityIBP = node->element;

        // Validate this entity
        bool stillValid = ValidateTask(nullptr, &entityIBP, nullptr);

        if (!stillValid) {
            // Add to removal list — grow array if needed
            uint32_t newCount = removeCount + 1;
            if (newCount != removeCount) {
                CitizenComponent** newArr = removeArr;
                if ((int)removeCapacity < (int)newCount) {
                    uint32_t newCap = newCount;
                    if (removeCount != 0) {
                        newCap = newCount * 3;
                    }
                    removeCapacity = newCap;
                    newArr = static_cast<CitizenComponent**>(System::MemoryAllocator::MemAlloc((int64_t)newCap * 8, 8));
                }

                // Copy existing entries if buffer changed
                uint32_t copyStart = 0;
                if (removeArr == newArr) {
                    copyStart = removeCount;
                }
                for (uint32_t i = copyStart; i < newCount; i++) {
                    CitizenComponent* val;
                    if (i < removeCount) {
                        val = removeArr[i];
                    } else {
                        val = entityIBP;
                    }
                    newArr[i] = val;
                }
                if (removeArr != newArr) {
                    System::MemoryAllocator::MemFree(removeArr);
                }
                removeArr = newArr;
                removeCount = newCount;
            }
        }

        // In-order successor: right child's leftmost descendant, or parent
        AssignmentNode* rightChild = node->right;
        if (rightChild != nullptr) {
            // Go right, then all the way left
            node = rightChild;
            while (node->left != nullptr) {
                node = node->left;
            }
        } else {
            // Go up until we come from a left child
            AssignmentNode* parent = node->parent;
            if (parent != nullptr && parent->left != node) {
                // We came from the right — keep going up
                while (parent != nullptr && parent->right == node) {
                    node = parent;
                    parent = parent->parent;
                }
                node = parent;
            } else {
                node = parent;
            }
        }
    }

    // Remove all invalid entries from BST (keys compare unsigned; the tree helper takes
    // the container as uintptr_t*: LEFT)
    if (removeCount > 0) {
        for (uint32_t i = 0; i < removeCount; i++) {
            // Find and remove the node with this key from BST
            AssignmentNode* searchNode = _citizens._root;
            while (searchNode != nullptr) {
                if (removeArr[i] < searchNode->element) {
                    searchNode = searchNode->left;
                } else {
                    if (removeArr[i] <= searchNode->element) {
                        WorkBST_BSTRemove_a6e0(&_citizens, searchNode);
                        break;
                    }
                    searchNode = searchNode->right;
                }
            }
        }
    }

    System::MemoryAllocator::MemFree(removeArr);
    return removeCount == 0;
}


// ============================================================
// HerbalistWork::Slot10 / GetWorkEntity
// The entity's (+0x10) WorkPlaceComponent (FUN_140002e10), its profession (+0x48)
// into the return slot.
// ============================================================
Profession** HerbalistWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// HerbalistWork::Serialize
// Work::Serialize + Serializer(version=0) + BST serialize + int field
// ============================================================
void HerbalistWork::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create first serializer (version block, version=0)
    {
        IO::Serializer serializer1(stream, 0);
    }

    // Create second serializer (version=0) for BST + int
    IO::Serializer serializer2(stream, 0);

    // Serialize BST at self+0x48 (the helper takes the container as uintptr_t*: LEFT)
    WorkBST_BST_Serialize_296b0(&_citizens, &serializer2);

    // Serialize int at self+0x58
    serializer2.Serialize(_failedSearchCount, nullptr);
}


// ============================================================
// HerbalistWork::DtorBody (0x1400EEC40)
// Sets vtables down chain, frees tree nodes at +0x48, then calls
// AutoPickupWork -> Work -> Internal dtor chain.
// ============================================================
// HerbalistWork dtor (retail 0x1400eec40, called by slot 0 0x14002a640). The stamps and the `~Internal` call are the compiler's chain.
HerbalistWork::~HerbalistWork() = default;   // the member's ~Set is retail's inlined Clear (0x1400eec40)




// [11] 0x140029860 (shared by 13 tables): `XOR EAX,EAX; CMP [RCX+0x50],EAX; SETG AL`
// — "is anyone assigned". The old table pointed at Work_Slot11_HasActiveTasks.
int HerbalistWork::GetPriority() {
    return _citizens._count > 0 ? 1 : 0;
}
// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
HerbalistWork::HerbalistWork() : AutoPickupWork(), _failedSearchCount(0) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_HerbalistWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over HerbalistWork_Factory / _CreateInPlace (both
// Work_GenericCtor), plus three vtable-pointer statics for the dtor's stamps.

// create 0x14002a5e0: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* HerbalistWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HerbalistWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HerbalistWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* HerbalistWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HerbalistWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(HerbalistWork, "AutoPickupWork")
