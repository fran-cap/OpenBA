// src/game/Work/plantorchard.cpp
// Clean C++ reimplementation of PlantOrchard Work class -- all vtable methods.
// Source: Game/Work/PlantOrchard.cpp (from profile build debug strings)
//
// PlantOrchard (0x70 bytes) -- Orchard planting work action.
// Inherits: Object::Internal -> Work (0x48) -> PlantOrchard
// Vtable: 0x140161910
//
// Nearly identical to PlantArea except:
// - Finds the OrchardComponent (0x140005440) instead of the CropFieldComponent
// - Uses different action ctor (0x1400be6e0) with different parameter layout
// - Orchard cells are the OrchardComponent's slots (+0x50, 0x28 stride)
// - Calls OrchardComponent::ValidateRefs (0x140051830) to refresh orchard slot state
// - OnComplete/OnCancel deal with single cell (param4 is plain int, not packed)
// - Validate marks cells with 0 (not available) instead of 1
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/PlantOrchard.h"
#include "Work/work_rules.h"   // review F-199..F-206: the shared Execute decisions
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Tutorial/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "system/MemoryAllocator.h"
#include "math/Math.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <new>
#include "Component/OrchardComponent.h"   // ENDGAME B.3.9 D5: OrchardComponent::TypeId()
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// Diagnostics-only counters (src/diag/orchard_diag.cpp). No-op unless
// BANISHED_ORCHARD_DIAG is set; never change behaviour.
#include "orchard_diag.h"   // standalone/: the orchard diagnostic counters

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x140003050  FindComponent<WorkComponent>
//   0x140005440  FindComponent<OrchardComponent>
//   0x14012e5c0 WorkComponent::CalcMaxWorkers
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x1400be6e0  WorkAct_PlantWorkCtor_be6e0
//   0x140029a40  WorkCellBST_Insert_9a40
//   0x14001ca60  WorkCellBST_Remove_ca60
//   0x140029de0  WorkCellBST_Serialize_9de0
//   0x14000e320  WorkBase_TreeNodeFree_e320
//   0x1400f7100  Work::ValidateTask
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  FindComponent<WorkPlaceComponent>
//   0x140002340  Reimpl_ExternalPtr_Serialize_StringTable
//   0x140051830  OrchardComponent::ValidateRefs
//   0x140172114  kRdata_140172114 (.rdata)


