// src/game/Work/pickup.cpp
// Clean C++ reimplementation of Pickup -- all methods.
// Source: Game/Work/Pickup.cpp (from profile build debug strings)
//
// Pickup (0x60 bytes) -- Item pickup work action.
// Inherits: Object::Internal -> Work (0x48) -> Pickup
// Vtable: 0x140161810
//
// Pickup differs from most Work classes:
//   - Has its OWN Execute override (re-targets when citizen changes workplace)
//   - Fields: status (+0x48), citizenIBP (+0x50), candidateList (+0x58)
//   - Status values: 0=unassigned, 1=assigned, 2=completing
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/Pickup.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Action/PathSearch.h"   // RawArray (PickBestDropTarget)
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Game/exe_bodies.h"        // B3R: 0x14009f3e0
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/MemoryAllocator.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode (Pickup_FindTypeWork)
#include "Game/ProfessionList.h"
#include "collection/Collection.h"
#include "collection/introsort_msvc.h"   // rtsort::Introsort (0x14002c070) / SelectionSortMaxToHigh (0x14002c380)
#include "Game/Profession.h"   // B8 RT2: ::Object::SerializeExternal<Profession> (the downcast needs it complete)


// ============================================================
// Sub-functions, called by name (B3R): 0x140018020 Reimpl_ExternalPtr_Serialize_StringTable,
// 0x1400f7100 Work::ValidateTask, 0x1400c5400 WorkBase_TaskListCleanup_c5400.
// ============================================================


// ============================================================
// Pickup::Serialize (0x1400f3aa0)
// Pattern: Work_Serialize + Serializer(v2)
//   + Serializer::Serialize(status at +0x48)
//   + InternalBasePtr::Serialize(citizenIBP at +0x50) + IsDerivedFrom validate
//   + if version > 0: the ExternalPtr<Profession> at +0x58 (0x140018020)
// ============================================================
void Pickup::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create serializer (version=2)
    IO::Serializer serializer(stream, 2);

    // Serialize the int status at +0x48
    serializer.Serialize(status, nullptr);

    // Serialize InternalBasePtr at +0x50 (the typed slot serialized through its base)
    citizenIBP.InternalBasePtr::Serialize(serializer);

    // Validate the deserialized ptr against Pickup type
    if (citizenIBP.Get() != nullptr) {
        // DAT_1401c7158 = CitizenComponent (ENDGAME B.3.9 D5: by cell, not the legacy
        // variable name "s_typeId_Pickup" -- this is not Pickup's own cell).
        if (!Object::IsDerivedFromNullSafe(citizenIBP.Get(), CitizenComponent::TypeId())) {
            citizenIBP = nullptr;
        }
    }

    // Version >= 1: serialize the ExternalPtr at +0x58
    int version = serializer.GetVersion();
    if (version > 0) {
        ::Object::SerializeExternal<Profession>(serializer, candidateList);
    }
}


