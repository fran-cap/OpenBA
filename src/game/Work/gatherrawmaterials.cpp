// src/game/Work/gatherrawmaterials.cpp
// Clean C++ reimplementation of GatherRawMaterials Work class.
// Source: Game/Work/GatherRawMaterials.cpp (from profile build debug strings)
//
// GatherRawMaterials -- raw material gathering work. Size = 0x78.
// Inherits: Object::Internal -> Work (0x48) -> GatherRawMaterials
// Ctor: 0x1400ec8a0, Vtable: 0x140161420
//
// Methods implemented:
//   Serialize (vtable slot 3, 0x1400ec560)
//   Execute   (vtable slot 4, 0x1400ebe40)
#include "Work/exe_bodies.h"
//   OnComplete(vtable slot 6, 0x1400ebdf0)
//   OnCancel  (vtable slot 7, 0x1400ebdb0)
//   Cleanup   (vtable slot 8, 0x1400ec480)
//   Validate  (vtable slot 9, 0x1400ebb50)
//   GetTarget (vtable slot 10, 0x1400ec510)
//

#include "options.h"
#include "Work/GatherRawMaterials.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/WorkScheduler.h"      // the harness entry reads the scheduler clock (+0x130)
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"          // B3R: bodies called by name
#include "Component/exe_bodies.h"       // B3R: bodies called by name
#include "Dialog/dialog_exe_helpers.h"  // B3R: Dialog_FindComponent_d6c0
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "Component/BuildDescription.h"   // ENDGAME B.3.8 CB3: BuildDescription::TypeId()
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/InteractDescription.h"   // ENDGAME B.3.9 D5: InteractDescription::TypeId()
#include "Component/InteractComponent.h"
#include "Component/BuildComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/StorageComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "Game/ProfessionList.h"
#include "Action/PathSearch.h"   // RawArray (PickBestDropTarget)


// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component/Dialog headers)
// ============================================================
//   0x1400296b0 simple-BST serialize (CitizenAI_impl_96b0)  WorkBST_BST_Serialize_296b0
//               (0x140025630, used earlier, is MID-FUNCTION inside TradeUI_impl_55a0;
//                GatherRawMaterials_Shutdown@0x1400ec62a CALLs 0x1400296b0)
//   0x140025520 tree remove by key                          WorkBST_BSTFindRemove_25520
//   0x14001a6e0 tree remove node (CitizenAI_impl_a6e0)      WorkBST_BSTRemove_a6e0
//               (0x140024f30, used earlier, is MID-FUNCTION inside Toolbar_vslot14;
//                GatherRawMaterials_Update@0x1400ebd24 CALLs 0x14001a6e0)
//   0x1400c5400 state machine release                       WorkBase_TaskListCleanup_c5400
//   0x1400f7100 Work::CanStartWork                          Work::ValidateTask
//   0x140002ed0 Entity_FindComponent_Position               WorkBase_FindComponent_2ed0
//   0x14000d6c0 FindComponent (BuildComponent)              Dialog_FindComponent_d6c0
//   0x140002e10 FindComponent_WorkplaceExt                  AttendanceComponent_GetJobHostComponent
//   0x14008d590 FindStorageForResource                      Pf_PickBestDropTarget_8d590
//   0x140006270 Entity_FindComponent_Storage                WorkBase_FindComponent_6270
//   0x14012cf10 StorageComponent_QueryCount                 StorageComponent::QueryCount (the member)
//   0x140116540 GetRemainingCapacity                        WorkLeaf_FreeStorageCapacity_116540
//   0x140115e60 CitizenAI_State_LongTermPlan                CitizenAI_State_LongTermPlan
//   0x140122be0 CalcBestTargetPos                           WorkImpl_WalkablePos_122be0
//   0x140122b00 CalcChildTargetPos                          WorkImpl_WalkablePosRecursive_122b00
//   0x1400f6ef0 BuildStructure_impl_6ef0 validate           WorkImpl_BuildStructureStep_6ef0
//   0x1400bf790 Pathfinding_FindPath                        WorkAct_GatherRawMatCtor_bf790
//   0x140021a30 RegisterWork (CitizenAI_impl_1a30)          WorkBST_RegisterWork_1a30
//   0x140014c30 tree node cleanup                           WorkBase_TreeNodeFree_14c30
//   MemAlloc / MemFree                                      System::MemoryAllocator::MemAlloc / MemFree
// B3R_RULES R4: the old code resolved MemAlloc/MemFree by export name and fell back to
// import slots 0x1401527e8 / 0x1401527f0 under the WRONG names (they are the
// Particle::_rng data import and Vector3::Lerp). The export was always found, so the
// fallback never ran; it is deleted, never registered.
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: GatherRawMaterials_Ctor
// disassembles to `0x1400ec8bd: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// (0x1401612e0, used earlier, is an INTERIOR address of FireWork's vtable.)

