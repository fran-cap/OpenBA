// src/game/Work/harvestorchard.cpp
// Clean C++ reimplementation of HarvestOrchard Work class.
// Source: Game/Work/HarvestOrchard.cpp (from profile build debug strings)
//
// HarvestOrchard (0x68 bytes) -- Orchard harvest work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> HarvestOrchard
// Vtable: 0x140161520
//
// Same struct layout as HarvestArea but different harvest logic:
// - Orchards use a different description component (OrchardDescription at DAT_1401c46b0)
// - Slot4 only harvests single trees (no paired cell logic like HarvestArea)
// - Slot4 uses FUN_1400bb7b0 to create harvest action (different from ShowTool_Action_Ctor4)
// - Slot4 also looks up RawMaterial description and passes extra params
// - Slot6/Slot7 use simple int key (not packed high/low 16-bit)
//
// Methods implemented:
//   Slot4 (0x1400ED8C0), Slot6 (0x1400ED880), Slot7 (0x1400ED810)
//   Slot8 (0x1400EDDC0), Slot9 (0x1400ED5E0), Slot10 (0x1400EDE40)
//   Serialize (0x1400EDEA0), ScalarDtor body (0x1400EDF60)
//

#include "Work/HarvestOrchard.h"
#include "Component/ComponentDescription.h"   // FindDescription<T>
#include "Component/RawMaterialDescription.h"   // FindDescription<RawMaterialDescription> (0x140006330)
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
#include <new>
#include "Component/OrchardDescription.h"   // ENDGAME B.3.9 D5: OrchardDescription::TypeId()
#include "Component/FruitDescription.h"     // ENDGAME B.3.9 D5: FruitDescription::TypeId()
#include "Component/FruitComponent.h"
#include "Component/OrchardComponent.h"
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

// Diagnostics-only counters (src/diag/orchard_diag.cpp). No-op unless
// BANISHED_ORCHARD_DIAG is set; never change behaviour.
#include "orchard_diag.h"   // standalone/: the orchard diagnostic counters

// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component/Tutorial/Game headers)
// ============================================================
//   AutoPickupWork::CheckAssignments (0x1400e3db0)  Work::TryAutoPickup
//   0x140003050 FindComponent MaterialDesc          WorkBase_FindComponent_3050
//   0x140002f90 FindComponent MeshInstance          WorkBase_FindComponent_2f90
//   0x140005440 FindComponent OrchardDescription    WorkAcc_FindComponent_5440
//   0x1400052c0 FindComponent NaturalResource       WorkBase_FindComponent_52c0
//   0x14000a0b0 FindComponent (description comp)    WorkLeaf_FindComp_a0b0
//   0x14012c3f0 MeshInstance set visible            CitizenComp_FUN_14012c3f0
//   0x14012e5c0 CalcMaxWorkers                      WorkComponent::CalcMaxWorkers
//   0x14009ccf0 Map_FindNearestPassable             WorkGrid_FindCell_9ccf0
//   0x1400a00f0 Map_CanPathTo                       WorkBase_ValidateCell_a00f0
//               (a uint64 whose low byte is the bool; read through uint8_t)
//   0x14009f3e0 GetTerrainPos                       Reimpl_MapGetTerrainPos
//   0x140006330 FindComponent RawMaterial           WorkAcc_FindComponent_6330
//   0x1400bb7b0 orchard harvest action ctor         WorkAct_GatherWorkCtor_bb7b0
//   0x140029a40 / 0x14001ca60 / 0x140029de0 cell BST insert / remove / serialize
//                                                   WorkCellBST_Insert_9a40 / _Remove_ca60 / _Serialize_9de0
//   0x1400f7100 ValidateTask                        Work::ValidateTask
//   0x1400c5400 cancel work                         WorkBase_TaskListCleanup_c5400
//   0x140002e10 FindPositionComponent               AttendanceComponent_GetJobHostComponent
//   0x14000e320 BST recursive free                  WorkBase_TreeNodeFree_e320
//   MemAlloc                                        System::MemoryAllocator::MemAlloc
//   DAT_140172114 = 0.5f                            kRdata_140172114


