// src/game/Work/harvestarea.cpp
// Clean C++ reimplementation of HarvestArea Work class.
// Source: Game/Work/HarvestArea.cpp (from profile build debug strings)
//
// HarvestArea (0x68 bytes) -- Crop/field harvest work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> HarvestArea
// Vtable: 0x1401614a0
//
// Struct layout:
//   +0x00: vtable
//   +0x08: Work/AutoPickupWork base (0x40 bytes of fields)
//   +0x48: BST root (tree of assigned work items, int-keyed)
//   +0x50: BST count (int)
//   +0x58: validity array ptr (byte array, one per field cell)
//   +0x60: field cell count (int)
//
// Methods implemented:
//   Slot4 (0x1400ECCD0) -- main crop harvest assignment logic
//   Slot6 (0x1400ECC20) -- tree insert with paired slot removal
//   Slot7 (0x1400ECB60) -- mark cell valid, remove paired entry
//   Slot8 (0x1400ED270) -- CancelAll
//   Slot9 (0x1400EC930) -- RemoveInvalid (hook disabled, bug class 2)
//   Slot10 (0x1400ED2F0) -- GetWorkEntity
//   Serialize (0x1400ED350) -- serialization
//   ScalarDtor body (0x1400ED410) -- destructor
//

#include "Work/HarvestArea.h"
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
#include "Game/exe_constants.h"     // B3R: kRdata_140172114
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "Component/CropFieldDescription.h"   // ENDGAME B.3.9 D5: CropFieldDescription::TypeId()
#include "Component/CropFieldComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"

// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component/Tutorial/Game headers)
// ============================================================
//   AutoPickupWork::CheckAssignments (0x1400e3db0)  Work::TryAutoPickup
//   0x140003050 FindComponent WorkComponent         ComponentEntity::FindComponent<WorkComponent>
//   0x140002f90 FindComponent StatusIconComponent   ComponentEntity::FindComponent<StatusIconComponent>
//   0x140005380 FindComponent CropFieldComponent    ComponentEntity::FindComponent<CropFieldComponent>
//   0x1400052c0 FindComponent NaturalResource       ComponentEntity::FindComponent<NaturalResourceComponent>
//   0x14012c3f0 status icon flag                    StatusIconComponent::SetFlag
//   0x14012e5c0 CalcMaxWorkers                      WorkComponent::CalcMaxWorkers
//   0x14009ccf0 Map_FindNearestPassable             WorkGrid_FindCell_9ccf0
//   0x1400a00f0 Map_CanPathTo                       WorkBase_ValidateCell_a00f0
//               (a uint64 whose low byte is the bool; read through uint8_t)
//   0x14009f3e0 GetTerrainPos                       Reimpl_MapGetTerrainPos
//   0x1400c1530 harvest action chain ctor           WorkAct_HarvestAreaCtor_c1530
//   0x140029a40 / 0x14001ca60 / 0x140029de0 cell BST insert / remove / serialize
//                                                   WorkCellBST_Insert_9a40 / _Remove_ca60 / _Serialize_9de0
//   0x1400f7100 ValidateTask                        Work::ValidateTask
//   0x1400c5400 cancel work                         WorkBase_TaskListCleanup_c5400
//   0x140002e10 FindComponent WorkPlaceComponent    ComponentEntity::FindComponent<WorkPlaceComponent>
//   0x14000e320 BST recursive free                  WorkBase_TreeNodeFree_e320
//   MemAlloc                                        System::MemoryAllocator::MemAlloc
//   DAT_140172114 = 0.5f                            kRdata_140172114
// 0x14000a590 (Array<bool>::SetSize) is `_slotsAvailable.SetSize`.


// ============================================================
// Helper: the claim tree searched by the int slot key
// ============================================================
static HarvestArea::ClaimNode* BST_Find(HarvestArea::ClaimNode* root, int key) {
    HarvestArea::ClaimNode* node = root;
    while (node != nullptr) {
        if (key < node->element.slot) {
            node = node->left;
        } else if (key > node->element.slot) {
            node = node->right;
        } else {
            return node;  // found
        }
    }
    return nullptr;
}


