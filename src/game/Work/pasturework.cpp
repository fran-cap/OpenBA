// src/game/Work/pasturework.cpp
// Clean C++ reimplementation of PastureWork -- all methods.
// Source: Game/Work/PastureWork.cpp (from profile build debug strings)
//
// PastureWork (0x58 bytes) -- Pasture animal work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> PastureWork
// Vtable: 0x1401617a0
//
// PastureWork is the most complex Work subclass. Slot4 handles:
// - Checking animal count vs max workers
// - Resource capacity checks
// - Removing animals from pasture (via RemoveAnimal action)
#include "Work/exe_bodies.h"
// - Finding empty pasture positions for animals
// - Creating grazing work actions
//
// Slot7 is unique: tree insertion + clearing "flagged for removal" entity bits.
// Slot8/Slot9/Slot10/Serialize/Dtor are identical patterns to HuntWork.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/PastureWork.h"
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
#include <cstring>
#include <new>
#include "Component/PastureDescription.h"   // ENDGAME B.3.9 D5
#include "Component/AnimalDescription.h"    // ENDGAME B.3.9 D5
#include "Component/PastureComponent.h"     // ENDGAME B.3.9 D5
#include "Component/AnimalComponent.h"
#include "Component/LivestockComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/InteractComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/MapComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
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
//   0x140006270  WorkBase_FindComponent_6270
//   0x14012cf10  StorageComponent::QueryCount
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x140021a30  WorkBST_RegisterWork_1a30
//   0x140115e60  CitizenAI_State_LongTermPlan
//   0x140004fe0  WorkLeaf_FindComp_4fe0
//   0x140004680  FloorComponent_FindResData
//   0x140008830  WorkBase_CalcDistance_8830
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x140005500  ActHunt_FindComponent_5500
//   0x140002ed0  WorkBase_FindComponent_2ed0
//   0x1400223c0  ActRm_FindComponent_223c0
//   0x1400012d0  ActMv_FindComponentFamily_12d0
//   0x1400296b0  WorkBST_BST_Serialize_296b0
//   0x1400f7100  Work::ValidateTask
//   0x14001a6e0  WorkBST_BSTRemove_a6e0
//   0x140025520  WorkBST_BSTFindRemove_25520
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  AttendanceComponent_GetJobHostComponent
//   0x140014c30  WorkBase_TreeNodeFree_14c30
//   0x140128350  WorkLeaf_Pasture_8350
//   0x1400bb0a0  WorkAct_GrazingWorkCtor_bb0a0
//   0x1400bbea0  Pf_RemoveAnimalCtor_bbea0
//   0x140122e20  Pf_EntityResolvePos_2e20
//   0x140123070  Pf_RecursiveChildSum_123070
//   0x1400f8620  WorkGrid_TryAssignWorkplace_f8620
//   0x1400c3470  ActHunt_NotifyEntityChanged_c3470
//   0x140172114  kRdata_140172114 (.rdata)
//
// Vtable pointers for dtor chain
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: PastureWork_Ctor
// disassembles to `0x1400f2da9: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// The AutoPickupWork vtable it then overwrites with is 0x140160fa0 (the next LEA
// in the same ctor); only after that comes this class's own vtable.
// These were previously 0x1401612e0 / 0x140161340, neither of which is a vtable:
// both are INTERIOR addresses of the FireWork (0x1401612a0) and FishingWork
// (0x140161320) vtables, and Ghidra reports ZERO xrefs to either.


