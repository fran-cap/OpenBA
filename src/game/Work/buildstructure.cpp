// src/game/Work/buildstructure.cpp
// Clean reimplementation of BuildStructure Work class.
//
// BuildStructure (0x70 bytes) — structure construction work action.
// Extends Work (0x48 base) which extends Object::Internal.
// Vtable: 0x1401610a0
//
// Fields:
//   +0x48: bool   _isDemolition — use alternate animation flag (serialized)
//   +0x4C: int    _scanIndex    — rotating scan index into bool array (serialized)
//   +0x50: void*  _slots      — byte array (one per build position: 0=free, 1=claimed)
#include "Work/exe_bodies.h"
//   +0x58: int    _slots._size      — count of build positions
//   +0x60: void*  _citizens._root      — BST root (tracks which entity claimed which position)
//   +0x68: int    _citizens._count      — build progress counter
//
// Work base fields used:
//   +0x10: void*  _entity      — InternalBasePtr to building entity
//   +0x20: int    _workId      — priority value
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/BuildStructure.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Tutorial/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"          // B3R: the Pf_* bodies called by name
#include "Component/exe_bodies.h"       // B3R: AttendanceComponent_GetJobHostComponent
#include "Dialog/dialog_exe_helpers.h"  // B3R: Dialog_FindComponent_d6c0
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Entity/ComponentEntity.h"       // ENDGAME B.3.9 D5: ComponentEntity::TypeId()
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/InteractComponent.h"
#include "Component/BuildComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/WorkComponent.h"
#include "Game/ProfessionList.h"
#include "Game/Map.h"
#include "Action/PathSearch.h"   // RegionGraph::PathLength
#include "Action/CompleteTask.h"
#include "Action/Action.h"

// ============================================================
// Ctor (0x1400e5a90)
// ============================================================
// Retail 0x1400e5a90: `??0Internal`, the Work base init, the two stamps (the
// compiler's), then +0x50/+0x58 (a qword: count AND capacity), +0x60, +0x68.
// +0x48/+0x4c are left to Persist / the caller, as in retail.
BuildStructure::BuildStructure()
    : Work() {}

// ============================================================
// Full Ctor (0x1400e59c0) — 3-arg: (self, &entityIBP, altAnim)
// ============================================================
// The REAL retail ctor used by BuildComponent State8Enter (sub_7ae0). Unlike the
// 1-arg ctor (0x1400e5a90) it (a) routes base init through Work_Ctor25 so _entity
// (building entity), +0x18 (WorkComponent) and +0x28 (target cell) are populated, and
// (b) ALLOCATES the build-position byte array _slots/_slots._size sized to the site's
// PositionComponent footprint (FUN_140123070 position count -> FUN_14000a590 resize),
// zeroing every position byte. Without this array Execute's unbuilt-position scan finds
// nothing (_slots._size == 0 -> unbuiltCount 0) and returns 1 (never assigns labor) — the
// 2c-B blocker. Faithful offset-for-offset port of the Ghidra decompilation.
BuildStructure::BuildStructure(ComponentEntity* const* entityIBP, bool altAnim)
    : Work(entityIBP),   // local_res10 = *entityIBP; Work_Ctor25(this, &local_res10)
      _isDemolition(altAnim), _scanIndex(0) {
    // The +0x50 array and the +0x60 set are members: their ctors zero data, count and
    // capacity (retail 0x1400e5a0a / 0x1400e5a0e: two qwords, so the resize below never
    // reads a garbage capacity) and the set's root (qword) and count (dword).
    // Allocate the build-position byte array from the site's PositionComponent count.
    if (entityIBP) {
        // The site's InteractComponent (Work(entityIBP) stored *entityIBP in _entity)
        InteractComponent* posComp = _entity->FindComponent<InteractComponent>();
        int count = posComp->NodeCount(1);   // 0x140123070
        // Array<uint8_t>::SetSize (0x14000a590): new bytes are uninitialised, so zero-fill below.
        _slots.SetSize(count);
        int n = _slots._size;
        uint8_t* arr = _slots._data;
        for (int i = 0; i < n && arr; i++) arr[i] = 0;
    }
}