// ============================================================
// HarvestArea::Slot4 -- Main crop harvest assignment logic
// ============================================================
//
// Algorithm:
// 1. Check auto-pickup assignments
// 2. Find the WorkComponent and the CropFieldComponent on the workplace entity
// 3. Check capacity
// 4. Iterate field cells (0..cellCount), find unharvested crops
// 5. For each valid cell: check pathing, check NaturalResource status
// 6. If pairable cell available, harvest two at once; else harvest one
// 7. Create harvest action, assign work, register with citizen AI
//
// Harvest-assignment census counters. State 11 posts this work and its Execute runs
// every frame; "runs but produces nothing" has EIGHT possible causes here and they need
// different fixes, so each is counted separately. Two of them (atCapacity, workerCap)
// return before the cell loop is even reached.
static long long g_ha_calls = 0, g_ha_auto = 0, g_ha_cap = 0, g_ha_wcap = 0,
                 g_ha_nomark = 0, g_ha_inbst = 0, g_ha_noent = 0;
[[maybe_unused]] static long long g_ha_nopass = 0;
[[maybe_unused]] static long long g_ha_nopath = 0, g_ha_ok = 0;

int HarvestArea::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // Harvest-assignment census. State 11 posts this work; whether a citizen ever
    // ACCEPTS a harvest cell is the last unmeasured link before a deposit, and an
    // Execute that finds nothing looks exactly like an Execute that is never called.
    {
        ++g_ha_calls;
    }

    CitizenComponent* citizenRef = *citizenIBP;

    // Step 1: Check auto-pickup (0x1400e3db0, Work::TryAutoPickup)
    int checkResult = TryAutoPickup(&citizenRef);
    if (checkResult == 2) {
        ++g_ha_auto;
        return 2;
    }

    // Step 2: Find components (0x140003050 WorkComponent, 0x140005380 CropFieldComponent)
    WorkComponent* matDesc = _entity->FindComponent<WorkComponent>();

    CropFieldComponent* cropField = _entity->FindComponent<CropFieldComponent>();

    // The crop field's description
    Description* descObj = cropField->_description;
    bool isDerived = Object::IsDerivedFromNullSafe(descObj, CropFieldDescription::TypeId());
    CropFieldDescription* cropDesc = isDerived ? static_cast<CropFieldDescription*>(descObj) : nullptr;

    // Capacity: the town's per-resource counts (CitizenBehavior +0x170) for the crop's
    // material
    int materialIdx = (int)cropDesc->_type;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    const ProfessionResourceEntry& resEntry = gameWorld->m_citizenList->professionResources._data[materialIdx];
    bool atCapacity = resEntry.maxCount <= resEntry.currentCount;

    // The status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
    StatusIconComponent* statusIcon = _entity->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);

    if (atCapacity) {
        ++g_ha_cap;
        return 1;
    }

    // Check tree count vs max workers (the tree's port of 0x14012e5c0 called here takes
    // the component as an integer: LEFT)
    int treeCount = _cells._count;
    if (matDesc->workState == 0) {
        int maxWorkers = matDesc->CalcMaxWorkers();
        matDesc->workState = maxWorkers;
    }
    if (treeCount > matDesc->workState) {
        ++g_ha_wcap;
        return 1;
    }

    // Step 3: Setup iteration
    Map* map = gameWorld->m_game;
    // The map helpers (0x14009ccf0, 0x1400a00f0) take the map as int*: LEFT.
    Map* mapGrid = map;

    // Get citizen position
    const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

    int mapSz = map->size;
    int citizenCellX = (int)(citizenPos.x + (float)(mapSz >> 1));
    int citizenCellY = (int)(citizenPos.y + (float)(mapSz >> 1));
    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSz) citizenCellX = mapSz - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSz) citizenCellY = mapSz - 1;
    uint32_t citizenGridCell = mapSz * citizenCellY + citizenCellX;

    int fieldCellCount = _slotsAvailable._size;

    if (fieldCellCount > 0) {
        int cellIdx2 = 1; // paired cell index starts at 1
        for (int i = 0; i < fieldCellCount; i++) {
            // Check if this cell's validity flag is set
            bool* validityArray = _slotsAvailable._data;
            if (validityArray[i] == 0) {
                ++g_ha_nomark;
                goto next_cell;
            }

            // Check if cell index is already in the claim tree (already assigned)
            {
                ClaimNode* found = BST_Find(_cells._root, i);
                if (found != nullptr) {
                    ++g_ha_inbst;
                    goto next_cell;
                }
            }

            // Check if paired cell is also available
            {
                bool pairedAvailable = false;
                if (cellIdx2 < fieldCellCount && validityArray[cellIdx2] != 0) {
                    ClaimNode* found2 = BST_Find(_cells._root, cellIdx2);
                    if (found2 == nullptr) {
                        pairedAvailable = true;
                    }
                }

                // Look up the occupant of this slot's cell
                ComponentEntity* entityAtCell1 = nullptr;
                ComponentEntity* entityAtCell2 = nullptr;

                MapCell* gridData = map->cells.GetData();
                int cellGridIdx = cropField->slots._data[i].cell;
                entityAtCell1 = gridData[cellGridIdx].occupant.Get();

                if (entityAtCell1 == nullptr) {
                    // No entity at cell -- mark as unavailable
                    ++g_ha_noent;
                    validityArray[i] = 0;
                } else {
                    if (pairedAvailable) {
                        int cellGridIdx2 = cropField->slots._data[cellIdx2].cell;
                        entityAtCell2 = gridData[cellGridIdx2].occupant.Get();
                        if (entityAtCell2 == nullptr) {
                            pairedAvailable = false;
                        }
                    }

                    // Check pathfinding from cell to citizen
                    int srcCellIdx = cropField->slots._data[i].cell;
                    int nearestCell =
                        mapGrid->FindCell(srcCellIdx, citizenGridCell, PathBits::Walk & ~PathBits::Obstacle);

                    if (nearestCell >= 0) {
                        uint64_t canPath = (uint64_t)static_cast<uint8_t>(mapGrid->ValidateCell(PathBits::Walk, citizenGridCell, nearestCell));   // the bool door read AL
                        if ((char)canPath != 0) {
                            // Check NaturalResourceComponent on entity
                            // natRes must be contiguous: the action ctor 0x1400c1530
                            // iterates param_4 as an array of harvestMode entries
                            NaturalResourceComponent* natRes[2] = {nullptr, nullptr};
                            NaturalResourceComponent*& natRes1 = natRes[0];
                            NaturalResourceComponent*& natRes2 = natRes[1];

                            natRes1 = entityAtCell1->FindComponent<NaturalResourceComponent>();

                            if (pairedAvailable) {
                                natRes2 = entityAtCell2->FindComponent<NaturalResourceComponent>();
                            }

                            // Check flag bit 20 on NatRes (flagged for removal): natRes1 is
                            // read with no test (0x1400ed082 MOV RAX,[RBP-0x29] / [RAX+0x20]);
                            // natRes2's null test below is retail's (0x1400ed0e9).
                            if ((natRes1->_flags >> 20) & 1) {
                                goto handle_removal;
                            }

                            if (natRes2 != nullptr) {
                                if (natRes2 != nullptr && (natRes2->_flags >> 20) & 1) {
                                    pairedAvailable = false;
                                }
                            }

                            if (!pairedAvailable) {
                                // Single cell harvest -- need cell count for packed info
                                cellIdx2 = fieldCellCount;
                            }

                        handle_removal:
                            if (!((natRes1->_flags >> 20) & 1)) {
                                // Valid cell -- create harvest action
                                float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
                                Math::Vector3 targetPos(
                                    (float)(nearestCell % map->size) - halfOffset,
                                    (float)(nearestCell / map->size) - halfOffset,
                                    0.0f);

                                float pathPos[4]; // extra padding for GetTerrainPos
                                map->GetTerrainPos(pathPos, &targetPos.x, 0, nullptr);

                                // Create the harvest action
                                CitizenComponent* actionBuf[2] = { *citizenIBP, nullptr };

                                int harvestMode = pairedAvailable ? 2 : 1;
                                uint32_t packedCellInfo = ((uint32_t)cellIdx2 << 16) | (uint32_t)i;

                                WorkAct_HarvestAreaCtor_c1530(
                                    actionBuf,                    // citizenIBP
                                    _workId,                     // priority
                                    harvestMode,                  // 1=single, 2=paired
                                    natRes,                       // target NatRes array (harvestMode entries)
                                    pathPos,                      // position
                                    packedCellInfo,               // packed cell indices
                                    0);                           // param7

                                // Assign work and register with AI
                                int priority = _workId;
                                CitizenComponent* citizenEnt = *citizenIBP;

                                Profession* workEnt = nullptr;
                                GetTarget(&workEnt);

                                citizenEnt->statusFlags |= Bits(CitizenStatus::TaskDone);
                                citizenEnt->lastWorkId = priority;
                                citizenEnt->lastErrand = CitizenErrand::None;

                                if (workEnt == citizenEnt->profession && priority >= 0) {
                                    citizenEnt->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                                }

                                // Insert {cell, citizen} into the claim tree (the helper
                                // takes the container as uintptr_t*: LEFT)
                                ClaimNode* insertResult[2] = {nullptr, nullptr};
                                Claim keyBuf;
                                keyBuf.slot = i;
                                keyBuf._pad04 = 0;
                                keyBuf.citizen = *citizenIBP;
                                WorkCellBST_Insert_9a40(&_cells, insertResult, &keyBuf);

                                // Mark cell as used
                                validityArray[i] = 0;

                                // If paired, also insert and mark
                                if (pairedAvailable) {
                                    ClaimNode* insertResult2[2] = {nullptr, nullptr};
                                    Claim keyBuf2;
                                    keyBuf2.slot = cellIdx2;
                                    keyBuf2._pad04 = 0;
                                    keyBuf2.citizen = *citizenIBP;
                                    WorkCellBST_Insert_9a40(&_cells, insertResult2, &keyBuf2);
                                    validityArray[cellIdx2] = 0;
                                }

                                return 2;
                            }
                        }
                    }

                    // Restore citizenGridCell for next iteration
                    citizenGridCell = (uint32_t)(mapSz * citizenCellY + citizenCellX);
                }
            }

        next_cell:
            cellIdx2 = i + 2; // paired cell is always i+1 in iteration
            // Note: cellIdx2 tracks as fVar13 in decompilation (i+1 each iteration)
        }
    }

    return 1;
}