// ============================================================
// Pickup::Execute (vtable +0x20, 0x1400f2f00)
//
// NAMING: Ghidra labels 0x1400f2f00 "Pickup_Serialize". It is NOT Serialize
// (that is 0x1400f3aa0) -- it is the real Execute, vtable slot 4. Verified by
// reading the vtable bytes at 0x140161810: [4]=0x1400f2f00, [5]=0x1400f38f0.
//
// WHAT IT DOES (plain terms): when this Pickup work is still unclaimed
// (status==0), a laborer runs it to claim the pile and start hauling:
//   1. Look up the target pile's RawMaterialComponent (0x1400223c0) and its
//      RawMaterialDescription (IsDerivedFrom vs DAT_1401c7cb8).
//   2. Compute the citizen's free carry capacity: StorageComponent+0x38 minus
//      StorageComponent::QueryCount (0x14012cf10). If one pile's worth
//      (desc+0x48 unitSize * rawmat+0x28 amount) does not fit -> tell the
//      citizen to re-plan (CitizenAI_State_LongTermPlan) and return 2.
//   3. Derive the citizen's cell from its ModelComponent Transform position
//      (model+0x158/+0x15c, i.e. Transform@+0x148 pos@+0x10/+0x14), clamped.
//   4. Seed a candidate array with this Pickup's own target, then sweep the
//      11x11 cell neighbourhood (dy,dx in -5..+5, skipping the centre) around
//      the *pile*, collecting every other reachable pile of the same kind whose
//      per-type Pickup work is not already claimed (vtable slot 11 == false).
//   5. Sort candidates nearest-first (0x14002c070, ascending on the +0x08 distance;
//      Pickup_SortByDistance, retail's sort skeleton -- see the note there).
//   6. For each candidate that still fits in the remaining capacity: convert its
//      access cell to a world position and dispatch PerformWork (0x1400c0ad0) --
//      THIS is what actually makes the citizen walk over and haul the pile --
//      then mark that per-type Pickup work claimed (+0x48=1, +0x50=citizen).
//   7. Stamp the citizen (+0x30 = work type id, +0x54 |= 4, +0xb0 = 0, and
//      +0x88 |= 0x20000 when the work targets the citizen's own workplace).
//
// Returns 0/1/2 as an int (never bool -- trampoline bug class #7): 1 = no work
// available (also the early-out when status != 0), 2 = work dispatched OR the
// citizen is full and must re-plan, 0 = nothing dispatched this tick.
//
// The candidate record is 32 bytes; layout recovered from the stores at
// 0x1400f320c-0x1400f3221 and the reads at 0x1400f3712-0x1400f378a:
//   +0x00 int   cell        (pile cell index)
//   +0x04 int   accessCell  (walkable cell the citizen stands on; 0x14009ccf0)
//   +0x08 int   distance    (0x140008830) <- the sort key used by 0x14002c070
//   +0x0c int   typeId      (resource type id; BST key into the WorkScheduler)
//   +0x10 IBP   entity      (the pile entity)
//   +0x18 IBP   rawMat      (its RawMaterialComponent)
// ============================================================

struct PickupCandidate {
    int32_t cell;        // +0x00
    int32_t accessCell;  // +0x04
    int32_t distance;    // +0x08
    int32_t typeId;      // +0x0c
    ComponentEntity*      entity;   // +0x10
    RawMaterialComponent* rawMat;   // +0x18
};
static_assert(sizeof(PickupCandidate) == 0x20, "PickupCandidate must be 32 bytes");

// The WorkScheduler's per-type work trees (+0x30, 0x10 stride) hold WorkSchedulerIdNode
// (Game/WorkSchedulerNodes.h: key +0x00, work +0x08, left +0x18, right +0x20, read by the
// descent at 0x1400f33b0).

// 0x14002c070 is the shipped MSVC sort skeleton (rtsort::Introsort,
// collection/introsort_msvc.h) instantiated on these 32-byte records, ascending on
// the int32 at +0x08 (distance), with the 32-byte swap inlined; ranges of 8 or
// fewer go to the max-to-high selection sort 0x14002c380 (rtsort::
// SelectionSortMaxToHigh). The sort is unstable and equal-distance piles are
// common, so the swap sequence decides which pile Execute dispatches first:
// it has to be retail's, not std::sort's.
static void Pickup_SortByDistance(PickupCandidate* buf, int count) {
    auto less = [](const PickupCandidate& a, const PickupCandidate& b) { return a.distance < b.distance; };
    auto swp = [](PickupCandidate* a, PickupCandidate* b) {
        const PickupCandidate t = *a;
        *a = *b;
        *b = t;
    };
    rtsort::Introsort(buf, count, less, swp,
                      [&](PickupCandidate* lo, PickupCandidate* hi) {   // 0x14002c380
                          rtsort::SelectionSortMaxToHigh(lo, hi, less, swp);
                      });
}

// ---- sub-callees of the Execute port, called by name (B3R) ----
//   0x1400223c0 ActRm_FindComponent_223c0      0x140006270 WorkBase_FindComponent_6270
//   0x14012cf10 StorageComponent_QueryCount    0x140115e60 CitizenAI_State_LongTermPlan
//   0x14008d3c0 CitizenBehavior::HasStorageFor 0x14009ccf0 WorkGrid_FindCell_9ccf0
//   0x1400a00f0 WorkBase_ValidateCell_a00f0    0x1400f6ef0 WorkImpl_BuildStructureStep_6ef0
//   0x14008d590 Pf_PickBestDropTarget_8d590    0x140008830 WorkBase_CalcDistance_8830
//   0x14012cd80 Pf_StorageCanAccept_cd80       0x140003050 WorkBase_FindComponent_3050
//   0x14009f3e0 Reimpl_MapGetTerrainPos        0x1400c0ad0 WorkAct_PerformWorkCtor9_c0ad0
//   slots 0x140152b60 / 0x140152b68: System::MemoryAllocator::MemAlloc / MemFree
// (the finders are ComponentEntity::FindComponent<T>())

