// src/game/Work/fishingwork.cpp
// Clean C++ reimplementation of FishingWork Work class methods.
// Source: Game/Work/FishingWork.cpp (from profile build debug strings)
//
// FishingWork — fishing dock work action. Size = 0x70.
// Inherits: Object::Internal -> Work (0x48) -> FishingWork
// Vtable: 0x140161320
//
// Implemented vtable slots:
//   +0x18: Serialize  (0x1400ea910) — array serialization with IsDerivedFrom
//   +0x20: Slot4      (0x1400e9f20) — fishing work assignment
//   +0x30: Slot6      (0x1400e9eb0) — clear citizen slot + mark spot done
//   +0x38: Slot7      (0x1400e9e80) — delegates to Slot6 via vtable +0x30
//   +0x40: Slot8      (0x1400ea830) — cancel all: clear citizens + cancel actions
//   +0x48: Slot9      (0x1400e9c30) — validate work assignments
//   +0x50: Slot10     (0x1400ea8b0) — get target entity position IBP
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/FishingWork.h"
#include "Work/work_rules.h"   // review F-199..F-206: the shared Execute decisions
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R (Pf_EntityResolvePos_2e20 went in B6: InteractNodePosition is called typed)
#include "Component/exe_bodies.h"   // B3R: CitizenComp_FUN_14012c3f0, AttendanceComponent_GetJobHostComponent
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "redzone_alloc.h"   // RedzoneAlloc_PrintBacktrace (wild-write diagnostic)
#include "Object/factory_registration.h"   // GetTypeIdByName (drop-spot occupancy dump)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/FishingComponent.h"     // ENDGAME B.3.9 D5: FishingComponent::TypeId()
#include "Component/FishingDescription.h"   // ENDGAME B.3.9 D5: FishingDescription::TypeId()
#include "Component/RadiusDescription.h"    // ENDGAME B.3.9 D5: RadiusDescription::TypeId()
#include "Component/StorageComponent.h"     // ENDGAME B.3.9 D5: StorageComponent::TypeId()
#include "Component/WorkComponent.h"        // ENDGAME B.3.9 D5: WorkComponent::TypeId()
#include "Component/CitizenComponent.h"     // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/InteractComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "Action/CompleteTask.h"

// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component exe_bodies.h)
// ============================================================
//   0x140002f90 Entity_FindComponent_MeshInstance   WorkBase_FindComponent_2f90
//   0x140002ed0 Entity_FindComponent_Position       WorkBase_FindComponent_2ed0
//   0x140122e20 Entity_impl_2e20 spot position      Pf_EntityResolvePos_2e20
//   0x14012c3f0 MeshInstance visibility toggle      CitizenComp_FUN_14012c3f0
//   0x1400beae0 Fishing action ctor                 WorkAct_FishWorkCtor_beae0
//   0x1400c2490 PlayAnimation_Action_Ctor           WorkAct_PlayAnimationCtor_c2490
//   0x1400f8620 TryAssignToFishingSpot              WorkGrid_TryAssignWorkplace_f8620
//   0x140002e10 find PositionComponent (GetTarget)  AttendanceComponent_GetJobHostComponent
//   0x1400c5400 cancel pending actions              WorkBase_TaskListCleanup_c5400
//   0x1400a00f0 Map_CheckPassable                   WorkBase_ValidateCell_a00f0
//   0x1400f7100 Work::CanStartWork                  WorkBase_ValidateTask_f7100
//   0x140115e60 CitizenAI_State_LongTermPlan        CitizenAI_State_LongTermPlan
// 0x1400032e0 (Array<InternalPtr>::SetSize) and 0x1400021d0 (Array<int32>::SetSize) are
// the citizenRefs / spotStates members' SetSize.

// ============================================================
// Helper: Find a description on a ComponentDescription's sub-description list
// (+0x28 array / +0x30 count) — different from an entity's component list.
// ============================================================
static Description* FindComponentOnSubObject(ComponentDescription* subObj, uint32_t typeId) {
    int count = subObj->_subDescCount;

    for (int i = 0; i < count; i++) {
        Description* comp = subObj->_subDescs[i];
        uint32_t compTypeId = comp->Type();
        if (compTypeId == typeId) {
            Description* c = subObj->_subDescs[i];
            if (Object::IsDerivedFromNullSafe(c, typeId)) {
                return c;
            }
            return nullptr;
        }
    }
    return nullptr;
}


