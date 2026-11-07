// src/game/Work/foresterwork.cpp
// Clean C++ reimplementation of ForesterWork::Slot4 — main work-assignment logic.
// Source: Game/Work/ForesterWork.cpp (from profile build debug strings)
//
// ForesterWork (0x60 bytes) — Forester resource gathering work action.
// Inherits: Object::Internal -> Work -> AutoPickupWork (0x48) -> ForesterWork
// Vtable: 0x1401613a0
//
// Slot4 (0x1400EAEC0) — 599 instructions, the largest single function across all Work classes.
// Finds nearby trees, checks terrain, assigns forester to cut/plant tasks.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/exe_bodies.h"
#include "Work/ForesterWork.h"
#include "Work/work_rules.h"   // review F-199..F-206: the shared Execute decisions
#include "options.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Tutorial/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "math/Math.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Player/exe_bodies.h"      // B3R: 0x14009f5d0
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include <cstring>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ForesterComponent.h"             // ENDGAME B.3.9 D5
#include "Component/ForesterDescription.h"           // ENDGAME B.3.9 D5
#include "Component/GrowthDescription.h"            // GrowthDescription::CheckFootprint (B8)
#include "Component/RadiusDescription.h"             // ENDGAME B.3.9 D5
#include "Component/NaturalResourceDescription.h"    // ENDGAME B.3.9 D5
#include "Component/NaturalResourceComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StatusIconComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ComponentDescription.h"
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
//   0x140132010  WorkImpl_Sqrtf_132010
//   0x14000c3c0  WorkLeaf_FindGrowthDescription_c3c0
//   0x140087fb0  Pf_EntityAnchorCell_7fb0
//   0x14009c480  Pf_ZoneRefresh_9c480
//   0x140008830  WorkBase_CalcDistance_8830
//   0x14009ccf0  WorkGrid_FindCell_9ccf0
//   0x1400a00f0  WorkBase_ValidateCell_a00f0
//   0x1400052c0  WorkBase_FindComponent_52c0
//   0x140006330  WorkAcc_FindComponent_6330
//   0x14009f5d0  PlayerPath_CharComp_f5d0
//   0x14009f3e0  Reimpl_MapGetTerrainPos
//   0x140120c30  WorkLeaf_FootprintOwnerCheck_120c30
//   0x1400be6e0  WorkAct_PlantWorkCtor_be6e0
//   0x1400c2490  WorkAct_PlayAnimationCtor_c2490
//   0x1400c1eb0  WorkAct_PerformWorkCtor_c1eb0
//   0x140021a30  WorkBST_RegisterWork_1a30
//   0x140115e60  CitizenAI_State_LongTermPlan
//   0x140006270  WorkBase_FindComponent_6270
//   0x14012cf10  StorageComponent::QueryCount
//   0x1400296b0  WorkBST_BST_Serialize_296b0
//   0x1400f7100  Work::ValidateTask
//   0x14001a6e0  WorkBST_BSTRemove_a6e0
//   0x140025520  WorkBST_BSTFindRemove_25520
//   0x1400c5400  WorkBase_TaskListCleanup_c5400
//   0x140002e10  AttendanceComponent_GetJobHostComponent
//   0x140014c30  WorkBase_TreeNodeFree_14c30
//   0x140172184  kRdata_140172184 (.rdata)
//   0x140172248  kRdata_140172248 (.rdata)
//   0x14017213c  kRdata_14017213c (.rdata)
//   0x140172114  kRdata_140172114 (.rdata)
//   0x14017214c  kRdata_14017214c (.rdata)
//   0x1401260f0  ActCNR_RemoveNatRes_1260f0 (the force-harvest harness only)
//
// Vtable pointers for dtor chain
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: ForesterWork_Ctor
// disassembles to `0x1400ebac9: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// The AutoPickupWork vtable it then overwrites with is 0x140160fa0 (the next LEA
// in the same ctor); only after that comes this class's own vtable.
// These were previously 0x1401612e0 / 0x140161340, neither of which is a vtable:
// both are INTERIOR addresses of the FireWork (0x1401612a0) and FishingWork
// (0x140161320) vtables, and Ghidra reports ZERO xrefs to either.