// DAT_140172114 = 0x3f000000 = 0.5f.
static float Pickup_CellCenterHalf() {
    return kRdata_140172114;
}

// Inlined Array<PickupCandidate>::Add. Mirrors the exe growth idiom byte-for-byte
// (0x1400f3545-0x1400f3630): capacity becomes newCount, or newCount*3 when the
// array was already non-empty; on realloc the old elements are copied and the old
// buffer freed; without realloc only the new tail element is written.
static void Pickup_CandAdd(PickupCandidate*& buf, int& count, int& capacity,
                           const PickupCandidate& v) {
    const int oldCount = count;
    const int newCount = oldCount + 1;
    PickupCandidate* oldBuf = buf;
    count = newCount;

    if (newCount > capacity) {
        capacity = newCount;
        if (oldCount != 0) capacity = newCount * 3;
        buf = static_cast<PickupCandidate*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(capacity) * 0x20, 8));
    }
    if (!buf) { buf = oldBuf; count = oldCount; return; }  // alloc failed -> no-op

    for (int i = (oldBuf == buf) ? oldCount : 0; i < newCount; ++i) {
        buf[i] = (i < oldCount && oldBuf) ? oldBuf[i] : v;
    }
    if (oldBuf != buf && oldBuf) System::MemoryAllocator::MemFree(oldBuf);
}

// The per-type Pickup work in the WorkScheduler's first tree (+0x30), by type id
// (the descent retail inlines twice): WorkSchedulerIdNode, key = the type id.
static Work* Pickup_FindTypeWork(WorkScheduler* scheduler, int typeId) {
    const WorkSchedulerIdNode* node = scheduler->trees[0].root;
    while (node != nullptr) {
        if (typeId < node->key) {
            node = node->left;
        } else if (typeId > node->key) {
            node = node->right;
        } else {
            return node->work.Get();
        }
    }
    return nullptr;
}

