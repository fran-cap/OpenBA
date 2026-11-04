// src/game/Work/tendarea.cpp
// Clean C++ reimplementation of TendArea -- all methods.
// Source: Game/Work/TendArea.cpp (from profile build debug strings)
//
// TendArea (0x60 bytes) -- Area tending work action (crops, orchards, etc).
// Inherits: Object::Internal -> Work (0x48) -> TendArea
// Vtable: 0x140161990
//
// BST node layout (simple, 0x20 bytes):
//   +0x00: key (InternalBasePtr, citizen entity)
//   +0x08: parent pointer
//   +0x10: left child
//   +0x18: right child
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/TendArea.h"
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
#include <new>
#include "Component/CropFieldComponent.h"   // ENDGAME B.3.9 D5: CropFieldComponent::TypeId()
#include "Component/OrchardComponent.h"     // ENDGAME B.3.9 D5: OrchardComponent::TypeId()
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x1400296b0  WorkBST_BST_Serialize_296b0
//   0x140025520  WorkBST_BSTFindRemove_25520
//   0x14001a6e0  WorkBST_BSTRemove_a6e0
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x1400f7100  WorkBase_ValidateTask_f7100
//   0x140014c30  WorkBase_TreeNodeFree_14c30
//   0x140002e10  AttendanceComponent_GetJobHostComponent
//   0x14012e5c0 WorkComponent::CalcMaxWorkers
//   0x140003050  WorkBase_FindComponent_3050
//   0x140005380  WorkAcc_FindComponent_5380
//   0x140005440  WorkAcc_FindComponent_5440
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x1400bb0a0  WorkAct_GrazingWorkCtor_bb0a0
//   0x140021a30  WorkBST_RegisterWork_1a30
//   0x140115e60  CitizenAI_State_LongTermPlan
//   0x140006270  WorkBase_FindComponent_6270
//   0x14012cf10  StorageComponent::QueryCount
//   0x140172114  kRdata_140172114 (.rdata)
//
// Vtable pointers for dtor chain
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: TendArea_Ctor
// disassembles to `0x1400f5a1d: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// This was previously 0x1401612e0, which is not a vtable at all: it is an INTERIOR
// address of FireWork's vtable (starts 0x1401612a0, 13 slots, ends 0x140161308 at the
// "FishingWork" name string) — i.e. slot 8. Ghidra reports ZERO xrefs to 0x1401612e0.