// ============================================================
// Helper: Clamp coordinate to map bounds
// ============================================================
static inline int ClampToMap(int val, int mapSize) {
    if (val < 0) return 0;
    if (val >= mapSize) return mapSize - 1;
    return val;
}

// ============================================================
// Slot4/Execute (0x1400e9f20)
// Main fishing work assignment logic. Called per citizen to assign
// them to a fishing spot at the fishing dock.
//
// Returns: 1 = not ready/retry, 2 = action pushed
// ============================================================
int FishingWork::Execute(CitizenComponent** citizenIBP, System::Time* /*time*/) {
    // Get game world
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // Find FishingComponent (DAT_1401c7660) on the building entity (+0x10)
    FishingComponent* fishing = _entity->FindComponent<FishingComponent>();

    // Get fishing description via IsDerivedFrom check on component+0x18
    Description* compBase = fishing->_description;
    FishingDescription* fishingDesc = nullptr;
    if (Object::IsDerivedFromNullSafe(compBase, FishingDescription::TypeId())) {
        fishingDesc = static_cast<FishingDescription*>(compBase);
    }

    // Check if fishing season is valid
    // The town's resource entry for fishingDesc+0x28 (CitizenBehavior +0x170, 0x20 stride)
    int spotMapIdx = (int)fishingDesc->_resourceLimit;
    ProfessionResourceEntry* mapEntries = gameWorld->m_citizenList->professionResources._data;
    ProfessionResourceEntry& entry = mapEntries[spotMapIdx];
    // atCapacity = (limit <= current) — true when resource count is at/above limit
    bool atCapacity = entry.maxCount <= entry.currentCount;   // +0x08 / +0x00

    // Toggle status icon visibility based on capacity (visGroup 2)
    StatusIconComponent* statusIcon = _entity->FindComponent<StatusIconComponent>();
    statusIcon->SetFlag(2, atCapacity ? 1 : 0);   // 0x14012c3f0

    if (atCapacity) {
        return 1;
    }

    // Find InteractComponent (the spot positions) on building entity
    InteractComponent* posComp = _entity->FindComponent<InteractComponent>();

    int citizenCount = citizenRefs._size;  // +0x50

    if (citizenCount < 1) {
        return 1;
    }

    // Find first empty citizen slot
    int emptySlot = -1;
    for (int i = 0; i < citizenCount; i++) {
        if (citizenRefs[i] == nullptr) {
            emptySlot = i;
            break;
        }
    }

    if (emptySlot == -1) {
        return 1;
    }

    // Find first available fishing spot (WorkSpotState::Available)
    // Also reset spots that were marked as state 2 (NeedsReset) if the map cell is now empty
    int spotCount = spotStates._size;    // +0x60
    int32_t* spotStates = this->spotStates._data;  // +0x58
    // The world map
    Map* map = gameWorld->m_game;
    int mapSize = map->size;
    int freeSpot = -1;
    int freeSpotMapCell = -1;

    if (spotCount > 0) {
        for (int i = 0; i < spotCount; i++) {
            // Get spot position (mode=2)
            Vector3Slot spotPos;
            posComp->NodePosition(&spotPos.v, 2, i, nullptr);   // 0x140122e20

            int half = mapSize >> 1;
            int cx = ClampToMap((int)(spotPos.v.x + (float)half), mapSize);
            int cy = ClampToMap((int)(spotPos.v.y + (float)half), mapSize);
            int cellIdx = mapSize * cy + cx;

            // If the spot is NeedsReset and its map cell is empty, return it to Available
            int* spotState = &spotStates[i];
            if (static_cast<WorkSpotState>(*spotState) == WorkSpotState::NeedsReset) {
                MapCell* mapEntryBase = map->cells.GetData();
                const ComponentEntity* occupant = mapEntryBase[cellIdx].occupant.Get();   // +0x08
                if (occupant == nullptr) {
                    *spotState = static_cast<int32_t>(WorkSpotState::Available);
                }
            }

            // Pick first free spot
            if (freeSpot < 0 && static_cast<WorkSpotState>(*spotState) == WorkSpotState::Available) {
                freeSpot = i;
                freeSpotMapCell = cellIdx;
            }
        }



        if (freeSpot >= 0) {
            // Get citizen position (mode=0) for pathfinding
            Vector3Slot citizenPos;
            float citizenHeading;
            posComp->NodePosition(&citizenPos.v, 0, emptySlot, &citizenHeading);   // 0x140122e20

            // The citizen's position (CharacterComponent -> ModelComponent -> the
            // transform translation, +0x158)
            const Math::Vector3& citizenPosData =
                (*citizenIBP)->character->modelComponent->transform.translation;

            int cMapSize = map->size;
            float cPosX = citizenPosData.x;
            float cPosY = citizenPosData.y;
            int cHalf = cMapSize >> 1;
            int ccx = ClampToMap((int)(cPosX + (float)cHalf), cMapSize);
            int ccy = ClampToMap((int)(cPosY + (float)cHalf), cMapSize);
            int citizenCell = cMapSize * ccy + ccx;

            // Get spot cell
            Vector3Slot spotPos;
            posComp->NodePosition(&spotPos.v, 0, emptySlot, &citizenHeading);   // 0x140122e20

            int spHalf = cMapSize >> 1;
            int scx = ClampToMap((int)(spotPos.v.x + (float)spHalf), cMapSize);
            int scy = ClampToMap((int)(spotPos.v.y + (float)spHalf), cMapSize);
            int spotCell = cMapSize * scy + scx;

            // Check passability. This is NOT an A*: 0x1400a00f0 is the region-equivalence
            // test (both cells' region word at cell+0x26 non-zero and EQUAL). A dock on
            // the far bank of a river is therefore unreachable no matter how good its
            // fishable-tile score, and this branch used to return silently — a working
            // dock and a stranded one logged exactly the same nothing. Count it and print
            // the two region ids, which name the cause outright.
            uint64_t passResult = map->ValidateCell(PathBits::Walk, (uint32_t)citizenCell, spotCell);
            if ((char)passResult == '\0') {
                return 1;
            }

            // Find the RadiusDescription on the building's description (+0x28)
            // DAT_1401c4870 = RadiusDescription (ENDGAME B.3.9 D5: by cell, not by the
            // legacy variable name "RadiusComponent").
            RadiusDescription* radiusComp = static_cast<RadiusDescription*>(
                FindComponentOnSubObject(_entity->_description, RadiusDescription::TypeId()));

            int radius = radiusComp->_radius;
            int radiusSq = radius * radius;

            // The work's target cell (+0x28) as grid coordinates for the random offset
            int citizenGridX = _cell / cMapSize;
            int citizenGridY = _cell % cMapSize;

            // Get random source from game world
            System::Random* randPtr = gameWorld->m_random;

            // Try up to 10 random fishing positions within radius.
            // Original uses nested do-while to guarantee each attempt has a
            // valid in-radius, in-bounds position before checking terrain.
            int attemptCount = 0;
            do {
                int rx, ry, targetX, targetY;
                // Inner do-while: generate valid position within radius + bounds
                do {
                    do {
                        rx = randPtr->RandTol(radius);
                        ry = randPtr->RandTol(radius);
                        targetX = citizenGridY + rx;
                        targetY = citizenGridX + ry;
                    } while (radiusSq < ry * ry + rx * rx);
                } while (WorkRules::SampleOffMap(targetX, targetY, cMapSize));   // Y only (0x1400ea39a)

                // NOT RETAIL: an X off the map reads the wrapped-row cell, as retail does; only
                // the cell before cell 0 / past the last one, retail's out-of-bounds read (UB),
                // is rejected as a failed attempt.
                if (WorkRules::SampleCellOutsideMap(targetX, targetY, cMapSize)) {
                    attemptCount++;
                    continue;
                }


                // Check map cell for valid water terrain
                int64_t mapCellIdx = (int64_t)(cMapSize * targetY + targetX);
                MapCell* mapData = map->cells.GetData();
                uint8_t terrainType = mapData[mapCellIdx].influenceCounts[8];   // +0x24 (influenceCounts[8])
                uint8_t terrainBit = static_cast<uint8_t>(mapData[mapCellIdx].terrainType);   // +0x29

                // Check fishing description allows this terrain and terrain is nonzero
                uint32_t fishingTerrainMask = fishingDesc->_terrainMask;   // COMP-c named FishingDescription +0x2c
                if (((fishingTerrainMask >> (terrainBit & 0x1f)) & 1) == 0) {
                    attemptCount++;
                    continue;
                }
                if (terrainType == 0) {
                    attemptCount++;
                    continue;
                }

                // Valid spot found — create fishing action

                // Build action parameters
                ComponentEntity* entityRef = _entity.Get();
                CitizenComponent* citizenRef = *citizenIBP;
                float uvScale = (kRdata_14017214c / (float)terrainType) * fishing->fishingRate;
                uint32_t spotInfo = ((freeSpot | 0x800) << 4) | emptySlot;

                WorkAct_FishWorkCtor_beae0(
                    &citizenRef, _workId, &entityRef,
                    &citizenPos.v.x, citizenHeading,
                    fishingDesc->_fish,
                    uvScale, freeSpotMapCell, spotInfo);

                // Mark spot as InUse
                spotStates[freeSpot] = static_cast<int32_t>(WorkSpotState::InUse);

                // Finalize: push action, set citizen slot
                int priority = _workId;  // +0x20
                CitizenComponent* ce = *citizenIBP;

                Profession* targetBuf;   // retail: slot 10 writes into the reused entityRef stack slot
                Profession* actionVal = *GetTarget(&targetBuf);

                ce->statusFlags |= Bits(CitizenStatus::TaskDone);
                ce->lastWorkId = priority;
                ce->lastErrand = CitizenErrand::None;

                if (actionVal == ce->profession && priority >= 0) {
                    ce->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                }

                // Store citizen ref in slot
                citizenRefs[emptySlot] = *citizenIBP;
                // Reset retry counter
                retryCount = 0;
                return 2;
            } while (attemptCount < 10);

            // All 10 random attempts failed — retry with animation if under limit
            if (retryCount < 0xb) {  // retry count < 11
                CitizenComponent* citizenRef2 = *citizenIBP;
                WorkAct_PlayAnimationCtor_c2490(&citizenRef2, _workId, emptySlot);

                int priority = _workId;
                CitizenComponent* ce = *citizenIBP;

                Profession* targetBuf;   // retail: slot 10 writes into the reused citizenRef2 stack slot
                Profession* actionVal = *GetTarget(&targetBuf);

                ce->statusFlags |= Bits(CitizenStatus::TaskDone);
                ce->lastWorkId = priority;
                ce->lastErrand = CitizenErrand::None;

                if (actionVal == ce->profession && priority >= 0) {
                    ce->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
                }

                citizenRefs[emptySlot] = *citizenIBP;
                // Increment retry counter
                retryCount = retryCount + 1;
                return 2;
            }

            // Retries exhausted — check if the citizen's storage has items, go to long-term plan
            StorageComponent* storage = (*citizenIBP)->_entity->FindComponent<StorageComponent>();

            int storageCount = storage->QueryCount();   // 0x14012cf10
            if (storageCount < 1) {
                return 1;
            }

            // Has storage items — replan citizen
            (*citizenIBP)->LongTermPlan();   // 0x140115e60
            return 2;
        }
    }

    // No free spot found — try workplace assignment fallback
    // Iterate spots and check if any has an entity that can be assigned
    if (spotCount > 0) {
        for (int i = 0; i < spotCount; i++) {
            Vector3Slot spotPos;
            posComp->NodePosition(&spotPos.v, 2, i, nullptr);   // 0x140122e20

            int half = map->size >> 1;
            int cx = ClampToMap((int)(spotPos.v.x + (float)half), map->size);
            int cy = ClampToMap((int)(spotPos.v.y + (float)half), map->size);
            int cellIdx = map->size * cy + cx;

            // Check if there's a structure at this cell (the cell's occupant, +0x08)
            MapCell* mapData = map->cells.GetData();
            ComponentEntity* cellEntity = mapData[cellIdx].occupant.Get();
            if (cellEntity == nullptr) continue;

            // ⚠ KNOWN DIVERGENCE FROM RETAIL — self-assignment guard.
            //
            // This fallback asks "is there some OTHER workplace standing on my attach
            // cells that this citizen could be assigned to instead?", looks up that
            // workplace's WorkComponent id and hands it to FUN_1400f8620, which dispatches
            // that work's Execute (vtable +0x20). In retail cell+8 on a dock's spot cells
            // holds a NATURAL RESOURCE (it is the resource slot — HerbalistWork reads the
            // same field to find herbs), so the work found is never this dock's own.
            //
            // In our standalone, cell+8 at the dock's own footprint holds the DOCK ENTITY,
            // so the lookup returns THIS FishingWork and f8620 re-enters this very
            // function with the same arguments — unbounded recursion and a STACK_OVERFLOW
            // (measured: FishingWork_Slot4 -> f8620 -> FishingWork_Slot4 ... same self,
            // same cellId=2). f8620's own re-entrancy guard cannot help: it stamps
            // work+0x2c only AFTER Execute returns, so the stamp is never reached.
            //
            // Skipping our own entity is therefore restoring retail's behaviour, not
            // papering over it — retail simply never encounters itself here.
            //
            // UPSTREAM CAUSE FOUND 2026-08-15 and fixed: the dock's own footprint claim
            // (dc20) was taking cell+8 on its '_' walkway cells, which are exactly where
            // its four mode-2 drop spots sit. BANISHED_LIVE_WALKWAY_DROPCELLS (default on)
            // leaves those cells unowned, so this branch should now be unreachable. It
            // stays as a tripwire: if it ever fires again the claim is back, and the
            // dock's 4-catch cap is back with it.
            if (cellEntity == _entity.Get()) {
                static bool s_warned = false;
                if (!s_warned) {
                    s_warned = true;
                    std::fprintf(stderr, "[FishWork] WARNING: the dock itself is at cell+8 of "
                                 "its own drop cell %d — the footprint claim is back "
                                 "(BANISHED_LIVE_WALKWAY_DROPCELLS off, or a new writer). "
                                 "The dock will cap at spotCount catches.\n", cellIdx);
                    std::fflush(stderr);
                }
                continue;
            }

            // Check for WorkComponent (DAT_1401c8070) on cell entity
            ComponentEntity* cellEnt = cellEntity;
            int compCount = cellEnt->_components._size;
            WorkComponent* workplaceComp = cellEnt->FindComponent<WorkComponent>();

            // ── GUARD FOR ONE OF OUR OWN DEVIATIONS (READ of 0x28) ───────────────────
            // The `workplaceComp->workId` read below is FAITHFUL. Verified against
            // retail 0x1400ea775..0x1400ea793: the not-found arm does
            // `xor edx,edx; Ctor(&ibp, edx); CheckPtr; mov rax,[ibp]; mov r8d,[rax+0x28]`
            // and the found arm `cmovne`s the component into the same rdx before joining
            // at 0x1400ea777, so BOTH arms reach the same unguarded deref. Retail is
            // relying on an invariant, and we know exactly which one — and exactly which
            // of our deviations breaks it. Measured end to end, seed 999:
            //
            //  1. Retail's footprint claim (dc20) writes the owner entity into cell+8 for
            //     '#' AND '_' cells alike — `CMP CL,0x2e ; SETNZ BL`, only '.' is free.
            //     BANISHED_LIVE_WALKWAY_DROPCELLS (default ON, keystone_a_map.cpp:411)
            //     deliberately treats '_' as '.' so the dock's drop cells read cell+8 == 0.
            //     That flag is what lifted the dock's 4-catch cap.
            //  2. SpawnComponent's DoSpawn gate TileFree (0x140120c30) tests cell+8 —
            //     `mov 0x8(%rax,%rcx,8),%rax ; test ; je reject`. An unclaimed drop cell
            //     therefore reads as free ground. The other gate, CanPlace (0x14009f5d0),
            //     rejects on the ZONE raster cell+0, which dc20 never writes for a
            //     building, so nothing else stops it.
            //  3. So a TREE spawns on the dock's pier after the dock is built. Confirmed
            //     by two measurements in one run: the footprint held ZERO standing
            //     natural resources at placement (startTown::residue silent), and a tree
            //     stood on drop cell (88,124) later.
            //  4. A tree carries MapComponent/ModelComponent/NaturalResourceComponent/
            //     GrowthComponent/SpawnComponent and NO WorkComponent, so the lookup
            //     below returns null and retail's deref faults.
            //
            // In retail step 1 never happens, cell+8 holds the DOCK, and the dock HAS a
            // WorkComponent — so retail's unguarded deref is safe by construction. This
            // guard therefore compensates for OUR deviation, which is the one case the
            // project's "don't fix a faithful crash with a null check" rule allows. It
            // MUST be removed when the '_' claim is restored, which is blocked on finding
            // retail's step that frees a claimed drop cell for a drop (still unfound).
            //
            // The dump names the occupant so a NEW upstream writer can't hide behind this.
            if (!workplaceComp) {
                static int s_n = 0;
                if (++s_n <= 8) {
                    const uint32_t want = WorkComponent::TypeId();
                    const char* wantName = ::Object::GetTypeNameById(want);
                    std::fprintf(stderr,
                        "[FishWork] +0x28 NULL: spot[%d] cell=%d tile=(%d,%d) "
                        "cellEntity=%p dock=%p wanted tid=0x%x (%s) — entity carries "
                        "%d component(s):\n", i, cellIdx, cx, cy, static_cast<void*>(cellEnt),
                        static_cast<void*>(_entity.Get()), want,
                        wantName ? wantName : "UNREGISTERED", compCount);
                    for (int k = 0; k < compCount; ++k) {
                        Component* c2 = cellEnt->_components[k].Get();
                        if (!c2) { std::fprintf(stderr, "[FishWork]     [%d] <null>\n", k); continue; }
                        uint32_t t2 = c2->Type();
                        const char* n2 = ::Object::GetTypeNameById(t2);
                        std::fprintf(stderr, "[FishWork]     [%d] tid=0x%x %s\n",
                                     k, t2, n2 ? n2 : "(unregistered)");
                    }
                    std::fflush(stderr);
                }
                continue;   // skip this spot — see the block above; remove with the '_' claim
            }

            // Try assignment via FUN_1400f8620 (GameUpdateLoop +0x118, the WorkScheduler)
            WorkScheduler* assignMgr = gameWorld->m_tutorial;
            CitizenComponent* citizenRef = *citizenIBP;
            int spotRadius = workplaceComp->workId;
            uint64_t assignResult = assignMgr->TryAssignWorkplace(&citizenRef, spotRadius, '\x01');

            if ((char)assignResult != '\0') {
                return 2;
            }
        }
    }

    return 1;
}