// ============================================================
// Helper: the claim tree searched by the int slot key
// ============================================================
static HarvestOrchard::ClaimNode* BST_Find(HarvestOrchard::ClaimNode* root, int key) {
    HarvestOrchard::ClaimNode* node = root;
    while (node != nullptr) {
        if (key < node->element.slot) {
            node = node->left;
        } else if (key > node->element.slot) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}


// ============================================================
// HarvestOrchard::Slot4 -- Main orchard harvest assignment logic
// ============================================================
//
// Key differences from HarvestArea:
// - Uses the OrchardComponent (DAT_1401c46b0) and its OrchardDescription
// - No paired cell harvesting (single tree per assignment)
// - Checks NaturalResource growth flags + description type
// - Uses FUN_1400bb7b0 to create harvest action (orchard-specific)
// - Passes raw material ref and material count to action constructor
// - Orchard slot data at +0x50 with stride 0x28 (not +0x58 with stride 0x20)
//
int HarvestOrchard::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    CitizenComponent* citizenRef = *citizenIBP;

    // Step 1: Check auto-pickup (0x1400e3db0, Work::TryAutoPickup)
    CitizenComponent* checkRef = citizenRef;
    int checkResult = TryAutoPickup(&checkRef);
    if (checkResult == 2) {
        return 2;
    }

    // Step 2: Find components (0x140003050 WorkComponent, 0x140005440 OrchardComponent)
    WorkComponent* matDesc = field_10->FindComponent<WorkComponent>();

    OrchardComponent* orchard = field_10->FindComponent<OrchardComponent>();

    // Get OrchardDescription object
    Description* descObj = orchard->_description;
    bool isDerived = Object::IsDerivedFromNullSafe(descObj, OrchardDescription::TypeId());
    OrchardDescription* orchardDesc = isDerived ? static_cast<OrchardDescription*>(descObj) : nullptr;

    // Check tree count vs max workers (the tree's port of 0x14012e5c0 called here takes
    // the component as an integer: LEFT)
    int treeCount = _cells._count;
    if (matDesc->workState == 0) {
        int maxWorkers = matDesc->CalcMaxWorkers();
        matDesc->workState = maxWorkers;
    }
    if (treeCount > matDesc->workState) {
        return 1;
    }

    // Check capacity (the town's per-resource counts, CitizenBehavior +0x170)
    int materialIdx = (int)orchardDesc->_type;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    const ProfessionResourceEntry& resEntry = gameWorld->m_citizenList->professionResources._data[materialIdx];
    bool atCapacity = resEntry.maxCount <= resEntry.currentCount;

    // The status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
    StatusIconComponent* statusIcon = field_10->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);

    if (atCapacity) {
        return 1;
    }

    // Step 3: Get map and citizen position
    Map* map = gameWorld->m_game;
    // The map helpers (0x14009ccf0, 0x1400a00f0) take the map as int*: LEFT.
    Map* mapGrid = map;

    const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

    int mapSz = map->size;
    int citizenCellX = (int)(citizenPos.x + (float)(mapSz >> 1));
    int citizenCellY = (int)(citizenPos.y + (float)(mapSz >> 1));
    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSz) citizenCellX = mapSz - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSz) citizenCellY = mapSz - 1;
    uint32_t citizenGridCell = mapSz * citizenCellY + citizenCellX;

    // Step 4: Iterate orchard slots
    int fieldCellCount = _field58._size;
    [[maybe_unused]] float cellFloat = 0.0f;

    if (fieldCellCount > 0) {
        for (int i = 0; i < fieldCellCount; i++) {
            bool* validityArray = _field58._data;
            if (validityArray[i] == 0) {
                goto next_cell;
            }

            // Check if slot is already in the claim tree
            {
                ClaimNode* found = BST_Find(_cells._root, i);
                if (found != nullptr) {
                    goto next_cell;
                }
            }

            // Look up the occupant of this slot's cell
            {
                MapCell* gridData = map->cells.GetData();
                // Orchard slot stride is 0x28 (different from HarvestArea's 0x20)
                int cellGridIdx = orchard->slots[i].tile;
                ComponentEntity* entityAtCell = gridData[cellGridIdx].occupant.Get();

                if (entityAtCell == nullptr) {
                    validityArray[i] = 0;
                } else {
                    // Check pathfinding
                    int srcCellIdx = orchard->slots[i].tile;
                    int nearestCell =
                        mapGrid->FindCell(srcCellIdx, citizenGridCell, PathBits::Walk & ~PathBits::Obstacle);

                    if (nearestCell >= 0) {
                        uint64_t canPath = (uint64_t)static_cast<uint8_t>(mapGrid->ValidateCell(PathBits::Walk, citizenGridCell, nearestCell));   // the bool door read AL
                        if ((char)canPath != 0) {
                            // Check the NaturalResourceComponent (0x1400052c0)
                            NaturalResourceComponent* natRes = entityAtCell->FindComponent<NaturalResourceComponent>();

                            if (natRes == nullptr) {
                                validityArray[i] = 0;
                            } else {
                                // Check flag bit 20 (flagged for removal)
                                if ((natRes->_flags >> 20) & 1) {
                                    goto next_cell;
                                }

                                // The entity's FruitComponent (0x14000a0b0)
                                FruitComponent* fruit = entityAtCell->FindComponent<FruitComponent>();

                                Description* descCompObj = fruit->_description;
                                // DAT_1401c4288 = FruitDescription (ENDGAME B.3.9 D5: by
                                // cell, not the legacy variable name "DescComp").
                                bool isDescDerived = Object::IsDerivedFromNullSafe(descCompObj, FruitDescription::TypeId());
                                FruitDescription* descComp = isDescDerived ? static_cast<FruitDescription*>(descCompObj) : nullptr;

                                // The fruit's raw material (FruitDescription::_ref30)
                                ComponentDescription* rawMatRef = descComp->_ref30;
                                rawMatRef->FindDescription<RawMaterialDescription>();   // 0x140006330 (its result is never read)

                                // Arg 7 of the action ctor. Read from the FruitComponent
                                // (RBP-0x39, the 0x14000a0b0 finder's out-param), NOT the
                                // orchard component, and as a FLOAT:
                                //   1400edc74  MOV RAX,[RBP-0x39] ; 1400edc7f MOVSS XMM6,[RAX+0x30]
                                //   1400edd04  MOVSS [RSP+0x30],XMM6      <- 7th argument slot
                                // It used to be read as a uint32 off orchardDescIBP — wrong object
                                // and wrong type, so the callee (which takes a float here, and
                                // forwards it to CreateRawMaterial in XMM2) got an integer bit
                                // pattern from an unrelated field.
                                float rawMatAmount = fruit->ripeness;

                                // Calculate world position
                                float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
                                Math::Vector3 targetPos(
                                    (float)(nearestCell % map->size) - halfOffset,
                                    (float)(nearestCell / map->size) - halfOffset,
                                    0.0f);

                                float pathPos[4];
                                map->GetTerrainPos(pathPos, &targetPos.x, 0, nullptr);

                                // The orchard component's entity (+0x10)
                                ComponentEntity* entityRef = orchard->_entity.Get();

                                // Create orchard harvest action
                                CitizenComponent* actionBuf[2] = { *citizenIBP, nullptr };
                                // Arg 6 is the PARITY of the slot index, not "is this the first
                                // cell". Retail: 1400edd00 MOVZX EAX,SIL ; 1400edd0a NOT AL ;
                                // 1400edd14 AND AL,1 — with ESI = i, that is `!(i & 1)`. The old
                                // `(i == 0) ? 0 : 1` was both the wrong test and inverted.
                                char altCellFlag = (char)(~i & 1);

                                {   // arg 6 gates exactly ONE node of the action chain the
                                    // ctor builds: CreateRawMaterial (verified in retail
                                    // 0x1400bb7b0 — `if (param_6 != 0)` wraps the
                                    // MemAlloc(0x58) + CreateRawMaterial_Action_CopyCtor).
                                    // So on ODD slot indices the citizen picks the tree and
                                    // reports it, but spawns no produce. That is retail's
                                    // shape (MOVZX EAX,SIL / NOT AL / AND AL,1 at
                                    // 0x1400edd00, ESI = the loop index) — this trace exists
                                    // to measure what it COSTS, since "half the trees yield
                                    // nothing" and "yield is batched two trees per pile"
                                    // look identical from the outside.
                                    static int s_n = 0;
                                    if (s_n++ < 24) {
                                        fprintf(stderr, "[OrchardHarvest] slot=%d cell=%u "
                                                "amount=%.3f createsProduce=%d\n",
                                                i, (unsigned)nearestCell, (double)rawMatAmount,
                                                (int)altCellFlag);
                                        fflush(stderr);
                                    }
                                }

                                WorkAct_GatherWorkCtor_bb7b0(
                                    actionBuf,                     // citizenIBP
                                    (uint32_t)field_20,            // work id / priority (self+0x20)
                                    &entityRef,                    // entity ref ptr
                                    pathPos,                       // position
                                    rawMatRef,                     // raw material ref
                                    altCellFlag,                   // alternating-cell flag
                                    rawMatAmount,                  // float, -> CreateRawMaterial XMM2
                                    (uint32_t)nearestCell,         // map cell index
                                    (uint32_t)i);                  // SLOT index — the value that
                                                                   // comes back as the harvested-slot
                                                                   // id (CompleteTask p5 /
                                                                   // SendComponentMessage p6). This
                                                                   // used to be `cellFloat`, a float
                                                                   // in an integer slot, so every
                                                                   // completion reported the same
                                                                   // wrong slot. See
                                                                   // reference_int_arg_typed_as_float.

                                // Assign work and register with AI
                                int priority = field_20;
                                CitizenComponent* citizenEnt = *citizenIBP;

                                Profession* workEnt = nullptr;
                                GetTarget(&workEnt);

                                citizenEnt->statusFlags |= Bits(CitizenStatus::TaskDone);
                                citizenEnt->field_0x30 = priority;
                                citizenEnt->lastErrand = CitizenErrand::None;

                                if (workEnt == citizenEnt->profession && priority >= 0) {
                                    citizenEnt->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                                }

                                // Insert {slot, citizen} into the claim tree (the helper
                                // takes the container as uintptr_t*: LEFT)
                                ClaimNode* insertResult[2] = {nullptr, nullptr};
                                Claim keyBuf;
                                keyBuf.slot = i;
                                keyBuf._pad04 = 0;
                                keyBuf.citizen = *citizenIBP;
                                WorkCellBST_Insert_9a40(&_cells, insertResult, &keyBuf);

                                // Mark slot as used
                                validityArray[i] = 0;

                                OrchardDiag_NoteHarvestExecuted();
                                return 2;
                            }
                        }
                    }
                }
            }

        next_cell:
            cellFloat = (float)(i + 1);
        }
    }

    return 1;
}


