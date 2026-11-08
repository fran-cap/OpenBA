// src/game/Component/workcomponent.cpp
// Clean reimplementation of WorkComponent lifecycle methods from Application-x64.exe.
//
// WorkComponent (0x38 bytes) is a direct Component subclass managing work ID,
// flags, and state fields. Three non-trivial vtable overrides:
//   Shutdown  (0x14012e710) — versioned serialization of workId, workState, workFlags
//   Draw      (0x14012e6f0) — compute work capacity and store at +0x30
//   OnCreate  (0x14012e6c0) — check if work slot available, register if so
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/WorkComponent.h"
#include <cstdint>
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/WorkDescription.h"   // ENDGAME B.3.9 TR
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Tutorial/exe_bodies.h"   // (WorkComponent_CalcMaxWorkers went in B6: CalcMaxWorkers is the member below)
#include "Action/exe_bodies.h"     // ActCt_TaskCleanup_8e30 (0x1400f8e30)
#include "Work/exe_bodies.h"
#include "Entity/ComponentEntity.h"            // ComponentEntity::FindComponent<T>
#include "Component/MapComponent.h"            // 0x140004680's class
#include "Component/StatusIconComponent.h"     // 0x140002f90's class
#include "Game/WorkScheduler.h"                // GameUpdateLoop::m_tutorial's class
#include "Game/WorkSchedulerNodes.h"           // WorkSchedulerIdNode (the goals tree's node)
#include "Work/Work.h"                         // Work::_queueOrder

// ============================================================
// Helpers
// ============================================================
// FUN_14012e5c0 — compute work capacity for this component. TWO bodies exist for
//   it: WorkComponent_CalcMaxWorkers (registered, what this file's door resolved)
//   and fn_StorageCountQuery at the bottom of this file (its own readers). Each
//   reader keeps the body it reached.
// FUN_14012ead0 WorkComponent_CheckWorkSlot / FUN_14012eb50
//   WorkComponent_RegisterWorkSlot — defined below.



// ============================================================
// Reimpl helpers for the work-slot BST functions
// ============================================================
// 0x1400f8e30 ActCt_TaskCleanup_8e30(tree, workId) — notify AI of slot change
// 0x140002f90 WorkBase_FindComponent_2f90 — Entity_FindComponent_MeshInstance
// 0x14012c3f0 CitizenComp_FUN_14012c3f0(comp, idx, char) — set mesh flag

// ============================================================
// Reimpl of FUN_14012ead0 (check whether this work slot is occupied:
//   BST lookup keyed by workId (+0x28) in the world work-slot tree)
// ============================================================
// Tree at *(g_pGameWorldSingleton+0x118) + 0x30. Node: key @+0x00, value IBP @+0x08,
// parent @+0x10, **LESSER child @+0x18, GREATER child @+0x20**, balance @+0x28 (0x30 bytes).
//
// The two child labels above were SWAPPED in this comment until Wave 9 lane N. The CODE
// was and is correct — it descends to +0x20 when the node's key is below the target — but
// the label was the wrong way round, and every other walker of this same node layout
// (CitizenAI_WorkScheduler_Rekey and _Insert in citizenai_work_scheduler.cpp,
// ActCt_TaskCleanup_8e30 steps 1/2/4 in action_base_complete.cpp,
// WorkComponent_RegisterWorkSlot just below) documents it correctly as lesser@+0x18.
// Left uncorrected in a family that has now paid THREE TIMES for a child-offset or
// slot mix-up (reference_workarea_bst_bad_entry.md), a comment that contradicts its own
// code and its four siblings is a defect waiting for the next reader.
// The goals tree is WorkScheduler::trees[0] (m_tutorial +0x30); its nodes are
// WorkSchedulerIdNode (Game/WorkSchedulerNodes.h).
bool WorkComponent::IsWorkPosted() const {
    if (GameUpdateLoop::_singleton == nullptr) return false;
    int key = workId;
    WorkSchedulerIdNode* node = GameUpdateLoop::_singleton->m_tutorial->trees[0].root;
    while (node != nullptr) {
        if (node->key <= key) {
            if (key <= node->key) {
                // equal — found; the value is the node's InternalPtr<Work> (+0x08)
                return node->work.Get() != nullptr;
            }
            node = node->right;   // +0x20: the node's key is below the target
        } else {
            node = node->left;    // +0x18
        }
    }
    // not found: construct an empty IBP and report non-null (matches original)
    int64_t ibp[1] = {0};
    new (ibp) ::Object::InternalBasePtr(nullptr);
    return ibp[0] != 0;
}