// Shared by slots 6 and 7: find `key` in the claim tree at +0x48 and remove it if
// present (the helper takes the container as uintptr_t*: LEFT).
static void HarvestArea_RemoveCellNode(HarvestArea* self, int key) {
    HarvestArea::ClaimNode* node = self->_cells._root;
    while (node != nullptr) {
        if (key < node->element.slot) {
            node = node->left;
        } else if (key > node->element.slot) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&self->_cells, node);
            return;
        }
    }
}


// ============================================================
// HarvestArea::Slot6 -- task COMPLETED (retail 0x1400ecc20, vtable +0x30)
// ============================================================
// Removes the primary cell's node, then (if the paired index is in range) the paired
// cell's node. param4 is packed: low 16 = primary cell, high 16 = paired cell.
//
// ⚠ Retail touches the validity array (+0x58) NOT AT ALL here: a harvested cell is
// gone and must not be re-offered. Only the CANCEL slot below restores validity.
// This port had the two slots' bodies mixed: slot 6 carried slot 7's validity writes
// (harvested cells re-offered, Slot4 then found no entity and re-cleared them -- a
// wasted scan per completion) and slot 7 LACKED the primary-node removal (see below).
// Same Slot6/Slot7 mix-up HarvestOrchard and PlantArea each paid for once already.
int HarvestArea::OnComplete(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    int primaryIdx = (int)(param4 & 0xFFFF);
    int pairedIdx  = (int)((param4 >> 16) & 0xFFFF);

    HarvestArea_RemoveCellNode(this, primaryIdx);
    if (pairedIdx < _slotsAvailable._size) {
        HarvestArea_RemoveCellNode(this, pairedIdx);
    }
    return 0;
}


