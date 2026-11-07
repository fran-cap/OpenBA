// src/game/Work/huntwork.cpp
// Clean C++ reimplementation of HuntWork -- all methods.
// Source: Game/Work/HuntWork.cpp (from profile build debug strings)
//
// HuntWork (0x58 bytes) -- Hunting work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> HuntWork
// Vtable: 0x140161620
//
// Key difference from HerbalistWork: no herb search or growth check.
// HuntWork finds a valid hunting area cell, checks animal population,
// then creates a PerformWork action to hunt at that location.
// Does NOT serialize an extra int field (unlike HerbalistWork/ForesterWork).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/HuntWork.h"
#include "Work/work_rules.h"   // review F-199..F-206: the shared Execute decisions
#include "object/Internal.h"
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
#include <cstdlib>
#include <new>
#include "Component/HuntDescription.h"   // ENDGAME B.3.9 D5: HuntDescription::TypeId()
#include "Component/HuntComponent.h"
#include "Component/RadiusDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/StatusIconComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"

// ── Slot4 destination census (BANISHED_CROPFIELD_TRACE, the hunt trace flag). ──
// Retail's sub_bbc0 builds TWO different chains from this function's last argument:
//   param6=1 -> MoveTo(chosen cell, Slow, 0xe1f) + HuntAnimal chained behind it
//   param6=0 -> PerformWork(Time(5.0s)) IN PLACE  + HuntAnimal chained behind it

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x1400e3db0  Work::TryAutoPickup
//   0x140003050  ComponentEntity::FindComponent<WorkComponent>
//   0x140002f90  ComponentEntity::FindComponent<StatusIconComponent>
//   0x14012c3f0  StatusIconComponent::SetFlag
//   0x14012e5c0 WorkComponent::CalcMaxWorkers
//   0x140006270  ComponentEntity::FindComponent<StorageComponent>
//   0x14012cf10  StorageComponent::QueryCount
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x1400bbbc0  WorkAct_HuntWorkCtor_bbbc0
//   0x140021a30  WorkBST_RegisterWork_1a30
//   0x140115e60  CitizenComponent::LongTermPlan
//   0x14002a730  ComponentEntity::FindComponent<HuntComponent>
//   0x1400063d0  the RadiusDescription scan (FindRadiusDescription below)
//   0x140008830  WorkBase_CalcDistance_8830
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x140087fb0  Pf_EntityAnchorCell_7fb0
//   0x1400296b0  WorkBST_BST_Serialize_296b0
//   0x1400f7100  Work::ValidateTask
//   0x14001a6e0  WorkBST_BSTRemove_a6e0
//   0x140025520  WorkBST_BSTFindRemove_25520
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  ComponentEntity::FindComponent<WorkPlaceComponent>
//   0x140014c30  WorkBase_TreeNodeFree_14c30
//   0x140172114  kRdata_140172114 (.rdata)
//
// Vtable pointers for dtor chain
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: HuntWork_Ctor
// disassembles to `0x1400ef749: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// The AutoPickupWork vtable it then overwrites with is 0x140160fa0 (the next LEA
// in the same ctor); only after that comes this class's own vtable.
// These were previously 0x1401612e0 / 0x140161340, neither of which is a vtable:
// both are INTERIOR addresses of the FireWork (0x1401612a0) and FishingWork
// (0x140161320) vtables, and Ghidra reports ZERO xrefs to either.