// ============================================================
// Reimpl of FUN_14012eb50 (register/refresh this work slot:
//   when occupied, sync worker id, poke the AI tree, refresh mesh flags 2 & 3)
// ============================================================
void WorkComponent::CancelWork() {
    if (!IsWorkPosted()) return;
    if (GameUpdateLoop::_singleton == nullptr) return;

    int key = workId;
    Work* value = nullptr;
    WorkSchedulerIdNode* node = GameUpdateLoop::_singleton->m_tutorial->trees[0].root;
    bool found = false;
    while (node != nullptr) {
        if (key < node->key) {
            node = node->left;    // +0x18
        } else if (key <= node->key) {
            value = node->work.Get();   // +0x08
            found = true;
            break;
        } else {
            node = node->right;   // +0x20
        }
    }
    if (!found) {
        new (&value) ::Object::InternalBasePtr(nullptr);
    }

    workFlags = value->_queueOrder;   // the work's +0x24 (the queue-order key)

    GameUpdateLoop::_singleton->m_tutorial->TaskCleanup(key);   // 0x1400f8e30

    // 0x140002f90 FindComponent<StatusIconComponent>; the null test is the tree's.
    if (StatusIconComponent* icon = _entity->FindComponent<StatusIconComponent>()) {
        icon->SetFlag(2, '\0');   // 0x14012c3f0
        icon->SetFlag(3, '\0');
    }
}


// ============================================================================
// ENDGAME B.3.8 VIS — WorkComponent as a REAL class (WorkComponent.h).
//
// Before: a 48-slot WorkComponent_LiveVtable filled per create, Inert_ScalarDtor in
// slot 0 (only ~Internal), the ctor values patched in after the zero-fill.
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"

// 0x14012e820. These seeds are load-bearing for the slot-7 hook: workId 0 is a
// VALID work id, so an unseeded component would ask the goals tree to cancel
// work 0 — someone else's task — instead of no-oping. -1 is retail's "I have
// posted no work" sentinel, and 0x80000000 is the same sentinel for workFlags
// (PostWork 0x14012ec60 tests `*(int*)(self+0x2c) == -0x80000000`).
WorkComponent::WorkComponent()
    : workId(-1), workFlags(kWorkFlagsNone), workState(0), _pad34{} {
    OrFlagsInCtor(0x3);
}

// ============================================================
// WorkComponent::Persist (vtable slot [3], 0x14012e710)
// ============================================================
// Versioned serialization:
//   - Always: serialize workId (+0x28)
//   - Version < 3: serialize a legacy bool (discarded)
//   - Version >= 1: serialize workState (+0x30)
//   - Version >= 2: serialize workFlags (+0x2c)

void WorkComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 3);

    // Always serialize workId
    ser.Serialize(workId, nullptr);

    // Legacy: version < 3 had a bool field (no longer used).
    // ⚠ It is a BOOL, so it must go through the bool overload
    // (?Serialize@Serializer@IO@@QEAAXAEA_NPEBD@Z), which moves ONE byte.
    // Casting a bool* to int* and using the int overload reads FOUR, and the
    // extra 3 bytes desync every object serialized after this component.
    // Measured on the packaged tutorial save (which is version < 3, so this
    // branch is live there): our reader ended 3 bytes PAST the start of the
    // next record. Our own saves hid it because the save leg wrote the same 4
    // bytes it read back -- only a retail-authored save can catch this.
    int version = ser.GetVersion();
    if (version < 3) {
        bool legacyBool = false;
        ser.Serialize(legacyBool, nullptr);
    }

    // Version >= 1: serialize workState
    version = ser.GetVersion();
    if (version >= 1) {
        ser.Serialize(workState, nullptr);
    }

    // Version >= 2: workFlags through Serialize(int&) on the field itself
    // (0x14012e7ab LEA RDX,[RDI+0x2c]; CALL [0x140152ac8]; B6 row 19).
    version = ser.GetVersion();
    if (version >= 2) {
        ser.Serialize(workFlags, nullptr);
    }
}

