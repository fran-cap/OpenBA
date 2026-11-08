// src/game/Work/locationwork.cpp
// Clean C++ reimplementation of LocationWork — all methods.
// Source: Game/Work/LocationWork.cpp (from profile build debug strings)
//
// LocationWork (0x50 bytes) — Location-based work action.
// Inherits: Object::Internal -> Work (0x48) -> LocationWork
// Vtable: 0x1401616a0
//
// Unlike HerbalistWork/ForesterWork which extend AutoPickupWork with a BST,
// LocationWork extends Work directly with a single InternalBasePtr at +0x48.
// This stores a reference to the citizen entity assigned to the work.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/LocationWork.h"
#include "object/TypeInfo.h"
#include "Work/exe_bodies.h"
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_14017214c
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CitizenComponent.h"     // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Component/InteractDescription.h"  // ENDGAME B.3.9 D5: InteractDescription::TypeId()
#include "Component/InteractComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"

// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component exe_bodies.h)
// ============================================================
//   0x140006270 Entity_FindComponent_Storage       ComponentEntity::FindComponent<StorageComponent>
//   0x14012cf10 StorageComponent_QueryCount        StorageComponent::QueryCount (the member)
//   0x140115e60 CitizenAI_State_LongTermPlan       CitizenComponent::LongTermPlan
//   0x140002ed0 Entity_FindComponent_Interact      ComponentEntity::FindComponent<InteractComponent>
//   0x140122be0 FindWalkablePosition               WorkImpl_WalkablePos_122be0
//   0x140122b00 FindWalkablePosition (sub)         WorkImpl_WalkablePosRecursive_122b00
//   0x1400bb490 Pathfinding_WalkTowards ctor       CitizenAI_Pathfinding_WalkTowards_bb490
//               (citizenIBP, priority, position, flag1, fScale, flag2)
//     ⚠ This read 0x1400f8620 until 2026-08-15 — a DIFFERENT function entirely
//     (WorkGrid_TryAssignWorkplace). It never fired before because nothing had ever
//     CONSTRUCTED a LocationWork (see reference_registered_but_never_executed).
//   0x1400f7100 ValidateEntity                     Work::ValidateTask
//   0x1400c5400 CancelAllWork                      WorkBase_TaskListCleanup_c5400
//   0x140002e10 FindWorkPlace                      ComponentEntity::FindComponent<WorkPlaceComponent>
//   0x14017214c g_fUVScale (.rdata, 1.0f)          kRdata_14017214c
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: LocationWork_Ctor
// disassembles to `0x1400efc7d: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// (0x1401612e0, used earlier, is an INTERIOR address of FireWork's vtable.)


// ============================================================
// LocationWork::Serialize (0x1400efba0)
// Pattern: Work_Serialize + Serializer(v0) + InternalBasePtr::Serialize(+0x48)
//   + validate IsDerivedFrom, clear if invalid
// ============================================================
void LocationWork::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create serializer (version=0)
    IO::Serializer serializer(stream, 0);

    // Serialize InternalBasePtr at +0x48 (the member is raw storage, H5: LEFT)
    _assignedCitizen.InternalBasePtr::Serialize(serializer);

    // Validate: if _assignedCitizen is non-null, check IsDerivedFrom
    if (!_assignedCitizen.IsNull()) {
        if (!Object::IsDerivedFromNullSafe(_assignedCitizen, CitizenComponent::TypeId())) {
            _assignedCitizen = nullptr;
        }
    }
}


// ============================================================
// LocationWork::Slot4 (0x1400ef880) — pathfinding/location work logic
//
// Algorithm:
// 1. If already has target (+0x48 != 0), return 1 (already assigned)
// 2. Check citizen's storage — if carrying items, call LongTermPlan
// 3. Find the InteractComponent on the workplace entity
// 4. Get citizen's current position
// 5. Find walkable position near workplace (main + sub-positions)
// 6. If found, create WalkTowards action and assign work
// 7. Store citizen ref at +0x48
// ============================================================
int LocationWork::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    // Step 1: Already has target?
    if (_assignedCitizen.Get() != nullptr) {
        return 1;
    }

    // Step 2: Check citizen's storage — if carrying items, replan
    StorageComponent* storage = (*citizenIBP)->_entity->FindComponent<StorageComponent>();

    int itemCount = storage->QueryCount();
    if (itemCount > 0) {
        (*citizenIBP)->LongTermPlan();
    }

    // Step 3: The InteractComponent of the workplace entity (self+0x10)
    InteractComponent* interact = _entity->FindComponent<InteractComponent>();

    // Step 4: Get citizen's current position: CharacterComponent (+0x278) ->
    // ModelComponent (+0x68) -> the transform's translation
    const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

    float srcX = citizenPos.x;
    float srcY = citizenPos.y;
    float srcZ = citizenPos.z;

    // Step 5: Find walkable positions
    // Its description, as an InteractDescription
    Description* descObj = interact->_description;
    int bestDist = 0x7fffffff;

    // DAT_1401c7938 = InteractDescription (ENDGAME B.3.9 D5: by cell, not the legacy
    // variable name "RadiusDesc").
    bool isDerivedRadius = Object::IsDerivedFromNullSafe(descObj, InteractDescription::TypeId());
    InteractDescription* interactDesc = nullptr;
    if (isDerivedRadius) {
        interactDesc = static_cast<InteractDescription*>(descObj);
    }

    float bestPos[3] = {0.0f, 0.0f, 0.0f};
    float srcPosArr[3] = {srcX, srcY, srcZ};

    // Check main position (the component and the description's use nodes, +0x28)
    uint8_t found = interact->FindWalkablePos(&interactDesc->_useNodes,
        srcPosArr, bestPos, &bestDist);

    // Also check the parts' use locations
    int subCount = interact->useLocations._size;
    if (subCount > 0) {
        Object::InternalPtr<InteractComponent>* subArray = interact->useLocations._data;
        for (int i = 0; i < subCount; i++) {
            uint8_t subFound = subArray[i].Get()->FindWalkablePosRecursive(0, srcPosArr, bestPos, &bestDist);
            found = found | subFound;
        }
    }

    if (!found) {
        return 0;  // No walkable position found
    }

    // Step 6: Create WalkTowards action and assign work
    CitizenComponent* citizenIBPLocal = *citizenIBP;

    // The Ghidra decompilation shows the last two params come from stack;
    // the pattern is: (citizenIBP, priority, position, 0, g_fUVScale, flag&0xffffff00)
    CitizenAI_Pathfinding_WalkTowards_bb490(
        &citizenIBPLocal,
        _workId,                // priority from self+0x20
        bestPos,
        0,                       // flag1
        kRdata_14017214c,            // UV scale constant
        0);                      // flag2 (cleared upper bits)

    int priority = _workId;

    // Get work entity via vtable slot +0x50 (Slot10)
    CitizenComponent* citEntPost = *citizenIBP;

    Profession* workEntity = nullptr;
    GetTarget(&workEntity);

    // Set citizen flags
    citEntPost->statusFlags |= Bits(CitizenStatus::TaskDone);
    citEntPost->lastWorkId = priority;
    citEntPost->lastErrand = CitizenErrand::None;

    // If the work's profession matches the citizen's (+0x1f0) and priority >= 0, set
    // the owned flag
    if (workEntity == citEntPost->profession && priority >= 0) {
        citEntPost->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
    }

    // Store citizen as target at +0x48
    _assignedCitizen = *citizenIBP;

    return 2;
}