// ============================================================
// BaseDtor (0x1400e5920)
// ============================================================
// The stamps (BuildStructure, then Work) and `~Internal` are the compiler's.
// Retail 0x1400e5920: stamp 0x1401610a0; the +0x60 set's inlined Clear (0x140014c30 on the
// root's +0x10 / +0x18 children, MemFree [0x140152b68] of the root, root = 0 qword, count =
// 0 dword); MemFree +0x50, +0x50 = 0, +0x58 = 0 (qwords: count AND capacity); stamp Work
// 0x140161a80 and tail-jump ~Internal. That is the compiler's chain over the two members in
// reverse declaration order: ~Set (Clear), then ~Array (_slots). (The tree's hand-written
// body left the +0x5C capacity; ~Array zeroes it, as retail's qword store does.)
BuildStructure::~BuildStructure() = default;

// ============================================================
// OnComplete (0x1400e4f80)
// ============================================================
int BuildStructure::OnComplete(CitizenComponent** citizenIBP, ComponentEntity** /*unused*/, int slotIndex) {
    WorkBST_BSTFindRemove_25520(&_citizens, citizenIBP);
    _slots[slotIndex] = 0;
    return 0;
}

// ============================================================
// OnCancel (0x1400e4f40)
// ============================================================
int BuildStructure::OnCancel(CitizenComponent** citizenIBP, ComponentEntity** /*unused*/, int slotIndex) {
    WorkBST_BSTFindRemove_25520(&_citizens, citizenIBP);
    _slots[slotIndex] = 0;
    return 0;
}

// ============================================================
// Cleanup (0x1400e57a0)
// ============================================================
int BuildStructure::Cleanup() {
    while (true) {
        AssignmentNode* root = _citizens._root;
        if (root == nullptr) return 0;

        AssignmentNode* node = root;
        AssignmentNode* leftChild = node->left;
        while (leftChild != nullptr) {
            node = leftChild;
            leftChild = node->left;
        }

        if (node == nullptr) return 0;

        AssignmentNode* node2 = root;
        if (node2 != nullptr) {
            AssignmentNode* left2 = node2->left;
            while (left2 != nullptr) {
                node2 = left2;
                left2 = node2->left;
            }
        }

        // The node's citizen; its CharacterComponent's action list (+0x48)
        CitizenComponent* citizen = node2->element;

        citizen->character->actionStack.Clear();
    }
}

// ============================================================
// GetPriority (0x140029670) — not hookable, available as clean code
// ============================================================
int BuildStructure::GetPriority() {
    return _citizens._count > 0;
}

// ============================================================
// BuildStructure::Serialize (0x1400E5850)
// ============================================================
void BuildStructure::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // IO::Serializer block (version 0)
    IO::Serializer ser(stream, 0);

    // Serialize bool at +0x48 (the field is uint8_t storage)
    ser.Serialize(_isDemolition, nullptr);

    // Serialize BST at +0x60 (InternalBasePtr tree)
    WorkBST_BST_Serialize_296b0(&_citizens, &ser);

    // Serialize count (+0x58), resize array on load
    int count = _slots._size;
    ser.Serialize(count, nullptr);

    // Check if loading (direction 0)
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        _slots.SetSize(count);   // 0x14000a590
    }

    // Bulk serialize the byte array (one byte per build position)
    ser.Serialize(count, _slots._data, nullptr);

    // Serialize rotating scan index at +0x4C
    ser.Serialize(_scanIndex, nullptr);

    // The serializer's destructor runs here
}