// ============================================================
// Pickup::Execute [4] 0x1400f2f00. (It used to be a dispatcher that first
// tried `ResolveAddr(Exe, 0x1400f2f00)` -- this very slot, never registered, so
// always null -- and fell back to this body; CALLER_SURFACE section 9 defect 3.)
// Retail reads RCX/RDX/R8 only.
// ============================================================
int Pickup::Execute(CitizenComponent** citizenRef, System::Time* time) {
    // 0x1400f2f37: CMP [RCX+0x48],0 -> anything but "unassigned" means no work.
    if (status != 0) return 1;

    // g_pGameWorldSingleton is STORAGE (B3R): its pointer test stays.
    if (!GameUpdateLoop::_singleton) {
        return 1;
    }

    const uint32_t tidRawMatDesc = RawMaterialDescription::TypeId();
    // 0x1401c6be0 (Pickup's own cell) is UNREGISTERED -- Pickup_Tid's raw-.rdata read
    // always failed and fell through to this same Pickup::TypeId() call, so calling
    // it directly is not a behaviour change (ENDGAME B.3.9 D5 note).
    const uint32_t tidPickup     = Pickup::TypeId();

    // --- pile's RawMaterialComponent + its RawMaterialDescription ---------------
    RawMaterialComponent* rawMatIBP = field_10->FindComponent<RawMaterialComponent>();
    Description* descRaw = rawMatIBP->_description;
    RawMaterialDescription* desc = Object::IsDerivedFromNullSafe(descRaw, tidRawMatDesc)
        ? static_cast<RawMaterialDescription*>(descRaw) : nullptr;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenBehavior* townMgr = gw->m_citizenList;
    Map* map = gw->m_game;
    // The map helpers (0x14009ccf0, 0x1400a00f0, 0x140008830) take the map as int*.
    Map* grid = map;

    // --- citizen's free carry capacity -----------------------------------------
    CitizenComponent* citizen = *citizenRef;
    StorageComponent* storageIBP = citizen->_entity->FindComponent<StorageComponent>();
    int cap = storageIBP->capacity;
    int freeCap = cap - static_cast<int>(storageIBP->QueryCount());

    // desc+0x48 = per-unit size, rawMat+0x28 = amount in the pile. (The original
    // dereferences desc unguarded here; kept faithful -- desc is non-null whenever
    // 0x1400223c0 found a RawMaterialComponent, which is the only way to get here.)
    const int needed = desc->_weight * rawMatIBP->quantity;
    if (freeCap < needed) {
        (*citizenRef)->LongTermPlan();
        return 2;  // citizen is full -> re-plan
    }

    // --- citizen's cell, from its ModelComponent transform ----------------------
    const Math::Vector3& citizenPos = (*citizenRef)->character->modelComponent->transform.translation;

    // Scratch Vector3 shared by 0x14008d590's out-position and the later
    // Vector3 ctor (both write all three floats). The exe pre-seeds only .z from
    // model+0x160 at 0x1400f3070 and never reads it back -- 0x14008d590 writes
    // param_7[0..2] unconditionally at its return. Kept for fidelity; it is dead.
    Math::Vector3 scratch(0.0f, 0.0f, 0.0f);
    scratch.z = citizenPos.z;

    const int dim = map->size;
    int cx = static_cast<int>(citizenPos.x + static_cast<float>(dim >> 1));
    int cz = static_cast<int>(citizenPos.y + static_cast<float>(dim >> 1));
    if (cx < 0)    cx = 0;
    if (cx >= dim) cx = dim - 1;
    if (cz < 0)    cz = 0;
    if (cz >= dim) cz = dim - 1;
    const int citizenCell = cz * dim + cx;

    // Work+0x28 = this work's target cell (32-bit; the decompiler widens it to 64).
    const int selfCell = field_28;
    const int pileCol  = selfCell % dim;
    const int pileRow  = selfCell / dim;

    // --- gate: does the town still want this resource? --------------------------
    int amount = rawMatIBP->quantity;
    if (!townMgr->HasStorageFor(field_10->_description, amount))
        return 1;

    const int selfAccess = grid->FindCell(selfCell, citizenCell, 0xe07);
    if (selfAccess < 0) return 1;
    if (!grid->ValidateCell(0xe1f, static_cast<uint64_t>(static_cast<uint32_t>(selfAccess)),
                          static_cast<uint32_t>(citizenCell)))
        return 1;

    CitizenComponent* assignIBP = *citizenRef;
    if (!static_cast<char>(StepProgress(&assignIBP, static_cast<uint32_t>(selfAccess),
                         static_cast<uint32_t>(citizenCell), time)))   // 0x1400f6ef0; the door read AL
        return 0;

    // --- is there anywhere to drop it off? --------------------------------------
    Object::InternalPtr<ComponentEntity> dropIBP = nullptr;
    ComponentEntity* storeOut = nullptr;
    amount = rawMatIBP->quantity;
    townMgr->PickBestDropTarget(
                       &storeOut, static_cast<uint32_t>(selfAccess),
                       field_10->_description,
                       amount, 1, &scratch.x, &dropIBP);   // 0x14008d590
    if (storeOut == nullptr) return 1;

    // Storage we will haul into -- used to test each candidate for acceptance.
    StorageComponent* dstStorageIBP = storeOut->FindComponent<StorageComponent>();

    // --- seed the candidate array with this Pickup's own target -----------------
    PickupCandidate cand;
    cand.cell       = selfCell;
    cand.accessCell = selfAccess;
    cand.distance   = grid->CalcDistance(static_cast<uint32_t>(citizenCell),
                                   static_cast<uint32_t>(selfCell));
    cand.typeId     = field_20;
    cand.entity     = field_10.Get();
    cand.rawMat     = rawMatIBP;

    int count = 1;
    int capacity = 1;
    PickupCandidate* buf =
        static_cast<PickupCandidate*>(System::MemoryAllocator::MemAlloc(0x20, 8));
    if (!buf) return 1;
    buf[0] = cand;
    // 0x1400f32cd: the exe frees the *previous* (null) buffer here -- a literal
    // MemFree(0) no-op. Mirrored for fidelity.
    System::MemoryAllocator::MemFree(nullptr);

    // --- sweep the 11x11 neighbourhood around the pile --------------------------
    WorkScheduler* scheduler = gw->m_tutorial;  // work/task registry; its per-type trees at +0x30

    int row = pileRow - 5;
    for (int dy = -5; dy <= 5; ++dy, ++row) {
        for (int dx = -5; dx <= 5; ++dx) {
            if (dx == 0 && dy == 0) continue;  // skip the pile's own cell
            const int col = pileCol + dx;
            if (col < 0 || col >= dim) continue;
            if (row < 0 || row >= dim) continue;

            const int nCell = row * dim + col;
            const MapCell& cellPtr = map->cells.GetData()[static_cast<int64_t>(nCell)];

            // Occupant: prefer the blocker (+0x00), else the occupant (+0x08), else null.
            ComponentEntity* occIBP = nullptr;
            if (cellPtr.blocker != nullptr) {
                occIBP = cellPtr.blocker.Get();
            } else if (cellPtr.occupant != nullptr) {
                occIBP = cellPtr.occupant.Get();
            } else {
                occIBP = nullptr;   // ??0InternalBasePtr(nullptr)
            }
            if (occIBP == nullptr) continue;

            RawMaterialComponent* nRawMatIBP = occIBP->FindComponent<RawMaterialComponent>();
            if (nRawMatIBP == nullptr) continue;

            ComponentDescription* nDesc = occIBP->_description;
            if (!static_cast<char>(dstStorageIBP->CanAdd(nDesc, 1))) continue;   // 0x14012cd80; the door read AL

            const int nAccess = grid->FindCell(nCell, citizenCell, 0xe07);
            if (nAccess < 0) continue;
            if (!grid->ValidateCell(0xe1f,
                                  static_cast<uint64_t>(static_cast<uint32_t>(nAccess)),
                                  static_cast<uint32_t>(citizenCell)))
                continue;

            WorkComponent* compIBP = occIBP->FindComponent<WorkComponent>();
            const int nTypeId = compIBP->workId;

            // Walk the type-id-keyed tree for this resource's Pickup work.
            Work* workIBP = Pickup_FindTypeWork(scheduler, nTypeId);

            Work* pickupRaw = workIBP;
            Pickup* pickupIBP = Object::IsDerivedFromNullSafe(pickupRaw, tidPickup)
                ? static_cast<Pickup*>(pickupRaw) : nullptr;
            if (pickupIBP == nullptr) continue;

            // vtable slot 11 (+0x58) = "already has a worker" -> skip this pile.
            // The caller reads AL only (hence the char).
            if (static_cast<char>(pickupIBP->GetPriority())) continue;

            PickupCandidate n;
            n.cell       = nCell;
            n.accessCell = nAccess;
            n.distance   = grid->CalcDistance(static_cast<uint32_t>(citizenCell),
                                        static_cast<uint32_t>(nCell));
            n.typeId     = nTypeId;
            n.entity     = occIBP;
            n.rawMat     = nRawMatIBP;
            Pickup_CandAdd(buf, count, capacity, n);
        }
    }

    // --- nearest-first ----------------------------------------------------------
    if (count > 1) Pickup_SortByDistance(buf, count);

    // --- dispatch ---------------------------------------------------------------
    const float half = Pickup_CellCenterHalf();
    char first = 1;
    int  result = 0;

    for (int i = 0; i < count; ++i) {
        PickupCandidate* p = &buf[i];

        Description* dRaw = p->rawMat->_description;
        RawMaterialDescription* d = Object::IsDerivedFromNullSafe(dRaw, tidRawMatDesc)
            ? static_cast<RawMaterialDescription*>(dRaw) : nullptr;

        const int need = d->_weight * p->rawMat->quantity;
        if (need > freeCap) continue;
        freeCap -= need;

        // Access cell -> world position. off = (dim>>1) - 0.5
        const int gdim = map->size;
        const float off = static_cast<float>(gdim >> 1) - half;
        scratch = Math::Vector3(
                   static_cast<float>(p->accessCell % gdim) - off,
                   static_cast<float>(p->accessCell / gdim) - off,
                   0.0f);

        float worldPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        map->GetTerrainPos(worldPos, &scratch.x, 0, nullptr);

        ComponentEntity* entIBP = p->entity;
        CitizenComponent* citIBP = *citizenRef;
        WorkAct_PerformWorkCtor9_c0ad0(&citIBP, static_cast<uint32_t>(p->typeId), &entIBP,
                        worldPos, p->cell, first);
        first = 0;


        // Mark this resource type's Pickup work as claimed by our citizen.
        Work* wRaw = Pickup_FindTypeWork(scheduler, p->typeId);
        Pickup* claimed = Object::IsDerivedFromNullSafe(wRaw, tidPickup)
            ? static_cast<Pickup*>(wRaw) : nullptr;
        claimed->status = 1;
        claimed->citizenIBP = *citizenRef;
    }

    // --- stamp the citizen ------------------------------------------------------
    const int32_t workTypeId = field_20;
    CitizenComponent* citEntity = *citizenRef;

    // vtable slot 10 (+0x50) GetTarget: member value-return ABI -- RCX=this,
    // RDX=hidden retbuf (the exe reuses its own `self` home slot as the buffer).
    Profession* targetOut = nullptr;
    GetTarget(&targetOut);   // slot 10, virtual in retail

    citEntity->field_0x30 = workTypeId;
    citEntity->statusFlags |= 4;
    citEntity->field_0xB0 = 0;
    if (targetOut == citEntity->profession && workTypeId >= 0) {
        citEntity->happinessFlags |= 0x20000;
    }

    // first still set => nothing was dispatched => 0; else 2.
    if (first == 0) result = 2;
    System::MemoryAllocator::MemFree(buf);
    return result;
}