// ============================================================
// Helper: 0x1400063d0 -- the RadiusDescription among a ComponentDescription's
// sub-descriptions (+0x28 data / +0x30 count), no CheckPtr; the first type-id match
// decides (IsDerivedFrom recheck, else null).
// ============================================================
static RadiusDescription* FindRadiusDescription(ComponentDescription* subObj) {
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
// HuntWork::Slot4 -- Hunt target assignment (0x1400EEFF0)
// ============================================================
//
// Algorithm:
// 1. Check auto-pickup assignments
// 2. If storage has items, replan citizen (LongTermPlan)
// 3. Find the WorkComponent, the HuntComponent and the RadiusDescription
// 4. Check if animal population allows hunting (resource tracker)
// 5. Set the status icon flag based on capacity
// 6. Check max workers vs current count
// 7. Random search for valid hunting cell within area radius
// 8. Validate cell: distance, pathability, terrain
// 9. Create the hunt action at the cell, or the roaming fallback
//
int HuntWork::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // Save citizen ref
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
    // Step 2: Check if citizen's storage has items -> replan
    // -------------------------------------------------------
    StorageComponent* storage = (*citizenIBP)->_entity->FindComponent<StorageComponent>();

    int storageCount = storage->QueryCount();
    if (storageCount >= 1) {
        (*citizenIBP)->LongTermPlan();
        return 2;
    }

    // -------------------------------------------------------
    // Step 3: Find the WorkComponent and the HuntComponent
    // -------------------------------------------------------
    WorkComponent* matDesc = field_10->FindComponent<WorkComponent>();

    HuntComponent* huntComp = field_10->FindComponent<HuntComponent>();

    // Its description, as a HuntDescription
    Description* herdObj = huntComp->_description;
    // DAT_1401c78c8 = HuntDescription (ENDGAME B.3.9 D5: by cell, not the legacy
    // variable name "HerdComponent").
    bool isHerd = Object::IsDerivedFromNullSafe(herdObj, HuntDescription::TypeId());
    HuntDescription* huntDesc = isHerd ? static_cast<HuntDescription*>(herdObj) : nullptr;

    // The RadiusDescription on the workplace's ComponentDescription (+0x28)
    RadiusDescription* area = FindRadiusDescription(field_10->_description);

    // -------------------------------------------------------
    // Step 4: Check animal population via resource tracker
    // (the town's per-resource counts, CitizenBehavior +0x170)
    // -------------------------------------------------------
    int materialIdx = (int)huntDesc->_type;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    const ProfessionResourceEntry& resEntry = gameWorld->m_citizenList->professionResources._data[materialIdx];
    bool atCapacity = resEntry.maxCount <= resEntry.currentCount;

    // -------------------------------------------------------
    // Step 5: The status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
    // -------------------------------------------------------
    StatusIconComponent* statusIcon = field_10->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);

    if (atCapacity) {
        return 1;
    }

    // -------------------------------------------------------
    // Step 6: Check max workers vs current count (the tree's port of 0x14012e5c0
    // called here takes the component as an integer: LEFT)
    // -------------------------------------------------------
    int huntCount = _citizens._count;

    if (matDesc->workState == 0) {
        int maxWorkers = matDesc->CalcMaxWorkers();
        matDesc->workState = maxWorkers;
    }

    if (WorkRules::OverWorkerCap(huntCount, matDesc->workState)) {   // 0x1400ef196 CMP; JLE
        return 1;
    }

    // -------------------------------------------------------
    // Step 7: Random search for valid hunting cell
    // -------------------------------------------------------
    Map* map = gameWorld->m_game;
    // The map helpers (0x140008830, 0x1400a00f0) take the map as int*: LEFT.
    Map* mapGrid = map;
    System::Random* rng = gameWorld->m_random;

    int areaRadius = area->_radius;
    int radiusSq = areaRadius * areaRadius;
    int mapSize = map->size;

    // Workplace grid cell position
    int workCell = field_28;
    int cellCol = workCell % mapSize;
    int cellRow = workCell / mapSize;

    // Generate random position within radius (rejection sampling)
    // Original only bounds-checks Y; we check both for safety but the
    // else branch below handles the case where the cell is invalid.
    int randX, randY;
    int testCellX, testCellY;

    do {
        do {
            randX = rng->RandTol(areaRadius);
            randY = rng->RandTol(areaRadius);
            testCellX = cellCol + randX;
            testCellY = cellRow + randY;
        } while (radiusSq < randY * randY + randX * randX);
    } while (testCellY < 0 || testCellY >= mapSize);
    // Note: X bounds not checked here matches original behavior.
    // Negative X will fail terrain/entity checks -> else branch handles it.

    uint32_t testCell = (uint32_t)(mapSize * testCellY + testCellX);

    // Get citizen's current position for pathfinding: CharacterComponent (+0x278) ->
    // ModelComponent (+0x68) -> the transform's translation
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

    // Get workplace entity's grid cell for distance check
    ComponentEntity* entityIBPBuf[2] = { field_10.Get(), nullptr };
    uint32_t entityGridCell = EntityAnchorCell(entityIBPBuf, nullptr);

    // -------------------------------------------------------
    // Step 8: Validate cell — terrain, distance, entity, passability
    // Original checks all 4 conditions; if ANY fail, uses fallback action.
    // -------------------------------------------------------
    MapCell* gridData = map->cells.GetData();
    bool allChecksPass = false;

    // Only check if cell index is in bounds (negative X can produce bad index)
    if (testCellX < 0 || testCellX >= mapSize) {
    } else {
        // Check 1: terrain type — skip resource terrain (0x1e0 mask)
        uint8_t terrainByte = static_cast<uint8_t>(gridData[testCell].terrainType);
        uint32_t terrainBit = 1u << (terrainByte & 0x1f);
        bool terrainOk = (terrainBit & Bits(PathBits::Zoneless)) == 0;

        // Check 2: distance from workplace to random cell
        int dist = mapGrid->CalcDistance(entityGridCell, testCell);
        bool distOk = dist <= areaRadius * 2;

        // Check 3: no entity at the cell (the cell's occupant, +8)
        ComponentEntity* cellEntity = gridData[testCell].occupant.Get();
        bool emptyCell = (cellEntity == nullptr);

        // Attribute the failure to the FIRST failing check only, so the columns sum to
        // the fallback count instead of double-counting a cell that fails several.

        // Check 4: passability from random cell to citizen
        if (terrainOk && distOk && emptyCell) {
            bool canPath = (char)mapGrid->ValidateCell(PathBits::Walk, testCell, citizenGridCell) != 0;
            allChecksPass = canPath;
        }
    }


    // (0x1400bbbc0 takes the HuntComponent slot as int64_t*: LEFT)
    if (allChecksPass) {
        // -------------------------------------------------------
        // Valid hunt target — create hunt action at terrain position
        // -------------------------------------------------------
        float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
        Math::Vector3 huntPos(
            (float)((int)testCell % map->size) - halfOffset,
            (float)((int)testCell / map->size) - halfOffset,
            0.0f);

        // Get terrain height
        float pathPos[3];
        map->GetTerrainPos(pathPos, &huntPos.x, 0, nullptr);

        CitizenComponent* citizenEntRef = *citizenIBP;

        HuntComponent* huntComp2 = field_10->FindComponent<HuntComponent>();

        WorkAct_HuntWorkCtor_bbbc0(
            &citizenEntRef,        // citizen entity IBP
            field_20,              // priority
            &huntComp2,            // hunt component IBP
            pathPos,               // terrain position
            0.0f,                  // float param
            1);                    // char param (specific location)
    } else {
        // -------------------------------------------------------
        // Cell checks failed — create fallback hunt action
        // Position (0,0,0), g_pAgeMultiplier float, char=0 (roaming)
        // -------------------------------------------------------
        CitizenComponent* citizenEntRef2 = *citizenIBP;

        HuntComponent* huntComp3 = field_10->FindComponent<HuntComponent>();

        float fallbackPos[3] = { 0.0f, 0.0f, 0.0f };

        WorkAct_HuntWorkCtor_bbbc0(
            &citizenEntRef2,       // citizen entity IBP
            field_20,              // priority
            &huntComp3,            // hunt component IBP
            fallbackPos,           // fallback position (0,0,0)
            kRdata_140172198,     // float param (roaming radius)
            0);                    // char param (roaming mode)
    }

    // -------------------------------------------------------
    // Both paths: assign work and return 2
    // -------------------------------------------------------
    int priority = field_20;
    CitizenComponent* citizenEnt = *citizenIBP;

    Profession* workEnt = nullptr;
    GetTarget(&workEnt);

    citizenEnt->statusFlags |= Bits(CitizenStatus::TaskDone);
    citizenEnt->lastErrand = CitizenErrand::None;
    citizenEnt->field_0x30 = priority;

    if (workEnt == citizenEnt->profession && priority >= 0) {
        citizenEnt->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
    }

    // (the tree helper takes the container as uintptr_t*: LEFT)
    WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEnt's)
    WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenIBP);
    return 2;
}