// ============================================================
// Slot9/Validate (0x1400e9c30)
// Validates all fishing work assignments. Iterates citizen slots,
// checks each via Work::CanStartWork. Invalid citizens are cleared,
// orphaned spots are marked for reset (state 1 InUse -> 2 NeedsReset).
//
// Returns: 1 = all valid (or no citizens), 0 = some invalid
// ============================================================
int FishingWork::Validate() {
    int citizenCount = citizenRefs._size;  // +0x50
    if (citizenCount <= 0) {
        return 1;
    }

    int* spotMapping = nullptr;     // dynamic array: citizen index -> spot index from task
    int mappingCount = 0;
    int mappingCapacity = 0;
    bool allValid = true;
    int iterIndex = 0;

    for (int i = 0; i < citizenCount; i++) {
        CitizenComponent* citizenRef = citizenRefs[i].Get();
        if (citizenRef == nullptr) continue;

        // Validate this citizen's task
        CitizenComponent* citizenIBP = citizenRef;
        CompleteTask* outTask = nullptr;
        char valid = (char)ValidateTask(nullptr, &citizenIBP, &outTask);   // 0x1400f7100

        if (valid == '\0') {
            // Invalid — clear citizen slot (the InternalBasePtr ctor with null)
            citizenRefs[i] = nullptr;
            allValid = false;
        } else {
            // Valid — record the spot index from task data
            int taskSpotInfo = outTask->taskParam;   // +0x34

            int newCount = mappingCount + 1;
            if (newCount != mappingCount) {
                // Grow array if needed
                if (mappingCapacity < newCount) {
                    int newCap = newCount;
                    if (mappingCount != 0) {
                        newCap = newCount * 3;
                    }
                    int* newArr = (int*)System::MemoryAllocator::MemAlloc((uint64_t)newCap * 4, 4);

                    // Copy existing data
                    int copyStart = (spotMapping == newArr) ? mappingCount : 0;
                    for (int j = copyStart; j < newCount; j++) {
                        if (newArr) {
                            if (j < mappingCount && spotMapping) {
                                newArr[j] = spotMapping[j];
                            } else {
                                // New entry: extract spot index from taskSpotInfo
                                newArr[j] = (taskSpotInfo >> 4) & 0xf;
                            }
                        }
                    }

                    int* oldArr = spotMapping;
                    spotMapping = newArr;
                    mappingCapacity = newCap;

                    if (oldArr != newArr && oldArr != nullptr) {
                        System::MemoryAllocator::MemFree(oldArr);
                    }
                } else {
                    // Room in existing array
                    for (int j = mappingCount; j < newCount; j++) {
                        if (spotMapping) {
                            spotMapping[j] = (taskSpotInfo >> 4) & 0xf;
                        }
                    }
                }
                mappingCount = newCount;
            }
        }
        iterIndex++;
    }

    // If any citizen was invalid, mark orphaned spots as needing reset
    if (!allValid) {
        int spotCount = spotStates._size;  // +0x60

        for (int s = 0; s < spotCount; s++) {
            // Check if any valid citizen maps to this spot
            bool spotOccupied = false;
            for (int m = 0; m < mappingCount; m++) {
                if (spotMapping[m] == s) {
                    spotOccupied = true;
                    break;
                }
            }

            if (!spotOccupied) {
                // If spot was InUse, mark it NeedsReset
                int* state = &spotStates[s];
                if (static_cast<WorkSpotState>(*state) == WorkSpotState::InUse) {
                    *state = static_cast<int32_t>(WorkSpotState::NeedsReset);
                }
            }
        }
    }

    System::MemoryAllocator::MemFree(spotMapping);
    return allValid ? 1 : 0;
}