// ============================================================
// Pickup::Slot5 [5] 0x1400f38f0 (re-target)
// UNIQUE to Pickup -- handles re-targeting when citizen changes
// workplace during an active pickup.
//
// Algorithm:
// 1. If status == 0, return false (no active work)
// 2. If citizenIBP is null, return false
// 3. Check if citizen's current profession matches citizenIBP's
// 4. If different: cancel old work, re-run Slot4 for both entities
// 5. Return false (keep executing)
// ============================================================
int Pickup::Slot5(CitizenComponent** citizenSlot, System::Time* param3) {
    // Check status != 0 and citizenIBP != null
    if (status == 0 || citizenIBP.Get() == nullptr) {
        return 0;
    }

    // The citizen slot's profession (+0x1f0)
    Profession* citizenProfession = (*citizenSlot)->profession;

    CitizenComponent* storedCitizen = citizenIBP.Get();

    if (citizenProfession != storedCitizen->profession) {
        // Citizen changed workplace -- cancel old work and re-target
        CitizenComponent* oldCitizen = citizenIBP.Get();

        oldCitizen->character->actionStack.Clear();

        // Re-run Slot4 with the new citizen
        CitizenComponent* local_buf[1] = { *citizenSlot };

        // 0x1400f3976 CALL 0x1400c5400 is followed directly by the slot-4 dispatch
        // (0x1400f3988 MOV RAX,[RSI]; 0x1400f3991 CALL [RAX+0x20]): no test between.
        // retail 0x1400f3991: CALL [RAX+0x20] with RCX/RDX/R8 only (slot 4
        // reads no R9), a virtual dispatch
        int result = Execute(local_buf, param3);

        if (result == 2) {
            return 1;  // Successfully re-targeted
        }

        // Try again with stored citizen
        CitizenComponent* storedBuf[1] = { storedCitizen };
        Execute(storedBuf, param3);
    }

    return 0;
}


