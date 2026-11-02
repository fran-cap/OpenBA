// src/game/Work/plantarea.cpp
// Clean C++ reimplementation of PlantArea Work class -- all vtable methods.
// Source: Game/Work/PlantArea.cpp (from profile build debug strings)
//
// PlantArea (0x70 bytes) -- Crop field planting work action.
// Inherits: Object::Internal -> Work (0x48) -> PlantArea
// Vtable: 0x140161890
//
// Fields (Work/PlantArea.h):
//   +0x48: the cell -> citizen set (WorkCellSetNode), count +0x50
//   +0x58: the per-slot byte array (1 = still to plant), count +0x60
//   +0x68: the crop's ComponentDescription (ExternalPtr)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/PlantArea.h"
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
#include <cstdlib>
#include <new>
#include "Component/CropFieldComponent.h"   // ENDGAME B.3.9 D5: CropFieldComponent::TypeId()
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "ext/registry.h"                                  // Ext::Fix().crop_planting_exit
#include "ext/crop_planting_exit/crop_planting_exit_rule.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x140003050  FindComponent<WorkComponent>
//   0x140005380  FindComponent<CropFieldComponent>
//   0x14012e5c0 WorkComponent::CalcMaxWorkers
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x1400bdd20  WorkAct_ShowToolCtor3_bdd20
//   0x140029a40  WorkCellBST_Insert_9a40
//   0x14001ca60  WorkCellBST_Remove_ca60
//   0x140029de0  WorkCellBST_Serialize_9de0
//   0x14000e320  WorkBase_TreeNodeFree_e320
//   0x1400f7100  Work::ValidateTask
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  FindComponent<WorkPlaceComponent>
//   0x140002340  Reimpl_ExternalPtr_Serialize_StringTable
//   0x14011b390  WorkLeaf_PlantArea_b390
//   0x140172114  kRdata_140172114 (.rdata)


// ============================================================
// The set's find by cell (the descent retail inlines): left on a smaller key,
// right on a greater one.
// ============================================================
static WorkCellSetNode* FindCell(WorkCellSetNode* node, int key) {
    while (node != nullptr) {
        if (key < node->element.cell) {
            node = node->left;
        } else if (key > node->element.cell) {
            node = node->right;
        } else {
            return node;  // found
        }
    }
    return nullptr;
}

// NOT RETAIL: fix.crop_planting_exit's count of the field's slots that left "empty" (growing or
// done), the sign that some worker reached the field.
static int SettledSlots(const CropFieldComponent* crop) {
    int n = 0;
    for (int i = 0; i < crop->slots._size; i++) {
        if (crop->slots._data[i].state != 0) n++;
    }
    return n;
}

// LEFT (every slot below): the set bodies 0x140029a40 / 0x14001ca60 / 0x140029de0
// keep their C signature (outside callers): the set as uintptr_t*.