// ============================================================
// HarvestArea::Slot7 -- task CANCELLED (retail 0x1400ecb60, vtable +0x38)
// ============================================================
// Removes the primary cell's node, marks the primary cell valid again, and (if the
// paired index is in range) marks the paired cell valid and removes its node too.
// param4 is packed: low 16 = primary cell, high 16 = paired cell.
//
// ⚠ THE PRIMARY-NODE REMOVAL IS LOAD-BEARING. Retail 0x1400ecb60 walks the tree for
// the primary key and calls ca60 on the hit BEFORE the validity writes. This port only
// removed the PAIRED node, so every cancelled harvest task left its primary
// {cell, citizen} node in the tree with the count at +0x50 still counting it:
//   * Slot9 (Validate) later handed that citizen pointer to Object::IsA. Once the
//     citizen had died and its block been recycled, IsA CALLed through recycled bytes
//     (the "READ of vtable+0x10" crash with RAX = 0000079700000552 -- a 40-byte record
//     array that reused the freed entity block; CheckPtr is a bare RET in the shipped
//     Runtime, so nothing nulls a dangling entry).
//   * Slot8 (CancelAll, teardown) picks the leftmost node, cancels that citizen's
//     action list and expects the cancel to reach this slot and drop the count. With
//     the node never removed the count never drops and it re-picks the same node
//     forever: the 100%-CPU quit hang after the [Residence] ERASE burst.
// Both symptoms are this one missing removal (2026-09-05).
int HarvestArea::OnCancel(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    int primaryIdx = (int)(param4 & 0xFFFF);
    int pairedIdx  = (int)((param4 >> 16) & 0xFFFF);

    HarvestArea_RemoveCellNode(this, primaryIdx);

    // Cancelled: the cell goes back in the pool.
    bool* validityArray = _slotsAvailable._data;
    validityArray[primaryIdx] = 1;

    if (pairedIdx < _slotsAvailable._size) {
        validityArray[pairedIdx] = 1;
        HarvestArea_RemoveCellNode(this, pairedIdx);
    }
    return 0;
}


