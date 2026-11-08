// src/game/Work/clearstorage.cpp
// Clean reimplementation of ClearStorage work class methods.
//
// ClearStorage is a Work subclass (0x68 bytes) for clearing items from storage
// buildings. Citizens assigned to this work pick up items from one storage and
// carry them to another available storage location.
//
// Vtable at 0x1401611a0.
//
// Decompiled from Application-x64.exe.

#include "Work/ClearStorage.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.9 D5
#include "Component/StorageComponent.h"         // ENDGAME B.3.9 D5
#include "Component/StorageLocationComponent.h"
#include "Component/InteractComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/ProfessionList.h"
#include "Game/Map.h"
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Action/PathSearch.h"   // RawArray (PickBestDropTarget)

// Sub-functions, called by name (B3R): 0x140025520 WorkBST_BSTFindRemove_25520,
// 0x1400c5400 WorkBase_TaskListCleanup_c5400, 0x140014c30 WorkBase_TreeNodeFree_14c30.

// ============================================================
// Helper: Clamp int to [0, max-1]
// ============================================================
static inline int ClampGrid(int val, int gridSize) {
    if (val < 0) return 0;
    if (val >= gridSize) return gridSize - 1;
    return val;
}

// ============================================================
// Helper: Compute grid cell index from world position + grid info
// ============================================================
static inline uint32_t WorldToCell(float x, float z, int gridSize) {
    int halfGrid = gridSize >> 1;
    int cellX = ClampGrid((int)(x + (float)halfGrid), gridSize);
    int cellZ = ClampGrid((int)(z + (float)halfGrid), gridSize);
    return (uint32_t)(cellZ * gridSize + cellX);
}