// ============================================================
// LocationWork::Slot6 (0x1400ef850) — clear target, return false
// Constructs null InternalBasePtr into +0x48 (a plain null store).
// ============================================================
int LocationWork::OnComplete(CitizenComponent** /*arg1*/, ComponentEntity** /*arg2*/, int /*arg3*/) {
    _assignedCitizen = nullptr;
    return false;
}


// ============================================================
// LocationWork::Slot7 (0x1400ef820) — clear target, void return
// Constructs null InternalBasePtr into +0x48 (a plain null store).
// ============================================================
int LocationWork::OnCancel(CitizenComponent** /*arg1*/, ComponentEntity** /*arg2*/, int /*arg3*/) {
    _assignedCitizen = nullptr;
    return 0;
}


// ============================================================
// LocationWork::Slot8 (0x1400efaf0) — cancel single target's work
// If the target at +0x48 exists, cancels its action list (CharacterComponent +0x48).
// ============================================================
int LocationWork::Cleanup() {
    if (_assignedCitizen.Get() == nullptr) {
        return 0;
    }

    CitizenComponent* targetEntity = _assignedCitizen.Get();
    targetEntity->character->actionStack.Clear();
    return 0;
}


// ============================================================
// LocationWork::Slot9 (0x1400ef7c0) — validate target
// Checks if the single target entity is still valid.
// If invalid, clears +0x48 to null and returns false.
// Returns true if valid (or no target).
// ============================================================
int LocationWork::Validate() {
    CitizenComponent* targetIBP = _assignedCitizen.Get();

    if (targetIBP == nullptr) {
        return true;  // No target — considered valid (nothing to invalidate)
    }

    // Validate the entity
    bool stillValid = ValidateTask(nullptr, &targetIBP, nullptr);

    if (!stillValid) {
        // Clear the target (a null InternalBasePtr)
        _assignedCitizen = nullptr;
        return false;
    }

    return true;
}


// ============================================================
// LocationWork::Slot10 (0x1400efb40) — the entity's (+0x10) WorkPlaceComponent
// (0x140002e10), its profession (+0x48) into the return slot (void* in Work.h's
// slot: LEFT). IDENTICAL to HuntWork/PastureWork Slot10.
// ============================================================
Profession** LocationWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// Slot 5 is Work's (0x1400f7540, `XOR AL,AL; RET`): the old dead
// LocationWork_Execute_Impl copy of it is gone.
// ============================================================
// Slot12 (+0x60) — retail 0x1400efae0, this class's own `RET 0` (every other Work
// table shares 0x140006b10). The same behaviour; declared so the table matches.
void LocationWork::Slot12(int /*version*/) {}


// ============================================================
// LocationWork::Slot11 (0x1400295a0) — checks +0x48 != 0
// Returns true if target entity reference is non-null.
// Too small to hook (10 bytes).
// ============================================================
int LocationWork::GetPriority() {
    return _assignedCitizen.Get() != nullptr ? 1 : 0;
}
// ~LocationWork is defaulted: the LocationWork then Work stamps and `~Internal` are the
// compiler's chain — all retail's 0x14002a850 does before the free.


// Ctor — the bytes the tree's factory built (Work_GenericCtor): the Work base init,
// the leaf field zeroed. The stamps are the compiler's.
LocationWork::LocationWork() : Work(), _assignedCitizen(nullptr) {
}

LocationWork::LocationWork(ComponentEntity* const* pEntityIBP) : Work(pEntityIBP), _assignedCitizen(nullptr) {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_LocationWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id, slot 12 the shared nop) and REGISTER_TYPE over LocationWork_Factory /
// _CreateInPlace (both Work_GenericCtor), plus two vtable-pointer statics for the
// dtor's stamps.

// create 0x14002a810: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* LocationWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LocationWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) LocationWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* LocationWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) LocationWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(LocationWork, "Work")