// ============================================================
// Helper: check terrain connectivity between two grid cells.
// Each cell is a MapCell (0x30 bytes) of the map's cell array (Map +0x80).
//   +0x26: zone (compared as int16)
//   +0x29: terrainType (bit index)
// Returns true if cells are walkably connected.
// ============================================================
static bool CheckCellConnectivity(const MapCell* gridData, int64_t cellA, int64_t cellB) {
    const MapCell& entryA = gridData[cellA];
    const MapCell& entryB = gridData[cellB];

    uint32_t bitA = 1u << (static_cast<uint8_t>(entryA.terrainType) & 0x1f);
    uint32_t bitB = 1u << (static_cast<uint8_t>(entryB.terrainType) & 0x1f);
    uint32_t combined = bitA | bitB;
    uint32_t mask = combined | Bits(PathBits::Walk);

    if (!(mask & bitA) || !(mask & bitB)) return false;
    if (combined & Bits(PathBits::Zoneless)) return true;

    int16_t zA = static_cast<int16_t>(entryA.zone);
    if (zA == 0) return false;
    int16_t zB = static_cast<int16_t>(entryB.zone);
    if (zB == 0) return false;
    return zA == zB;
}

// ============================================================
// Helper: convert world coords to clamped grid cell index.
// ============================================================
static int WorldToCell(int gridSize, float x, float z) {
    int half = gridSize >> 1;
    int cx = (int)(x + (float)half);
    int cz = (int)(z + (float)half);
    if (cx < 0) cx = 0;
    if (cx >= gridSize) cx = gridSize - 1;
    if (cz < 0) cz = 0;
    if (cz >= gridSize) cz = gridSize - 1;
    return gridSize * cz + cx;
}