// ============================================================
// ClearStorage::Execute (Slot4, vtable +0x20)
// Original: 0x1400e6c50
// ============================================================
// Iterates storage items on the assigned building. For each item type with
// available quantity, finds a destination storage, validates the path, and
// sets up citizen pathfinding to carry items.
//
// Returns: 0 = failed validation, 1 = no work available (retry), 2 = work assigned
int ClearStorage::Execute(CitizenComponent** citizenAI, System::Time* time) {
    // Find components on the entity (+0x10) through the guarded finders (typed locals)
    // 0x1400e6c8b CALL 0x140002ed0: FindComponent<InteractComponent> into [RBP-0x40]
    InteractComponent* positionComp = _entity->FindComponent<InteractComponent>();

    StorageLocationComponent* unknownComp = _entity.Get()->FindComponent<StorageLocationComponent>(); // cell 0x1401c7ee8

    StorageComponent* storage = _entity.Get()->FindComponent<StorageComponent>();

    // Get game world grid data
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* map = gameWorld->m_game;
    Map* gridDataPtr = map;
    CitizenBehavior* worldArray = gameWorld->m_citizenList;

    // Citizen position: CharacterComponent (+0x278) -> ModelComponent (+0x68)
    // -> transform translation (+0x158)
    const Math::Vector3& posData = (*citizenAI)->character->modelComponent->transform.translation;

    int gridSize = map->size;
    // citizenY at +0x160 is stored but only used for searchPos[2] init
    (void)posData.z;  // citizenY — unused in grid calc
    float citizenX = posData.x;
    float citizenZ = posData.y;

    uint32_t srcCell = WorldToCell(citizenX, citizenZ, gridSize);

    // Get storage item count
    int itemCount = storage->storageSlots._size;

    if (itemCount <= 0) {
        return 1;  // No items in storage
    }

    // Iterate over storage items
    for (int i = 0; i < itemCount; i++) {
        StorageComponent::Item& itemEntry = storage->storageSlots[i];

        // Check available count: stored - reserved > 0
        int stored = itemEntry._count;
        int reserved = itemEntry._reserve;
        int availCount = stored - reserved;
        if (availCount <= 0) continue;

        // The stored material's template (+0x00)
        ComponentDescription* itemEntity = itemEntry._resource;

        // Find the RawMaterialDescription among the template's sub-descriptions
        RawMaterialDescription* rawMatDesc = nullptr;
        if (itemEntity->_subDescCount > 0) {
            for (int j = 0; j < itemEntity->_subDescCount; j++) {
                Description** compArray = itemEntity->_subDescs;
                Description* comp = compArray[j];
                // slot 2 Type() of the sub-description
                uint32_t typeId = comp->Type();
                if (typeId == RawMaterialDescription::TypeId()) {
                    // Verify via IsDerivedFrom
                    comp = itemEntity->_subDescs[j];
                    if (Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId())) {
                        rawMatDesc = static_cast<RawMaterialDescription*>(comp);
                    }
                    break;
                }
            }
        }

        // Find StorageComponent on the citizen's entity (+0x10): the FindComponent walk
        StorageComponent* destStorage = (*citizenAI)->_entity->FindComponent<StorageComponent>();

        // Check capacity: destStorage capacity - count vs rawMat resourceSize * availCount
        int maxCapacity = destStorage->capacity;

        int currentCount = destStorage->QueryCount();   // 0x14012cf10

        int freeSpace = maxCapacity - currentCount;
        int resourceSize = rawMatDesc ? rawMatDesc->_weight : 1;

        if (freeSpace < resourceSize * availCount) {
            // Not enough room in dest, check citizen's building storage
            StorageComponent* altStorage = ((*citizenAI)->_entity.Get())->FindComponent<StorageComponent>();

            int altMax = altStorage->capacity;

            int altCurrent = altStorage->QueryCount();   // 0x14012cf10

            availCount = (altMax - altCurrent) / resourceSize;
        }

        if (availCount == 0) {
            // No capacity available, transition to long-term plan
            (*citizenAI)->LongTermPlan();   // 0x140115e60
            return 2;
        }

        // Set up search for destination storage
        Object::InternalPtr<ComponentEntity> entityIBP = _entity;

        float searchPos[3] = {0.0f, 0.0f, 0.0f};
        uint32_t resourceTypeId = (uint32_t)_cell;
        ComponentEntity* destEntity = nullptr;

        worldArray->PickBestDropTarget(
                           &destEntity, resourceTypeId, itemEntity, availCount, 1,
                           searchPos, &entityIBP);   // 0x14008d590

        if (destEntity == nullptr) {
            continue;
        }

        // Check if path is possible
        uint32_t workTypeId = (uint32_t)_cell;
        if (!gridDataPtr->ValidateCell(PathBits::Walk, srcCell, workTypeId)) {
            return 1;
        }

        // Determine destination position and cell
        Math::Vector3 destPos;
        uint32_t destCell = 0;

        int dbgBranch = -1, dbgSlot = 0;   // trace: 0=position, 1=findslot
        if (unknownComp == nullptr) {
            // No storage-location component — use the InteractComponent. The null test is
            // retail's: 0x1400e70ee CMP [RBP-0x40],0 / JZ 0x1400e6fb9 (MOV EAX,1).
            if (positionComp == nullptr) {
                return 1;
            }
            dbgBranch = 0;
            positionComp->NodePosition(&destPos, 0, 0, nullptr);   // 0x1400e7103 / 0x1400e7118 CALL 0x140122e20
            destCell = WorldToCell(destPos.x, destPos.y, gridSize);
        } else {
            // Use search position
            dbgBranch = 1;
            destCell = WorldToCell(searchPos[0], searchPos[1], gridSize);

            int slotIdx = unknownComp->FindBestAdjacentCell(destCell, &destPos.x);   // 0x14012d5d0 (the forwarder dropped workTypeId)
            dbgSlot = slotIdx;
            if (slotIdx < 0) {
                return 1;
            }
        }

        // Validate the work assignment
        CitizenComponent* citizenVal = *citizenAI;

        if (!StepProgress(citizenAI, destCell, srcCell, time)) {
            return 0;
        }

        // Set up pathfinding
        ComponentEntity* workEntity = _entity.Get();
        int pathType = _workId;
        ComponentEntity* destEntityCopy = destEntity;
        uint32_t pathFlags = (availCount << 8) | 0;  // flags from local_res20[0]

        // 0x1400bf790's hidden return slot; the tree's slot was a 4-byte uint32_t the 8-byte
        // InternalPtr store overran (the value is never read).
        ::Object::InternalPtr<Action> pathResult;
        WorkAct_GatherRawMatCtor_bf790(&pathResult, &citizenVal, pathType, &destEntityCopy,
                                searchPos, &workEntity, &destPos.x,
                                itemEntity, availCount, pathFlags, 0, 1, 0, 0);

        // Get the Slot10 result (vtable +0x50)
        CitizenComponent* citizenPtr = *citizenAI;

        Profession* entityRefOut = nullptr;
        Profession* resultVal = *GetTarget(&entityRefOut);

        // Update citizen state
        citizenPtr->statusFlags |= Bits(CitizenStatus::TaskDone);
        citizenPtr->lastWorkId = pathType;
        citizenPtr->lastErrand = CitizenErrand::None;

        if (resultVal == citizenPtr->profession && pathType >= 0) {
            citizenPtr->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
        }

        // Register work with citizen AI (the tree helpers take the root as uintptr_t*)
        WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses entityRefOut's)
        WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenAI);
        return 2;
    }

    return 1;  // No valid items found
}