// ============================================================
// TendArea::Slot4 -- Tending target assignment (0x1400f5490)
// ============================================================
//
// Algorithm:
// 1. Check max workers vs current count
// 2. Get citizen position, random access RNG
// 3. Depending on tendMode: find CropAreaComponent or OrchardAreaComponent
// 4. Pick a random cell from the area, check pathability
// 5. Get terrain position for target and citizen
// 6. Create PerformWork action
// 7. Register with CitizenAI
//
int TendArea::Execute(CitizenComponent** citizenRef, System::Time* /*context*/) {
    // Step 1: Check max workers
    WorkComponent* workComp = field_10->FindComponent<WorkComponent>();   // 0x140003050

    int currentWorkers = _citizens._count;

    if (workComp->workState == 0) {
        int maxWorkers = workComp->CalcMaxWorkers();
        workComp->workState = maxWorkers;
    }

    if (WorkRules::OverWorkerCap(currentWorkers, workComp->workState)) {   // 0x1400f54ed CMP; JLE
        return 1;  // At capacity, no work
    }

    // Step 2: Get game world singletons
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* map = gameWorld->m_game;
    Map* mapGrid = map;
    System::Random* rng = gameWorld->m_random;

    // Step 3: Get citizen position (the model's translation, character +0x68)
    CitizenComponent* citizen = *citizenRef;
    ModelComponent* posComp = citizen->character->modelComponent.Get();

    int mapSize = map->size;
    int citizenCellX = (int)(posComp->transform.translation.x + (float)(mapSize >> 1));
    int citizenCellY = (int)(posComp->transform.translation.y + (float)(mapSize >> 1));

    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSize) citizenCellX = mapSize - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSize) citizenCellY = mapSize - 1;

    uint32_t citizenGridCell = (uint32_t)(mapSize * citizenCellY + citizenCellX);

    // Step 4: Choose area component based on tendMode and pick random cell
    int targetCell;
    uint32_t typeId;
    int workFlag;
    uint32_t randIdx;

    if (tendMode == 0) {
        // Crop mode: the field's CropFieldComponent (0x140005380)
        CropFieldComponent* crop = field_10->FindComponent<CropFieldComponent>();

        uint32_t areaSize = static_cast<uint32_t>(crop->slots._size);
        randIdx = rng->Rand(areaSize);

        workFlag = 1;
        targetCell = crop->slots._data[(int)randIdx].cell;
        typeId = CropFieldComponent::TypeId();  // DAT_1401c7510
    } else {
        // Orchard mode: the OrchardComponent (0x140005440)
        OrchardComponent* orchard = field_10->FindComponent<OrchardComponent>();

        uint32_t areaSize = static_cast<uint32_t>(orchard->slotCount);
        randIdx = rng->Rand(areaSize);

        workFlag = 2;
        targetCell = orchard->slots[(int)randIdx].tile;
        typeId = OrchardComponent::TypeId();  // DWORD_1401c46b0
    }

    // Step 5: Find nearest passable cell and check pathability
    int nearestCell = mapGrid->FindCell(targetCell, citizenGridCell, 0xe1f);
    if (nearestCell < 0 || (int)randIdx < 0) {
        return 0;  // No passable cell found
    }

    bool canPath = static_cast<uint8_t>(mapGrid->ValidateCell(0xe1f, citizenGridCell, nearestCell));   // the door read AL
    if (!canPath) {
        return 0;  // Can't path to cell
    }

    // Step 6: Calculate terrain positions
    float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
    Math::Vector3 targetPos(
        (float)(targetCell % map->size) - halfOffset,
        (float)(targetCell / map->size) - halfOffset,
        0.0f);
    float targetTerrain[3];
    map->GetTerrainPos(targetTerrain, &targetPos.x, '\0', nullptr);

    Math::Vector3 citizenPos(
        (float)(nearestCell % map->size) - halfOffset,
        (float)(nearestCell / map->size) - halfOffset,
        0.0f);
    float citizenTerrain[3];
    map->GetTerrainPos(citizenTerrain, &citizenPos.x, '\0', nullptr);

    // Step 7: Create PerformWork action
    ComponentEntity* workEntityRef = field_10.Get();
    CitizenComponent* entityBuf[2] = { *citizenRef, nullptr };

    WorkAct_GrazingWorkCtor_bb0a0(
        entityBuf,                  // citizenIBP
        field_20,                   // priority
        &workEntityRef,             // work entity
        0,                          // unused
        citizenTerrain,             // target position
        targetTerrain,              // source position
        typeId,                     // type ID
        workFlag,                   // work flag (1=crop, 2=orchard)
        randIdx);                   // cell index

    // Step 8: Assign work and register with AI
    int priority = field_20;
    CitizenComponent* citEnt = *citizenRef;

    // Slot 10 (GetTarget): the workplace's Profession
    Profession* workEnt = nullptr;
    GetTarget(&workEnt);

    citEnt->statusFlags |= 4;
    citEnt->field_0x30 = priority;
    citEnt->field_0xB0 = 0;

    if (workEnt == citEnt->profession && priority >= 0) {
        citEnt->happinessFlags |= 0x20000;
    }

    WorkCitizenSetNode* inserted = nullptr;
    WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenRef);
    return 2;
}


// LEFT (slots 6 / 7, the validate removal, Persist): the set bodies 0x140025520 /
// 0x14001a6e0 / 0x1400296b0 keep their C signature (outside callers): the set as
// uintptr_t*.