// [6] 0x14012e6f0 (Ghidra "Draw"): `CALL 0x14012e5c0; MOV [RBX+0x30],EAX` — the
// starting worker count is computed when the entity is added. The tree's table never
// installed this body (it lived as the uncalled WorkComponent_Draw) and every reader
// computed +0x30 lazily on a 0.
void WorkComponent::OnAdded() {
    workState = CalcMaxWorkers();   // 0x14012e5c0
}

// ============================================================
// WorkComponent slot [7] = +0x38, 0x14012e6c0 — CANCEL MY WORK ON REMOVAL
// ============================================================
// Ghidra names this "OnCreate"; it is the REMOVE hook (the documented
// OnCreate/OnRemove off-by-one). ComponentEntity::MarkForRemoval
// (0x1400c2ea0) broadcasts vtable +0x38 to every component whose +0x20
// flags have bit 1 — retail's ctor sets +0x20 |= 3, so WorkComponent is in
// that broadcast — and it fires BEFORE the 0x400 removal bit, i.e. while the
// entity is still fully alive.
//
// Retail body: `if (ead0(self)) eb50(self)` — "if the work I posted is still
// in the goals tree, cancel it", and eb50 is what calls Goals::RemoveWork
// (0x1400f8e30). This is the ONLY route by which a raw-material pile's Pickup
// leaves the goals trees: Pickup::OnComplete (0x1400f2ec0) returns false in
// retail too, so CompleteTask never removes it. A pile is created by
// RawMaterialComponent (0x1401291d0, which posts the Pickup through this
// component) and dies on an expiry timer in RawMaterialComponent::Update.
//
// UNWIRED until 2026-08-29: the body was ported but nothing installed it in
// slot 7, so EVERY Pickup ever posted stayed in the goals trees with a freed
// target entity. The next save walked one and faulted in Work::Serialize
// (0x1400f7400) dereferencing the dead target — measured as work ids 3..22,
// one per haul, zero exceptions. See [[reference_ported_registered_but_unwired]].

void WorkComponent::OnRemove() {
    bool available = IsWorkPosted();   // 0x14012ead0
    if (available) {
        CancelWork();                  // 0x14012eb50
    }
}

// [13] 0x14012e6b0 / [14] 0x14012e6a0 — the class's own trivial bodies.
uint32_t WorkComponent::CommandMask(int) { return 0; }
void WorkComponent::Command(int) {}

// create 0x140039e70: MemAlloc(0x38, 8), then the ctor.
::Object::Base* WorkComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(WorkComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) WorkComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* WorkComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) WorkComponent());
}

DEFINE_EXE_TYPE(WorkComponent, "Component")

// 0x14012e5c0 — the starting worker count from the WorkDescription (+0x18) and the
// footprint (0x140004680 FindComponent<MapComponent>). Read whole (B6 WORK, D12): the
// description is IsDerivedFrom ? desc : null (0x14012e5e9 CMOVNZ) and +0x34 is read with
// no test; the MapComponent is read with no test; a result above _defaultWorkers returns
// _defaultWorkers as is (0x14012e668 CMP / JG 0x14012e67b), only a result at or below it
// is clamped to >= 1 (0x14012e66d-0x14012e677).
int WorkComponent::CalcMaxWorkers() const {
    Description* base = _description;
    WorkDescription* jobType = base->IsDerivedFrom(WorkDescription::TypeId())
        ? static_cast<WorkDescription*>(base) : nullptr;
    if (!jobType->_scaleWorkersWithSize) {                        // _scaleWorkersWithSize
        return jobType->_defaultWorkers;                    // _defaultWorkers
    }
    MapComponent* build = _entity->FindComponent<MapComponent>();
    int defaultWorkers = jobType->_defaultWorkers;   // _defaultWorkers (NOT _maxWorkerCount)
    int maxArea        = jobType->_maxArea;   // _maxArea (NOT the building's tile area)
    int width      = static_cast<int16_t>(build->tileX1);
    int height     = static_cast<int16_t>(build->tileY1);
    int result = (int)(((float)(width * height) / (float)maxArea) * (float)defaultWorkers + 1.0f);
    if (result > defaultWorkers) return defaultWorkers;
    if (result < 1)              result = 1;
    return result;
}