// ============================================================
// Serialize (0x1400ea910)
// Serializes FishingWork fields:
//   1. Work base serialize
//   2. IO::Serializer(version=0)
//   3. Serialize citizenCount (+0x50), resize IBP array if loading
//   4. Loop: IBP::Serialize each citizen ref, validate with IsDerivedFrom
//   5. Serialize spotCount (+0x60), resize int array if loading
//   6. Serialize spotStates int array (+0x58)
//   7. Serialize retryCount (+0x68)
// ============================================================
void FishingWork::Persist(IO::Stream& stream) {
    // 1. Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // 2. Create IO::Serializer (version=0)
    IO::Serializer serializer(stream, 0);

    // 3. Serialize citizenCount at +0x50
    int citizenCount = citizenRefs._size;
    serializer.Serialize(citizenCount, nullptr);

    // If loading (serializer mode == 0 at *(int*)(serializer[0] + 8)):
    // resize the IBP array at +0x48 to the deserialized count
    // 0x1400032e0 == Collection::Array<InternalPtr>::SetSize (kSpec_PtrZero).
    if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        citizenRefs.SetSize(citizenCount);
    }

    // 4. Loop over each citizen IBP, serialize and validate
    Object::InternalPtr<CitizenComponent>* ibpPtr = citizenRefs._data;
    int64_t remaining = (int64_t)citizenCount;
    if (remaining > 0) {
        do {
            // Serialize the InternalBasePtr
            ibpPtr->InternalBasePtr::Serialize(serializer);

            // Validate: if the ptr is non-null and not derived from expected type, null it
            // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
            // downcast to CitizenComponent*; the answer is the same.
            if (!ibpPtr->IsNull()) {
                if (!Object::IsDerivedFromNullSafe(*ibpPtr, CitizenComponent::TypeId())) {
                    *ibpPtr = nullptr;
                }
            }

            ibpPtr++;
            remaining--;
        } while (remaining != 0);
    }

    // 5. Serialize spotCount at +0x60
    int spotCount = spotStates._size;
    serializer.Serialize(spotCount, nullptr);

    // If loading, resize the int array at +0x58
    // 0x1400021d0 == Collection::Array<int32>::SetSize (retail FishingWork_Shutdown
    // calls it at 1400ea9ef): a grown tail is left as the allocation found it, like retail. Safe: the resize runs only when loading and
    // the bulk int read below writes all spotCount spot states from the stream.
    if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        spotStates.SetSize(spotCount);
    }

    // 6. Serialize the spotStates int array
    serializer.Serialize(spotCount, spotStates._data, nullptr);

    // 7. Serialize retryCount at +0x68
    serializer.Serialize(retryCount, nullptr);

    // The serializer's destructor runs here
}