// Gated Execute-decision trace (BANISHED_BUILD_TRACE), to pinpoint where a construction
// gather work bails before enqueuing a real town->site haul. Diagnostic only.
#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================
// Helper: the RawMaterialDescription among a ComponentDescription's sub-descriptions
// (+0x28 data / +0x30 count) -- the inlined scan retail repeats (no CheckPtr).
// ============================================================
static RawMaterialDescription* FindRawMaterialInList(ComponentDescription* entity) {
    const uint32_t typeId = RawMaterialDescription::TypeId();
    int count = entity->_subDescCount;
    for (int i = 0; i < count; i++) {
        Description* obj = entity->_subDescs[i];
        if (obj->Type() == typeId) {
            Description* comp = entity->_subDescs[i];
            if (Object::IsDerivedFromNullSafe(comp, typeId)) {
                return static_cast<RawMaterialDescription*>(comp);
            }
            return nullptr;
        }
    }
    return nullptr;
}

// ============================================================
// Serialize (0x1400ec560)
// Work::Serialize + 2 int arrays + tree serialization
// ============================================================
void GatherRawMaterials::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create IO::Serializer with version 0
    IO::Serializer serializer(stream, 0);

    // Check if loading (stream+8 == 0 means loading)
    bool isLoading = (static_cast<int>(serializer.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210

    // --- Serialize first int array count (+0x50) ---
    int count50 = _present._size;
    serializer.Serialize(count50, nullptr);

    if (isLoading) {
        // FUN_1400021d0 == Collection::Array<int32>::SetSize (retail
        // GatherRawMaterials_Shutdown calls it at 1400ec5be, the member's own SetSize; no init arm) — a grown tail
        // is left as the allocation found it, like retail. Safe: load-only, and the bulk
        // int read below writes all count50 elements from the stream.
        _present.SetSize(count50);
        _present._size = count50;
    }

    // Bulk-serialize the first int array
    serializer.Serialize(_present._size, _present._data, nullptr);

    // --- Serialize second int array count (+0x60) ---
    int count60 = _inFlight._size;
    serializer.Serialize(count60, nullptr);

    if (isLoading) {
        // FUN_1400021d0 == Collection::Array<int32>::SetSize (retail
        // GatherRawMaterials_Shutdown calls it at 1400ec606, the member's own SetSize; no init arm) — a grown tail
        // is left as the allocation found it, like retail. Safe: load-only, and the bulk
        // int read below writes all count60 elements from the stream.
        _inFlight.SetSize(count60);
        _inFlight._size = count60;
    }

    // Bulk-serialize the second int array
    serializer.Serialize(_inFlight._size, _inFlight._data, nullptr);

    // --- Serialize the citizen set at +0x68 ---
    WorkBST_BST_Serialize_296b0(&_citizens, &serializer);
}

// ============================================================
// Execute (0x1400ebe40) -- vtable slot 4
//
// Main gather logic. Iterates material description slots to find
// resources that need gathering. For each slot with capacity, searches
// for a storage entity with the resource, calculates path, and assigns
// the citizen to fetch it.
//
// Returns: 0=fail, 1=retry/done, 2=action pushed.
// ============================================================
int GatherRawMaterials::Execute(CitizenComponent** entityIBP, System::Time* context) {
    // --- Find the InteractComponent on the work entity (self+0x10) (0x140002ed0) ---
    InteractComponent* posComp = field_10->FindComponent<InteractComponent>();

    // --- Find the BuildComponent on the work entity (0x14000d6c0) ---
    BuildComponent* descComp = field_10->FindComponent<BuildComponent>();

    // Its BuildDescription (+0x18)
    Description* descBase = descComp->_description;
    bool isDerived = Object::IsDerivedFromNullSafe(descBase, BuildDescription::TypeId());
    BuildDescription* gatherDesc = isDerived ? static_cast<BuildDescription*>(descBase) : nullptr;

    // --- Check the workplace enable flag on the site's WorkPlaceComponent (0x140002e10;
    // its out slot is the R9 home [RBP+0x7f], 0x1400ebee4) ---
    WorkPlaceComponent* workPlace = descComp->_entity->FindComponent<WorkPlaceComponent>();
    if (workPlace->isAccepting == 0) {
        return 1;  // Workplace disabled
    }

    // --- Get game world data ---
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* map = gameWorld->m_game;
    CitizenBehavior* worldEntityList = gameWorld->m_citizenList;

    // --- Get citizen position for cell calculation: CharacterComponent (+0x278) ->
    // ModelComponent (+0x68) -> the transform's translation ---
    const Math::Vector3& citizenPos = (*entityIBP)->character->modelComponent->transform.translation;

    int mapSize = map->size;
    int halfMap = mapSize >> 1;
    int cellX = (int)(citizenPos.x + (float)halfMap);
    int cellY = (int)(citizenPos.y + (float)halfMap);
    if (cellX < 0) cellX = 0;
    if (cellX >= mapSize) cellX = mapSize - 1;
    if (cellY < 0) cellY = 0;
    if (cellY >= mapSize) cellY = mapSize - 1;
    uint32_t citizenCell = (uint32_t)(cellY * mapSize + cellX);

    // --- Iterate material slots ---
    int numSlots = _present._size;
    if (numSlots < 1) {
        return 1;
    }

    for (int slotIdx = 0; slotIdx < numSlots; slotIdx++) {
        // The BuildDescription cost entry for this slot
        const BuildCost* cost = &gatherDesc->_costs[slotIdx];

        // Calculate target amount: material quantity * the BuildComponent's cost multiplier
        int targetAmount = cost->quantity * descComp->costMultiplier;


        // Check: buildCount[slotIdx] + _present[slotIdx] < targetAmount
        int buildCount = _inFlight[slotIdx];
        int assignCount = _present[slotIdx];
        if (buildCount + assignCount >= targetAmount) {
            // This slot is already fulfilled
            goto next_slot;
        }

        {
            // --- Find the RawMaterialDescription of the cost's material ---
            RawMaterialDescription* rawMatComp = FindRawMaterialInList(cost->material);

            // --- Search for a storage entity with this resource ---
            ComponentEntity* foundEntity = nullptr;
            float searchPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Object::InternalPtr<ComponentEntity> excludeIBP = nullptr;  // Original: local null-initialized IBP (NOT &self->field_10)

            worldEntityList->PickBestDropTarget(
                &foundEntity, citizenCell,
                cost->material, 1, '\0', searchPos,
                &excludeIBP);   // 0x14008d590

            if (foundEntity == nullptr) {
                // No storage found for this slot
                goto next_slot;
            }

            {
                // --- Calculate how much to gather ---
                int remaining = targetAmount - buildCount - assignCount;

                // Check citizen's carrying capacity (its entity's StorageComponent)
                StorageComponent* citizenStorage = (*entityIBP)->_entity->FindComponent<StorageComponent>();

                int capacity = citizenStorage->capacity;
                int currentStored = citizenStorage->QueryCount();


                int carryWeight = rawMatComp->_weight;
                if (capacity - currentStored < carryWeight * remaining) {
                    // Not enough carry capacity -- recalculate (0x140116540 takes the
                    // citizen as an integer: LEFT)
                    int remCapacity = (*entityIBP)->FreeStorageCapacity();
                    remaining = remCapacity / carryWeight;
                }

                if (remaining == 0) {
                    // Can't carry anything -- replan
                    (*entityIBP)->LongTermPlan();
                    return 2;
                }

                // --- Find storage on the found entity and match material slot ---
                StorageComponent* storageComp = foundEntity->FindComponent<StorageComponent>();
                int storageSlotCount = storageComp->storageSlots._size;
                int availableAmount = 0;

                // Search storage slots for matching material type
                // R8D=slotIdx starts at 0, increments on miss, used on match
                if (storageSlotCount > 0) {
                    const StorageComponent::Item* slotArray = storageComp->storageSlots._data;
                    for (int si = 0; si < storageSlotCount; si++) {
                        if (slotArray[si]._resource == cost->material) {
                            if (si >= 0) {  // R8D >= 0 (always true for unsigned, but matches original TEST R8D,R8D / JS)
                                availableAmount = slotArray[si]._count - slotArray[si]._reserve;
                            }
                            break;
                        }
                    }
                }

                // Clamp remaining to available
                if (availableAmount < remaining) {
                    remaining = availableAmount;
                }

                // --- Calculate target position ---
                float targetPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};

                Description* posDescBase = posComp->_description;
                int bestDist = 0x7fffffff;

                // DAT_1401c7938 = InteractDescription (ENDGAME B.3.9 D5: by cell -- the
                // registered cell's own class, not the legacy healing fallback which used
                // to read MapDescription::TypeId()).
                bool isDerivedPos = Object::IsDerivedFromNullSafe(posDescBase, InteractDescription::TypeId());
                InteractDescription* posDesc = isDerivedPos ? static_cast<InteractDescription*>(posDescBase) : nullptr;
                uint8_t found = posDesc ? posComp->FindWalkablePos(
                    &posDesc->_buildNodes, searchPos, targetPos, &bestDist) : 0;

                if (posComp->useLocations._size > 0) {
                    int childCount = 0;
                    do {
                        uint8_t childFound = posComp->useLocations[childCount].Get()->FindWalkablePosRecursive(1,
                            searchPos, targetPos, &bestDist);
                        childCount++;
                        found |= childFound;
                    } while (childCount < posComp->useLocations._size);
                }

                if (found == 0) {
                    // CalcBestTargetPos (0x140122be0) walks posDesc+0x38: {ptr, count} of
                    // local-space access offsets, and returns false when the count is 0 or
                    // every offset fails the 0xe1f walkability probe. The child loop above
                    // is the multi-part fallback (posComp+0x28/+0x30). Log both counts so a
                    // "site is unreachable" bail is distinguishable from "site has no
                    // access points at all".
                    return 1;
                }

                // --- Convert target position to grid cell ---
                int mapSz = map->size;
                int tgtCellX = (int)(targetPos[0] + (float)(mapSz >> 1));
                int tgtCellY = (int)(targetPos[1] + (float)(mapSz >> 1));
                if (tgtCellX < 0) tgtCellX = 0;
                if (tgtCellX >= mapSz) tgtCellX = mapSz - 1;
                if (tgtCellY < 0) tgtCellY = 0;
                if (tgtCellY >= mapSz) tgtCellY = mapSz - 1;
                int targetCell = tgtCellY * mapSz + tgtCellX;

                // --- Validate work assignment: Work::StepProgress 0x1400f6ef0 (was its
                // WorkImpl_BuildStructureStep_6ef0 forwarder). Its fifth argument is
                // Execute's own `context`: 0x1400ec355 MOV RAX,[RBP+0x77] (the R8 home,
                // spilled at 0x1400ebe40 and never rewritten), 0x1400ec35c [RSP+0x20] ---
                CitizenComponent* citizenRef = *entityIBP;
                uint64_t validated = StepProgress(&citizenRef, targetCell, citizenCell, context);

                if ((char)validated == '\0') {
                    return 0;
                }

                // --- Find path and assign work ---
                // The hidden return slot (original: local_58, which held worldEntityList
                // before the call; that value and the slot's two other qwords are never read).
                ::Object::InternalPtr<Action> pathBuf;
                ComponentEntity* destEntity = foundEntity;
                ComponentEntity* srcEntity = field_10.Get();  // Reread from self
                CitizenComponent* citizenPtr = *entityIBP;

                WorkAct_GatherRawMatCtor_bf790(
                    &pathBuf,           // the hidden return slot
                    &citizenPtr,        // citizenPtr
                    field_20,           // pathType (priority/workplaceId)
                    &srcEntity,         // the work entity
                    targetPos,          // targetPos
                    &destEntity,        // destEntityPtr
                    searchPos,          // srcPos
                    cost->material,     // materialType
                    remaining,          // amount
                    (remaining << 8) | slotIdx,  // packed slot info
                    0, '\0', 0, 0);

                // --- Finalize: set citizen state and register work ---
                int priority = field_20;
                CitizenComponent* citizenFinal = *entityIBP;

                // Call vtable slot +0x50 (GetTarget)
                Profession* workEntity = nullptr;
                GetTarget(&workEntity);

                citizenFinal->statusFlags |= Bits(CitizenStatus::TaskDone);
                citizenFinal->field_0x30 = priority;
                citizenFinal->lastErrand = CitizenErrand::None;

                if (workEntity == citizenFinal->profession && priority >= 0) {
                    citizenFinal->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                }

                // (the tree helper takes the container as uintptr_t*: LEFT)
                WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEntity's)
                WorkBST_RegisterWork_1a30(&_citizens, &inserted, entityIBP);

                // Increment build count for this slot
                _inFlight[slotIdx] += remaining;


                return 2;
            }
        }

    next_slot:
        ;
    }

    return 1;  // No work found
}