// ============================================================
// TendArea::OnComplete [6] -- BSTFindRemove + return false (0x1400f5470)
// ============================================================
int TendArea::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return false;
}


// ============================================================
// TendArea::OnCancel [7] -- BSTFindRemove thunk (0x1400f5460)
// ============================================================
int TendArea::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}


// ============================================================
// TendArea::Cleanup [8] / CancelAll (0x1400f5830)
// While the set is not empty: clear the leftmost citizen's action stack.
// ============================================================
int TendArea::Cleanup() {
    while (_citizens._count > 0) {
        // Find the leftmost node
        WorkCitizenSetNode* node = _citizens._root;
        WorkCitizenSetNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        // Cancel the work item
        CitizenComponent* citizen = leftmost->element;

        citizen->character->actionStack.Clear();
    }
    return 0;
}


// ============================================================
// TendArea::Validate [9] / RemoveInvalid (0x1400f5230)
// Iterates the set, validates each citizen, removes invalid ones.
// Returns true if all valid, false otherwise.
// ============================================================
int TendArea::Validate() {
    WorkCitizenSetNode* root = _citizens._root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root == nullptr) {
        return true;
    }

    // Find leftmost node (minimum)
    WorkCitizenSetNode* node = root;
    while (node->left != nullptr) {
        node = node->left;
    }

    // In-order traversal
    while (node != nullptr) {
        CitizenComponent* citizen = node->element;

        // Validate this entry
        bool stillValid = ValidateTask(nullptr, &citizen, nullptr);

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
                        val = citizen;
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
        WorkCitizenSetNode* rightChild = node->right;
        if (rightChild != nullptr) {
            node = rightChild;
            while (node->left != nullptr) {
                node = node->left;
            }
        } else {
            WorkCitizenSetNode* parent = node->parent;
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

    // Remove all invalid entries from the set
    if (removeCount > 0) {
        for (uint32_t i = 0; i < removeCount; i++) {
            WorkCitizenSetNode* searchNode = _citizens._root;
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
// TendArea::GetTarget [10] (0x1400f58a0)
// The post's WorkPlaceComponent's Profession, through the hidden return slot.
// ============================================================
Profession** TendArea::GetTarget(Profession** outPtr) {
    // 0x140002e10 = FindComponent<WorkPlaceComponent>.
    WorkPlaceComponent* workplace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workplace->profession;
    return outPtr;
}


// ============================================================
// TendArea::GetPriority [11] / HasWorkers (0x140029860)
// 9-byte thunk, returns (_citizens._count > 0).
// ============================================================
int TendArea::GetPriority() {
    // Retail 0x140029860 is `CMP [RCX+0x50],EAX; SETG` — a SIGNED compare.
    return _citizens._count > 0 ? 1 : 0;
}





// ============================================================
// TendArea::Persist [3] (0x1400f5900)
// Work::Persist + Serializer(v0) + the set + tendMode.
// ============================================================
void TendArea::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    IO::Serializer ser(stream, 0);

    // The set at +0x48
    WorkBST_BST_Serialize_296b0(&_citizens, &ser);

    // tendMode (+0x58)
    ser.Serialize(tendMode, nullptr);
}   // ~Serializer


// ============================================================
// TendArea dtor (called by slot 0 0x14002c5d0). The stamps and the `~Internal`
// call are the compiler's chain. Frees the set's nodes.
// ============================================================
TendArea::~TendArea() = default;   // the member's ~Set is retail's inlined Clear (0x1400f5970)




// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The stamps are the compiler's.
TendArea::TendArea() : Work(), tendMode(0) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_TendArea_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over TendArea_Factory / _CreateInPlace (both
// Work_GenericCtor), plus two vtable-pointer statics for the dtor's stamps.

// create 0x14002c590: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* TendArea::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TendArea), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TendArea());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* TendArea::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TendArea());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(TendArea, "Work")