// ============================================================
// PastureWork::Slot4 -- Pasture work assignment (0x1400F2370)
// ============================================================
//
// This is the most complex Slot4 in the Work hierarchy (509 instructions, 279 lines).
// Three main branches:
//   A) If pasture resource NOT at capacity -> find empty animal slot, create RemoveAnimal action
//   B) If at capacity -> iterate position component for overflow storage work
//   C) Final fallback -> random position via Area component, create grazing work action
//
// Decompiled from Ghidra at 0x1400F2370, all control flow matched to original.
//
int PastureWork::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // local_res20 = *(longlong*)param_2;  local_res10 = param_2
    CitizenComponent* local_res20 = *citizenIBP;

    // -------------------------------------------------------
    // Step 1: Check auto-pickup assignments (0x1400e3db0, Work::TryAutoPickup)
    // -------------------------------------------------------
    int checkResult = TryAutoPickup(&local_res20);
    if (checkResult == 2) {
        return 2;
    }

    // -------------------------------------------------------
    // Step 2: Find the WorkComponent (0x140003050) and PastureComponent (0x140004fe0)
    // -------------------------------------------------------
    WorkComponent* local_58 = _entity->FindComponent<WorkComponent>();

    PastureComponent* local_a8 = _entity->FindComponent<PastureComponent>();

    // -------------------------------------------------------
    // Step 3: Get pasture description and worker count
    // -------------------------------------------------------
    Description* pvVar20 = local_a8->_description;  // pasture component description
    bool bVar8 = Object::IsDerivedFromNullSafe(pvVar20, PastureDescription::TypeId());  // DAT_1401c7bd8
    int lVar18 = _citizens._count;  // worker count at self+0x50
    PastureDescription* pvVar19 = nullptr;
    if (bVar8) {
        pvVar19 = static_cast<PastureDescription*>(pvVar20);
    }

    // -------------------------------------------------------
    // Step 4: Check max workers (the tree's port of 0x14012e5c0 called here takes the
    // component as an integer: LEFT)
    // -------------------------------------------------------
    if (local_58->workState == 0) {
        int iVar9 = local_58->CalcMaxWorkers();
        local_58->workState = iVar9;
    }

    // Original: if ((int)lVar18 <= workState) { ... whole body ... }
    // Inverted: if workerCount > maxWorkers, skip to return 1 at end
    if (lVar18 > local_58->workState) {
        return 1;
    }

    // -------------------------------------------------------
    // Step 5: Check resource capacity via the town's per-resource counts
    //   (CitizenBehavior +0x170, indexed by the PastureDescription's type)
    // -------------------------------------------------------
    {
        int descIndex = (int)pvVar19->_type;
        const ProfessionResourceEntry& entry =
            GameUpdateLoop::_singleton->m_citizenList->professionResources._data[descIndex];
        int currentCount = entry.currentCount;
        int maxCount = entry.maxCount;
        bVar8 = (maxCount <= currentCount);  // at capacity?

        // Set the status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
        StatusIconComponent* meshInst = _entity->FindComponent<StatusIconComponent>();
        meshInst->SetFlag(2, bVar8 ? 1 : 0);
    }

    // -------------------------------------------------------
    // Branch A: NOT at capacity (bVar8 == false) -> find empty slot, remove animal
    // -------------------------------------------------------
    if (!bVar8) {
        GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
        Map* map = gameWorld->m_game;
        // The map helpers (0x14009ccf0, 0x1400a00f0, 0x14009f3e0) take the map as int*:
        // LEFT.
        Map* piVar4 = map;

        // Get citizen position for pathfinding: CharacterComponent (+0x278) ->
        // ModelComponent (+0x68) -> the transform's translation
        const Math::Vector3& citPos = (*citizenIBP)->character->modelComponent->transform.translation;

        int iVar21 = map->size;  // map size
        (void)citPos.z;  // local_48: read but unused (matches original)
        int iVar9 = (int)(citPos.x + (float)(iVar21 >> 1));
        int iVar10 = (int)(citPos.y + (float)(iVar21 >> 1));
        if (iVar9 < 0) iVar9 = 0;
        if (iVar21 <= iVar9) iVar9 = iVar21 - 1;
        if (iVar10 < 0) iVar10 = 0;
        if (iVar21 <= iVar10) iVar10 = iVar21 - 1;
        uint32_t uVar23 = iVar21 * iVar10 + iVar9;  // citizen cell index

        // Check if pasture is over its capacity (capacity < animal count)
        int pastureCount = local_a8->animals._size;
        if (local_a8->capacity < pastureCount) {
            // Find an animal to remove via helper (0x140128350 takes the component as an
            // integer: LEFT)
            int slotIdx = local_a8->FindOldestLivestock();
            if (slotIdx >= 0) {
                // The livestock at that slot, its entity's AnimalComponent (0x140005500)
                LivestockComponent* livestock = local_a8->animals[slotIdx].Get();

                AnimalComponent* natResIBP = livestock->_entity->FindComponent<AnimalComponent>();

                if (natResIBP != nullptr) {
                    // Check if not flagged for removal (bit 20 of the component flags)
                    if (((natResIBP->_flags >> 0x14) & 1) == 0) {
                        // The animal entity's CharacterComponent (the guarded finder 0x1400012d0)
                        CharacterComponent* familyComp = natResIBP->_entity.Get()->FindComponent<CharacterComponent>();
                        const Math::Vector3& animalPos = familyComp->modelComponent->transform.translation;

                        // Get animal position as cell index
                        iVar21 = map->size;
                        (void)animalPos.z;  // local_48: read but unused
                        iVar9 = (int)(animalPos.x + (float)(iVar21 >> 1));
                        iVar10 = (int)(animalPos.y + (float)(iVar21 >> 1));
                        if (iVar9 < 0) iVar9 = 0;
                        if (iVar21 <= iVar9) iVar9 = iVar21 - 1;
                        if (iVar10 < 0) iVar10 = 0;
                        if (iVar21 <= iVar10) iVar10 = iVar21 - 1;
                        int animalCell = iVar21 * iVar10 + iVar9;

                        // Pathfinding check
                        uint32_t uVar11 = (uint32_t)piVar4->FindCell(animalCell, uVar23, PathBits::Walk);
                        uint64_t uVar16 = (uint64_t)piVar4->ValidateCell(PathBits::Walk, (uint32_t)uVar11, uVar23);

                        if ((char)uVar16 != '\0') {
                            // The AnimalComponent's description for the RemoveAnimal action
                            Description* descPtr = natResIBP->_description;
                            bool isDerived = Object::IsDerivedFromNullSafe(descPtr, AnimalDescription::TypeId());
                            AnimalDescription* animalDesc = nullptr;
                            if (isDerived) {
                                animalDesc = static_cast<AnimalDescription*>(descPtr);
                            }

                            // Compute world position from cell index
                            float halfOff = (float)(map->size >> 1) - kRdata_140172114;
                            Math::Vector3 local_80(
                                (float)((int)uVar11 % map->size) - halfOff,
                                (float)((int)uVar11 / map->size) - halfOff,
                                0.0f);
                            float local_70[4];  // Vector3 + padding
                            map->GetTerrainPos(local_70, &local_80.x, '\0', nullptr);

                            // Build RemoveAnimal action (0x1400bbea0)
                            CitizenComponent* local_90 = *citizenIBP;
                            Component* local_a0_ibp = natResIBP;   // the AnimalComponent IBP
                            RemoveAnimalActions(
                                &local_90,              // citizenIBP
                                _workId,               // priority
                                local_70,               // position
                                animalCell,             // cell index
                                &local_a0_ibp,          // the AnimalComponent IBP
                                animalDesc,             // description ptr
                                1);                     // flag (CONCAT31 result)

                            // Assign work to citizen
                            int priority = _workId;
                            CitizenComponent* citizenEnt = *citizenIBP;

                            // Call vtable slot +0x50 (GetTarget)
                            Profession* workEnt = nullptr;
                            GetTarget(&workEnt);

                            citizenEnt->statusFlags |= Bits(CitizenStatus::TaskDone);
                            citizenEnt->lastWorkId = priority;
                            citizenEnt->lastErrand = CitizenErrand::None;

                            if (workEnt == citizenEnt->profession && priority >= 0) {
                                citizenEnt->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                            }
                            return 2;
                        }
                    }
                }
            }
            // Slot search failed
            return 1;
        }

        // -------------------------------------------------------
        // Branch B: At animal capacity -> try position-based overflow storage work
        // -------------------------------------------------------
        InteractComponent* posComp = _entity->FindComponent<InteractComponent>();

        int posCount = posComp->NodeCount(2);   // 0x140123070

        if (posCount > 0) {
            int iPvVar20 = 0;
            do {
                Vector3Slot local_80;
                posComp->NodePosition(&local_80.v, 2, iPvVar20, nullptr);

                int iVar10map = map->size;
                int iVar21pos = (int)(local_80.v.x + (float)(iVar10map >> 1));
                int iVar12 = (int)(local_80.v.y + (float)(iVar10map >> 1));
                if (iVar21pos < 0) iVar21pos = 0;
                if (iVar10map <= iVar21pos) iVar21pos = iVar10map - 1;
                if (iVar12 < 0) iVar12 = 0;
                if (iVar10map <= iVar12) iVar12 = iVar10map - 1;

                // Read the occupant from the map grid cell
                ComponentEntity* occupant = map->cells.GetData()[(uintptr_t)iVar12 * (uintptr_t)iVar10map + (uintptr_t)iVar21pos].occupant.Get();

                if (occupant != nullptr) {
                    // Its RawMaterialComponent (0x1400223c0)
                    RawMaterialComponent* rawMat = occupant->FindComponent<RawMaterialComponent>();

                    if (rawMat != nullptr) {
                        // Its WorkComponent (0x140003050)
                        WorkComponent* storageWork = occupant->FindComponent<WorkComponent>();

                        // (0x1400f8620 takes the scheduler as an integer: LEFT)
                        CitizenComponent* local_90_val = *citizenIBP;
                        WorkScheduler* storageWorkMgr = gameWorld->m_tutorial;
                        uint64_t overflowResult = storageWorkMgr->TryAssignWorkplace(&local_90_val,
                            storageWork->workId, '\x01');

                        if ((char)overflowResult != '\0') {
                            return 2;
                        }
                    }
                }

                iPvVar20++;
            } while (iPvVar20 < posCount);
        }

        // -------------------------------------------------------
        // Branch C: Final fallback -> random area position, create grazing work
        // (the MapComponent footprint rect, 0x140004680)
        // -------------------------------------------------------
        MapComponent* area = _entity->FindComponent<MapComponent>();

        System::Random* rng = gameWorld->m_random;

        short sVar1 = area->tileX0;
        short sVar2 = area->tileY0;
        short sVar3 = area->tileX1;

        // Random::Rand(h) then Rand(w), the bounded overload (0x1400f299d / 0x1400f29aa)
        int randCell = WorkRules::PastureGrazeCell(*rng, sVar1, sVar2, sVar3, area->tileY1, map->size);

        int nearCell = piVar4->FindCell(randCell, uVar23, PathBits::Walk);
        if (nearCell >= 0) {
            uint64_t canPath = (uint64_t)piVar4->ValidateCell(PathBits::Walk, uVar23, nearCell);
            uint32_t dVar7 = PastureComponent::TypeId();  // DWORD_1401c7ba0

            if ((char)canPath != '\0') {
                float halfOff = (float)(map->size >> 1) - kRdata_140172114;
                Math::Vector3 local_80(
                    (float)(randCell % map->size) - halfOff,
                    (float)(randCell / map->size) - halfOff,
                    0.0f);
                float local_a0_pos[4];  // position buffer
                map->GetTerrainPos(local_a0_pos, &local_80.x, '\0', nullptr);

                halfOff = (float)(map->size >> 1) - kRdata_140172114;
                Math::Vector3 local_70_pos(
                    (float)(nearCell % map->size) - halfOff,
                    (float)(nearCell / map->size) - halfOff,
                    0.0f);
                // Retail 0x1400F2370: this local is a Vector3 (12 bytes) that GetTerrainPos
                // fills; the port had it as float[2], 4 bytes short -- a stack overwrite.
                Math::Vector3 local_50;  // dest position
                map->GetTerrainPos(&local_50.x, &local_70_pos.x, '\0', nullptr);

                // Create grazing work action (FUN_1400bb0a0)
                ComponentEntity* workEntity = _entity.Get();
                CitizenComponent* local_90_val = *citizenIBP;
                WorkAct_GrazingWorkCtor_bb0a0(
                    &local_90_val,          // citizenIBP value
                    _workId,               // priority
                    &workEntity,            // work entity IBP
                    0,                      // param4
                    &local_50.x,            // dest position
                    local_a0_pos,           // source position
                    dVar7,                  // type ID (DWORD_1401c7ba0)
                    0,                      // param8
                    0);                     // param9

                // Assign work to citizen
                int priority = _workId;
                CitizenComponent* citizenEnt = *citizenIBP;

                // ⚠ Retail saves the citizen IBP pointer (pIVar15) BEFORE the
                // vtable+0x50 call: slot 10 writes its result (the workplace's
                // profession key, wpc+0x48) THROUGH the out param — i.e. it
                // OVERWRITES local_res10. The register call below must use the
                // SAVED pointer; passing the clobbered local made the BST key
                // *(Profession*) = g_Prof_Vtable, and the next Slot9 validate
                // crashed IsA on it (organic herder, first completed cycle).
                // Here the out slot is its own local.
                Profession* workEnt = nullptr;
                GetTarget(&workEnt);

                citizenEnt->statusFlags |= Bits(CitizenStatus::TaskDone);
                citizenEnt->lastWorkId = priority;
                citizenEnt->lastErrand = CitizenErrand::None;

                // Profession match: slot 10's result vs citizen+0x1F0 (Profession*).
                if (workEnt == citizenEnt->profession && priority >= 0) {
                    citizenEnt->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                }

                // Register work with CitizenAI (CitizenAI_impl_1a30):
                // (tree root = self+0x48, outResult = the GetTarget slot, key = citizen IBP;
                // the helper takes the container as uintptr_t*: LEFT)
                WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEnt's)
                WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenIBP);
                return 2;
            }
        }
        return 0;
    }

    // Worker count at or below max but resource at capacity -> return 1
    return 1;
}