int BuildStructure::Execute(CitizenComponent** citizenPtr, System::Time* /*context*/) {
    // --- Phase 1: Validate building is enabled ---
    // Find InteractComponent (the build positions) on the building entity
    InteractComponent* posComp = _entity->FindComponent<InteractComponent>();

    // Find the BuildComponent (type DWORD_1401c7008)
    BuildComponent* buildMgmt = _entity->FindComponent<BuildComponent>();

    // Traverse: buildMgmt -> its entity (+0x10) -> the WorkPlaceComponent (DWORD_1401c80e0).
    // (Was documented as "build description"; corrected 2026-08-09 — the +0x50 read below
    // is the workplace accepting flag, not a description field. Behaviour unchanged.)
    WorkPlaceComponent* buildDesc = buildMgmt->_entity->FindComponent<WorkPlaceComponent>();

    // Check if building is enabled (byte at +0x50 of the workplace)
    if (!buildDesc->isAccepting) {
        return 1;
    }

    // --- Phase 2: Check if we have positions left to build ---
    int totalPositions = posComp->NodeCount(1);   // 0x140123070
    if (_citizens._count >= totalPositions) {
        return 1;
    }

    // --- Phase 3: Check builder count vs max, verify citizen authorization ---
    WorkPlaceComponent* bc = _entity->FindComponent<WorkPlaceComponent>();
    int currentBuilders = bc->workers._size;

    WorkComponent* matDesc = bc->_entity->FindComponent<WorkComponent>();

    // Calculate max builders if not yet done (WorkComponent +0x30)
    if (matDesc->workState == 0) {
        matDesc->workState = matDesc->CalcMaxWorkers();
    }

    // If at max builders, verify this citizen is on the builder list
    if (currentBuilders == matDesc->workState) {
        bool found = false;
        if (bc->workers._size < 1) {
            return 1;
        }

        int i = 0;
        do {
            CitizenComponent* builderId = bc->workers[i].Get();

            if (builderId == *citizenPtr) {
                found = true;
            }
            i++;

        } while (i < bc->workers._size);

        if (!found) {
            return 1;
        }
    }

    // --- Phase 4: Count unbuilt positions ---
    int unbuiltCount = 0;
    int posCount = _slots._size;
    if (posCount > 0) {
        uint8_t* arr = _slots._data;
        for (int i = 0; i < posCount; i++) {
            if (arr[i] == 0) unbuiltCount++;
        }
    }

    // --- Phase 5: Get map grid and building center cell ---
    // The world map
    Map* map = GameUpdateLoop::_singleton->m_game;

    // The citizen's position: CharacterComponent (+0x278) -> ModelComponent (+0x68)
    // -> transform translation (+0x158)
    const Math::Vector3& posData = (*citizenPtr)->character->modelComponent->transform.translation;

    (void)posData.z;  // centerY: available for future use
    int gridSize = map->size;
    int buildingCell = WorldToCell(gridSize, posData.x, posData.y);

    int chosenIndex = -1;
    int attemptCount = 0;

    if (unbuiltCount <= 0) {
        return 1;
    }

    // --- Phase 6: Search for accessible unbuilt position ---
    while (attemptCount < unbuiltCount) {
        // Scan for next unbuilt position using rotating index
        int scanTotal = _slots._size;
        if (scanTotal > 0) {
            for (int scan = 0; scan < scanTotal; scan++) {
                int idx = _scanIndex;
                if (_slots[idx] == 0) {
                    chosenIndex = idx;
                }
                int next = idx + 1;
                if (next >= scanTotal) next = 0;
                _scanIndex = next;
                if (chosenIndex >= 0) break;
            }
        }

        // Get world position for this build point
        Vector3Slot candidatePos;
        posComp->NodePosition(&candidatePos.v, 1, chosenIndex, nullptr);   // 0x140122e20

        gridSize = map->size;
        int candidateCell = WorldToCell(gridSize, candidatePos.v.x, candidatePos.v.y);

        if (candidateCell != 0) {
            map->FillZones();   // 0x14009c480; retail sets RCX only (0x1400e532a / 54ea / 556c)

            const MapCell* gridData = map->cells.GetData();
            if (CheckCellConnectivity(gridData, buildingCell, candidateCell)) {
                // Found accessible point -- get exact position
                Vector3Slot bestPos;
                posComp->NodePosition(&bestPos.v, 1, chosenIndex, nullptr);   // 0x140122e20

                gridSize = map->size;
                int exactCell = WorldToCell(gridSize, bestPos.v.x, bestPos.v.y);

                // Calculate initial path cost
                int bestPathCost = map->PathDistance((uint32_t)buildingCell, exactCell, 1);   // 0x14009f810

                // Iterate ALL positions to find a shorter path
                int total = _slots._size;
                int64_t centerCellIdx = (int64_t)buildingCell;
                if (total < 1) goto doSetupActions;

                for (int posIdx = 0; posIdx < total; posIdx++) {
                    if (posIdx == chosenIndex) continue;

                    Vector3Slot testPos;
                    posComp->NodePosition(&testPos.v, 1, posIdx, nullptr);   // 0x140122e20

                    gridSize = map->size;
                    int testCell = WorldToCell(gridSize, testPos.v.x, testPos.v.y);

                    // Check connectivity (first pass)
                    map->FillZones();   // 0x14009c480; retail sets RCX only (0x1400e532a / 54ea / 556c)
                    gridData = map->cells.GetData();
                    if (!CheckCellConnectivity(gridData, buildingCell, testCell))
                        continue;

                    // Check connectivity (second pass after re-invalidation)
                    map->FillZones();   // 0x14009c480; retail sets RCX only (0x1400e532a / 54ea / 556c)
                    gridData = map->cells.GetData();
                    if (!CheckCellConnectivity(gridData, buildingCell, testCell))
                        continue;

                    // Get path region IDs from grid entries
                    const MapCell& cEntry = gridData[centerCellIdx];
                    const MapCell& tEntry = gridData[(int64_t)testCell];
                    int32_t regionCenter = cEntry.regionId;
                    int32_t regionTest = tEntry.regionId;

                    if (regionCenter < 0) {
                        regionCenter = map->NearestRegionId(buildingCell);   // 0x14009d0a0
                    }
                    if (regionTest < 0) {
                        regionTest = map->NearestRegionId(testCell);   // 0x14009d0a0
                    }
                    if (regionCenter < 0 || regionTest < 0) continue;

                    int dist;
                    if ((uint32_t)regionCenter == (uint32_t)regionTest) {
                        dist = 1;
                    } else {
                        // Normalize: hi >= lo
                        uint32_t hi = (uint32_t)regionCenter;
                        uint32_t lo = (uint32_t)regionTest;
                        if ((int32_t)hi < (int32_t)lo) {
                            hi = (uint32_t)regionTest;
                            lo = (uint32_t)regionCenter;
                        }

                        // Search the region pair cache's index (Map +0x78; index +0x28)
                        RegionPairCache* pathCache = map->regionPairCache;
                        AvlNode30* node = pathCache->index.root;
                        dist = -1;
                        bool cached = false;

                        while (node != nullptr) {
                            int nHi = node->key0;
                            if ((int)hi < nHi) {
                                node = node->left;
                            } else if ((int)hi > nHi) {
                                node = node->right;
                            } else {
                                int nLo = node->key1;
                                if ((int)lo < nLo) {
                                    node = node->left;
                                } else if ((int)lo <= nLo) {
                                    // Exact match: the ring entry's distance (ring +0x18, entry +0x08)
                                    int eIdx = node->key2;
                                    dist = pathCache->ring.data[(int64_t)eIdx].distance;
                                    if (dist >= 0) cached = true;
                                    break;
                                } else {
                                    node = node->right;
                                }
                            }
                        }

                        if (!cached) {
                            // Retail 0x1400e5630..0x1400e56a1: R9D = the smaller region id (`lo`), left there by
                            // the cache-key normalisation; 0x1400a8ba0 forwards R9 to 0x14001dc00 untouched.
                            // (the forwarder of 0x1400a8ba0 never read its fourth argument, lo)
                            dist = map->regionGraph->PathLength(regionCenter, static_cast<uint32_t>(regionTest));   // 0x1400a8ba0
                            map->regionPairCache->Store(regionCenter, regionTest, dist);   // 0x1400a5b30
                        }

                        if (dist < 0) continue;
                    }

                    // If path is shorter than half current best, adopt this position
                    if (dist < (bestPathCost >> 1)) {
                        bestPos.v.x = testPos.v.x;
                        bestPos.v.y = testPos.v.y;
                        bestPos.v.z = testPos.v.z;
                    }
                }

                goto doSetupActions;

            doSetupActions:
                // --- Phase 7: Setup action queue ---
                {
                    // Prepare IBP values for action setup
                    CitizenComponent* citizenEntityVal = *citizenPtr;

                    // FUN_1400c0690: sets up MoveTo, ShowTool, PerformWork, CompleteTask actions
                    // Args: citizenEntityIBP, priority, buildingEntityIBPptr, position, index, altAnim
                    BuildStructure_SetupBuildActions(
                        &citizenEntityVal,
                        _workId,
                        &_entity,
                        &bestPos.v.x,
                        chosenIndex,
                        _isDemolition
                    );

                    // Update building entity state
                    int priority = _workId;
                    CitizenComponent* bldgEntity = *citizenPtr;

                    // Get target via vtable slot 10 (GetTarget, +0x50)
                    Profession* targetIBP = nullptr;
                    Profession* targetVal = *GetTarget(&targetIBP);

                    bldgEntity->statusFlags |= Bits(CitizenStatus::TaskDone);
                    bldgEntity->lastWorkId = priority;
                    bldgEntity->lastErrand = CitizenErrand::None;

                    if (targetVal == bldgEntity->profession && priority >= 0) {
                        bldgEntity->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                    }

                    // Insert claimed position into BST at +0x60
                    WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses the target's)
                    WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenPtr);

                    // Mark build position as claimed
                    _slots[chosenIndex] = 1;

                    return 2;
                }
            }
        }

        attemptCount++;
    }

    return 1;
}