// ============================================================
// PlantArea::Execute [4] -- Main planting assignment logic (0x1400f4010)
// ============================================================
//
// Algorithm:
// 1. Find the WorkComponent and the CropFieldComponent on the field entity
// 2. Check max workers vs current assigned count
// 3. Get citizen position, compute grid cell
// 4. Iterate field cells: find plantable spots (validity flag set, not in the set)
// 5. Check for paired cell (adjacent, also valid and unassigned)
// 6. Look up the map cell of the slot, check passability
// 7. Create planting action, assign work, insert into the set
// 8. If no cells remaining and count==0, mark the field's slots done
//
int PlantArea::Execute(CitizenComponent** citizenRef, System::Time* /*context*/) {
    // Step 1: Find components on the field entity
    WorkComponent* workComp = field_10->FindComponent<WorkComponent>();         // 0x140003050
    CropFieldComponent* crop = field_10->FindComponent<CropFieldComponent>();   // 0x140005380

    // Step 2: Check max workers vs current tree count
    int treeCount = _cells._count;
    if (workComp->workState == 0) {
        int maxWorkers = workComp->CalcMaxWorkers();
        workComp->workState = maxWorkers;
    }

    if (treeCount >= workComp->workState) {
        return 1;
    }

    // Step 3: Get map and citizen position (the model's translation, character +0x68)
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

    // Step 4: Iterate field cells
    int fieldCellCount = field_58._size;
    bool anyUnplanted = true;  // tracks if we found any cell with validity flag set

    if (fieldCellCount > 0) {
        for (int i = 0; i < fieldCellCount; i++) {
            bool* validityArray = field_58._data;
            if (validityArray[i] == 0) {
                continue;
            }

            // Check if cell is already assigned (in the set)
            if (FindCell(_cells._root, i) != nullptr) {
                continue;
            }

            // This cell needs planting -- mark that we had work to do
            anyUnplanted = false;

            // Check for paired cell (i+1)
            bool pairedAvailable = false;
            int pairedIdx = i + 1;
            if (pairedIdx < fieldCellCount && validityArray[pairedIdx] != 0) {
                if (FindCell(_cells._root, pairedIdx) == nullptr) {
                    pairedAvailable = true;
                }
            }

            // The map cell of the field slot
            int srcCellIdx = crop->slots._data[i].cell;

            // ⚠ The action ctor's 7th argument is a pointer to TWO adjacent ints, not one.
            // Retail builds them as the stack pair local_70 / local_6c:
            //     local_70 = slot[i].cell
            //     local_6c = paired ? slot[pairedIdx].cell : -1
            // and passes &local_70, so the callee reads [ptr+0] AND [ptr+4].
            //
            // This used to pass the address of a LONE int, and computed the paired cell
            // only to discard it with a (void) cast. The callee therefore read whatever
            // stack local happened to follow, and CreateNaturalResource ended up with a
            // truncated heap pointer as its cell index (measured: cellIndex=87879760),
            // faulting on grid + cell*0x30. That is the out-param-buffer-too-small trap
            // already recorded for this codebase, in its read-side form.
            int32_t cellPair[2];
            cellPair[0] = srcCellIdx;
            cellPair[1] = -1;
            if (pairedAvailable) {
                cellPair[1] = crop->slots._data[pairedIdx].cell;
            }

            // Check pathfinding
            int nearestCell = mapGrid->FindCell(srcCellIdx, citizenGridCell, 0xe0f);
            if (nearestCell < 0) {
                continue;
            }

            bool canPath = static_cast<uint8_t>(mapGrid->ValidateCell(0xe1f, citizenGridCell, nearestCell));   // the door read AL
            if (!canPath) {
                continue;
            }

            uint32_t typeConstVal = CropFieldComponent::TypeId();  // DAT_1401c7510
            ComponentEntity* entityRef = field_10.Get();

            // The slot indices the per-slot messages carry: primary, then paired
            uint32_t packedCells[2];
            packedCells[0] = (uint32_t)i;
            packedCells[1] = (uint32_t)pairedIdx;

            float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
            Math::Vector3 targetPos(
                (float)(nearestCell % map->size) - halfOffset,
                (float)(nearestCell / map->size) - halfOffset,
                0.0f);

            float pathPos[4];
            map->GetTerrainPos(pathPos, &targetPos.x, 0, nullptr);

            // Get citizen entity for action
            CitizenComponent* citizenBuf = *citizenRef;

            int harvestMode = pairedAvailable ? 2 : 1;
            uint32_t packedCellInfo = ((uint32_t)pairedIdx << 16) | (uint32_t)i;

            // Call ShowTool_Action_Ctor3 (0x1400bdd20) with 12 params
            WorkAct_ShowToolCtor3_bdd20(
                &citizenBuf,                      // RCX: citizenIBP
                field_20,                         // EDX: priority
                field_68,                         // R8: the crop (ExternalPtr)
                0,                                // R9D: 0
                pathPos,                          // [RSP+0x20]: position
                harvestMode,                      // [RSP+0x28]: 1=single, 2=paired
                cellPair,                         // [RSP+0x30]: ptr to {cell, pairedCell}
                packedCellInfo,                   // [RSP+0x38]: packed cell indices
                &entityRef,                       // [RSP+0x40]: entity ref from field_10
                &typeConstVal,                    // [RSP+0x48]: type constant
                0,                                // [RSP+0x50]: unused
                packedCells                       // [RSP+0x58]: the slot indices, one per cell
            );

            // Assign work and register with AI
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

            // Insert primary cell into the set: {cell, citizen}
            WorkCellSetNode* insertResult = nullptr;
            WorkCellAssignment insertKey;
            insertKey.cell = i;
            insertKey._pad04 = 0;
            insertKey.citizen = *citizenRef;
            WorkCellBST_Insert_9a40(&_cells, &insertResult, &insertKey);

            // Mark primary cell as used (not available)
            validityArray[i] = 0;

            // If paired, also insert and mark
            if (pairedAvailable) {
                WorkCellSetNode* insertResult2 = nullptr;
                WorkCellAssignment insertKey2;
                insertKey2.cell = pairedIdx;
                insertKey2._pad04 = 0;
                insertKey2.citizen = *citizenRef;
                WorkCellBST_Insert_9a40(&_cells, &insertResult2, &insertKey2);
                validityArray[pairedIdx] = 0;
            }

            return 2;
        }

        // If we had no cells with validity flag set (anyUnplanted still true)
        // we return 1 below
        // FIX crop_planting_exit (docs/retail_bugs.md section 11): here every slot still to plant
        // failed the reach test for this citizen. Retail returns, so a slot no worker can reach
        // keeps the field in state 9 for good; with the row on, once nothing is in flight and some
        // slot was planted, the end-of-planting branch below runs instead.
        if (!anyUnplanted) {
            const bool fixOn = Ext::Fix().crop_planting_exit;
            const int settled = fixOn ? SettledSlots(crop) : 0;
            if (!Ext::CropPlantingExit::EndPlantingOnUnreachable(fixOn, _cells._count, settled)) {
                return 1;
            }
        }
    }

    // If tree count is 0 and we exhausted all cells, mark the field's slots done
    if (_cells._count == 0) {
        crop->MarkPlantSlots();
    }

    return 1;
}