// ============================================================
// PastureWork::Slot6 -- NOT HOOKABLE (RIP-relative CALL)
// Thunk: sub rsp,0x28; add rcx,0x48; call CitizenAI_impl_5520; xor al,al; add rsp,0x28; ret
// (The tree helper takes the container as uintptr_t*: LEFT.)
// ============================================================
int PastureWork::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return false;
}


// ============================================================
// PastureWork::Slot7 (0x1400F22C0) -- OnCancel
// 1. Remove the citizen from the BST at self+0x48 (0x140025520)
// 2. If the param3 entity is non-null (0x1400f22d2):
//    a. IsDerivedFrom ComponentEntity (cell 0x1401c5d70) ? entity : null (CMOVNZ)
//    b. Find its AnimalComponent (0x140005500), no null test
//    c. ALWAYS clear bit 20 (0x100000) of the component flags at +0x20 (BTR, 0x1400f2319)
//    d. Notify the entity (+0x10, 0x1400c3470) only when (old ^ new) & 0xFFFF != 0
//       (0x1400f2320 XOR / 0x1400f2322 MOVZX EAX,AX). Retail quirk, copied: bit 20 is
//       outside the low 16 bits, so the test is always 0 and the notify never runs.
// ============================================================
int PastureWork::OnCancel(CitizenComponent** param2, ComponentEntity** param3, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);

    ComponentEntity* entity = *param3;
    if (entity == nullptr) {
        return 0;
    }

    ComponentEntity* asEntity = entity->IsDerivedFrom(ComponentEntity::TypeId()) ? entity : nullptr;
    AnimalComponent* animal = asEntity->FindComponent<AnimalComponent>();

    uint32_t oldFlags = animal->_flags;
    uint32_t newFlags = oldFlags & ~0x100000u;
    animal->_flags = newFlags;
    if (static_cast<uint16_t>(oldFlags ^ newFlags) != 0) {   // never true (retail quirk above)
        ComponentEntity* parentEntity = animal->_entity.Get();
        if (parentEntity != nullptr) {   // 0x1400f2329 CMP [RBX+0x10],0 (retail's own test)
            parentEntity->RefreshEntityFlags();   // 0x1400c3470
        }
    }
    return 0;
}