// ============================================================
// OnComplete (0x1400ebdf0) -- vtable slot 6
//
// Completion handler. Removes citizen from tree at +0x68,
// updates build counts (decrement _inFlight, increment _present).
// Returns: value from _present masking.
// ============================================================
int GatherRawMaterials::OnComplete(CitizenComponent** param2, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);

    // Remove entity from tree at +0x68 (the helper takes the container as uintptr_t*: LEFT)
    WorkBST_BSTFindRemove_25520(&_citizens, param2);

    // Extract slot index (low byte) and amount (high 24 bits, shifted right by 8)
    uint32_t slotIdx = param4 & 0xFF;
    int amount = (int)param4 >> 8;

    // Decrement _inFlight[slotIdx] (build counts) by amount
    _inFlight[slotIdx] -= amount;

    // Increment _present[slotIdx] (assigned counts) by amount
    _present[slotIdx] += amount;


    // Original returns uVar3 & 0xffffffffffffff00: retail's bool false in AL over the
    // array pointer left in RAX.
    return reinterpret_cast<uintptr_t>(_present._data) & 0xFFFFFF00;  // CAST-OK: ptr_int
}

// ============================================================
// OnCancel (0x1400ebdb0) -- vtable slot 7
//
// Cancel handler. Removes from tree, decrements build count only
// (does NOT increment assigned count).
// ============================================================
int GatherRawMaterials::OnCancel(CitizenComponent** param2, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);

    // Remove entity from tree at +0x68 (the helper takes the container as uintptr_t*: LEFT)
    WorkBST_BSTFindRemove_25520(&_citizens, param2);

    // Extract slot index and amount
    uint32_t slotIdx = param4 & 0xFF;
    int amount = (int)param4 >> 8;

    // Decrement _inFlight[slotIdx] only (cancel = don't credit the work)
    _inFlight[slotIdx] -= amount;
    return 0;
}