// ============================================================
// Slot6 (0x1400e9eb0)
// Called when a citizen completes fishing work at a spot.
// Clears the citizen IBP slot and marks the spot state as 2 (NeedsReset, done)
// if the high bit flag is set in param_4.
//
// param_4 encoding: bits[3:0] = citizen slot index,
//                   bit 15 = has spot info,
//                   bits[7:4] = spot index
// Returns: 0 (via cleared upper bytes)
// ============================================================
int FishingWork::OnComplete(CitizenComponent** param2, ComponentEntity** param3, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    (void)param2;
    (void)param3;

    // Clear citizen IBP slot: citizenRefs[param4 & 0xf] = nullptr IBP
    uint32_t citizenSlot = param4 & 0xf;
    citizenRefs[citizenSlot] = nullptr;

    // If bit 15 is set, mark the spot state as 2 (NeedsReset)
    if ((param4 >> 15) & 1) {
        uint32_t spotIdx = (param4 >> 4) & 0xf;
        // TEARDOWN_WILD_WRITE.md: same unbounded retail store (0x1400e9eb0) as
        // ConsumeProduce::OnComplete; report an index past the spot count first.
        if ((int)spotIdx >= spotStates._size || (int)citizenSlot >= citizenRefs._size) {
            std::fprintf(stderr,
                "[WILDWRITE] FishingWork::Slot6 OUT OF RANGE: work=%p entity=%p workId=%d "
                "param4=0x%x -> spotIdx=%u (count %d, cap %d, array %p) citizenSlot=%u (count %d)\n",
                (void*)this, static_cast<void*>(_entity.Get()), _workId, param4,
                spotIdx, spotStates._size, spotStates._capacity, static_cast<void*>(spotStates._data),
                citizenSlot, citizenRefs._size);
            RedzoneAlloc_PrintBacktrace("FishingWork::Slot6");
        }
        spotStates[spotIdx] = static_cast<int32_t>(WorkSpotState::NeedsReset);
    }

    return 0;
}