// ============================================================
// PastureWork::Slot8 / CancelAll (0x1400F2B90)
// Identical to HuntWork::Slot8 -- loops tree, cancels each.
// ============================================================
int PastureWork::Cleanup() {
    int count = _citizens._count;

    while (count > 0) {
        AssignmentNode* node = _citizens._root;
        AssignmentNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        CitizenComponent* citizenEntity = leftmost->element;

        citizenEntity->character->actionStack.Clear();

        count = _citizens._count;
    }
    return 0;
}


// ============================================================
// PastureWork::Slot9 / RemoveInvalid (0x1400F2090)
// Identical to HuntWork::Slot9 -- BST traversal, validates, removes.
// Hook DISABLED (bug class 3 -- MOV RAX,RSP).
// ============================================================
int PastureWork::Validate() {
    AssignmentNode* root = _citizens._root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root == nullptr) {
        return true;
    }

    // Find leftmost node
    AssignmentNode* node = root;
    while (node->left != nullptr) {
        node = node->left;
    }

    // In-order traversal
    while (node != nullptr) {
        CitizenComponent* entityIBP = node->element;

        bool stillValid = ValidateTask(nullptr, &entityIBP, nullptr);

        if (!stillValid) {
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

    // (keys compare unsigned; the tree helper takes the container as uintptr_t*: LEFT)
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
// PastureWork::Slot10 / GetWorkEntity (0x1400F2C00)
// Identical to HuntWork::Slot10: the entity's WorkPlaceComponent (0x140002e10), its
// profession (+0x48) into the return slot.
// ============================================================
Profession** PastureWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// PastureWork::GetPriority [11] 0x140029860 (shared by 13 tables) / HasWorkers.
// Returns (this->_citizens._count > 0).
// ============================================================
int PastureWork::GetPriority() {
    // Retail 0x140029860 is `CMP [RCX+0x50],EAX; SETG` — a SIGNED compare.
    return _citizens._count > 0 ? 1 : 0;
}




// ============================================================
// PastureWork::Serialize (0x1400F2C60)
// Work::Serialize + Serializer(version=0) + BST serialize.
// Identical to HuntWork::Serialize (no extra int field).
// ============================================================
void PastureWork::Persist(IO::Stream& stream) {
    Work::Persist(stream);   // 0x1400f7400, a direct call in retail — QUALIFIED here

    {
        IO::Serializer serializer1(stream, 0);
    }

    IO::Serializer serializer2(stream, 0);

    // (the tree helper takes the container as uintptr_t*: LEFT)
    WorkBST_BST_Serialize_296b0(&_citizens, &serializer2);
}


// ============================================================
// PastureWork::DtorBody (0x1400F2CE0)
// Identical pattern to HuntWork, uses vtable_PastureWork.
// ============================================================
// PastureWork dtor (retail 0x1400f2ce0, called by slot 0 0x14002bf70). The stamps and the `~Internal` call are the compiler's chain.
PastureWork::~PastureWork() = default;   // the member's ~Set is retail's inlined Clear (0x1400f2ce0)




// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
PastureWork::PastureWork() : AutoPickupWork() {
}

// ============================================================
// PastureWork::PastureWork (0x1400f2d80) — the retail ctor Slot14 cmd-2 calls
// when it turns the herder on. Base = Work_Ctor25 (0x1400f7250 pattern):
//   +0x10 = *entityIBP (raw 8-byte copy of the pasture entity ref)
//   +0x18 = WorkComponent ref (finder 0x140003050 on the same entity)
//   +0x20 = -1, +0x28 = -1 OR the Area component's center cell:
//           ((h>>1)+y)*mapDim + (w>>1)+x  from area +0x28/+0x2a/+0x2c/+0x2e
//   +0x24/+0x2c/+0x30/+0x38/+0x40 = 0
// PastureWork tail: vtable, +0x48 BST root = 0, +0x50 count = 0.
// The tree's port starts from the default shape (Work(): the zero fields and the two
// -1s) and adds the entity-dependent fields on top; that is kept.
// (The entity slot is void*, the caller's ABI: LEFT.)
// ============================================================
PastureWork::PastureWork(ComponentEntity* const* entityIBP) : AutoPickupWork() {
    _entity = *entityIBP;
    if (_entity != nullptr &&
        GameUpdateLoop::_singleton != nullptr) {
        MapComponent* a = _entity->FindComponent<MapComponent>();
        if (a != nullptr) {
            int mapDim = GameUpdateLoop::_singleton->m_game->size;
            _cell =
                (((int)a->tileY1 >> 1) +
                  (int)a->tileY0) * mapDim +
                ((int)a->tileX1 >> 1) +
                 (int)a->tileX0;
        }
        // 0x140003050 = the WorkComponent finder
        _workComponent = _entity->FindComponent<WorkComponent>();
    }
}
// Convenience for PastureComponent Slot14 (pasturecomponent_states.cpp, block MI-B):
// MemAlloc(0x58,8) + the faithful ctor. `entityRefSlot` points at an 8-byte entity ref
// (the pasture's owner entity).
extern "C" Work* PastureWork_CreateForEntity(ComponentEntity* const* entityRefSlot) {
    void* p = Object::detail::InternalMemAlloc(sizeof(PastureWork), 8);
    return p ? new (p) PastureWork(entityRefSlot) : nullptr;
}


// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_PastureWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over PastureWork_Factory / _CreateInPlace (both
// Work_GenericCtor), plus three vtable-pointer statics for the dtor's stamps.
// Nothing outside this block reads 0x1401617a0 back, so that row goes with the table.

// create 0x14002bf00: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* PastureWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PastureWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PastureWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* PastureWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PastureWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(PastureWork, "AutoPickupWork")