// ============================================================
// Cleanup (0x1400ec480) -- vtable slot 8
//
// Release all assigned citizens from tree at +0x68.
// Walks tree from leftmost node, for each citizen calls
// CitizenAI::StateMachineRelease.
// ============================================================
int GatherRawMaterials::Cleanup() {
    while (true) {
        AssignmentNode* root = _citizens._root;
        if (root == nullptr) return 0;

        // Find leftmost node
        AssignmentNode* node = root;
        while (node->left != nullptr) {
            node = node->left;
        }

        // Also try walking up from leftmost
        AssignmentNode* current = root;
        while (current != nullptr) {
            AssignmentNode* next = current->left;
            if (next == nullptr) break;
            current = next;
        }

        // node is now leftmost
        if (node == nullptr) return 0;

        // Release the citizen's action list: CharacterComponent (+0x278) -> +0x48
        CitizenComponent* citizenEntity = node->element;
        citizenEntity->character->actionStack.Clear();
    }
}

// ============================================================
// Validate (0x1400ebb50) -- vtable slot 9
//
// Check all assigned tasks in tree at +0x68 are still valid.
// Walks tree, calls Work::CanStartWork for each. Invalid ones
// are collected and removed, then cleanup is triggered.
//
// Returns: true (nonzero) if nothing was removed, false if any removed.
// ============================================================
int GatherRawMaterials::Validate() {
    // Walk tree at +0x68 (_citizens._root), collect invalid entries
    AssignmentNode* root = _citizens._root;
    uint32_t invalidCount = 0;
    CitizenComponent** invalidArray = nullptr;
    uint32_t invalidCapacity = 0;

    if (root != nullptr) {
        // Find leftmost node
        AssignmentNode* node = root;
        while (node->left != nullptr) {
            node = node->left;
        }

        while (node != nullptr) {
            // Check if this task is still valid
            CitizenComponent* taskIBP[1];
            taskIBP[0] = node->element;

            char valid = ValidateTask(nullptr, taskIBP, nullptr);

            if (valid == '\0') {
                // Invalid -- add to removal list
                uint32_t newCount = invalidCount + 1;
                if (newCount != invalidCount) {
                    if ((int)invalidCapacity < (int)newCount) {
                        uint32_t newCap = newCount;
                        if (invalidCount != 0) {
                            newCap = newCount * 3;
                        }
                        invalidCapacity = newCap;
                        CitizenComponent** newArray = static_cast<CitizenComponent**>(System::MemoryAllocator::MemAlloc((int64_t)(int)newCap << 3, 8));

                        // Copy old entries
                        int copyStart = (invalidArray == newArray) ? (int)invalidCount : 0;
                        for (int ci = copyStart; ci < (int)newCount; ci++) {
                            CitizenComponent** dst = &newArray[ci];
                            *dst = nullptr;
                            if (ci < (int)invalidCount) {
                                *dst = invalidArray[ci];
                            } else {
                                *dst = node->element;
                            }
                        }

                        if (invalidArray != newArray) {
                            System::MemoryAllocator::MemFree(invalidArray);
                        }
                        invalidArray = newArray;
                    } else {
                        // Space available, just add
                        invalidArray[invalidCount] = node->element;
                    }
                    invalidCount = newCount;
                }
            }

            // Get next node in tree (in-order traversal)
            AssignmentNode* right = node->right;
            if (right != nullptr) {
                // Go right, then leftmost
                node = right;
                while (node->left != nullptr) {
                    node = node->left;
                }
            } else {
                // Go up until we come from a left child
                AssignmentNode* parent = node->parent;
                while (parent != nullptr) {
                    if (parent->left == node) {
                        // node is left child of parent -- parent is next
                        break;
                    }
                    // node is right child, keep going up
                    node = parent;
                    parent = node->parent;
                }
                node = parent;
            }
        }
    }

    // Remove invalid entries from tree (keys compare unsigned)
    if ((int)invalidCount > 0) {
        for (uint32_t i = 0; i < invalidCount; i++) {
            // Find node in tree by key and remove
            CitizenComponent* searchKey = invalidArray[i];
            AssignmentNode* treeNode = _citizens._root;

            while (treeNode != nullptr) {
                CitizenComponent* nodeKey = treeNode->element;
                if (searchKey < nodeKey) {
                    treeNode = treeNode->left;
                } else {
                    if (searchKey <= nodeKey) {
                        // Found -- remove this node (the helper takes the container as
                        // uintptr_t*: LEFT)
                        WorkBST_BSTRemove_a6e0(&_citizens, treeNode);
                        break;
                    }
                    treeNode = treeNode->right;
                }
            }
        }

        // If any were removed, call vtable slot +0x40 (Cleanup) and reset build counts
        if ((int)invalidCount > 0) {
            Cleanup();   // slot 8 (vtable +0x40), a virtual dispatch in retail

            // Reset all build counts to 0
            int numSlots = _inFlight._size;
            for (int i = 0; i < numSlots; i++) {
                _inFlight[i] = 0;
            }
        }
    }

    System::MemoryAllocator::MemFree(invalidArray);
    return (invalidCount == 0) ? 1 : 0;
}