// ============================================================
// Slot7 (0x1400e9e80)
// Delegates to Slot6 (vtable +0x30) with same parameters.
// The decompiled code copies param2 and param3 then calls
// vtable[+0x30](self, &copy2, &copy3).
// ============================================================
//
// THE TEARDOWN WILD WRITE (docs/history/runtime_port/TEARDOWN_WILD_WRITE.md, closed 2026-09-13).
// Retail 0x1400e9e80 is 11 instructions and never touches R9: the caller
// (CompleteTask::CheckEntity -> 0x1400f8800 TaskReassign) loads R9 = the action's
// packed slot word, and Slot6 reads the SAME R9 — the 4th argument is forwarded by
// register passthrough, which Ghidra renders as a 3-parameter function. This port
// copied the decompile: three parameters, three arguments, and Slot6 read whatever
// GCC had left in R9 (measured: 0xc9309598, the low half of a code address), so
// `spotStates[(R9 >> 4) & 0xf] = 2` and `citizenRefs[R9 & 0xf] = 0` landed up to
// 60 bytes past two 4-entry arrays on every cancelled fishing action. The 4-byte
// `2` that hit a neighbouring boundary tag was the crash in EntityList::Shutdown.
// See [[feedback_ghidra_dropped_args]]: an indirect call's register args are not
// in the decompile; read the disassembly for every slot that forwards.
int FishingWork::OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    // Copy the pointed-to values
    CitizenComponent* copy2 = *param2;
    ComponentEntity* copy3 = *param3;

    // Dispatch slot 6 OnComplete with the packed slot word forwarded; retail
    // returns its result (0 on every path).
    return OnComplete(&copy2, &copy3, static_cast<int>(param4));
}