// ============================================================
// HuntWork::Slot6 -- NOT HOOKABLE (RIP-relative CALL)
// Thunk: sub rsp,0x28; add rcx,0x48; call CitizenAI_impl_5520; xor al,al; add rsp,0x28; ret
// Calls BSTFindRemove on the BST at self+0x48, returns false.
// (The tree helper takes the container as uintptr_t*: LEFT.)
// ============================================================
int HuntWork::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return false;
}


// ============================================================
// HuntWork::Slot7 -- 9-byte thunk, too small to hook.
// Thunk: add rcx,0x48; jmp CitizenAI_impl_5520
// Calls BSTFindRemove on the BST at self+0x48.
// ============================================================
int HuntWork::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}


// ============================================================
// HuntWork::Slot8 / CancelAll (0x1400EF530)
// Loops through tree at +0x48 while count (+0x50) > 0.
// For each leftmost node, cancels the work item via engine call.
// ============================================================
int HuntWork::Cleanup() {
    int count = _citizens._count;

    while (count > 0) {
        // Find leftmost node in BST (following the left child, +0x10)
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
// HuntWork::Slot9 / RemoveInvalid (0x1400EED90)
// Iterates BST, validates each entry via FUN_1400f7100,
// collects invalid ones, removes them from tree.
// Returns true if all valid (removeCount == 0), false otherwise.
// Hook DISABLED (bug class 3 -- MOV RAX,RSP).
// ============================================================
int HuntWork::Validate() {
    AssignmentNode* root = _citizens._root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root == nullptr) {
        return true;  // Empty tree -- nothing to validate
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
            // Add to removal list -- grow array if needed
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
            node = rightChild;
            while (node->left != nullptr) {
                node = node->left;
            }
        } else {
            AssignmentNode* parent = node->parent;
            if (parent != nullptr && parent->left != node) {
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
// HuntWork::Slot10 / GetWorkEntity (0x1400EF5A0)
// The entity's (+0x10) WorkPlaceComponent (0x140002e10), its profession (+0x48) into
// the return slot.
// ============================================================
Profession** HuntWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// HuntWork::GetPriority [11] 0x140029860 (shared by 13 tables) / HasWorkers.
// Returns (this->_citizens._count > 0). Slot 5 is Work's (0x1400f7540): the old dead
// HuntWork_Execute_Impl copy of it is gone.
// ============================================================
int HuntWork::GetPriority() {
    // Retail 0x140029860 is `CMP [RCX+0x50],EAX; SETG` — a SIGNED compare.
    return _citizens._count > 0 ? 1 : 0;
}


// ============================================================
// HuntWork::Serialize (0x1400EF600)
// Work::Serialize + Serializer(version=0) + BST serialize.
// Note: No extra int field (unlike HerbalistWork/ForesterWork).
// ============================================================
void HuntWork::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create first serializer (version block, version=0)
    {
        IO::Serializer serializer1(stream, 0);
    }

    // Create second serializer (version=0) for BST
    IO::Serializer serializer2(stream, 0);

    // Serialize BST at self+0x48 (the helper takes the container as uintptr_t*: LEFT)
    WorkBST_BST_Serialize_296b0(&_citizens, &serializer2);
}


// ============================================================
// HuntWork::DtorBody (0x1400EF680)
// Sets vtables down chain, frees tree nodes at +0x48, then calls
// AutoPickupWork -> Work -> Internal dtor chain.
// ============================================================
// HuntWork dtor (retail 0x1400ef680, called by slot 0 0x14002a700). The stamps and the `~Internal` call are the compiler's chain.
HuntWork::~HuntWork() = default;   // the member's ~Set is retail's inlined Clear (0x1400ef680)
// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
HuntWork::HuntWork() : AutoPickupWork() {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_HuntWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over HuntWork_Factory / _CreateInPlace (both
// Work_GenericCtor), plus three vtable-pointer statics for the dtor's stamps.

// create 0x14002a690: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* HuntWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HuntWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HuntWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* HuntWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HuntWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(HuntWork, "AutoPickupWork")