// ============================================================
// Pickup::Slot6 / OnComplete (0x1400f2ec0)
// Resets citizenIBP to null, sets status to 2 (completing).
// Returns false.
// ============================================================
int Pickup::OnComplete(CitizenComponent** /*arg1*/, ComponentEntity** /*arg2*/, int /*arg3*/) {
    citizenIBP = nullptr;   // ??0InternalBasePtr(nullptr)
    status = 2;
    return false;
}


// ============================================================
// Pickup::Slot7 / OnCancel (0x1400f2e80)
// Resets citizenIBP to null, sets status to 0 (unassigned).
// ============================================================
int Pickup::OnCancel(CitizenComponent** /*arg1*/, ComponentEntity** /*arg2*/, int /*arg3*/) {
    citizenIBP = nullptr;   // ??0InternalBasePtr(nullptr)
    status = 0;
    return 0;
}


// ============================================================
// Pickup::Slot8 / CancelAll (0x1400f39f0)
// If status == 1 (assigned), cancels the citizen's active work.
// ============================================================
int Pickup::Cleanup() {
    if (status == 1) {
        CitizenComponent* citizenEntity = citizenIBP.Get();

        citizenEntity->character->actionStack.Clear();
    }
    return 0;
}


// ============================================================
// Pickup::Slot9 / Validate (0x1400f2e20)
// Validates the assigned citizen entity.
// If invalid, resets citizenIBP to null and status to 0.
// Returns true if valid (or no citizen assigned).
// ============================================================
int Pickup::Validate() {
    CitizenComponent* citizenRef = citizenIBP.Get();

    if (citizenRef == nullptr) {
        return true;  // No citizen assigned -- nothing to validate
    }

    // Validate entity via FUN_1400f7100
    CitizenComponent* entityIBP[1] = { citizenRef };
    bool valid = ValidateTask(nullptr, entityIBP, nullptr);

    if (!valid) {
        // Entity no longer valid -- reset
        citizenIBP = nullptr;   // ??0InternalBasePtr(nullptr)
        status = 0;
        return false;
    }

    return true;
}