// ============================================================
// GetTarget (0x1400ec510) -- vtable slot 10
//
// The ProfessionList's +0x30 profession from the game world singleton.
// Hidden return ptr: RCX=self, RDX=retbuf.
// ============================================================
Profession** GatherRawMaterials::GetTarget(Profession** retbuf) {
    *retbuf = GameUpdateLoop::_singleton->m_professionList->field_0x30;
    return retbuf;
}


// ============================================================
// GatherRawMaterials::DtorBody (0x1400ec650)
// Sets vtable to GatherRawMaterials, frees BST at +0x68,
// frees int arrays at +0x58 and +0x48, sets vtable to Work,
// then calls Object::Internal::~Internal.
// ============================================================
// GatherRawMaterials dtor 0x1400ec650. The stamps and the `~Internal` call are the compiler's chain.
// Retail 0x1400ec650: stamp 0x140161420; the +0x68 set's inlined Clear (0x140014c30 on the
// root's +0x10 / +0x18 children, MemFree [0x140152b68] of the root, root = 0 qword, count =
// 0 dword); MemFree +0x58, +0x58 = 0, +0x60 = 0 (qwords); MemFree +0x48, +0x48 = 0, +0x50 = 0;
// stamp Work 0x140161a80 and tail-jump ~Internal. That is the compiler's chain over the three
// members in reverse declaration order: ~Set (Clear), ~Array (_inFlight), ~Array (_present).
GatherRawMaterials::~GatherRawMaterials() = default;
// Slot11 (+0x58) — retail 0x14002a400 is `return 0 < *(int*)(self + 0x70)` (this class
// keeps its tree count at +0x70). Was pinned to 0 by Work_SlotRet0 (2026-09-05).
int GatherRawMaterials::GetPriority() {
    return _citizens._count > 0 ? 1 : 0;
}

// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The stamps are the compiler's.
GatherRawMaterials::GatherRawMaterials()
    : Work() {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_GatherRawMaterials_Vtable` (slot 1 null, slot 2 a
// lazily cached id) and REGISTER_TYPE over GatherRawMaterials_Factory / _CreateInPlace
// (both Work_GenericCtor). Nothing outside this block reads 0x140161420 back (sweep by
// hex and by GatherRawMaterialsAddr::Vtable; the site ctor that stamped it is the real
// ctor now), so its registration goes with the table.

// create 0x14002a3c0: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* GatherRawMaterials::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GatherRawMaterials), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) GatherRawMaterials());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* GatherRawMaterials::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) GatherRawMaterials());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(GatherRawMaterials, "Work")

// ---- Construction force-harness wrappers (construction_force_harness.cpp) ----
// Mirror the ForesterWork_Factory/ForceDrive/ExecuteReady trio so the generic
// ForceWork harness can drive construction gathering.

// Build a GatherRawMaterials bound to a construction SITE entity using the FAITHFUL
// site ctor (seeds per-material slots from the site's BuildDescription + the amount
// already in the site StorageComponent) rather than the generic factory. buildEntityIBP
// is an InternalBasePtr* whose *value is the site entity pointer.
extern "C" GatherRawMaterials* GatherRawMaterials_ConstructForSite(ComponentEntity* const* buildEntityIBP) {
    void* mem = Object::detail::InternalMemAlloc(sizeof(GatherRawMaterials), 8);
    if (!mem) return nullptr;
    return new (mem) GatherRawMaterials(buildEntityIBP);
}

// Drive Execute with the WORK-DRIVER ABI (self, citizenIBP). Execute's `context` is the
// Time it hands Work::StepProgress (row 861), so the harness passes what the game's own
// callers pass: the WorkScheduler's time (GameUpdateLoop +0x118, scheduler +0x130).
extern "C" int GatherRawMaterials_ForceDrive(Work* self, CitizenComponent** citizenIBP) {
    return static_cast<GatherRawMaterials*>(self)->GatherRawMaterials::Execute(
        citizenIBP, &GameUpdateLoop::_singleton->m_tutorial->time);
}

// Execute-readiness gate for the force harness. B3R: every helper Execute and its pushed
// action chain use is called by name, so only the game world is left to test.
extern "C" bool GatherRawMaterials_DepsReady() {
    return GameUpdateLoop::_singleton != nullptr;
}