// ============================================================
// ClearStorage::Validate (Slot9, vtable +0x48)
// Original: 0x1400e69f0
// ============================================================
// Traverses the BST at self+0x58, checking each stored task via
// Work::CanStartWork. Invalid tasks are collected into a temporary
// array and then removed from the BST.
//
// Returns: true (1) if no entries remain (all cleared), false (0) otherwise.
//
// NOTE: Hook is DISABLED due to bug class 3 (MOV RAX,RSP in prologue).
// This clean implementation exists for documentation and future standalone exe.
int ClearStorage::Validate() {

    // Phase 1: In-order traverse BST, collect entries that fail CanStartWork
    AssignmentNode** removeList = nullptr;
    int removeCount = 0;
    int removeCapacity = 0;

    // Find leftmost node (smallest key)
    AssignmentNode* node = _citizens._root;
    if (node == nullptr) goto phase2;

    // Go to leftmost
    while (node->left != nullptr) {
        node = node->left;
    }

    // In-order traversal
    while (node != nullptr) {
        // Get task key from node
        CitizenComponent* taskIBP = node->element;

        // Check if task is still valid
        bool canStart = ValidateTask(nullptr, &taskIBP, nullptr);   // 0x1400f7100

        if (!canStart) {
            // Need to add this node to remove list
            int newCount = removeCount + 1;
            if (newCount > removeCapacity) {
                // Grow capacity
                int newCap = newCount;
                if (removeCount != 0 && newCount == removeCount + 1) {
                    newCap = newCount * 3;
                }
                removeCapacity = newCap;
                AssignmentNode** newList = static_cast<AssignmentNode**>(System::MemoryAllocator::MemAlloc((uint64_t)newCap * 8, 8));

                // Copy existing entries
                int copyStart = 0;
                if (removeList == newList) {
                    copyStart = removeCount;
                }
                for (int j = copyStart; j < newCount; j++) {
                    if (newList != nullptr) {
                        AssignmentNode* src = (j < removeCount && removeList != nullptr)
                            ? removeList[j]
                            : node;  // Current node for the new entry
                        newList[j] = src;
                    }
                }

                if (removeList != newList && removeList != nullptr) {
                    System::MemoryAllocator::MemFree(removeList);
                }
                removeList = newList;
            } else {
                // Enough capacity, just store
                if (removeList != nullptr) {
                    removeList[removeCount] = node;
                }
            }
            removeCount = newCount;
        }

        // Move to next in-order node
        AssignmentNode* right = node->right;
        if (right != nullptr) {
            // Go to leftmost of right subtree
            node = right;
            while (node->left != nullptr) {
                node = node->left;
            }
        } else {
            // Go up until we come from a left child
            AssignmentNode* parent = node->parent;
            if (parent != nullptr && parent->left == node) {
                // We are left child of parent, parent is next
                node = parent;
            } else {
                // We are right child, go up while we are right child
                while (parent != nullptr && parent->right == node) {
                    node = parent;
                    parent = node->parent;
                }
                node = parent;
            }
        }
    }

phase2:
    // Phase 2: Remove collected entries from BST
    if (removeCount > 0) {
        for (int i = 0; i < removeCount; i++) {
            // Find node in BST by key
            AssignmentNode* searchNode = _citizens._root;
            CitizenComponent* key = removeList[i]->element;

            while (searchNode != nullptr) {
                CitizenComponent* nodeKey = searchNode->element;
                if (key < nodeKey) {
                    searchNode = searchNode->left;
                } else {
                    if (key <= nodeKey) {
                        // Found it, remove (the tree helpers take the root as uintptr_t*: LEFT)
                        WorkBST_BSTRemove_a6e0(&_citizens, searchNode);
                        break;
                    }
                    searchNode = searchNode->right;
                }
            }
        }
    }

    // Free remove list
    System::MemoryAllocator::MemFree(removeList);

    return removeCount == 0 ? 1 : 0;
}