// ============================================================
// The set's find by cell (the descent retail inlines)
// ============================================================
static WorkCellSetNode* FindCell(WorkCellSetNode* node, int key) {
    while (node != nullptr) {
        if (key < node->element.cell) {
            node = node->left;
        } else if (key > node->element.cell) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

// LEFT (every slot below): the set bodies 0x140029a40 / 0x14001ca60 / 0x140029de0
// keep their C signature (outside callers): the set as uintptr_t*.


// ============================================================
// PlantOrchard::Execute [4] -- Main orchard planting assignment (0x1400f4af0)
// ============================================================
//
// Algorithm:
// 1. Find the WorkComponent and the OrchardComponent on the orchard entity
// 2. Refresh orchard slot state (OrchardComponent::ValidateRefs)
// 3. Check max workers vs current count
// 4. Iterate orchard cells, find plantable spots (validity flag==0, not in the set)
// 5. Check orchard slot data for available planting location
// 6. Check pathfinding
// 7. Create orchard planting action, assign work, insert into the set
//
int PlantOrchard::Execute(CitizenComponent** citizenRef, System::Time* /*context*/) {
    // Step 1: Find components
    WorkComponent* workComp = field_10->FindComponent<WorkComponent>();          // 0x140003050
    OrchardComponent* orchard = field_10->FindComponent<OrchardComponent>();     // 0x140005440

    // Step 2: Refresh orchard slot state
    orchard->ValidateRefs();   // 0x140051830

    // Step 3: Check max workers
    int treeCount = _cells._count;
    if (workComp->workState == 0) {
        int maxWorkers = workComp->CalcMaxWorkers();
        workComp->workState = maxWorkers;
    }
    if (WorkRules::OverWorkerCap(treeCount, workComp->workState)) {   // 0x1400f4b70 CMP; JLE
        return 1;
    }

    // Step 4: Get map and citizen position (the model's translation, character +0x68)
    Map* map = GameUpdateLoop::_singleton->m_game;
    Map* mapGrid = map;

    CitizenComponent* citizen = *citizenRef;
    ModelComponent* posComp = citizen->character->modelComponent.Get();

    int mapSz = map->size;
    int citizenCellX = (int)(posComp->transform.translation.x + (float)(mapSz >> 1));
    int citizenCellY = (int)(posComp->transform.translation.y + (float)(mapSz >> 1));
    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSz) citizenCellX = mapSz - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSz) citizenCellY = mapSz - 1;
    uint32_t citizenGridCell = (uint32_t)(mapSz * citizenCellY + citizenCellX);

    // Step 5: Iterate orchard cells
    int fieldCellCount = field_58._size;

    if (fieldCellCount > 0) {
        for (int i = 0; i < fieldCellCount; i++) {
            bool* validityArray = field_58._data;

            // Orchard uses inverted validity: 0 = available, 1 = planted
            if (validityArray[i] != 0) {
                continue;
            }

            // Check if cell is already assigned (in the set)
            if (FindCell(_cells._root, i) != nullptr) {
                continue;
            }

            // The orchard slot: if it already holds a tree, it's occupied
            if (orchard->slots[i].tree != nullptr) {
                continue;
            }

            // The map cell of the orchard slot
            int srcCellIdx = orchard->slots[i].tile;

            // Check pathfinding
            int nearestCell = mapGrid->FindCell(srcCellIdx, citizenGridCell, 0xe0f);
            if (nearestCell < 0) {
                continue;
            }

            bool canPath = static_cast<uint8_t>(mapGrid->ValidateCell(0xe1f, citizenGridCell, nearestCell));   // the door read AL
            if (!canPath) {
                continue;
            }

            // Valid spot found -- create orchard planting action
            uint32_t typeConstVal = OrchardComponent::TypeId();  // DAT_1401c46b0
            ComponentEntity* entityRef = field_10.Get();

            float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
            Math::Vector3 targetPos(
                (float)(nearestCell % map->size) - halfOffset,
                (float)(nearestCell / map->size) - halfOffset,
                0.0f);

            float pathPos[4];
            map->GetTerrainPos(pathPos, &targetPos.x, 0, nullptr);

            // Get citizen entity
            CitizenComponent* citizenBuf = *citizenRef;

            // Call FUN_1400be6e0 orchard planting action ctor
            WorkAct_PlantWorkCtor_be6e0(
                &citizenBuf,                      // RCX: citizenIBP
                field_20,                         // EDX: priority
                field_68,                         // R8: the tree type (ExternalPtr)
                0,                                // R9D: 0
                pathPos,                          // [RSP+0x20]: position
                srcCellIdx,                       // [RSP+0x28]: srcCellIdx
                (uint32_t)i,                      // [RSP+0x30]: cellIdx (integer, EBX)
                &entityRef,                       // [RSP+0x38]: entity ref from field_10
                &typeConstVal,                    // [RSP+0x40]: type constant
                0,                                // [RSP+0x48]: unused
                (uint32_t)i                       // [RSP+0x50]: cellIdx again (integer, EBX)
            );

            // Assign work
            int priority = field_20;
            CitizenComponent* citizenEnt = *citizenRef;

            // Slot 10 (GetTarget): the workplace's Profession
            Profession* workEnt = nullptr;
            GetTarget(&workEnt);

            citizenEnt->statusFlags |= 4;
            citizenEnt->field_0x30 = priority;
            citizenEnt->field_0xB0 = 0;

            if (workEnt == citizenEnt->profession && priority >= 0) {
                citizenEnt->happinessFlags |= 0x20000;
            }

            // Insert into the set: {cell, citizen}.
            // Integer, not float — retail is `MOV dword ptr [RBP+0xf],EBX` (0x1400f4e3a),
            // and the matching lookups (FindCell above, OnComplete's param4) both
            // compare it as an int.
            WorkCellSetNode* insertResult = nullptr;
            WorkCellAssignment insertKey;
            insertKey.cell = i;
            insertKey._pad04 = 0;
            insertKey.citizen = *citizenRef;
            WorkCellBST_Insert_9a40(&_cells, &insertResult, &insertKey);

            // Mark cell as planted (not available)
            validityArray[i] = 1;

            OrchardDiag_NotePlantExecuted();
            return 2;
        }
    }

    return 1;
}