// A .rdata constant stored as a bit pattern, read as the float the cell held.
static inline float WorkRdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// Harness entry (no retail address): the force-driver gates on this. B3R: the helpers
// are called by name, so the only thing left to test is the game world (STORAGE).
bool ForesterWork_ExecuteReady() {
    return GameUpdateLoop::_singleton != nullptr;
}

// extern "C" wrapper so the force harness can drive ForesterWork::Execute directly (the Work
// driver ABI: self + the worker citizen's IBP) without needing the ForesterWork type/mangling.
extern "C" int ForesterWork_ForceDrive(Work* self, CitizenComponent** citizenIBP) {
    return static_cast<ForesterWork*>(self)->ForesterWork::Execute(citizenIBP, nullptr);
}


// ============================================================
// Helper: the T among a ComponentDescription's sub-descriptions (+0x28 data /
// +0x30 count) -- the inlined scan retail repeats (no CheckPtr), for
// RadiusDescription (DAT_1401c4870) and RawMaterialDescription (0x140006330).
// ============================================================
template <class T>
static T* FindSubComponentInList(ComponentDescription* subObj) {
    const uint32_t typeId = T::TypeId();
    int count = subObj->_subDescCount;
    for (int i = 0; i < count; i++) {
        Description* obj = subObj->_subDescs[i];
        if (obj->Type() == typeId) {
            Description* comp = subObj->_subDescs[i];
            if (Object::IsDerivedFromNullSafe(comp, typeId)) {
                return static_cast<T*>(comp);
            }
            return nullptr;
        }
    }
    return nullptr;
}


// ============================================================
// Helper: Assign work and register with AI
// Common tail code for both plant and cut paths.
// Sets citizen flags, stores priority, registers work with citizen AI.
// ============================================================
static void AssignWorkToEntity(ForesterWork* self, CitizenComponent** citizenIBP, int priority) {
    CitizenComponent* citizen = *citizenIBP;

    // Call vtable slot +0x50 (GetTarget) on self
    Profession* workEntity = nullptr;
    self->GetTarget(&workEntity);

    // Set citizen flags
    citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
    citizen->lastErrand = CitizenErrand::None;
    citizen->field_0x30 = priority;

    // If the work's profession matches the citizen's (+0x1f0) and priority >= 0,
    // set the "owned work" flag
    if (workEntity == citizen->profession && priority >= 0) {
        citizen->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
    }

    // Register work with citizen AI subsystem at self+0x48 (the tree helper takes the
    // container as uintptr_t*: LEFT)
    WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEntity's)
    WorkBST_RegisterWork_1a30(&self->_citizens, &inserted, citizenIBP);
}