// ============================================================
// Slot8/CancelAll (0x1400ea830)
// Cancels all active fishing work:
//   - Iterates citizen slots, for each non-null citizen:
//     calls CancelPendingActions on their state machine action list
//   - Then clears the citizen IBP slot
// ============================================================
int FishingWork::Cleanup() {
    if (citizenRefs._size <= 0) return 0;

    int i = 0;
    do {
        if (citizenRefs[i] != nullptr) {
            // CheckPtr on citizen IBP; its CharacterComponent (+0x278)
            CharacterComponent* character = citizenRefs[i]->character.Get();

            // Cancel pending actions on the character's action list (+0x48)
            character->actionStack.Clear();
        }

        i++;
    } while (i < citizenRefs._size);
    return 0;
}

// ============================================================
// Slot10/GetTarget (0x1400ea8b0)
// Returns the target entity's position IBP (entity+0x48) via
// the building entity reference at self+0x10.
// Uses FUN_140002e10 to find PositionComponent on building entity.
// Returns param2 (hidden return pointer pattern).
// ============================================================
Profession** FishingWork::GetTarget(Profession** param2) {
    // CheckPtr on building entity IBP (at self+0x10); its WorkPlaceComponent
    // (FUN_140002e10)
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();

    // CheckPtr on the returned IBP

    // The workplace's profession (+0x48) into the return slot
    *param2 = workPlace->profession;

    return param2;
}


// [11] 0x140029860 (shared by 13 tables): `XOR EAX,EAX; CMP [RCX+0x50],EAX; SETG AL`
// — "is anyone assigned" (+0x50 is the citizen count). The old table pointed at
// Work_Slot11_HasActiveTasks, the same body.
int FishingWork::GetPriority() {
    return citizenRefs._size > 0 ? 1 : 0;
}

// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf fields zeroed. The stamps are the compiler's.
FishingWork::FishingWork()
    : Work(), retryCount(0) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_FishingWork_Vtable` (slot 0 Work_GenericScalarDtor,
// slot 1 null, slot 2 a lazily cached id) and REGISTER_TYPE over FishingWork_Factory /
// _CreateInPlace, both Work_GenericCtor.

// create 0x14002a280: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* FishingWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FishingWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FishingWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* FishingWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FishingWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(FishingWork, "Work")