// Retail 0x1400e73b0: stamp 0x1401611a0; the +0x58 set's inlined Clear (0x140014c30 on the
// root's +0x10 / +0x18 children, MemFree [0x140152b68] of the root, root = 0 qword, count =
// 0 dword); MemFree +0x48, +0x48 = 0, +0x50 = 0 (qwords); stamp Work 0x140161a80 and
// tail-jump ~Internal. That is the compiler's chain over the two members in reverse
// declaration order: ~Set (Clear), then ~Array (_field48).
ClearStorage::~ClearStorage() = default;

// ============================================================
// ClearStorage::Slot6 (vtable +0x30, 0x1400e6c30)
// ============================================================
// BST find-and-remove by key on the citizen BST at self+0x58,
// then returns false (0). This removes a specific citizen's
// entry from the work tracking tree.
int ClearStorage::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {

    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}

// ============================================================
// ClearStorage::Slot7 (vtable +0x38, 0x1400e6c20) -- task CANCELLED
// ============================================================
// Retail is the 6-byte thunk `ADD RCX,0x58; JMP CitizenAI_impl_5520`: the same
// remove-by-citizen as slot 6, without the return-0 wrapper.
//
// ⚠ This slot was pinned to Work_SlotRet0 ("NOT IMPLEMENTED"), so a cancelled
// clear-storage task left its citizen node in the +0x58 tree forever. That is the
// exact stale-entry family HarvestArea::Slot7 had (harvestarea.cpp): CancelAll below
// spins on the leftmost node until the count drops, and Validate hands the stale
// citizen pointer to Object::IsA, which CALLs through it. Ported 2026-09-05.
int ClearStorage::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {

    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}

// ============================================================
// ClearStorage::Slot11 (vtable +0x58, 0x14002a0f0) -- HasActiveTasks
// ============================================================
// Retail: `return 0 < *(int*)(self + 0x60)`. ClearStorage keeps its citizen tree at
// +0x58 with the count at +0x60 (one qword later than the +0x48/+0x50 layout the
// shared Work_Slot11_HasActiveTasks reads), so it needs its own body. Was pinned to
// Work_SlotRet0, i.e. permanently "nobody is working this".
int ClearStorage::GetPriority() {
    return (int)_citizens._count > 0 ? 1 : 0;   // retail's signed compare of the dword
}