// ============================================================
// ForesterWork::Slot4 — Main work-assignment logic
// ============================================================
//
// Algorithm:
// 1. Check if citizen already has auto-pickup assignments
// 2. Find WorkComponent, ForesterComponent / ForesterDescription, RadiusDescription
// 3. Check if material count has reached capacity (return 1 = done if so)
// 4. Calculate search radius from workplace radius
// 5. Random iteration over grid cells in radius
// 6. For each valid cell:
//    a. If empty cell and planting enabled: check terrain, create plant action
//    b. If occupied cell and cutting enabled: check resource, create cut action
// 7. If no work found after all attempts:
//    a. If enough failures, check storage and replan
//    b. Otherwise play idle animation and increment fail counter
//
int ForesterWork::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // Bring-up trace (first few ticks): confirms ForesterWork::Execute is actually being
    // ticked by the executor and reports the work-area entity + workplace cell it operates on.
    { static int s_tc = 0;
      if (s_tc < 6) { ++s_tc;
        fprintf(stderr, "[ForesterWork::Execute] tick#%d this=%p workArea+0x10=%p cell+0x28=%d\n",
                s_tc, (void*)this, field_10.Get(), field_28);
        fflush(stderr); } }

    // Save citizen ref for later use
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
    // Step 2: Find components on the workplace entity
    // -------------------------------------------------------

    // The WorkComponent of the workplace entity (self+0x10) (0x140003050)
    WorkComponent* matDesc = field_10->FindComponent<WorkComponent>();

    // Find ForesterComponent on workplace entity (DAT_1401c7740, ENDGAME B.3.9 D5:
    // by cell -- not the legacy variable name "WorkplaceComponent").
    ForesterComponent* workplaceComp = field_10->FindComponent<ForesterComponent>();

    // Get ForesterDescription from workplace description (+0x18) (DAT_1401c7778,
    // ENDGAME B.3.9 D5: by cell -- not the legacy variable name "RadiusComponent").
    Description* workplaceDesc = workplaceComp->_description;
    ForesterDescription* radiusComp = nullptr;
    if (Object::IsDerivedFromNullSafe(workplaceDesc, ForesterDescription::TypeId())) {
        radiusComp = static_cast<ForesterDescription*>(workplaceDesc);
    }

    // Find RadiusDescription on the workplace's ComponentDescription (+0x28) (DAT_1401c4870,
    // ENDGAME B.3.9 D5: by cell -- not the legacy variable name "MaterialComponent").
    RadiusDescription* materialComp = FindSubComponentInList<RadiusDescription>(field_10->_description);

    // -------------------------------------------------------
    // Step 3: Check capacity — is material count at limit?
    // (the town's per-resource counts, CitizenBehavior +0x170)
    // -------------------------------------------------------
    int materialIdx = (int)radiusComp->_type;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    CitizenBehavior* resourceTracker = gameWorld->m_citizenList;
    ProfessionResourceEntry* resourceTable = resourceTracker->professionResources._data;
    const ProfessionResourceEntry& resEntry = resourceTable[materialIdx];
    bool atCapacity = resEntry.maxCount <= resEntry.currentCount;

    // Set the status icon's flag 2 from the capacity (0x140002f90, 0x14012c3f0)
    StatusIconComponent* statusIcon = field_10->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);

    // Get tree count (the assigned count, self+0x50)
    int treeCount = _citizens._count;

    // If the worker capacity cache is 0, calculate it (the tree's port of 0x14012e5c0
    // called here takes the component as an integer: LEFT)
    if (matDesc->workState == 0) {
        int maxWorkers = matDesc->CalcMaxWorkers();
        matDesc->workState = maxWorkers;
    }

    // If treeCount exceeds max workers, we're done (return 1)
    if (treeCount > matDesc->workState) {
        return 1;
    }

    // -------------------------------------------------------
    // Step 4: Calculate search parameters
    // -------------------------------------------------------
    System::Random* rng = gameWorld->m_random;
    Map* map = gameWorld->m_game;
    // (0x140008830, 0x14009ccf0, 0x1400a00f0, 0x14009c480 take the Map.)

    int workplaceRadius = materialComp->_radius;
    int radiusSq = workplaceRadius * workplaceRadius;
    float searchArea = WorkImpl_Sqrtf_132010((float)radiusSq * (kRdata_140172184));
    int mapSize = map->size;
    int maxAttempts = (int)searchArea;

    // Workplace grid cell position
    int workplaceCell = field_28;
    int cellRow = workplaceCell / mapSize;
    int cellCol = workplaceCell % mapSize;

    // Get the GrowthDescription from the forester description's material ref
    GrowthDescription* natResDesc = nullptr;
    natResDesc = radiusComp->_ref20->FindDescription<GrowthDescription>();   // 0x14000c3c0

    // Get citizen's current position: CharacterComponent (+0x278) -> ModelComponent
    // (+0x68) -> the transform's translation (+0x158)
    const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

    int mapSz = map->size;
    (void)citizenPos.z;  // citizenYaw at +0x160, unused in this path
    int citizenCellX = (int)(citizenPos.x + (float)(mapSz >> 1));
    int citizenCellY = (int)(citizenPos.y + (float)(mapSz >> 1));

    // Clamp citizen cell to map bounds
    if (citizenCellX < 0) citizenCellX = 0;
    if (citizenCellX >= mapSz) citizenCellX = mapSz - 1;
    if (citizenCellY < 0) citizenCellY = 0;
    if (citizenCellY >= mapSz) citizenCellY = mapSz - 1;

    uint32_t citizenGridCell = mapSz * citizenCellY + citizenCellX;

    // Get entity's grid cell for distance calculations
    ComponentEntity* entityIBPBuf[2] = { field_10.Get(), nullptr };
    uint32_t entityGridCell = EntityAnchorCell(entityIBPBuf, nullptr);

    // -------------------------------------------------------
    // Step 5: Random search for tree spots
    // -------------------------------------------------------
    float fMaxHeight = WorkRdataF(kRdata_140172248);
    float fMinGrowthVal = WorkRdataF(kRdata_14017213c);
    // Note: XMM7 = g_fUVScale (DAT_14017214c = 1.0f) used for growth comparison.
    float fGrowthThreshold = kRdata_14017214c;   // DAT_14017214c

    int entityCellSigned = (int)entityGridCell;

    if (maxAttempts > 0) {
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            int randX, randY;
            int testCellX, testCellY;

            // Generate random position within radius (rejection sampling)
            do {
                do {
                    randX = rng->RandTol(workplaceRadius);
                    randY = rng->RandTol(workplaceRadius);
                    testCellX = cellCol + randX;
                    testCellY = cellRow + randY;
                } while (randY * randY + randX * randX > radiusSq);
            } while (WorkRules::SampleOffMap(testCellX, testCellY, mapSize));   // Y only (0x1400eb2be)

            // NOT RETAIL: retail reads the wrapped cell before cell 0 / past the last one (UB).
            if (WorkRules::SampleCellOutsideMap(testCellX, testCellY, mapSize)) {
                continue;
            }

            uint32_t testCell = (uint32_t)(map->size) * testCellY + testCellX;
            const MapCell& cellData = map->cells.GetData()[testCell];

            // Check terrain type bitmask — skip "forest-incompatible" terrain types
            // bit 5..8 (0x1e0 mask) indicates forest/grass terrain
            uint8_t terrainByte = static_cast<uint8_t>(cellData.terrainType);
            uint32_t terrainBit = 1u << (terrainByte & 0x1f);
            if ((terrainBit & Bits(PathBits::Zoneless)) != 0) {
                // Terrain already has resources (trees), skip
                goto next_attempt;
            }

            {
                // Map invalidation for connectivity
                // Retail 0x1400eb303: `MOV RCX,R15; CALL 0x14009c480` -- only RCX is set,
                // and the body has no other parameter.
                map->FillZones();   // 0x14009c480

                // Read fresh grid data after invalidation
                MapCell* gridData2 = map->cells.GetData();
                const MapCell& entityCellData = gridData2[entityCellSigned];
                const MapCell& testCellData2 = gridData2[(int)testCell];

                uint32_t entityTerrainBit = 1u << (static_cast<uint8_t>(entityCellData.terrainType) & 0x1f);
                uint32_t testTerrainBit = 1u << (static_cast<uint8_t>(testCellData2.terrainType) & 0x1f);
                uint32_t combinedBits = testTerrainBit | entityTerrainBit;
                uint32_t fullMask = combinedBits | Bits(PathBits::Walk);

                // Both cells must be passable
                if ((fullMask & testTerrainBit) == 0) goto next_attempt;
                if ((fullMask & entityTerrainBit) == 0) goto next_attempt;

                // Either both are "forest terrain" (0x1e0), or they share the same zone
                if ((combinedBits & Bits(PathBits::Zoneless)) == 0) {
                    int16_t zone1 = (int16_t)testCellData2.zone;
                    if (zone1 == 0) goto next_attempt;
                    int16_t zone2 = (int16_t)entityCellData.zone;
                    if (zone2 == 0) goto next_attempt;
                    if (zone1 != zone2) goto next_attempt;
                }

                // Cell must be unoccupied (no entity at cell)
                if (testCellData2.blocker != nullptr) goto next_attempt;

                // Check distance is within 2x workplace radius
                int dist = map->CalcDistance(entityGridCell, testCell);
                if (dist > workplaceRadius * 2) goto next_attempt;

                // Get resource entity at this cell
                ComponentEntity* resourceEntity = gridData2[(int)testCell].occupant.Get();

                // -------------------------------------------------------
                // Branch: Check workplace flags for cut vs plant
                // -------------------------------------------------------
                // PORT-BUG FIX: the original (0x1400eaec0) enters PLANT only when the
                // ForesterComponent's plant-enabled flag (+0x50) is NON-zero and the cell
                // is empty:  else-branch of `if (cmp[0x50]==0 || resource!=0)` ==
                // `(+0x50 != 0 && resource==0)`. The prior port had `+0x50 == 0`, which
                // (with the inert lodge's flag defaulting to 0) ALWAYS entered the plant
                // path -> CheckResourceDensity(natResDesc=null)+0x5c null-deref. CUT is
                // unchanged: `+0x51 != 0 && resource!=0`. After this fix the if/else
                // matches the original's nested branches exactly for all four cases.
                bool plantEnabled = (workplaceComp->plantTrees != 0);  // plant-trees enabled
                bool cutEnabled = (workplaceComp->cutTrees != 0);      // cut-trees enabled

                if (plantEnabled && resourceEntity == nullptr) {
                    // PLANT PATH: empty cell, check if we can plant here
                    bool canPath = static_cast<uint8_t>(map->ValidateCell(PathBits::Walk, testCell, citizenGridCell));   // the door read AL
                    if (canPath) {
                        // Check terrain suitability for planting
                        bool terrainOk = map->TestPlacement(   // 0x14009f5d0 (the Map* overload; the int* forwarder went)
                            testCellX, testCellY, 4,
                            fMinGrowthVal, fMaxHeight, 0.0f, 0, 0, nullptr);
                        if (terrainOk) {
                            // Check natural resource density limits (0x140120c30)
                            bool densityOk = static_cast<uint8_t>(natResDesc->CheckFootprint(testCellX, testCellY));   // the door read AL
                            if (densityOk) {
                                // Reset fail counter
                                _field58 = 0;

                                // Calculate world position for target cell
                                float halfOffset = (float)(map->size >> 1) - kRdata_140172114;
                                Math::Vector3 targetPos(
                                    (float)(testCell % map->size) - halfOffset,
                                    (float)(testCell / map->size) - halfOffset,
                                    0.0f);

                                // Get terrain height at position
                                float pathPos[3];
                                map->GetTerrainPos(pathPos, &targetPos.x, 0, nullptr);

                                // The forester description's material ref for the plant action
                                ComponentDescription* matDescRef = radiusComp->_ref20;

                                // Create plant action chain
                                CitizenComponent* plantBuf[2] = { *citizenIBP, nullptr };
                                ComponentEntity* resourceVal = nullptr;   // the parent reference: none
                                uint32_t resCountBuf[4] = { 0, 0, 0, 0 };
                                WorkAct_PlantWorkCtor_be6e0(
                                    plantBuf,                          // entityIBP
                                    field_20,                          // priority (workplaceId)
                                    matDescRef,                        // matDescRef
                                    0,                                 // param4
                                    pathPos,                           // pathPos
                                    testCell,                          // cellIdx
                                    0,                                 // param7
                                    &resourceVal,                      // resourcePtr
                                    resCountBuf,                       // resCountPtr
                                    0,                                 // param10
                                    0);                                // param11

                                // Assign work and register with AI
                                AssignWorkToEntity(this, citizenIBP, field_20);
                                return 2;
                            }
                        }
                    }
                }
                else if (cutEnabled && resourceEntity != nullptr) {
                    // CUT PATH: cell has a resource, check if we can cut it

                    // Find NaturalResourceComponent on the resource entity (0x1400052c0)
                    NaturalResourceComponent* natResComp = resourceEntity->FindComponent<NaturalResourceComponent>();

                    if (natResComp != nullptr) {
                        // Check capacity — can we still add more resources?
                        int matIdx2 = (int)radiusComp->_type;
                        const ProfessionResourceEntry& resEntry2 = resourceTable[matIdx2];
                        if (resEntry2.currentCount < resEntry2.maxCount) {
                            // Find the NaturalResourceComponent again, and its description
                            NaturalResourceComponent* natResComp2 = resourceEntity->FindComponent<NaturalResourceComponent>();

                            Description* natResDescObj = natResComp2->_description;
                            NaturalResourceDescription* natResDescFound = nullptr;
                            if (Object::IsDerivedFromNullSafe(natResDescObj, NaturalResourceDescription::TypeId())) {
                                natResDescFound = static_cast<NaturalResourceDescription*>(natResDescObj);
                            }

                            // Find the RawMaterialDescription (0x140006330)
                            RawMaterialDescription* rawMatPtr =
                                FindSubComponentInList<RawMaterialDescription>(natResDescFound->_building);

                            // Check resource flags:
                            // bit 5 of +0x5c = "being harvested"
                            // bit 13 of +0x5c = "reserved"
                            uint32_t resFlags = rawMatPtr->_flags;
                            if ((resFlags >> 5) & 1) goto next_attempt;
                            if ((resFlags >> 13) & 1) goto next_attempt;

                            // Check growth is sufficient (>= g_fUVScale threshold)
                            float growth = natResComp2->resourceScale;
                            if (growth < fGrowthThreshold) goto next_attempt;

                            // Check bit 20 of the component flags (+0x20) = "flagged for removal"
                            uint32_t flags20 = natResComp2->_flags;
                            if ((flags20 >> 20) & 1) goto next_attempt;

                            // Find nearest passable cell for pathfinding
                            int nearestCell = map->FindCell(testCell, citizenGridCell, PathBits::Access);
                            if (nearestCell < 0) goto next_attempt;

                            // Check path to the nearest passable cell
                            bool canPath2 = static_cast<uint8_t>(map->ValidateCell(PathBits::Walk, nearestCell, citizenGridCell));   // the door read AL
                            if (!canPath2) goto next_attempt;

                            // Reset fail counter
                            _field58 = 0;

                            // Calculate world position for nearest cell
                            float halfOffset2 = (float)(map->size >> 1) - kRdata_140172114;
                            Math::Vector3 cutPos(
                                (float)(nearestCell % map->size) - halfOffset2,
                                (float)(nearestCell / map->size) - halfOffset2,
                                0.0f);

                            // Get terrain height
                            float cutPathPos[3];
                            map->GetTerrainPos(cutPathPos, &cutPos.x, 0, nullptr);

                            // ── PHASE 5 PRODUCE VERIFICATION (env BANISHED_FORCE_HARVEST, default OFF) ──
                            // The organic delivery of a citizen to the tree is gated on the pathfinder
                            // MoveTo/arrival keystone (project_citizen_pathfinding_zero_dest) — citizens
                            // currently drift via walk-anim root-motion and never arrive. To verify the
                            // Phase-5 PRODUCTION chain (RemoveNaturalResource::Main → FUN_1401260f0 →
                            // RegisterRawMaterial) end-to-end independent of that movement blocker, this
                            // block invokes the real produce helper directly on the tree the real forester
                            // scan just found + validated. It mirrors RemoveNaturalResource::Main's flag
                            // setup (natResComp+0x2d harvestable, +0x2c mature) then calls 0x1401260f0.
                            if (Opt().forceHarvest) {
                                static int s_harvestCount = 0;
                                if (s_harvestCount < 6) {
                                    ++s_harvestCount;
                                    NaturalResourceComponent* nrc = natResComp2;
                                    nrc->harvestable = true;   // +0x2d
                                    nrc->mature = true;        // +0x2c
                                    CitizenComponent* czEnt = *citizenIBP;
                                    ComponentEntity* residence = czEnt ? czEnt->workplace.Get() : nullptr;
                                    ComponentEntity* workIBPbuf[2] = { residence, nullptr };
                                    fprintf(stderr, "[FORCE-HARVEST] #%d invoking produce on tree natResComp=%p "
                                            "(mirrors RemoveNaturalResource::Main; bypasses MoveTo arrival)\n",
                                            s_harvestCount, (void*)nrc);
                                    fflush(stderr);
                                    // 0x1401260f0 (was its ActCNR_RemoveNatRes_1260f0 forwarder)
                                    nrc->RemoveResource(workIBPbuf, 1);
                                    // With BANISHED_PRODUCE_SPAWN_ENTITY the force-produce just
                                    // REMOVED the tree (dirty-list -> freed next EntityList tick).
                                    // Do NOT also queue the real cut sequence on it: its pending
                                    // AnimateNaturalResource/PerformWork children would execute
                                    // against the freed entity (retail order runs all tree-touching
                                    // actions BEFORE RemoveNaturalResource, so this ordering only
                                    // exists under the harness). Scan on for a different tree.
                                    goto next_attempt;
                                }
                            }

                            // Create PerformWork action (cut tree)
                            CitizenComponent* cutBuf[2] = { *citizenIBP, nullptr };
                            NaturalResourceComponent* cutTargetBuf[2] = { natResComp2, nullptr };
                            WorkAct_PerformWorkCtor_c1eb0(
                                cutBuf,                            // entityIBP
                                field_20,                          // priority (workplaceId)
                                cutTargetBuf,                      // targetIBP
                                cutPathPos,                        // position
                                0,                                 // flag
                                1);                                // param6

                            // Assign work and register with AI
                            AssignWorkToEntity(this, citizenIBP, field_20);
                            return 2;
                        }
                    }
                }
            }

        next_attempt:
            ;  // Continue to next random attempt
        }
    }

    // -------------------------------------------------------
    // Step 7: No work found — handle failure
    // -------------------------------------------------------
    int failCount = _field58;

    if (failCount > maxAttempts) {
        // Too many failures — check if citizen's storage has items, then replan
        StorageComponent* storage = (*citizenIBP)->_entity->FindComponent<StorageComponent>();

        int storageCount = storage->QueryCount();
        if (storageCount < 1) {
            return 0;
        }

        // Citizen has items in storage — replan long term
        (*citizenIBP)->LongTermPlan();
    }
    else {
        // Play idle animation and increment fail counter
        CitizenComponent* idleBuf[2] = { *citizenIBP, nullptr };
        WorkAct_PlayAnimationCtor_c2490(idleBuf, field_20, 0);

        // Assign work (idle animation) and register with AI
        int priority = field_20;
        CitizenComponent* citizenEnt2 = *citizenIBP;

        // Call vtable slot +0x50 (GetTarget)
        Profession* workEnt = nullptr;
        GetTarget(&workEnt);

        citizenEnt2->statusFlags |= Bits(CitizenStatus::TaskDone);
        citizenEnt2->field_0x30 = priority;
        citizenEnt2->lastErrand = CitizenErrand::None;

        if (workEnt == citizenEnt2->profession && priority >= 0) {
            citizenEnt2->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
        }

        // (the tree helper takes the container as uintptr_t*: LEFT)
        WorkCitizenSetNode* inserted;   // the out-node slot (retail reuses workEnt's)
        WorkBST_RegisterWork_1a30(&_citizens, &inserted, citizenIBP);

        // Increment fail counter
        _field58 = failCount + 1;
    }

    return 2;
}