// ============================================================
// Pickup::Slot10 / GetTarget (0x1400f3a50)
// The ProfessionList's +0x30 profession (GameWorld+0xb0).
// This is a global target, not per-instance.
// ============================================================
Profession** Pickup::GetTarget(Profession** outPtr) {
    *outPtr = GameUpdateLoop::_singleton->m_professionList->field_0x30;
    return outPtr;
}


// ============================================================
// Pickup::Slot11 / HasWorker (0x14002c000)
// Returns true if citizenIBP is non-null (worker assigned).
// 10 bytes, unhookable.
// ============================================================
int Pickup::GetPriority() {
    return citizenIBP.Get() != nullptr ? 1 : 0;
}

// ============================================================
// Pickup::Slot12 (0x1400f3a70, 40 B) -- the WorkScheduler's post-load pass
// (goals_serialize.cpp, the call at 0x1400f9340) calls it for every filed work item. Pickup's
// body files the item under the profession GetTarget answers (ProfessionList +0x30):
//   0x1400f3a73  RCX = [0x1401c3dd8] (the game world), R9 = [RCX+0xb0] (ProfessionList)
//   0x1400f3a81  R8  = this+0x58 (the profession the item was saved under)
//   0x1400f3a85  EDX = this+0x20 (the work id; the version argument is overwritten)
//   0x1400f3a88  R9  = ProfessionList +0x30
//   0x1400f3a8c  RCX = world +0x118 (the WorkScheduler); JMP 0x1400f8400 Rekey
// No test of the world, the list or the scheduler: retail has none. +0x58 keeps the old
// value (retail does not write it).
// ============================================================
void Pickup::Slot12(int /*version*/) {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    world->m_tutorial->Rekey(static_cast<uint32_t>(field_20), candidateList,
                             world->m_professionList->field_0x30);   // 0x1400f8400
}











// ~Pickup is defaulted: the Pickup then Work stamps and `~Internal` are the compiler's
// chain — all retail's 0x14002c010 does before the free.



// Ctor 0x1400f3c00: `??0Internal`, the Work base fields, the leaf fields zeroed (the
// two stamps are the compiler's).
Pickup::Pickup() : Work(), status(0), citizenIBP(nullptr), candidateList(nullptr) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_Pickup_Vtable` (slot 1 null, slot 2 a lazily cached
// id, slot 12 Work's nop -- Pickup's own 0x1400f3a70 is now Pickup::Slot12), Pickup_Ctor_Impl (the
// ctor with both stamps written by hand) and REGISTER_TYPE over Pickup_Factory /
// _CreateInPlace, plus two vtable-pointer statics for the dtor's stamps.

// create 0x14002bfc0: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* Pickup::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Pickup), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) Pickup());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* Pickup::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) Pickup());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(Pickup, "Work")