// ============================================================
// HarvestOrchard::Slot6 -- task COMPLETED (retail 0x1400ed880)
// ============================================================
// Simpler than HarvestArea -- single int key, no paired cell.
//
// ⚠ This slot must NOT restore the validity byte. Retail 0x1400ed880 is exactly
// "walk the BST, remove the node if found, XOR AL,AL, RET" — there is no store to
// [self+0x58] anywhere in the function. Only the CANCEL slot (0x1400ed810, idx 7)
// writes validityArray[param4] = 1, and it does so on BOTH exits. The distinction is
// the whole point: a cancelled pick goes back in the pool, a COMPLETED one does not.
//
// This port had Slot7's body copied into Slot6, so every completed pick immediately
// re-marked its tree as available. Slot4 scans from i=0 and takes the lowest valid
// index, so one worker re-harvested slot 0 forever: 85 assignments against a single
// distinct picked slot, `picked` stuck at 1/12, and state 11 could never reach its
// `picked >= live` latch. The [OrchardGrow] harness corroborated it — exactly one of
// twelve trees was repeatedly zeroed and re-ripened.
int HarvestOrchard::OnComplete(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int param4) {
    // Search the claim tree for the key and remove if found (the helper takes the
    // container as uintptr_t*: LEFT)
    ClaimNode* node = _cells._root;
    while (node != nullptr) {
        if (param4 < node->element.slot) {
            node = node->left;
        } else if (param4 > node->element.slot) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // No validity-byte write here — see the header note. Retail: XOR AL,AL ; RET.
    return 0;
}


// ============================================================
// HarvestOrchard::Slot7 -- task CANCELLED (retail 0x1400ed810)
// ============================================================
// The mirror of Slot6: same BST walk, but it DOES restore the validity byte, on both
// the found and not-found exits (0x1400ed84d and 0x1400ed865). Cancelling a pick
// returns that tree to the pool.
int HarvestOrchard::OnCancel(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int param4) {
    // Search the claim tree for the key and remove if found (the helper takes the
    // container as uintptr_t*: LEFT)
    ClaimNode* node = _cells._root;
    while (node != nullptr) {
        if (param4 < node->element.slot) {
            node = node->left;
        } else if (param4 > node->element.slot) {
            node = node->right;
        } else {
            WorkCellBST_Remove_ca60(&_cells, node);
            break;
        }
    }

    // Mark slot as valid
    bool* validityArray = _field58._data;
    validityArray[param4] = 1;
    return 0;
}


// ============================================================
// HarvestOrchard::Slot8 -- CancelAll
// ============================================================
// The walk follows the LEFT child (+0x18 in the 0x30 node); the tree called the
// node "rightmost".
int HarvestOrchard::Cleanup() {
    int count = _cells._count;

    while (count > 0) {
        ClaimNode* node = _cells._root;
        ClaimNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        CitizenComponent* citizenEntity = leftmost->element.citizen.Get();
        citizenEntity->character->actionStack.Clear();

        count = _cells._count;
    }
    return 0;
}


// ============================================================
// HarvestOrchard::Slot9 -- RemoveInvalid
// ============================================================
// Identical structure to HarvestArea::Slot9.
// Hook DISABLED (bug class 2: MOV RAX,RSP in prologue).
int HarvestOrchard::Validate() {
    int* collectArray = nullptr;
    int* currentArray = nullptr;
    uint32_t collectCount = 0;
    uint32_t collectCapacity = 0;

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

    while (node != nullptr) {
        CitizenComponent* ibpData = node->element.citizen.Get();

        uint8_t valid = ValidateTask(nullptr, &ibpData, nullptr);

        if (valid == 0) {
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

            bool* validityArray = _field58._data;
            validityArray[node->element.slot] = 1;
        }

        // Move to next in-order node
        ClaimNode* right = node->right;
        if (right != nullptr) {
            ClaimNode* cur = right;
            while (cur != nullptr) {
                node = cur;
                cur = cur->left;
            }
        } else {
            ClaimNode* parent = node->parent;
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
    // (the helper takes the container as uintptr_t*: LEFT)
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
// HarvestOrchard::Slot10 -- GetWorkEntity: the entity's WorkPlaceComponent
// (0x140002e10), its profession (+0x48) into the return slot.
// ============================================================
Profession** HarvestOrchard::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// HarvestOrchard::Serialize
// ============================================================
void HarvestOrchard::Persist(IO::Stream& stream) {
    Work::Persist(stream);   // 0x1400f7400, a direct call in retail — QUALIFIED here

    {
        IO::Serializer serializer(stream, 0);
    }

    IO::Serializer serializer2(stream, 0);

    // (the helper takes the container as uintptr_t*: LEFT)
    WorkCellBST_Serialize_9de0(&_cells, &serializer2);

    int cellCount = _field58._size;
    int cellCountBuf[2] = { cellCount, 0 };
    serializer2.Serialize(cellCountBuf[0], nullptr);

    // 0x14000a590 == Collection::Array<bool>::SetSize.
    if (static_cast<int>(serializer2.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        _field58.SetSize(cellCountBuf[0]);
    }

    // Serialize validity array through the BOOL-ARRAY door.
    //
    // ENDGAME B.3.4 callers batch H — SETTLED FROM RETAIL. HarvestOrchard's
    // Serialize (Application-x64.exe 0x1400edea0) is instruction-for-instruction
    // the same as HarvestArea's at this site:
    //     1400edf2d  XOR R9D,R9D                    ; name = nullptr
    //     1400edf30  MOV R8,qword ptr [RDI + 0x58]  ; the buffer
    //     1400edf34  MOV EDX,dword ptr [RSP + 0x60] ; the count
    //     1400edf38  LEA RCX,[RSP + 0x28]           ; the Serializer
    //     1400edf3d  CALL qword ptr [0x140151458]
    // 0x140151458 is the four-parameter (int n, bool*, const char*) array door,
    // not the three-parameter `Serialize(int&)` scalar door 0x140152ac8 that
    // the old `s_fn_Serializer_SerializeBoolArray` alias pointed at (removed with
    // its door in the B.3.4a closing sweep). See
    // harvestarea.cpp for the full argument.
    serializer2.Serialize(cellCountBuf[0], _field58._data, nullptr);   // slot 0x140151458 = bool-array door
}


// The HarvestOrchard / AutoPickupWork / Work stamps and the `~Internal` call are the
// compiler's chain.
HarvestOrchard::~HarvestOrchard() {
    // The +0x58 flags are the `_field58` member's ~Array (MemFree, then +0x58 = +0x60 = 0),
    // then the +0x48 set's Clear is the `_cells` member's ~Set: reverse declaration order
    // after this empty body (0x1400edf60).
}



// [11] 0x140029860 (shared by 13 tables): `+0x50 > 0` — "is anyone assigned". The
// old table pointed at Work_Slot11_HasActiveTasks, the same body.
int HarvestOrchard::GetPriority() {
    return _cells._count > 0 ? 1 : 0;
}

// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
HarvestOrchard::HarvestOrchard()
    : AutoPickupWork() {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_HarvestOrchard_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over HarvestOrchard_Factory / _CreateInPlace (both
// Work_GenericCtor + the orchard diagnostic's construction count). Nothing outside this
// block reads 0x140161520 back (the from-entity ctor that stamped it is the real ctor
// now), so its registration goes with the table.

// create 0x14002a520: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* HarvestOrchard::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HarvestOrchard), 8);
    if (!p) return nullptr;
    Object::Base* r = new (p) HarvestOrchard();
    OrchardDiag_NoteHarvestConstructed();
    return r;
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* HarvestOrchard::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    Object::Base* r = new (storage) HarvestOrchard();
    OrchardDiag_NoteHarvestConstructed();
    return r;
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(HarvestOrchard, "AutoPickupWork")