// ============================================================
// ClearStorage::CancelAll (Slot8, vtable +0x40, 0x1400e72a0)
// ============================================================
// Iterates the BST at self+0x58 by finding the leftmost node
// each iteration. For each node, resolves the citizen entity,
// navigates to its state machine component (+0x278), and cancels
// the pending work action via TaskCancel on its action list (+0x48).
// Repeats until the BST is empty (count at self+0x60 drops to 0).
int ClearStorage::Cleanup() {

    while (true) {
        AssignmentNode* node = _citizens._root;
        if (node == nullptr) {
            return 0;
        }

        // Find leftmost node in BST
        AssignmentNode* leftmost = node;
        AssignmentNode* left = leftmost->left;
        while (left != nullptr) {
            leftmost = left;
            left = leftmost->left;
        }

        if (leftmost == nullptr) {
            return 0;
        }

        // The node's citizen; cancel its CharacterComponent's action list (+0x48)
        CitizenComponent* citizen = leftmost->element;
        citizen->character->actionStack.Clear();
    }
    return 0;
}

// ============================================================
// ClearStorage::GetTarget (Slot10, vtable +0x50, 0x1400e7330)
// ============================================================
// Returns the profession list's +0x30 profession (GameUpdateLoop::m_professionList).
// Uses hidden return pointer ABI (member function returning non-trivial).
Profession** ClearStorage::GetTarget(Profession** retbuf) {
    *retbuf = GameUpdateLoop::_singleton->m_professionList->laborerProfession;
    return retbuf;
}

// ============================================================
// Slot mapping ground-truthed from the real exe vtable @ 0x1401611a0 (2026-09-05):
//   slot6 OnComplete (0x1400e6c30), slot7 OnCancel (0x1400e6c20, same remove),
//   slot8 Cleanup (0x1400e72a0), slot11 GetPriority / HasActiveTasks (0x14002a0f0,
//   count at +0x60). ENDGAME B.3.8: all members of the class now.
// ============================================================

// ============================================================
// Slot3 Serialize (vtable +0x18, retail 0x1400e7350) — was Work_SerializeNop.
// ============================================================
// A no-op here consumes ZERO bytes on load while retail's body consumes the
// ClearStorage task BST, so every save that contains a live ClearStorage work
// object desynced the entity stream on the record AFTER it. Retail, read off the
// 0x1400e7350 disassembly (22 instructions, no branches):
//
//   Work::Serialize(this, stream)                       CALL 0x1400f7400
//   IO::Serializer ser(stream, 0)                       CALL [0x140152ad8]
//   CitizenAI_impl_96b0(this + 0x58, &ser)              CALL 0x1400296b0   <- the BST
//   ~IO::Serializer(ser)                                CALL [0x140152ad0]
//
// Identical in shape to FireWork::Serialize (0x1400e9a50, firework.cpp) minus the
// trailing capacity int — FireWork's BST is at +0x50, ClearStorage's at +0x58
// (LEA RCX,[RDI+0x58] at 0x1400e7381). Both sub-calls are already ported and
// registered (reg_work_base.cpp / reg_work_bst.cpp), so this is a transcription.
// ENDGAME B.3.8: the two function-local lazy pointer caches are gone — the base call
// is the qualified Work::Persist, the BST body is called by name (B38 D7).
void ClearStorage::Persist(IO::Stream& stream) {

    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    IO::Serializer ser(stream, 0);
    WorkBST_BST_Serialize_296b0(&_citizens, &ser);   // 0x1400296b0 (work_base_bst.cpp)
}

// ============================================================
// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The stamps are the compiler's.
// ============================================================
ClearStorage::ClearStorage()
    : Work() {
}

ClearStorage::ClearStorage(ComponentEntity* const* pEntityIBP)
    : Work(pEntityIBP) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_ClearStorage_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over ClearStorage_Factory / _CreateInPlace, both
// Work_GenericCtor (the object zero-filled from +0x10, +0x20/+0x28 = -1).

// create 0x14002a0b0: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* ClearStorage::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ClearStorage), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ClearStorage());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* ClearStorage::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ClearStorage());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(ClearStorage, "Work")