// ============================================================
// HarvestArea::Slot8 -- CancelAll
// ============================================================
// Loops through the claim tree, cancels each work item via citizen AI task cleanup.
// The walk follows the LEFT child (+0x18 in the 0x30 node); the tree called the node
// "rightmost".
int HarvestArea::Cleanup() {
    int count = _cells._count;

    while (count > 0) {
        ClaimNode* node = _cells._root;
        ClaimNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        // Cancel work via citizen AI
        CitizenComponent* citizenEntity = leftmost->element.citizen.Get();
        citizenEntity->character->actionStack.Clear();

        // 0x1400ed2cb CMP [RDI+0x50],0 / JG: loop while the count is positive, with no
        // progress test (a cancel that does not drop the count spins, as retail).
        count = _cells._count;
    }
    return 0;
}


// ============================================================
// HarvestArea::Slot9 -- RemoveInvalid
// ============================================================
// Hook DISABLED (bug class 2: MOV RAX,RSP in prologue).
// Implemented for standalone exe.
//
// Iterates the claim tree in-order, validates each entry via FUN_1400f7100,
// collects invalid ones, then removes them from the tree.
int HarvestArea::Validate() {
    int* collectArray = nullptr;
    int* currentArray = nullptr;
    uint32_t collectCount = 0;
    uint32_t collectCapacity = 0;

    // In-order traversal of the claim tree
    ClaimNode** root = &_cells._root;
    ClaimNode* node = *root;

    if (node == nullptr) {
        goto cleanup;
    }

    // Find leftmost node
    {
        ClaimNode* cur = node;
        while (cur != nullptr) {
            node = cur;
            cur = cur->left;
        }
    }

    // Traverse in-order
    while (node != nullptr) {
        CitizenComponent* ibpData = node->element.citizen.Get();

        uint8_t valid = ValidateTask(nullptr, &ibpData, nullptr);

        if (valid == 0) {
            // Invalid -- add to collect list
            uint32_t newCount = collectCount + 1;
            if (newCount != collectCount) {
                if ((int)collectCapacity < (int)newCount) {
                    uint32_t newCap = newCount;
                    if (collectCount != 0) {
                        newCap = newCount * 3;
                    }
                    collectCapacity = newCap;
                    int* newArray = static_cast<int*>(System::MemoryAllocator::MemAlloc((int64_t)newCap << 2, 4));
                    currentArray = newArray;

                    uint32_t copyStart = 0;
                    if (collectArray == newArray) {
                        copyStart = collectCount;
                    }
                    for (uint32_t j = copyStart; j < newCount; j++) {
                        if (j < collectCount) {
                            newArray[j] = collectArray[j];
                        } else {
                            newArray[j] = node->element.slot;
                        }
                    }
                    if (collectArray != newArray) {
                        System::MemoryAllocator::MemFree(collectArray);
                    }
                    collectArray = newArray;
                } else {
                    currentArray = collectArray;
                    collectArray[collectCount] = node->element.slot;
                }
                collectCount = newCount;
            }

            // Mark validity flag
            bool* validityArray = _slotsAvailable._data;
            validityArray[node->element.slot] = 1;
        }

        // Move to next in-order node
        ClaimNode* right = node->right;
        if (right != nullptr) {
            // Go to leftmost of right subtree
            ClaimNode* cur = right;
            while (cur != nullptr) {
                node = cur;
                cur = cur->left;
            }
        } else {
            // Go up until we find a node we're the left child of
            ClaimNode* parent = node->parent;
            if (parent != nullptr) {
                if (parent->left != node) {
                    // Not left child -- go up
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
    // Remove collected invalid nodes from the tree (the helper takes the container as
    // uintptr_t*: LEFT)
    if ((int)collectCount > 0) {
        for (uint32_t k = 0; k < collectCount; k++) {
            ClaimNode* bstNode = *root;
            while (bstNode != nullptr) {
                if (collectArray[k] < bstNode->element.slot) {
                    bstNode = bstNode->left;
                } else if (collectArray[k] > bstNode->element.slot) {
                    bstNode = bstNode->right;
                } else {
                    WorkCellBST_Remove_ca60(&_cells, bstNode);   // root == &_cells._root
                    break;
                }
            }
        }
    }

    System::MemoryAllocator::MemFree(currentArray ? currentArray : collectArray);
    return collectCount == 0 ? 1 : 0;
}


// ============================================================
// HarvestArea::Slot10 -- GetWorkEntity: the entity's WorkPlaceComponent (0x140002e10),
// its profession (+0x48) into the return slot.
// ============================================================
Profession** HarvestArea::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// HarvestArea::Serialize
// ============================================================
// Work::Serialize + version block + BST serialize + array + int
void HarvestArea::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Version block with no extra data
    {
        IO::Serializer serializer(stream, 0);
    }

    // Serializer (version=0) for our data
    IO::Serializer serializer2(stream, 0);

    // Serialize the claim tree at +0x48 (the helper takes the container as uintptr_t*:
    // LEFT)
    WorkCellBST_Serialize_9de0(&_cells, &serializer2);

    // Serialize field cell count at +0x60
    int cellCount = _slotsAvailable._size;
    int cellCountBuf[2] = { cellCount, 0 };
    serializer2.Serialize(cellCountBuf[0], nullptr);

    // If reading (serializer mode == 0), resize validity array.
    // 0x14000a590 == Collection::Array<bool>::SetSize.
    if (static_cast<int>(serializer2.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        _slotsAvailable.SetSize(cellCountBuf[0]);
    }

    // Serialize validity array through the BOOL-ARRAY door.
    //
    // ENDGAME B.3.4 callers batch H — SETTLED FROM RETAIL, not from the alias.
    // Retail HarvestArea::Serialize (Application-x64.exe 0x1400ed350) ends with
    //     1400ed3dd  XOR R9D,R9D                    ; name = nullptr
    //     1400ed3e0  MOV R8,qword ptr [RDI + 0x58]  ; the buffer
    //     1400ed3e4  MOV EDX,dword ptr [RSP + 0x60] ; the count
    //     1400ed3e8  LEA RCX,[RSP + 0x28]           ; the Serializer
    //     1400ed3ed  CALL qword ptr [0x140151458]
    // 0x140151458 is `?Serialize@Serializer@IO@@QEAAXHPEA_NPEBD@Z`
    // (address_registry.cpp:1163), the four-parameter (int n, bool*, const char*)
    // array door at rw_vtbl::kArrayBool. It is NOT 0x140152ac8, which is what the
    // old `s_fn_Serializer_SerializeBoolArray` alias (removed with its door in the
    // B.3.4a closing sweep) pointed at — that is the THREE-parameter
    // `Serialize(int&)` scalar door, so the old call put the COUNT in the register
    // the scalar body dereferences as `int*`.
    serializer2.Serialize(cellCountBuf[0], _slotsAvailable._data, nullptr);   // slot 0x140151458 = bool-array door
}


// The HarvestArea / AutoPickupWork / Work stamps and the `~Internal` call are the
// compiler's chain.
HarvestArea::~HarvestArea() {
    // The +0x58 flags are the `_slotsAvailable` member's ~Array (MemFree, then +0x58 = +0x60 = 0),
    // then the +0x48 set's Clear is the `_cells` member's ~Set: reverse declaration order
    // after this empty body (0x1400ed410).
}



// [11] 0x140029860 (shared by 13 tables): `+0x50 > 0` — "is anyone assigned". The
// old table pointed at Work_Slot11_HasActiveTasks, the same body.
int HarvestArea::GetPriority() {
    return _cells._count > 0 ? 1 : 0;
}

// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
HarvestArea::HarvestArea()
    : AutoPickupWork() {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_HarvestArea_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over HarvestArea_Factory / _CreateInPlace (both
// Work_GenericCtor), plus three vtable-pointer statics for the dtor's stamps.
// create 0x14002a460: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* HarvestArea::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HarvestArea), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HarvestArea());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* HarvestArea::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HarvestArea());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(HarvestArea, "AutoPickupWork")