// ============================================================
// ForesterWork::DtorBody (0x1400EBA00)
// Sets vtables down ForesterWork -> AutoPickupWork -> Work chain,
// frees BST nodes at +0x48, zeros count at +0x50, then calls
// Object::Internal::~Internal via tail-call.
// ============================================================
// ForesterWork dtor (retail slot 0 0x14002A370 inlines it). The stamps and the `~Internal` call are the compiler's chain.
ForesterWork::~ForesterWork() = default;   // the member's ~Set is retail's inlined Clear (0x1400eba00)



// ============================================================
// ForesterWork::Serialize (0x1400EB970)
// Work::Serialize + version block + BST serialization + int field
// ============================================================
void ForesterWork::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create first serializer (version block, version=0)
    {
        IO::Serializer serializer1(stream, 0);
    }

    // Create second serializer (version=0) for BST + int
    IO::Serializer serializer2(stream, 0);

    // Serialize BST at self+0x48 (the helper takes the container as uintptr_t*: LEFT)
    WorkBST_BST_Serialize_296b0(&_citizens, &serializer2);

    // Serialize int field at self+0x58
    serializer2.Serialize(_field58, nullptr);
}


// ============================================================
// ForesterWork::Slot6 (0x1400EAEA0) — NOT HOOKABLE
// 20-byte thunk: sub rsp,0x28; add rcx,0x48; call CitizenAI_impl_5520;
//                xor al,al; add rsp,0x28; ret
// Removes entity from BST at self+0x48, returns false.
// Standalone exe only. (The tree helper takes the container as uintptr_t*: LEFT.)
// ============================================================
int ForesterWork::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}