// ============================================================
// GetTarget (vtable +0x50, 0x1400E5830)
// ============================================================
// Returns the profession list's +0x38 profession (GameUpdateLoop::m_professionList).
// Uses hidden return pointer ABI: RCX=self, RDX=retBuf
Profession** BuildStructure::GetTarget(Profession** retBuf) {
    *retBuf = GameUpdateLoop::_singleton->m_professionList->builderProfession;
    return retBuf;
}

// ============================================================
// Validate (vtable +0x48, 0x1400E4BE0)
// ============================================================
// Walks the BST at +0x60 (citizens assigned to build slots).
// For each citizen, checks if they still have a matching CompleteTask.
// Citizens without a task are collected for removal.
// After traversal, removes invalid citizens from the BST and
// updates the slot array (+0x50) accordingly.
//
// Returns: 0 = some citizens were removed
//          1 = none removed, or the BST is empty (0x1400e4eea MOV BL,1)

int BuildStructure::Validate() {

    // Dynamic arrays for citizens to remove and their slot indices
    CitizenComponent** removeList = nullptr;   // array of citizen keys (8 bytes each)
    int removeCount = 0;
    int removeCap = 0;

    int* slotList = nullptr;        // array of int slot indices (4 bytes each)
    int slotCount = 0;
    int slotCap = 0;

    // BST in-order traversal: find leftmost node first
    AssignmentNode* root = _citizens._root;
    if (root == nullptr) goto done_empty;

    {
        // Find leftmost (minimum) node
        AssignmentNode* node = root;
        while (node->left != nullptr) {  // left child at +0x10
            node = node->left;
        }

        // In-order traversal
        while (node != nullptr) {
            CitizenComponent* citizenKey = node->element;
            CompleteTask* taskResult = nullptr;

            // 0x1400e4c67 CALL 0x1400f7100: Work::ValidateTask(this, -, &key, &task).
            bool hasTask = ValidateTask(nullptr, &citizenKey, &taskResult);

            if (!hasTask) {
                // Citizen has no matching task — add to remove list
                int newCount = removeCount + 1;
                if (newCount != removeCount) {
                    if (removeCap < newCount) {
                        removeCap = (removeCount == 0) ? newCount : newCount * 3;
                        CitizenComponent** newBuf = static_cast<CitizenComponent**>(System::MemoryAllocator::MemAlloc((uint64_t)removeCap * 8, 8));
                        if (removeList != newBuf) {
                            // Copy existing entries
                            for (int k = 0; k < removeCount; k++) {
                                newBuf[k] = removeList[k];
                            }
                        }
                        // Set the new entry
                        newBuf[removeCount] = citizenKey;
                        if (removeList != newBuf) {
                            System::MemoryAllocator::MemFree(removeList);
                        }
                        removeList = newBuf;
                    } else {
                        removeList[removeCount] = citizenKey;
                    }
                    removeCount = newCount;
                }
            } else {
                // Citizen has matching task — record the slot index from task+0x34
                int slotIdx = taskResult->taskParam;

                int newSlotCount = slotCount + 1;
                if (newSlotCount != slotCount) {
                    if (slotCap < newSlotCount) {
                        slotCap = (slotCount == 0) ? newSlotCount : newSlotCount * 3;
                        int* newBuf = static_cast<int*>(System::MemoryAllocator::MemAlloc((uint64_t)slotCap * 4, 4));
                        if (slotList != newBuf) {
                            for (int k = 0; k < slotCount; k++) {
                                newBuf[k] = slotList[k];
                            }
                        }
                        newBuf[slotCount] = slotIdx;
                        if (slotList != newBuf) {
                            System::MemoryAllocator::MemFree(slotList);
                        }
                        slotList = newBuf;
                    } else {
                        slotList[slotCount] = slotIdx;
                    }
                    slotCount = newSlotCount;
                }
            }

            // Move to next node (in-order successor)
            AssignmentNode* right = node->right;  // right child
            if (right != nullptr) {
                // Go right, then find leftmost
                node = right;
                while (node->left != nullptr) {
                    node = node->left;
                }
            } else {
                // Go up until we come from a left child
                AssignmentNode* parent = node->parent;  // parent at +0x08
                if (parent != nullptr && parent->left == node) {
                    // We are the left child — parent is next
                    node = parent;
                } else {
                    // We are the right child — go up until we find a left-child relationship
                    AssignmentNode* prev = node;
                    while (parent != nullptr && parent->right == prev) {
                        prev = parent;
                        parent = parent->parent;
                    }
                    node = parent;
                }
            }
        }

        // Process remove list: remove each citizen from the BST
        if (removeCount > 0) {
            CitizenComponent** rList = removeList;
            for (int i = 0; i < removeCount; i++) {
                // BST lookup to find the node
                AssignmentNode* searchNode = _citizens._root;
                while (searchNode != nullptr) {
                    if (rList[i] < searchNode->element) {
                        searchNode = searchNode->left;  // left at +0x10
                    } else {
                        if (rList[i] <= searchNode->element) {
                            // Found — unlink THIS node (0x1400e4e7d CALL 0x14001a6e0)
                            WorkBST_BSTRemove_a6e0(&_citizens, searchNode);
                            break;
                        }
                        searchNode = searchNode->right;  // right at +0x18
                    }
                }
            }

            // Update slot array: mark only validated slots as claimed
            int totalSlots = _slots._size;
            for (int si = 0; si < totalSlots; si++) {
                // Check if this slot index appears in the slotList
                bool found = false;
                for (int j = 0; j < slotCount; j++) {
                    if (slotList[j] == si) {
                        found = true;
                        break;
                    }
                }
                _slots[si] = found ? 1 : 0;
            }

            System::MemoryAllocator::MemFree(slotList);
            System::MemoryAllocator::MemFree(removeList);
            return 0;
        }
    }

    // Nothing removed: 0x1400e4e2c TEST R12D,R12D / JZ 0x1400e4ee2 -> MOV BL,1 (known_port_issues
    // row 900: the tree returned 0 here; every caller ignores the result).
    System::MemoryAllocator::MemFree(slotList);
    System::MemoryAllocator::MemFree(removeList);
    return 1;

done_empty:
    System::MemoryAllocator::MemFree(slotList);
    System::MemoryAllocator::MemFree(removeList);
    return 1;
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_BuildStructure_Vtable` (slot 1 answered null, slot 2
// a lazily cached id) registered at 0x1401610a0, REGISTER_TYPE over
// BuildStructure_Factory / BuildStructure_CreateInPlace, and a lazy vtable-pointer
// cache (EnsureHelpers / EnsureVtables). Nothing outside this file reads 0x1401610a0
// back (sweep by hex and by BuildStructureAddr::Vtable), so the table's
// registration goes with it.

// create 0x140029630: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* BuildStructure::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BuildStructure), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) BuildStructure());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* BuildStructure::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) BuildStructure());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(BuildStructure, "Work")

// ---- Construction force-harness wrappers (construction_force_harness.cpp) ----
// Mirror the GatherRawMaterials ConstructForSite/ForceDrive/DepsReady trio so the generic
// ForceWork harness can drive the state-8 BUILD LABOR half exactly like it drives the
// state-1 gather half. BuildStructure::Execute assigns the bound citizen a build position
// and enqueues the timed-labor action chain (MoveTo/ShowTool/PerformWork/CompleteTask) onto
// its CharacterComponent action stack; MoveTick's executor runs it. The gather driver reached
// state 8 crash-free; this drives the site 8 -> 2.

// Build a BuildStructure bound to a construction SITE entity using the FAITHFUL full ctor
// (0x1400e59c0): it routes base init through Work_Ctor25 (_entity=site entity, +0x18
// WorkComponent, +0x28 target cell) AND allocates the build-position byte array (_slots/
// _slots._size) from the site's PositionComponent footprint — without which Execute's unbuilt-
// position scan finds nothing (_slots._size==0) and returns 1 (never assigns labor). buildEntityIBP
// is an InternalBasePtr* whose *value is the site entity pointer.
extern "C" BuildStructure* BuildStructure_ConstructForSite(ComponentEntity** buildEntityIBP) {
    void* mem = Object::detail::InternalMemAlloc(sizeof(BuildStructure), 8);
    if (!mem) return nullptr;
    return new (mem) BuildStructure(buildEntityIBP, 0);
}

// Drive Execute with the WORK-DRIVER ABI (self, citizenIBP). Execute reads *citizenIBP as the
// citizen entity, so a plain InternalBasePtr* is correct (same shape as the gather driver).
extern "C" int BuildStructure_ForceDrive(Work* self, CitizenComponent** citizenIBP) {
    return static_cast<BuildStructure*>(self)->BuildStructure::Execute(citizenIBP, nullptr);
}

// Execute-readiness gate for the force harness (no retail address). B3R: BuildStructure's
// helpers are called by name, so the only thing left to test is the game world (STORAGE).
extern "C" bool BuildStructure_DepsReady() {
    return GameUpdateLoop::_singleton != nullptr;
}