// ============================================================
// PlantArea::OnComplete [6] -- 0x1400f3f60
// Removes the set entries.
// param4 packed: low 16 = primary cell, high 16 = paired cell.
// ============================================================

int PlantArea::OnComplete(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);

    uint32_t primaryIdx = param4 & 0xFFFF;
    uint32_t pairedIdx = (param4 >> 16) & 0xFFFF;

    // Search the set for the primary cell and remove it
    WorkCellSetNode* node = _cells._root;
    while (node != nullptr) {
        if ((int)primaryIdx < node->element.cell) {
            node = node->left;
        } else if ((int)primaryIdx > node->element.cell) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // ⚠ DO NOT restore the validity byte here. Retail's slot 6 (0x1400f3f60) removes the
    // primary and paired indices from the set and touches +0x58 NOT AT ALL; it is slot 7,
    // OnCancel (0x1400f3ea0), that sets validity[idx] = 1 to put the cells back up for
    // replanting. This port had slot 7's body in slot 6, so every COMPLETED plant
    // immediately re-offered its own cell: the citizen replanted the same tile forever,
    // and CreateNaturalResource bailed "tile already holds an entity" on every retry
    // (measured: 129 plant attempts, 2 binds, the same cell 41318 every time).
    //
    // This is the identical Slot6/Slot7 swap the orchard wave already paid for — see
    // reference note "Slot6 (COMPLETED) must NOT restore the validity byte". Same bug,
    // different class, found the same way: by a counter showing work happening over and
    // over with no progress.
    //
    // Retail's return here really is 0 in the low byte (`in_RAX & ...ff00`), so
    // TryCompleteTask reporting false is faithful and was never the defect.

    // Handle paired cell (if within bounds)
    if ((int)pairedIdx < field_58._size) {
        // Remove paired cell from the set
        WorkCellSetNode* node2 = _cells._root;
        while (node2 != nullptr) {
            if ((int)pairedIdx < node2->element.cell) {
                node2 = node2->left;
            } else if ((int)pairedIdx > node2->element.cell) {
                node2 = node2->right;
            } else {
                WorkCellBST_Remove_ca60(&_cells, node2);
                break;
            }
        }
    }
    return 0;
}


// ============================================================
// PlantArea::OnCancel [7] -- 0x1400f3ea0
// Marks cells valid, removes the set entries.
// param4 packed: low 16 = primary cell, high 16 = paired cell.
// ============================================================
int PlantArea::OnCancel(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);

    uint32_t primaryIdx = param4 & 0xFFFF;
    uint32_t pairedIdx = (param4 >> 16) & 0xFFFF;

    // Search the set for the primary cell and remove it
    WorkCellSetNode* node = _cells._root;
    while (node != nullptr) {
        if ((int)primaryIdx < node->element.cell) {
            node = node->left;
        } else if ((int)primaryIdx > node->element.cell) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // Mark primary cell as valid
    bool* validityArray = field_58._data;
    validityArray[primaryIdx] = 1;

    // Handle paired cell
    if ((int)pairedIdx < field_58._size) {
        validityArray[pairedIdx] = 1;

        // Remove paired cell from the set
        WorkCellSetNode* node2 = _cells._root;
        while (node2 != nullptr) {
            if ((int)pairedIdx < node2->element.cell) {
                node2 = node2->left;
            } else if ((int)pairedIdx > node2->element.cell) {
                node2 = node2->right;
            } else {
                WorkCellBST_Remove_ca60(&_cells, node2);
                return 0;
            }
        }
    }
    return 0;
}


// ============================================================
// PlantArea::Cleanup [8] -- 0x1400f4460
// CancelAll: while the set is not empty, clear the action stack of the citizen
// of its leftmost entry.
// ============================================================
int PlantArea::Cleanup() {
    int count = _cells._count;

    while (count > 0) {
        // Find the leftmost node
        WorkCellSetNode* node = _cells._root;
        WorkCellSetNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        // Cancel work via the citizen's action stack
        CitizenComponent* citizen = leftmost->element.citizen.Get();
        citizen->character->actionStack.Clear();

        count = _cells._count;
    }
    return 0;
}