// ============================================================
// PlantOrchard::OnComplete [6] -- 0x1400f4a90
// Removes the set entry, marks cell available (sets validity to 0).
// param4 is plain int cell index (not packed like PlantArea).
// ============================================================
int PlantOrchard::OnComplete(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int param4) {
    // Search the set for the cell index and remove it
    WorkCellSetNode* node = _cells._root;
    while (node != nullptr) {
        if (param4 < node->element.cell) {
            node = node->left;
        } else if (param4 > node->element.cell) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // Mark cell as available (validity = 0 for orchard)
    bool* validityArray = field_58._data;
    validityArray[param4] = 0;
    return 0;
}


// ============================================================
// PlantOrchard::OnCancel [7] -- 0x1400f4a20
// Removes the set entry and marks the cell available.
// ============================================================
int PlantOrchard::OnCancel(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int param4) {
    // Search the set for the cell and remove it
    WorkCellSetNode* node = _cells._root;
    while (node != nullptr) {
        if (param4 < node->element.cell) {
            node = node->left;
        } else if (param4 > node->element.cell) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // Mark cell as available
    bool* validityArray = field_58._data;
    validityArray[param4] = 0;
    return 0;
}


// ============================================================
// PlantOrchard::Cleanup [8] -- 0x1400f4e70
// CancelAll: identical to PlantArea -- while the set is not empty, clear the
// action stack of the citizen of its leftmost entry.
// ============================================================
int PlantOrchard::Cleanup() {
    int count = _cells._count;

    while (count > 0) {
        WorkCellSetNode* node = _cells._root;
        WorkCellSetNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        CitizenComponent* citizen = leftmost->element.citizen.Get();
        citizen->character->actionStack.Clear();

        count = _cells._count;
    }
    return 0;
}


// ============================================================
// PlantOrchard::Validate [9] -- 0x1400f47f0
// RemoveInvalid: identical pattern to PlantArea, but marks cells with 0.
// ============================================================
int PlantOrchard::Validate() {
    int* collectArray = nullptr;
    int* currentArray = nullptr;
    uint32_t collectCount = 0;
    uint32_t collectCapacity = 0;

    WorkCellSetNode* node = _cells._root;

    if (node == nullptr) {
        goto cleanup;
    }

    // Find leftmost node
    {
        WorkCellSetNode* cur = node;
        while (cur != nullptr) {
            node = cur;
            cur = cur->left;
        }
    }

    // In-order traversal
    while (node != nullptr) {
        CitizenComponent* ibpData = node->element.citizen.Get();

        uint8_t valid = ValidateTask(nullptr, &ibpData, nullptr);

        if (valid == 0) {
            uint32_t newCount = collectCount + 1;
            if (newCount != collectCount) {
                int* newArray = collectArray;
                if ((int)collectCapacity < (int)newCount) {
                    uint32_t newCap = newCount;
                    if (collectCount != 0) {
                        newCap = newCount * 3;
                    }
                    collectCapacity = newCap;
                    newArray = static_cast<int*>(System::MemoryAllocator::MemAlloc((int64_t)newCap << 2, 4));
                    currentArray = newArray;
                }

                uint32_t copyStart = 0;
                if (collectArray == newArray) {
                    copyStart = collectCount;
                }
                for (uint32_t j = copyStart; j < newCount; j++) {
                    if (j < collectCount) {
                        newArray[j] = collectArray[j];
                    } else {
                        newArray[j] = node->element.cell;
                    }
                }
                if (collectArray != newArray) {
                    System::MemoryAllocator::MemFree(collectArray);
                }
                collectArray = newArray;
                collectCount = newCount;
            }

            // Mark cell available (orchard uses 0 for available)
            bool* validityArray = field_58._data;
            validityArray[node->element.cell] = 0;
        }

        // Move to next in-order node
        WorkCellSetNode* right = node->right;
        if (right != nullptr) {
            WorkCellSetNode* cur = right;
            while (cur != nullptr) {
                node = cur;
                cur = cur->left;
            }
        } else {
            WorkCellSetNode* parent = node->parent;
            if (parent != nullptr) {
                if (parent->left != node) {
                    while (parent != nullptr && parent->right == node) {
                        node = parent;
                        parent = parent->parent;
                    }
                }
                node = parent;
            } else {
                node = nullptr;
            }
        }
    }

cleanup:
    if ((int)collectCount > 0) {
        for (uint32_t k = 0; k < collectCount; k++) {
            WorkCellSetNode* bstNode = _cells._root;
            while (bstNode != nullptr) {
                if (collectArray[k] < bstNode->element.cell) {
                    bstNode = bstNode->left;
                } else if (collectArray[k] > bstNode->element.cell) {
                    bstNode = bstNode->right;
                } else {
                    WorkCellBST_Remove_ca60(&_cells, bstNode);
                    break;
                }
            }
        }
    }

    System::MemoryAllocator::MemFree(currentArray ? currentArray : collectArray);
    return collectCount == 0;
}


// ============================================================
// PlantOrchard::GetTarget [10] -- 0x1400f4ef0
// Identical to PlantArea::GetTarget.
// ============================================================
Profession** PlantOrchard::GetTarget(Profession** outPtr) {
    // 0x140002e10 = FindComponent<WorkPlaceComponent>.
    WorkPlaceComponent* workplace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workplace->profession;
    return outPtr;
}


// ============================================================
// PlantOrchard::GetPriority [11] -- shared 0x140029860
// ============================================================
int PlantOrchard::GetPriority() {
    return _cells._count > 0 ? 1 : 0;
}


// ============================================================
// PlantOrchard::Persist [3] -- 0x1400f4f50
// Identical to PlantArea::Persist.
// ============================================================
void PlantOrchard::Persist(IO::Stream& stream) {
    Work::Persist(stream);   // 0x1400f7400, a direct call in retail — QUALIFIED here

    IO::Serializer ser(stream, 0);

    WorkCellBST_Serialize_9de0(&_cells, &ser);

    int cellCountBuf[2] = { field_58._size, 0 };
    ser.Serialize(cellCountBuf[0], nullptr);

    // 0x14000a590 == Collection::Array<bool>::SetSize.
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        field_58.SetSize(cellCountBuf[0]);
    }

    // Serialize validity array through the BOOL-ARRAY door.
    //
    // ENDGAME B.3.4a closing — same alias bug, same retail proof as
    // plantarea.cpp and harvestarea.cpp. PlantOrchard::Serialize
    // (Application-x64.exe 0x1400f4f50) is instruction-for-instruction the same
    // function as PlantArea's with one different field offset earlier:
    //     1400f4fc1  XOR R9D,R9D                    ; name = nullptr
    //     1400f4fc4  MOV R8,qword ptr [RDI + 0x58]  ; the buffer
    //     1400f4fc8  MOV EDX,dword ptr [RSP + 0x50] ; the count
    //     1400f4fcc  LEA RCX,[RSP + 0x28]           ; the Serializer
    //     1400f4fd1  CALL qword ptr [0x140151458]   ; the bool-array door
    // The local pointer was aliased onto 0x140152ac8, the three-parameter
    // `Serialize(int&)` scalar door, and called with four arguments.
    ser.Serialize(cellCountBuf[0], field_58._data, nullptr);   // 0x140151458 = the bool-array overload

    ::Object::SerializeExternal<ComponentDescription>(ser, field_68);
}   // ~Serializer


// ============================================================
// PlantOrchard dtor 0x1400f5010. Identical structure to PlantArea's dtor.
// The stamps and the `~Internal` call are the compiler's chain.
// ============================================================
PlantOrchard::~PlantOrchard() {
    // The +0x58 flags are the `field_58` member's ~Array (MemFree, then +0x58 = +0x60 = 0),
    // then the +0x48 set's Clear is the `_cells` member's ~Set: reverse declaration order
    // after this empty body (0x1400f5010).

}




// Ctor 0x1400f50b0 (deserialize/factory variant): `??0Internal`, the Work base fields,
// the leaf fields zeroed (+0x60/+0x64 as the one qword the old port wrote; the two
// stamps are the compiler's). The orchard diagnostic counts the construction (the old
// factory ctor did the same).
PlantOrchard::PlantOrchard()
    : Work(),
      field_68(nullptr) {
    OrchardDiag_NotePlantConstructed();
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================

// create 0x14002c500: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* PlantOrchard::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PlantOrchard), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PlantOrchard());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* PlantOrchard::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PlantOrchard());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(PlantOrchard, "Work")