// ============================================================
// ForesterWork::Slot7 (0x1400EAE90) — 9-byte thunk, too small to hook.
// Thunk: add rcx,0x48; jmp CitizenAI_impl_5520
// Calls BSTFindRemove on the BST at self+0x48.
// Standalone exe only.
// ============================================================
int ForesterWork::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    WorkBST_BSTFindRemove_25520(&_citizens, param2);
    return 0;
}


// ============================================================
// ForesterWork::Slot8 / CancelAll (0x1400EB8A0)
// Loops through tree at +0x48 while count (+0x50) > 0.
// For each leftmost node, cancels the work item via engine call.
// ============================================================
int ForesterWork::Cleanup() {
    int count = _citizens._count;

    while (count > 0) {
        // Find leftmost node in BST
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
// ForesterWork::Slot9 / RemoveInvalid (0x1400EAC60) — 164 instructions
// Iterates BST at +0x48, validates each entry via FUN_1400f7100.
// Collects invalid ones in a dynamic array, removes them from tree.
// Returns true if all valid (removeCount == 0), false otherwise.
// ============================================================
int ForesterWork::Validate() {
    AssignmentNode* root = _citizens._root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root != nullptr) {
        // Find leftmost node (minimum)
        AssignmentNode* node = root;
        while (node->left != nullptr) {
            node = node->left;
        }

        // In-order traversal
        while (node != nullptr) {
            CitizenComponent* entityIBP = node->element;

            // Validate this entity
            char stillValid = ValidateTask(nullptr, &entityIBP, nullptr);

            if (stillValid == '\0') {
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
                        newArr = static_cast<CitizenComponent**>(System::MemoryAllocator::MemAlloc((int64_t)(int)newCap * 8, 8));
                    }

                    // Copy existing entries if buffer changed
                    int copyStart = (removeArr == newArr) ? (int)removeCount : 0;
                    for (int ci = copyStart; ci < (int)newCount; ci++) {
                        CitizenComponent** dst = &newArr[ci];
                        *dst = nullptr;
                        if (ci < (int)removeCount) {
                            *dst = removeArr[ci];
                        } else {
                            *dst = entityIBP;
                        }
                    }

                    if (removeArr != newArr) {
                        System::MemoryAllocator::MemFree(removeArr);
                    }
                    removeArr = newArr;
                    removeCount = newCount;
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
    if ((int)removeCount > 0) {
        for (uint32_t i = 0; i < removeCount; i++) {
            CitizenComponent* searchKey = removeArr[i];
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
    }

    System::MemoryAllocator::MemFree(removeArr);
    return (removeCount == 0) ? 1 : 0;
}


// ============================================================
// ForesterWork::Slot10 / GetWorkEntity (0x1400EB910)
// The entity's (+0x10) WorkPlaceComponent (FUN_140002e10), its profession (+0x48)
// through the output pointer.
// Hidden return ptr ABI: RCX=self, RDX=retbuf.
// ============================================================
Profession** ForesterWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = field_10->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}



// [11] 0x140029860 (shared by 13 tables): `XOR EAX,EAX; CMP [RCX+0x50],EAX; SETG AL`
// — "is anyone assigned". The old table pointed at Work_Slot11_HasActiveTasks.
int ForesterWork::GetPriority() {
    return _citizens._count > 0 ? 1 : 0;
}
// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The three stamps are the compiler's.
ForesterWork::ForesterWork() : AutoPickupWork(), _field58(0) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_ForesterWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over ForesterWork_Factory / _CreateInPlace (both
// Work_GenericCtor), plus three vtable-pointer statics for the dtor's stamps.

// create 0x14002a310: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* ForesterWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ForesterWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ForesterWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* ForesterWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ForesterWork());
}


#include "Object/factory_registration.h"
DEFINE_EXE_TYPE(ForesterWork, "AutoPickupWork")