// ============================================================
// PlantArea::Validate [9] -- 0x1400f3c70
// RemoveInvalid: iterates the set, validates entries, removes dead ones.
// Marks invalid cells as valid (available for re-planting).
// ============================================================
int PlantArea::Validate() {
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
            // Invalid -- add to collect list (grow array if needed)
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
                        newArray[j] = node->element.cell;  // cell index key
                    }
                }
                if (collectArray != newArray) {
                    System::MemoryAllocator::MemFree(collectArray);
                }
                collectArray = newArray;
                collectCount = newCount;
            }

            // Mark cell as valid (available for re-planting)
            bool* validityArray = field_58._data;
            validityArray[node->element.cell] = 1;
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
    // Remove collected invalid nodes from the set
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
// PlantArea::GetTarget [10] -- 0x1400f44e0
// The field's WorkPlaceComponent's Profession, through the hidden return slot.
// ============================================================
Profession** PlantArea::GetTarget(Profession** outPtr) {
    // 0x140002e10 = FindComponent<WorkPlaceComponent>.
    WorkPlaceComponent* workplace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workplace->profession;
    return outPtr;
}


// ============================================================
// PlantArea::GetPriority [11] -- 0x140029860
// 9 bytes, shared across ClearArea/PlantArea/PlantOrchard.
// Returns (this->_cells._count > 0).
// ============================================================
int PlantArea::GetPriority() {
    return _cells._count > 0 ? 1 : 0;
}


// ============================================================
// PlantArea::Persist [3] -- 0x1400f4540
// Work::Persist + the set + count + bool array + ExternalRef.
// ============================================================
void PlantArea::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create Serializer with version=0
    IO::Serializer ser(stream, 0);

    // Serialize the set at +0x48
    WorkCellBST_Serialize_9de0(&_cells, &ser);

    // Serialize field cell count at +0x60
    int cellCountBuf[2] = { field_58._size, 0 };
    ser.Serialize(cellCountBuf[0], nullptr);

    // If reading (serializer mode == 0), resize validity array.
    // 0x14000a590 == Collection::Array<bool>::SetSize.
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        field_58.SetSize(cellCountBuf[0]);
    }

    // Serialize validity array through the BOOL-ARRAY door.
    //
    // ENDGAME B.3.4a closing — SETTLED FROM RETAIL, not from the alias, exactly
    // as batch H settled the identical bug in harvestarea.cpp. Retail
    // PlantArea::Serialize (Application-x64.exe 0x1400f4540) ends with
    //     1400f45b1  XOR R9D,R9D                    ; name = nullptr
    //     1400f45b4  MOV R8,qword ptr [RDI + 0x58]  ; the buffer
    //     1400f45b8  MOV EDX,dword ptr [RSP + 0x50] ; the count
    //     1400f45bc  LEA RCX,[RSP + 0x28]           ; the Serializer
    //     1400f45c1  CALL qword ptr [0x140151458]
    // 0x140151458 is `?Serialize@Serializer@IO@@QEAAXHPEA_NPEBD@Z`
    // (address_registry.cpp:1163), the FOUR-parameter (int n, bool*, const char*)
    // array door at rw_vtbl::kArrayBool.
    //
    // `s_fn_Serializer_SerializeBoolArray` was ALIASED onto 0x140152ac8 (that alias
    // and its door are gone with the B.3.4a closing sweep), which is the
    // THREE-parameter `Serialize(int&)` SCALAR door. Called with four arguments,
    // the element COUNT landed in RDX — the register the scalar body
    // dereferences as `int*` — so this was a wild read off a small integer, not
    // a differently-shaped write. It could never have been retail behaviour.
    ser.Serialize(cellCountBuf[0], field_58._data, nullptr);

    // Serialize ExternalRef at +0x68
    ::Object::SerializeExternal<ComponentDescription>(ser, field_68);
}   // ~Serializer


// ============================================================
// PlantArea dtor 0x1400f4600: frees the validity array and the set, then the
// Work dtor chain. The stamps and the `~Internal` call are the compiler's chain.
// ============================================================
PlantArea::~PlantArea() {
    // The +0x58 flags are the `field_58` member's ~Array (MemFree, then +0x58 = +0x60 = 0),
    // then the +0x48 set's Clear is the `_cells` member's ~Set: reverse declaration order
    // after this empty body (0x1400f4600).

}




// Ctor 0x1400f4770 (factory variant): `??0Internal`, the Work base fields, the leaf
// fields zeroed (the two stamps are the compiler's).
PlantArea::PlantArea()
    : Work(),
      field_68(nullptr) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================

// create 0x14002c470: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* PlantArea::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PlantArea), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PlantArea());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* PlantArea::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PlantArea());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(PlantArea, "Work")
